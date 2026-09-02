/***************************************************************************
 * DSD64 decoder for DSF and uncompressed stereo DSDIFF.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2.
 ****************************************************************************/
#include "codeclib.h"

CODEC_HEADER

#define DSD_RATE 2822400
#define PCM_RATE 44100
#define DSF_BLOCK 4096
#define PCM_FRAMES 512

static int32_t pcm[PCM_FRAMES * 2] IBSS_ATTR;
static unsigned char left_block[DSF_BLOCK];
static unsigned char right_block[DSF_BLOCK];

static uint32_t le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const unsigned char *p)
{
    return le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static uint64_t be64(const unsigned char *p)
{
    uint64_t hi = ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    uint64_t lo = ((uint32_t)p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    return (hi << 32) | lo;
}

static unsigned popcount8(unsigned v)
{
    v = v - ((v >> 1) & 0x55);
    v = (v & 0x33) + ((v >> 2) & 0x33);
    return (v + (v >> 4)) & 0x0f;
}

static int32_t decimate64(const unsigned char *p)
{
    unsigned ones = 0;
    for (int i = 0; i < 8; ++i)
        ones += popcount8(p[i]);
    return ((int32_t)ones - 32) << 23;
}

static bool read_exact(void *dst, size_t size)
{
    return ci->read_filebuf(dst, size) == size;
}

static bool parse_dsf(uint64_t *data_size, uint64_t *sample_count)
{
    unsigned char h[92];
    if (!ci->seek_buffer(0) || !read_exact(h, sizeof(h)) ||
        ci->memcmp(h, "DSD ", 4) || ci->memcmp(h + 28, "fmt ", 4) ||
        le32(h + 52) != 2 || le32(h + 56) != DSD_RATE ||
        (le32(h + 60) != 1 && le32(h + 60) != 8) ||
        le32(h + 72) != DSF_BLOCK ||
        ci->memcmp(h + 80, "data", 4))
        return false;
    uint64_t chunk_size = le64(h + 84);
    if (chunk_size < 12)
        return false;
    *data_size = chunk_size - 12;
    *sample_count = le64(h + 64);
    return true;
}

static enum codec_status decode_dsf(void)
{
    uint64_t remaining;
    uint64_t sample_count;
    if (!parse_dsf(&remaining, &sample_count))
        return CODEC_ERROR;

    uint64_t total = remaining;
    uint64_t total_frames = sample_count / 64;
    uint64_t frames_done = 0;
    while (remaining >= DSF_BLOCK * 2 && frames_done < total_frames)
    {
        intptr_t param;
        long action = ci->get_command(&param);
        if (action == CODEC_ACTION_HALT)
            break;
        if (action == CODEC_ACTION_SEEK_TIME)
        {
            uint64_t target_frame = MIN((uint64_t)param * PCM_RATE / 1000,
                                        total_frames);
            uint64_t block = target_frame / PCM_FRAMES;
            if (!ci->seek_buffer(92 + block * DSF_BLOCK * 2))
                return CODEC_ERROR;
            frames_done = block * PCM_FRAMES;
            remaining = total - MIN(total, block * DSF_BLOCK * 2);
            ci->seek_complete();
        }
        if (!read_exact(left_block, DSF_BLOCK) || !read_exact(right_block, DSF_BLOCK))
            break;
        int frames = MIN((uint64_t)PCM_FRAMES, total_frames - frames_done);
        for (int i = 0; i < frames; ++i)
        {
            pcm[i * 2] = decimate64(left_block + i * 8);
            pcm[i * 2 + 1] = decimate64(right_block + i * 8);
        }
        ci->pcmbuf_insert(pcm, NULL, frames);
        frames_done += frames;
        remaining -= DSF_BLOCK * 2;
        ci->set_elapsed(frames_done * 1000 / PCM_RATE);
    }
    return CODEC_OK;
}

static enum codec_status decode_dff(void)
{
    unsigned char head[16];
    unsigned char chunk[12];
    uint64_t data_size = 0;
    uint64_t total_size = 0;
    size_t data_start = 0;

    if (!ci->seek_buffer(0) || !read_exact(head, sizeof(head)) ||
        ci->memcmp(head, "FRM8", 4) || ci->memcmp(head + 12, "DSD ", 4))
        return CODEC_ERROR;

    while (read_exact(chunk, sizeof(chunk)))
    {
        uint64_t size = be64(chunk + 4);
        if (!ci->memcmp(chunk, "PROP", 4) && size >= 4)
        {
            unsigned char type[4];
            if (!read_exact(type, 4) || ci->memcmp(type, "SND ", 4))
                return CODEC_ERROR;
            continue;
        }
        else if (!ci->memcmp(chunk, "DSD ", 4))
        {
            data_size = size;
            total_size = size;
            data_start = ci->curpos;
            break;
        }
        if (!ci->memcmp(chunk, "DST ", 4) || !ci->seek_buffer(ci->curpos + size + (size & 1)))
            return CODEC_ERROR;
    }
    if (!data_size)
        return CODEC_ERROR;

    uint64_t frames_done = 0;
    unsigned char packed[16];
    while (data_size >= sizeof(packed))
    {
        intptr_t param;
        long action = ci->get_command(&param);
        if (action == CODEC_ACTION_HALT)
            break;
        if (action == CODEC_ACTION_SEEK_TIME)
        {
            uint64_t frame = (uint64_t)param * PCM_RATE / 1000;
            uint64_t offset = MIN(total_size, frame * 16);
            offset -= offset % 16;
            if (!ci->seek_buffer(data_start + offset))
                return CODEC_ERROR;
            data_size = total_size - offset;
            frames_done = offset / 16;
            ci->seek_complete();
        }
        int frames = 0;
        while (frames < PCM_FRAMES && data_size >= sizeof(packed) &&
               read_exact(packed, sizeof(packed)))
        {
            for (int i = 0; i < 8; ++i)
            {
                left_block[i] = packed[i * 2];
                right_block[i] = packed[i * 2 + 1];
            }
            pcm[frames * 2] = decimate64(left_block);
            pcm[frames * 2 + 1] = decimate64(right_block);
            ++frames;
            data_size -= sizeof(packed);
        }
        ci->pcmbuf_insert(pcm, NULL, frames);
        frames_done += frames;
        ci->set_elapsed(frames_done * 1000 / PCM_RATE);
    }
    return CODEC_OK;
}

enum codec_status codec_main(enum codec_entry_call_reason reason)
{
    if (reason == CODEC_LOAD)
        ci->configure(DSP_SET_SAMPLE_DEPTH, PCM_OUTPUT_DEPTH - 1);
    return CODEC_OK;
}

enum codec_status codec_run(void)
{
    unsigned char magic[16];
    if (codec_init())
        return CODEC_ERROR;
    codec_set_replaygain(ci->id3);
    ci->configure(DSP_SET_FREQUENCY, PCM_RATE);
    ci->configure(DSP_SET_STEREO_MODE, STEREO_INTERLEAVED);
    if (!ci->seek_buffer(0) || !read_exact(magic, sizeof(magic)))
        return CODEC_ERROR;
    if (!ci->memcmp(magic, "DSD ", 4))
        return decode_dsf();
    if (!ci->memcmp(magic, "FRM8", 4))
        return decode_dff();
    return CODEC_ERROR;
}


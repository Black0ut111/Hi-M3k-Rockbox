/***************************************************************************
 * DSD stream metadata for DSF and DSDIFF.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2.
 ****************************************************************************/
#include <string.h>
#include "metadata.h"
#include "metadata_parsers.h"

static uint32_t le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const unsigned char *p)
{
    return le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint64_t be64(const unsigned char *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static void set_common(struct mp3entry *id3, uint32_t rate,
                       uint64_t samples, uint64_t data_size)
{
    id3->frequency = rate / 64;
    id3->samples = samples / 64;
    id3->length = rate ? (unsigned long)(samples * 1000 / rate) : 0;
    id3->filesize = data_size;
    id3->bitrate = rate ? (rate * 2) / 1000 : 5644;
    id3->vbr = false;
}

static bool parse_dsf(int fd, struct mp3entry *id3, const unsigned char *head)
{
    unsigned char fmt[52];
    uint64_t metadata_offset = le64(head + 20);

    if (lseek(fd, 28, SEEK_SET) < 0 || read(fd, fmt, sizeof(fmt)) != sizeof(fmt) ||
        memcmp(fmt, "fmt ", 4) || le64(fmt + 4) < sizeof(fmt) ||
        le32(fmt + 24) != 2 || le32(fmt + 28) != 2822400 ||
        (le32(fmt + 32) != 1 && le32(fmt + 32) != 8) ||
        le32(fmt + 44) != 4096)
        return false;

    set_common(id3, le32(fmt + 28), le64(fmt + 36), le64(head + 12));
    id3->first_frame_offset = 92;

    if (metadata_offset && metadata_offset + 10 < (uint64_t)filesize(fd) &&
        lseek(fd, metadata_offset, SEEK_SET) >= 0)
    {
        unsigned char tag[10];
        if (read(fd, tag, sizeof(tag)) == sizeof(tag) && !memcmp(tag, "ID3", 3))
        {
            id3->id3v2len = 10 + ((tag[6] & 0x7f) << 21) +
                ((tag[7] & 0x7f) << 14) + ((tag[8] & 0x7f) << 7) +
                (tag[9] & 0x7f);
            lseek(fd, metadata_offset, SEEK_SET);
            setid3v2title(fd, id3);
        }
    }
    return true;
}

static bool parse_dff(int fd, struct mp3entry *id3)
{
    unsigned char chunk[12];
    uint32_t rate = 0;
    uint32_t channels = 0;
    uint64_t dsd_size = 0;
    off_t end = filesize(fd);

    if (lseek(fd, 16, SEEK_SET) < 0)
        return false;

    while (lseek(fd, 0, SEEK_CUR) + 12 <= end)
    {
        if (read(fd, chunk, sizeof(chunk)) != sizeof(chunk))
            break;
        uint64_t size = be64(chunk + 4);
        off_t data = lseek(fd, 0, SEEK_CUR);
        if (!memcmp(chunk, "PROP", 4) && size >= 4)
        {
            unsigned char type[4];
            if (read(fd, type, 4) != 4 || memcmp(type, "SND ", 4))
                return false;
            continue;
        }
        else if (!memcmp(chunk, "FS  ", 4) && size >= 4)
        {
            unsigned char value[4];
            if (read(fd, value, 4) != 4)
                return false;
            rate = be32(value);
        }
        else if (!memcmp(chunk, "CHNL", 4) && size >= 2)
        {
            unsigned char value[2];
            if (read(fd, value, 2) != 2)
                return false;
            channels = (value[0] << 8) | value[1];
        }
        else if (!memcmp(chunk, "DSD ", 4))
        {
            dsd_size = size;
            id3->first_frame_offset = data;
            break;
        }
        else if (!memcmp(chunk, "DST ", 4))
            return false;

        if (lseek(fd, data + size + (size & 1), SEEK_SET) < 0)
            return false;
    }

    if (rate != 2822400 || channels != 2 || !dsd_size)
        return false;
    set_common(id3, rate, dsd_size * 4, dsd_size);
    return true;
}

bool get_dsd_metadata(int fd, struct mp3entry *id3)
{
    unsigned char head[28];
    if (lseek(fd, 0, SEEK_SET) < 0 || read(fd, head, sizeof(head)) != sizeof(head))
        return false;
    if (!memcmp(head, "DSD ", 4))
        return parse_dsf(fd, id3, head);
    if (!memcmp(head, "FRM8", 4) && !memcmp(head + 12, "DSD ", 4))
        return parse_dff(fd, id3);
    return false;
}


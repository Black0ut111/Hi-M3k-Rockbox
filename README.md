# Rockpod for FiiO M3K

A clean-room port of the portable Rockpod user-interface features to the native
Rockbox target for the FiiO M3K.

## Status

Early bring-up. The first milestone builds an unmodified upstream Rockbox image
for `fiiom3k` in GitHub Actions. Rockpod-specific UI patches will be added only
after that baseline build is reproducible.

## Scope

Planned:

- Dynamic colours derived from album art
- Portrait-friendly Cover Flow / PictureFlow for the M3K's 240×320 display
- Rockpod-style theme and rendering improvements
- Standard native Rockbox features for FiiO M3K

Not portable from the iPod implementation:

- Apple MFi / iAP digital audio
- iPod dock USB implementation
- iFlash / ATA and iPod-specific power-management code

## Safety

Test packages replace only the `.rockbox` directory on the microSD card.
They do not replace the already-installed native M3K bootloader. Always keep a
known-good backup of your current `.rockbox` directory.

## License

The port is based on Rockbox and Rockpod, both distributed under GPL-2.0.

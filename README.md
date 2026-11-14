# Duet

An open-source, 3D-printed, Kindle-sized **e-ink reader and MP3 player** built on a
Soldered Inkplate 6 and an Adafruit HUZZAH32, with a headphone jack and Bluetooth.

![Duet logo](duet/brand/duet-logo.png)

- **[Build guide](duet/README.md)**: parts, printing, assembly, firmware, controls
- `duet/firmware/`: the reader (Inkplate 6) and audio board (HUZZAH32) sketches
- `duet/case/`: the parametric OpenSCAD case, STLs and previews
- `duet/brand/`: the logo
- `Duet Media Player.3mf`, `Duet.sketch`: print project and design source

This is a work in progress: the reader firmware runs on hardware; the audio board
firmware and the latest case revision have not been tested on hardware yet.

Licensed under the [GNU GPL v3.0](LICENSE). Bundled third-party files (fonts, the
miniz inflate code) keep their own licences, noted in the build guide.

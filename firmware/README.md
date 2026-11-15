# Firmware

Two Arduino sketches, one per board. They talk over I2C (Qwiic cable, address 0x42)
using the protocol in `duet_link.h`.

| Folder | Board | Does |
|---|---|---|
| `duet-reader/` | Soldered Inkplate 6 | Screen, books, page keys, music browser |
| `duet-audio/` | Adafruit HUZZAH32 Feather | MP3 decoding, headphone jack, Bluetooth |
| `shared/` | — | Master copy of `duet_link.h` |

## The protocol header

Arduino only compiles files inside a sketch's own folder, so each sketch keeps its own
copy of `duet_link.h`. **Edit `shared/duet_link.h` only**, then run:

```
tools/sync-link.sh
```

A GitHub check fails if the copies ever differ. Flash **both** boards whenever the
protocol changes.

## Reader (Inkplate 6): `duet-reader/`

- Board package: *Inkplate Boards* → **Soldered Inkplate6**
- Library: **InkplateLibrary**
- `ROTATION` is 1 to match the board's orientation in the Duet case (confirmed on
  hardware). Use 3 if you ever mount it the other way up.
- **Splash:** on power-on the reader shows the Duet logo in 8-level grey
  (`duet_logo.h`, one full refresh), then switches to fast black-and-white. Waking from
  sleep skips it. To change the logo, regenerate `duet_logo.h` from
  `brand/duet-logo.svg` at 180 px wide.
- `src/epub/` reads EPUB files; `src/inflate/` is the miniz inflate code it uses (MIT).
- `Fonts/` are GNU FreeFont headers from the Inkplate library examples.

## Audio (HUZZAH32): `duet-audio/`

- Board: **Adafruit ESP32 Feather** (Espressif esp32 core)
- Partition scheme: **Huge APP (3MB No OTA)**. Bluetooth + MP3 need the space.
- Libraries: [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools),
  [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix),
  [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)
- Flash it through the side USB slot with the audio switch **off** (so its charger and
  the Inkplate's aren't both on the battery).

## Versioning

`DUET_VERSION` in `duet-reader.ino` is the firmware version shown in Settings. Bump it
when you tag a release and add an entry to [CHANGELOG.md](../CHANGELOG.md).

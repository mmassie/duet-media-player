# What's tested and what isn't

Status as of v0.5. Update this page whenever something is confirmed (or broken) on
real hardware.

## Case

- Board holes and connector positions come from Adafruit's published Eagle files
  (HUZZAH32, UDA1334A, #4682) and Soldered's Inkplate 6 KiCad file (v1.2.1).
- Every part was checked in OpenSCAD for collisions against the Inkplate's back-side
  components (`hardware/case/inkplate_parts.scad`), the battery, the strap, the boards
  and each other, including the screw heads under the face-down boards. Component
  **heights** in that file are conservative estimates, not measurements.
- All four STLs are watertight.
- The key and switch fit is checked in software against a model of a 6 × 6 × 5 mm
  through-hole switch: each switch drops into its seat without touching anything, each
  side key can be lowered in and pushed out through its hole, every key presses
  0.25 mm with 0.3 mm of free play, and nothing reaches the Inkplate's parts. Real
  switches vary, so print `duet-key_test.stl` first.
- **Not yet printed:** the flared Inkplate posts (the v0.5 posts snapped at the back), the reworked key pockets and chin seats (v0.5 prints didn't fit),
  the top tongue-and-groove joint, the recessed key icons, and the screen window moved
  5 mm toward the top.

## Reader firmware

- Runs on real hardware.
- The EPUB converter was tested natively on three full Standard Ebooks titles (Moby
  Dick, Pride and Prejudice, The Time Machine): output matches an independent
  extraction word for word (367,828 words), across zip tools and compression levels,
  plus a hand-made EPUB of awkward cases.
- The page layout was run over every page of those books (≈2,000 pages): no lost or
  repeated words, nothing past the margins, every chapter on a new page.

## Audio firmware

- **Has not run on hardware yet**, and neither v0.5 sketch has been compiled with the
  ESP32 toolchain. The library calls were checked against the libraries' own source
  (arduino-audio-tools, ESP32-A2DP, the ESP32 core's I2C slave driver, the Inkplate
  library), and both sketches run in a desktop simulator with stand-ins for those
  libraries.
- Expect to tune two things on real hardware: the pairing distance (`PAIR_RSSI_NEAR`)
  and the tap catcher (`CATCH_TAPS_DURING_REFRESH`).
- Known limitation: mono and 48 kHz MP3s play at the wrong speed over Bluetooth.

## Bench check before you solder the music switch

With the Feather's two I2C wires connected but the Feather unpowered (switch off), make
sure the Inkplate still turns pages. An unpowered ESP32 can drag the shared I2C lines
down, and the Inkplate needs that bus to drive its screen. If pages stop turning with
the music off, the fix is either an I2C level-shifter board (BSS138 type, e.g.
Adafruit #757) in the two I2C wires, or leaving the Feather powered and making the
switch put it to sleep instead (a firmware change).

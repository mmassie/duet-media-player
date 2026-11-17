# Changelog

Each release is a Git tag (`v0.5`, `v0.6`, …). Add changes under *Unreleased* as you
make them; when you tag a release, rename that heading to the version and date.

## Unreleased

### Case
- **Keys and switches reworked** (the v0.5 parts didn't fit):
  - Side page keys: the switch pocket used to sit in the key's path, so a key
    couldn't be put in, and its flange had no room between the wall and the switch.
    The pocket now starts beyond the key's length, there's 0.3 mm of play
    (`key_slack`), and the switch drops in with its legs either side of the back stop
    instead of having to fit through a slot.
  - Chin keys: a seat behind each of the three keys (the centre one had none; the two
    pillars only held a perfboard strip). No perfboard needed now.
  - More room for the chin keys' flanges between the bezel rails (`key_side_fit`).
  - New switch settings (`tact_*`) to match your switches, and a key fit-test part
    (`duet-key_test.stl`).
- Screen window in the bezel moved 5 mm toward the top of the case so it frames the
  display (`window_dy`). Bezel and fit-test STLs re-exported.

### Repository
- Reorganised into `firmware/`, `hardware/`, `brand/`, `docs/` and `tools/`.
- Build guide split into a build guide, a user guide and a test-status page.
- `firmware/shared/duet_link.h` is now the master copy of the protocol, with
  `tools/sync-link.sh` and a GitHub check to keep both sketches in step.
- `tools/export-case.sh` re-exports the STLs.
- Added the wiring diagram, UI screens and the introductory blog post to `docs/`.

## v0.5 — 2026-11-14

First version in this repository.

### Reader
- Library rebuilt to match the music browser: same row-based list, 10 rows per page.
- Music browser split into Albums, then tracks (one album or the whole card).
- Taps made during a screen refresh are kept and acted on next.
- Sleep screen shows the Duet logo in 8-level grey, with no frame.
- All UI text in sans-serif.

### Audio
- Albums from folders (up to 3 deep), sorted folder by folder; track numbers left
  off titles; shuffle that plays each track once.
- I2C replies prepared on receive (classic ESP32 slave quirk), with a checksum.
- Bluetooth: pairs with nearby headphones first, widens after 45 s, restarts the
  search if a connection hangs for 25 s.

### Protocol (`duet_link.h`, flash both boards)
- Added `CMD_GET_FOLDER` and `CMD_GET_STATUS`, a "music starting" flag, and a
  checksum byte on every reply.

### Case
- Tongue-and-groove joint along the top so the bezel can't peel up; the two chin
  screws lock it.
- Recessed icons on the keys, square key flanges, fit-test part.

## Before v0.5

Earlier versions (the Folio prototype and Duet v1–v3) were developed before this
repository existed and aren't included.

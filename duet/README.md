# Duet — open-source e-ink reader + MP3 player

The Duet is a Kindle-sized reader, in the Folio case design, built on a Soldered **Inkplate 6**, with a second ESP32
(Adafruit **HUZZAH32**) that plays MP3s to a **headphone jack or Bluetooth
headphones**. Case is parametric OpenSCAD, about **112 × 165 × 17 mm**.

Nothing inside is held by tape or glue: every board screws onto **M2.5 heat-set
insert standoffs** printed into the back tray, the battery sits in a printed cradle
under a screwed-down strap, and the switches drop into printed pockets.

```
duet/
├── brand/
│   ├── duet-logo.svg        full logo (mark + wordmark), clean vector, three greys
│   ├── duet-mark.svg        the book-and-rays mark on its own
│   └── duet-logo.png        1440 px preview
├── case/
│   ├── duet-case.scad      parametric model (edit numbers at the top)
│   ├── inkplate_parts.scad  Inkplate back-side parts, from Soldered's KiCad file (used for clearance)
│   ├── stl/                 ready-to-print parts
│   └── preview/             renders
└── firmware/
    ├── duet-reader/        Inkplate 6 sketch (books, page keys, music status)
    └── duet-audio/         HUZZAH32 sketch (MP3 → jack or Bluetooth)
```

Wiring is on the design canvas (“Duet — Wiring”). Pin numbers there match the code.

---

## 1. Print the fit test first (≈15 min)

`stl/duet-fit_test.stl` holds two strips: the top 14 mm of the back tray and
the top 14 mm of the bezel. Push the bare Inkplate into the tray strip and check
that the **USB-C**, **microSD slot** and **power button pin hole** line up. Then
slide the bezel strip's two tongues into the tray strip's top wall. They should
go in by hand with a little drag and no wobble front-to-back. If they're too
tight, raise `tongue_fit` (0.1 → 0.15); if they're loose, lower it. If they’re off, change `stack`, `pcb_back` or the
`*_z` values in `duet-case.scad` and re-export.

Measure these with calipers before the full print:

| Measure | Variable | Default |
|---|---|---|
| Panel front → back end of the Inkplate’s M3 standoffs | `stack` | 10.0 |
| Panel front → back of the Inkplate PCB | `pcb_back` | 3.0 |
| Height of the DAC’s headphone jack above its PCB | `dac_tall` | 5.5 |
| Your battery’s thickness (the strap clamps to exactly this) | `batt_t` | 5.0 |
| Hole size your M2.5 inserts ask for | `ins25_d` | 3.6 |

## 2. Print settings

| Part | Orientation (as exported) | Notes |
|---|---|---|
| `duet-bezel.stl` | Face down | Textured PEI gives a matte, Kindle-like front. No supports. The two top tongues overhang 1.2 mm; that prints fine without support. |
| `duet-tray.stl` | Back down | No supports. Standoffs, pockets and cradle all grow up from the bed. |
| `duet-keys.stl` | Flat | 3 chin keys, 2 side keys, the WAKE plunger and the battery strap. The keys have icons sunk 0.5 mm into their tops and square flanges so they can’t turn; use a 0.4 mm nozzle or smaller and 0.1–0.15 mm layers for crisp icons. The two skip keys are the same part: fit one turned round. |
| `duet-fit_test.stl` | As exported | Tray strip back down, bezel strip face down. Checks the ports and the top tongues. |

PETG or PLA+, 0.2 mm layers, **4 walls** (gives the insert standoffs solid
plastic to melt into), 20 % infill.

## 3. Hardware

| Part | Qty | Where |
|---|---|---|
| M2.5 heat-set inserts, ~4 mm long (hole size → `ins25_d`) | 10 | Feather 2, DAC 4, microSD 2, battery strap 2 |
| M2.5 × 5 mm button-head screws | 10 | Same places |
| M3 × 6 mm screws | 4 | Tray → the Inkplate’s own standoffs |
| M2 heat-set inserts (3.2 mm hole, 4 mm long) | 2 | Bezel chin bosses |
| M2 × 12 mm countersunk screws | 2 | Bezel → tray |
| 6 × 6 × 5 mm tactile switches | 5 | 3 chin, 2 side |
| SS12D00-style slide switch | 1 | Audio power |
| 0.5 mm foam strip | 1 | Gasket around the screen window only |

The microSD breakout is **Adafruit #4682** (3 V only), which has two M2.5 holes.
The older #254 has 2.2 mm holes and won’t take these screws.

## 4. Assembly order

1. **Inserts.** Press the 10 M2.5 inserts into the tray standoffs and the 2 M2 inserts into the bezel chin bosses (iron at ~220 °C for PETG; let the insert sink under its own weight, finish flush).
2. **WAKE plunger.** Drop it into its guide tube from inside the tray, stop flange toward the board.
3. **Side keys.** Put a printed key through each side hole from inside, then press a 6 × 6 switch into the pocket behind it, plunger against the key. Legs go through the slot in the pocket’s back.
4. **Slide switch.** Press it into the pocket behind the bottom-edge slot, lever through the slot.
5. **Chin keys.** Solder the three chin switches to a perfboard strip ~42 × 12 mm at 15.9 mm spacing. It’s clamped between the bezel and the two tray pillars when the bezel goes on.
6. **Audio boards — face-down.** Each board mounts *components toward the back of the case*, flat side toward the Inkplate:
   - **Solder wires before mounting, from the component side**, and trim the legs flush on the flat side. The flat side has only 0.8 mm of clearance to the Inkplate.
   - microSD board (top right): card edge to the side slot, 2 screws.
   - DAC (middle right): jack to the side hole, 4 screws.
   - Feather (bottom right): USB to the side slot. 2 screws at the USB end; the two printed pins at the far end go through its small holes.
7. **Battery.** Lay it in the cradle, leads toward the bottom, and screw the strap across it (2 screws, heads sit in the strap’s counterbores).
8. **Wiring** per the wiring sheet. Cut the red 3.3 V wire of the Qwiic cable. Keep the two page-key wires routed along the walls, not under the boards.
9. **Inkplate.** Plug in the battery and the Qwiic cable, then screw the board to the tray (4 × M3 from the back), screen facing out.
10. **Bezel.** Foam strip around the window. Lay the bezel on about 1.5 mm low, press it flat, and slide it up until its two top tongues seat in the tray's top wall. Then put the 2 × M2 screws in the chin. The screws stop it sliding back down, so the top can't lift. To open it, take out the screws, slide the bezel down, and lift it off.

## 5. Firmware

**Reader (Inkplate 6)** — `firmware/duet-reader`
- Board package: *Inkplate Boards* → **Soldered Inkplate6**
- Library: **InkplateLibrary**
- `ROTATION` is 1 to match the board’s orientation in the Duet case (confirmed on hardware). Use 3 if you ever mount it the other way up.
- **Splash:** on power-on the reader shows the Duet logo in 8-level grey (`duet_logo.h`, one full refresh), then switches to fast black-and-white. Waking from sleep skips it. To change the logo, regenerate `duet_logo.h` from `brand/duet-logo.svg` at 180 px wide.

**Audio (HUZZAH32)** — `firmware/duet-audio`
- Board: **Adafruit ESP32 Feather** (Espressif esp32 core)
- Partition scheme: **Huge APP (3MB No OTA)** — Bluetooth + MP3 need the space
- Libraries: [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools),
  [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix),
  [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)
- Flash it through the side USB slot with the audio switch **off** (so its charger and the Inkplate’s aren’t both on the battery).
- Flash **both** boards whenever either sketch changes: `duet_link.h` (the same file in both folders) is the protocol between them, and v0.5 changed it.

**Cards** (format them FAT32)

```
Book card (Inkplate, top edge)     Music card (#4682, right side)
/books/                            /music/Album name/01 Song.mp3   ← each folder is an album
  Moby-Dick.epub                   /music/Artist/Album/01 Song.mp3 ← up to 3 folders deep
  Some Notes.txt                   /music/Loose song.mp3
  .duet    (auto: bookmark)        /bt.txt   ← optional, one line: the start of your headphones'
  .cache/  (auto: converted EPUBs            Bluetooth name. Only if Duet pairs with the wrong device.
            + page indexes — safe to delete)
```

**Books.** The reader opens **EPUB** (DRM-free: Standard Ebooks, Project
Gutenberg, most indie stores) and plain-text `.txt`. The first time you open a
book it is prepared once — an EPUB is unpacked into reading text, then every book
gets a page index. Expect a few seconds to ~half a minute for a long novel, with a
progress screen; after that it opens instantly and paging back is as fast as
paging forward. Chapters start on a new page and headings are bold and centred.
The built-in fonts are plain ASCII, so accented letters show without their accents
(é → e) and images are skipped. Books bought from Kindle, Apple or Kobo are
DRM-protected and will show “This book is DRM-protected”.

**Music.** Up to 600 MP3s in up to 200 folders. Tracks play in name order,
album after album; a track number at the front of a file name (`07 - Song.mp3`)
sets the order and is left off the title on screen. Shuffle plays every track
once in a random order, so ◀◀ really goes back. Bluetooth needs **44.1 kHz
stereo** MP3s; mono or 48 kHz files play at the wrong speed there (they are fine
on the headphone jack).

**Bluetooth.** Switch the output to Bluetooth in Settings (or hold ▶‖ for 2 s).
The first time, put your headphones in pairing mode and hold them next to Duet:
it pairs with the first pair within about arm’s length, and only after 45 s
with anything further away, so it doesn’t grab a TV or speaker across the room.
From then on it reconnects by itself whenever the headphones are on. Connecting
takes 15–25 s (most of it is a fixed wait inside the Bluetooth library), and the
music starts by itself. The play, pause and skip buttons on the headphones work.

## 6. Controls

| Where | Tap L / R | Hold L | Hold R |
|---|---|---|---|
| Reading | Page back / forward | Library | Now playing |
| Home, Library, Albums, Tracks, Settings | Move up / down | Back one level | Open / play / change |
| Now playing | Previous / next track | Albums and tracks | — |
| Bluetooth connecting screen | — | Use the headphone jack | Pair new headphones |

Hold **both** side keys anywhere for Home. A tap made while the page is still
redrawing is kept and acted on next (set `CATCH_TAPS_DURING_REFRESH` to `false`
in the reader sketch if pages ever turn by themselves).

| Key | Short press | Hold |
|---|---|---|
| ◀◀ ▶‖ ▶▶ (chin) | Previous (restarts the track if more than 3 s in) / play-pause / next | ◀◀ ▶▶ = volume, ▶‖ 2 s = jack ↔ Bluetooth |
| WAKE (bottom edge) | Wake from sleep | — |
| Pin hole (top edge) | Inkplate power button | — |

## 7. What’s tested and what isn’t

- Board holes and connector positions come from Adafruit’s published Eagle files
  (HUZZAH32, UDA1334A, #4682) and Soldered’s Inkplate 6 KiCad file (v1.2.1).
- Every part was checked in OpenSCAD for collisions against the Inkplate’s
  back-side components (`inkplate_parts.scad`), the battery, the strap, the
  boards and each other, including the screw heads under the face-down boards.
  Component **heights** in that file are conservative estimates, not measurements.
- The reader runs on real hardware. The EPUB converter was tested natively on three
  full Standard Ebooks titles (Moby Dick, Pride and Prejudice, The Time Machine):
  output matches an independent extraction word for word (367,828 words), across
  zip tools and compression levels, plus a hand-made EPUB of awkward cases.
- The page layout was run over every page of those books (≈2,000 pages): no lost or
  repeated words, nothing past the margins, every chapter on a new page.
- The **audio board firmware has not run on hardware yet**, and neither sketch could
  be compiled here with the ESP32 toolchain. For v0.5 the library
  calls were checked against the libraries’ own source (arduino-audio-tools,
  ESP32-A2DP, the ESP32 core’s I2C slave driver, the Inkplate library), and both
  sketches run in a desktop simulator with stand-ins for those libraries. Expect to tune two things on real
  hardware: the pairing distance (`PAIR_RSSI_NEAR`) and the tap catcher above.
- **Check this on the bench before you solder the music switch:** with the Feather’s
  two I2C wires connected but the Feather unpowered (switch off), make sure the
  Inkplate still turns pages. An unpowered ESP32 can drag the shared I2C lines
  down, and the Inkplate needs that bus to drive its screen. If pages stop
  turning with the music off, the fix is either an I2C level-shifter board
  (BSS138 type, e.g. Adafruit #757) in the two I2C wires, or leaving the Feather
  powered and making the switch put it to sleep instead (a firmware change).
- All four STLs are watertight.

Licences: the Duet firmware and case are released under the **GNU GPL v3.0** (see `LICENSE` at the top of the repository). `src/inflate/` is the inflate half of
miniz (MIT licence, notice in the file). The font headers in
`firmware/duet-reader/Fonts/` are GNU FreeFont (GPL with font exception), as
shipped in the Inkplate library examples.

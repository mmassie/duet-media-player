# Hardware

```
case/
  duet-case.scad        the parametric case: edit the numbers at the top
  inkplate_parts.scad   Inkplate back-side parts, from Soldered's KiCad file (collision checks)
  stl/                  printable parts, exported from duet-case.scad
  print/                Bambu Studio project (plates and print settings)
```

Building one? Follow the [build guide](../docs/build-guide.md).

## Changing the case

1. Edit `case/duet-case.scad` in OpenSCAD. The variables people usually adjust are in
   the build guide's measurement table (`stack`, `pcb_back`, `batt_t`, `ins25_d`,
   `tongue_fit`, `window_dy`).
2. Re-export the STLs:
   ```
   tools/export-case.sh            # all parts
   tools/export-case.sh bezel      # just one
   ```
3. Commit the `.scad` change and the STLs it produced together, and note it in
   [CHANGELOG.md](../CHANGELOG.md) under *Unreleased*.
4. If the change affects how it looks, re-render the matching image in
   `docs/images/` (`case-*.png`).

Set `part="assembly"` in OpenSCAD to preview everything together (not for printing).

## The print project

`case/print/duet-bambu-project.3mf` is a Bambu Studio project with the bezel, tray and
keys on plates. It was saved on 18 Nov 2026 at 3:34 pm, before the Feather and microSD
boards swapped places, so its tray is one revision behind. Re-import `duet-tray.stl`
before printing, then save the project back here.

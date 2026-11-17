#!/usr/bin/env bash
# Re-export every printable case part from hardware/case/duet-case.scad.
#
#   tools/export-case.sh            export all four STLs
#   tools/export-case.sh bezel      export one part (bezel, tray, keys, fit_test, key_test)
#
# Needs OpenSCAD on your PATH (on macOS: brew install --cask openscad, or set
# OPENSCAD=/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
case_dir="$root/hardware/case"
openscad="${OPENSCAD:-openscad}"
parts=("${@:-bezel tray keys fit_test key_test}")
parts=(${parts[*]})

command -v "$openscad" >/dev/null || { echo "OpenSCAD not found (set OPENSCAD=...)"; exit 1; }

for p in "${parts[@]}"; do
  out="$case_dir/stl/duet-$p.stl"
  echo "→ $p"
  "$openscad" -q -D "part=\"$p\"" -o "$out" "$case_dir/duet-case.scad"
done
echo "Done. Commit the STLs together with the .scad change that produced them."

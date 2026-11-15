#!/usr/bin/env bash
# duet_link.h is the I2C protocol shared by both boards. Arduino only compiles
# files inside a sketch's own folder, so each sketch keeps a copy. The master
# copy lives in firmware/shared/; edit that one, then run this script.
#
#   tools/sync-link.sh          copy firmware/shared/duet_link.h into both sketches
#   tools/sync-link.sh --check  fail if any copy differs (used by CI)
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/firmware/shared/duet_link.h"
copies=("$root/firmware/duet-reader/duet_link.h" "$root/firmware/duet-audio/duet_link.h")

if [[ "${1:-}" == "--check" ]]; then
  status=0
  for c in "${copies[@]}"; do
    if ! cmp -s "$src" "$c"; then
      echo "out of sync: ${c#$root/} (edit firmware/shared/duet_link.h, then run tools/sync-link.sh)"
      status=1
    fi
  done
  [[ $status -eq 0 ]] && echo "duet_link.h copies match"
  exit $status
fi

for c in "${copies[@]}"; do cp "$src" "$c"; echo "updated ${c#$root/}"; done
echo "Flash BOTH boards: they must run the same protocol version."

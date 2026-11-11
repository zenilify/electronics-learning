#!/usr/bin/env bash
# Fetch the pinned ESP RainMaker Neo firmware SDK into vendor/ (gitignored). Pin = README.md "Vendor pin".
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="https://github.com/espressif/esp-rainmaker-neo-firmware.git"
PIN="d944081ba9e0635da9da84da6bb0144cfa5c4a6a"   # main @ 2026-11-06, no tags published yet
DST="$HERE/vendor/esp-rainmaker-neo-firmware"
if [ ! -d "$DST/.git" ]; then
  git clone -q "$REPO" "$DST"
fi
git -C "$DST" fetch -q origin
git -C "$DST" checkout -q --detach "$PIN"
echo "vendor @ $(git -C "$DST" rev-parse HEAD) ($(git -C "$DST" log -1 --format=%cs))"

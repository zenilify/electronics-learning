#!/usr/bin/env bash
# Local iteration build on cachy with the SAME pinned image the Foundry uses (espressif/idf v6.0.3 by digest).
#   tools/build-local.sh            -> build/  (papp_tab5.bin, bootloader, partition table, flash_args)
# The authoritative build is the Foundry (foundry/submit.sh firmware idf); this one is for fast loops.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
IMG=espressif/idf:v6.0.3@sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e
# rsync -a keeps the Mac mtimes, which can be older than build objects -> ninja skips edits. Always rebuild main.
touch "$HERE"/main/*.c "$HERE"/main/*.h
# build/.ok = "this binary came out of a build that succeeded". A failed build leaves the previous papp_tab5.bin in
# place, and flash.sh would happily flash it again (it did, 2026-11-11) - so the marker is removed first.
rm -f "$HERE/build/.ok"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$HERE":/p -w /p "$IMG" \
  bash -c '[ -f build/CMakeCache.txt ] || idf.py set-target esp32p4 >/dev/null; idf.py build'
sha256sum "$HERE/build/papp_tab5.bin" > "$HERE/build/.ok"

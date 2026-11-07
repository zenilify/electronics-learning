#!/usr/bin/env bash
# Local iteration build on cachy with the SAME pinned image the Foundry uses (espressif/idf v6.0.3 by digest).
#   tools/build-local.sh            -> build/  (papp_tab5.bin, bootloader, partition table, flash_args)
# The authoritative build is the Foundry (foundry/submit.sh firmware idf); this one is for fast loops.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
IMG=espressif/idf:v6.0.3@sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$HERE":/p -w /p "$IMG" \
  bash -c '[ -f build/CMakeCache.txt ] || idf.py set-target esp32p4 >/dev/null; idf.py build'

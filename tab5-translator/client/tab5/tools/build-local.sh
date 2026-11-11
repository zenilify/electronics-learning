#!/usr/bin/env bash
# Local build with the SAME pinned ESP-IDF image the Foundry uses (espressif/idf v6.0.3, by digest).
#   tools/build-local.sh            -> build/  (papp_tab5.bin, bootloader, partition table, flash_args)
# Runs on cachy (Linux, x86-64) and on the MacBook (macOS, Apple Silicon; Docker engine = colima, see README
# "From the MacBook"). The authoritative build is the Foundry (foundry/submit.sh firmware idf); this one is for fast
# loops and for the road.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
# One ESP-IDF release, one image per CPU type: the digest names the exact image for this computer.
case "$(uname -m)" in
  x86_64|amd64)  IMG=espressif/idf:v6.0.3@sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e ;;
  arm64|aarch64) IMG=espressif/idf:v6.0.3@sha256:da1f22c157dcb4f684f822ff37be17dc0abef82160d889bfd7fd23cc1299f0fa ;;
  *) echo "no pinned ESP-IDF image for a $(uname -m) CPU"; exit 2 ;;
esac
DOCKER=(docker)
if [ "$(uname -s)" = Darwin ]; then
  # The engine on the Mac itself is colima's. It is addressed by its socket and with a docker settings folder of its
  # own, because Erick's ~/.docker/config.json (a) points the docker command at cachy (context "cachy") and (b) names
  # a credential helper that is not installed (docker-credential-desktop), which makes even a public pull fail.
  # Neither is touched.
  export DOCKER_HOST="unix://$HOME/.colima/default/docker.sock"
  export DOCKER_CONFIG="${T5_DOCKER_CONFIG:-$HOME/t5/docker-config}"; mkdir -p "$DOCKER_CONFIG"
fi
"${DOCKER[@]}" info >/dev/null 2>&1 || {
  echo "the Docker engine is not running$([ "$(uname -s)" = Darwin ] && echo ' - start it with: colima start --activate=false')"; exit 2; }
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
# rsync -a keeps the Mac mtimes, which can be older than build objects -> ninja skips edits. Always rebuild main.
touch "$HERE"/main/*.c "$HERE"/main/*.h
# build/.ok = "this binary came out of a build that succeeded". A failed build leaves the previous papp_tab5.bin in
# place, and flash.sh would happily flash it again (it did, 2026-11-11) - so the marker is removed first.
rm -f "$HERE/build/.ok"
"${DOCKER[@]}" run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$HERE":/p -w /p "$IMG" \
  bash -c '[ -f build/CMakeCache.txt ] || idf.py set-target esp32p4 >/dev/null; idf.py build'
echo "$(sha "$HERE/build/papp_tab5.bin")  build/papp_tab5.bin" > "$HERE/build/.ok"
echo "built: sha256 $(cut -c1-16 "$HERE/build/.ok")...  $HERE/build/papp_tab5.bin"

#!/usr/bin/env bash
# Flash PAPP onto Erick's Tab5 from cachy. Refuses unless the Tab5's USB serial is present BY ID
# (MAC 30:ED:A0:E2:EF:41) — cachy also has another Espressif board on USB.
#   tools/flash.sh backup [LABEL]  full 16 MB read-back into a fresh folder (tools/backup_full.sh)
#   tools/flash.sh nvs        write ~/t5/secrets/nvs.bin at 0x9000 (tools/provision.py makes it)
#   tools/flash.sh app [DIR]  write bootloader + partition table + app from DIR (default build/; or a Foundry artifact dir)
#   tools/flash.sh monitor    30 s of serial log to stdout (opening the port resets the Tab5)
#   tools/flash.sh console S 'AT:cmd'...  reset, send console commands at AT seconds, log S seconds
#   tools/flash.sh bench [S]  reset, then send 'status' + 'demo' on the console and log S seconds
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEV=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00
IMG=espressif/idf:v6.0.3@sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e
[ -e "$DEV" ] || { echo "Tab5 30:ED:A0:E2:EF:41 not on USB — plug it into cachy"; exit 2; }
REAL=$(readlink -f "$DEV")
run() { docker run --rm --device "$REAL" --group-add "$(stat -c %g "$REAL")" -u "$(id -u):$(id -g)" -e HOME=/tmp \
          -v "$HERE":/p -v "$HOME/t5":/t5 -w /p "$IMG" bash -c "$*"; }
case "${1:-}" in
  backup) # delegated: the old code here kept its segments in one fixed folder and reused them on the next run,
          # which can glue two different firmwares into one "backup" (found 2026-11-11).
          exec bash "$HERE/tools/backup_full.sh" "${2:-manual}" ;;
  nvs)    [ -s ~/t5/secrets/nvs.bin ] || { echo "run tools/provision.py first"; exit 2; }
          run "python -m esptool --chip esp32p4 -p $REAL -b 921600 write-flash 0x9000 /t5/secrets/nvs.bin" ;;
  app)    D="${2:-build}"
          # the local build dir must come from a build that succeeded (build-local.sh writes build/.ok)
          if [ "$D" = build ] && ! ( cd "$HERE" && sha256sum -c --status build/.ok 2>/dev/null ); then
            echo "build/.ok is missing or does not match build/papp_tab5.bin - the last build failed or is stale. Not flashing."; exit 4
          fi
          run "cd $D && python -m esptool --chip esp32p4 -p $REAL -b 921600 --before default-reset --after hard-reset write-flash @flash_args" ;;
  monitor) run "timeout ${2:-30} python -c 'import serial,sys; s=serial.Serial(\"$REAL\",115200,timeout=1); [sys.stdout.write(s.read(4096).decode(\"utf-8\",\"replace\")) or sys.stdout.flush() for _ in iter(int,1)]' || true" ;;
  console) shift; S="$1"; shift; A=""; for x in "$@"; do A="$A '$x'"; done
          run "python tools/console.py $REAL $S $A" ;;
  bench)  run "python tools/bench_serial.py $REAL ${2:-120}" ;;
  *) sed -n 2,9p "$0"; exit 2 ;;
esac

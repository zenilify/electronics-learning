#!/usr/bin/env bash
# Flash PAPP onto Erick's Tab5 from cachy or from the MacBook. Refuses unless the Tab5's USB serial is present BY ID
# (MAC 30:ED:A0:E2:EF:41) — other Espressif boards are on USB too, and one of them must never be flashed.
# On the MacBook only `port`, `app` and `backup` exist (esptool runs from ~/t5/venv; README "From the MacBook").
#   tools/flash.sh port            which Espressif boards are connected to this computer
#   tools/flash.sh backup [LABEL]  full 16 MB read-back into a fresh folder (tools/backup_full.sh)
#   tools/flash.sh nvs        write ~/t5/secrets/nvs.bin at 0x9000 (tools/provision.py makes it)
#   tools/flash.sh app [DIR]  write bootloader + partition table + app from DIR (default build/; or a Foundry artifact dir)
#   tools/flash.sh monitor    30 s of serial log to stdout (opening the port resets the Tab5)
#   tools/flash.sh console S 'AT:cmd'...  reset, send console commands at AT seconds, log S seconds
#   tools/flash.sh bench [S]  reset, then send 'status' + 'demo' on the console and log S seconds
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
# the local build dir must come from a build that succeeded (build-local.sh writes build/.ok = hash of the binary)
ok_marker() { [ -f "$HERE/build/.ok" ] && [ "$(cut -d' ' -f1 "$HERE/build/.ok")" = "$(sha "$HERE/build/papp_tab5.bin")" ]; }
NOT_OK="build/.ok is missing or does not match build/papp_tab5.bin - the last build failed or is stale. Not flashing."

if [ "$(uname -s)" = Darwin ]; then
  # MacBook: Docker on macOS cannot hand a USB port to a container, so esptool runs natively from the tool folder's Python.
  PY="${T5_PY:-$HOME/t5/venv/bin/python}"
  [ -x "$PY" ] || { echo "no $PY - one-time setup: README, 'From the MacBook'"; exit 2; }
  case "${1:-}" in
    port)   exec "$PY" "$HERE/tools/t5port.py" --list ;;
    app)    D="${2:-build}"
            if [ "$D" = build ] && ! ok_marker; then echo "$NOT_OK"; exit 4; fi
            PORT=$("$PY" "$HERE/tools/t5port.py") || { echo "Tab5 30:ED:A0:E2:EF:41 not on USB — plug it into the MacBook (tools/flash.sh port shows what is connected)"; exit 2; }
            cd "$HERE" && cd "$D" && exec "$PY" -m esptool --chip esp32p4 -p "$PORT" -b 921600 --before default-reset --after hard-reset write-flash @flash_args ;;
    backup) exec bash "$HERE/tools/backup_full.sh" "${2:-manual}" ;;
    *)      sed -n 2,6p "$0"; exit 2 ;;
  esac
fi

DEV=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00
[ "${1:-}" = port ] && exec python3 "$HERE/tools/t5port.py" --list
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
          if [ "$D" = build ] && ! ok_marker; then echo "$NOT_OK"; exit 4; fi
          run "cd $D && python -m esptool --chip esp32p4 -p $REAL -b 921600 --before default-reset --after hard-reset write-flash @flash_args" ;;
  monitor) run "timeout ${2:-30} python -c 'import serial,sys; s=serial.Serial(\"$REAL\",115200,timeout=1); [sys.stdout.write(s.read(4096).decode(\"utf-8\",\"replace\")) or sys.stdout.flush() for _ in iter(int,1)]' || true" ;;
  console) shift; S="$1"; shift; A=""; for x in "$@"; do A="$A '$x'"; done
          run "python tools/console.py $REAL $S $A" ;;
  bench)  run "python tools/bench_serial.py $REAL ${2:-120}" ;;
  *) sed -n 2,9p "$0"; exit 2 ;;
esac

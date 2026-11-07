#!/usr/bin/env bash
# Flash PAPP onto Erick's Tab5 from cachy. Refuses unless the Tab5's USB serial is present BY ID
# (MAC 30:ED:A0:E2:EF:41) — cachy also has another Espressif board on USB.
#   tools/flash.sh backup     full 16 MB read-back to ~/t5/backup/ (+ sha256)  [do this once, before the first flash]
#   tools/flash.sh nvs        write ~/t5/secrets/nvs.bin at 0x9000 (tools/provision.py makes it)
#   tools/flash.sh app [DIR]  write bootloader + partition table + app from DIR (default build/; or a Foundry artifact dir)
#   tools/flash.sh monitor    30 s of serial log to stdout (no reset)
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEV=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00
IMG=espressif/idf:v6.0.3@sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e
[ -e "$DEV" ] || { echo "Tab5 30:ED:A0:E2:EF:41 not on USB — plug it into cachy"; exit 2; }
REAL=$(readlink -f "$DEV")
run() { docker run --rm --device "$REAL" --group-add "$(stat -c %g "$REAL")" -u "$(id -u):$(id -g)" -e HOME=/tmp \
          -v "$HERE":/p -v "$HOME/t5":/t5 -w /p "$IMG" bash -c "$*"; }
case "${1:-}" in
  backup) # 16 x 1 MB segments, each retried up to 3x: one 16 MB read over USB-Serial/JTAG died at 10 %
          # on 2026-11-07 ("Packet content transfer stopped"). Segments are concatenated + hashed.
          d=~/t5/backup/seg; mkdir -p "$d"; f=~/t5/backup/tab5-30eda0e2ef41-full-$(date +%Y%m%d-%H%M).bin
          for i in $(seq 0 15); do s=/t5/backup/seg/$(printf %02d $i).bin
            [ -s "$HOME/t5/backup/seg/$(printf %02d $i).bin" ] && continue
            for t in 1 2 3; do run "python -m esptool --chip esp32p4 -p $REAL read-flash $((i*0x100000)) 0x100000 $s" >/dev/null 2>&1 && break; sleep 2; done
            [ "$(stat -c %s "$HOME/t5/backup/seg/$(printf %02d $i).bin" 2>/dev/null)" = 1048576 ] || { echo "segment $i failed"; exit 3; }
            echo "segment $i ok"; done
          cat "$d"/??.bin > "$f"; ls -l "$f"; sha256sum "$f" | tee "$f.sha256" ;;
  nvs)    [ -s ~/t5/secrets/nvs.bin ] || { echo "run tools/provision.py first"; exit 2; }
          run "python -m esptool --chip esp32p4 -p $REAL -b 921600 write-flash 0x9000 /t5/secrets/nvs.bin" ;;
  app)    D="${2:-build}"
          run "cd $D && python -m esptool --chip esp32p4 -p $REAL -b 921600 --before default-reset --after hard-reset write-flash @flash_args" ;;
  monitor) run "timeout ${2:-30} python -c 'import serial,sys; s=serial.Serial(\"$REAL\",115200,timeout=1); [sys.stdout.write(s.read(4096).decode(\"utf-8\",\"replace\")) or sys.stdout.flush() for _ in iter(int,1)]' || true" ;;
  *) sed -n 2,9p "$0"; exit 2 ;;
esac

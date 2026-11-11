#!/usr/bin/env bash
# Full 16 MB read-back of the Tab5 flash into a FRESH directory per run (lane T5, 2026-11-11).
#   tools/backup_full.sh LABEL
#     -> ~/t5/backup/pdca-YYYYMMDD/HHMM-LABEL/{seg/NN.bin, full.bin, full.bin.sha256, flash_id.txt}
# Why not `flash.sh backup`: it keeps its segments in one fixed folder and skips the ones already there, so a
# second run mixes old and new flash contents (found 11-11: the 7 Nov run stopped at segment 1 and left 14 old
# segments behind). Here every run starts empty, and a run that fails leaves no full.bin.
# The image contains the NVS partition (Wi-Fi passwords, PAPP bearer, remotes key): files are 0600, the folder
# stays on cachy and never goes to git. Restore: esptool --chip esp32p4 -p PORT write-flash 0x0 full.bin
set -euo pipefail
DEV=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00
[ -e "$DEV" ] || { echo "Tab5 30:ED:A0:E2:EF:41 not on USB"; exit 2; }
LABEL="${1:?usage: backup_full.sh LABEL}"
export TZ=America/Mexico_City
OUT=~/t5/backup/pdca-$(date +%Y%m%d)/$(date +%H%M)-$LABEL
umask 077
mkdir -p "$OUT/seg"
for i in $(seq 0 15); do
  n=$(printf %02d "$i"); s="$OUT/seg/$n.bin"
  for t in 1 2 3; do
    # 1 MB at a time, up to 3 tries: one 16 MB read over USB-Serial/JTAG died at 10 % on 2026-11-07
    esptool --chip esp32p4 -p "$DEV" --before default-reset --after no-reset \
        read-flash $((i * 0x100000)) 0x100000 "$s" >"$OUT/seg/$n.log" 2>&1 && break
    sleep 2
  done
  [ "$(stat -c %s "$s" 2>/dev/null || echo 0)" = 1048576 ] || { echo "segment $i failed (see $OUT/seg/$n.log)"; exit 3; }
  echo "segment $i ok"
done
cat "$OUT"/seg/??.bin > "$OUT/full.bin"
# flash-id as a record of the chip, and --after hard-reset boots the application again
esptool --chip esp32p4 -p "$DEV" --before default-reset --after hard-reset flash-id >"$OUT/flash_id.txt" 2>&1 || true
( cd "$OUT" && sha256sum full.bin >full.bin.sha256 && for f in seg/??.bin; do sha256sum "$f"; done >seg.sha256 )
ls -l "$OUT/full.bin"; cat "$OUT/full.bin.sha256"; echo "OUT=$OUT"

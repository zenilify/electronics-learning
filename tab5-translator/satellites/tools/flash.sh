#!/usr/bin/env bash
# PAPP remotes — MAC-checked flash on cachy (ports by-id only).
#   tools/flash.sh <sticks3|dial> [--fresh]
#   --fresh : write the full factory image at 0x0 (also blanks NVS -> provision again). Default: app only at 0x10000 (keeps key + Wi-Fi).
# Refuses to write unless a verified pre-flash backup of that device exists in ~/t5/backup/remotes/ (house rule).
set -euo pipefail
NAME="${1:?sticks3|dial}"; MODE="${2:-}"
case "$NAME" in sticks3) MAC=14:C1:9F:D5:35:3C;; dial) MAC=70:AF:09:DE:94:4C;; *) echo "unknown device"; exit 1;; esac
HERE="$(cd "$(dirname "$0")/.." && pwd)"
B="$HERE/remote-$NAME/.pio/build/$NAME"
PORT="/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${MAC}-if00"
BK=$(ls ~/t5/backup/remotes/${NAME}-$(echo $MAC | tr -d : | tr A-F a-f)-pre-t5r-*.bin 2>/dev/null | head -1 || true)
[ -n "$BK" ] && [ -f "$BK.sha256" ] && [ "$(stat -c %s "$BK")" = 8388608 ] || { echo "FAIL: no full backup for $NAME in ~/t5/backup/remotes"; exit 2; }
[ -e "$PORT" ] || { echo "FAIL: port for $MAC not present"; exit 2; }
INFO=$(esptool --port "$PORT" --after no-reset chip-id 2>&1) || { echo "$INFO" | tail -3; exit 3; }
got=$(echo "$INFO" | grep -m1 -oiE "MAC: *[0-9a-f:]{17}" | awk '{print toupper($2)}')
[ "$got" = "$MAC" ] || { echo "FAIL: MAC mismatch want=$MAC got=$got"; exit 3; }
echo "MAC ok $got · backup $(basename "$BK")"
if [ "$MODE" = "--fresh" ]; then
  F="$B/firmware.factory.bin"; OFF=0x0
else
  F="$B/firmware.bin"; OFF=0x10000
fi
sha256sum "$F" | cut -c1-16
# watchdog-reset: the hard-reset path leaves the StickS3 latched in download mode (sticks3-bridge README)
esptool --port "$PORT" --baud 921600 --before no-reset --after watchdog-reset write-flash --flash-mode keep --flash-freq keep --flash-size keep "$OFF" "$F" 2>&1 | grep -E "Wrote|Hash|verified|error|Error|Leaving|reset" 
echo "DONE flash $NAME $(basename "$F") @ $OFF"

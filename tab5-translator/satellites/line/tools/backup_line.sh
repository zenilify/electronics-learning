#!/usr/bin/env bash
# T5-I: full 8 MB flash backup + sha256 of the three line satellites, by-id only. Read-only on the devices.
B=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_
cd ~/t5/backup/line
for m in 50:78:7D:CD:64:8C 9C:CC:01:E0:EA:68 50:78:7D:CE:8B:10; do
  f="s3-${m//:/}-$(date +%Y%m%d-%H%M).bin"
  echo "=== $m -> $f $(date +%T)"
  [ -e ${B}${m}-if00 ] || { echo "MISSING PORT $m"; continue; }
  esptool --port ${B}${m}-if00 --baud 921600 read-flash 0 0x800000 "$f" 2>&1 | grep -vE '^\s*$|%' | tail -6
  echo "rc=${PIPESTATUS[0]} size=$(stat -c %s "$f" 2>/dev/null)"
  sha256sum "$f" | tee -a SHA256SUMS
done
echo "ALL DONE $(date +%T)"

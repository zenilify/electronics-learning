#!/usr/bin/env bash
# Boot an ESP32-S3 image in Espressif's QEMU fork and look for proof of life.
#   qemu_smoke.sh <target-name> [seconds]
# PASS = 2nd-stage bootloader loaded the app AND the app reached app_main/user code.
# A board-specific crash after that (no PMIC/LCD on the virtual I2C/SPI) is expected
# and recorded, not hidden.
set -uo pipefail
. "$(dirname "$0")/common.sh"
. "$IDF_PATH/export.sh" >/dev/null 2>&1
name="$1"; secs="${2:-20}"; src="$OUT/$name"; d="$OUT/qemu-$name"; t0=$SECONDS
mkdir -p "$d"
QEMU=$(command -v qemu-system-xtensa || true)
if [ -z "$QEMU" ] || [ ! -f "$src/merged-qemu.bin" ]; then
  echo "qemu=${QEMU:-missing} image=$(ls "$src/merged-qemu.bin" 2>&1)" | tee "$d/build.log"
  write_status "qemu-$name" qemu fail 0 "qemu missing or no merged image"; exit 0
fi
VER="$($QEMU --version | head -1)"
cp "$src/merged-qemu.bin" /tmp/flash.bin
timeout "$secs" "$QEMU" -nographic -no-reboot -machine esp32s3 -m 8M \
  -drive file=/tmp/flash.bin,if=mtd,format=raw -serial mon:stdio > "$d/build.log" 2>&1 < /dev/null
tr -d '\r' < "$d/build.log" > "$d/serial.txt"
boot=$(grep -cE 'boot: Loaded app|Loaded app from partition' "$d/serial.txt")
user=$(grep -cE 'Calling app_main|main_task: Calling app_main|cpu_start: Pro cpu start user code' "$d/serial.txt")
panic=$(grep -m1 -E 'Guru Meditation|abort\(\)|assert failed' "$d/serial.txt" | cut -c1-200)
echo "---- serial (last 40 lines) ----"; tail -40 "$d/serial.txt"
if [ "$boot" -gt 0 ] && [ "$user" -gt 0 ]; then st=ok; else st=fail; fi
write_status "qemu-$name" qemu "$st" $((SECONDS-t0)) "$VER" "bootloader_lines=$boot user_code_lines=$user; first_panic=${panic:-none}"
exit 0

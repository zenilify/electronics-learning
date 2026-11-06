#!/usr/bin/env bash
# Bare ESP-IDF projects (image: kaizen-foundry/idf). microlink is baked at /opt/microlink.
set -uo pipefail
. "$(dirname "$0")/common.sh"
. "$IDF_PATH/export.sh" >/dev/null 2>&1
mkdir -p "$HOME/esp" && ln -sfn /opt/microlink "$HOME/esp/microlink"   # CMakeLists wants $HOME/esp/microlink
VER="esp-idf $(cd "$IDF_PATH" && git describe --tags 2>/dev/null || echo "${IDF_VERSION:-?}"); microlink $(cat /opt/microlink/.foundry-rev 2>/dev/null)"

targets idf | while IFS=$'\t' read -r name path chip; do
  d="$OUT/$name"; w="/workspace/build-idf/$name"; t0=$SECONDS
  mkdir -p "$d" "$(dirname "$w")"; rm -rf "$w"; cp -r "$SRC/$path" "$w"
  defaults="sdkconfig.defaults"
  for f in "$w"/sdkconfig.*; do  # sdkconfig.<board> overlays, never sdkconfig.credentials (not staged anyway)
    b=$(basename "$f"); case "$b" in sdkconfig.defaults|sdkconfig.credentials|sdkconfig.old) ;; *) defaults="$defaults;$b";; esac
  done
  if ( cd "$w" && idf.py -DSDKCONFIG_DEFAULTS="$defaults" set-target "$chip" && idf.py build ) > "$d/build.log" 2>&1; then
    ( cd "$w/build" && cp -- *.bin flasher_args.json flash_args "$d/" 2>/dev/null
      cp bootloader/bootloader.bin partition_table/partition-table.bin "$d/" 2>/dev/null )
    # QEMU flavour: Espressif's esp32s3 QEMU emulates quad-SPI PSRAM only, so the
    # smoke-test image is a second build with octal PSRAM switched to quad.
    # The real artifacts above are untouched; merged-qemu.bin is for QEMU only.
    if grep -q '^CONFIG_SPIRAM_MODE_OCT=y' "$w"/sdkconfig.* 2>/dev/null; then
      q="$w-qemu"; rm -rf "$q"; cp -r "$SRC/$path" "$q"
      printf 'CONFIG_SPIRAM_MODE_QUAD=y\n# CONFIG_SPIRAM_MODE_OCT is not set\n' > "$q/sdkconfig.foundry-qemu"
      ( cd "$q" && idf.py -DSDKCONFIG_DEFAULTS="$defaults;sdkconfig.foundry-qemu" set-target "$chip" && idf.py build ) >> "$d/qemu-build.log" 2>&1
    else q="$w"; fi
    ( cd "$q/build" && { python -m esptool --chip "$chip" merge-bin -o "$d/merged-qemu.bin" --pad-to-size "${FLASH_SIZE:-8MB}" @flash_args \
        || python -m esptool --chip "$chip" merge_bin -o "$d/merged-qemu.bin" --fill-flash-size "${FLASH_SIZE:-8MB}" @flash_args; } ) >> "$d/qemu-build.log" 2>&1
    ls -l "$d"/merged-qemu.bin 2>&1 || tail -5 "$d/qemu-build.log"
    write_status "$name" idf ok $((SECONDS-t0)) "$VER" "defaults=$defaults; target=$chip"
  else
    write_status "$name" idf fail $((SECONDS-t0)) "$VER" "defaults=$defaults"
  fi
done
exit 0

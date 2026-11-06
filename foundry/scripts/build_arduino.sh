#!/usr/bin/env bash
# arduino-cli sketches + PlatformIO projects (image: kaizen-foundry/arduino).
# Seeeduino:samd and its libs are baked into the image; the esp32 core (multi-GB)
# is installed at build time only if an esp32:* target is in the matrix.
set -uo pipefail
. "$(dirname "$0")/common.sh"
# Cloud Build runs steps with HOME=/builder/home; the cores/libs were baked under /root
[ -d /root/.arduino15 ] && export HOME=/root
ACLI="arduino-cli $(arduino-cli version | awk '{print $3}')"

if targets arduino | cut -f3 | grep -q '^esp32:'; then
  arduino-cli core install "esp32:esp32@${ESP32_CORE:-3.3.12}" 2>&1 | tail -5
  arduino-cli lib install "M5GFX@${M5GFX:-0.2.26}" "M5Unified@${M5UNIFIED:-0.2.19}" 2>&1 | tail -5
fi

targets arduino | while IFS=$'\t' read -r name path fqbn; do
  d="$OUT/$name"; w="/workspace/build-arduino/$name"; t0=$SECONDS
  mkdir -p "$d"; rm -rf "$w"; mkdir -p "$(dirname "$w")"
  cp -r "$SRC/$path" "$w"
  # sketch dir must be named like the .ino
  ino=$(basename "$(ls "$w"/*.ino | head -1)" .ino); [ "$(basename "$w")" != "$ino" ] && mv "$w" "$(dirname "$w")/$ino" && w="$(dirname "$w")/$ino"
  [ -f "$w/secrets.h.example" ] && python3 "$(dirname "$0")/dummy_secrets.py" header "$w/secrets.h.example" "$w/secrets.h"
  core=$(arduino-cli core list | awk -v c="${fqbn%:*}" '$1==c {print $1"@"$2}')
  if arduino-cli compile -b "$fqbn" --output-dir "$d/bin" "$w" > "$d/build.log" 2>&1; then
    find "$d/bin" -type f \( -name '*.bin' -o -name '*.uf2' -o -name '*.hex' -o -name '*.elf' \) -exec mv {} "$d/" \;
    rm -rf "$d/bin"
    write_status "$name" arduino ok $((SECONDS-t0)) "$ACLI; core $core" "fqbn=$fqbn; libs=$(arduino-cli lib list 2>/dev/null | awk 'NR>1{printf "%s@%s ",$1,$2}')"
  else
    write_status "$name" arduino fail $((SECONDS-t0)) "$ACLI; core $core" "fqbn=$fqbn"
  fi
done

targets pio | while IFS=$'\t' read -r name path env; do
  d="$OUT/$name"; w="/workspace/build-pio/$name"; t0=$SECONDS
  mkdir -p "$d"; rm -rf "$w"; mkdir -p "$(dirname "$w")"; cp -r "$SRC/$path" "$w"
  if pio run -d "$w" -e "$env" > "$d/build.log" 2>&1; then
    cp "$w/.pio/build/$env"/{firmware.bin,bootloader.bin,partitions.bin,firmware.elf} "$d/" 2>/dev/null
    pio pkg list -d "$w" -e "$env" > "$d/pins.txt" 2>&1
    write_status "$name" pio ok $((SECONDS-t0)) "platformio $(pio --version | awk '{print $NF}')" \
      "$(grep -oE '(framework-arduinoespressif32|platform espressif32|M5Unified|M5GFX|toolchain-xtensa-esp32s3)[^│]*' "$d/pins.txt" | tr -s ' ' | tr '\n' ';' | cut -c1-400)"
  else
    write_status "$name" pio fail $((SECONDS-t0)) "platformio $(pio --version | awk '{print $NF}')"
  fi
done
exit 0

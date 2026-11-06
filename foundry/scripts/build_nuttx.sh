#!/usr/bin/env bash
# "TabOS" spike: Apache NuttX for the M5Stack Tab5 (ESP32-P4), pinned commits.
set -uo pipefail
. "$(dirname "$0")/common.sh"
NUTTX_SHA="${NUTTX_SHA:?}"; APPS_SHA="${APPS_SHA:?}"
base=/workspace/nuttxspace
fetch() {  # fetch <url> <sha> <dir>
  mkdir -p "$3" && git -C "$3" init -q && git -C "$3" fetch -q --depth 1 "$1" "$2" && git -C "$3" checkout -q FETCH_HEAD
}
mkdir -p "$base"
rm -rf "$base/nuttx" "$base/apps"
for i in 1 2 3; do  # GitHub fetches are retried; output goes to the Cloud Build log
  fetch https://github.com/apache/nuttx.git "$NUTTX_SHA" "$base/nuttx" && \
  fetch https://github.com/apache/nuttx-apps.git "$APPS_SHA" "$base/apps" && break
  echo "nuttx fetch attempt $i failed"; rm -rf "$base/nuttx" "$base/apps"; sleep 10
done
git config --global --add safe.directory '*' 2>/dev/null
VER="nuttx ${NUTTX_SHA:0:12}; apps ${APPS_SHA:0:12}; $(riscv-none-elf-gcc --version | head -1); esptool $(esptool.py version 2>/dev/null | tail -1)"

targets nuttx | while IFS=$'\t' read -r name _ cfg; do
  d="$OUT/$name"; t0=$SECONDS; mkdir -p "$d"
  (
    cd "$base/nuttx" || exit 1
    make distclean >/dev/null 2>&1 || true
    ./tools/configure.sh -l "$cfg" && make -j"$JOBS"
  ) > "$d/build.log" 2>&1
  rc=$?
  if [ $rc -eq 0 ] && [ -f "$base/nuttx/nuttx" ]; then
    cp "$base/nuttx/nuttx.bin" "$d/" 2>/dev/null; cp "$base/nuttx/.config" "$d/config.txt"
    ls "$base/nuttx"/*.bin >/dev/null 2>&1 && cp "$base/nuttx"/*.bin "$d/" 2>/dev/null
    riscv-none-elf-size "$base/nuttx/nuttx" > "$d/size.txt" 2>&1
    write_status "$name" nuttx ok $((SECONDS-t0)) "$VER" "config=$cfg; $(tail -1 "$d/size.txt" | awk '{print "text="$1" data="$2" bss="$3}')"
  else
    write_status "$name" nuttx fail $((SECONDS-t0)) "$VER" "config=$cfg rc=$rc"
  fi
done
exit 0

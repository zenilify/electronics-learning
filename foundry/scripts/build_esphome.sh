#!/usr/bin/env bash
# ESPHome matrix: leaders one at a time (warm PlatformIO cache), rest in parallel.
set -uo pipefail
. "$(dirname "$0")/common.sh"
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-/workspace/.cache/platformio}"
export ESPHOME_IS_HA_ADDON=false
VER="esphome $(esphome version 2>/dev/null | awk '{print $2}')"

# placeholder secrets.yaml in every dir holding a target
targets esphome | cut -f2 | xargs -n1 dirname | sort -u | sed "s#^#$SRC/#" \
  | xargs python3 "$(dirname "$0")/dummy_secrets.py" esphome

uses() {  # uses <yaml> <owner/repo>: does the config or a file it !includes mention the repo?
  local y="$1" f; local files=("$y")
  for f in $(grep -oE '!include[[:space:]]+[^[:space:]}]+' "$y" | awk '{print $2}'); do files+=("$(dirname "$y")/$f"); done
  grep -qs -- "$2" "${files[@]}"
}

one() {  # one name path
  local name="$1" yaml="$SRC/$2" d="$OUT/$1" t0=$SECONDS
  mkdir -p "$d"
  if esphome compile "$yaml" > "$d/build.log" 2>&1; then
    local bdir
    # build_path comes from the validated config (default .esphome/build/<name>)
    bdir=$(esphome config "$yaml" 2>/dev/null | awk '/^esphome:/{f=1} f && /^  build_path:/{print $2; exit}')
    # a relative build_path is resolved against <config dir>/.esphome (ESPHome's data dir)
    case "$bdir" in /*) ;; *) for c in "$(dirname "$yaml")/.esphome/$bdir" "$(dirname "$yaml")/$bdir"; do
        [ -d "$c" ] && { bdir="$c"; break; }; done;; esac
    # 2026.10 builds with native ESP-IDF: <build_path>/build/firmware.{factory,ota}.bin
    # (older/PlatformIO builds: <build_path>/.pioenvs/<name>/firmware*.bin)
    find "$bdir/build" "$bdir/.pioenvs" -maxdepth 2 -name 'firmware*.bin' -exec cp {} "$d/" \; 2>/dev/null
    # record the platform pin ESPHome chose + any git external components
    { grep -oE -m1 'esphome/idf/frameworks/[0-9][0-9.]*' "$d/build.log" | sed 's#.*/#esp-idf #'
      grep -E '^(platform|board|framework|platform_packages)\s*=' "$bdir/platformio.ini" 2>/dev/null
      for g in "$(dirname "$yaml")"/.esphome/external_components/*/ "$HOME"/.cache/esphome/external_components/*/ \
               "$(dirname "$yaml")"/.esphome/packages/*/; do
        [ -d "$g/.git" ] || continue
        url=$(git -C "$g" config --get remote.origin.url); slug=$(echo "$url" | sed -E 's#^.*github.com[/:]##; s#\.git$##')
        # the cache is shared by all targets: list a component only if this config (or a file it includes) names it
        uses "$yaml" "$slug" && echo "ext_component $url @ $(git -C "$g" rev-parse --short=12 HEAD)"
      done; } > "$d/pins.txt" 2>/dev/null
    if ls "$d"/firmware*.bin >/dev/null 2>&1; then
      write_status "$name" esphome ok $((SECONDS-t0)) "$VER" "$(tr '\n' ';' < "$d/pins.txt")"
    else
      echo "FOUNDRY ERROR: compile ok but no firmware*.bin under $bdir" > "$d/build.log.tail"; cat "$d/build.log.tail" >> "$d/build.log"
      write_status "$name" esphome nobin $((SECONDS-t0)) "$VER" "compile ok but no firmware*.bin under $bdir"
    fi
  else
    write_status "$name" esphome fail $((SECONDS-t0)) "$VER"
  fi
}
export -f one uses write_status first_error; export OUT SRC VER

targets esphome | awk -F'\t' '$3=="leader"' | while IFS=$'\t' read -r n p _; do one "$n" "$p"; done
targets esphome | awk -F'\t' '$3!="leader" {print $1"\t"$2}' \
  | xargs -P "${ESPHOME_PARALLEL:-6}" -L1 bash -c 'one "$0" "$1"'
exit 0

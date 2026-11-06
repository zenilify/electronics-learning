# shellcheck shell=bash
# Shared helpers. Every build script writes, per target:
#   $OUT/<name>/status.json  {name, kind, status, first_error, seconds, toolchain, notes}
#   $OUT/<name>/build.log    full log (uploaded with the binaries)
#   $OUT/<name>/<binaries>
# Scripts never exit non-zero for a failed target: the manifest step decides.
OUT="${OUT:-/workspace/out}"
SRC="${SRC:-/workspace/src}"
TARGETS="${TARGETS:-/workspace/foundry/targets.tsv}"
JOBS="${JOBS:-$(nproc)}"

targets() {  # targets <kind> -> "name<TAB>path<TAB>extra" lines
  awk -F'\t' -v k="$1" '$1==k {print $2"\t"$3"\t"$4}' "$TARGETS"
}

first_error() {  # first_error <log>: the most specific error line, short
  local t; t=$(mktemp); tr '\r' '\n' < "$1" > "$t"
  { grep -m1 -E '^\s*(Invalid configuration|.* is not a valid|Unable to find|Could not find|Couldn.t find)' "$t" \
      || grep -m1 -E '(error:|Error:|ERROR |FAILED:|fatal error|No rule to make|\*\*\* )' "$t" \
      || grep -m1 -E '(^|[^a-z_])(error|fatal|Traceback|undefined reference|No such file)([^a-z_]|$)' "$t"; } \
    | cut -c1-300 | sed 's/"/\x27/g' || true
  rm -f "$t"
}

write_status() {  # write_status name kind status seconds toolchain [notes]
  local d="$OUT/$1" err=""
  mkdir -p "$d"
  if [ "$3" = nobin ]; then err="${6:-}"
  elif [ "$3" != ok ] && [ -f "$d/build.log" ]; then err="$(first_error "$d/build.log")"; fi
  python3 - "$d/status.json" "$1" "$2" "$3" "$4" "$5" "${6:-}" "$err" <<'PY'
import json, sys
p, name, kind, status, secs, tool, notes, err = sys.argv[1:]
json.dump({"name": name, "kind": kind, "status": status, "seconds": int(secs),
           "toolchain": tool, "notes": notes, "first_error": err or None},
          open(p, "w"), indent=1)
PY
  echo "[$2] $1: $3 (${4}s)${err:+  first error: $err}"
}

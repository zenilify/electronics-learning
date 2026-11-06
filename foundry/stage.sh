#!/usr/bin/env bash
# Assemble the Cloud Build context from GIT-TRACKED files only, so nothing
# gitignored (secrets.yaml, secrets.h, sdkconfig.credentials, tokens) can leak.
# Run on the machine that holds the clones (macbook/estudio).
#   foundry/stage.sh [OUT_DIR]      -> OUT_DIR/{foundry,src,sources.tsv}
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
P="${PARA_PROJECTS:-$(cd "$HERE/../.." && pwd)}"
OUT="${1:-/tmp/kaizen-foundry-ctx}"

# staged-name  repo-path  subpaths...
REPOS=(
  "home-lab|$P/home-lab|esphome firmware/sticks3-bridge"
  "wio-mission-control|$P/SeeedStudio/wio-mission-control|firmware"
  "electronics-learning|$P/electronics-learning|projects"
  "paper-badge|$P/Zenilify-Consulting|docs/brand/paper-badge/firmware"
)
# Untracked-but-required build inputs (generated, not secret). Recorded by sha256.
EXTRA=(
  "paper-badge|$P/Zenilify-Consulting|docs/brand/paper-badge/firmware/src/screens.h"
)

rm -rf "$OUT"; mkdir -p "$OUT/src"
printf 'repo\tcommit\tdirty_files_in_scope\torigin\n' > "$OUT/sources.tsv"
for spec in "${REPOS[@]}"; do
  IFS='|' read -r name repo subs <<<"$spec"
  sha=$(git -C "$repo" rev-parse HEAD)
  origin=$(git -C "$repo" remote get-url origin 2>/dev/null | sed -E 's#^[a-z]+://[^@]*@#https://#')
  # shellcheck disable=SC2086
  dirty=$(git -C "$repo" status --porcelain -- $subs | wc -l | tr -d ' ')
  printf '%s\t%s\t%s\t%s\n' "$name" "$sha" "$dirty" "$origin" >> "$OUT/sources.tsv"
  # shellcheck disable=SC2086
  git -C "$repo" ls-files -z -- $subs | while IFS= read -r -d '' f; do
    dst="$f"
    [ "$name" = paper-badge ] && dst="${f#docs/brand/paper-badge/}"
    mkdir -p "$OUT/src/$name/$(dirname "$dst")"
    cp -p "$repo/$f" "$OUT/src/$name/$dst"
  done
done
for spec in "${EXTRA[@]}"; do
  IFS='|' read -r name repo f <<<"$spec"
  dst="${f#docs/brand/paper-badge/}"
  if [ -f "$repo/$f" ]; then
    mkdir -p "$OUT/src/$name/$(dirname "$dst")"; cp -p "$repo/$f" "$OUT/src/$name/$dst"
    printf 'extra:%s/%s\tsha256:%s\tuntracked\t-\n' "$name" "$dst" "$(shasum -a 256 "$repo/$f" | cut -c1-16)" >> "$OUT/sources.tsv"
  fi
done

# Secret gate: refuse to continue if any credential-bearing file made it in.
bad=$(find "$OUT/src" \( -name 'secrets.yaml' -o -name 'secrets.h' -o -name 'sdkconfig.credentials' \
        -o -name '*.token' -o -name '.env' -o -name '*.pem' -o -name '*.key' \) -print)
if [ -n "$bad" ]; then echo "SECRET GATE: refusing, found:"; echo "$bad"; exit 3; fi
# Shape scan (Tailscale/GitHub/Google keys, private keys). Prints file names only.
if grep -rlE 'tskey-[a-z]+-[A-Za-z0-9]{8,}|ghp_[A-Za-z0-9]{30,}|AIza[0-9A-Za-z_-]{30,}|-----BEGIN [A-Z ]*PRIVATE KEY-----' "$OUT/src" ; then
  echo "SECRET GATE: credential-shaped string in the files above; refusing"; exit 3
fi

rsync -a --exclude '.DS_Store' --exclude '._*' "$HERE/" "$OUT/foundry/"
# macOS: drop AppleDouble '._*' files and xattrs so Linux builders never see them
find "$OUT" -name '._*' -delete; command -v xattr >/dev/null && xattr -rc "$OUT" 2>/dev/null || true
echo "staged -> $OUT ($(du -sh "$OUT" | cut -f1)); sources:"; column -t -s$'\t' "$OUT/sources.tsv"

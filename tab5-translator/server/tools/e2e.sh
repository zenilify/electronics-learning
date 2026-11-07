#!/usr/bin/env bash
# End-to-end over HTTP: every direction, N runs each, against one base URL.
# Usage: tools/e2e.sh http://100.69.244.21:8787 [repeat]   (run from server/, needs testaudio/*.wav)
set -euo pipefail
URL=${1:?base url}; N=${2:-2}
CLIENT=${CLIENT:-$(dirname "$0")/../../client/desktop/papp_client.py}
for pair in es:zh zh:es en:zh zh:en es:en en:es; do
  s=${pair%:*}; t=${pair#*:}
  python3 "$CLIENT" --url "$URL" --src "$s" --tgt "$t" "testaudio/$s.wav" -o "/tmp/papp_${s}_${t}.wav" --repeat "$N"
done

#!/usr/bin/env bash
# PAPP offline brain — one command:  ./start.sh            (foreground, Ctrl-C stops)
#   PAPP_MT=qwen3|qwen25|nllb  PAPP_TTS=piper|say  PAPP_PORT=8787  PAPP_MDNS=1
# One-time, while online: ./setup.sh  (venv in ~/papp-brain/.venv + models into the HF cache)
set -euo pipefail
# --airgap: re-exec inside macOS sandbox-exec with every outbound IP connection denied (airgap.sb)
if [ "${1:-}" = "--airgap" ] && [ -z "${PAPP_AIRGAPPED:-}" ]; then
  export PAPP_AIRGAPPED=1; exec sandbox-exec -f "$(cd "$(dirname "$0")" && pwd)/airgap.sb" "$0" "$@"
fi
HERE="$(cd "$(dirname "$0")" && pwd)"
STATE="${PAPP_STATE:-$HOME/papp-brain}"
PY="$STATE/.venv/bin/python"
mkdir -p "$STATE/cache" "$STATE/logs"
[ -x "$PY" ] || { echo "no venv at $STATE/.venv — run ./setup.sh while online" >&2; exit 1; }
[ -s "${PAPP_TOKEN_FILE:-$HOME/.config/zenilify/papp_token}" ] || { echo "token file missing (0600)" >&2; exit 1; }

# Best effort: refresh the FX snapshot if there is internet; silently keep the old one if not.
curl -sf -m 4 https://open.er-api.com/v6/latest/USD 2>/dev/null | "$PY" -c '
import json,sys
j=json.load(sys.stdin); keep=("USD","CNY","MXN","EUR","CHF","HKD","JPY")
json.dump({"base":"USD","date":j.get("time_last_update_utc",""),"source":"open.er-api.com (cached by offline brain)",
           "rates":{k:j["rates"][k] for k in keep if k in j["rates"]}},sys.stdout)' > "$STATE/cache/fx.json.tmp" \
  && mv "$STATE/cache/fx.json.tmp" "$STATE/cache/fx.json" || rm -f "$STATE/cache/fx.json.tmp"

# Best effort: refresh the SD phrasebook pack from cachy (rsync over the tailnet/LAN); keep the old copy if offline.
rsync -a --timeout=8 cachy:papp/phrasebook-pack-v2/papp/ "$STATE/sdpack/" 2>/dev/null || true

# Never touch the network from here on: a missing model must fail, not download.
export HF_HUB_OFFLINE=1 TRANSFORMERS_OFFLINE=1 HF_DATASETS_OFFLINE=1 TOKENIZERS_PARALLELISM=false
export PAPP_STATE="$STATE" PAPP_MT="${PAPP_MT:-qwen3}" PAPP_TTS="${PAPP_TTS:-piper}" PAPP_PORT="${PAPP_PORT:-8787}"
cd "$HERE/brain"
# 0.0.0.0 on purpose: on a phone hotspot the Mac's IP changes; the bearer token guards every
# endpoint except /health. Do not port-forward this anywhere.
exec "$PY" -m uvicorn app:app --host 0.0.0.0 --port "$PAPP_PORT" --no-access-log

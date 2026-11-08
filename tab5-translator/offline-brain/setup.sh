#!/usr/bin/env bash
# One-time (ONLINE) setup of the PAPP offline brain on an Apple Silicon Mac.
# venv + models live outside the repo: ~/papp-brain/.venv and the Hugging Face cache (~9 GB).
# Token: the Tab5's bearer must match — copy it from cachy over ssh stdin, never via chat/argv:
#   (umask 077; ssh cachy 'cat ~/.config/zenilify/papp_token' > ~/.config/zenilify/papp_token)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
STATE="${PAPP_STATE:-$HOME/papp-brain}"
mkdir -p "$STATE/logs" "$STATE/cache"
command -v uv >/dev/null || { echo "install uv first: brew install uv" >&2; exit 1; }
[ -x "$STATE/.venv/bin/python" ] || uv venv -p 3.12 "$STATE/.venv"
VIRTUAL_ENV="$STATE/.venv" uv pip install -r "$HERE/requirements.txt"
"$STATE/.venv/bin/python" "$HERE/fetch_models.py"
echo "setup OK — now: $HERE/start.sh"

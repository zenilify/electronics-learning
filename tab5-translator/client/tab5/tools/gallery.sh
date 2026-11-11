#!/usr/bin/env bash
# Screenshots of every PAPP screen into one folder (lane T5, 2026-11-11) - no reset, about 1 minute.
#   tools/gallery.sh [OUTDIR]     default ~/t5/shots/gallery-<date>-<time>
# Uses the keyboard shortcuts of the home screen (key t e f l c p n d s r o a). The camera screen shows whatever the
# camera sees; "Diálogo" and "Subtítulos" are shot idle (nothing is recorded or played).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$HOME/t5/shots/gallery-$(TZ=America/Mexico_City date +%Y%m%d-%H%M)}"
mkdir -p "$OUT"
python3 "$HERE/screenshot.py" "$OUT/00-home.jpg" --pre "key esc" --pre "key esc" --wait 2 --quality 90
i=1
for pair in t:traductor e:escribe f:frases l:llevame c:convertir p:camara n:notas d:dialogo s:subtitulos r:controles o:linea a:ajustes; do
  key="${pair%%:*}"; name="${pair##*:}"
  python3 "$HERE/screenshot.py" "$OUT/$(printf %02d "$i")-$name.jpg" --pre "key esc" --pre "key esc" --pre "key $key" --wait 4 --quality 90
  i=$((i + 1))
done
python3 "$HERE/console_nr.py" /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00 3 0.5:"key esc" 1.5:"key esc" >/dev/null 2>&1 || true
ls -1 "$OUT"; echo "OUT=$OUT"

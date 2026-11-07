#!/usr/bin/env bash
# Build the CJK font subset the firmware embeds (main/assets/papp_cjk.ttf).
# Runs on cachy (needs network + fontTools). Source: Noto Sans SC (variable, OFL 1.1)
# from github.com/google/fonts, pinned to wght=500 and subset to:
#   ASCII + Latin-1 (es accents, ¿¡) + general/CJK punctuation + full-width forms
#   + GB2312 level-1 hanzi (3,755 most common) + every character in main/*.c strings
#   and the demo phrases (so the UI never shows a missing glyph).
# Budget: <= 2.2 MB (the app partition is 12 MB; the font is read straight from flash).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
W="${WORK:-$HOME/t5/font}"; mkdir -p "$W"
URL='https://github.com/google/fonts/raw/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf'
[ -s "$W/NotoSansSC-VF.ttf" ] || curl -fsSL "$URL" -o "$W/NotoSansSC-VF.ttf"
[ -s "$W/OFL.txt" ] || curl -fsSL 'https://github.com/google/fonts/raw/main/ofl/notosanssc/OFL.txt' -o "$W/OFL.txt"
sha256sum "$W/NotoSansSC-VF.ttf" | cut -c1-16 > "$W/source.sha256"
python3 -m fontTools.varLib.instancer "$W/NotoSansSC-VF.ttf" wght=500 -o "$W/NotoSansSC-500.ttf" -q
python3 - "$HERE" "$W/chars.txt" <<'PY'
import sys, glob, pathlib
here, out = sys.argv[1], sys.argv[2]
cs = set(chr(c) for c in range(0x20, 0x7F)) | set(chr(c) for c in range(0xA0, 0x100))
cs |= set(chr(c) for c in range(0x2010, 0x2028)) | set("…•→⇄·×°")
cs |= set(chr(c) for c in range(0x3000, 0x3020)) | set(chr(c) for c in range(0xFF01, 0xFF5F))
for hi in range(0xB0, 0xD8):            # GB2312 level 1 = rows 16..55
    for lo in range(0xA1, 0xFF):
        try: cs.add(bytes([hi, lo]).decode('gb2312'))
        except UnicodeDecodeError: pass
for f in glob.glob(f"{here}/main/*.[ch]"):
    cs |= set(pathlib.Path(f).read_text(encoding='utf-8'))
cs = {c for c in cs if c.isprintable() or c == ' '}
pathlib.Path(out).write_text(''.join(sorted(cs)), encoding='utf-8')
print("chars", len(cs))
PY
pyftsubset "$W/NotoSansSC-500.ttf" --text-file="$W/chars.txt" --output-file="$HERE/main/assets/papp_cjk.ttf" \
  --layout-features='' --no-hinting --desubroutinize --notdef-outline --name-IDs='*' --name-languages='*'
cp "$W/OFL.txt" "$HERE/main/assets/OFL-NotoSansSC.txt"
ls -l "$HERE/main/assets/papp_cjk.ttf"; sha256sum "$HERE/main/assets/papp_cjk.ttf" | cut -c1-16

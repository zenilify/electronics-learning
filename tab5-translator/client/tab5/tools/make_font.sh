#!/usr/bin/env bash
# Build the CJK font subset the firmware embeds (main/assets/papp_cjk.ttf).
# Runs on cachy (needs network + fontTools). Source: Noto Sans SC (variable, OFL 1.1)
# from github.com/google/fonts, pinned to wght=500 and subset to:
#   ASCII + Latin-1 (es accents, ¿¡) + general/CJK punctuation + full-width forms
#   + all of GB2312 (level 1 = the 3,755 most common simplified hanzi, level 2 = 3,008 rarer ones)
#   + the common traditional characters (Big5 level 1: signs in Hong Kong / Taiwan through the camera)
#   + every character in main/*.c strings and in the phrase list server/phrasebook/phrases.psv.
# Until 2026-11-11 only GB2312 level 1 was in: two phrases of the pack (茉莉花茶, 我扭伤了脚踝) and every
# traditional sign showed an empty box per missing character. The script now fails if a phrase character
# that the source font has is missing from the result.
# Size: about 2.7 MB (was 1.14 MB). The app partition is 12 MB and the font is read straight from flash;
# what a character costs in RAM is set by the glyph caches in main/ui.c, not by how many the file holds.
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
# pinyin: every tone-marked vowel, both cases. The phrase pack's pinyin lines are made on the server (pypinyin),
# so they are in no file of this repo: "lǜ" (绿) and "lǚ" (旅) were boxes until 2026-11-11.
cs |= set("āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜüêńňǹḿɑɡ" "ĀÁǍÀĒÉĚÈĪÍǏÌŌÓǑÒŪÚǓÙǕǗǙǛÜ")
for hi in range(0xB0, 0xF8):            # GB2312 level 1 = rows 16..55, level 2 = rows 56..87
    for lo in range(0xA1, 0xFF):
        try: cs.add(bytes([hi, lo]).decode('gb2312'))
        except UnicodeDecodeError: pass
for hi in range(0xA4, 0xC7):            # Big5 level 1 (frequently used traditional characters) = A440..C67E
    for lo in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
        try: c = bytes([hi, lo]).decode('big5')
        except UnicodeDecodeError: continue
        if '\u4e00' <= c <= '\u9fff': cs.add(c)
for f in glob.glob(f"{here}/main/*.[ch]"):
    cs |= set(pathlib.Path(f).read_text(encoding='utf-8'))
phrases = pathlib.Path(here) / ".." / ".." / "server" / "phrasebook" / "phrases.psv"
if phrases.exists():                    # the source of the SD phrase pack: its characters must never be a box
    cs |= set(phrases.read_text(encoding='utf-8'))
cs = {c for c in cs if c.isprintable() or c == ' '}
pathlib.Path(out).write_text(''.join(sorted(cs)), encoding='utf-8')
print("chars", len(cs))
PY
pyftsubset "$W/NotoSansSC-500.ttf" --text-file="$W/chars.txt" --output-file="$HERE/main/assets/papp_cjk.ttf" \
  --layout-features='' --no-hinting --desubroutinize --notdef-outline --name-IDs='*' --name-languages='*'
python3 - "$HERE" "$W/NotoSansSC-500.ttf" "$W/chars.txt" <<'PY'
import pathlib, sys
from fontTools.ttLib import TTFont
here, src, chars = sys.argv[1], sys.argv[2], sys.argv[3]
out = TTFont(f"{here}/main/assets/papp_cjk.ttf").getBestCmap(); have = TTFont(src).getBestCmap()
want = set(pathlib.Path(chars).read_text(encoding='utf-8')) - {' '}
lost = sorted(c for c in want if ord(c) in have and ord(c) not in out)
nosrc = sorted(c for c in want if ord(c) not in have)
print(f"font: {len(out)} code points · asked for {len(want)} · not in the source font: {len(nosrc)} {''.join(nosrc)[:40]}")
# The phrase list, and - where it exists (cachy) - the built SD pack with its server-made pinyin: a character of
# either that the source font HAS must be in the result. One the source lacks (an emoji) is only reported.
texts = [pathlib.Path(here) / ".." / ".." / "server" / "phrasebook" / "phrases.psv",
         pathlib.Path.home() / "papp" / "phrasebook-pack-v2" / "papp" / "phrasebook" / "index.json"]
box, cannot = set(), set()
for t in texts:
    if not t.exists(): continue
    for c in t.read_text(encoding='utf-8'):
        if ord(c) > 0x20 and ord(c) not in out: (box if ord(c) in have else cannot).add(c)
    print(f"checked {t.name}")
if cannot: print("in the phrases but not in the source font (shown as a box):", ''.join(sorted(cannot)))
if lost or box:
    sys.exit(f"FAILED: dropped by the subset: {''.join(lost)[:60]} · phrase characters without a glyph: {''.join(sorted(box))[:60]}")
print("every phrase character the source font has is in the result")
PY
cp "$W/OFL.txt" "$HERE/main/assets/OFL-NotoSansSC.txt"
ls -l "$HERE/main/assets/papp_cjk.ttf"; sha256sum "$HERE/main/assets/papp_cjk.ttf" | cut -c1-16

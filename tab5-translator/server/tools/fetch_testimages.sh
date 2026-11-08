#!/usr/bin/env bash
# Public-domain / CC signage photos from Wikimedia Commons for the OCR probe (cachy, server/).
set -euo pipefail
mkdir -p testimages; cd testimages
UA="papp-ocr-probe/0.1 (lane T5-S)"
for f in "Beijing_Street_Sign_3298381366.jpg" "Shenzhen_Huanggang_border_crossing_direction_to_HK.jpg"; do
  [ -s "$f" ] || curl -sfL -A "$UA" -o "$f" "https://commons.wikimedia.org/wiki/Special:FilePath/$f?width=1280"
done
ls -la
# plus the top Commons file hits for a menu and a metro/shop sign
for q in "Chinese restaurant menu board" "Shanghai metro station sign" "Chinese shop sign street"; do
  f=$(curl -sf -A "$UA" "https://commons.wikimedia.org/w/api.php?action=query&list=search&srnamespace=6&srlimit=3&format=json&srsearch=$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1]))' "$q")" |
      python3 -c 'import sys,json;h=[x["title"][5:] for x in json.load(sys.stdin)["query"]["search"] if x["title"].lower().endswith((".jpg",".jpeg",".png"))];print(h[0] if h else "")')
  [ -n "$f" ] || continue; n=$(echo "$f" | tr " " "_")
  [ -s "$n" ] || curl -sfL -A "$UA" -o "$n" "https://commons.wikimedia.org/wiki/Special:FilePath/$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1]))' "$n")?width=1280" || true
done
ls -la

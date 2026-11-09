# Native-speaker review of phrases.psv — status

Sheets built by `make_review_sheets.py OUT_DIR Rain Daniel James` (113 / 113 / 114 rows, split by order,
de-duplicated by Chinese) and sent 2026-11-08 from erick@zenilify-consulting.com.

| Sheet | Reviewer | Rows | Returned | Result | Applied to phrases.psv |
|---|---|---|---|---|---|
| A | Rain | A001–A113 | — | waiting | — |
| B | Daniel Hsieh | B001–B113 (Drinks → Time & numbers) | 2026-11-09 (Proton, `PAPP-phrase-review-B-Daniel(ALL OK--NOV 9,2026).xlsx`) | **113 × OK, 0 FIX**, no "better Chinese", no comments; Chinese column unchanged vs the sent file. Daniel: "all O.K. after our quicker look with English and Chinese column" (a quick pass) | nothing to change |
| C | James Lin | C001–C114 | — | said yes 2026-11-08, waiting | — |

Merge rule for FIX rows when A/C come back: replace the zh (and regenerate pinyin) in phrases.psv, re-render
that phrase's TTS (`fnv32("zh|<text>")` key changes → new WAV in the SD pack), and note the row here.

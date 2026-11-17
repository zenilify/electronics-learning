# Native-speaker review of phrases.psv — status

Sheets built by `make_review_sheets.py OUT_DIR Rain Daniel James` (113 / 113 / 114 rows, split by order,
de-duplicated by Chinese) and sent 2026-11-08 from erick@zenilify-consulting.com.

| Sheet | Reviewer | Rows | Returned | Result | Applied to phrases.psv |
|---|---|---|---|---|---|
| A | Rain | A001–A113 | — | waiting | — |
| B | Daniel Hsieh | B001–B113 (Drinks → Time & numbers) | 2026-11-09 (Proton, `PAPP-phrase-review-B-Daniel(ALL OK--NOV 9,2026).xlsx`) | **113 × OK, 0 FIX**, no "better Chinese", no comments; Chinese column unchanged vs the sent file. Daniel: "all O.K. after our quicker look with English and Chinese column" (a quick pass) | nothing to change |
| C | James Lin | C001–C114 | 2026-11-16 (file `PAPP-phrase-review-C-James.xlsx`, handed over by Erick 11-17) | **111 × OK, 3 FIX** (C005, C022, C048), no comments; the 114 Chinese cells match `phrases.psv` verbatim. His three fixes are written in Traditional characters with Taiwan wording (精實生產, 人工智慧, 顧問公司, 有在…) | applied 2026-11-17 **mainland-adapted** on Erick's choice — see below |

Merge rule for FIX rows when A/C come back: replace the zh (and regenerate pinyin) in phrases.psv, re-render
that phrase's TTS (`fnv32("zh|<text>")` key changes → new WAV in the SD pack), and note the row here.

## Sheet C fixes applied 2026-11-17 (Erick's decisions in chat: "Mainland-adapted", C005 = "Give me five minutes")

| Row | psv id | Was | James wrote | Applied |
|---|---|---|---|---|
| C005 | tiempo-010 | 五分钟后 · En cinco minutos · In five minutes | 給我五分鐘 | **给我五分钟** · es/en changed to match: Dame cinco minutos · Give me five minutes |
| C022 | reuniones-005 | 我们是一家精益制造和人工智能咨询公司 | 我們是一家專注於精實生產與 AI（人工智慧）的顧問公司 | **我们是一家专注于精益生产与人工智能的咨询公司** (his structure; mainland terms 精益生产 / 人工智能 / 咨询公司; "AI（…）" dropped so the voice does not say it twice) |
| C048 | fabrica-007 | 你们用精益生产或者改善吗？ | 你們有在實行精實生產 (Lean) 或是持續改善 (Kaizen) 的管理模式嗎？ | **你们有没有实行精益生产或持续改善的管理模式？** (his structure; 有没有 for the Taiwan-style 有在; bracketed English dropped for speech) |

The mainland wording is Claude's adaptation of James's fixes, not reviewed by a mainland speaker — sheet A (Rain) is still out.

Rendered on cachy 11:40 CST: `tools/phrasebook.py` → `~/papp/phrasebook-pack-v2` (4 new clips: zh ×3 `065cdd7c` `6e39b1f2` `caf842a5`, es `c5d3e3a5`; 684 files) and
`tools/phrasebook_voice.py render` → `~/papp/voice-pack-erick` (2 new clips, es + en of C005; 682 files). Each of the six clips was transcribed back with VoiceStudio's whisper and
matches its text. The running `papp` container sees the new index and files through its `/sdpack` mount (no restart). Backups on cachy: `phrases.psv.bak-20261117`, `index.json.bak-20261117`,
`MANIFEST.sha256.bak-20261117` in both packs. The old clips stay in the packs (unused keys). **Not done: the Tab5's SD card still holds the old pack until it syncs.**

# Native-speaker review of phrases.psv — status

Sheets built by `make_review_sheets.py OUT_DIR Rain Daniel James` (113 / 113 / 114 rows, split by order,
de-duplicated by Chinese) and sent 2026-11-08 from erick@zenilify-consulting.com.

| Sheet | Reviewer | Rows | Returned | Result | Applied to phrases.psv |
|---|---|---|---|---|---|
| A | Rain | A001–A113 | 2026-11-12 (Proton, `PAPP-phrase-review-A-Rain.xlsx`, sha256 `d5325a44…`; found by the session on 11-17) | **109 × OK, 2 FIX** (A023, A042), 2 rows left unmarked (A001 你好, A002 谢谢); a stray "O" in the better-Chinese cell of A096 (marked OK) ignored; the 113 Chinese cells match `phrases.psv` verbatim | applied 2026-11-17 as she wrote them — see below |
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

The mainland wording is Claude's adaptation of James's fixes, not reviewed by a mainland speaker (Rain's sheet A covered other rows).

Rendered on cachy 11:40 CST: `tools/phrasebook.py` → `~/papp/phrasebook-pack-v2` (4 new clips: zh ×3 `065cdd7c` `6e39b1f2` `caf842a5`, es `c5d3e3a5`; 684 files) and
`tools/phrasebook_voice.py render` → `~/papp/voice-pack-erick` (2 new clips, es + en of C005; 682 files). Each of the six clips was transcribed back with VoiceStudio's whisper and
matches its text. The running `papp` container sees the new index and files through its `/sdpack` mount (no restart). Backups on cachy: `phrases.psv.bak-20261117`, `index.json.bak-20261117`,
`MANIFEST.sha256.bak-20261117` in both packs. The old clips stay in the packs (unused keys). **Tab5 card synced 2026-11-17 ~11:55 CST** (`sdsync` over the USB console on cachy with `console_nr.py`, no reset, uptime 2.5 d kept): `sd sync: 1374 files, 14 new (1084 kB; 2 of them same size, other content), 1360 kept, 0 failed, 11.8 s`. Not checked: tapping the three phrases on the Tab5 (Erick).

## Sheet A fixes applied 2026-11-17 ~12:15 CST (Rain, mainland reviewer — taken as written)

| Row | psv id | Was | Applied |
|---|---|---|---|
| A023 | hotel-002 | 请给我无线网密码 | **请问Wi-Fi密码是多少？** · pinyin `qǐng wèn Wi-Fi mìmǎ shì duōshao` (she typed "WI-FI" and a pinyin without all tone marks; casing and tones normalised) |
| A042 | basico-010 | 不是 | **不用** (bú yòng) — es/en stay "No"; it now means "no, thanks / no need", not a factual "no" |

Rendered on cachy (`phrasebook.py`, 2 new zh clips `71717085`, `887da471`; the Erick-voice pack needed nothing). Transcribed back: 不用 exact; the Wi-Fi clip came back as
"请问,YFI密码是多少?" — the Chinese is right and "Wi-Fi" is spoken the English way; **not confirmed by ear**. Tab5 synced: `1378 files, 8 new, 0 failed`, then `0 new`.
Both rows are among the first 40 = the firmware's built-in list (`client/tab5/main/content.h`), which was **not** changed (no rebuild before the trip): the Frases screen reads the card's
index, so the built-in wording only shows if the card is missing.

Housekeeping: this session first put its `*.bak-20261117*` backups inside the served pack folders, so six small backup files were synced to the Tab5 card. They were moved to
`cachy:~/papp/backups-20261117/`; the copies on the card stay there, unused (the Tab5 still counts 1378 files).

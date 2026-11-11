# PAPP server v2 — API contract (lane T5-S, 2026-11-07)

Base URLs, in the Tab5's failover order: `http://100.69.244.21:8787` (cachy, Tailscale) ·
`http://192.168.1.5:8787` (cachy, LAN / UTR Teleport) · `https://papp-krkzbkmwjq-uc.a.run.app` (Cloud Run).
Same bearer everywhere: `Authorization: Bearer <papp_token>` (401 without it). `GET /health` needs no auth.
Languages: `es`, `en`, `zh`. Audio: WAV mono PCM16, 16 kHz recommended; `rate=` picks the reply rate (8000–44100).
Errors: JSON `{"detail": "..."}` with 400 (bad input) · 401 · 404 (unknown stream sid) · 413 (too big) ·
502 (upstream NVIDIA/Google failed — retry or fail over to the next base URL).

| Method · path | Body | Reply | Notes |
|---|---|---|---|
| `GET /health` | — | `{ok, version:"0.2.0", features[], ocr_engines[], cooling_down{}, cache{}}` | v1 clients unaffected |
| `POST /translate?src=es&tgt=zh[&format=wav][&rate=16000]` | WAV (raw `audio/wav` or multipart `audio`) ≤ 30 s | v1 unchanged | |
| `POST /translate?src=auto&tgt=zh` | WAV | JSON adds `detected`; wav adds headers `X-Papp-Detected`, `X-Papp-Src`, `X-Papp-Tgt` | detected == tgt → no translation, `tgt_text = src_text`, empty audio |
| `POST /translate?pair=es,zh[&format=wav]` | WAV | as above | **Conversation mode**: language ID picks the speaker; target = the other language of the pair |
| `POST /translate-text?src=auto&tgt=zh` | `{"text"}` ≤ 2000 chars | v1 shape + `detected` | text LID |
| `POST /tts?lang=zh[&rate=16000]` | `{"text"}` ≤ 1000 chars | `audio/wav`, headers `X-Papp-Cache: hit|miss`, `X-Papp-Ms` | server disk cache; every phrasebook phrase is pre-cached |
| `GET or POST /fx` | — | `{base:"USD", date, source, rates{USD,CNY,MXN,EUR,CHF,HKD,JPY}, pairs{"CNY>MXN",…}, fetched_at, stale}` | cached 6 h, persisted to disk; `stale:true` = served from the last good copy |
| `POST /summarize?lang=auto&out=es` | WAV ≤ 10 min, or `{"text"}` ≤ 20 000 chars | `{lang, out, audio_s, transcript, title, summary, bullets[], actions[], markdown, model, timings_ms}` | `markdown` is ready to write to `/sdcard/papp/notes/<ts>.md` |
| `POST /ocr-translate?engine=vl[&tts=es]` | `image/jpeg` or `image/png` (raw or multipart `image`) ≤ 6 MB | `{engine, items:[{zh, pinyin, es, en}], summary_es, summary_en, fallbacks[], timings_ms[, audio_b64, audio_rate]}` | `engine`: `vl` (default, 2–5 s) · `ocr` (fast lines, printed simplified only) · `best` (Gemini; cachy forwards to Cloud Run). Falls back automatically; `engine` says what answered |
| `POST /stream/start?lang=auto&tgt=es` | — | `{sid, rate:16000, chunk_ms:1000, idle_timeout_s:60}` | live subtitles |
| `POST /stream/chunk?sid=…` | raw PCM16 mono 16 kHz (≈1 s = 32 000 bytes) or a WAV | `{seq, partial, final:[{text, lang, tgt_text, dur_s, ms}], open_s, lang, ms}` | show `partial` as the grey line, append each `final` |
| `POST /stream/stop?sid=…` | optional last chunk | same, flushes the open segment, frees the sid | |

Phrasebook SD pack: copy the pack's `papp/` folder onto the TF card root (merges with `/sdcard/papp`).
`papp/tts/%08x.wav` are named `fnv32("<lang>|<text>")` — the firmware's own speech-cache key — so v0.2.0
already plays the built-in phrases and places offline. `papp/phrasebook/index.json` holds every entry
(`id, cat, zh, py, es, en, audio_zh, audio_es`) and the categories; `MANIFEST.sha256` checks with
`cd /Volumes/PAPP/papp && shasum -a 256 -c phrasebook/MANIFEST.sha256`.

## Measured 2026-11-07 23:2x CST — `tools/bench_v2.py` from the MacBook (desktop client), Toluca

Client round-trip in ms (cachy over Tailscale · Cloud Run us-central1, warm). "hit" = server cache.

| Endpoint | cachy :8787 | Cloud Run | Notes |
|---|---|---|---|
| `GET /health` | 9 | 143 | v0.2.0 both |
| `/tts` first call (miss, NVIDIA Magpie) | 372–963 | 593–876 | the 2–7 s seen on the Tab5 earlier was **not reproduced** from cachy (direct Magpie 0.35–0.94 s, 4 calls); repeats are cache hits → 3–6 ms cachy, ~190 ms Cloud Run (RTT) |
| `/translate?pair=es,zh` es speech | 881 (nmt+tts cached) | 2148 (asr 637 · nmt 697 · tts 523) | detected es → zh ✅ |
| `/translate?pair=es,zh` zh speech | 393 (cached) | 1171 | detected zh → es ✅ |
| `/translate?src=auto&tgt=zh` en speech | 378 | 1348 | detected en ✅ "很高兴见到你。这是我的翻译。" |
| `/translate?src=en&tgt=es` (v1 path) | 1112 | 1419 | unchanged contract |
| `/translate-text?src=auto&tgt=zh` | 6 (cached) · 802 cold | 929 | "USB-C 电缆的价格是多少?" |
| `GET /fx` | 2 | 212 | CNY>MXN 2.7878 (open.er-api.com, 6 h cache, disk-persisted) |
| `/summarize` (10 s voice note) | 1409 (asr 682 · llm 725) | 1819 | nemotron-3.5-lightning-30b-a3b |
| `/ocr-translate engine=ocr` | 1277 | 1597 | Hongqiao metro sign: 8 lines, "虹桥火车站 = Estación de tren de Hongqiao" |
| `/ocr-translate engine=vl` | 5910 | 3472 | nemotron-3.5-super-vl: 6 items |
| `/ocr-translate engine=best` | 5041 (forwarded to Cloud Run) | 6239 | Gemini 3.5 Flash: 9 items, best quality |
| `/stream/chunk` (1 s chunks) | worst 962 | worst 452 | zh speech → final "你好,请问洗手间在哪里?" + es "¿Dónde está el baño, por favor?" |

OCR quality on 5 real Wikimedia Commons signage photos (Beijing street sign, Shenzhen Huanggang border sign,
Shanghai Hongqiao metro sign, HK traditional shop sign, a cafeteria): Gemini read all of them right including
the traditional HK sign; the NVIDIA VLM was right on the simplified signs and invented two characters on the HK
sign (田園翠翠); nemotron-ocr-v2 is exact on printed simplified text, misses stylised/traditional. Hosted
PaddleOCR returned garbage for Chinese (Latin-only model) and is not used. No STOP: quality is good on real signage.

Upstream notes: NVIDIA LLMs `nemotron-nano-3-30b-a3b`, `nemotron-3-super-120b-a12b`, `mistral-nemo-12b`,
`gemma-3-12b-it` answer 404 "Function not found for account" for this key; `gemma-4-31b-it`, `gpt-oss-20b`,
`deepseek-v4.1-flash` timed out. Riva streaming TTS (`SynthesizeOnline`) fails on the hosted Magpie function —
batch only. Gemini 3.x on Vertex answers only on `locations/global` (us-central1 = 404).
Cloud Run calls Gemini with its attached SA `papp-run@espbuilder` (role `aiplatform.user` on papps-513907) —
no key file anywhere; cachy has no Google credentials and forwards `engine=best` to Cloud Run.

## v0.2.1 (lane T5-S, 2026-11-08) — conversation mode translates what people MEAN

Additive only; every v1/v2 request the Tab5 sends is unchanged.

| Change | Contract |
|---|---|
| `GET /health` | adds `"commit"` (git short hash baked at build) — compare cachy vs Cloud Run; `version` 0.2.1; features `mt-llm`, `asr-alt` |
| `/translate?pair=…` (default `mt=llm`, T5 21c3754) | for **zh** speech a second recognition (NVIDIA canary-1b-asr) runs **in parallel** with whisper; the MT LLM (nemotron-3.5-super-vl) sees both + a glossary. JSON adds `asr_alt`, `mt`; `format=wav` adds headers `X-Papp-Mt`, `X-Papp-Asr-Alt` (URL-encoded) |
| `/translate?…&nocache=1` | bench only: MT/TTS cache reads miss (writes kept). The Tab5 should never send it |
| `/summarize` | transcript passes deterministic spelling fixes (`Eric/Erik→Erick`, `LM→LLM`, env `PAPP_ASR_FIXES`); super-vl first (glossary); one retry if a field is not in `out` |

Why (measured on cachy, 11-08, `tools/probe_asr_zh.py`, `tools/probe_mt.py`):
- whisper-large-v3 zh hears z16-2 as `这个开发版…你卖10个`; canary-1b-asr zh-CN hears `开发板…你买十个` but is worse on everything else
  (m16 CER 0.35 vs whisper 0.12) → canary is only a hint. Whisper ignores `custom_configuration` prompts (identical output);
  hosted `parakeet-ctc-0_6b-zh-cn` answers INVALID_ARGUMENT "Unavailable model" → no zh hotword boosting on NVCF today.
- super-vl with one transcript: "Si me **vende** diez" 4/4; with both hypotheses: "placa de desarrollo… Si **compra(s)** diez" 2/2 (probe)
  and in each server bench run where the first rep is printed (cachy ×2, Cloud Run ×1). lightning given two hypotheses echoed "ASR A/B" or "Soy un modelo de
  lenguaje" → lightning only ever gets the single-transcript prompt, and `_mt_sane()` rejects echoed/annotated replies.

### A/B — `/translate?pair=es,zh&format=wav&nocache=1`, 9 clips (z16-1..4 zh, demo d1..d5) × 3, `tools/bench_v2.py --clip`

| Target | mt | client median | p90 | server median asr · mt · tts |
|---|---|---|---|---|
| cachy LAN 192.168.1.5 (from the MacBook) | **llm** | **1851 ms** | 2963 | 541 · 706 · 642 |
| cachy LAN | nmt | 1467 ms | 1618 | 528 · 330 · 584 |
| Cloud Run (from cachy, 7 clips × 2) | **llm** | **1875 ms** | 2165 | 407 · 524 · 536 |
| Cloud Run | nmt | 1539 ms | 1771 | 379 · 314 · 484 |

mt=llm costs ~0.35–0.4 s median over NMT and fixes meaning: z16-2 nmt "¿Cuánto cuesta vender 10 copias de este producto?" →
llm "¿Cuánto cuesta esta placa de desarrollo? Si compra diez, puedo darle un descuento."

### `/summarize?out=es` acceptance (`tools/eval_summarize.py`) — m16 (1:45 zh meeting) + EN note + DE note (Magpie voices)

Before (cachy 21c3754): 0/3 — "Eric"/"Erik", "módulo LM", German title on the DE note. After (3e51ffe): **3/3 PASS on cachy and on Cloud Run** —
no CJK, no German/English sentence, actions plain strings with deadlines as said ("Erick: enviar documentación técnica antes del 5 de
diciembre", "…12 de diciembre a las 3 p. m. hora de Beijing"), "Erick"/"LLM" right. Residual: the DE note's ASR heard "OEE-Auswertung"
as "UE-Auswertung", and the title keeps that German compound.

## v0.2.2 — Traceability Cell `/scan` + `/line/*` (T5, 2026-11-10; contract = N4 proposal §3, AGREEd 11-09)

Same bearer. SQLite `PAPP_CACHE_DIR/line.db` (persistent on cachy; Cloud Run not deployed with it yet — LAN/Teleport only).
Code: `server/papp/line.py` (ported from T5-I's `satellites/line/tools/mock_line.py`, same replies without `"mock"`).

| Method · path | Body / query | Reply |
|---|---|---|
| `POST /scan` | `{code, station, operator?, kind:"qr"\|"rfid", result:"ok"\|"scrap"\|"rework", reason?, lot?, ts?}` | `{id, seq, station, lot, duplicate, fifo_warning, counts{good,scrap,rework}}`; 422 on missing fields |
| `POST /line/event` | `{station, type:"run"\|"stop"\|"fault"\|"andon"\|"andon_clear"\|"login"\|"logout"\|"kanban", operator?, reason?, text?, value?, unit?, count?}` | `{id, state, andon_open[]}`; andon `text` translated es/zh/en (NMT) |
| `GET /line/kpi?window=shift\|hour\|<iso>/<iso>&station=` | — | `{window, planned_s, run_s, stop_s, ideal_cycle_s, total, good, scrap, availability, performance, quality, oee, andon_open[{station,reason,es,zh,en,operator,ts}], top_reasons[{code,es,zh,en,count}], last_scans[10], sensors{station:{value,unit,ts}}}` |
| `GET /line/trace?code=` | — | `{code, lot, scans[], lot_scans[], events[]}` |
| `GET\|POST /line/config` | `{stations[], ideal_cycle_s, shift{start,end,breaks[]}, reasons{code:{es,zh,en}}}` | current config (persisted to `line-config.json`) |
| `POST /line/report?out=es\|zh\|en` | — | `{title, summary, bullets[], markdown, kpi}` (deterministic, no LLM) |
| `POST /line/reset` | `{confirm:"demo"}` | `{ok, kept:"line.db.<ts>"}` — old DB kept, nothing deleted |

OEE: A = run_s / planned_s, with planned time starting at the first event/scan of the window (a demo cell switched on
mid-shift is not penalised for the hours before) · P = ideal_cycle_s × total / run_s (cap 1) · Q = good / total.
12 default reason codes: S01–S05 (quality), D01–D05 (downtime), A01–A02 (help) — `GET /line/config`.
Measured on cachy 11-10: 6 scans over 12 s → A 1.0 · P 1.0 · Q 0.833 · OEE 0.833; andon "falta tornillo M3" → zh 缺少螺丝 M3 /
en missing screw M3; no bearer → 401; Tab5 Línea tile → `kpi HTTP 200`.

## 2026-11-10 23:4x CST — parity: cachy and Cloud Run both v0.2.2 / commit 9ecee58 (lane T5-S, Erick: "deploy the board there too")

- Cloud Run `papp` (espbuilder, rev papp-00004-qkz) now serves `/scan` and `/line/*` too. Verified: `GET /line/kpi` 200 (0.13 s),
  3 × `POST /scan` 200 → total 3 / good 3, then `POST /line/reset {"confirm":"demo"}` → total 0 (test data removed).
- **Cloud Run line data is demo-only:** SQLite in `/tmp` (in-memory), max 1 instance → consistent while the instance lives, gone
  after ~15 min idle (scale to zero). cachy keeps the persistent line.db. For a show on Cloud Run: run the satellites against it for
  the session, or set `--min-instances 1` for the demo hours (≈ cost of one always-on 1 vCPU / 1 GiB instance while set).
- cachy rebuilt with the commit stamp (was reporting 0.2.1 / "dev" since T5's /line build); rollback `papp:prev-20261111a`.


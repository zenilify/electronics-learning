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

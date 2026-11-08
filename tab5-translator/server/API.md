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

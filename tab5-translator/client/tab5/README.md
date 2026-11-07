# Tab5 client spec (ESP32-P4 · ESP-IDF + LVGL) — not built yet

Status 2026-11-07: **spec only**. The firmware does not exist yet; `../desktop/papp_client.py`
is the reference implementation — the Tab5 must send exactly the bytes it sends.

## The one HTTP call

```
POST {BASE}/translate?src={es|en|zh}&tgt={es|en|zh}&format=wav&rate=16000
Authorization: Bearer {PAPP_TOKEN}
Content-Type: audio/wav
Content-Length: {44 + n_samples*2}

<RIFF/WAVE header, PCM16, mono, 16000 Hz><samples little-endian>
```

Reply `200 audio/wav` (PCM16 mono at `rate`, standard 44-byte header) plus headers:

| Header | Content |
|---|---|
| `X-Papp-Src-Text` | recognised text, URL-encoded UTF-8 (show on screen) |
| `X-Papp-Tgt-Text` | translation, URL-encoded UTF-8 (show on screen — needs a CJK font) |
| `X-Papp-Timings` | JSON `{"asr":ms,"nmt":ms,"tts":ms,"total":ms}` |
| `X-Papp-Asr-Model` | which hosted ASR answered (fallback chain) |

Errors: `400` bad audio/lang · `401` token · `413` > 30 s audio · `502` NVIDIA upstream down
(show "servicio no disponible", keep the recording, offer retry).

## Endpoints, in the order the firmware tries them

| # | BASE | When it works |
|---|---|---|
| 1 | `http://100.69.244.21:8787` | Tab5 on a network routed into the tailnet (travel router with Tailscale / exit node) — **the China path** |
| 2 | `http://192.168.1.5:8787` | at home, Toluca LAN |
| 3 | `https://papp-138753081869.us-central1.run.app` | anywhere Google is reachable (not mainland China) |

Probe `GET {BASE}/health` with a 2 s timeout at boot and on Wi-Fi change; pick the first `200`.
The ESP32-P4 has no Tailscale client, so in China the Tab5 joins the **travel router's Wi-Fi**
(GL.iNet with Tailscale, subnet route / exit node into the tailnet); then path 1 is plain HTTP
inside WireGuard. Do not expose 8787 publicly to "fix" this.

## Firmware outline (ESP-IDF 6.x, M5Stack Tab5 BSP)

1. **UI (LVGL)**: two big buttons = push-to-talk for each side of the pair
   (`ES → 中文` and `中文 → ES`); a pair selector (es↔zh, en↔zh, es↔en); two text areas
   (source / translation) with a font that covers CJK (e.g. a subset of Noto Sans SC
   compiled with `lv_font_conv` — subset ~3500 common hanzi keeps it < 1.5 MB in PSRAM/flash).
2. **Record**: while the button is held, read the ES7210 mic codec via the BSP I2S at
   16 kHz / 16-bit / mono into a PSRAM ring (max 30 s = 960 KB). Release = stop.
3. **Send**: write the 44-byte WAV header + samples with `esp_http_client`
   (`esp_http_client_open(len)` + `esp_http_client_write` in 4 KB chunks — no multipart,
   no base64). Timeout 30 s. Token from NVS (provisioned once over USB serial, never in source).
4. **Receive**: `esp_http_client_fetch_headers`, read the 3 `X-Papp-*` headers (URL-decode),
   stream the body to PSRAM, skip the 44-byte header, play through the ES8388 speaker codec
   at the reply rate. Show texts while playing.
5. **Latency budget** measured server-side: 1.4–2.6 s warm for a 4–6 s utterance; first
   request after idle on Cloud Run adds a cold start (see top-level README). Show a spinner.

## Build check

Optional: add a `tab5-papp` row to `foundry/targets.tsv` (ESP-IDF family) once the app exists.

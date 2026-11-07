# PAPP — Tab5 pocket translator backend (es/en ↔ zh)

Push-to-talk on an M5Stack Tab5 → one HTTP POST of a 16 kHz WAV → speech recognition,
translation and speech synthesis on **NVIDIA hosted Riva / Nemotron Speech**
(build.nvidia.com, gRPC `grpc.nvcf.nvidia.com:443`) → the Tab5 plays the translated WAV.
Built 2026-11-07 (lane L4) for the China trip (Espressif 20 Nov, M5Stack 26 Nov).

```
Tab5 ──HTTP (Tailscale / LAN)──▶ cachy :8787  ┐
     └─HTTPS (outside China)───▶ Cloud Run    ┴─ FastAPI papp ─gRPC/TLS─▶ NVCF
                                                   ASR  parakeet / whisper (fallback chain)
                                                   NMT  riva-translate-1_6b (regional codes)
                                                   TTS  magpie-tts-multilingual
```

| Path | Directory |
|---|---|
| service (FastAPI, pipeline, NVCF lookup) | `server/papp/` |
| container + cachy compose + systemd user unit + Cloud Run script | `server/Dockerfile`, `server/deploy/` |
| probes and tests | `server/tools/` (`probe_nvcf.py`, `probe_asr.py`, `smoke_pipeline.py`, `e2e.sh`) |
| reference client (the Tab5 mirrors it) | `client/desktop/papp_client.py` |
| Tab5 firmware spec | `client/tab5/README.md` |

## API

`GET /health` (no auth) · `POST /translate?src=es&tgt=zh[&format=wav|json][&rate=16000]`
with `Authorization: Bearer <token>` and the WAV as a raw `audio/wav` body (or multipart
field `audio`) · `POST /tts?lang=zh` `{"text": "..."}` → WAV. Full contract in
`client/tab5/README.md`. Directions: any pair of es, en, zh.

## China caveat — why cachy is the primary

Google services (including `*.run.app`) are usually blocked in mainland China. NVIDIA's
NVCF endpoint is reached **from cachy in Toluca**, not from the Tab5, so in China the Tab5
only needs a path to cachy: join the travel router's Wi-Fi (Tailscale on the router,
subnet route/exit node into the tailnet) and use `http://100.69.244.21:8787`. Cloud Run is
the fallback outside China and when cachy is down/asleep. Port 8787 is published only on
`192.168.1.5` (LAN) and `100.69.244.21` (Tailscale) — never 0.0.0.0, never forwarded.
If cachy sleeps (`sleep-cachy`), the translator is down until `wake-cachy`.

## Run it

```bash
# cachy (build + start; survives reboot via systemd --user, linger on)
cd ~/papp/src && docker build -t papp:latest .
cp deploy/papp.service ~/.config/systemd/user/ && systemctl --user daemon-reload
systemctl --user enable --now papp.service
# secrets: ~/.config/zenilify/nvidia_build.env (API_KEY=…) and ~/.config/zenilify/papp_token,
# both 0600, bind-mounted read-only. The token was generated with python secrets (43 chars).

# test (from cachy server/, after tools/smoke_pipeline.py has produced testaudio/*.wav)
CLIENT=~/papp/client/desktop/papp_client.py tools/e2e.sh http://100.69.244.21:8787 2
# from the MacBook, token over stdin only:
ssh cachy cat .config/zenilify/papp_token | client/desktop/papp_client.py \
  --url http://100.69.244.21:8787 --token-file - --src es --tgt zh in.wav --play

# Cloud Run (espbuilder / us-central1), from cachy server/:
deploy/cloudrun.sh infra|secret|image|deploy|all
```

Cloud Run: service `papp`, min 0 / max 1 instance, concurrency 4, 1 vCPU / 512 MiB,
request-based CPU, SA `papp-run@espbuilder` with **only** `secretAccessor` on secret
`papp-l4` (landmine 312). The secret holds `API_KEY=` and `PAPP_TOKEN=` and is mounted
as a file (`PAPP_SECRETS_FILE`). Ingress is public (`allUsers` invoker) because the Tab5
cannot mint Google ID tokens; the app enforces the bearer (401 without it — verified).
Alternative if Erick wants IAM-only: `--no-allow-unauthenticated` and a small token-minting
proxy — not worth it for an ESP32.

## Measured latency (2026-11-07, synthetic Magpie speech, 4.4–5.7 s utterances)

| Path | Warm end-to-end (client RTT) | Notes |
|---|---|---|
| direct pipeline on cachy | 2.0–4.2 s | ASR 0.5–2.4 s · NMT 0.36–0.46 s · TTS 0.8–1.6 s |
| cachy via Tailscale IP | 1.8–2.7 s | 12 calls, all 6 directions |
| Cloud Run | 1.4–2.7 s | US region sits next to NVCF |
| first es call after a restart | ~11 s | `parakeet-ctc-0_6b-es` dead → 8 s deadline → fallback; then 5 min cooldown |

Hosted-function health on 2026-11-07 01:20 CST: `parakeet-ctc-0_6b-es` and
`parakeet-ctc-0_6b-zh-cn` listed ACTIVE but answered `DEADLINE_EXCEEDED "failed to establish
link to worker"` after ~31 s. The fallback chain (`parakeet-1_1b-rnnt-multilingual-asr`,
`whisper-large-v3`) carried es and zh. **ACTIVE in the functions list does not mean a worker
is up.** `GET /health` shows which functions are cooling down.

Quality notes: the RNNT model writes `dónde¿`; NMT turned `早上好` (no punctuation from
whisper) into "Buenas noches" once — es→zh and en↔zh were correct.

## Costs

| Item | Fixed / month | Variable |
|---|---|---|
| Artifact Registry `papp` (one ~71 MB image) | ≈ 0 (inside the 0.5 GB free tier; $0.10/GB-month above it) | — |
| Secret Manager `papp-l4` (1 version) | 0 (6 versions free) | access ops free < 10k/month |
| Cloud Run `papp` (min 0) | **0 when idle** | ~2 s × 1 vCPU / 512 MiB per call — far inside the monthly free tier |
| Egress (~170 KB reply) | — | free tier 1 GB/month North America |
| NVIDIA hosted API | — | build.nvidia.com key (free credits / rate limits, not GCP) |
| cachy | electricity only | — |

After 9 Nov (trial ends) this bills the Developer Program credits: expected ≈ 0 MXN/month
idle; keep `min-instances 0`. To remove everything: delete service `papp`, repo `papp`,
secret `papp-l4`, SA `papp-run` (Erick's go first).

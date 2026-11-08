# PAPP offline brain: the translator API on the MacBook, no internet needed

ROADMAP §3, layer 2. The MacBook (Apple Silicon) serves the **same contract** as cachy and Cloud Run
(`GET /health`, `POST /translate`, `/translate-text`, `/tts`, `/fx`, same bearer token, same `X-Papp-*`
headers). It announces itself over **mDNS as `_papp._tcp`**, so the Tab5 can list it as endpoint 0.
Built 2026-11-07 by lane T5-B (`claude-1b`).

```
Tab5 ──Wi-Fi (phone hotspot, no mobile data)──▶ MacBook :8787  (_papp._tcp, TXT backend=offline-brain)
        ASR  mlx-whisper large-v3-turbo (MLX, Metal)
        MT   Qwen3-8B-Instruct 4-bit (mlx-lm, greedy, thinking off)   ← won the 30-sentence bake-off
        TTS  Piper zh_CN-huayan / es_MX-claude / en_US-lessac (ONNX)  ← D1 a (Erick 11-07)
```

## Use it

```bash
./setup.sh                 # once, ONLINE: venv in ~/papp-brain/.venv + ~9 GB of models in the HF cache
./start.sh                 # foreground; HF/transformers forced offline; mDNS on; 0.0.0.0:8787
./start.sh --airgap        # same, inside macOS sandbox-exec with every outbound IP connection denied
PAPP_MT=qwen25 PAPP_TTS=say ./start.sh   # alternatives: MT qwen3|qwen25|nllb · TTS piper|say
```

Token: the same bearer as cachy, in `~/.config/zenilify/papp_token` (0600). Copy it over ssh stdin,
never through chat or argv. `setup.sh` shows the command. `start.sh` refreshes `cache/fx.json` while it is
online; offline, `/fx` serves the last snapshot and adds `stale_s`.
Stop: Ctrl-C in the foreground, or kill the PID listening on :8787 (`lsof -tiTCP:8787 -sTCP:LISTEN`).
`dns-sd` deregisters on a clean shutdown (checked: `pgrep` returns nothing after the kill).

## Measured 2026-11-07 (MacBook M4 Max 64 GB · cachy cloud path for comparison)

### MT bake-off: `eval/mt_bench.py`, 30 sentences in `eval/sentences.tsv` (references written by hand), chrF

| Engine | es→zh | zh→es | en→zh | zh→en | median latency |
|---|---|---|---|---|---|
| cloud riva-translate-1_6b (production) | 42.0 | 66.5 | 47.7 | 71.2 | 0.26–0.28 s |
| NLLB-200 distilled 600M | 30.9 | 56.6 | 39.2 | 62.1 | 0.34–0.39 s |
| Qwen2.5-7B-Instruct 4-bit | 51.5 | 63.1 | 48.9 | 72.0 | 0.61–0.71 s |
| **Qwen3-8B 4-bit (default)** | **52.1** | **70.6** | **59.6** | **70.9** | 0.71–0.84 s |

Meaning errors are what decided it, not just chrF (`eval/results/mt_*.tsv`):
- **cloud** es→zh turned *¿Cuánto cuesta esto?* into 什么是"我"的意思, *La cuenta* into 账户, *No como cerdo* into 不是猪肉.
- **Qwen2.5** zh→es turned 我对花生过敏 (peanut allergy) into *almendros*.
- **Qwen3** made none of these mistakes.

### End to end (speech in → speech out): `eval/e2e_bench.py`, 30 Magpie-synthesised WAVs per direction

| Backend | Dir | client RTT median (max) | ASR / MT / TTS median | ASR CER | chrF |
|---|---|---|---|---|---|
| cloud via cachy (Tailscale) | es→zh | 1.32 s (10.3 s) | 0.44 / 0.28 / 0.58 s | 0.008 | 38.0 |
| cloud via cachy (Tailscale) | zh→es | 1.17 s (2.7 s) | 0.42 / 0.28 / 0.43 s | 0.046 | 61.3 |
| brain, macOS `say` | es→zh | 2.82 s (3.7 s) | 0.29 / 0.43 / 2.03 s | 0.023 | 44.9 |
| brain, macOS `say` | zh→es | 3.09 s (3.8 s) | 0.31 / 0.52 / 2.20 s | 0.042 | 66.7 |
| **brain, Piper** | es→zh | **1.67 s** (3.1 s) | 0.50 / 0.63 / 0.44 s | 0.023 | 44.9 |
| **brain, Piper** | zh→es | **1.91 s** (3.2 s) | 0.42 / 0.77 / 0.76 s | 0.042 | 66.7 |

The brain translates better than the cloud on both directions and adds about 0.4–0.7 s of latency.
The inputs are synthetic speech, not a real voice on the Tab5 mic, so real-mic ASR is still unmeasured.
Known weak spot: product codes said aloud ("ESP32-P4" came out as 波茨瓦那普拉 in the ASR).

## Offline proof (what was verified, and how)

| Check | Result |
|---|---|
| Probe can detect internet (control, no sandbox) | `curl https://huggingface.co` → 200 |
| Same probe inside `airgap.sb` | curl rc 7; python `connect(8.8.8.8:53)` → PermissionError |
| Server started with `./start.sh --airgap` | engines ready 6.8 s; the FX refresh inside it failed (fx.json mtime unchanged), so the sandbox was in force |
| Round trip from cachy over LAN to `macbook.local:8787` | es→zh and zh→es, 3/3 each, correct texts, RTT median 3.2 s / 1.8 s |
| mDNS seen from cachy | `avahi-browse -rpt _papp._tcp` → `macbook.local;192.168.1.234;8787;"path=/" "backend=offline-brain" "v=1" "prio=0"` |

**Not verified yet:** the link-level test (Mac on the S26U hotspot with mobile data OFF). The sandbox
proves the *process* needs no internet. It does not prove the Wi-Fi/hotspot path. D2 a: rehearse it
with the Tab5 before the 17 Nov freeze.
From cachy, `macbook.local` resolved to IPv6 first, and the first call took about 2.8 s longer than its
server time. The Tab5 should connect to the IPv4 A record from the mDNS answer.

## Files

`brain/app.py` (API, mDNS, copied from `server/papp/app.py`, not imported) · `brain/engines.py` ·
`start.sh` · `setup.sh` · `fetch_models.py` · `airgap.sb` · `requirements.txt` · `eval/` (sentences,
benches, results). Venv, models and test WAVs live in `~/papp-brain/` and the HF cache, outside git.

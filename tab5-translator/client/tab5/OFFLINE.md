# PAPP offline mode — options and recommendation (lane T5, 2026-11-07)

**Today PAPP needs the internet for every live turn.** The Tab5 records and plays, cachy (or Cloud Run) relays,
and speech recognition, translation and TTS run on NVIDIA's hosted models. The Tailscale and LAN paths reach
cachy without internet, but cachy itself needs it. With no network, only demo mode works (5 built-in turns).

Tags: **[M]** measured · **[S]** vendor docs (links below) · **[H]** hypothesis, measure before relying on it.

## The options

| Option | What it can do | Verdict for the M5Stack visit |
|---|---|---|
| **Unit ASR (CI-03T)** | Fixed command words only: up to 300, 42 English ones preloaded, zh/en/ja/ko; UART 115200 on Port C; speaker + AEC on board [S] | No free speech and no Spanish → at most an "offline phrasebook" (recognised phrase → pre-rendered translation from flash/TF). Cheap trick, low wow |
| **NVIDIA models on a TF card, run by the ESP32-P4** | The TF card only adds storage. Parakeet/Whisper-class ASR needs hundreds of MB of working RAM and billions of operations per second; the P4 has 32 MB PSRAM and no NPU [M/S] | **Not feasible.** ESP-SR on the P4 does command words, not dictation |
| **Module LLM / LLM630 Compute Kit (Axera AX630C, 3.2 TOPS, 4 GB, 1.5 W)** | StackFlow models: Whisper tiny/base/small, SenseVoice-small, Qwen2.5 0.5/1.5/3B, Qwen3-0.6B, **MeloTTS English/Chinese/Spanish**, KWS, VAD [S] | **Recommended.** All-M5Stack, fully offline. ASR + translation quality on zh⇄es is unmeasured [H] |
| **LLM-8850 (AX8850, 24 TOPS, 8 GB, M.2) or AI Pyramid (AX650)** | Same StackFlow packages (`-axcl` / `-ax650`) plus bigger models (Qwen3-1.7B, CosyVoice2) [S] | Stronger, but the LLM-8850 needs a Linux PCIe host (Pi 5 / x86) → a second box on the table |
| NVIDIA models on Axera | Possible only after conversion to `.axmodel` with Axera's toolchain [S: "market models cannot be used directly"] | Research project, not for 26 Nov |

## Recommended architecture — "PAPP Offline Brain", no firmware protocol change

```
            ┌──────────── no internet needed ────────────┐
 Tab5 ──Wi-Fi──▶ LLM630 Compute Kit (AX630C)               │
 (P4 + C6)       tiny HTTP shim: POST /translate (same API) │
                 → StackFlow: Whisper-small → Qwen2.5-1.5B → MeloTTS (zh/es/en)
            └─────────────────────────────────────────────┘
 Tab5 endpoint order becomes: 0 local brain → 1 Tailscale → 2 LAN → 3 Cloud Run (NVIDIA quality when online)
```

- The Tab5 firmware keeps the exact contract (`POST /translate`, raw WAV in, WAV + `X-Papp-*` headers out). Adding
  the brain = one more URL in `papp_http.c` plus a status-bar label "Offline brain".
- The shim on the LLM630 Kit (Linux, Wi-Fi/Ethernet) calls StackFlow over its local socket and reuses
  `server/papp`'s HTTP layer. Same token check.
- Module LLM (the M5-Bus/UART version) works too, but then the Tab5 must speak StackFlow JSON over UART. That's more
  firmware work, and whether the Tab5's back connector matches M5-Bus is not verified [H].
- Story for M5Stack: *"Espressif P4 terminal + M5Stack AX630C brain, all M5Stack hardware, zero cloud — and when
  there is internet, it upgrades itself to NVIDIA's models."*

## Plan to 26 Nov (if the hardware is on hand)

1. Confirm what's on hand: Unit ASR (CI-03T) vs Module LLM / LLM630 Kit (only the CI-03T is named so far).
2. LLM630 Kit: `apt install` the StackFlow packages (whisper-small, qwen2.5-1.5B, melotts zh/es/en). Then measure
   latency per stage and quality on the 5 demo phrases + 20 real ones.
3. Shim (~150 lines Python) exposing `/health` + `/translate` → Tab5 endpoint 0 → `tools/flash.sh bench`.
4. Go/no-go on 18 Nov: median turn ≤ 5 s and ≥ 4/5 understandable to a native speaker → demo it. Otherwise cloud
   path + demo mode.

Sources: [Module LLM](https://docs.m5stack.com/en/module/Module_LLM) ·
[StackFlow model list](https://docs.m5stack.com/en/stackflow/models/list) ·
[MeloTTS-Spanish](https://docs.m5stack.com/en/stackflow/models/melotts-spanish) ·
[Unit ASR](https://docs.m5stack.com/en/unit/Unit_ASR) ·
[LLM-8850 (CNX)](https://www.cnx-software.com/2025/10/03/m5stack-llm-8850-card-an-m-2-m-key-ai-accelerator-module-based-on-axera-ax8850-24-tops-soc/) ·
earlier lab scope: home-lab `docs/m5stack/tab5-translator-scope-2026-10-05.md`.

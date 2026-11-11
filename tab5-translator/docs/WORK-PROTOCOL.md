# PAPP / Tab5 translator — work protocol (IP record + where things run)

Owner of the project and of all work below: **Erick Zenil (Zenilify)**. Every lane was opened, scoped and steered by Erick in
chat; decisions are logged with his answers (D-codes). Code and documents were written by Claude sessions acting on his
instructions (AI-assisted authorship: every commit carries `Co-Authored-By: Claude Opus 5.5` or `Claude Fable 5.2` and a
`Claude-Session:` trailer, so each change is attributable to one session and one lane). Git history in
`zenilify/electronics-learning` (path `tab5-translator/`) is the primary, timestamped evidence; mailboxes and handoffs in
`zenilify/mission-control/claude/{mail,handoffs}/` record the decisions.

Compiled 2026-11-10 18:3x CST by lane T5-S (session `claude-45 [1f352f]`) from: `git log -- tab5-translator` (53 commits),
home-lab `git log -- '*tab5*'` (29 commits), 7 handoffs, 6 mailboxes, and a live check of cachy, the MacBook and Cloud Run.
Regenerate the commit ledger with the command at the end. Times are CST (America/Mexico_City).

## 1 · Lanes (who did what)

| Lane | Session(s) · surface · model | Dates | Scope (OWN) | Delivered | Evidence |
|---|---|---|---|---|---|
| Pre-history: **tab5-banco** (ESPHome bench console) | several home-lab lanes (e.g. `bc7ee30e [cc]`) | 2026-09-21 → 11-02 | home-lab `esphome/tab5-banco*`, StickS3 bridge | Tab5 bring-up (P4 rev v1.3, ST7121 panel), bench console, HA chat page, Keyboard for Tab5, Casa map, Wi-Fi via StickS3/UTR; retired on this unit by **D-T5-2** (11-07) | home-lab `01d5b9f` … `ca4ee12` (29 commits) |
| Scope study | home-lab lane L65 | 2026-10-05 | `docs/m5stack/tab5-translator-scope-2026-10-05.md` | ZH/EN/ES translator scope, online MVP + offline options | home-lab `958694b` |
| **L4** — backend v1 | tower `01UigXmJ` (Cowork GCP sprint) | 2026-11-07 01:27 | `tab5-translator/server`, `client/desktop` | FastAPI on NVIDIA Riva (ASR→NMT→TTS), cachy container, Cloud Run `papp` (espbuilder), desktop client | `61e4a2d`, `b8a17df` |
| **T5** — Tab5 firmware, integrator / acting tower | `claude-3d [52c5ca]` · Cowork · Opus | 11-07 15:10 → 11-10 | `client/tab5/**`, the Tab5 (MAC 30:ED:A0:E2:EF:41), later also `server/` (/line), `satellites/xiao-cam` | PAPP firmware v0.1→**v0.4.0** (ESP-IDF 6.0.3 + LVGL 9): translator, C6 OTA to esp-hosted 2.12.13, home screen + apps (phrasebook, take-me-to, type&translate, converter, notes, conversation, camera OCR, live subtitles, Línea board), SD sync, MacBook brain via mDNS, XIAO back camera, /line server routes (v0.2.2) | 31 commits `c6b603d` … `36f76d8`; handoffs 11-07, 11-09 |
| **T5-S** — server v2 / v2.1 | `claude-98 [52c5ca]` (11-07), `claude-45 [1f352f]` (11-08 →) · Cowork · Opus | 11-07 22:53 → 11-10 | `server/**`, cachy `papp`, Cloud Run `papp` | v0.2.0: language ID, TTS/NMT cache, /summarize, /ocr-translate (3 engines), /fx, live-subtitle stream, 340-phrase SD pack (zh+es); v0.2.1: 2nd ASR hypothesis (canary) + LLM MT guard, summaries in the right language, /health commit; review sheets for the 4 dialog clips | 8 commits `da78b81` … `7c38709`; handoffs 11-07, 11-08 |
| **T5-B** — MacBook offline brain | `claude-1b [52c5ca]` · Cowork · Opus | 11-07 22:56 → 11-09 16:20 | `offline-brain/**` | Same API offline on the MacBook (Qwen3-8B, mlx-whisper, Piper TTS), mDNS `_papp._tcp`, airgap sandbox; ch.2 brain v0.2.0 (pair/auto, summarize, sd) | `36f0e6a`, `27a72b2`; handoffs 11-07, 11-09 |
| **N4** — Erick-voice pack, OCR eval, /line proposal | subagent of tower `01UigXmJ [cc]` · Claude Code | 11-08 01:24 → | `server/tools/phrasebook_voice.py`, `ocr_eval.py` | Erick-voice phrase pack (680 clips, **personal, cachy only**), OCR eval set (20 signs) → D-N4-1 engine policy, Traceability Cell API proposal (built later by T5) | `a583890`; handoff 2026-11-08-n4 |
| Tower phrase review | tower `01UigXmJ` | 11-08 00:15 | `server/phrasebook/make_review_sheets.py` | Native-speaker review sheets (Rain / Daniel / James) | `1fa60d7`; `1b3846d` (Daniel: 113 OK) |
| **T5-R** — remotes | `claude-ab [52c5ca]` · Cowork · Fable 5.2 | 11-10 | `satellites/PROTOCOL.md`, `remote-*`, `tools` | Remotes protocol v1 (UDP 47800), StickS3 + M5Dial firmware 0.1.0 → 0.1.2 | `87427d1`, `e5d0349`, `6ef3537`; mail t5-r.md |
| **T5-I** — Traceability Cell satellites | `claude-e4 [52c5ca]` · Cowork · Fable 5.2 | 11-10 10:47 → 13:10 | `satellites/line/**` | StampS3 sensor, AtomS3 Lite + QRCode2 scanner, Cardputer ADV + RFID2 badge → live OEE/Andon on the Tab5 "Línea" tile | `62a2bc0` … `4d91be9`; handoff 2026-11-10-t5-i |
| Related media lanes (not Tab5 code) | X7 (M5Stack deck), X9 / X4c (demo clips with the translator) | 11-08 → 11-10 | Zenilify-Consulting / Pictures | Showcase deck and videos that depict the translator | mission-control mail x7/x9 |

## 2 · Decisions that shaped the product (Erick)

D-T5-1 a OTA the C6 co-processor · D-T5-2 PAPP replaces tab5-banco on this unit · D-T5-5 c all apps + converter + voice
notes · D-T5-7 network layers (phones + UTR + MacBook) · D-T5-8 own native firmware, giltal launcher later · D-T5-3 LLM630
(James) + 2 × Module LLM Kit (Rain) for offline · D-N4-1 OCR default `vl`, `best` button · D-N4-2 Erick-voice pack on the SD
via Wi-Fi from cachy (follow-up 11-10: keep Wi-Fi, no USB) · D-T5S-1 Cloud Run stays in espbuilder · T5-B D1 Piper as
offline TTS · T5-I D1–D5 satellite hardware · T5-R D1 bench on home Wi-Fi, D2 47800/udp runtime-only.
Full wording: `claude/mail/t5*.md`, `n4.md`, handoffs.

## 3 · What runs where (live check 2026-11-10 18:3x CST)

| Where | What | State | Notes |
|---|---|---|---|
| **cachy** (192.168.1.5 / 100.69.244.21) | Docker `papp` on :8787 (LAN + Tailscale only), systemd --user `papp.service` | ✅ up, `/health` = **0.2.2 / 9ecee58** (rebuilt 11-10 23:4x by T5-S; was 0.2.1/"dev") | Rollback tags `papp:prev-20261107` … `prev-20261111a`, `v1-rollback` |
| cachy | `~/papp/cache` (TTS/NMT/fx, 51 MB) · `~/papp/phrasebook-pack-v2` (35 MB, mounted `/sdpack`) · `~/papp/voice-pack-erick` (39 MB, **personal voice**, mounted `/sdpack/voices`, never git) · `/cache/line.db` (Traceability Cell) | ✅ | Secrets `~/.config/zenilify/{papp_token,nvidia_build.env,t5-wifi}` 0600 |
| cachy | `~/t5/` firmware build mirror, logs, device backups (`backup/`, `backup/line`, `backup/remotes`), test audio | files | `~/t5/wt-t5i` git worktree left by T5-I |
| cachy firewall | 8788/tcp and 47800/udp were runtime-only openings | closed again | The reboot at 17:58 dropped them (current list has neither) |
| **Cloud Run** `papp` (espbuilder, us-central1) | image `papp:20261108-1256`, min 0 / max 1 | ✅ `/health` **0.2.2 / 9ecee58** (rev papp-00004-qkz, 11-10 23:4x) | `/scan` + `/line/*` deployed 11-10 (Erick) — line data demo-only (in-memory, resets after idle). Gemini `engine=best` via the service account. Also in espbuilder: `killswitch` service (cost guard, not PAPP) |
| **MacBook** | offline brain on :8787 (pid 45239, listening on all interfaces) | ✅ running | electronics-learning `offline-brain/` v0.2.0; ~7 GB RAM; stop with `kill $(lsof -tiTCP:8787 -sTCP:LISTEN)` |
| **Tab5** 30:ED:A0:E2:EF:41 | PAPP firmware v0.4.x (`36f76d8` tree), 1 TB TF "PAPP" with phrase pack + Erick-voice pack | in use | C6 esp-hosted 2.12.13 |
| Satellites | StickS3 + M5Dial (remotes fw 0.1.2) · StampS3 sensor 50:78:7D:CD:64:8C · AtomS3 Lite + QRCode2 50:78:7D:CE:8B:10 · Cardputer ADV + RFID2 9C:CC:01:E0:EA:68 · XIAO ESP32S3 Sense back camera | flashed | Factory dumps of the three S3 boards in cachy `~/t5/backup/line/` (sha256 in the T5-I handoff) |

## 4 · Personal data and third-party material (for the IP file)

- **Erick-voice pack** is a clone of Erick's own voice (VoiceStudio OmniVoice). It is kept on cachy and the Tab5 SD only:
  never in git, never in a bucket, never in the Cloud Run image (N4 handoff §1).
- Phrasebook Chinese/pinyin: written by T5-S (Claude) on 11-07; native-speaker review in progress (`server/phrasebook/REVIEWS.md`).
- Test images for OCR: Wikimedia Commons signage photos downloaded on cachy (`server/tools/fetch_testimages.sh`) — not committed;
  synthetic sign set by N4 (Noto CJK fonts).
- Cloud/model services used at runtime: NVIDIA hosted API (build key), Google Vertex Gemini (Cloud Run only),
  open.er-api.com (FX). Open-source stacks: ESP-IDF, LVGL, M5Stack BSP, FastAPI, NVIDIA Riva client, pypinyin, Noto Sans SC,
  Qwen3 / mlx-whisper / Piper (brain). **Their licences have not been checked in this protocol** — do that before publishing
  the code or shipping a product (open item P-1).

## 5 · Open items / divergences found while compiling

| # | Item | Owner |
|---|---|---|
| P-1 | Licence inventory of third-party components and models before any publication | Erick / a lane |
| P-2 | ✅ resolved 11-10 23:4x: cachy + Cloud Run both 0.2.2 / 9ecee58, /line on both (Cloud Run demo-only store) | T5-S |
| P-3 | Phrase review: sheets A (Rain) and C (James) out; dialog-clip review (`Documents/PAPP-phrase-review/dialog/`) not yet recorded as sent | Erick |
| P-4 | Leftovers kept (no deletions without Erick): `~/papp/phrasebook-pack/` (first run), `~/t5/wt-t5i`, `~/t5/line-mock.db` | Erick |
| P-5 | Session logs in the vault exist for T5-S, T5 and T5-B; T5-I wrote none | tower |

## 6 · Commit ledger (electronics-learning `tab5-translator/`, generated)

53 commits. Author field = git identity of the machine; Co-author = the AI model; Lane = from the `Claude-Session` trailer.

| # | Commit | Date (CST) | Lane | Co-author | Areas | Subject |
|---|---|---|---|---|---|---|
| 1 | `61e4a2d` | 2026-11-07 01:27 | L4 / tower 01UigXmJ (incl. N4) | Claude Opus 5.5 | .gitignore, README.md, client/desktop, client/tab5, server/.dockeri… | tab5-translator: PAPP pocket translator backend (es/en <-> zh) on NVCF Riva |
| 2 | `b8a17df` | 2026-11-07 01:47 | L4 / tower 01UigXmJ (incl. N4) | Claude Opus 5.5 | README.md | tab5-translator: record measured Cloud Run cold start (5.7 s, scale-to-zero ~15 min) |
| 3 | `c6b603d` | 2026-11-07 15:30 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5-translator: PAPP Tab5 firmware (ESP-IDF 6.0.3, LVGL 9) + Foundry target tab5-papp |
| 4 | `5560547` | 2026-11-07 15:52 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5-translator/tab5: boots on P4 rev v1.3 — LEDC backlight workaround, LVGL buffers in PSRAM, bench console |
| 5 | `fd6c198` | 2026-11-07 16:16 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5-translator/tab5: Wi-Fi robustness on the hosted ESP32-C6 (log reasons, retries, PS off, full scan) |
| 6 | `ebb4e3b` | 2026-11-07 16:17 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5-translator/tab5: README verification table + known issues (lane T5) |
| 7 | `23a3439` | 2026-11-07 17:06 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5-translator/tab5: C6 co-processor OTA (1.4.1 -> esp-hosted 2.12.13), SDIO 20 MHz, offline-mode plan |
| 8 | `dd4f63f` | 2026-11-07 17:12 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5-translator/tab5: pin the Tab5 Wi-Fi antenna switch (IO exp 0x43 P0) to internal |
| 9 | `3a5c8b8` | 2026-11-07 17:46 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5-translator/tab5: antenna select (NVS ant, console `ant ext/int`), console `scan`, README antennas + open SDIO issue |
| 10 | `94273b7` | 2026-11-07 18:33 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5-translator/tab5: TAB5-CAPABILITIES.md — ports (from Erick's photos + schematic), phone tether, 1 TB SD, camera, PAPPS options |
| 11 | `1ec0e8c` | 2026-11-07 20:16 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: ROADMAP to Shenzhen — apps, network layers, device roles, launcher decision |
| 12 | `166be46` | 2026-11-07 21:33 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5, server/papp | tab5 v0.2.0: PAPP OS home screen + SD card + phrasebook, take-me-to, type & translate, converter, settings; server /translate-text + /fx |
| 13 | `4e863a6` | 2026-11-07 21:33 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5 ROADMAP: v0.2.0 status, RFID2 + QRCode2 roles |
| 14 | `9026010` | 2026-11-07 22:21 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5 home: tiles use a flex column (name/sub no longer overlap), 'Diálogo' fits |
| 15 | `fb787ed` | 2026-11-07 22:52 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: Keyboard for Tab5 driver (HID 0x6D, Alt layer for á é í ó ú ñ ¿ ¡) + scan-first Wi-Fi over 6 slots; ROADMAP: UTR layer 0 |
| 16 | `da78b81` | 2026-11-07 23:12 | T5-S (claude-98, claude-45) | Claude Opus 5.5 | server/API.md, server/Dockerfile, server/deploy, server/papp, serve… | papp server v2 (T5-S): auto language ID + conversation pair, TTS/NMT disk cache, /summarize, /ocr-translate, /fx pairs, live-subtitle stream, phrasebook pre-render |
| 17 | `b250442` | 2026-11-07 23:18 | T5-S (claude-98, claude-45) | Claude Opus 5.5 | server/API.md, server/tools | papp v2 (T5-S): measured latency table (cachy + Cloud Run) + OCR quality notes in API.md; bench CA fallback; phrasebook parser fix |
| 18 | `9090d1f` | 2026-11-07 23:38 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: Wi-Fi picker in Ajustes (+ console wifi <slot>/auto, nets); picked network remembered and preferred when visible |
| 19 | `36f0e6a` | 2026-11-07 23:54 | T5-B (claude-1b) | Claude Opus 5.5 | offline-brain/README.md, offline-brain/airgap.sb, offline-brain/bra… | tab5 offline-brain (T5-B): same PAPP API on the MacBook, fully local + mDNS _papp._tcp |
| 20 | `1fa60d7` | 2026-11-08 00:15 | L4 / tower 01UigXmJ (incl. N4) | Claude Opus 5.5 | server/phrasebook | phrasebook: make_review_sheets.py — native-speaker review spreadsheets (one per reviewer, EN/ZH instructions, OK/FIX dropdown) |
| 21 | `35cdef2` | 2026-11-08 00:24 | T5 (claude-3d) | Claude Opus 5.5 | README.md, client/tab5 | tab5: 64 KB TCP windows (Teleport turn 9.9-23.6 s -> 2.2-3.4 s); ROADMAP Teleport verified + helper lanes §6; README/OFFLINE pointers |
| 22 | `e84c1a5` | 2026-11-08 00:34 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: fix reboot on opening Ajustes (landmine 322) — esp_hosted RPCs only from the net task; scans sized by ap_num; console 'key <k>' to inject keys |
| 23 | `1827a7b` | 2026-11-08 00:45 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5, server/deploy, server/papp | tab5: phrase pack sync over Wi-Fi + MacBook offline brain via mDNS |
| 24 | `842bc1b` | 2026-11-08 01:01 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: Point & Translate (camera) — SC202CS 1280x720 RGB565 preview, HW JPEG (50 kB in 18 ms), POST /ocr-translate engine=ocr/vl; photos kept on SD |
| 25 | `8cd8871` | 2026-11-08 01:20 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5 v0.3: voice notes, hands-free conversation, PPA camera preview |
| 26 | `a583890` | 2026-11-08 01:38 | L4 / tower 01UigXmJ (incl. N4) | Claude Opus 5.5 | server/tools | tab5 server tools: Erick-voice phrasebook pack (VoiceStudio) + OCR eval set/scorer (lane N4) |
| 27 | `21c3754` | 2026-11-08 12:01 | T5 (claude-3d) | Claude Opus 5.5 | server/papp | papp server: speech-aware LLM translation for conversation mode; voice-note summaries in the requested language |
| 28 | `6d9cbf8` | 2026-11-08 12:45 | T5-S (claude-98, claude-45) | Claude Opus 5.5 | server/Dockerfile, server/deploy, server/papp, server/tools | papp v0.2.1 (T5-S): second ASR hypothesis (canary zh) for the conversation-mode MT guard; glossary; summary names + language retry; /health commit |
| 29 | `fe079c6` | 2026-11-08 12:47 | T5-S (claude-98, claude-45) | Claude Opus 5.5 | server/papp, server/tools | papp v0.2.1 (T5-S): ?nocache=1 for benches (cache reads miss, writes kept); bench_v2 --clip mt=llm vs nmt A/B with per-stage medians |
| 30 | `f152b42` | 2026-11-08 12:51 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5 v0.3.1: Erick's UI notes — camera result screen, notes language choice + history, dialog history |
| 31 | `211ef80` | 2026-11-08 12:52 | T5-S (claude-98, claude-45) | Claude Opus 5.5 | server/papp, server/tools | papp v0.2.1 (T5-S): summaries on super-vl first (follows the glossary; lightning 0/3), glossary hints for ASR spellings (Eric/Erik, LM), German detected in the language check |
| 32 | `3e51ffe` | 2026-11-08 12:54 | T5-S (claude-98, claude-45) | Claude Opus 5.5 | server/papp | papp v0.2.1 (T5-S): deterministic ASR spelling fixes (Eric/Erik->Erick, LM->LLM; PAPP_ASR_FIXES) on transcripts before MT and summaries |
| 33 | `49ea036` | 2026-11-08 13:02 | T5-S (claude-98, claude-45) | Claude Opus 5.5 | server/API.md | papp v0.2.1 (T5-S): API.md — v0.2.1 contract additions, mt=llm vs nmt A/B (cachy LAN + Cloud Run), summarize acceptance |
| 34 | `7c38709` | 2026-11-08 13:23 | T5-S (claude-98, claude-45) | Claude Opus 5.5 | server/phrasebook | phrasebook: make_dialog_review.py — native-speaker check of the 4 conversation clips (z16), one xlsx per reviewer + m4a audio |
| 35 | `18fee8d` | 2026-11-08 13:59 | T5 (claude-3d) | Claude Opus 5.5 | satellites/xiao-cam | xiao-cam: PAPP back camera firmware (Seeed XIAO ESP32S3 Sense + OV3660) |
| 36 | `320c7aa` | 2026-11-08 14:07 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: back camera — XIAO ESP32S3 Sense (OV3660) over Wi-Fi, front/back toggle on the camera screen |
| 37 | `ab2ca3a` | 2026-11-08 14:07 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5 camera: cite landmine 326 (320c7aa's message says 323, which belongs to lane N3) |
| 38 | `5af3d75` | 2026-11-09 00:07 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5 v0.4.0: live subtitles, back-camera fallback, China time zone, OCR engine policy, brain-loss crash fix |
| 39 | `e1fbfd1` | 2026-11-09 00:10 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: Ajustes "Voz: PAPP / Erick" — es/en phrases from the card's Erick-voice pack (N4), zh stays Magpie |
| 40 | `db2292f` | 2026-11-09 00:13 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: "Línea · 产线" OEE/Andon board tile (Traceability Cell, N4 /line/kpi contract) |
| 41 | `1b3846d` | 2026-11-09 15:20 | T5 (claude-3d) | Claude Opus 5.5 | server/phrasebook | phrasebook: REVIEWS.md — Daniel's sheet B back, 113 x OK, 0 fixes; A (Rain) and C (James) pending |
| 42 | `27a72b2` | 2026-11-09 16:19 | T5-B (claude-1b) | Claude Opus 5.5 | offline-brain/README.md, offline-brain/brain, offline-brain/eval, o… | tab5 offline-brain v0.2.0 (T5-B ch.2): pair=/src=auto, /summarize, /sd/*, 404 for the rest |
| 43 | `d493ac2` | 2026-11-09 22:46 | T5 (claude-3d) | Claude Opus 5.5 | server/deploy | papp deploy: cachy-only /sdpack/voices mount for the Erick-voice pack (D-N4-2) — deployed on cachy 11-09 22:4x |
| 44 | `36f76d8` | 2026-11-10 10:53 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5 | tab5: SD sync pins cachy (LAN) when it answers — the MacBook brain only mirrors the phrasebook; Erick-voice pack synced (D-N4-2) |
| 45 | `87427d1` | 2026-11-10 11:00 | T5-R (claude-ab) | Claude Fable 5.2 | satellites/PROTOCOL.md, satellites/remote-common, satellites/remote… | satellites: PAPP remotes protocol v1 + StickS3/M5Dial firmware 0.1.0 + cachy tools (lane T5-R) |
| 46 | `e5d0349` | 2026-11-10 11:04 | T5-R (claude-ab) | Claude Fable 5.2 | satellites/PROTOCOL.md, satellites/tools | satellites: fake_remote.py (receiver test without hardware, selftest PASS vs udp_sink), key retry in provisioning, hello period 5 s in the spec |
| 47 | `62a2bc0` | 2026-11-10 11:11 | T5-I (claude-e4) | Claude Fable 5.2 | satellites/line | satellites/line: Traceability Cell satellites (T5-I) — §3 mock server, scanner (AtomS3 Lite + QRCode2), badge (Cardputer ADV + RFID2), sensor (StampS3) |
| 48 | `bb6a0f5` | 2026-11-10 11:45 | T5-I (claude-e4) | Claude Fable 5.2 | satellites/line | satellites/line: README (build, LIVE vs MOCK, cheat-sheet, draft storylines Rain/Daniel/James); QRCode2 UART rx=G5 tx=G6 as measured |
| 49 | `67d5a07` | 2026-11-10 12:14 | T5-I (claude-e4) | Claude Fable 5.2 | satellites/line | satellites/line scanner: ignore any code seen in the last 5 s (8-code memory) — a sheet of labels double-counted L2611A-005 |
| 50 | `6ef3537` | 2026-11-10 12:16 | T5-R (claude-ab) | Claude Fable 5.2 | satellites/remote-common, satellites/remote-dial, satellites/remote… | satellites remotes fw 0.1.2: never block on the USB console (landmine 336 - loop froze ~4 min after link), no LCD redraw between a press and its ack |
| 51 | `35d5e3f` | 2026-11-10 12:26 | T5 (claude-3d) | Claude Opus 5.5 | client/tab5, server/API.md, server/papp | papp v0.2.2: Traceability Cell /scan + /line/* (kpi, event, trace, config, report, reset) — built by T5 (N4 closed without shipping) |
| 52 | `1abe553` | 2026-11-10 12:34 | T5-I (claude-e4) | Claude Fable 5.2 | satellites/line | satellites/line: scanner button 400 ms lock-out (release bounce disarmed scrap); badge 'i' also scans the internal I2C bus |
| 53 | `4d91be9` | 2026-11-10 12:53 | T5-I (claude-e4) | Claude Fable 5.2 | satellites/line | satellites/line badge: RFID2 on PORT.A or on the internal G8/G9 bus (InWire over M5.In_I2C), hot-plug probe every 5 s |

Regenerate: `TZ=America/Mexico_City python3 tab5-translator/docs/make_protocol_ledger.py` (rewrites this section).

# PROGRESS.md — electronics-learning

Session-by-session log. **Newest entries at the top.** Claude reads this at
the start of each session and adds a new entry at the top at the end of
each session.

---

## 2026-11-11 — Tab5 overnight PDCA: camera power, charging, ESP-NOW bridge, live subtitles (lane T5, Claude Code on cachy)

Four unattended hours on the Tab5 translator (`tab5-translator/`). Full record with every measurement:
`tab5-translator/docs/PDCA-2026-11-11.md`. Firmware 0.4.0 → **0.4.2** overnight, **0.4.3** by noon and **0.4.4** in the afternoon;
0.4.3 is merged to `main` (PR #3, #4, #5).

**Done and measured**

- The XIAO back camera had no power: the Tab5 never switched on the 5 V of its USB-A port. On at boot now.
- The camera was also often on a different Wi-Fi than the Tab5. The Tab5 now reads the camera's address from the
  camera's own log over the USB cable and, if needed, joins the camera's network.
- The battery was never charged while PAPP ran (charger enable pin low). Enabled; charging at 0.32 A measured — but
  on cachy's USB port it only starts on some boots. Needs a look with the wall charger.
- New tools: console without reset, screenshots of the Tab5 screen over USB, a full flash backup that really is one.
- ESP-NOW: not possible on the Tab5's own radio (Espressif's esp-hosted has none). Built a small S3 bridge instead and
  measured it with the StickS3: 80/80 events acknowledged, median 24 ms. The cable from bridge to Tab5 is not built.
- SD sync now compares file contents (12 re-taken voice clips had been skipped because their size was unchanged).
- RainMaker command-line tools installed on cachy (`rmaker`, `rmaker-neo`); logging in is Erick's step.
- Frases now shows the 340 phrases of the SD card (16 groups) instead of 40 built-in ones; the sync skips unchanged
  files through a manifest (41 s → 9 s).
- **Most important, found late (03:48):** a software restart put the Tab5 into an endless boot loop — the Wi-Fi chip
  never answered again and the firmware restarted itself every 18 s. The screen-freeze watchdog added earlier the same
  night restarts that way, and so does a crash. The final build (c17) turns every restart into a reset of the whole
  chip. A 15-minute soak then showed how common the fault is: 20 of 57 CPU-only resets failed, and all 20 were
  recovered by the new whole-chip reset on the next boot.
- Internal RAM measured per app (`tools/ram_walk.py`): no leak, but the screens stay built. Frases gives its phrase
  cards back when you leave it (+20 kB).

**Same day, with Erick at the bench**

- The wall charger charges the pack (Ajustes: "cargando", +0.15 A at 8.38 V).
- Plugging USB power in restarted the Tab5. Cause: the supply dips for a moment and the chip's brownout detector (a
  guard that resets the chip when its voltage is too low) resets it; the Wi-Fi chip drops in the same dip. It depends
  on the port: through a USB-C-to-A adapter on a USB-A socket 7 of 7 plug-ins restarted it, USB-C to USB-C into a USB-C
  port 0 of 5. Firmware 0.4.3 shows in Ajustes why the Tab5 last started ("último arranque: …").
- The screen watchdog fired twice in real use (about 09:15) and the Tab5 came back by itself both times. What
  triggered it is not known.
- The MacBook can build the same firmware and flash the Tab5 (measured end to end), and PlatformIO on it builds both
  remotes and the bridge. The secrets (Wi-Fi list, remote key, full backups) stay on cachy — Erick's decision.
- **Live subtitles showed nothing** (Erick's test that morning). Cause, measured: the Tab5's microphone delivers speech
  at a level of 130–240 (on a scale where 32768 is the maximum) and the server's speech detector only reacted above
  400. Fixed on both sides in the afternoon — firmware 0.4.4 raises the level while subtitles run, server 0.2.3 judges
  loudness against the room's own noise — and checked without hands, using the Tab5's own loudspeaker as the voice.

**Things worth understanding (lesson material)**

- *High-Z output*: an I/O-expander pin can be "set high" and still drive nothing until it is switched to push-pull.
  That one detail explained both the dead USB port and the antenna switch that never switched.
- *snprintf returns what it wanted to write*, not what it wrote — `w += snprintf(...)` walks off the buffer on truncation.
- *A test that cannot fail proves nothing*: the local-model code review found 0 of 2 planted bugs, so its "0 findings"
  on the real files meant nothing.
- *There is more than one kind of reset.* `esp_restart()` on the ESP32-P4 restarts only the CPU; the SD/SDIO and USB
  controllers keep whatever state they were in. A watchdog reset of the whole chip starts them clean. The boot log
  names the kind in its first line (`rst:0xc` CPU only, `rst:0x9` whole chip by watchdog, `rst:0x17` over USB).
- *A handful of good runs proves little about an intermittent fault.* The same restart worked at 02:29, looped at
  03:48 and worked seven times in a row after 04:15 — I wrote "not reproducible". A soak of 57 resets found 20
  failures. Count before you conclude, and let the fix not depend on knowing the trigger.
- *Check that the preparation took effect before a hands-on test.* One of Erick's plug-in tests tested nothing: the
  console command that should have switched the brownout detector off had been lost, and I had not read the answer back.
- *The same firmware is not the same bytes.* Two builds of the same source differ in the compile time and in the
  checksums that cover it (about 70 bytes). `tools/same_firmware.py` compares everything else.
- *A lock file only pins what is in it.* `dependencies.lock` had no entry for one component, so every fresh clone
  chose a version again. A fresh clone that builds without changing a tracked file is the test.
- *A threshold in absolute units is a hidden assumption about the hardware.* "Speech is anything above 400" was
  tuned on studio-loud test clips (level 1300–2500). A different microphone, and the feature is dead. Compare with the
  room's own noise instead: "three times louder than the quiet moments".
- *A test that feeds perfect input proves the plumbing, not the feature.* The built-in clip test passed the whole
  time; it bypasses the microphone. The first real test was a person speaking.
- *Every route should log one line.* The subtitle routes logged nothing, so "the Tab5 never called" and "the server
  heard only silence" looked the same from the outside.

**Open** — see §4 of the PDCA file (ESP-NOW cable, RainMaker login, camera-freeze follow-up, who receives the access
token on a foreign Wi-Fi, internal RAM margin, cause of the restart fault).

---

## 2026-11-06 — Kaizen Firmware Foundry: whole fleet builds in Cloud Build (GCP sprint lane C, macbook)

New `foundry/`: four pinned builder images in Artifact Registry (ESP-IDF 6.0.3 +
microlink, ESPHome 2026.10.2, arduino-cli/Seeed SAMD/PlatformIO, NuttX RISC-V GCC)
and a `cloudbuild.yaml` matrix fed by `targets.tsv`. `stage.sh` ships git-tracked
files only (secret gate), `dummy_secrets.py` fills placeholders. First full run:
**21/22 OK** (all ESPHome nodes except stackchan-estudio, StickS3, Wio x3, M5 Core,
PaperS3, NuttX nsh + lvgl_demo for the Tab5) + QEMU boots the StickS3 image to
`app_main()`. Details and cost in `foundry/RESULTS.md`. No device was flashed.

### Next steps (start here next session)

1. Done same day: stackchan-estudio migrated to esphome-audio-stack v2026.11.0
   (home-lab 0284c5e, 31fa2ca), matrix 22/22. It now needs ESPHome >= 2026.10 to
   build: upgrade cachy's ESPHome before the next flash of the Stack-chan.
2. Pin `platform = espressif32@8.0.0` in the PaperS3 badge `platformio.ini`.
3. Optional: first NuttX contribution candidate = ESP32-C6 hosted Wi-Fi for the Tab5.

---

## 2026-09-25 — Lesson 00 written: protoboards & soldering roadmap (cockpit session, estudio)

Added `lessons/00-protoboards-and-soldering-roadmap.md`: an 8-phase roadmap
(bench + safety → breadboard fluency → first joints → headers on the XIAO
ESP32-S3 → INMP441/MAX98357 breakouts for the DIY voice satellite → a
perfboard AHT20+BMP280 node in a box → rework with wick/pump/858D → optional
KiCad PCB), each phase with a "done when" checkpoint, plus the safety card
and a glossary. Written against the real bench in `INVENTORY.md`.

- Corrects the standing "pending soldering iron purchase" note: the Weller
  station, 858D hot-air gun, 60/40 solder, flux, wick, pump and third hand
  have been on the bench since the 2026-07 CDMX purchases.
- Repo status: **sub-project of Home-Lab** from today (dashboard rows
  `🔌 Electrónica ·`; `repos.yml` in mission-control now maps it there).
- No hardware touched this session; Tasks 17/18 unchanged.

### Next steps (start here next session)

1. Task 17 (Wio accelerometer) and Task 18 (commit the Wio chapter) — unchanged.
2. Then **Lesson 00, phases 0–3** in one sitting (~3 h): bench, breadboard,
   30 practice joints, headers on two XIAO ESP32-S3 → first XIAO node on the
   Grove shield running ESPHome.
3. Then phase 5: the perfboard plant node (project 07, node #1).

---

## 2026-08-19 — Project focus reset: pure electronics + Home Assistant (ESPHome)

Cleaned house and locked the repo onto its real purpose: **hobby electronics —
dev boards, sensors, soldering, ESP32 — building sensors that report to Home
Assistant.**

### Accomplished

- **Removed the textile-KPI side-quest** entirely. It was never electronics;
  it only existed as a note plus a big Python data-analysis toolkit.
- **Repurposed the Python venv toward the real goal:** `requirements.txt` is now
  just **ESPHome** (was ~78 pandas/matplotlib/weasyprint packages). ESPHome is
  the standard way to get an ESP32 into Home Assistant, and its configs validate
  + compile on any computer — no board required.
- **Project 09 — ESP32 → Home Assistant sensor (ESPHome):** first HA-connected
  device. Exposes Wi-Fi signal, uptime, ESP32 internal temperature, and IP to
  Home Assistant over the encrypted native API. No external parts, so it builds
  clean as a "Hello, World!".
- Verified end-to-end in the cloud: `esphome config` → *Configuration is valid!*
  and `esphome compile` → *Successfully compiled program.* (912 KB ESP32 image).
- Added `secrets.yaml.example` pattern + git-ignored `secrets.yaml`/`.esphome/`
  so Wi-Fi passwords and keys never get committed.
- **Reconciled the hardware inventory** against the real bench audit: added the
  full detailed list as `INVENTORY.md` (source of truth) and rewrote the
  `CLAUDE.md` summary. Headlines: a large **ESP32-S3 fleet** arrived (devkits
  ×3 w/ ext antenna, +OLED ×2, D1-UNO ×2, Super Mini ×2, XIAO ESP32-S3 ×3), the
  full **M5Stack family**, **AHT20+BMP280 ×3** temp/hum/pressure sensors, and
  the **INMP441 ×5 + MAX98357 ×5** voice-satellite BOM. Dropped the phantom
  XIAO nRF52840 / ESP32-C3 (never in inventory).
- **Soldering is unblocked:** Weller station + LEFAVOR 858D hot-air rework +
  consumables verified on the bench.

### Gotchas / key decisions

- ESPHome via `!secret` needs a `secrets.yaml` next to the config; any valid
  values let `config`/`compile` pass without hardware.
- First `esphome compile` downloads the ESP32 toolchain (~2 min, network) into
  `~/.cache/esphome`; later builds are fast. Flashing (`esphome run`) still
  needs the physical board on USB.
- YAML/ESPHome joins the existing CircuitPython + Arduino/C++ tracks; it does
  not replace them.

### Next steps (start here next session)

1. Flash Project 09 to a real generic ESP32 DevKit and confirm auto-discovery
   in Home Assistant.
2. Swap in a real environmental sensor (BME280 / SHT31 over I2C).
3. Add a PIR motion `binary_sensor`; then a capacitive soil-moisture sensor for
   the plant-monitor idea.

### Deferred / blocked

- Project 07 plant sensors: capacitive soil-moisture still on import (the
  temp/hum/pressure half already arrived as AHT20+BMP280 ×3).
- Some parts live in Toluca, not with the CDMX bench (INMP441 ×5, MAX98357 ×5,
  screw-terminal shields, GPIO breakouts) — see `INVENTORY.md`.
- Zigbee smart-home devices need the Toluca coordinator; Shelly Gen4 units can
  run on WiFi/Matter meanwhile.

---

## 2026-08-01 — M5Stack Core (M5GO) onboarding: Project 08 (estudio, cowork)

First session after the smart-home + electronics merge into Home-Lab.
Onboarded the M5GO kit as the third board family in the repo (after CPX
and Wio Terminal).

### Accomplished

- Identified the kit: **M5GO IoT Starter Kit** (blue box, lego-compatible
  mounts, Grove Units: ENV, PIR motion, IR, RGB, angle).
- Core enumerates via CH9102 USB-serial bridge →
  `/dev/cu.usbserial-5A6D0199351` (unlike SAMD boards' native USB).
- **First Library Manager install** (milestone originally planned for
  Task 17): M5Unified 0.2.19 + M5GFX 0.2.26 dependency.
- Board setup: esp32 core 3.3.10 was already installed on estudio →
  board entry **"M5Core"**, no Boards Manager step needed.
- **Project 08 — M5Stack Hello:** hello screen + 3 button counters +
  battery % (M5GO base). Uploaded and verified: 272 KB compressed,
  hash verified, RTS auto-reset. Buttons increment exactly 1 per press.
- Concepts learned: `M5.update()` polling pattern vs raw
  `digitalRead()` (edge detection + debounce for free); esptool
  write → verify → hard-reset sequence; one-object board abstraction
  (M5Unified) vs per-peripheral libraries (TFT_eSPI).

### Gotchas / key decisions

- **Arduino IDE hang:** after the library install, the IDE started an
  optional esp32 3.3.11 core update that stalled >10 min ("Processing
  esp32:3.3.11", zero file/network activity). Fix: force quit, relaunch,
  dismiss the update. **Rule adopted: toolchain updates only at session
  START, never mid-session.** Core 3.3.10 works fine.
- Boards Manager shows a second ESP32 package ("Arduino ESP32 Boards
  2.0.18" — Arduino's fork). Harmless but remove someday to avoid
  selecting the wrong core.
- `~/Library/Arduino15/staging` cache = 3.3 GB on estudio — safe to
  clean for disk space.
- **Numbering:** projects 05–07 stay reserved for the protected learning
  track (05 soldering, 06 voice satellite, 07 plant sensor) per the
  Smart Home & Electronics MOC — the M5Stack took **08**.

### Next steps (start here next session)

1. **Task 17 — Wio accelerometer:** LIS3DHTR live X/Y/Z tilt on the LCD
   (Library Manager skill now already acquired).
2. **Project 05 — first soldering:** ESP32-S3 DevKit headers ×2 → MB-102
   (Weller station + consumables all on the bench).
3. **Buy INMP441 mic** for Project 06 (voice satellite) — confirmed not
   in inventory; batch with the mid-August sensor import if possible.
4. Idea parked: M5GO + ENV Unit as zero-solder ESPHome test mule; M5GO /
   Wio as HA desktop display (after the protected track).

### Deferred / blocked

- Project 07 sensors: capacitive soil + BME280/SHT31 on import, ETA
  ~mid-August.
- NAS mirror strategy for GitHub repos: unchanged.

---

## 2026-07-18 — Mac onboarding: repo review + encoding/doc fixes (estudio)

First session on the Mac Studio. Pulled the repo into PARA, did a full
project review, and fixed the three issues the analysis surfaced.

### Accomplished

- Repo now cloned and working at
  `/Volumes/HomeX/estudio/PARA/01-Projects/electronics-learning` (estudio).
- Full repo analysis: structure matches conventions; all 4 projects reviewed.
- Fixed `requirements.txt`: was UTF-16 + CRLF (PowerShell 5.1 `pip freeze >`
  trap) → re-encoded to UTF-8 + LF; 78 packages, pip-readable on any OS.
- Updated `README.md`: added projects 03–04, fixed "stey-by-step" and
  "Seed XIAO" typos.
- Fixed this file's header: entries are PREPENDED at the top (the old text
  said "appends at the end", contradicting CLAUDE.md).
- Committed and pushed as `c7d48a8`.
- Created the Obsidian side of the project: MOC + session log +
  Project Dashboard section in vault `SyncV2` (syncs to all devices).

### Key decisions

- On Windows, regenerate requirements.txt with
  `python -m pip freeze | Out-File -Encoding utf8` (never plain `>`).
- Left `c7d48a8`'s garbled commit message as-is — already pushed, harmless.

### Next steps (start here next session)

1. **Task 17 — Wio accelerometer:** LIS3DHTR live X/Y/Z tilt on the LCD;
   first Library Manager install (`Seeed_Arduino_LIS3DHTR`).
2. **Task 18 — Commit Wio Terminal chapter** after Task 17.

### Deferred / blocked

- Unchanged: soldering iron purchase (XIAO SAMD21 chapter), NAS mirror
  strategy, incremental MCP connector authorization.

---

## 2026-07-08 — Foundation, CPX chapter, Wio Terminal chapter (partial)

Big first day. Set up the whole toolchain from zero, completed the Adafruit
Circuit Playground Express onboarding, pushed to GitHub, pivoted the XIAO
SAMD21 chapter to Wio Terminal (unsoldered pins on the XIAO), and got two
Wio Terminal C++ projects running.

### Accomplished

- Set up `E:\electronics-learning` with Python venv (`.venv`).
- Installed and configured VS Code, Arduino IDE 2.3.10, Mu editor.
- **Project 01 — CPX Blinky:** CircuitPython on Adafruit Circuit Playground
  Express; RGB NeoPixel cycle. First working edit-save-run loop.
- **Project 02 — CPX Light Sensor:** reactive NeoPixels driven by onboard
  light sensor. Learned the input → decide → output pattern.
- Initialized Git repo at `E:\electronics-learning`.
- Cleaned up accidental Git repo at `C:\Users\ezeni\.git` (created when
  `git add .` was accidentally run from the wrong directory).
- Configured Git Credential Manager, generated GitHub PAT, cleared stale
  Windows credential entries.
- Pushed repo to `https://github.com/zenilify/electronics-learning`.
- Pivoted from XIAO SAMD21 (unsoldered pins, blocked) to Wio Terminal.
- Installed Seeed SAMD Boards package in Arduino IDE.
- Verified toolchain with built-in Blink sketch.
- **Project 03 — Wio Hello LCD:** color text on 320x240 LCD via TFT_eSPI.
  First C++ program, first `#include`, first custom colors and font sizes.
- **Project 04 — Wio Input Dashboard:** live joystick + top-button dashboard.
  First custom C++ function (`showButton`), first use of `INPUT_PULLUP` and
  `digitalRead()`. Sketch is written and uploaded; deep experiments
  (counter extension, floating-pin demo) are optional next-session
  exploration.
- Added `CLAUDE.md`, `PROGRESS.md`, and `.gitattributes` to the repo for
  the cross-device agent workflow.

### Key decisions

- CircuitPython for Adafruit boards and XIAO series (when soldered).
- Arduino/C++ for Wio Terminal and likely ESP32 boards later.
- One project per numbered folder under `projects/NN-short-name/`.
- GitHub account for this project: `zenilify` (not `ezenil-max`).
- Adopted CLAUDE.md + PROGRESS.md workflow so agent context follows the
  code across devices (MSI now, Mac Studio possibly later).
- `.gitattributes` normalizes line endings to LF everywhere.

### Next steps (start here next session)

1. **Task 17 — Wio accelerometer:** read the onboard LIS3DHTR 3-axis
   accelerometer, display live X/Y/Z tilt values on the LCD. Introduces
   using a Seeed library (`Seeed_Arduino_LIS3DHTR`) that isn't bundled with
   the board package — first `Library Manager` install.
2. **Task 18 — Commit Wio Terminal chapter:** copy Projects 03, 04, and
   17 into `E:\electronics-learning\projects\`, commit, push.
3. After that, Erick picks the next direction:
   - Continue Wio Terminal → Wi-Fi + sensor logging
   - Jump to XIAO ESP32-S3 Sense → camera / vision projects
   - ESP32 → Home Assistant sensors via ESPHome

### Deferred / blocked

- ~~XIAO SAMD21 chapter: pending soldering iron purchase~~ — **stale since 2026-07** (bench fully equipped, see `INVENTORY.md` → Banco de soldadura); the SAMD21 is superseded by the XIAO ESP32-S3 ×3. Retired 2026-09-25 by Lesson 00.
- NAS mirror strategy for GitHub repos: pending Synology restore
  completion.
- MCP connector authorization in claude.ai settings still incremental for
  Notion, Canva, Exa.

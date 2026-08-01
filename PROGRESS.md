# PROGRESS.md — electronics-learning

Session-by-session log. **Newest entries at the top.** Claude reads this at
the start of each session and adds a new entry at the top at the end of
each session.

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
   - Side-quest: textile-KPI Excel analysis using the Python venv

### Deferred / blocked

- XIAO SAMD21 chapter: pending soldering iron purchase (basic 60W station,
  0.6–0.8 mm rosin-core solder — ~USD 20–40).
- NAS mirror strategy for GitHub repos: pending Synology restore
  completion.
- MCP connector authorization in claude.ai settings still incremental for
  Notion, Canva, Exa.

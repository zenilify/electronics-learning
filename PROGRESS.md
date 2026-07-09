# PROGRESS.md — electronics-learning

Session-by-session log. **Newest entries at the top.** Claude reads this at
the start of each session and appends a new entry at the end.

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

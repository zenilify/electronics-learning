# PROGRESS.md — electronics-learning

Session-by-session log. **Newest entries at the top.** Claude reads this at
the start of each session and appends a new entry at the end.

---

## 2026-07-08 — Foundation, CPX chapter, Wio Terminal chapter

### Accomplished

- Set up `E:\electronics-learning` with Python venv (`.venv`).
- Installed and configured VS Code, Arduino IDE 2.3.10, Mu editor.
- **Project 01 — CPX Blinky:** CircuitPython on Adafruit Circuit Playground
  Express; RGB NeoPixel cycle. First working edit-save-run loop.
- **Project 02 — CPX Light Sensor:** reactive NeoPixels driven by onboard
  light sensor. Learned input → decide → output pattern.
- Initialized Git repo at `E:\electronics-learning`.
- Cleaned up accidental Git repo at `C:\Users\ezeni\.git` (ran `git add .`
  from wrong directory).
- Configured Git Credential Manager, generated GitHub PAT, cleared stale
  Windows credential entries.
- Pushed repo to `https://github.com/zenilify/electronics-learning`.
- Pivoted away from XIAO SAMD21 (unsoldered pins) to Wio Terminal.
- Installed Seeed SAMD Boards package in Arduino IDE.
- Verified toolchain with built-in Blink sketch (Task 14).
- **Project 03 — Wio Hello LCD:** color text on 320x240 LCD via TFT_eSPI.
  First C++ program, first `#include`, first custom colors and font sizes.
- **Project 04 — Wio Input Dashboard:** live joystick + top-button dashboard.
  First custom C++ function (`showButton`), first use of `INPUT_PULLUP` and
  `digitalRead()`.

### Key decisions

- CircuitPython for Adafruit boards and XIAO series (when soldered).
- Arduino/C++ for Wio Terminal and likely ESP32 boards later.
- One project per numbered folder under `projects/NN-short-name/`.
- GitHub account for this project: `zenilify` (not the earlier
  `ezenil-max`, which was empty).
- Adopted CLAUDE.md + PROGRESS.md pattern for cross-device agent context
  and session memory (this file).

### Next steps

- **Task 17:** Read Wio Terminal onboard accelerometer (LIS3DHTR), display
  live X/Y/Z tilt values on the LCD.
- **Task 18:** Copy Wio Terminal projects (03, 04, 17-result) into
  `E:\electronics-learning\projects\`, commit, push.
- After that, Erick's choice:
  - Continue Wio Terminal (add Wi-Fi, sensor logging)
  - Jump to XIAO ESP32-S3 Sense (camera + vision projects)
  - Side-quest: textile-KPI Excel analysis with the Python venv already set up

### Deferred / blocked

- XIAO SAMD21 chapter: pending soldering iron purchase.
- NAS mirror strategy for GitHub repos: pending Synology restore completion.
- MCP connectors for Notion, Canva, Exa still finishing authorization in
  claude.ai settings.

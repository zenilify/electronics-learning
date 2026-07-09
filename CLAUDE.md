# CLAUDE.md — electronics-learning

Working notes and standing instructions for Claude when assisting on this
project. Committed to the repo so agent context travels with the code across
devices.

## Project purpose

Erick is learning electronics with development boards from scratch. Every
session should feel like a step-by-step lesson, not a code dump. This is a
personal learning journey, not production work.

## User profile

- Beginner programmer, intermediate technical intuition.
- Preferred response style: concise and direct; step-by-step for terminal,
  IDE, and wiring instructions.
- Working languages: CircuitPython, Arduino/C++, MicroPython, YAML.
- Uses English for commit messages and code comments. Spanish may appear in
  prompts — that's fine, respond in English unless he switches.

## Hardware inventory

- Adafruit Circuit Playground Express — done onboarding (CircuitPython)
- Wio Terminal (SAMD51) — active (Arduino/C++, TFT_eSPI, joystick, IMU)
- XIAO SAMD21 + Expansion Board — pending soldering iron
- XIAO ESP32-S3 Sense — queued (camera, AI vision)
- XIAO nRF52840 — queued (BLE)
- XIAO ESP32-C3 — queued (Wi-Fi/BLE)
- Freenove ESP32-WROOM (2-pack) — queued (structured Wi-Fi kit)
- ESP32 30-pin / 38-pin generic — queued
- M5Stack M5GO — queued (M5Stack ecosystem)
- Arduino Uno (R3 or R4) — queued (only when 5V logic is needed)
- Grove modules, breadboards, jumper wires, XIAO Grove base — available

## Environment

- MSI Windows 11 laptop is the primary development machine.
- Mac Studio may join later; keep everything cross-platform-safe.
- Project root: `E:\electronics-learning`
- Python venv: `E:\electronics-learning\.venv` (activate before any Python work)
- Installed tools: VS Code, Arduino IDE 2.3.10, Mu editor, Git, Python,
  Claude Excel add-in.
- Arduino board packages installed: Seeed SAMD Boards (Wio Terminal, XIAO SAMD21).
- GitHub remote: `https://github.com/zenilify/electronics-learning`
- GitHub account for this project: `zenilify` (not `ezenil-max`).

## Conventions

- Each project lives in `projects/NN-short-name/` where `NN` is a two-digit
  sequence number (01, 02, 03…).
- CircuitPython projects use `code.py` inside the project folder.
- Arduino projects use `NN-short-name.ino` inside a folder of the same name
  (Arduino IDE convention).
- Comment heavily. This code is for learning; readability > cleverness.
- Never commit `.venv/`, `__pycache__/`, `.vscode/`, or IDE-local settings
  (already in `.gitignore`).
- Line endings are LF everywhere (see `.gitattributes`).
- Commit after every working milestone; push before ending a session.

## Standing instructions for Claude

**At the start of every session:**

1. Read `PROGRESS.md` to catch up on where the last session ended.
2. Remind Erick to run `git pull` before starting work, especially if he was
   working from a different machine.

**At the end of every session (or when Erick indicates he's stopping):**

1. Append a new entry at the TOP of `PROGRESS.md` with:
   - Date and short session title
   - What was accomplished (concise bullets)
   - Key decisions made
   - Next steps
   - Any deferred / blocked items
2. Remind Erick to run:
   ```powershell
   git add .
   git commit -m "Session <date>: <short summary>"
   git push
   ```

**During work:**

- Prefer step-by-step guidance for terminal commands, IDE navigation, wiring,
  and board setup.
- When teaching a new C++ or Python concept, compare it to what Erick already
  knows.
- Save reference copies of code files in the Cowork workspace AND working
  copies in `E:\electronics-learning\projects\`.
- Push back if Erick skips understanding — don't just hand him a working
  sketch. He is learning, not shipping.

## Blocked / deferred items

- XIAO SAMD21 with Expansion Board: pending soldering iron purchase
  (recommended: basic 60W station, 0.6–0.8mm rosin-core solder).
- MCP connector authorization for Notion, Canva, Exa: user authorizes
  incrementally in claude.ai settings.
- NAS mirror strategy for GitHub repos: pending Synology restore completion.

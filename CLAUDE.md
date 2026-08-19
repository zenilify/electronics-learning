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

Summary of the learning-relevant hardware. The full, detailed audit (SKUs,
quantities, verification dates, smart-home devices, soldering bench) is the
source of truth in `INVENTORY.md` — update that file when hardware arrives /
leaves.

**Location:** the physical workbench is in **Toluca**. CDMX is only a
staging/logging spot (no bench there); hardware logged in CDMX gets transported
to Toluca (next trip: Sunday). So "on the bench" effectively means Toluca.

**ESP32 fleet (primary focus — ESPHome → Home Assistant):**

- ESP32-S3 DevKit 44-pin N16R8 ×3 (external u.FL antenna) — main HA sensor nodes
- ESP32-S3 DevKit N16R8 + OLED 0.96" ×2 — nodes with a built-in display
- ESP32-S3 D1 UNO ×2 (UNO form factor, barrel jack) — accepts Arduino shields
- ESP32-S3 Super Mini ×2 — tiniest nodes (BLE proxy, embedded sensors)
- Seeed XIAO ESP32-S3 ×3 — thumb-size, u.FL, pairs with the XIAO Grove shield
- Freenove ESP32-WROOM ×2 — earmarked for the plant sensors (project 07)

**Other microcontrollers:**

- Seeed Wio Terminal D51R (SAMD51) — active (Arduino/C++, TFT_eSPI, joystick, IMU)
- Seeed XIAO SAMD21 + expansion board + Grove shield — unsoldered; soldering now unblocked
- Seeed XIAO nRF52840 — present but unsoldered (BLE track)
- Seeed XIAO ESP32-C3 — present but unsoldered (Wi-Fi/BLE)
- Arduino UNO R4 WiFi, Arduino UNO (CH340 clone), Arduino MEGA 2560 (clone)
- Adafruit Circuit Playground Express — available on the workbench; used in projects 01–02
- BBC micro:bit GO — gift candidate

**M5Stack family (M5Unified/ESP32; voice-satellite candidates for project 06):**

- M5Stack Core, CoreS3 Dev Kit, StackChan (+ spare StackChan Core), Cardputer
  ADV, M5StickS3, AI Chatbot kit (AtomS3R + Voice Base), Voice Pyramid base

**Sensors:**

- Grove Beginner Kit (DHT, light, sound, BMP280, accel, OLED, buzzer, pot, ...)
- AHT20 + BMP280 combo ×3 (I²C temp/hum/pressure) — for the plant nodes
- GY-521 / MPU-6050 IMU ×1, INMP441 I2S mic ×5, Elechouse Voice Recognition V3.1
- M5Stack Grove Units: PaHub v2.1 (I²C mux), Unit MIC, Unit NFC
- Starter-kit loose: HC-SR04, HC-SR501 PIR, MQ-x gas, water-level, DHT11, RC522
- ⏳ Pending import: capacitive soil-moisture sensors

**Smart-home devices (Home Assistant integration targets):**

- ThirdReality Zigbee Smart Plug Gen2 + mmWave Presence Sensor R3
- Shelly Plug US Gen4 ×2, Shelly 2PM Gen4 in-wall, Shelly BLU button + H&T
  (WiFi/Matter usable now; Zigbee needs the Toluca coordinator)

**Proto / audio-out / soldering:**

- Breadboards (MB-102, Radox 830pt), perfboard ×10+, resistor & capacitor kits,
  WAGO 221, ESP32/S3 screw-terminal shields + 44-pin GPIO breakouts ×5, headers
- MAX98357 I2S amp ×5 + mini 4Ω speaker — DIY voice-satellite audio out
- **Soldering bench available (Toluca):** Weller station + LEFAVOR 858D hot-air
  rework + full consumables — unblocks the unsoldered XIAO boards (SAMD21,
  nRF52840, ESP32-C3)

## Environment

- MSI Windows 11 laptop is the primary development machine.
- Mac Studio may join later; keep everything cross-platform-safe.
- Project root: `E:\electronics-learning`
- Python venv: `E:\electronics-learning\.venv` (activate before any Python work)
- Installed tools: VS Code, Arduino IDE 2.3.10, Mu editor, Git, Python,
  ESPHome (in the project venv, for ESP32 → Home Assistant sensors).
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

- ~~XIAO SAMD21 soldering: pending soldering iron~~ — **UNBLOCKED** (2026-08):
  Weller station + LEFAVOR 858D hot-air rework + consumables are on the Toluca
  bench. The unsoldered XIAO boards (SAMD21, nRF52840, ESP32-C3) can be done there.
- Capacitive soil-moisture sensors (project 07 plants): still on import; the
  temp/hum/pressure half already arrived as AHT20+BMP280 ×3.
- Hardware logged in CDMX (there is no CDMX bench) is transported to Toluca —
  next trip Sunday — where the actual bench lives. See `INVENTORY.md` for which
  items are already in Toluca vs. still staged in CDMX.
- Zigbee smart-home devices need the Toluca coordinator (SLZB-MR3U); Shelly
  Gen4 units can run on WiFi/Matter in the meantime.
- MCP connector authorization for Notion, Canva, Exa: user authorizes
  incrementally in claude.ai settings.
- NAS mirror strategy for GitHub repos: pending Synology restore completion.

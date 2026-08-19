# AGENTS.md — electronics-learning

Project overview and standing agent notes live in `CLAUDE.md` (purpose, hardware
inventory, conventions) and `PROGRESS.md` (session log). Read those first.

## Cursor Cloud specific instructions

This repo is a personal electronics-learning journal. It contains two kinds of
code, neither of which is a server/desktop "application":

- CircuitPython firmware (`projects/*/code.py`) for Adafruit boards.
- Arduino/C++ sketches (`projects/*/*.ino`) for Wio Terminal, M5Stack, etc.

That firmware runs on physical microcontrollers that are **not** attached to the
cloud VM, so it cannot be flashed or executed here. Compiling the `.ino` sketches
would require the Arduino IDE / `arduino-cli` plus vendor board cores (Seeed
SAMD, ESP32) and libraries (TFT_eSPI, M5Unified) — none of which are committed
or installed. Do not treat "run the firmware" as a runnable target in the cloud.

### The runnable environment = the Python venv (ESPHome)

The reproducible dev environment is the Python virtual environment defined by
`requirements.txt`, whose star is **ESPHome** — the tool this project uses to
build ESP32/ESP8266 firmware that reports sensors to Home Assistant. ESPHome
turns a YAML file into firmware and, crucially, can **validate and compile
configs with no board attached**, so it is a genuine runnable/testable target in
the cloud.

- The startup update script creates `.venv` and installs `requirements.txt`.
- Use it via `. .venv/bin/activate` (or call `.venv/bin/esphome` /
  `.venv/bin/python` directly).
- System dependency (baked into the environment, not the update script):
  the `python3.12-venv` apt package is required for `python3 -m venv` to work.

### Testing / lint / build (what actually runs in the cloud)

ESPHome YAML configs live under `projects/*/*.yaml`. Validate and build them
with (no hardware needed):

- Validate (fast, the "lint"): `.venv/bin/esphome config projects/<name>/<name>.yaml`
- Compile firmware (the "build"): `.venv/bin/esphome compile projects/<name>/<name>.yaml`

Gotchas:
- A config that uses `!secret` needs a `secrets.yaml` in the same folder. It is
  git-ignored; copy `secrets.yaml.example` to `secrets.yaml` and fill it in
  (any syntactically valid values let `config`/`compile` pass without a board).
- The first `esphome compile` downloads the ESP32 toolchain (a few minutes,
  network required) into `~/.cache/esphome`; later builds are fast.
- `esphome run`/`upload` (flashing) needs a physical board on USB and cannot be
  done in the cloud.

There are no other committed automated tests or lint config. To sanity-check the
older CircuitPython firmware without hardware, syntax-check it:
`.venv/bin/python -m py_compile projects/01-cpx-blinky/code.py projects/02-cpx-light-sensor/code.py`
(`.ino` Arduino sketches are not valid Python and cannot be checked this way).

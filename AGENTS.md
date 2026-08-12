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

### The runnable environment = the Python venv

The only reproducible dev environment is the Python virtual environment defined
by `requirements.txt`. It is a general-purpose data-analysis / document toolkit
(pandas, numpy, matplotlib, seaborn, statsmodels, polars, openpyxl, weasyprint,
playwright, sqlalchemy, ...) used for the documented "textile-KPI Excel analysis"
side-quest, not for the firmware itself.

- The startup update script creates `.venv` and installs `requirements.txt`.
- Use it via `. .venv/bin/activate` (or call `.venv/bin/python` / `.venv/bin/pip`
  directly).
- System dependency (baked into the environment, not the update script):
  the `python3.12-venv` apt package is required for `python3 -m venv` to work.
  `weasyprint`'s native libraries are already present in the base image.

### Testing / lint / build

There are no committed automated tests, lint config, or build system. To sanity-
check firmware source without hardware, syntax-check the CircuitPython files:
`.venv/bin/python -m py_compile projects/01-cpx-blinky/code.py projects/02-cpx-light-sensor/code.py`
(`.ino` sketches are not valid Python and cannot be checked this way).

# rainmaker-neo — ESP RainMaker Neo evaluation on M5Stack (lane RM-1)

Evaluation of [ESP RainMaker Neo](https://docs.neo.rainmaker.espressif.com/) (Espressif's
open-source, AWS-serverless successor to RainMaker Classic) on an **M5Stamp S3**, against the
**public Neo deployment** (free, 20 devices/user). Started 2026-11-11 by lane RM-1
(`macbook-e77ef0f2`, Claude Code). Handoff: `mission-control/claude/handoffs/2026-11-1X-rm-1.md`.

## Layout

| Path | What |
|---|---|
| `vendor/esp-rainmaker-neo-firmware/` | the SDK, **gitignored**, pinned clone — run `tools/fetch-vendor.sh` |
| `switch-stamps3/` | the SDK's `examples/switch` copied out (per the docs' *Start your own project*), paths re-pointed at `vendor/`, `project(switch_stamps3)`, `sdkconfig.defaults.esp32s3` for the Stamp |
| `tools/fetch-vendor.sh` | clones/checks out the pin below |

## Vendor pin

| Repo | Pin | Date | Note |
|---|---|---|---|
| `espressif/esp-rainmaker-neo-firmware` | `d944081ba9e0635da9da84da6bb0144cfa5c4a6a` | 2026-11-06 | `main`; the repo publishes **no tags** yet (checked `git ls-remote --tags`, 2026-11-11) |

The SDK has no `install.sh` and no submodules; third-party code is vendored or pulled by the IDF
component manager (`espressif/led_strip`, `espressif/rmaker_app_network`, `rmaker_aws_sign`).

## Toolchain — ESP-IDF v6.0.2 side by side

Neo needs **ESP-IDF ≥ v6.0.2** (targets esp32 / c2 / c3 / c5 / c6 / s3; **no s2, h2, p4**).
The fleet's PlatformIO (`~/.platformio`, IDF 5.5.4) and ESPHome (`~/.cache/esphome`, IDF 5.5.5/5.5.6)
installs are untouched; v6 lives in its own tree with its own tools path:

| Host | `IDF_PATH` | `IDF_TOOLS_PATH` | Python in the IDF venv | Wrapper |
|---|---|---|---|---|
| cachy | `~/esp/esp-idf-v6.0.2` (tag v6.0.2, `7101770d`) | `~/.espressif-v6` | system 3.14.9 | `~/bin/idf6` |
| macbook | `~/esp/esp-idf-v6.0.2` (same tag) | `~/.espressif-v6` | uv-managed 3.14.10 (never a brew keg — landmine 330) | `~/bin/idf6` |

`idf6` with no arguments opens a bash subshell with the v6 environment; `idf6 <cmd>` runs one
command in it. Outside the wrapper nothing is exported (`which idf.py` → not found).

## Build / flash (ESP-IDF)

```bash
cd switch-stamps3
idf6 idf.py set-target esp32s3
idf6 idf.py build
# house rules: chip-id + MAC first, backup + sha256 before writing — see the handoff
idf6 idf.py -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00 flash monitor
```

## Build / run (POSIX host, no hardware)

```bash
cd switch-stamps3
CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S . -B build-host -G Ninja   # macOS: ~/bin/cc is a tmux wrapper, not a compiler
cmake --build build-host
cd build-host && ./switch_stamps3        # mock bootloader; needs nvs_persistent/ (factory NVS) to connect
```

## Public Neo deployment — endpoints seen 2026-11-11

| Surface | URL | Probe |
|---|---|---|
| Docs | https://docs.neo.rainmaker.espressif.com/ | 200 |
| Admin dashboard | https://dashboard.neo.rainmaker.espressif.com/ | 200 |
| API / user API | https://api.neo.rainmaker.espressif.com · https://user.api.neo.rainmaker.espressif.com | 403 unauthenticated (expected) |
| **MCP** | `https://mcp.neo.rainmaker.espressif.com/v1/mcp` | 401 + `WWW-Authenticate: Bearer resource_metadata=…/.well-known/oauth-protected-resource` (OAuth, scopes openid email) |
| Phone app | ESP RainMaker Home (iOS id1563728960 · Android `com.espressif.novahome`) → choose **ESP RainMaker Neo** | — |
| Launchpad | https://espressif.github.io/esp-rainmaker-neo-firmware/launchpad.toml | prebuilt switch/light/fan/… for esp32s3 |

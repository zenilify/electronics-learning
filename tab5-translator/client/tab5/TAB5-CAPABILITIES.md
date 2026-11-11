# Tab5 — every port, what it's for, and what PAPP can grow into (lane T5, 2026-11-07)

Sources: Erick's photos `~/Blip/Gadgets/M5Stack/Tab5/` (the back sticker + battery bay sticker, read directly),
M5Stack schematic p.4, Tab5 docs. **[M]** = read from the photo/schematic/measured · **[H]** = hypothesis.

## 1 · Ports and the switch (back of the unit, battery cover off)

| Port | Where | Pins (from the sticker) [M] | What it is for |
|---|---|---|---|
| **M5-BUS** 2×15 female | top of the back, big black header | G16, G17 PB_IN, RST/EN, G45, G52 PB_OUT, 3V3, G37 TXD0, G38 RXD0, G6 PC_TX, G7 PC_RX, G32 intSCL, G31 intSDA, G4, G3, G48, G2, G35, G47, G51, G18 MOSI, G19 MISO, G5 SCK, Ext5V, VBAT_OUT, HVIN×3, GND×3 | Stack an M5Stack *Module* (54×54 mm family): SPI, UART, I²C, GPIO + power both ways |
| **ExtPort1** 2×5 2.54 mm | left edge | GND/HVIN · GND/GND · EXT5V/3V3 · G0/G1 · G49/G50 | Where the **Tab5 Keyboard** plugs (I²C G0/G1, INT G50, addr 0x6D) — or any 2.54 mm accessory |
| **ExtPort2** 6-pin | top, next to the switch | SCL G32 · SDA G31 · 485A · 485B · HVIN · GND | **RS-485** (SIT3088: TX G20, RX G21, DIR G34) + I²C + **6–24 V power input** |
| **485-SW** slide switch | top right of ExtPort2 | OFF / ON "RS485 120R" | **The only switch:** ON = 120 Ω termination across A/B. Only ON if the Tab5 is at the *end* of an RS-485 cable; otherwise OFF |
| **STAMP / COM.X** pads | middle, grey square | G31 G32 G33 G6 G46 G19 G18 G5 · GND 3V3 SYS-5V | Solder a Stamp radio (Cat-M / NB-IoT / LoRaWAN) — no pins fitted; its antenna goes out via MMCX J16 |
| **CAM_FPC / LCD_FPC** | internal ribbons | — | Camera (SC2356) and display — don't touch |
| 2S battery socket | battery bay | +, − (gold pins) | NP-F550/750/950. Yours: Tectra NP-F550C 7.2 V 2800 mAh (USB-C self-charging) — fine; "Support DC 6–8.4 V" |
| **Port A** HY2.0-4P | bottom edge | GND · Ext5V · G53 · G54 | Grove Units (I²C or UART or GPIO) |
| 3.5 mm "HEADSET" | bottom edge | L/R + mic | Headphones/headset (HP_DETECT) — a powered speaker for loud rooms |
| **USB-C** | bottom edge | — | Power/charge, flashing, serial console (the P4's USB-Serial/JTAG) |
| **USB-A** | bottom edge | — | USB *host*: keyboard, flash drive, USB-RNDIS phone tether [H], USB camera, 4G dongle |
| 2× **MMCX** | top edge | J15 = Wi-Fi external (switch, 0x43 P0) · J16 = pass-through for a Stamp radio | see README "Antennas" |
| microSD | side | SDMMC (SD 3.0) | storage — see §4 |
| Power button | side | — | press = on/reset · double = off · hold = boot/download |

## 2 · What you can connect

- **Port A (Grove):** the Units you own — NFC (U216, I²C), PaHub v2.1 (6× I²C), Unit MIC (analog → needs an ADC pin; G53/G54 [H]), Grove angle/motion sensors, the Grove kit. One I²C or one UART device at a time without the hub.
- **M5-BUS:** M5 Modules. Your **Module ASR** (Chipintelli, offline *command words*, UART via DIP switches for Core/Core2/CoreS3 pins) [M photo] — the DIP options don't match the Tab5's UART pins (G37/G38), so wire it with 4 jumpers from its PA header to Port A instead. Module LLM would sit here too (UART) [H fit].
- **ExtPort2:** Modbus-RTU sensors/PLCs/energy meters (RS-485), or a 12/24 V supply (car, bench, DIN rail — fits your DIN Base story).
- **USB-A:** USB keyboard/mouse, flash drive, UVC webcam, a 4G/5G dongle (RNDIS) or the S26 Ultra via USB tethering [H].
- **ESP-NOW (no cable):** your M5Dial, StickS3, AtomS3R, CoreS3 as wireless remotes/displays. Corrected 2026-11-11: on the Tab5's own radio this works **only with ESPHome's C6 firmware** (their ESP-NOW overlay); Espressif's esp-hosted, which PAPP uses, has no ESP-NOW. PAPP's route is a small S3 bridge next to the Tab5 — built and measured, see `../../satellites/PROTOCOL.md` §6.

## 3 · Phones as the network (S26 Ultra + iPhone)

- **Wi-Fi hotspots (works today, recommended):** add both hotspots to NVS (`wifi2`, `wifi3`) — the Tab5 tries them
  in order; the MacBook joins the same hotspot, so a Mac "offline brain" is reachable on the same LAN (found via
  mDNS `papp-brain.local`, so the changing hotspot IP doesn't matter).
- **USB tethering through USB-A:** Android (S26U) does USB tethering as **RNDIS**; Espressif ships a USB-host
  RNDIS driver for the ESP32-P4 (`espressif/iot_usbh_rndis`). Feasible, **untested** with a phone [H]. Gives the
  Tab5 internet without Wi-Fi (and charges the phone from the Tab5 battery — mind the 2800 mAh). iPhone USB
  tethering is not RNDIS/ECM-standard → not supported; use its Wi-Fi hotspot.

## 4 · The 1 TB microSD

ESP-IDF 6.0.3 ships FatFs with **exFAT off** (`FF_FS_EXFAT 0`) [M] → format the card **FAT32** (64 KB clusters)
or patch FatFs to enable exFAT. Reformatting erases what the S26U tablet left on it — copy anything first.
Best uses, in order: (1) **offline phrasebook audio** — thousands of pre-rendered phrases zh/es/en, instant and
offline; (2) **conversation log** — every turn's audio + texts (review, and later training data); (3) **full CJK
fonts** (all ~27k hanzi, several weights) and LVGL images; (4) **camera** photos/clips; (5) **app bundles** for a
PAPPS launcher (ELF apps loaded into PSRAM); (6) offline map tiles for Shanghai/Shenzhen.
Watch-out: the SD slot and the C6 Wi-Fi share the P4's SDMMC host (slot 0 / slot 1) — test both together [H].

## 5 · Camera (SC2356, 2 MP, MIPI-CSI; P4 has ISP + JPEG + H.264 encoders)

Point-and-translate (menu/sign/business card → OCR + translation on the backend → overlay) · QR/barcode
scanner · document/whiteboard capture to the SD card · face detection (ESP-DL) to know who is speaking ·
video stream (H.264) to the Mac/Home Assistant.

## 6 · "PAPPS" — more than one app

Options: our **own launcher** in this firmware (home screen + apps, each a screen in PSRAM; fastest, full
control) → later **`espressif/elf_loader`** to load app ELFs from the SD card · **ESP-Brookesia** (Espressif's
phone-style app framework, P4 example) · **Tactility** (ESP32 app OS with an app hub; ESP32-P4 support since
Jan 2026, Tab5 not confirmed) · UIFlow2/MicroPython. Recommendation: own launcher now, ELF apps after the trip.

## 7 · Type & translate (keyboard)

Yes: Tab5 Keyboard (or on-screen) → English/Spanish text → translation (Chinese, big) → spoken by TTS. Needs a
text endpoint on the backend (`POST /translate-text` — NMT only; `/tts` already exists) → server lane.
Also handy offline: canned phrase cards from the SD card.

## 8 · More PAPPS for the M5Stack visit (shortlist)

Conversation mode without buttons (VAD) · Live subtitles (continuous ASR, for CHTF talks) · "Take me to…"
cards (hotel/meeting addresses in big Chinese + map) · Voice notes → transcript + summary · Phrasebook with
pinyin + audio · Currency/units · M5Dial as wireless language/volume knob + StickS3 as wireless PTT (ESP-NOW) ·
RS-485/Modbus industrial demo (OpEx/Kaizen story) · Home Assistant panel.

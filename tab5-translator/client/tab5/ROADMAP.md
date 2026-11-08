# Tab5 "PAPP OS" — roadmap to Shenzhen (owner: lane T5, acting tower for this project)

Freeze **Tue 17 Nov** (flash + SD image frozen, spare build on the MacBook) · depart **18 Nov** · demo to M5Stack **26 Nov**.
Scope = D-T5-5 "c" + converter + voice notes (Erick, 2026-11-07).

## 1 · Architecture decision (D-T5-8, taken as tower — reversible)

**Our own native firmware with an LVGL home screen and built-in apps.** giltal's ESP32_P4_Launcher is *not* the base, for now:

| Fact (read from the zip, 0.26.0) | Consequence |
|---|---|
| `.papp` SDK has SYS/MEM/FS/DISP/AUDI(out only)/INPT/THRD/UI — **no NET, no mic, no camera** (NET = WORKPLAN C3 "only when an app needs it", CAM = future) | The translator, OCR and voice notes can't be `.papp` today |
| Built on **ESP-IDF 5.5.6**, ours is 6.0.3 | Two toolchains |
| Pins Tab5 C6 to **esp_hosted 1.4** (M5's C6 fw); our C6 is now **2.12.13** | Wi-Fi in the launcher likely breaks on this unit — test before relying on it |
| Tab5 ST7123/**ST7121** panels "detected … waiting for a test on such a unit"; ours is ST7121 | We can be the first ST7121 test → upstream issue/PR = good community story for M5Stack |

Later (stretch, after 17 Nov only if green): dual-boot — shrink our factory to 8 MB, put the launcher in a 3 MB slot, home-screen tile "Games ▸" sets the boot partition. Also: an SD loader for our own apps is a v2 idea, not a demo item.

## 2 · Apps (home screen tiles) — priority order

| # | App | Needs | Target |
|---|---|---|---|
| 0 | Home/launcher + status bar + settings (Wi-Fi list, rotation, antenna, volume) | — | ✅ v0.2.0 flashed 11-07 (166be46) |
| 1 | **Translator** (PTT, done) | — | ✅ |
| 2 | **SD card** service: logs, history, phrase audio cache, screenshots | SDMMC slot 0 shared host w/ C6 SDIO | ✅ 977 GB mounted with Wi-Fi up (console `sd`, 11-07 21:3x) |
| 3 | **Conversation mode** (no buttons, VAD, auto-language) | VAD on device + server `auto` src | 11 Nov |
| 4 | **Type & Translate** ✅ v0.2.0 (USB/Port keyboard + on-screen) → text + voice | server `/translate-text`, `/tts` | 11 Nov |
| 5 | **Phrasebook** ✅ v0.2.0 (offline: pre-rendered zh audio on SD) | SD | 12 Nov |
| 6 | **"Take me to" cards** ✅ v0.2.0 (big hanzi + pinyin, address, show taxi driver; NFC card tap) | SD + Unit NFC on Port A | 12 Nov |
| 7 | **Converter** ✅ v0.2.0 (CNY⇄MXN⇄USD cached rates, units, sizes) | server `/fx` (cached daily) | 13 Nov |
| 8 | **Voice notes → transcript + summary** (to SD + MD sync) | server `/summarize` | 13 Nov |
| 9 | **Point & Translate** (camera → OCR → overlay) | server `/ocr-translate`, camera driver | 14 Nov |
| 10 | **Live subtitles** (streamed chunks, big captions) | chunked ASR | 15 Nov |
| 11 | **Remotes**: Dial knob = language/volume/PTT, StickS3 = PTT clicker, over ESP-NOW | §4 | 15 Nov |
| 12 | **RS-485 demo** (Ext Port 2) + **HA panel** (lights/Casa from the old tab5-banco) | Modbus unit / HA token-less feed | 16 Nov |

Server endpoints (I own the server now as tower): `/translate-text`, `/tts`, `/summarize`, `/ocr-translate`, `/fx`, `/translate?src=auto`. Same bearer, same 3-endpoint failover.

## 3 · Network (D-T5-7) — three layers, the Tab5 never cares which

1. **Direct**: NVS holds up to 3 SSIDs → S26 Ultra hotspot (first), iPhone hotspot (fallback), home. *Need from Erick: SSIDs + passwords into `~/.config/zenilify/t5-wifi` (0600) on cachy — never in chat.*
2. **MacBook "offline brain"**: the MacBook joins the same hotspot and runs the server; the Tab5 finds it by **mDNS `_papp._tcp`** and puts it first in the endpoint list. Works with no mobile data at all.
3. **StickS3 "HEZ puente"** (already built 09-27, `home-lab/firmware/sticks3-bridge`): SoftAP + tailnet node → cachy at home from any hotspot behind CGNAT. Needs NAPT for Tab5→tailnet outbound (today it only relays :6053 inward) and the security cleanup listed in PROGRESS-LOG §11 before it travels.
Stretch: S26U USB tethering into the Tab5 USB-A (iot_usbh_rndis).

## 4 · Devices on hand → role

| Device | ID (measured on cachy USB) | Role |
|---|---|---|
| Tab5 | 30:ED:A0:E2:EF:41 | the product |
| StickS3 "HEZ puente" | 14:C1:9F:D5:35:3C | pocket bridge (§3.3) **and** ESP-NOW PTT clicker (KEY1 = talk) |
| M5Dial v1.1 | 70:AF:09:DE:94:4C | ESP-NOW remote: knob = language pair / volume, press = PTT, NFC = phrase card reader |
| AtomS3R cam | 98:88:E0:0E:C3:B0 | **not ours** — Frigate camera (atoms3r-cam-echo); don't flash |
| StampS3 1.27 mm | — | in the Grove Breakout → Tab5 STAMP pad / Port A sensor node |
| StampS3 2.54 mm | — | breadboard: RS-485 Modbus slave for app 12 |
| Unit NFC, Pa.HUB (70H/77H), Base Dual 16340, Screw Kit | photos 11-07 | NFC → "Take me to" cards; Pa.HUB if Port A gets crowded; 16340 base = spare power for a Core stack, not Tab5 |
| Unit RFID2 (WS1850S, I2C 0x28) | photo 11-07 | Port A: tap a 13.56 MHz card/sticker → "Take me to" / phrase card (one card per hotel, airport…) |
| Unit QRCode2 | photo 11-07 | Port A (UART/I2C): scan a menu/sign QR → show + translate its text; scan a WeChat ID QR → show it big |
| ASR CI1302 | — | offline wake/commands ("translate", "repeat") — stretch |

Every write to a device: `chip-id` + MAC first, backup + sha256 first (house rule 1 & 5).

## 5 · Risks

| Risk | Mitigation |
|---|---|
| C6 SDIO bring-up intermittent after OTA (landmine 318, open) | 20-reboot count; slave rebuild without HIGH_SPEED; SD + SDIO coexistence test |
| Wi-Fi association needs retries | test on phone hotspot vs ISP router; keep 3× per SSID |
| Server TTS latency 2–7 s | cache phrase TTS on SD; stream TTS |
| Flash size (12 MB app) | font + clips to SD; keep a 1.1 MB CJK font in flash as fallback |
| Demo-day network | layer 2 (MacBook offline brain) rehearsed with mobile data OFF |

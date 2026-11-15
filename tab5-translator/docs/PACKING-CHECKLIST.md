# Packing checklist — Tab5 demo kit for Shenzhen

Written 14 Nov 2026 (tracker row Erick #19). Freeze is Tue 17 Nov, departure Wed 18 Nov, M5Stack visit Thu 26 Nov.
Device names and IDs are from the tracker's Gear sheet. Tick each line with the item in your hand.

## The product

- [ ] **Tab5** (30:ED:A0:E2:EF:41) with its battery pack fitted, and the **SD card inside** (the phrase pack, the voice pack and the offline phrase audio live on it)
- [ ] **XIAO ESP32S3 Sense camera** and its short USB cable to the Tab5's USB-A port (no case yet: pack it padded)
- [ ] Charger for the Tab5 and a **USB-C to USB-C cable**. Do not use a USB-A adapter: through one the Tab5 restarts when power is plugged in (7 of 7), through USB-C to USB-C it did not (0 of 5)
- [ ] Check the travel charger once before leaving: plug in while the Tab5 runs on battery, then Ajustes must not show "último arranque: baja tensión"

## Remotes

- [ ] **StickS3** push-to-talk clicker (14:C1:9F:D5:35:3C) and its USB-C cable
- [ ] **M5Dial** (70:AF:09:DE:94:4C) and its USB-C cable
- [ ] Both still point at the Tab5's home address (192.168.1.118). They must be re-pointed after the travel-router test (tracker Erick #1, Claude #1) — not done yet

## Línea demo kit

- [ ] **AtomS3 Lite + Atomic QRCode2 base** (scanner, 50:78:7D:CE:8B:10)
- [ ] **Cardputer ADV + Unit RFID2** (operator badge, 9C:CC:01:E0:EA:68) and at least one RFID card (83971505 is the tested one)
- [ ] **StampS3 on the Grove breakout** (line sensor, 50:78:7D:CD:64:8C)
- [ ] Printed lot labels with QR codes for the scanner
- [ ] USB power for the three (a small hub or a power bank with three ports) and three cables

## Network

- [ ] **UniFi Travel Router** (SSID kaizen-tr) and its power supply
- [ ] Phone that tethers by USB into the travel router (S26 Ultra) and that cable
- [ ] **MacBook** and charger. It carries the offline brain (translation with no internet) and the spare firmware build

## Badge and paper

- [ ] **PaperS3 badge**, charged. The refreshed page 6 is staged but not on the badge yet (see the handoff: `./flash-badge.sh m5`)
- [ ] Printed one-pager for the Seeed meeting (decided 10 Nov: printed, not on the badge)

## Spares and tools

- [ ] Second USB-C to USB-C cable, a USB-C data cable for flashing from the MacBook
- [ ] Spare SD card (optional: it would have to be synced from the home server before leaving)
- [ ] Power bank

## On the MacBook before leaving (no internet needed afterwards)

- [ ] `~/t5/` is present: the tested firmware folders, the build tools and the repo clone
- [ ] One build was run after the last `git pull` (so the components are on the MacBook)
- [ ] The offline brain starts: `cd ~/t5/electronics-learning/tab5-translator/offline-brain && bash start.sh`, then `curl http://127.0.0.1:8787/health`
- [ ] Not on the MacBook by decision (D-13): the Wi-Fi and remote-key secrets. Setting a board up from scratch is only possible from the home machine

## Stays at home

- The AtomS3R camera (98:88:E0:0E:C3:B0) — it is the house camera, not part of the kit
- The home server keeps running for the trip (no nightly sleep from Tue 17 Nov 20:00 to Sun 29 Nov 12:00)

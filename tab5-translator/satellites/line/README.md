# Kaizen Lab Traceability Cell — satellites (lane T5-I)

A portable "factory in a case": three small M5Stack boards feed N4's line API on the PAPP server, and the
Tab5 tile **Línea · 产线** (key `o`) shows live OEE / Andon in es / zh / en.

| Folder | Board (MAC, measured 2026-11-10) | Role | Posts |
|---|---|---|---|
| `scanner-atomlite/` | **AtomS3 Lite** on the Atomic QRCode2 Base · 50:78:7D:CE:8B:10 | fixed QR scanner; button = next scan is scrap | `POST /scan` |
| `badge-cardputer/` | **Cardputer ADV** + Unit RFID2 on the Grove port · 9C:CC:01:E0:EA:68 | badge login/logout, RFID kanban, andon keyboard | `POST /line/event` |
| `sensor-stamps3/` | **StampS3** on the Grove Breakout · 50:78:7D:CD:64:8C | piece counter (button) + temperature, every 10 s | `POST /line/event` run/stop + `value` |
| `tools/` | cachy | `mock_line.py` (§3-exact mock), `gen_secrets.py`, `sermon.py`, `backup_line.sh`, `ident.py` | — |

Contract: mission-control `claude/handoffs/2026-11-08-n4.md` §3. Toolchain: PlatformIO, pioarduino 55.03.38-1
(arduino-esp32 3.3.8). The folder is called `scanner-atomlite` after the lane prompt; the board is an AtomS3 Lite.

## Build and flash (on cachy, zsh)

```sh
cd ~/Documents/claude-projects/electronics-learning/tab5-translator/satellites/line
python3 tools/gen_secrets.py            # writes common/secrets.h (0600, gitignored); prints lengths only
cd sensor-stamps3   && pio run -t upload   # each platformio.ini pins its own /dev/serial/by-id port
cd ../scanner-atomlite && pio run -t upload
cd ../badge-cardputer  && pio run -t upload
python3 ../tools/sermon.py CE:8B:10 15 '?'  # serial console of one board: '?' status, 'w' Wi-Fi scan
```

Restore a board to what it ran before: `esptool --port <by-id> write-flash 0x0 ~/t5/backup/line/s3-<MAC>-*.bin`
(sha256 in `~/t5/backup/line/SHA256SUMS`; the Cardputer dump is Bruce 1.11).

## LIVE vs MOCK

Every board probes `GET /line/kpi` on `LINE_BASE` (cachy :8787). While that is 404 it posts to `LINE_FALLBACK`
(`tools/mock_line.py`, cachy :8788) and **says so**: serial prints `MOCK`, the Cardputer bar is blue with the
word MOCK, the LEDs are blue. The probe repeats every 60 s, so the day N4 ships nothing is reflashed. The Tab5
reads :8787 only — mock numbers never appear on it.

## Operator cheat-sheet

- **Scanner** — show a QR: green = counted good. Press the Atom button (LED red), then scan = scrap (S01).
  Yellow = duplicate or FIFO warning from the server. Purple blink = server unreachable.
- **Cardputer** — tap your badge = login; same badge again = logout; any other tag = kanban bin signal.
  `1` S01 material defect · `2` D01 no material · `3` D02 changeover · `4` D03 breakdown → andon.
  Type a code (`S04`) + Enter → andon with that code. `c` clears the andon, `r` = run, `S` + Enter = stop,
  `b` = the next card is the badge.
- **Sensor** — short press = one piece; hold 1.5 s = toggle RUN/STOP. Green run, red stop.

## Storylines — 2 minutes each, same kit (DRAFT — Erick reviews the business content)

Each one: 20 s set-up line, three actions on the kit, one sentence on the Tab5, one closing question.

### Rain — textile auxiliaries / food · 23 Nov · "batch genealogy"
1. *Set-up:* "Every drum of auxiliary you ship has a lot. When a customer complains about a shade, how long
   until you know which lots went into that order?"
2. Badge in on the Cardputer (operator is now on every record).
3. Scan three drum labels of lot A, then one of an **older** lot B → the scanner turns yellow: FIFO warning.
4. Arm scrap, scan one drum → scrap with reason S01 (material defect / 材料缺陷).
5. Tab5: quality drops, the reason appears in Chinese and Spanish; `GET /line/trace?code=` returns who scanned
   which drum, when, at which station.
6. *Close:* "This is three boards and a tablet. Which of your lines loses the most time finding a lot?"

### Daniel — sourcing / incoming inspection · 23 Nov · "incoming inspection scan"
1. *Set-up:* "Your inspector in Shenzhen and your buyer in Mexico read the same board, each in their language."
2. Badge in as the inspector.
3. Scan ten supplier cartons: eight good, two rejected (button → scrap).
4. Type the defect code on the Cardputer → an Andon opens; the Tab5 shows it in zh + es.
5. Tap a bin tag → kanban signal: "replace this lot".
6. Tab5: accept rate (quality) for the delivery, top reject reasons; the end-of-shift report is one call.
7. *Close:* "What would you want on this report before releasing payment to a supplier?"

### James — chemicals, paper & water treatment · 24 Nov · "sensor node"
1. *Set-up:* "A dosing skid runs unattended. You want to know it is running, and its temperature or pH, without
   a PLC project."
2. The StampS3 posts a reading every 10 s — today the chip temperature; a pH or temperature probe plugs into
   the same Grove port.
3. Hold the button: the node reports STOP → availability falls on the Tab5 in real time.
4. Raise an Andon `D03` (breakdown / 设备故障) from the Cardputer; clear it with `c` when "repaired".
5. Scan the chemical drum being dosed → the batch is tied to the time window of the readings.
6. *Close:* "Which measurement would you put on this node first?"

**Honest limits to say out loud:** the sensor value is the chip's own temperature until a probe is fitted; the
`value` field is an extension N4 has not confirmed; the twelve reason codes are N4's (four are known today).

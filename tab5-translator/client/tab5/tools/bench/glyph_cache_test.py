#!/usr/bin/env python3
"""What does the font cache cost? (lane T5, 2026-11-11) - through the console logger's inbox, no hands.

Types every distinct Chinese character of the phrase pack (537 on 11 Nov) into the Escribe screen, 100 at a time,
and reads free internal RAM after each step. A cache shows as a fall that stops at its size:
  build c21 (512 entries per font size): 238 -> 213 -> 187 -> 166 -> 145 -> 135 -> 135 kB, kept after leaving
  build c22 (sized per font, main/ui.c):  288 -> 274 -> 273 -> 273 kB

  python3 tools/bench/glyph_cache_test.py [path to the phrase pack's index.json]
Needs tools/console_log.py running (it owns the port). Run it right after a boot, before other Chinese text was shown.
"""
import os, re, sys, time

os.environ["TZ"] = "America/Mexico_City"; time.tzset()
INBOX = os.path.expanduser("~/t5/watch/inbox.txt")
LOG = os.path.expanduser(time.strftime("~/t5/watch/console-%Y%m%d.log"))
PACK = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/papp/phrasebook-pack-v2/papp/phrasebook/index.json")

seen, chars = set(), []
for ch in open(PACK, encoding="utf-8").read():
    if "一" <= ch <= "鿿" and ch not in seen:
        seen.add(ch); chars.append(ch)
print("distinct Chinese characters in the pack:", len(chars))


def send(c):
    with open(INBOX, "a") as f: f.write(c + "\n")


def status(tag):
    pos = os.path.getsize(LOG); send("status"); time.sleep(3.5)
    with open(LOG, errors="replace") as f:
        f.seek(pos); t = f.read()
    g = lambda p: (re.search(p, t) or [None, "?"])[1]
    print(f"{time.strftime('%H:%M:%S')} {tag:<36} free {g(r'free_int=(\d+) kB')} kB · largest {g(r'int_largest=(\d+) kB')} kB · psram {g(r'psram=(\d+) kB')} kB", flush=True)


send("key esc"); time.sleep(0.8); send("key esc"); time.sleep(1.2)
status("home")
send("key e"); time.sleep(2.5); status("Escribe opened")
n = 0
for i in range(0, len(chars), 100):                 # 100 characters = 300 bytes: the console line holds 720
    part = "".join(chars[i:i + 100]); n += len(part)
    send("key " + part); time.sleep(2.5); status(f"typed {n} distinct characters")
send("key esc"); time.sleep(1.5); status("text cleared")
send("key esc"); time.sleep(1.5); status("back home")

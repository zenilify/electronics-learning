#!/usr/bin/env python3
"""Does the caption history stay bounded? (lane T5, 2026-11-11, build c21)

One live-subtitles session; the room has a television running, so captions keep coming. Every 20 s: free internal
RAM, largest free piece, captions so far. Ends at 30 captions or after 7 minutes, then stops, leaves the screen and
reads the memory once more. Two known phrases are spoken through the Tab5's loudspeaker on the way.
"""
import os, re, time

os.environ["TZ"] = "America/Mexico_City"; time.tzset()
INBOX = os.path.expanduser("~/t5/watch/inbox.txt")
LOG = os.path.expanduser(time.strftime("~/t5/watch/console-%Y%m%d.log"))
OUT = open(os.path.expanduser("~/t5/exp-subs-mem.log"), "a")


def say(*a):
    line = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    print(line, flush=True); OUT.write(line + "\n"); OUT.flush()


def send(cmd):
    with open(INBOX, "a") as f: f.write(cmd + "\n")


def since(pos):
    with open(LOG, errors="replace") as f:
        f.seek(pos); return f.read()


def status():
    pos = os.path.getsize(LOG); send("status"); time.sleep(3.5)
    t = since(pos)
    g = lambda p: (re.search(p, t) or [None, "?"])[1]
    return g(r"free_int=(\d+) kB"), g(r"int_largest=(\d+) kB"), g(r" up=(\d+) s"), g(r"ui_restarts=(\d+)")


time.sleep(12)                                   # Wi-Fi after the flash
f, l, up, ui = status(); say(f"home screen after boot: free {f} kB, largest {l} kB, up {up} s, ui_restarts {ui}")
send("key s"); time.sleep(2.5)
f, l, up, ui = status(); say(f"subtitles screen built: free {f} kB, largest {l} kB")
pos0 = os.path.getsize(LOG); send("key enter"); t0 = time.time(); spoke = 0
PH = [("es", "Necesitamos doscientas piezas para el viernes."), ("zh", "这个多少钱？可以便宜一点吗？")]
while True:
    time.sleep(16.5)
    f, l, up, ui = status()
    t = since(pos0); n = t.count("subs: final")
    say(f"  +{time.time() - t0:4.0f} s: captions {n:2d} · free {f} kB · largest {l} kB")
    if spoke < len(PH) and time.time() - t0 > 30 + 60 * spoke:
        send(f"say {PH[spoke][0]} {PH[spoke][1]}"); spoke += 1
    if n >= 30 or time.time() - t0 > 420: break
t = since(pos0)
fin = re.findall(r"subs: final \[(\w+)\] (.*?) -> ", t)
odd = [a for _, a in fin if re.search(r"[぀-ヿ가-힯Ѐ-ӿ]", a) or not re.search(r"\w", a)]
say(f"captions {len(fin)}; in another script or without letters: {len(odd)} {odd[:4]}")
say("heard the two spoken phrases:", "piezas" in t, "/", "多少钱" in t)
send("key enter"); time.sleep(3)
f, l, up, ui = status(); say(f"stopped, still on the screen: free {f} kB, largest {l} kB")
send("key esc"); time.sleep(2.5)
f, l, up, ui = status(); say(f"left the screen: free {f} kB, largest {l} kB, up {up} s, ui_restarts {ui}")

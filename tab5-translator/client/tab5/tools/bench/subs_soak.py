#!/usr/bin/env python3
"""Live-subtitles soak on the Tab5 (lane T5, 2026-11-11) - through the console logger's inbox, no hands.

Part 1: 15 quick start/stop cycles (2-6 s on, 1-2 s off) - does starting and stopping ever stall the screen?
Part 2: one session of about 4 minutes; every 40 s the Tab5 speaks a known phrase through its own loudspeaker and the
        captions that come back are compared with it.
Reads `status` before and after: the uptime must keep counting and ui_restarts must not change.
"""
import os, random, re, sys, time, unicodedata

os.environ["TZ"] = "America/Mexico_City"; time.tzset()
INBOX = os.path.expanduser("~/t5/watch/inbox.txt")
LOG = os.path.expanduser(time.strftime("~/t5/watch/console-%Y%m%d.log"))
OUT = open(os.path.expanduser("~/t5/exp-subs-soak.log"), "a")
rnd = random.Random(20)


def say(*a):
    line = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    print(line, flush=True); OUT.write(line + "\n"); OUT.flush()


def send(cmd):
    with open(INBOX, "a") as f: f.write(cmd + "\n")


def since(pos):
    with open(LOG, errors="replace") as f:
        f.seek(pos); return f.read()


def status():
    pos = os.path.getsize(LOG); send("status"); time.sleep(4)
    t = since(pos)
    g = lambda p: (re.search(p, t) or [None, "?"])[1]
    return dict(up=g(r" up=(\d+) s"), ui=g(r"ui_restarts=(\d+)"), free=g(r"free_int=(\d+) kB"), largest=g(r"int_largest=(\d+) kB"),
                wifi=g(r"status wifi=(\d)"), ep=g(r" ep=(-?\d+)"))


def norm(s):
    s = unicodedata.normalize("NFD", s.lower())
    return re.sub(r"[^a-z0-9一-鿿 ]", "", "".join(c for c in s if unicodedata.category(c) != "Mn"))


PHRASES = [("es", "Hola, buenos días. ¿Dónde está la estación de tren?", ["estacion de tren"]),
           ("zh", "你好，请问洗手间在哪里？", ["洗手间"]),
           ("en", "Good morning. We would like to see the production line today.", ["production line"]),
           ("es", "Necesitamos doscientas piezas para el viernes.", ["doscientas piezas", "200 piezas"]),
           ("en", "How much does this cost per thousand units?", ["thousand units", "1000 units", "1,000 units"]),
           ("zh", "这个多少钱？可以便宜一点吗？", ["多少钱"])]

s0 = status(); say("before:", s0)
send("key esc"); time.sleep(0.6); send("key esc"); time.sleep(0.6); send("key s"); time.sleep(2.5)

say("part 1: 15 start/stop cycles")
pos = os.path.getsize(LOG)
for i in range(15):
    send("key enter"); time.sleep(rnd.uniform(2, 6))
    send("key enter"); time.sleep(rnd.uniform(1, 2))
t = since(pos)
say(f"part 1: stream on x{t.count('audio: stream: on')}, off x{t.count('audio: stream: off')}, session starts {t.count('subs: start HTTP 200')}, "
    f"start errors {len(re.findall(r'subs: start HTTP (?!200)', t))}, 'UI silent' {t.count('UI silent')}, boots {t.count('PAPP Tab5 v')}")
s1 = status(); say("after part 1:", s1)

say("part 2: one session, a phrase every 40 s")
pos = os.path.getsize(LOG); send("key enter"); time.sleep(6)
hits = 0
for k, (lang, text, want) in enumerate(PHRASES):
    p = os.path.getsize(LOG); send(f"say {lang} {text}"); time.sleep(40)
    finals = re.findall(r"subs: final \[(\w+)\] (.*?) -> (.*)", since(p))
    got = " | ".join(f"[{l}] {a}" for l, a, _ in finals)
    ok = any(norm(w) in norm(got) for w in want)
    hits += ok
    say(f"  {k + 1} {'ok  ' if ok else 'MISS'} said [{lang}] {text}  ->  heard: {got[:260] or '(nothing)'}")
send("key enter"); time.sleep(4)
t = since(pos)
lat = [int(x) for x in re.findall(r"-> HTTP 200 in (\d+) ms", t)]
lat.sort()
say(f"part 2: {hits} of {len(PHRASES)} phrases captioned; chunks {len(lat)}, round trip median {lat[len(lat) // 2] if lat else '?'} ms, "
    f"p95 {lat[int(len(lat) * 0.95)] if lat else '?'} ms, max {lat[-1] if lat else '?'} ms; merged {t.count('subs: merged')}, "
    f"dropped {t.count('queue full')}, HTTP errors {len(re.findall(r'subs: chunk HTTP', t))}, finals {t.count('subs: final')}, 'UI silent' {t.count('UI silent')}")
s2 = status(); say("after part 2:", s2)
ok_up = s0["up"].isdigit() and s2["up"].isdigit() and int(s2["up"]) > int(s0["up"])
say(f"RESULT: uptime kept counting: {ok_up} ({s0['up']} -> {s2['up']} s) · ui_restarts {s0['ui']} -> {s2['ui']} · free internal RAM {s0['free']} -> {s2['free']} kB")
send("key esc")

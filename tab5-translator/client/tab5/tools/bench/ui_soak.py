#!/usr/bin/env python3
"""UI soak on build c22 (lane T5, 2026-11-11): walk through every app for about 12 minutes, no hands.

Each round opens the twelve apps one after the other (the camera app stays open 6 s so its preview runs, subtitles
are started and stopped once, a built-in clip goes through the translator every third round) and reads `status`.
Pass = the uptime keeps counting, ui_restarts does not change, no "UI silent" line, free internal RAM settles.
"""
import os, re, sys, time

os.environ["TZ"] = "America/Mexico_City"; time.tzset()
INBOX = os.path.expanduser("~/t5/watch/inbox.txt")
LOG = os.path.expanduser(time.strftime("~/t5/watch/console-%Y%m%d.log"))
OUT = open(os.path.expanduser("~/t5/exp-ui-soak.log"), "a")
MINUTES = float(sys.argv[1]) if len(sys.argv) > 1 else 12


def say(s):
    line = time.strftime("%H:%M:%S ") + s; print(line, flush=True); OUT.write(line + "\n"); OUT.flush()


def send(c):
    with open(INBOX, "a") as f: f.write(c + "\n")


def since(pos):
    with open(LOG, errors="replace") as f:
        f.seek(pos); return f.read()


def status():
    pos = os.path.getsize(LOG); send("status"); time.sleep(3.2); t = since(pos)
    g = lambda p: (re.search(p, t) or [None, "?"])[1]
    return dict(free=g(r"free_int=(\d+) kB"), largest=g(r"int_largest=(\d+) kB"), ui=g(r"ui_restarts=(\d+)"), up=g(r" up=(\d+) s"),
                bad=g(r"cam_bad_frames=(\d+)"), wifi=g(r"status wifi=(\d)"))


pos0 = os.path.getsize(LOG)
send("key esc"); time.sleep(0.8); send("key esc"); time.sleep(1.2)
s0 = status(); say(f"start: {s0}")
t0 = time.time(); rounds = opens = caps = 0
apps = "t e f l c p n d s r o a".split()
while time.time() - t0 < MINUTES * 60:
    rounds += 1
    for k in apps:
        send("key " + k); opens += 1
        if k == "p": time.sleep(6.0)                        # camera preview running
        elif k == "s": time.sleep(1.5); send("key enter"); time.sleep(5); send("key enter"); time.sleep(1.5)
        elif k == "t" and rounds % 3 == 1: time.sleep(1.5); send("demo"); time.sleep(14); send("demo"); time.sleep(1)
        else: time.sleep(2.0)
        send("key esc"); time.sleep(0.9); send("key esc"); time.sleep(0.9)
    s = status()
    say(f"round {rounds:2d}: free {s['free']} kB · largest {s['largest']} kB · ui_restarts {s['ui']} · up {s['up']} s · cam_bad_frames {s['bad']} · wifi {s['wifi']}")
t = since(pos0)
s1 = status()
ok = s0["up"].isdigit() and s1["up"].isdigit() and int(s1["up"]) > int(s0["up"]) and s0["ui"] == s1["ui"] and "UI silent" not in t and "PAPP Tab5 v" not in t
crashes = len(re.findall(r"Guru Meditation|abort\(\)|assert failed", t))
errors = len(re.findall(r"^\S+ E \(", t, re.M))                 # console lines of level E, after the logger's time stamp
say(f"END after {rounds} rounds, {opens} app opens, {(time.time() - t0) / 60:.1f} min: 'UI silent' {t.count('UI silent')} · boots {t.count('PAPP Tab5 v')} · "
    f"crash lines {crashes} · subtitle sessions {t.count('subs: start HTTP 200')} · E-level log lines {errors} · "
    f"ui_restarts {s0['ui']} -> {s1['ui']} · uptime {s0['up']} -> {s1['up']} s · PASS {ok}")

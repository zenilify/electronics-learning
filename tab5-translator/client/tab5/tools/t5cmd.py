#!/usr/bin/env python3
"""t5cmd.py 'cmd' ['cmd' ...] [--wait S] [--gap S] [--grep RE] [--not RE]

Send console commands to the Tab5 through the running console logger (its inbox file), wait, and print the log lines
that arrived meanwhile. Wi-Fi names are masked. A command of the form `sleep:3` just waits 3 s between commands.
"""
import os, re, sys, time

os.environ["TZ"] = "America/Mexico_City"; time.tzset()
INBOX = os.path.expanduser("~/t5/watch/inbox.txt")


def logpath():
    return os.path.expanduser(time.strftime("~/t5/watch/console-%Y%m%d.log"))


def mask(l):
    l = re.sub(r'cam "[^"]*"', 'cam "<ssid>"', l)
    l = re.sub(r'(wifi\d? "?)[^" ]+("?)', r'\1<ssid>\2', l) if " joining " in l or "ssid" in l.lower() else l
    return l


args = sys.argv[1:]; wait = 4.0; gap = 0.6; pat = None; neg = None; cmds = []
i = 0
while i < len(args):
    if args[i] == "--wait": wait = float(args[i + 1]); i += 2
    elif args[i] == "--gap": gap = float(args[i + 1]); i += 2
    elif args[i] == "--grep": pat = re.compile(args[i + 1]); i += 2
    elif args[i] == "--not": neg = re.compile(args[i + 1]); i += 2
    else: cmds.append(args[i]); i += 1
lp = logpath(); pos = os.path.getsize(lp) if os.path.exists(lp) else 0
for c in cmds:
    if c.startswith("sleep:"):
        time.sleep(float(c[6:])); continue
    with open(INBOX, "a") as f: f.write(c + "\n")
    time.sleep(gap)
time.sleep(wait)
with open(lp, errors="replace") as f:
    f.seek(pos); new = f.read()
for l in new.splitlines():
    if pat is not None and not pat.search(l): continue
    if neg is not None and neg.search(l): continue
    print(mask(l)[:240])

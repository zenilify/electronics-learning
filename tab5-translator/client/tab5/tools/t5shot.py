#!/usr/bin/env python3
"""t5shot.py OUT.jpg [quality] : screenshot of the Tab5 through the running console logger (inbox + log file).

The firmware answers `shot <q>` with "@@SHOT BEGIN w h n fnv ms", base64 lines that start with "@@", "@@SHOT END".
The logger writes each console line with a 13-character time prefix; this reads them back out of today's log.
Exit code 0 = file written and its checksum matched.
"""
import base64, os, sys, time

os.environ["TZ"] = "America/Mexico_City"; time.tzset()
out = sys.argv[1]; q = sys.argv[2] if len(sys.argv) > 2 else "70"
log = os.path.expanduser(time.strftime("~/t5/watch/console-%Y%m%d.log"))
inbox = os.path.expanduser("~/t5/watch/inbox.txt")


def fnv1a(b):
    h = 0x811C9DC5
    for x in b: h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


for attempt in range(3):
    pos = os.path.getsize(log)
    with open(inbox, "a") as f: f.write(f"shot {q}\n")
    t0 = time.time(); meta = None; chunks = []; done = False; err = ""
    while time.time() - t0 < 20 and not done:
        time.sleep(0.4)
        with open(log, errors="replace") as f:
            f.seek(pos); new = f.read()
        meta = None; chunks = []
        for l in new.splitlines():
            s = l[13:]
            if not s.startswith("@@"): continue
            s = s[2:]
            if s.startswith("SHOT FAIL"): err = s; done = True; break
            if s.startswith("SHOT BEGIN"):
                p = s.split(); meta = dict(w=int(p[2]), h=int(p[3]), n=int(p[4]), fnv=int(p[5], 16)); chunks = []
            elif s.startswith("SHOT END"): done = True; break
            elif meta is not None: chunks.append(s.strip())
    if err or not meta or not done:
        print(f"attempt {attempt + 1}: {err or 'no complete answer'}", file=sys.stderr); continue
    try: data = base64.b64decode("".join(chunks))
    except Exception as ex:
        print(f"attempt {attempt + 1}: base64 {ex}", file=sys.stderr); continue
    if len(data) != meta["n"] or fnv1a(data) != meta["fnv"]:
        print(f"attempt {attempt + 1}: checksum/size mismatch ({len(data)} of {meta['n']} B)", file=sys.stderr); continue
    with open(out, "wb") as f: f.write(data)
    print(f"{out}: {meta['w']}x{meta['h']} {len(data)} B"); sys.exit(0)
sys.exit(1)

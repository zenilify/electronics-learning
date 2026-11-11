#!/usr/bin/env python3
"""Live subtitles end to end with QUIET speech - the case that showed nothing on the Tab5 on 2026-11-11.

  python3 server/tools/probe_stream_quiet.py [BASE_URL ...]
  default: cachy on the LAN and Cloud Run

Builds 11 s of sound at the level the Tab5's microphone delivers (speech RMS 200 over a noise floor of 70): the
Spanish demo clip at 2.0 s and the Chinese one at 7.2 s, sends it in 1 s chunks like the firmware does
(/stream/start?lang=auto&tgt=en, /stream/chunk, /stream/stop) and prints the captions that come back. This goes
through the real recogniser and translator, so it costs a few upstream calls. Expected: the Spanish sentence(s)
and the Chinese one, each with an English translation.

The bearer token is read from ~/.config/zenilify/papp_token inside this process (never on a command line).
"""
import array, json, math, os, random, sys, time, urllib.request, wave

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.join(HERE, "..", "..", "client", "tab5", "main", "assets", "demo")
BASES = sys.argv[1:] or ["http://192.168.1.5:8787", "https://papp-138753081869.us-central1.run.app"]
TOKEN = open(os.path.expanduser("~/.config/zenilify/papp_token")).read().strip()


def clip(n):
    w = wave.open(os.path.join(DEMO, f"d{n}_src.wav"))
    return array.array("h", w.readframes(w.getnframes()))


def rms(a):
    return math.sqrt(sum(x * x for x in a) / len(a))


def call(base, path, body=b""):
    r = urllib.request.Request(base + path, data=body, method="POST",
                               headers={"Authorization": "Bearer " + TOKEN, "Content-Type": "application/octet-stream"})
    return json.load(urllib.request.urlopen(r, timeout=60))


rnd = random.Random(3)
sig = array.array("h", (int(rnd.gauss(0, 70)) for _ in range(16000 * 11)))
for n, at in ((1, 2.0), (2, 7.2)):
    c = clip(n); g = 200 / rms(c); o = int(at * 16000)
    for i, x in enumerate(c):
        if o + i < len(sig):
            sig[o + i] = max(-32768, min(32767, sig[o + i] + int(x * g)))

bad = 0
for base in BASES:
    try:
        sid = call(base, "/stream/start?lang=auto&tgt=en", b"{}")["sid"]; fin = []; worst = 0.0; out = {}
        for i in range(0, len(sig), 16000):
            t1 = time.time(); out = call(base, f"/stream/chunk?sid={sid}", sig[i:i + 16000].tobytes()); worst = max(worst, time.time() - t1)
            fin += out["final"]
        fin += call(base, f"/stream/stop?sid={sid}")["final"]
        print(f"{base}: {len(fin)} captions, slowest chunk {worst * 1000:.0f} ms, noise floor the server saw {out.get('noise')}")
        for f in fin:
            print(f"   [{f.get('lang')}] {f.get('text')}  ->  {f.get('tgt_text', '')}")
        bad += not fin
    except Exception as e:  # noqa: BLE001
        print(f"{base}: FAILED {type(e).__name__} {str(e)[:120]}"); bad += 1
sys.exit(1 if bad else 0)

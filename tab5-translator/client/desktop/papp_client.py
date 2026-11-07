#!/usr/bin/env python3
"""PAPP desktop test client — the reference the Tab5 firmware mirrors byte for byte.

Stdlib only. Sends the WAV as a raw `audio/wav` body (exactly what the Tab5 does),
asks for `format=wav`, plays/saves the reply and prints the X-Papp-* headers.

  papp_client.py --url http://100.69.244.21:8787 --token-file ~/.config/zenilify/papp_token \
                 --src es --tgt zh in.wav [-o out.wav] [--play] [--repeat 3]
  --token-file -   reads the token from stdin (keeps it off argv and off disk):
  ssh cachy cat .config/zenilify/papp_token | papp_client.py --token-file - ...
"""
import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
import urllib.parse
import urllib.request


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--url", default=os.environ.get("PAPP_URL", "http://100.69.244.21:8787"))
    ap.add_argument("--token-file", default=os.path.expanduser("~/.config/zenilify/papp_token"))
    ap.add_argument("--src", required=True, choices=["es", "en", "zh"])
    ap.add_argument("--tgt", required=True, choices=["es", "en", "zh"])
    ap.add_argument("--rate", type=int, default=16000)
    ap.add_argument("-o", "--out", default="papp_out.wav")
    ap.add_argument("--play", action="store_true")
    ap.add_argument("--repeat", type=int, default=1)
    a = ap.parse_args()

    token = (sys.stdin.readline() if a.token_file == "-" else open(a.token_file).read()).strip()
    body = open(a.wav, "rb").read()
    q = urllib.parse.urlencode({"src": a.src, "tgt": a.tgt, "format": "wav", "rate": a.rate})
    rtts = []
    for i in range(a.repeat):
        req = urllib.request.Request(f"{a.url.rstrip('/')}/translate?{q}", data=body, method="POST",
                                     headers={"Content-Type": "audio/wav",
                                              "Authorization": f"Bearer {token}"})
        t0 = time.perf_counter()
        with urllib.request.urlopen(req, timeout=90) as r:
            out = r.read()
            h = r.headers
        rtt = (time.perf_counter() - t0) * 1000
        rtts.append(rtt)
        print(json.dumps({
            "run": i + 1, "client_rtt_ms": round(rtt),
            "server_ms": json.loads(h.get("X-Papp-Timings", "{}")),
            "asr_model": h.get("X-Papp-Asr-Model"),
            "src_text": urllib.parse.unquote(h.get("X-Papp-Src-Text", "")),
            "tgt_text": urllib.parse.unquote(h.get("X-Papp-Tgt-Text", "")),
            "out_bytes": len(out)}, ensure_ascii=False), flush=True)
    with open(a.out, "wb") as fh:
        fh.write(out)
    if a.repeat > 1:
        print(f"rtt ms: min {min(rtts):.0f} median {statistics.median(rtts):.0f} max {max(rtts):.0f}")
    if a.play:
        player = shutil.which("afplay") or shutil.which("aplay") or shutil.which("paplay")
        if player:
            subprocess.run([player, a.out], check=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())

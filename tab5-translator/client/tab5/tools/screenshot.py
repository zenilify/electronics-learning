#!/usr/bin/env python3
"""Screenshot of the Tab5 screen over the USB console — no reset, no SD card, no network (lane T5, 2026-11-11).

  screenshot.py OUT.jpg [--port PORT] [--pre "key esc" ...] [--wait S] [--quality Q]
  e.g. screenshot.py /tmp/home.jpg --pre "key esc"          home screen
       screenshot.py /tmp/ajustes.jpg --pre "key a" --wait 2 open Ajustes (shortcut a), wait 2 s, shoot

The firmware (main/shot.c, console `shot [quality]`) renders the active LVGL screen, JPEG-encodes it on the P4 and prints
base64 lines that start with "@@". Everything else on the console (normal log lines) is ignored. The image is checked
with FNV-1a 32 and the transfer is retried up to 3 times. Exit code 0 = file written.
"""
import argparse, base64, os, select, sys, termios, time

PORT = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00"

def open_noreset(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL          # no HUPCL: closing must not drop DTR/RTS (= reset)
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

def lines(fd, seconds):
    """yield complete lines for up to `seconds` (bytes, no newline)"""
    buf, t0 = b"", time.time()
    while time.time() - t0 < seconds:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r: continue
        try: d = os.read(fd, 65536)
        except BlockingIOError: continue
        buf += d
        while b"\n" in buf:
            ln, buf = buf.split(b"\n", 1)
            yield ln.rstrip(b"\r")

def fnv1a(b):
    h = 2166136261
    for x in b: h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h

def shoot(fd, quality):
    os.write(fd, f"\nshot {quality}\n".encode())
    meta, chunks = None, []
    for ln in lines(fd, 40):
        if not ln.startswith(b"@@"): continue
        s = ln[2:].decode("ascii", "replace")
        if s.startswith("SHOT FAIL"): return None, s
        if s.startswith("SHOT BEGIN"):
            p = s.split(); meta = dict(w=int(p[2]), h=int(p[3]), n=int(p[4]), fnv=int(p[5], 16), ms=int(p[6])); chunks = []
        elif s.startswith("SHOT END"):
            if not meta: return None, "END without BEGIN"
            try: data = base64.b64decode("".join(chunks))
            except Exception as ex: return None, f"base64: {ex}"
            if len(data) != meta["n"]: return None, f"length {len(data)} != {meta['n']}"
            if fnv1a(data) != meta["fnv"]: return None, "checksum mismatch"
            return data, meta
        elif meta is not None: chunks.append(s)
    return None, "timeout"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("--port", default=PORT)
    ap.add_argument("--pre", action="append", default=[], help="console command(s) to send first, e.g. 'key a'")
    ap.add_argument("--wait", type=float, default=1.2, help="seconds to wait after the last --pre command")
    ap.add_argument("--quality", type=int, default=90)
    a = ap.parse_args()
    fd = open_noreset(a.port)
    try:
        for _ in lines(fd, 0.3): pass                           # drain whatever the board said before us
        for c in a.pre:
            os.write(fd, b"\n" + c.encode() + b"\n"); time.sleep(0.6)
        if a.pre:
            for _ in lines(fd, a.wait): pass
        for attempt in range(3):
            data, info = shoot(fd, a.quality)
            if data:
                with open(a.out, "wb") as f: f.write(data)
                print(f"{a.out}: {info['w']}x{info['h']} {len(data)} B (encode {info['ms']} ms, try {attempt + 1})")
                return 0
            print(f"try {attempt + 1} failed: {info}", file=sys.stderr); time.sleep(1)
        return 1
    finally:
        os.close(fd)

if __name__ == "__main__":
    sys.exit(main())

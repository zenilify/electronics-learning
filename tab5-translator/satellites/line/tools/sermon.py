#!/usr/bin/env python3
"""sermon.py — read (and optionally write lines to) a satellite's USB serial console, by-id only.
  sermon.py <MAC-or-suffix> [seconds] [line ...]     e.g. sermon.py CE:8B:10 15 '?' 'LOT2611A-001'
Lines are sent 3 s after opening, 0.7 s apart. Does not reset the board (DTR/RTS left low)."""
import glob, sys, time, serial
pat = sys.argv[1].upper()
ports = [p for p in glob.glob('/dev/serial/by-id/*') if pat in p.upper()]
if len(ports) != 1: sys.exit(f'need exactly one port for {pat}, got {len(ports)}')
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10
lines = sys.argv[3:]
s = serial.Serial(); s.port = ports[0]; s.baudrate = 115200; s.timeout = 0.2; s.dtr = False; s.rts = False; s.open()
t0 = time.time(); nxt = t0 + 3
while time.time() - t0 < secs:
    d = s.read(4096)
    if d: sys.stdout.write(d.decode('utf-8', 'replace')); sys.stdout.flush()
    if lines and time.time() >= nxt:
        s.write((lines.pop(0) + '\n').encode()); nxt = time.time() + 0.7

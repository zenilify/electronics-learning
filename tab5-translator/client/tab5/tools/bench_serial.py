#!/usr/bin/env python3
"""Bench test over the Tab5 USB-C console: open the port (resets the board), wait for boot + Wi-Fi,
send `status`, `demo` (5 canned turns through the live pipeline) and `status` again; print the log.
  bench_serial.py PORT [SECONDS]"""
import serial, sys, time
port, dur = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 120
s = serial.Serial(port, 115200, timeout=0.5)
t0, sent = time.time(), set()
cmds = [(14, b"status\n"), (16, b"demo\n"), (dur - 5, b"status\n")]
while time.time() - t0 < dur:
    d = s.read(4096)
    if d:
        sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
    for at, c in cmds:
        if at not in sent and time.time() - t0 > at:
            s.write(c); sent.add(at)

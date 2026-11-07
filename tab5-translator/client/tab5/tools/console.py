#!/usr/bin/env python3
"""Send console commands to the Tab5 over USB-C and print the log.
  console.py PORT SECONDS [AT:COMMAND ...]   e.g.  console.py /dev/ttyACM2 120 14:c6ver 16:"c6ota http://192.168.1.5:8099/network_adapter.bin"
Opening the port resets the board; AT is seconds after open."""
import serial, sys, time
port, dur = sys.argv[1], int(sys.argv[2])
cmds = []
for a in sys.argv[3:]:
    at, c = a.split(":", 1); cmds.append((float(at), c.encode() + b"\n"))
s = serial.Serial(port, 115200, timeout=0.5)
t0, sent = time.time(), set()
while time.time() - t0 < dur:
    d = s.read(4096)
    if d: sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
    for i, (at, c) in enumerate(cmds):
        if i not in sent and time.time() - t0 > at:
            try: s.write(c); sent.add(i)
            except serial.SerialException: pass

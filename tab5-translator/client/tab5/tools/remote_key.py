#!/usr/bin/env python3
"""Give the Tab5 the remotes' HMAC key and accept devices, over the USB console (protocol v1, satellites/PROTOCOL.md).

  python3 tools/remote_key.py [--accept stick-353c,dial-944c,fake-0001] [--wait 25] [--log SECONDS]

The key comes from ~/.config/zenilify/t5-remote-key (0600, 32 hex chars) and goes straight from this process to the
serial port — never on argv, never printed (the Tab5 logs only "len 16, sha256 xxxxxxxx"). Opening the port may reset the
Tab5, so the commands are sent after --wait seconds. Runs on cachy with the host python3 + pyserial.
"""
import argparse, hashlib, os, sys, time
import serial

DEV = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00"
ap = argparse.ArgumentParser()
ap.add_argument("--accept", default="stick-353c,dial-944c")
ap.add_argument("--wait", type=float, default=25)
ap.add_argument("--log", type=float, default=20)
ap.add_argument("--key-file", default="~/.config/zenilify/t5-remote-key")
a = ap.parse_args()
p = os.path.expanduser(a.key_file)
if os.stat(p).st_mode & 0o077:
    sys.exit("key file must be chmod 600")
key = open(p).read().strip()
if len(bytes.fromhex(key)) != 16:
    sys.exit("key must be 16 bytes")
print(f"key len 16, sha256 {hashlib.sha256(bytes.fromhex(key)).hexdigest()[:8]}", flush=True)
s = serial.Serial(DEV, 115200, timeout=0.3)
cmds = [(a.wait, b"rkey " + key.encode() + b"\n")] + \
       [(a.wait + 1 + i * 0.5, f"remote accept {d}\n".encode()) for i, d in enumerate(x for x in a.accept.split(",") if x)]
cmds.append((a.wait + a.log - 2, b"remote list\n"))
t0, i = time.time(), 0
while time.time() - t0 < a.wait + a.log:
    d = s.read(4096)
    if d:
        txt = d.decode("utf-8", "replace")
        if key in txt:                       # never echo the key, even if the firmware ever did
            txt = txt.replace(key, "<key>")
        sys.stdout.write(txt); sys.stdout.flush()
    while i < len(cmds) and time.time() - t0 > cmds[i][0]:
        s.write(cmds[i][1]); i += 1
key = "0" * 32

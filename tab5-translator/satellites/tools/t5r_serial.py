#!/usr/bin/env python3
"""PAPP remotes — USB serial helper (cachy). Ports are by-id only; secrets go from 0600 files straight to the device.

  t5r_serial.py mkkey                         create ~/.config/zenilify/t5-remote-key (0600) if absent; prints len + sha prefix
  t5r_serial.py <sticks3|dial> provision [--host 192.168.1.5]
                                              key + Wi-Fi slots (from ~/.config/zenilify/t5-wifi, "ssid<TAB>psk" per line) -> device NVS
  t5r_serial.py <sticks3|dial> cmd "info" [--wait 3] [--until linked]
                                              send one non-secret console command, print the device's "T5R ..." lines

Nothing secret is accepted on the command line and nothing secret is printed: the firmware answers with lengths / hash prefixes only.
Opening the port resets the app (USB-Serial-JTAG); the tool waits for "T5R ready".
"""
import argparse, hashlib, json, os, secrets, sys, time

MAC = {"sticks3": "14:C1:9F:D5:35:3C", "dial": "70:AF:09:DE:94:4C"}
KEYF = os.path.expanduser("~/.config/zenilify/t5-remote-key")
WIFIF = os.path.expanduser("~/.config/zenilify/t5-wifi")


def port(name):
    return f"/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_{MAC[name]}-if00"


def mkkey():
    if not os.path.exists(KEYF):
        fd = os.open(KEYF, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(secrets.token_hex(16) + "\n")
        made = True
    else:
        made = False
    mode = os.stat(KEYF).st_mode & 0o777
    k = bytes.fromhex(open(KEYF).read().strip())
    print(json.dumps({"key_file": KEYF, "created": made, "mode": oct(mode), "len": len(k), "sha": hashlib.sha256(k).hexdigest()[:8]}))
    return k


class Dev:
    def __init__(self, name):
        import serial
        self.s = serial.Serial()
        self.s.port = port(name)
        self.s.baudrate = 115200
        self.s.timeout = 0.2
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        self.buf = b""

    def lines(self, secs, stop=None):
        t0 = time.time()
        got = []
        while time.time() - t0 < secs:
            self.buf += self.s.read(512)
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                l = l.decode("utf-8", "replace").strip()
                if l.startswith("T5R "):
                    got.append(l)
                    if stop and stop(l):
                        return got
        return got

    def send(self, text: str):
        self.s.write(text.encode() + b"\n")
        self.s.flush()

    def wait_ready(self):
        # the open() may or may not have reset the app: wait for the banner, else poke with info
        if any(l.startswith("T5R ready") for l in self.lines(4, lambda l: l.startswith("T5R ready"))):
            return True
        for _ in range(5):
            self.send("T5R info")
            if self.lines(1.5, lambda l: l.startswith("T5R info")):
                return True
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target")
    ap.add_argument("action", nargs="?")
    ap.add_argument("text", nargs="?")
    ap.add_argument("--host", default="")
    ap.add_argument("--wait", type=float, default=3)
    ap.add_argument("--until", default="", help="poll info until link == this value (max --wait s)")
    a = ap.parse_args()
    if a.target == "mkkey":
        mkkey(); return
    if a.target not in MAC:
        sys.exit("target: sticks3 | dial | mkkey")
    d = Dev(a.target)
    if not d.wait_ready():
        sys.exit("device did not answer (no 'T5R ready' / 'T5R info')")
    if a.action == "provision":
        k = open(KEYF).read().strip()
        if os.stat(KEYF).st_mode & 0o077 or len(k) != 32:
            sys.exit("key file must be 0600 with 32 hex chars")
        ok = False
        for _ in range(4):                      # the first line after a port-open reset can be lost: ping, then retry
            d.send("T5R info")
            d.lines(1.5, lambda l: l.startswith("T5R info"))
            d.send("T5R key " + k)
            for l in d.lines(2, lambda l: l.startswith(("T5R ok key", "T5R err"))):
                if l.startswith(("T5R ok", "T5R err")): print(l)
                ok = ok or l.startswith("T5R ok key")
            if ok: break
        if not ok: sys.exit("key was not acknowledged by the device")
        i = 0
        for raw in open(WIFIF).read().splitlines():
            if "\t" not in raw or i > 3:
                continue
            d.send(f"T5R wifi {i} {raw}")
            for l in d.lines(2, lambda l: l.startswith(("T5R ok wifi", "T5R err"))):
                if l.startswith(("T5R ok", "T5R err")): print(l)
            i += 1
        d.send("T5R host " + (a.host or "-"))
        for l in d.lines(2, lambda l: l.startswith(("T5R ok host", "T5R err"))):
            if l.startswith(("T5R ok", "T5R err")): print(l)
        d.send("T5R rejoin")
        d.lines(1)
        a.action, a.text, a.until, a.wait = "cmd", "info", "linked", max(a.wait, 40)
    if a.action == "cmd":
        t = (a.text or "info").strip()
        if t.split()[0] in ("key", "wifi"):
            sys.exit("secrets are not accepted on the command line; use 'provision'")
        if a.until:
            t0 = time.time()
            last = ""
            while time.time() - t0 < a.wait:
                d.send("T5R info")
                for l in d.lines(1.5, lambda l: l.startswith("T5R info")):
                    if l.startswith("T5R info"):
                        last = l
                    elif l.startswith("T5R log"):
                        print(l)
                if f'"link":"{a.until}"' in last:
                    break
            print(last)
            if t != "info":
                d.send("T5R " + t)
                for l in d.lines(a.wait): print(l)
        else:
            d.send("T5R " + t)
            for l in d.lines(a.wait): print(l)


if __name__ == "__main__":
    main()

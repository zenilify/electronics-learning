#!/usr/bin/env python3
"""Log the Tab5's `status` lines to a CSV every few minutes WITHOUT resetting the board (lane T5, 2026-11-11).

  watch_status.py [--every 600] [--hours 24] [--out ~/t5/watch] [--port PORT]

One row per reading: local time, uptime, Wi-Fi, server, zfix (Wi-Fi watchdog), battery V / A, free RAM, firmware,
camera address, UI restarts, charger kicks. A reading whose uptime is smaller than the previous one is marked REBOOT.
The port is open for about 3 s per reading; if something else uses the console at that moment the row says NO-ANSWER.
Stop it: touch <out>/STOP (or kill the process). Meant for the "watch it for 24 h" jobs: zfix, charging, UI restarts.
"""
import argparse, csv, os, re, select, sys, termios, time

PORT = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00"
ANSI = re.compile(r"\x1b\[[0-9;]*m")

def ask(port, cmd=b"status\n", listen=3.0):
    try:
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as e:
        return f"OPEN-FAILED {e}"
    try:
        a = termios.tcgetattr(fd)
        a[0] = 0; a[1] = 0; a[3] = 0
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL      # no HUPCL, DTR/RTS never written: no reset
        a[4] = a[5] = termios.B115200
        a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, a)
        buf, t0, sent = b"", time.time(), False
        while time.time() - t0 < listen:
            if not sent and time.time() - t0 > 0.4:
                os.write(fd, cmd); sent = True
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try: buf += os.read(fd, 8192)
                except BlockingIOError: pass
        return ANSI.sub("", buf.decode("utf-8", "replace"))
    finally:
        os.close(fd)

def field(text, pat, default=""):
    m = re.search(pat, text)
    return m.group(1) if m else default

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--every", type=int, default=600); ap.add_argument("--hours", type=float, default=24)
    ap.add_argument("--out", default="~/t5/watch"); ap.add_argument("--port", default=PORT)
    a = ap.parse_args()
    out = os.path.expanduser(a.out); os.makedirs(out, exist_ok=True)
    path = os.path.join(out, time.strftime("status-%Y%m%d-%H%M.csv"))
    cols = ["time", "note", "up_s", "wifi", "rssi", "ep", "zfix", "health_ms", "batt_v", "batt_a", "free_int_kb", "fw", "cam", "ui_restarts", "chg_kicks"]
    with open(path, "w", newline="") as f: csv.writer(f).writerow(cols)
    t_end, last_up = time.time() + a.hours * 3600, None
    print("logging to", path, flush=True)
    while time.time() < t_end and not os.path.exists(os.path.join(out, "STOP")):
        txt = ask(a.port)
        up = field(txt, r" up=(\d+) s")
        note = "" if up else ("NO-ANSWER" if not txt.startswith("OPEN-FAILED") else txt[:60])
        if up and last_up is not None and int(up) < last_up: note = "REBOOT"
        if up: last_up = int(up)
        row = [time.strftime("%Y-%m-%d %H:%M:%S"), note, up, field(txt, r"status wifi=(\d)"), field(txt, r" rssi=(-?\d+)"), field(txt, r" ep=(-?\d+)"),
               field(txt, r" zfix=(\d+)"), field(txt, r" health=(\d+) ms"), field(txt, r" batt=([0-9.]+) V"), field(txt, r" V (-?[0-9.]+) A"),
               field(txt, r" free_int=(\d+) kB"), field(txt, r" v=([0-9.]+)"), field(txt, r"status2 cam=(\S+)"), field(txt, r"ui_restarts=(\d+)"),
               field(txt, r"chg_kicks=(\d+)")]
        with open(path, "a", newline="") as f: csv.writer(f).writerow(row)
        for _ in range(a.every):
            if os.path.exists(os.path.join(out, "STOP")): break
            time.sleep(1)
    return 0

if __name__ == "__main__":
    sys.exit(main())

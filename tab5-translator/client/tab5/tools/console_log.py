#!/usr/bin/env python3
"""Continuous log of the Tab5 console WITHOUT resetting the board (lane T5, 2026-11-11).

  console_log.py [--every 300] [--out ~/t5/watch] [--port PORT]

Why: on 2026-11-11 09:15 the UI watchdog restarted the Tab5 twice and the only trace was a counter, because
tools/watch_status.py listens for 3 s every 5 minutes. This one keeps the port open and writes every console line with
the wall-clock time in front (America/Mexico_City) to <out>/console-YYYYMMDD.log, so a freeze, a restart or an error
shows what the firmware was doing. It also sends `status` every --every seconds and appends the answer to
<out>/status-YYYYMMDD.csv (same columns as watch_status.py), so the 24 h watch continues.

Only one reader may own the port: do not run watch_status.py, console_nr.py or another copy at the same time (a lock
file refuses a second copy). To send the board a command while this runs, append a line to <out>/inbox.txt
(echo status >> ~/t5/watch/inbox.txt): it is written to the console and logged as "## sent: ...". Every command is sent with an empty line in front: the
first command after a reconnect used to get lost (2026-11-11: a `bod off` vanished that way), because it was glued to
whatever was left in the board's line buffer. The Tab5 may be unplugged and plugged back: the log notes it and carries on.
Stop it: kill the process (pid in <out>/console_log.pid). STOP IT BEFORE FLASHING the Tab5.
Lines that start with "##" are ours, everything else is the board.
"""
import argparse, csv, fcntl, os, re, select, sys, termios, time

os.environ["TZ"] = "America/Mexico_City"; time.tzset()
PORT = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00"
ANSI = re.compile(r"\x1b\[[0-9;]*m")
COLS = ["time", "note", "up_s", "wifi", "rssi", "ep", "zfix", "health_ms", "batt_v", "batt_a", "free_int_kb", "fw", "cam",
        "ui_restarts", "chg_kicks", "int_largest_kb", "boot_reason", "cpu_resets_fixed"]

def open_noreset(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL          # no HUPCL, DTR/RTS never written: no reset
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

def f(text, pat):
    m = re.search(pat, text)
    return m.group(1) if m else ""

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--every", type=int, default=300); ap.add_argument("--out", default="~/t5/watch"); ap.add_argument("--port", default=PORT)
    a = ap.parse_args()
    out = os.path.expanduser(a.out); os.makedirs(out, exist_ok=True)
    lock = open(os.path.join(out, ".console_log.lock"), "w")
    try: fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        print("another console_log.py is running (lock held) - not starting", file=sys.stderr); return 3
    with open(os.path.join(out, "console_log.pid"), "w") as p: p.write(str(os.getpid()) + "\n")

    def logline(s):
        t = time.time()
        with open(os.path.join(out, time.strftime("console-%Y%m%d.log", time.localtime(t))), "a") as fh:
            fh.write(time.strftime("%H:%M:%S", time.localtime(t)) + f".{int(t * 1000) % 1000:03d} " + s + "\n")

    def csvrow(note, block):
        path = os.path.join(out, time.strftime("status-%Y%m%d.csv"))
        new = not os.path.exists(path)
        row = [time.strftime("%Y-%m-%d %H:%M:%S"), note, f(block, r" up=(\d+) s"), f(block, r"status wifi=(\d)"), f(block, r" rssi=(-?\d+)"),
               f(block, r" ep=(-?\d+)"), f(block, r" zfix=(\d+)"), f(block, r" health=(\d+) ms"), f(block, r" batt=([0-9.]+) V"),
               f(block, r" V (-?[0-9.]+) A"), f(block, r" free_int=(\d+) kB"), f(block, r" v=([0-9.]+)"), f(block, r"status2 cam=(\S+)"),
               f(block, r"ui_restarts=(\d+)"), f(block, r"chg_kicks=(\d+)"), f(block, r"int_largest=(\d+) kB"),
               f(block, r"boot_reason=(0x[0-9a-f]+)"), f(block, r"cpu_resets_fixed=(\d+)")]
        with open(path, "a", newline="") as fh:
            w = csv.writer(fh)
            if new: w.writerow(COLS)
            w.writerow(row)

    logline(f"## console_log.py started, pid {os.getpid()}, status every {a.every} s")
    inbox = os.path.join(out, "inbox.txt")
    open(inbox, "a").close()
    inbox_pos = os.path.getsize(inbox)                           # only lines appended from now on are sent
    fd, buf, block, asked_at, next_status, last_up, boot_note = None, b"", "", 0.0, time.time() + 5, None, ""
    while True:
        if fd is None:
            try:
                fd = open_noreset(a.port); logline("## port open")
            except OSError:
                time.sleep(2); continue
        now = time.time()
        try:
            if os.path.getsize(inbox) > inbox_pos:
                with open(inbox) as fh:
                    fh.seek(inbox_pos); new = fh.read(); inbox_pos = fh.tell()
                for c in new.splitlines():
                    c = c.strip()
                    if not c: continue
                    try: os.write(fd, b"\n" + c.encode() + b"\n"); logline("## sent: " + c)
                    except OSError as e: logline(f"## could not send '{c}': {e}")
                    time.sleep(0.3)
        except OSError: pass
        if now >= next_status:
            try: os.write(fd, b"\nstatus\n")
            except BlockingIOError: pass
            except OSError: pass
            asked_at, block, next_status = now, "", now + a.every
        if asked_at and now - asked_at > 4:                      # the answer (4 lines) has had its time
            if " up=" in block:
                up = f(block, r" up=(\d+) s"); note = boot_note
                if up and last_up is not None and int(up) < last_up and not note: note = "REBOOT"
                if up: last_up = int(up)
                csvrow(note, block); boot_note = ""
            else: csvrow("NO-ANSWER", "")
            asked_at = 0.0
        try:
            r, _, _ = select.select([fd], [], [], 0.2)
            d = os.read(fd, 65536) if r else b""
            if r and not d: raise OSError("port closed")
        except BlockingIOError: d = b""
        except OSError as e:
            logline(f"## port gone ({e}) - waiting for the Tab5 to come back")
            try: os.close(fd)
            except OSError: pass
            fd, buf = None, b""; time.sleep(2); continue
        if not d: continue
        buf += d
        while b"\n" in buf:
            ln, buf = buf.split(b"\n", 1)
            s = ANSI.sub("", ln.decode("utf-8", "replace")).rstrip("\r")
            if not s.strip(): continue
            logline(s)
            if asked_at and "papp: status" in s: block += s + "\n"
            m = re.search(r"rst:(0x[0-9a-f]+) \((\w+)\)", s)
            if m: boot_note = f"REBOOT {m.group(1)} {m.group(2)}"
        if len(buf) > 8192: logline(ANSI.sub("", buf.decode("utf-8", "replace"))); buf = b""

if __name__ == "__main__":
    sys.exit(main())

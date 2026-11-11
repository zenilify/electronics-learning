#!/usr/bin/env python3
"""How much INTERNAL RAM does each PAPP screen cost? (lane T5, 2026-11-11) - no reset, about 1 minute per pass.

  ram_walk.py [--passes 2] [--dwell 3] [--port PORT] [--csv FILE]

For every app (home-screen shortcut keys t e f l c p n d s r o a) it reads `status` three times:
  home  -> free internal RAM on the home screen before the app
  open  -> with the app on screen
  back  -> on the home screen again
and prints one row per app. "kept" = home - back: what the app still holds after you left it (screens are built once
and kept). Pass 2 visits every app again: a "kept" that is still positive there is not a cache, it is a leak.
Why it matters: Wi-Fi (esp-hosted), TLS and LVGL's small allocations all live in internal RAM; the Tab5 has 32 MB of
PSRAM but only ~300 kB of internal RAM free after boot.
Nothing is recorded or played: Diálogo / Subtítulos / Notas are only opened, the camera shows its live preview.
"""
import argparse, os, re, select, sys, termios, time

PORT = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00"
APPS = [("t", "Traductor"), ("e", "Escribe"), ("f", "Frases"), ("l", "Llévame"), ("c", "Convertir"), ("p", "Cámara"),
        ("n", "Notas"), ("d", "Diálogo"), ("s", "Subtítulos"), ("r", "Controles"), ("o", "Línea"), ("a", "Ajustes")]

def open_noreset(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL          # no HUPCL: closing must not drop DTR/RTS (= reset)
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

def read_for(fd, seconds):
    buf, t0 = b"", time.time()
    while time.time() - t0 < seconds:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: buf += os.read(fd, 65536)
            except BlockingIOError: pass
    return buf.decode("utf-8", "replace")

def send(fd, cmd, wait):
    os.write(fd, b"\n" + cmd.encode() + b"\n")
    return read_for(fd, wait)

# From the `status4` line (firmware c16 and later). "min" is ESP-IDF's low-water figure: one minimum per heap region,
# added up - a worst-case bound, NOT a moment when that little was free (on this board it sits 74 kB under "free" from
# boot on). "blk" is the largest free piece right now, which is a real measurement.
LOW = {"min": None, "blk": None}

def status(fd):
    """(free internal kB, free PSRAM kB, uptime s) - asks up to 4 times (the first command after open can get lost)"""
    for _ in range(4):
        out = send(fd, "status", 1.2)
        m = re.search(r"free_int=(\d+) kB psram=(\d+) kB .*?up=(\d+) s", out)
        m4 = re.search(r"status4 int_min=(\d+) kB int_largest=(\d+) kB", out)
        if m4: LOW["min"], LOW["blk"] = int(m4.group(1)), int(m4.group(2))
        if m: return int(m.group(1)), int(m.group(2)), int(m.group(3))
    raise SystemExit("no answer to `status` - is the Tab5 running PAPP >= 0.4.2 and is the port free?")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--passes", type=int, default=2); ap.add_argument("--dwell", type=float, default=3.0)
    ap.add_argument("--port", default=PORT); ap.add_argument("--csv")
    a = ap.parse_args()
    fd = open_noreset(a.port)
    rows = []
    try:
        read_for(fd, 0.5)
        send(fd, "key esc", 0.8); send(fd, "key esc", 1.2)
        i0, p0, up = status(fd)
        print(f"start: internal {i0} kB free, PSRAM {p0} kB free, up {up} s"
              + (f", worst-case low-water {LOW['min']} kB, largest piece {LOW['blk']} kB" if LOW["min"] is not None else ""))
        for n in range(1, a.passes + 1):
            print(f"\npass {n}\n{'app':<11}{'home':>6}{'open':>6}{'back':>6}{'kept':>6}{'low':>6}{'piece':>6}"
                  "   (kB of internal RAM; kept = home - back; low = worst-case low-water, see top of file; piece = largest free piece)")
            for key, name in APPS:
                home, _, _ = status(fd)
                send(fd, f"key {key}", a.dwell)
                opened, _, _ = status(fd)
                send(fd, "key esc", 0.8); send(fd, "key esc", 1.5)
                back, ps, _ = status(fd)
                rows.append((n, name, home, opened, back, home - back, ps, LOW["min"], LOW["blk"]))
                print(f"{name:<11}{home:>6}{opened:>6}{back:>6}{home - back:>6}{str(LOW['min'] or '-'):>6}{str(LOW['blk'] or '-'):>6}")
            end, _, _ = status(fd)
            print(f"after pass {n}: internal {end} kB free ({i0 - end} kB less than at the start)")
    finally:
        os.close(fd)
    if a.csv:
        with open(a.csv, "w") as f:
            f.write("pass,app,home_kb,open_kb,back_kb,kept_kb,psram_kb,int_min_kb,int_largest_kb\n")
            for r in rows: f.write(",".join(str(x) for x in r) + "\n")
        print(f"csv: {a.csv}")
    return 0

if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Bench "wire" between the ESP-NOW bridge and the Tab5 (lane T5, 2026-11-11).

In the field the bridge hangs on the Tab5 (UART on the Grove port, or USB). On the bench both are on cachy's USB, so
this script is the cable: it copies the bridge's "R <mac> <rssi> <datagram>" lines into the Tab5 console as
`t5b R ...` and the Tab5's "@@T5B T <mac> <datagram>" answers back to the bridge as "T <mac> <datagram>".
Nothing is decoded here: HMAC, nonce and acks stay between the remote and the Tab5.

  bridge_relay.py --bridge /dev/serial/by-id/..._50:78:7D:CD:64:8C-if00 [--seconds 120] [--log ~/t5/espnow/relay.log]

Neither port is reset (plain os.open + termios, DTR/RTS never written, HUPCL off). Prints one line per frame and a
summary at the end: frames each way and the time the Tab5 took to answer (console poll + HMAC + print).
"""
import argparse, os, select, sys, termios, time

TAB5 = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E2:EF:41-if00"

def open_noreset(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bridge", required=True); ap.add_argument("--tab5", default=TAB5)
    ap.add_argument("--seconds", type=float, default=120); ap.add_argument("--log", default="")
    ap.add_argument("--quiet", action="store_true", help="summary only")
    a = ap.parse_args()
    fb, ft = open_noreset(a.bridge), open_noreset(a.tab5)
    logf = open(os.path.expanduser(a.log), "a") if a.log else None
    buf = {fb: b"", ft: b""}
    n_r = n_t = 0; lat = []; pend = {}          # pend: src mac -> time its last frame went into the Tab5
    t0 = time.time()
    def note(s):
        line = f"{time.time() - t0:8.3f} {s}"
        if not a.quiet: print(line, flush=True)
        if logf: logf.write(line + "\n"); logf.flush()
    os.write(fb, b"T5B info\n")
    try:
        while time.time() - t0 < a.seconds:
            r, _, _ = select.select([fb, ft], [], [], 0.02)
            for fd in r:
                try: d = os.read(fd, 65536)
                except BlockingIOError: continue
                buf[fd] += d
                while b"\n" in buf[fd]:
                    ln, buf[fd] = buf[fd].split(b"\n", 1)
                    ln = ln.rstrip(b"\r")
                    if fd == fb:
                        if ln.startswith(b"R "):
                            os.write(ft, b"t5b " + ln + b"\n"); n_r += 1
                            mac = ln[2:14].decode("ascii", "replace"); pend[mac] = time.time()
                            note("remote>tab5 " + ln[:150].decode("utf-8", "replace"))
                        elif ln.startswith(b"T5B"): note("bridge      " + ln.decode("utf-8", "replace"))
                    else:
                        i = ln.find(b"@@T5B T ")
                        if i >= 0:
                            pl = ln[i + 6:]                       # "T <mac> <datagram>"
                            os.write(fb, pl + b"\n"); n_t += 1
                            mac = pl[2:14].decode("ascii", "replace")
                            if mac in pend: lat.append((time.time() - pend.pop(mac)) * 1000)
                            note("tab5>remote " + pl[:150].decode("utf-8", "replace"))
                        elif b"remotes:" in ln and not a.quiet:
                            note("tab5 log    " + ln.decode("utf-8", "replace")[-110:])
        os.write(fb, b"T5B info\n"); time.sleep(0.3)
        try: note("bridge      " + os.read(fb, 4096).decode("utf-8", "replace").strip())
        except BlockingIOError: pass
    finally:
        os.close(fb); os.close(ft)
    lat.sort()
    med = lat[len(lat) // 2] if lat else 0
    print(f"SUMMARY remote>tab5={n_r} tab5>remote={n_t} tab5_answer_ms: n={len(lat)} min={lat[0] if lat else 0:.1f} median={med:.1f} max={lat[-1] if lat else 0:.1f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Talk to an ESP console over USB-Serial-JTAG WITHOUT resetting the board (lane T5, 2026-11-11).

  console_nr.py PORT SECONDS [AT:COMMAND ...]
  e.g. console_nr.py /dev/serial/by-id/usb-Espressif_..._30:ED:A0:E2:EF:41-if00 4 0.5:status

Why not console.py: pyserial writes the DTR and RTS lines one after the other. On the ESP32's built-in
USB-Serial-JTAG "RTS on, DTR off" means RESET, so opening or closing the port that way can reboot the board
(and a reboot wipes what we wanted to read: uptime, zfix, the remotes table).
Here the port is opened with plain os.open + termios and the modem lines are never written by us:
  - the kernel raises DTR+RTS together on open (one USB request: no reset),
  - HUPCL is cleared, so close() does not drop them again.
Runs on the host (user must be in group uucp); no docker, no pyserial.
Exit code 0 always; the caller greps the output. A line starting with "## " is ours, everything else is the board.
"""
import os, sys, time, termios, select

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    port, dur = sys.argv[1], float(sys.argv[2])
    cmds = []
    for a in sys.argv[3:]:
        at, c = a.split(":", 1)
        cmds.append((float(at), b"\n" + c.encode() + b"\n"))   # empty line first: ends whatever is left in the board's line buffer
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0                                                   # iflag: raw
    a[1] = 0                                                   # oflag: raw
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL        # cflag: 8N1, ignore modem status, NO HUPCL
    a[3] = 0                                                   # lflag: no echo, no canonical mode
    a[4] = a[5] = termios.B115200                              # ignored by USB CDC, set for tidiness
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    t0, sent, buf = time.time(), set(), b""
    try:
        while time.time() - t0 < dur:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try: d = os.read(fd, 4096)
                except BlockingIOError: d = b""
                if d:
                    sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
            for i, (at, c) in enumerate(cmds):
                if i not in sent and time.time() - t0 >= at:
                    os.write(fd, c); sent.add(i)
    finally:
        os.close(fd)
    return 0

if __name__ == "__main__":
    sys.exit(main())

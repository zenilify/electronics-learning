#!/usr/bin/env python3
"""Where is the Tab5's console port on this computer? (lane T5, 2026-11-11)

  t5port.py [SERIAL]     prints the device path; exit code 2 if that board is not connected
  t5port.py --list       every Espressif USB serial port that is connected, with its serial number

A board is found by the serial number its USB port reports (the chip's MAC address, e.g. 30:ED:A0:E2:EF:41 for Erick's
Tab5) - never by "the first port": on the bench and on the road several Espressif boards are plugged in at once, and
one of them must never be flashed.

  Linux (cachy):  /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<SERIAL>-if00   (udev makes this name)
  macOS (MacBook): the /dev/cu.usbmodem* whose USB serial number is SERIAL. Needs pyserial, so run it with the
                   Python of the tool folder:  ~/t5/venv/bin/python tools/t5port.py
"""
import glob, os, platform, sys

TAB5 = "30:ED:A0:E2:EF:41"

def _mac_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        sys.exit("pyserial is missing - run this with ~/t5/venv/bin/python (README: MacBook setup)")
    return [(p.device, (p.serial_number or ""), p.vid, p.pid, p.product or "") for p in list_ports.comports() if p.vid == 0x303A]

def find(serial=TAB5):
    """device path of the board with this USB serial number, or None"""
    if platform.system() == "Darwin":
        for dev, sn, _, _, _ in _mac_ports():
            if sn.upper() == serial.upper(): return dev
        return None
    p = f"/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_{serial}-if00"
    return p if os.path.exists(p) else None

def listing():
    if platform.system() == "Darwin":
        return [(dev, sn, f"{vid:04x}:{pid:04x} {prod}") for dev, sn, vid, pid, prod in _mac_ports()]
    out = []
    for p in sorted(glob.glob("/dev/serial/by-id/usb-Espressif_*-if00")):
        sn = os.path.basename(p).split("_")[-1].replace("-if00", "")
        out.append((p, sn, os.path.realpath(p)))
    return out

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--list":
        rows = listing()
        for dev, sn, extra in rows: print(f"{sn or '?':<20} {dev}  {extra}" + ("   <- the Tab5" if sn.upper() == TAB5 else ""))
        if not rows: print("no Espressif USB serial port connected")
        sys.exit(0)
    serial = sys.argv[1] if len(sys.argv) > 1 else TAB5
    dev = find(serial)
    if not dev:
        print(f"board {serial} is not connected to this computer", file=sys.stderr); sys.exit(2)
    print(dev)

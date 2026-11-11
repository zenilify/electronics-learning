#!/usr/bin/env python3
"""Are two builds of the app the same firmware? (lane T5, 2026-11-11)

  same_firmware.py A.bin B.bin      exit code 0 = same code and data, 1 = different, 2 = not comparable

Two builds of the same source are not byte-identical: ESP-IDF writes the compile time and date into the image, and two
checksums follow from it. Measured 2026-11-11 (fresh clone on cachy against the firmware on the Tab5): 69 bytes differ,
all in these fields, everything else is equal. This tool compares two app images and ignores exactly them:
  0x70..0x7f  compile time            0x80..0x8f  compile date        (ESP-IDF application descriptor)
  0xb0..0xcf  SHA-256 of the ELF file (same descriptor)
  the last 33 bytes: image checksum byte + appended SHA-256 of the image
Use it to check that a build made on another computer (the MacBook, the Foundry) is the firmware that was tested.
"""
import sys

IGNORE = [(0x70, 0x90), (0xB0, 0xD0)]

def main():
    if len(sys.argv) != 3: print(__doc__); return 2
    a, b = (open(p, "rb").read() for p in sys.argv[1:3])
    if len(a) != len(b):
        print(f"different: sizes {len(a)} and {len(b)} bytes"); return 1
    if a[0] != 0xE9 or b[0] != 0xE9:
        print("not ESP application images (first byte is not 0xE9)"); return 2
    tail = len(a) - 33
    skip = lambda i: i >= tail or any(lo <= i < hi for lo, hi in IGNORE)
    diff = [i for i in range(len(a)) if a[i] != b[i]]
    real = [i for i in diff if not skip(i)]
    t = lambda x: (x[0x70:0x80].split(b"\0")[0].decode("ascii", "replace"), x[0x80:0x90].split(b"\0")[0].decode("ascii", "replace"))
    print(f"A built {t(a)[1]} {t(a)[0]} · B built {t(b)[1]} {t(b)[0]} · {len(a)} bytes each")
    if real:
        print(f"DIFFERENT firmware: {len(real)} bytes differ outside the time stamp and checksum fields, first at 0x{real[0]:x}")
        return 1
    print(f"same firmware: {len(diff)} bytes differ, all of them in the compile time/date and the checksums that follow from it")
    return 0

if __name__ == "__main__":
    sys.exit(main())

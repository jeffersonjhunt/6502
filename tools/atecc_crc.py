#!/usr/bin/env python3
"""The ATECC608B's CRC-16, our own oracle for the emulator's (hw/design.md, "The random source in the emulator").

Polynomial $8005, the register starting at 0, each byte fed least significant bit first, no final reflection;
the chip sends it low byte first. `atecc_crc.py HEX...` prints the two bytes it would send for those bytes;
`atecc_crc.py --check` tests the four check values the spec gives (exit status 1 if one is wrong).
"""
import sys


def crc16(data):
    crc = 0
    for byte in data:
        for bit in range(8):
            if ((byte >> bit) & 1) ^ (crc >> 15):
                crc = ((crc << 1) ^ 0x8005) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


CHECKS = [  # the bytes, then the CRC as sent (low byte first)
    (bytes([0x04, 0x11]), bytes([0x33, 0x43])),              # the wake answer
    (bytes([0x07, 0x1B, 0x00, 0x00, 0x00]), bytes([0x24, 0xCD])),  # Random
    (bytes([0x07, 0x02, 0x00, 0x15, 0x00]), bytes([0x17, 0x5D])),  # the lock byte's Read
    (b"123456789", bytes([0xDD, 0xBC])),                     # $BCDD
]


def main(argv):
    if argv == ["--check"]:
        bad = 0
        for data, want in CHECKS:
            c = crc16(data)
            got = bytes([c & 0xFF, c >> 8])
            ok = got == want
            bad += not ok
            print(f"{'ok  ' if ok else 'FAIL'} {data.hex(' ')}: {got.hex(' ')}" + ("" if ok else f", want {want.hex(' ')}"))
        return 1 if bad else 0
    c = crc16(bytes(int(h, 16) for h in argv))
    print(f"{c & 0xFF:02X} {c >> 8:02X}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

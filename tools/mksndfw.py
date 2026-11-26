#!/usr/bin/env python3
"""Sound card revision 3 firmware images, for tests (hw/design.md, "The firmware image").

An image is a 32-byte header, then its body:

    +0-3    "OOSD"                  +12-15  the body's length
    +4      the card type ($A3)     +16-19  the body's CRC-32
    +5      the header's version, 1 +20-27  reserved, 0
    +6-7    the version, major then minor   +28-31  the CRC-32 of +0-27
    +8-9    the build               +10-11  reserved, 0

The CRC-32 is zlib's (polynomial $04C11DB7 reflected, from $FFFFFFFF, inverted). The emulator's card can't run
a new body (its firmware is its C++), so a body is any bytes: what is tested is the update's path (SNDFW.COM,
function 82's SN_FIRMWARE, FW_BEGIN and FW_COMMIT).

    python3 -I tools/mksndfw.py OUT [--version 1.1] [--build 2] [--card A3] [--body-bytes 4096]
                                    [--bad-crc body|header] [--magic OOSD]
"""

import argparse
import struct
import sys
import zlib


def image(version=(1, 1), build=2, card=0xA3, body=b"", bad_crc=None, magic=b"OOSD"):
    body_crc = zlib.crc32(body) & 0xFFFFFFFF
    if bad_crc == "body":
        body_crc ^= 1
    head = magic + bytes([card, 1, version[0], version[1]]) + struct.pack("<HH", build, 0)
    head += struct.pack("<II", len(body), body_crc) + bytes(8)
    head_crc = zlib.crc32(head) & 0xFFFFFFFF
    if bad_crc == "header":
        head_crc ^= 1
    return head + struct.pack("<I", head_crc) + body


def main(argv):
    p = argparse.ArgumentParser(description="Make a sound card revision 3 firmware image (for tests).")
    p.add_argument("out")
    p.add_argument("--version", default="1.1", help="major.minor, as FWVER will read")
    p.add_argument("--build", type=int, default=2)
    p.add_argument("--card", default="A3", help="the card type, hex")
    p.add_argument("--body-bytes", type=int, default=4096, help="a body of this many bytes of a pattern")
    p.add_argument("--bad-crc", choices=["body", "header"])
    p.add_argument("--magic", default="OOSD")
    a = p.parse_args(argv)
    major, minor = (int(x) for x in a.version.split("."))
    body = bytes((i * 7 + 3) & 0xFF for i in range(a.body_bytes))
    data = image((major, minor), a.build, int(a.card, 16), body, a.bad_crc, a.magic.encode("ascii"))
    with open(a.out, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main(sys.argv[1:])

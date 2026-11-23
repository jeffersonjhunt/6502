"""tools/mksndfw.py's images: the header the sound card's firmware checks (hw/future/sound-device.md, S1).

    python3 -I test_mksndfw.py path/to/tools/mksndfw.py
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib


def run(tool, out, *args):
    subprocess.run([sys.executable, "-I", tool, out, *args], check=True)
    with open(out, "rb") as f:
        return f.read()


def main():
    tool = sys.argv[1]
    failures = 0

    def check(what, ok):
        nonlocal failures
        print(("  ok   " if ok else "NOT OK ") + what)
        failures += 0 if ok else 1

    with tempfile.TemporaryDirectory() as d:
        good = run(tool, os.path.join(d, "good.bin"), "--version", "2.3", "--build", "9", "--body-bytes", "1000")
        check("the magic, the card type and the header's version", good[:6] == b"OOSD\xa3\x01")
        check("the version and the build", good[6:10] == bytes([2, 3, 9, 0]))
        length, crc = struct.unpack("<II", good[12:20])
        check("the body's length", length == 1000 and len(good) == 32 + 1000)
        check("the body's CRC-32", crc == zlib.crc32(good[32:]) & 0xFFFFFFFF)
        check("the header's CRC-32", struct.unpack("<I", good[28:32])[0] == zlib.crc32(good[:28]) & 0xFFFFFFFF)
        check("reserved bytes 0", good[10:12] == b"\0\0" and good[20:28] == bytes(8))
        bad = run(tool, os.path.join(d, "bad.bin"), "--version", "2.3", "--build", "9", "--body-bytes", "1000",
                  "--bad-crc", "body")
        check("--bad-crc body: only the body's CRC wrong", struct.unpack("<I", bad[16:20])[0] != crc and bad[32:] == good[32:])
        other = run(tool, os.path.join(d, "other.bin"), "--card", "A2")
        check("--card: another card's type", other[4] == 0xA2)
    print(f"\n{failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

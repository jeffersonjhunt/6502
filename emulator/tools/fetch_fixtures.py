#!/usr/bin/env python3
"""Fetch the third-party CPU test fixtures into test/host/fixtures/.

Neither suite is vendored. Dormann's tests are GPL-3.0 and the Harte set is
~1 GB of JSON, so both are fetched on demand from pinned commits and kept out
of git (test/host/fixtures/.gitignore).

  python3 tools/fetch_fixtures.py              # 100 cases per legal opcode
  python3 tools/fetch_fixtures.py --cases 1000 # more coverage, ~10x download

Harte files are fetched with an HTTP Range request rather than whole: the
first N cases are all a test needs, and taking a prefix of each file keeps the
download near 1 MB per opcode instead of 5. The truncated tail is discarded by
decoding objects one at a time and stopping at the first incomplete one.

Output for Harte is one case per line, all decimal, so the C++ side needs no
JSON parser:

  pc s a x y p  n (addr val)*n   pc s a x y p  n (addr val)*n   cycles
  \\_ initial _______________/  \\_ final ________________/
"""

import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import sys
import urllib.request

DORMANN_COMMIT = "7954e2dbb49c469ea286070bf46cdd71aeb29e4b"
DORMANN_URL = ("https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/"
               f"{DORMANN_COMMIT}/bin_files/6502_functional_test.bin")
DORMANN_SHA256 = "fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd"

HARTE_COMMIT = "2f6980a2d95757486c7bee24355c360e40e2a224"
HARTE_URL = ("https://raw.githubusercontent.com/SingleStepTests/65x02/"
             f"{HARTE_COMMIT}/6502/v1/{{op:02x}}.json")

# The 151 documented NMOS opcodes. The core runs everything else as a NOP, so
# testing undocumented opcodes would only measure that choice.
LEGAL_OPCODES = sorted({
    0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71,  # ADC
    0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31,  # AND
    0x0A, 0x06, 0x16, 0x0E, 0x1E,                    # ASL
    0x90, 0xB0, 0xF0, 0x30, 0xD0, 0x10, 0x50, 0x70,  # branches
    0x24, 0x2C,                                      # BIT
    0x00,                                            # BRK
    0x18, 0xD8, 0x58, 0xB8,                          # CLC CLD CLI CLV
    0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1,  # CMP
    0xE0, 0xE4, 0xEC, 0xC0, 0xC4, 0xCC,              # CPX CPY
    0xC6, 0xD6, 0xCE, 0xDE, 0xCA, 0x88,              # DEC DEX DEY
    0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51,  # EOR
    0xE6, 0xF6, 0xEE, 0xFE, 0xE8, 0xC8,              # INC INX INY
    0x4C, 0x6C, 0x20,                                # JMP JSR
    0xA9, 0xA5, 0xB5, 0xAD, 0xBD, 0xB9, 0xA1, 0xB1,  # LDA
    0xA2, 0xA6, 0xB6, 0xAE, 0xBE,                    # LDX
    0xA0, 0xA4, 0xB4, 0xAC, 0xBC,                    # LDY
    0x4A, 0x46, 0x56, 0x4E, 0x5E,                    # LSR
    0xEA,                                            # NOP
    0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11,  # ORA
    0x48, 0x08, 0x68, 0x28,                          # PHA PHP PLA PLP
    0x2A, 0x26, 0x36, 0x2E, 0x3E,                    # ROL
    0x6A, 0x66, 0x76, 0x6E, 0x7E,                    # ROR
    0x40, 0x60,                                      # RTI RTS
    0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1,  # SBC
    0x38, 0xF8, 0x78,                                # SEC SED SEI
    0x85, 0x95, 0x8D, 0x9D, 0x99, 0x81, 0x91,        # STA
    0x86, 0x96, 0x8E, 0x84, 0x94, 0x8C,              # STX STY
    0xAA, 0xA8, 0xBA, 0x8A, 0x9A, 0x98,              # transfers
})
assert len(LEGAL_OPCODES) == 151, len(LEGAL_OPCODES)

# Bytes per case is ~550 on average and ~1 kB for the longest (BRK, JSR, RTI);
# asking for 1.2 kB per case covers the worst opcode with room to spare.
BYTES_PER_CASE = 1200


def fetch(url, byte_range=None):
    req = urllib.request.Request(url)
    if byte_range:
        req.add_header("Range", f"bytes=0-{byte_range - 1}")
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read()


def fetch_dormann(out_dir):
    dest = out_dir / "6502_functional_test.bin"
    data = fetch(DORMANN_URL)
    digest = hashlib.sha256(data).hexdigest()
    if digest != DORMANN_SHA256:
        sys.exit(f"dormann: sha256 mismatch ({digest}), refusing to write {dest}")
    dest.write_bytes(data)
    print(f"dormann: {dest} ({len(data)} bytes, sha256 ok)")


def state_fields(st):
    ram = st["ram"]
    out = [st["pc"], st["s"], st["a"], st["x"], st["y"], st["p"], len(ram)]
    for addr, val in ram:
        out += [addr, val]
    return out


def harte_cases(op, cases):
    text = fetch(HARTE_URL.format(op=op), cases * BYTES_PER_CASE).decode("utf-8", "replace")
    dec = json.JSONDecoder()
    pos = text.index("[") + 1
    rows = []
    while len(rows) < cases:
        while pos < len(text) and text[pos] in " \t\r\n,":
            pos += 1
        try:
            case, pos = dec.raw_decode(text, pos)
        except json.JSONDecodeError:
            break  # reached the truncated tail of the ranged download
        row = state_fields(case["initial"]) + state_fields(case["final"]) + [len(case["cycles"])]
        rows.append(" ".join(str(v) for v in row))
    return op, rows


def fetch_harte(out_dir, cases):
    dest_dir = out_dir / "harte"
    dest_dir.mkdir(exist_ok=True)
    short = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        for op, rows in pool.map(lambda o: harte_cases(o, cases), LEGAL_OPCODES):
            if len(rows) < cases:
                short.append(f"{op:02x}:{len(rows)}")
            (dest_dir / f"{op:02x}.txt").write_text("\n".join(rows) + "\n")
    (dest_dir / "SOURCE").write_text(f"SingleStepTests/65x02 {HARTE_COMMIT} 6502/v1, {cases} cases/opcode\n")
    print(f"harte: {len(LEGAL_OPCODES)} opcodes x {cases} cases -> {dest_dir}")
    if short:
        sys.exit(f"harte: fewer cases than asked for {', '.join(short)}; raise BYTES_PER_CASE")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cases", type=int, default=100, help="Harte cases per opcode (default 100)")
    ap.add_argument("--out", type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parent.parent / "test" / "host" / "fixtures")
    ap.add_argument("--skip-harte", action="store_true")
    ap.add_argument("--skip-dormann", action="store_true")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    if not args.skip_dormann:
        fetch_dormann(args.out)
    if not args.skip_harte:
        fetch_harte(args.out, args.cases)


if __name__ == "__main__":
    main()

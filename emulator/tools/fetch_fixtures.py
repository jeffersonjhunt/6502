#!/usr/bin/env python3
"""Fetch the third-party CPU test fixtures into test/host/fixtures/.

Neither suite is vendored. Dormann's tests are GPL-3.0 and the Harte set is
~1 GB of JSON, so both are fetched on demand from pinned commits and kept out
of git (test/host/fixtures/.gitignore).

  python3 tools/fetch_fixtures.py              # 100 cases per opcode, both CPUs
  python3 tools/fetch_fixtures.py --cases 1000 # more coverage, ~10x download

Two CPUs, each with a Dormann image and a Harte set: the NMOS 6502 (244
opcodes: all but the jams) and the WDC 65C02, the real board's W65C02S (254
opcodes; Harte has no cases for WAI and STP).

Harte files are fetched with an HTTP Range request rather than whole: the
first N cases are all a test needs, and taking a prefix of each file keeps the
download near 1 MB per opcode instead of 5. The truncated tail is discarded by
decoding objects one at a time and stopping at the first incomplete one.

Both CPUs' sets are written to scratch directories beside the old ones and
swapped in only once all of both have arrived (both old sets out, then both
new in, undone on a failure), so a failed or interrupted fetch leaves what
was there; what a killed one leaves is swept up by the next run. The Harte files have no published hashes (and
a ranged prefix could not match one): the URL pins the commit, and each
case is checked to run the opcode its file is named for.

Output for Harte is one case per line, all decimal, so the C++ side needs no
JSON parser (format 2, which added the bus cycles):

  pc s a x y p  n (addr val)*n   pc s a x y p  n (addr val)*n   c (addr val w)*c
  \\_ initial _______________/  \\_ final ________________/   \\_ bus cycles __/

where w is 1 for a write and 0 for a read.
"""

import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import shutil
import sys
import tempfile
import urllib.request

DORMANN_COMMIT = "7954e2dbb49c469ea286070bf46cdd71aeb29e4b"
DORMANN_URL = ("https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/"
               f"{DORMANN_COMMIT}/bin_files/{{name}}")
# The test runners hard-code each image's start and success addresses, so a
# different build of either image must fail here, not there.
DORMANN_IMAGES = {
    "6502_functional_test.bin": "fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd",
    "65C02_extended_opcodes_test.bin": "10a2a07fa240666fa610c46accebe8d42b1000feef3aae619da15a8d152869b2",
}

HARTE_COMMIT = "2f6980a2d95757486c7bee24355c360e40e2a224"
HARTE_URL = ("https://raw.githubusercontent.com/SingleStepTests/65x02/"
             f"{HARTE_COMMIT}/{{cpu}}/v1/{{op:02x}}.json")
HARTE_FORMAT = 2

# Every NMOS opcode but the twelve that jam the CPU: the documented 151 and
# the undocumented ones (LAX, SAX, DCP, the unstable ANE and SHA, ...), which
# a 6510's tunes use. A jam has no end, so Harte's cases for one record an
# arbitrary number of cycles; the core just stops (test_cpu_smoke checks it).
JAM_OPCODES = [0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x92, 0xB2, 0xD2, 0xF2]
NMOS_OPCODES = [op for op in range(256) if op not in JAM_OPCODES]
assert len(NMOS_OPCODES) == 244, len(NMOS_OPCODES)

# Every opcode but WAI ($CB) and STP ($DB), which Harte cannot test.
WDC_OPCODES = [op for op in range(256) if op not in (0xCB, 0xDB)]

HARTE_SETS = {"6502": NMOS_OPCODES, "wdc65c02": WDC_OPCODES}

# Bytes per case is ~550 on average and ~1 kB for the longest (BRK, JSR, RTI);
# asking for 1.5 kB per case covers the worst opcode with room to spare.
BYTES_PER_CASE = 1500
HARTE_CASES_PER_FILE = 10000


def positive_cases(text):
    n = int(text) if text.isdigit() else 0
    if not 1 <= n <= HARTE_CASES_PER_FILE:
        raise argparse.ArgumentTypeError(f"want 1 to {HARTE_CASES_PER_FILE}, not {text!r}")
    return n


def fetch(url, byte_range=None):
    req = urllib.request.Request(url)
    if byte_range:
        req.add_header("Range", f"bytes=0-{byte_range - 1}")
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read()


def fetch_dormann(out_dir):
    for name, sha in DORMANN_IMAGES.items():
        dest = out_dir / name
        data = fetch(DORMANN_URL.format(name=name))
        digest = hashlib.sha256(data).hexdigest()
        if digest != sha:
            sys.exit(f"dormann: {name} sha256 mismatch ({digest}), refusing to write {dest}")
        part = dest.with_name(dest.name + ".part")
        part.write_bytes(data)
        part.replace(dest)  # never half a file under the real name
        print(f"dormann: {dest} ({len(data)} bytes, sha256 ok)")


def state_fields(st):
    ram = st["ram"]
    out = [st["pc"], st["s"], st["a"], st["x"], st["y"], st["p"], len(ram)]
    for addr, val in ram:
        out += [addr, val]
    return out


def harte_cases(cpu, op, cases):
    text = fetch(HARTE_URL.format(cpu=cpu, op=op), cases * BYTES_PER_CASE).decode("utf-8", "replace")
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
        cycles = case["cycles"]
        pc = case["initial"]["pc"]
        if not cycles or cycles[0][:2] != [pc, op] or [pc, op] not in case["initial"]["ram"]:
            sys.exit(f"harte: {cpu}/{op:02x}: case {len(rows)} does not run opcode {op:02x}; not the pinned file?")
        row = state_fields(case["initial"]) + state_fields(case["final"]) + [len(cycles)]
        for addr, val, kind in cycles:
            row += [addr, val, 1 if kind == "write" else 0]
        rows.append(" ".join(str(v) for v in row))
    return op, rows


def sweep(harte):
    """What a killed run left: an old set moved out with no new one in goes back; scratch sets go."""
    for cpu in HARTE_SETS:
        dest_dir, old_dir = harte / cpu, harte / f".{cpu}.old"
        if old_dir.is_dir() and not dest_dir.exists():
            old_dir.rename(dest_dir)
        for debris in harte.glob(f".{cpu}.*"):
            shutil.rmtree(debris, ignore_errors=True)


def swap_in(harte, fetched):
    """Every old set out, then every new one in; any failure (Ctrl-C too) puts the old ones back."""
    moved_out, moved_in = [], []
    try:
        for cpu in fetched:
            dest_dir, old_dir = harte / cpu, harte / f".{cpu}.old"
            shutil.rmtree(old_dir, ignore_errors=True)
            if dest_dir.exists():
                dest_dir.rename(old_dir)
                moved_out.append(cpu)
        for cpu, new_dir in fetched.items():
            new_dir.rename(harte / cpu)
            moved_in.append(cpu)
    except BaseException:
        for cpu in moved_in:
            (harte / cpu).rename(fetched[cpu])  # back to scratch, for the caller to remove
        for cpu in moved_out:
            (harte / f".{cpu}.old").rename(harte / cpu)
        raise
    for cpu in fetched:
        shutil.rmtree(harte / f".{cpu}.old", ignore_errors=True)


def fetch_harte(out_dir, cases):
    harte = out_dir / "harte"
    harte.mkdir(parents=True, exist_ok=True)
    sweep(harte)
    fetched = {}  # cpu -> its new set, in a scratch directory beside the old
    try:
        short = []
        for cpu, opcodes in HARTE_SETS.items():
            new_dir = fetched[cpu] = pathlib.Path(tempfile.mkdtemp(prefix=f".{cpu}.", dir=harte))
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                for op, rows in pool.map(lambda o: harte_cases(cpu, o, cases), opcodes):
                    if len(rows) < cases:
                        short.append(f"{cpu}/{op:02x}:{len(rows)}")
                    (new_dir / f"{op:02x}.txt").write_text("\n".join(rows) + "\n")
            (new_dir / "SOURCE").write_text(
                f"format {HARTE_FORMAT}\nSingleStepTests/65x02 {HARTE_COMMIT} {cpu}/v1, {cases} cases/opcode\n")
        if short:
            sys.exit(f"harte: fewer cases than asked for {', '.join(short)}; raise BYTES_PER_CASE")
        # Every set whole, or none: a smaller one must not leave stale opcodes behind.
        swap_in(harte, fetched)
        for cpu in fetched:
            print(f"harte: {cpu}: {len(HARTE_SETS[cpu])} opcodes x {cases} cases -> {harte / cpu}")
    finally:
        for new_dir in fetched.values():
            shutil.rmtree(new_dir, ignore_errors=True)  # gone already if it was swapped in


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cases", type=positive_cases, default=100, help="Harte cases per opcode (default 100)")
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

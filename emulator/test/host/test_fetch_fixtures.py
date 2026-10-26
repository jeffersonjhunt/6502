#!/usr/bin/env python3
"""tools/fetch_fixtures.py's Harte handling, against a fake download.

  test_fetch_fixtures.py <fetch_fixtures.py>

Nothing is fetched: the tool's fetch() is replaced, and every run writes
under a temporary directory, never test/host/fixtures. What it checks: a
download that fails, or brings back cases that are not the opcode asked
for, leaves the set already there as it was; a good one replaces it whole,
stale opcodes and all; one interrupted mid-swap puts the old sets back, and
what a killed run left is swept up by the next; --cases outside what the
suite has is refused.
"""

import importlib.util
import json
import os
import pathlib
import re
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ in the source tree


def load(tool):
    spec = importlib.util.spec_from_file_location("fetch_fixtures", tool)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def fake_cases(op, n=20, wrong=False):
    cases = []
    for i in range(n):
        pc = 0x1000 + i
        state = {"pc": pc, "s": 0xFD, "a": i, "x": 0, "y": 0, "p": 0x24, "ram": [[pc, op]]}
        read = (op + 1) & 0xFF if wrong else op
        cases.append({"name": f"{op:02x} {i}", "initial": state, "final": dict(state, pc=pc + 1),
                      "cycles": [[pc, read, "read"]]})
    return json.dumps(cases, indent=1).encode()


def fake_fetch(fail=None, wrong=None):
    """fetch(url, byte_range) for the Harte URLs; `fail`/`wrong`: (cpu, op) to break."""
    def fetch(url, byte_range=None):
        m = re.search(r"/([0-9a-z]+)/v1/([0-9a-f]{2})\.json$", url)
        cpu, op = m.group(1), int(m.group(2), 16)
        if fail == (cpu, op):
            raise OSError("connection reset")
        data = fake_cases(op, wrong=wrong == (cpu, op))
        return data[:byte_range] if byte_range else data
    return fetch


def snapshot(root):
    return {str(p.relative_to(root)): p.read_bytes() for p in sorted(root.rglob("*")) if p.is_file()}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    tool = load(sys.argv[1])
    failures = []

    def check(name, cond, detail=""):
        if not cond:
            failures.append(f"{name}{': ' + detail if detail else ''}")

    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "fixtures"
        out.mkdir()

        # A good fetch writes every opcode, and SOURCE; a stale file goes.
        stale = out / "harte" / "6502" / "zz.txt"
        stale.parent.mkdir(parents=True)
        stale.write_text("stale\n")
        tool.fetch = fake_fetch()
        tool.fetch_harte(out, 5)
        nmos = out / "harte" / "6502"
        check("good fetch: opcodes", len(list(nmos.glob("*.txt"))) == 244, str(len(list(nmos.glob("*.txt")))))
        check("good fetch: stale file removed", not stale.exists())
        check("good fetch: SOURCE", "5 cases/opcode" in (nmos / "SOURCE").read_text())
        rows = (nmos / "a9.txt").read_text().splitlines()
        check("good fetch: 5 cases", len(rows) == 5, str(len(rows)))
        before = snapshot(out)

        # A download that dies part way leaves the old set, and no debris.
        tool.fetch = fake_fetch(fail=("wdc65c02", 0x80))
        try:
            tool.fetch_harte(out, 7)
            check("failed fetch: no error", False)
        except (OSError, SystemExit):
            pass
        check("failed fetch: old set kept", snapshot(out) == before,
              f"{len(snapshot(out))} files, had {len(before)}")
        check("failed fetch: nothing left over", sorted(os.listdir(out / "harte")) == ["6502", "wdc65c02"],
              str(os.listdir(out / "harte")))

        # Cases that are not the opcode asked for are refused the same way.
        tool.fetch = fake_fetch(wrong=("6502", 0x10))
        try:
            tool.fetch_harte(out, 7)
            check("wrong opcode: accepted", False)
        except SystemExit as e:
            check("wrong opcode: says which", "6502/10" in str(e.code), str(e.code))
        check("wrong opcode: old set kept", snapshot(out) == before)

        # Interrupted while swapping in (Ctrl-C between the two CPUs' renames):
        # both old sets go back, not one new and one old, or one missing.
        real_rename = pathlib.Path.rename
        interrupts = [1]

        def rename(self, target):
            if pathlib.Path(target) == out / "harte" / "wdc65c02" and interrupts:
                interrupts.pop()  # once: the new set's way in, not the old one's way back
                raise KeyboardInterrupt
            return real_rename(self, target)

        tool.fetch = fake_fetch()
        pathlib.Path.rename = rename
        try:
            tool.fetch_harte(out, 7)
            check("interrupted swap: no error", False)
        except KeyboardInterrupt:
            pass
        finally:
            pathlib.Path.rename = real_rename
        check("interrupted swap: old sets kept", snapshot(out) == before,
              f"{sorted(set(snapshot(out)) ^ set(before))[:4]}")
        check("interrupted swap: nothing left over", sorted(os.listdir(out / "harte")) == ["6502", "wdc65c02"],
              str(os.listdir(out / "harte")))

        # What a killed run leaves (a scratch set; an old set moved out and the
        # new not yet in) is swept up at the start of the next: the old set back.
        (out / "harte" / ".6502.k1ll3d").mkdir()
        (out / "harte" / ".6502.k1ll3d" / "a9.txt").write_text("half\n")
        (out / "harte" / "wdc65c02").rename(out / "harte" / ".wdc65c02.old")
        tool.fetch = fake_fetch(fail=("6502", 0x00))
        try:
            tool.fetch_harte(out, 7)
        except (OSError, SystemExit):
            pass
        check("swept: old set back, debris gone", snapshot(out) == before
              and sorted(os.listdir(out / "harte")) == ["6502", "wdc65c02"], str(os.listdir(out / "harte")))

        # --cases: at least one, at most the 10,000 each file holds.
        tool.fetch = fake_fetch()
        for cases in ("0", "-3", "10001", "many"):
            sys.argv = ["fetch_fixtures.py", "--cases", cases, "--out", str(out), "--skip-dormann"]
            try:
                tool.main()
                check(f"--cases {cases}: accepted", False)
            except SystemExit as e:
                check(f"--cases {cases}: usage error", e.code == 2, str(e.code))
        check("--cases: old set kept", snapshot(out) == before)

    for f in failures:
        print(f"FAIL {f}")
    print(f"{'NOT OK' if failures else 'ok'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

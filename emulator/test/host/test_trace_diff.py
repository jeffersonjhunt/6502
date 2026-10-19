#!/usr/bin/env python3
"""tools/trace_diff.py on small hand-written traces.

  test_trace_diff.py <trace_diff.py>

The rules in docs/BUS_TRACE.md that a real board capture will lean on:
cycle numbers compared relative to each trace's start, gaps compared,
`?` matching anything (a whole field, or one of the two signal flags),
--from lining up a capture that began mid-program, and exit status 2 (not
0 or 1) for a malformed trace.
"""

import os
import subprocess
import sys
import tempfile

HEAD = "# ooe-bus-trace 1\n"
EMU = HEAD + """# cpu w65c02s
1 00000 00 R -- 0
2 0FFFC 00 R -V 4
3 0FFFD E0 R -V 4
4 0E000 A9 R S- 4
5 0E001 41 R -- 4
6 0E002 CB R S- 4
7 0E003 EA R -- 4
20 0E003 EA R S- 4
"""


def main():
    tool = sys.argv[1]
    failures = []
    with tempfile.TemporaryDirectory() as d:
        def trace(name, body):
            path = os.path.join(d, name)
            with open(path, "w") as f:
                f.write(body)
            return path

        def run(*args):
            r = subprocess.run([sys.executable, "-I", tool, *args], capture_output=True, text=True)
            return r.returncode, r.stdout + r.stderr

        emu = trace("emu.trace", EMU)
        cases = [
            # (name, other trace body, extra args, want rc, want text)
            ("identical", EMU, [], 0, "traces agree: 8 cycles"),
            ("cycles offset", EMU.replace("\n1 ", "\n101 ").replace("\n2 ", "\n102 ").replace("\n3 ", "\n103 ")
             .replace("\n4 ", "\n104 ").replace("\n5 ", "\n105 ").replace("\n6 ", "\n106 ").replace("\n7 ", "\n107 ")
             .replace("\n20 ", "\n120 "), [], 0, "traces agree"),
            ("gap length differs", EMU.replace("\n20 ", "\n21 "), [], 1, "cycle +19 vs +20"),
            ("data differs", EMU.replace("5 0E001 41", "5 0E001 42"), [], 1, "data 41 vs 42"),
            ("rw differs", EMU.replace("5 0E001 41 R", "5 0E001 41 W"), [], 1, "R/W R vs W"),
            ("sync differs", EMU.replace("4 0E000 A9 R S-", "4 0E000 A9 R --"), [], 1, "signals S- vs --"),
            ("module differs", EMU.replace("1 00000 00 R -- 0", "1 00000 00 R -- -"), [], 1, "module 0 vs -"),
            ("? matches", EMU.replace("5 0E001 41 R -- 4", "5 ? ? ? ? ?"), [], 0, "traces agree"),
            ("? per signal", EMU.replace("2 0FFFC 00 R -V", "2 0FFFC 00 R ?V"), [], 0, "traces agree"),
            ("shorter", "".join(EMU.splitlines(True)[:6]), [], 0, "has 4 more"),
            ("--from", HEAD + "50 0E000 A9 R S- 4\n51 0E001 41 R -- 4\n52 0E002 CB R S- 4\n", ["--from", "E000"], 0,
             "traces agree for 3 cycles"),
            ("--from never reached", HEAD + "1 0E001 41 R -- 4\n", ["--from", "E000"], 2, "no opcode fetch at 0E000"),
            ("bad header", "1 00000 00 R -- 0\n", [], 2, "expected '# ooe-bus-trace 1'"),
            ("bad line", HEAD + "1 0000 00 R -- 0\n", [], 2, ":2: not a trace line"),
            ("bad signals", HEAD + "1 00000 00 R XX 0\n", [], 2, "not a trace line"),
            ("empty", HEAD, [], 2, "has no cycles"),
        ]
        for name, body, extra, want_rc, want_text in cases:
            other = trace("other.trace", body)
            rc, out = run(*extra, emu, other)
            if rc != want_rc or want_text not in out:
                failures.append(f"{name}: rc {rc} (want {want_rc}), output:\n{out}")

        rc, out = run(emu, os.path.join(d, "missing.trace"))
        if rc != 2:
            failures.append(f"missing file: rc {rc}, want 2\n{out}")

    for f in failures:
        print(f"FAIL {f}")
    print(f"{'NOT OK' if failures else 'ok'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

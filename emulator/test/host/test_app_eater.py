#!/usr/bin/env python3
"""The Eater machine in the real app, in a pseudo-terminal.

  test_app_eater.py <emulator binary> <machines dir> <eater rom dir>

What only the app can show: the host terminal driving Woz Monitor over the
6551 (keys in as bytes, CR out as a new line), and keyboard.s typing through
the PS/2 keyboard onto the LCD, which the terminal draws as a box. A machine
with nothing to talk through is refused before the terminal goes raw.

The pty helpers are test_app_tty.py's, loaded by path (python -I does not
put this directory on sys.path).
"""

import importlib.util
import os
import sys
import tempfile
import termios

SKIP = 77
LFLAG = 3

sys.dont_write_bytecode = True  # no __pycache__ in the source tree
spec = importlib.util.spec_from_file_location("tty", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                  "test_app_tty.py"))
tty = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tty)


def session(binary, args, cwd, steps, settle=0.0):
    """Run, then for each (bytes to send, bytes to wait for): send and wait. Ctrl-C at the end.

    `settle` seconds pass after the first step's output before anything is typed."""
    proc, master, slave = tty.start(binary, args, cwd)
    before = termios.tcgetattr(slave)
    seen = []
    for send, want in steps:
        got = b""
        for b in send:  # one at a time, as typed
            os.write(master, bytes([b]))
            got += tty.read_until(master, b"\0", 0.05)
        if want not in got:
            got += tty.read_until(master, want, 10)
        if not seen and settle:
            got += tty.read_until(master, b"\0", settle)
        seen.append(got)
    os.write(master, b"\x03")
    rc = tty.finish(proc)
    after = termios.tcgetattr(slave)
    os.close(master)
    os.close(slave)
    lost = [f for f in ("ICANON", "ECHO")
            if (before[LFLAG] & getattr(termios, f)) and not (after[LFLAG] & getattr(termios, f))]
    return rc, lost, seen


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    binary, machines, roms = sys.argv[1:]
    wozmon, keyboard = os.path.join(roms, "wozmon.rom"), os.path.join(roms, "keyboard.rom")
    if not os.path.exists(wozmon):
        print(f"SKIP: {wozmon} not built (needs vasm)")
        return SKIP
    eater = os.path.join(machines, "eater.machine")

    failures = []
    with tempfile.TemporaryDirectory() as cwd:
        rc, lost, seen = session(binary, ["--machine", eater, wozmon], cwd,
                                 [(b"", b"\\"), (b"FF00.FF03\r", b"FF00: A9 1F 8D 03")])
        if b"\\" not in seen[0]:
            failures.append(f"wozmon: no prompt (got {seen[0][-200:]!r})")
        if b"FF00: A9 1F 8D 03" not in seen[1]:
            failures.append(f"wozmon: no examine output (got {seen[1][-300:]!r})")
        if b"\n" not in seen[1].split(b"FF00:")[0]:  # (the pty itself turns \n into \r\n)
            failures.append(f"wozmon: echoed CR not shown as a new line (got {seen[1][-300:]!r})")
        if rc != 0 or lost:
            failures.append(f"wozmon: rc={rc} lost={lost}")

        # keyboard.s zeroes its key buffer pointers after setting up the LCD,
        # so a key in its first ~2 ms is lost (on the board too): wait first.
        rc, lost, seen = session(binary, ["--machine", eater, keyboard], cwd,
                                 [(b"", b"+----------------+"), (b"hi", b"|hi    ")], settle=0.5)
        if b"|hi              |" not in seen[1]:
            failures.append(f"keyboard: LCD box does not show 'hi' (got {seen[1][-400:]!r})")
        if rc != 0 or lost:
            failures.append(f"keyboard: rc={rc} lost={lost}")

        bare = os.path.join(cwd, "bare.machine")
        with open(bare, "w") as f:
            f.write("ram 0000-7FFF\n")
        proc, master, slave = tty.start(binary, ["--machine", bare], cwd)
        rc = tty.finish(proc, timeout=5)
        out = tty.read_until(master, b"\0", 0.5).decode(errors="replace")
        os.close(master)
        os.close(slave)
        if rc in (None, 0) or "no 'terminal'" not in out:
            failures.append(f"bare machine: rc={rc}, got {out[-300:]!r}")

    for f in failures:
        print(f"FAIL {f}")
    print(f"{'NOT OK' if failures else 'ok'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

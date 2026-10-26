#!/usr/bin/env python3
"""The Phase 5 bring-up tools in the real app, in a pseudo-terminal.

  test_app_debug.py <emulator binary> <ibasic.rom> <ibasic.rom.lbl> <trace_diff.py> <machines dir>

What only the app can show: Ctrl-] stops a running machine into the
debugger and `r` resumes it; --halt, --break and --symbols work from the
command line; Ctrl-C at the debugger's prompt (a blocked read, not the raw
keyboard) still quits and restores the terminal; --load puts HEX and raw
images on the bus, ROM included; STP stops into the debugger; so does a
Console card's HALT set by the program, and `r` releases it; --trace
writes a well-formed trace that tools/trace_diff.py agrees with, and
disagrees with once a byte is changed; --screen saves the video card's
frame as a PPM, and fails on a machine without one; leaving Apple II mode
by a hard reset paces at --clock again. Bad --load and --break arguments,
an option missing its value or given twice, stop the app before the
terminal goes raw.

The pty helpers are test_app_tty.py's, loaded by path (python -I does not
put this directory on sys.path).
"""

import importlib.util
import os
import re
import subprocess
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

PROMPT = b"debug> "


class Session:
    """One emulator run; send() returns everything printed until `until` (or the timeout)."""

    def __init__(self, binary, args, cwd):
        self.proc, self.master, self.slave = tty.start(binary, args, cwd)
        self.before = termios.tcgetattr(self.slave)
        self.log = b""

    def expect(self, needle, timeout=10):
        out = tty.read_until(self.master, needle, timeout)
        self.log += out
        return out

    def send(self, data, until=PROMPT, timeout=10):
        os.write(self.master, data)
        return self.expect(until, timeout)

    def close(self):
        rc = tty.finish(self.proc)
        self.log += tty.read_until(self.master, b"\0", 0.3)
        after = termios.tcgetattr(self.slave)
        os.close(self.master)
        os.close(self.slave)
        lost = [f for f in ("ICANON", "ECHO")
                if (self.before[LFLAG] & getattr(termios, f)) and not (after[LFLAG] & getattr(termios, f))]
        return rc, lost


def check(failures, name, cond, detail=""):
    if not cond:
        failures.append(f"{name}{': ' + detail if detail else ''}")


def text(b):
    return b.decode(errors="replace")[-600:]


def write_hex(path, records):
    """records: [(addr, bytes)] -> an Intel HEX file with an EOF record."""
    with open(path, "w") as f:
        for addr, data in records:
            rec = bytes([len(data), addr >> 8, addr & 0xFF, 0]) + bytes(data)
            f.write(":" + rec.hex().upper() + f"{(-sum(rec)) & 0xFF:02X}\n")
        f.write(":00000001FF\n")


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    binary, rom, lbl, trace_diff, machines = sys.argv[1:]
    if not os.path.exists(rom) or not os.path.exists(lbl):
        print(f"SKIP: {rom} or {lbl} not built")
        return SKIP

    failures = []
    with tempfile.TemporaryDirectory() as cwd:
        # Ctrl-] stops a running Woz Monitor; fetch looks at the ROM; r resumes it.
        s = Session(binary, [rom], cwd)
        check(failures, "ctrl-]: no Woz prompt", b"\\" in s.expect(b"\\"))
        out = s.send(b"\x1d")
        check(failures, "ctrl-]: not halted", b"halted (Ctrl-])" in out and PROMPT in out, text(out))
        out = s.send(b"f FF00 4\n")
        check(failures, "fetch: Woz's first bytes", b"FF00  D8 58 A0 7F" in out, text(out))
        out = s.send(b"r\n", until=b"running")
        check(failures, "run: not resumed", b"running" in out, text(out))
        out = s.send(b"A", until=b"A")  # Woz echoes the key: the machine is running again
        check(failures, "run: machine not running after r", b"A" in out, text(out))
        os.write(s.master, b"\x03")
        rc, lost = s.close()
        check(failures, "ctrl-] session: exit", rc == 0 and not lost, f"rc={rc} lost={lost}")

        # --halt + --symbols + --break by label; labels in the listing; q quits.
        s = Session(binary, ["--symbols", lbl, "--break", "ECHO", "--halt", rom], cwd)
        out = s.expect(PROMPT)
        check(failures, "--halt: not at the prompt at reset",
              b"halted (--halt)" in out and b"FF00  D8        RESET: cld" in out, text(out))
        out = s.send(b"r\n")
        check(failures, "--break ECHO: not hit", b"halted (break)" in out and b"PC=FFEF" in out
              and b"ECHO: bit $D012" in out, text(out))
        out = s.send(b"l GETLINE 2\n")
        check(failures, "list by label", b"FF1F" in out and b"GETLINE:" in out, text(out))
        os.write(s.master, b"q\n")
        rc, lost = s.close()
        check(failures, "q: exit", rc == 0 and not lost, f"rc={rc} lost={lost}")

        # Ctrl-C while blocked at the prompt (cooked mode: the tty sends SIGINT).
        s = Session(binary, ["--halt", rom], cwd)
        s.expect(PROMPT)
        os.write(s.master, b"\x03")
        rc, lost = s.close()
        check(failures, "ctrl-c at the prompt", rc == 0 and not lost, f"rc={rc} lost={lost} {text(s.log)}")

        # --load: HEX into RAM and over ROM, raw at an address; STP stops into
        # the debugger on the W65C02S machine.
        hexfile = os.path.join(cwd, "prog.hex")
        write_hex(hexfile, [(0x0300, [0xA9, 0x42, 0xDB]),  # LDA #$42; STP
                            (0xFFF0, [0x5A])])             # over ROM: --load is not a CPU store
        raw = os.path.join(cwd, "data.bin")
        with open(raw, "wb") as f:
            f.write(bytes([0x11, 0x22, 0x33]))
        machine = os.path.join(machines, "apple1-65c02.machine")
        s = Session(binary, ["--machine", machine, "--load", hexfile, "--load", raw + "@0280", "--halt", rom], cwd)
        s.expect(PROMPT)
        out = s.send(b"f 0280 3\n")
        check(failures, "--load raw", b"0280  11 22 33" in out, text(out))
        out = s.send(b"f FFF0 1\n")
        check(failures, "--load hex over ROM", b"FFF0  5A" in out, text(out))
        out = s.send(b"x pc=0300\n")
        out = s.send(b"r\n")
        check(failures, "STP: did not halt", b"halted (STP)" in out and b"A=42" in out
              and b"(stopped: STP)" in out, text(out))
        os.write(s.master, b"q\n")
        rc, lost = s.close()
        check(failures, "stp session: exit", rc == 0 and not lost, f"rc={rc} lost={lost}")

        # A Console card's HALT, set by the program, stops into the debugger
        # as "panel"; r releases the card, so the program runs on.
        panel = os.path.join(cwd, "panel.machine")
        with open(machine) as f, open(panel, "w") as out_f:
            out_f.write(f.read() + "\nconsole  C700-C77F  name=console\n")
        write_hex(hexfile, [(0x0300, [0xA9, 0x01, 0x8D, 0x06, 0xC7,  # LDA #$01; STA CTRL (HALT)
                                      0x4C, 0x05, 0x03])])         # JMP *
        s = Session(binary, ["--machine", panel, "--load", hexfile, "--halt", rom], cwd)
        s.expect(PROMPT)
        s.send(b"x pc=0300\n")
        out = s.send(b"r\n")
        check(failures, "panel: CTRL HALT did not stop", b"halted (panel)" in out and b"PC=0305" in out, text(out))
        out = s.send(b"r\n", until=b"running")
        out += s.send(b"\x1d")
        check(failures, "panel: r did not release the card",
              b"halted (Ctrl-])" in out and b"halted (panel)" not in out, text(out))
        os.write(s.master, b"q\n")
        rc, lost = s.close()
        check(failures, "panel session: exit", rc == 0 and not lost, f"rc={rc} lost={lost}")

        # --screen: a program sets a border colour on a backplane machine's video
        # card; the PPM written on the way out is that colour.
        video = os.path.join(cwd, "video.machine")
        with open(video, "w") as f:
            f.write("cpu w65c02s\nnorthbridge backplane bootsel\nram 00000-FFFFF module=0\n"
                    "rom 00000-7FFFF space=flash module=4\nacia6551 300-37F space=io name=terminal\n"
                    "video 480-4FF space=io name=video irq\n")
        write_hex(hexfile, [(0x0300, [0xA9, 0x20, 0x8D, 0x81, 0xC6,   # LDA #$20; STA WIN1: $4000 = $80000
                                      0xA9, 0x23, 0x8D, 0x06, 0x40,   # palette entry 3 = $123
                                      0xA9, 0x01, 0x8D, 0x07, 0x40,
                                      0xA9, 0x03, 0x8D, 0x81, 0xC4,   # BORDER 3
                                      0xA9, 0x01, 0x8D, 0x80, 0xC4,   # CTRL: display on
                                      0x4C, 0x19, 0x03])])           # JMP *
        screen = os.path.join(cwd, "screen.ppm")
        s = Session(binary, ["--machine", video, "--load", hexfile, "--screen", screen, "--halt", rom], cwd)
        s.expect(PROMPT)
        s.send(b"x pc=0300\n")
        s.send(b"r\n", until=b"running")
        s.expect(b"\0", timeout=0.5)  # a few frames at 1 MHz
        s.send(b"\x1d")
        os.write(s.master, b"q\n")
        rc, lost = s.close()
        check(failures, "screen session: exit", rc == 0 and not lost, f"rc={rc} lost={lost} {text(s.log)}")
        ppm = open(screen, "rb").read() if os.path.exists(screen) else b""
        head = b"P6\n640 480\n255\n"
        check(failures, "screen: a 640x480 PPM", ppm.startswith(head) and len(ppm) == len(head) + 640 * 480 * 3,
              f"{len(ppm)} bytes")
        check(failures, "screen: the border colour", ppm[len(head):len(head) + 3] == bytes([0x11, 0x22, 0x33])
              and ppm[-3:] == bytes([0x11, 0x22, 0x33]), ppm[len(head):len(head) + 3].hex())
        s = Session(binary, ["--screen", screen, "--halt", rom], cwd)  # the Apple-1: no video card
        s.expect(PROMPT)
        os.write(s.master, b"q\n")
        rc, lost = s.close()
        check(failures, "screen without a video card", rc == 1 and b"has no video card" in s.log and not lost,
              f"rc={rc} {text(s.log)}")

        # Apple II mode paces at its own clock; a hard reset leaves it, and
        # pacing (and the cards) go back to --clock, not stay at 1.02 MHz.
        apple = os.path.join(cwd, "apple.machine")
        with open(apple, "w") as f:
            f.write("cpu w65c02s\nnorthbridge backplane\nram 00000-FFFFF module=0\n"
                    "rom 00000-7FFFF space=flash module=4\nacia6551 300-37F space=io name=terminal\n")
        write_hex(hexfile, [(0x0300, [0xA9, 0x01, 0x8D, 0x85, 0xC6,   # LDA #$01; STA MODE: Apple II mode
                                      0x4C, 0x05, 0x03])])           # JMP *
        s = Session(binary, ["--machine", apple, "--clock", "10000", "--load", hexfile, "--halt", rom], cwd)
        s.expect(PROMPT)
        s.send(b"x pc=0300\n")
        s.send(b"r\n", until=b"running")
        s.expect(b"\0", timeout=0.3)  # in Apple II mode, at 1.02 MHz
        s.send(b"\x1d")
        s.send(b"reset\n")
        out = s.send(b"x pc=0305\n")

        def cycles(text):
            found = re.findall(rb"cycles=(\d+)", text)
            return int(found[-1]) if found else None

        before = cycles(out)
        s.send(b"r\n", until=b"running")
        s.expect(b"\0", timeout=1.0)
        after = cycles(s.send(b"\x1d"))
        os.write(s.master, b"q\n")
        rc, lost = s.close()
        ran = after - before if before is not None and after is not None else None
        check(failures, "apple mode left: paced at --clock again", ran is not None and ran < 100_000,
              f"{ran} cycles in about a second at --clock 10000 {text(s.log)}")
        check(failures, "apple session: exit", rc == 0 and not lost, f"rc={rc} lost={lost}")

        # --trace: header, reset sequence, one line per cycle; trace_diff agrees
        # with itself and finds a changed byte.
        trace = os.path.join(cwd, "run.trace")
        s = Session(binary, ["--trace", trace, "--halt", rom], cwd)
        s.expect(PROMPT)
        out = s.send(b"s 3\n")  # CLD CLI LDY #: 6 cycles after reset's 7
        out = s.send(b"t\n")
        check(failures, "trace command", b"trace on, 13 cycles written" in out, text(out))
        os.write(s.master, b"q\n")
        rc, lost = s.close()
        check(failures, "trace session: exit", rc == 0 and not lost, f"rc={rc} lost={lost}")
        lines = open(trace).read().splitlines() if os.path.exists(trace) else []
        want_head = ["# ooe-bus-trace 1", "# cpu nmos6502", "1 00000 00 R -- -"]
        check(failures, "trace header", lines[:3] == want_head, f"{lines[:3]}")
        check(failures, "trace length", len(lines) == 2 + 13, f"{len(lines)} lines")
        check(failures, "trace: reset vector pull", "6 0FFFC 00 R -V -" in lines and "7 0FFFD FF R -V -" in lines,
              "\n".join(lines[:10]))
        check(failures, "trace: first fetch", "8 0FF00 D8 R S- -" in lines, "\n".join(lines[:10]))

        def diff(*args):
            r = subprocess.run([sys.executable, "-I", trace_diff, *args], capture_output=True, text=True)
            return r.returncode, r.stdout + r.stderr

        rc, out = diff(trace, trace)
        check(failures, "trace_diff: self", rc == 0 and "traces agree: 13 cycles" in out, out)
        changed = os.path.join(cwd, "changed.trace")
        with open(changed, "w") as f:
            f.write("\n".join(l.replace("8 0FF00 D8", "8 0FF00 EA") for l in lines) + "\n")
        rc, out = diff("--symbols", lbl, trace, changed)
        check(failures, "trace_diff: changed byte", rc == 1 and "data D8 vs EA" in out and "RESET" in out, out)

        # Bad arguments stop the app before the terminal is touched.
        for args, needle in ((["--load", raw, rom], "needs a hex load address"),
                             (["--load", os.path.join(cwd, "none.hex"), rom], "none.hex: cannot open"),
                             (["--load", hexfile + "@0300", rom], "drop the @ADDR"),
                             (["--symbols", raw, rom], "data.bin:1:"),
                             (["--break", "NOPE", rom], "--break NOPE"),
                             (["--load", raw + "@", rom], "needs a hex load address"),
                             (["--load", raw + "@0x300", rom], "needs a hex load address"),
                             (["--load", "--halt", rom], "--load needs a value"),
                             (["--clock", "1000", "--clock", "2000", rom], "--clock given twice"),
                             (["--machine", machine, "--machine", machine, rom], "--machine given twice"),
                             ([rom, rom], "rom given twice")):
            s = Session(binary, args, cwd)
            rc, lost = s.close()
            check(failures, f"{' '.join(args[:2])}: refused", rc not in (None, 0) and needle in text(s.log),
                  f"rc={rc} {text(s.log)}")

    for f in failures:
        print(f"FAIL {f}")
    print(f"{'NOT OK' if failures else 'ok'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

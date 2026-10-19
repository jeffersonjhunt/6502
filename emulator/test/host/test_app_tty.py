#!/usr/bin/env python3
"""The emulator app in a real pseudo-terminal: start-up, Ctrl-C, and exit.

  test_app_tty.py <emulator binary> <rom>

Covers the app-level bugs in EMULATOR_PLAN.md section 1, which no in-process
test can see:

- Ctrl-C killed the process (raw mode leaves ISIG on, so the kernel sends
  SIGINT before the app ever reads the 0x03 byte), skipping the termios
  restore and leaving the shell without echo or line editing.
- Objects were leaked at exit. The test build has ASan, so LeakSanitizer
  turns a leak into a non-zero exit status.
- A missing ROM was only logged; the app then ran 64 KB of zeros forever.

A broken --machine file stops the app with its file and line (Phase 3).

And the Phase 2 logging contract: nothing is written unless --log is given
(the terminal is the Apple-1's screen), and --log FILE gets the run.

Each check has a timeout: "it hung" is the failure mode for two of these.
Exit code 77 (CTest skip) only when there is no tty support at all.
"""

import fcntl
import os
import select
import signal
import subprocess
import sys
import tempfile
import termios
import time

SKIP = 77
LFLAG = 3  # index of c_lflag in termios.tcgetattr()'s list


def read_until(fd, needle, timeout):
    buf = b""
    deadline = time.monotonic() + timeout
    while needle not in buf and time.monotonic() < deadline:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                buf += os.read(fd, 4096)
            except OSError:
                break
    return buf


def start(binary, args, cwd):
    master, slave = os.openpty()
    proc = subprocess.Popen([binary, *args], stdin=slave, stdout=slave, stderr=slave, cwd=cwd,
                            start_new_session=True,
                            preexec_fn=lambda: fcntl.ioctl(0, termios.TIOCSCTTY, 0))
    return proc, master, slave


def finish(proc, timeout=10):
    try:
        return proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        return None


def check_quit(name, binary, args, cwd, quit_action):
    """Start, wait for the Woz prompt, quit with quit_action, check what is left."""
    failures = []
    proc, master, slave = start(binary, args, cwd)
    before = termios.tcgetattr(slave)
    banner = read_until(master, b"\\", 10)
    if b"\\" not in banner:
        failures.append(f"{name}: no Woz Monitor prompt within 10 s (got {banner[-200:]!r})")
    quit_action(proc, master)
    rc = finish(proc)
    tail = read_until(master, b"\0", 0.5)  # drain whatever was printed on the way out
    after = termios.tcgetattr(slave)
    os.close(master)
    os.close(slave)

    if rc is None:
        failures.append(f"{name}: still running 10 s after quitting")
    elif rc != 0:
        failures.append(f"{name}: exit status {rc}, want 0\n{tail.decode(errors='replace')[-2000:]}")
    for flag in ("ICANON", "ECHO"):
        bit = getattr(termios, flag)
        if (before[LFLAG] & bit) and not (after[LFLAG] & bit):
            failures.append(f"{name}: terminal left with {flag} off")
    return failures


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    binary, rom = sys.argv[1:]
    if not os.path.exists(rom):
        print(f"SKIP: {rom} not built")
        return SKIP

    failures = []
    with tempfile.TemporaryDirectory() as cwd:
        failures += check_quit("ctrl-c", binary, [rom], cwd, lambda p, m: os.write(m, b"\x03"))
        failures += check_quit("sigterm", binary, [rom], cwd, lambda p, m: p.send_signal(signal.SIGTERM))
        if os.listdir(cwd):
            failures.append(f"no --log: files written anyway: {os.listdir(cwd)}")

        log = os.path.join(cwd, "run.log")
        failures += check_quit("--log", binary, ["--log", log, rom], cwd, lambda p, m: os.write(m, b"\x03"))
        text = open(log).read() if os.path.exists(log) else ""
        for needle in ("loaded", "exiting"):
            if needle not in text:
                failures.append(f"--log: {needle!r} not in log (got {text[-300:]!r})")

        # A ROM that is not there must stop the app, not run zeros.
        proc, master, slave = start(binary, [os.path.join(cwd, "missing.rom")], cwd)
        rc = finish(proc, timeout=5)
        out = read_until(master, b"\0", 0.5).decode(errors="replace")
        os.close(master)
        os.close(slave)
        if rc is None:
            failures.append("missing rom: still running after 5 s")
        elif rc == 0:
            failures.append("missing rom: exit status 0, want non-zero")
        elif "missing.rom" not in out:
            failures.append(f"missing rom: no message naming the file (got {out[-300:]!r})")

        # A bad machine description must name the file and line, and stop.
        bad = os.path.join(cwd, "bad.machine")
        with open(bad, "w") as f:
            f.write("ram 0000-7FFF\nflux 8000-FFFF\n")
        proc, master, slave = start(binary, ["--machine", bad], cwd)
        rc = finish(proc, timeout=5)
        out = read_until(master, b"\0", 0.5).decode(errors="replace")
        os.close(master)
        os.close(slave)
        if rc is None or rc == 0:
            failures.append(f"bad machine: exit status {rc}, want non-zero")
        elif "bad.machine:2:" not in out:
            failures.append(f"bad machine: no 'bad.machine:2:' in message (got {out[-300:]!r})")

    for f in failures:
        print(f"FAIL {f}")
    print(f"{'NOT OK' if failures else 'ok'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

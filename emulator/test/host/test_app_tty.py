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

Every signal that ends the app puts the terminal back: SIGTERM, SIGHUP,
SIGQUIT (Ctrl-\\) and SIGPIPE as well as Ctrl-C. Ctrl-Z, under a small
job-control "shell", leaves the terminal cooked while stopped and raw again
once continued. And stdin need not be a terminal: through a pipe, a quiet
input does not stall the machine, a script can press Ctrl-] and give
debugger commands in the same stream, and more input than the key queue
holds waits for the machine rather than being dropped.

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


# A shell's job control in miniature: the emulator in its own foreground
# process group; on its stop, report the terminal's mode, put the "shell's"
# cooked mode back as bash would, then continue it in the foreground.
JOB_SHELL = r"""
import os, signal, sys, termios
binary, args = sys.argv[1], sys.argv[2:]
signal.signal(signal.SIGTTOU, signal.SIG_IGN)
cooked = termios.tcgetattr(0)
pid = os.fork()
if pid == 0:
    os.setpgid(0, 0)
    os.tcsetpgrp(0, os.getpid())
    signal.signal(signal.SIGTTOU, signal.SIG_DFL)
    os.execv(binary, [binary, *args])
os.setpgid(pid, pid)
os.tcsetpgrp(0, pid)
_, st = os.waitpid(pid, os.WUNTRACED)
if not os.WIFSTOPPED(st):
    print("SHELL: not stopped", flush=True)
    sys.exit(3)
os.tcsetpgrp(0, os.getpgrp())
lflag = termios.tcgetattr(0)[3]
print("SHELL: stopped icanon=%d echo=%d" % (bool(lflag & termios.ICANON), bool(lflag & termios.ECHO)), flush=True)
termios.tcsetattr(0, termios.TCSANOW, cooked)
os.tcsetpgrp(0, pid)
os.killpg(pid, signal.SIGCONT)
_, st = os.waitpid(pid, 0)
sys.exit(os.waitstatus_to_exitcode(st))
"""


def check_job_control(binary, rom, cwd):
    failures = []
    proc, master, slave = start(sys.executable, ["-I", "-c", JOB_SHELL, binary, rom], cwd)
    before = termios.tcgetattr(slave)
    if b"\\" not in read_until(master, b"\\", 10):
        failures.append("ctrl-z: no Woz Monitor prompt within 10 s")
    os.write(master, b"\x1a")
    out = read_until(master, b"SHELL: stopped", 10)
    out += read_until(master, b"\n", 1)
    if b"SHELL: stopped icanon=1 echo=1" not in out:
        failures.append(f"ctrl-z: terminal not restored while stopped (got {out[-200:]!r})")
    deadline = time.monotonic() + 5  # continued: raw again
    while time.monotonic() < deadline and termios.tcgetattr(slave)[LFLAG] & termios.ICANON:
        time.sleep(0.05)
    if termios.tcgetattr(slave)[LFLAG] & (termios.ICANON | termios.ECHO):
        failures.append("ctrl-z: terminal not raw again after continuing")
    os.write(master, b"A")
    if b"A" not in read_until(master, b"A", 5):
        failures.append("ctrl-z: the machine does not take keys after continuing")
    os.write(master, b"\x03")
    rc = finish(proc)
    read_until(master, b"\0", 0.3)
    after = termios.tcgetattr(slave)
    os.close(master)
    os.close(slave)
    if rc != 0:
        failures.append(f"ctrl-z: exit status {rc}, want 0")
    if (before[LFLAG] & termios.ICANON) and not (after[LFLAG] & termios.ICANON):
        failures.append("ctrl-z: terminal left with ICANON off")

    # At the debugger's prompt: the read blocked there carries on afterwards.
    proc, master, slave = start(sys.executable, ["-I", "-c", JOB_SHELL, binary, "--halt", rom], cwd)
    read_until(master, b"debug> ", 10)
    os.write(master, b"\x1a")
    out = read_until(master, b"SHELL: stopped", 10)
    if b"SHELL: stopped icanon=1 echo=1" not in out + read_until(master, b"\n", 1):
        failures.append(f"ctrl-z at the prompt: not stopped as it was (got {out[-200:]!r})")
    time.sleep(0.3)
    os.write(master, b"f FF00 4\n")
    out = read_until(master, b"FF00  D8 58 A0 7F", 5)
    if b"FF00  D8 58 A0 7F" not in out:
        failures.append(f"ctrl-z at the prompt: no prompt after continuing (got {out[-200:]!r})")
    os.write(master, b"q\n")
    rc = finish(proc)
    os.close(master)
    os.close(slave)
    if rc != 0:
        failures.append(f"ctrl-z at the prompt: exit status {rc}, want 0")
    return failures


def check_pipe(binary, rom, cwd):
    """stdin and stdout are pipes, not a terminal."""
    failures = []
    proc = subprocess.Popen([binary, rom], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            cwd=cwd)
    fd = proc.stdout.fileno()
    if b"\\" not in read_until(fd, b"\\", 10):
        failures.append("pipe: no Woz Monitor prompt with nothing to read: the machine is stalled")
    proc.stdin.write(b"\x1df FF00 4\n")
    proc.stdin.flush()
    out = read_until(fd, b"FF00  D8 58 A0 7F", 10)
    if b"FF00  D8 58 A0 7F" not in out:
        failures.append(f"pipe: Ctrl-] then a command in one write: no fetch (got {out[-300:]!r})")
    proc.stdin.write(b"r\n")
    proc.stdin.flush()

    # More input than the key queue holds: the machine takes it at its own
    # pace and none is dropped. 12 KB of Woz commands, each printing a line.
    lines = 2500
    proc.stdin.write(b"FF00\n" * lines)
    proc.stdin.flush()
    out, deadline = b"", time.monotonic() + 60
    while out.count(b"FF00: D8") < lines and time.monotonic() < deadline:
        more = read_until(fd, b"\0", 3)
        if not more:
            break  # the machine has stopped answering: the rest was lost
        out += more
    if out.count(b"FF00: D8") != lines:
        failures.append(f"pipe: {lines} commands in one write, {out.count(b'FF00: D8')} answered")
    proc.stdin.write(b"\x1dq\n")
    proc.stdin.flush()
    rc = finish(proc)
    proc.stdin.close()
    proc.stdout.close()
    if rc != 0:
        failures.append(f"pipe: exit status {rc}, want 0")
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
        failures += check_quit("sighup", binary, [rom], cwd, lambda p, m: p.send_signal(signal.SIGHUP))
        failures += check_quit("ctrl-\\", binary, [rom], cwd, lambda p, m: os.write(m, b"\x1c"))
        failures += check_quit("sigpipe", binary, [rom], cwd, lambda p, m: p.send_signal(signal.SIGPIPE))
        failures += check_job_control(binary, rom, cwd)
        failures += check_pipe(binary, rom, cwd)
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

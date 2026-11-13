"""GW-BASIC's translation ledger, checked (hw/oodos.md, "The plan, phase 7": the source and the licence).

    python3 -I ledger.py ledger.txt [--src GWSRC] [--lbl NAME=FILE.lbl ...]

ledger.txt has a line for each 8086 routine the port needs: `FILE.ASM LABEL -> TARGET`, where TARGET is a
6502 file and label (`res/x.s NAME`, `p1/y.s NAME`), `OEM res/x.s NAME`, `later 7b` (a later sub-phase's),
or `dropped: why`. It fails on:

  - a line that isn't of that form, or a routine named twice;
  - with --src (a clone of microsoft/GW-BASIC): a file that isn't there, or a label it doesn't define; and a
    routine that a translated one calls (CALL, JMP or a conditional jump to another routine) with no line of
    its own, so nothing that is called goes unaccounted for;
  - with --lbl (ld65's -Ln of the resident link, `res=...`, and of each block, `p1=...`): a 6502 label the
    link hasn't, or a 6502 file that isn't in the tree.

It prints how many routines are translated, replaced by OEM code, left for later or dropped, by 8086 file. The
source is read as text, never run.
"""
import argparse
import os
import re
import sys

LINE = re.compile(r"^(\S+\.ASM)\s+(\S+)\s+->\s+(.+?)\s*$")
TRANSLATED = re.compile(r"^(OEM\s+)?((res|p[1-8])/[\w./-]+\.s)\s+(\S+)$")
LATER = re.compile(r"^later\s+7[a-g]\b")
DROPPED = re.compile(r"^dropped:\s+\S")
# An 8086 label at the start of a line: NAME: or NAME PROC or NAME LABEL.
DEFINE = r"^\s*{0}\s*(:|\s+(PROC|LABEL)\b)"
CALLS = re.compile(r"^\s*(?:[\w$?@.]+:)?\s*(CALL|JMP|J[A-Z]{1,3})\s+(?:SHORT\s+|NEAR\s+PTR\s+|FAR\s+PTR\s+)?([\w$?@.]+)",
                   re.I)
TOPLABEL = re.compile(r"^([\w$?@.]+)\s*:")


def parse(path):
    entries, problems = {}, []
    for n, raw in enumerate(open(path, encoding="ascii"), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        m = LINE.match(line)
        if not m:
            problems.append("%s:%d: not FILE.ASM LABEL -> TARGET: %s" % (path, n, line.strip()))
            continue
        f, label, target = m.groups()
        key = (f, label)
        if key in entries:
            problems.append("%s:%d: %s %s is named twice" % (path, n, f, label))
        t = TRANSLATED.match(target)
        if t:
            kind = "oem" if t.group(1) else "translated"
        elif LATER.match(target):
            kind = "later"
        elif DROPPED.match(target):
            kind = "dropped"
        else:
            problems.append("%s:%d: a target that is no file and label, later 7x or dropped: %s" % (path, n, target))
            continue
        entries[key] = (n, kind, t.group(2) if t else None, t.group(4) if t else None)
    return entries, problems


def source(gwsrc):
    files = {}
    for f in os.listdir(gwsrc):
        if f.upper().endswith(".ASM"):
            files[f] = open(os.path.join(gwsrc, f), encoding="latin-1").read().splitlines()
    return files


def bodies(lines):
    """Each routine of a file (a label at the start of a line that something CALLs, or a PUBLIC one) and its
    lines, to the next routine's label."""
    called = set()  # a routine: a CALL's target, or a PUBLIC name (a jump's target alone is a routine's own label)
    for l in lines:
        code = l.split(";", 1)[0]
        m = CALLS.match(code)
        if m and m.group(1).upper() == "CALL":
            called.add(m.group(2).upper())
        p = re.match(r"\s*PUBLIC\s+(.*)", code, re.I)
        if p:
            called.update(x.strip().upper() for x in p.group(1).split(",") if x.strip())
    out, cur = {}, None
    for l in lines:
        m = TOPLABEL.match(l)
        if m and (m.group(1).upper() in called or cur is None):
            cur = m.group(1)
            out.setdefault(cur, [])
        if cur is not None:
            out[cur].append(l)
    return out, called


def labels(path):
    out = set()
    for l in open(path):
        m = re.match(r"al [0-9A-F]+ \.(\S+)", l)
        if m:
            out.add(m.group(1))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ledger")
    ap.add_argument("--src")
    ap.add_argument("--lbl", action="append", default=[], help="NAME=FILE: res=gwbasic.lbl, p1=p1.lbl, ...")
    a = ap.parse_args()
    root = os.path.dirname(os.path.abspath(a.ledger))
    entries, problems = parse(a.ledger)
    if a.src:
        files = source(a.src)
        owner = {}  # every routine label of the source: its file
        for f, lines in files.items():
            for name in bodies(lines)[0]:
                owner.setdefault(name.upper(), f)
        for (f, label), (n, kind, _, _) in sorted(entries.items(), key=lambda e: e[1][0]):
            if f not in files:
                problems.append("ledger.txt:%d: no %s in the source" % (n, f))
            elif not any(re.match(DEFINE.format(re.escape(label)), l) for l in files[f]):
                problems.append("ledger.txt:%d: %s defines no %s" % (n, f, label))
        named = {label.upper() for (_, label) in entries}
        for (f, label), (n, kind, _, _) in sorted(entries.items(), key=lambda e: e[1][0]):
            if kind not in ("translated", "oem") or f not in files:
                continue
            body = bodies(files[f])[0].get(label, [])
            for l in body:
                m = CALLS.match(l.split(";", 1)[0])
                if m:
                    t = m.group(2).upper()
                    if t in owner and t not in named and t != label.upper():
                        problems.append("ledger.txt:%d: %s %s calls %s (%s), which has no line" %
                                        (n, f, label, m.group(2), owner[t]))
                        named.add(t)
    if a.lbl:
        links = {}
        for spec in a.lbl:
            name, path = spec.split("=", 1)
            links[name] = labels(path)
        for (f, label), (n, kind, file6502, label6502) in sorted(entries.items(), key=lambda e: e[1][0]):
            if file6502 is None:
                continue
            if not os.path.exists(os.path.join(root, file6502)):
                problems.append("ledger.txt:%d: no %s" % (n, file6502))
            link = file6502.split("/", 1)[0]
            if link in links and label6502 not in links[link]:
                problems.append("ledger.txt:%d: the %s link has no %s" % (n, link, label6502))
    counts = {}
    for (f, _), (_, kind, _, _) in entries.items():
        counts.setdefault(f, {}).setdefault(kind, 0)
        counts[f][kind] += 1
    kinds = ("translated", "oem", "later", "dropped")
    print("ledger: %d routines; %s" % (len(entries), ", ".join(
        "%d %s" % (sum(c.get(k, 0) for c in counts.values()), k) for k in kinds)))
    for f in sorted(counts):
        print("  %-12s %s" % (f, "  ".join("%s %d" % (k, counts[f].get(k, 0)) for k in kinds)))
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

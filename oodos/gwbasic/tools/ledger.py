"""GW-BASIC's translation ledger, checked (hw/oodos.md, "The plan, phase 7": the source and the licence).

    python3 -I ledger.py ledger.txt [--src GWSRC | --names srcnames.txt] [--lbl NAME=FILE.lbl ...]
    python3 -I ledger.py ledger.txt --src GWSRC --write-names srcnames.txt

ledger.txt has a line for each 8086 routine the port needs: `FILE.ASM LABEL -> TARGET`, where TARGET is a
6502 file and label (`res/x.s NAME`, `p1/y.s NAME`), `OEM res/x.s NAME`, `later 7b` (a later sub-phase's),
or `dropped: why`. It fails on:

  - a line that isn't of that form, or a routine named twice;
  - with the source's names (--src, a clone of microsoft/GW-BASIC, or --names, the list made from it): a
    file that isn't there, or a label it doesn't define; and a routine that a translated one reaches with no
    line of its own, so nothing that is reached goes unaccounted for. A translated routine reaches:
      - what it CALLs, JMPs to or jumps to on a condition outside its own body (a tail jump into another
        routine, or into the middle of one, as much as a CALL);
      - what its dispatch tables name: IBMRES.ASM's R and R2 macros (STMDSP's statements, FUNDSP's
        functions) and a DW's code labels (MATH1.ASM's and SCNDRV.ASM's tables);
    where a routine is a code label that something CALLs, makes PUBLIC or names in a dispatch table, and its
    body runs to the next one;
  - with --lbl (ld65's -Ln of the resident link, `res=...`, and of each block, `p1=...`): a 6502 label the
    link hasn't, or a 6502 file that isn't in the tree.

srcnames.txt is the source's names alone, none of its text: each file's code labels, routine by routine,
and the names each routine's code reaches. --write-names makes it from --src; given both --src and --names,
the two must agree. So the build checks the ledger against srcnames.txt (the Makefile's default target),
without the source. Q and QF (IBMRES.ASM's reserved words) name tokens, not routines: they are counted, not
followed.

It prints how many routines are translated, replaced by OEM code, left for later or dropped, by 8086 file,
and the dispatch tables' counts. The source is read as text, never run, and nothing is written there.
"""
import argparse
import os
import re
import sys

LINE = re.compile(r"^(\S+\.ASM)\s+(\S+)\s+->\s+(.+?)\s*$")
TRANSLATED = re.compile(r"^(OEM\s+)?((res|p[1-8])/[\w./-]+\.s)\s+(\S+)$")
LATER = re.compile(r"^later\s+7[a-g]\b")
DROPPED = re.compile(r"^dropped:\s+\S")
NAME = r"[\w$?@.]+"
# A label at the start of a line: NAME: or NAME PROC or NAME LABEL.
DEFINE = re.compile(r"^\s*(%s)\s*(?::|\s+(?:PROC|LABEL)\b)" % NAME, re.I)
CALLS = re.compile(r"^\s*(?:%s:)?\s*(CALL|JMP|J[A-Z]{1,3})\s+(?:SHORT\s+|NEAR\s+PTR\s+|FAR\s+PTR\s+)?(%s)" % (NAME, NAME),
                   re.I)
MACRO = re.compile(r"^\s*(R2|R|QF|Q)\s+(%s)(?:\s*,\s*(%s))?" % (NAME, NAME), re.I)
DW = re.compile(r"^\s*(?:%s:?\s+)?DW\s+(.*)" % NAME, re.I)
SEGMENT = re.compile(r"^\s*(\w+)\s+(SEGMENT|ENDS)\b", re.I)
PUBLIC = re.compile(r"^\s*PUBLIC\s+(.*)", re.I)
HOW = {"c": "calls", "j": "jumps to", "r": "dispatches to (R)", "r2": "dispatches to (R2)", "dw": "dispatches to (DW)"}


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


def scan(lines):
    """One file's lines, read for names alone. Returns, a line each, (its code label or None, what it reaches
    as [(kind, name)], its reserved words); the names the file CALLs or makes PUBLIC, and the names its
    dispatch tables give; and its data labels (a label in a DSEG segment: data, not a routine). A file that
    defines a macro of its own named R, R2, Q or QF (GWRAM.ASM's R lays out RAM) has no dispatch table of them."""
    out, called, tables, data = [], set(), set(), []
    seg = None
    own = {m.group(1).upper() for m in (re.match(r"^\s*(R2|R|QF|Q)\s+MACRO\b", l, re.I) for l in lines) if m}
    for l in lines:
        code = l.split(";", 1)[0]
        s = SEGMENT.match(code)
        if s:
            seg = s.group(1).upper() if s.group(2).upper() == "SEGMENT" else None
        label, refs, words = None, [], 0
        d = DEFINE.match(code)
        if d and seg != "DSEG":
            label = d.group(1)
        elif d:
            data.append(d.group(1))
        m = CALLS.match(code)
        if m:
            kind = "c" if m.group(1).upper() == "CALL" else "j"
            refs.append((kind, m.group(2)))
            if kind == "c":
                called.add(m.group(2).upper())
        p = PUBLIC.match(code)
        if p:
            called.update(x.strip().upper() for x in p.group(1).split(",") if x.strip())
        mm = MACRO.match(code)
        if mm and mm.group(1).upper() not in own and mm.group(2).upper() != "MACRO":
            op = mm.group(1).upper()
            if op in ("Q", "QF"):
                words += 1
            elif op == "R" or mm.group(3):
                t = mm.group(3) if op == "R2" else mm.group(2)
                refs.append((op.lower(), t))
                tables.add(t.upper())
        w = DW.match(code)
        if w:
            for op in w.group(1).split(","):
                op = re.sub(r"(?i)^\s*OFFSET\s+", "", op).strip()
                if re.match(r"^[A-Za-z$?@][\w$?@.]*$", op):
                    refs.append(("dw", op))
                    tables.add(op.upper())
        out.append((label, refs, words))
    return out, called, tables, data


def model_from_source(gwsrc):
    """The source's names: {file: {"words": n, "data": [label, ...], "routines": [([label, ...], [(kind, name),
    ...])]}}, a routine's labels its head first. A head is a code label something CALLs, makes PUBLIC or names in a
    dispatch table (in any file), or a file's first code label; a routine's body runs to the next head. A
    DW's name that is no code label (a data address: ITSA86.ASM's) is left out."""
    files = {}
    for f in sorted(os.listdir(gwsrc)):
        if f.upper().endswith(".ASM"):
            files[f] = scan(open(os.path.join(gwsrc, f), encoding="latin-1").read().splitlines())
    heads, code = set(), set()
    for lines, called, tables, _ in files.values():
        heads |= called | tables
        code |= {label.upper() for label, _, _ in lines if label is not None}
    model = {}
    for f, (lines, _, _, data) in files.items():
        routines, cur, words = [], None, 0
        for label, refs, w in lines:
            words += w
            if label is not None and (label.upper() in heads or cur is None):
                cur = ([], [])
                routines.append(cur)
            if cur is None:
                continue
            if label is not None:
                cur[0].append(label)
            cur[1].extend(r for r in refs if r[0] != "dw" or r[1].upper() in code)
        model[f] = {"words": words, "data": data, "routines": routines}
    return model


def write_names(model, path, commit):
    out = ["# srcnames.txt: microsoft/GW-BASIC at %s, its names alone, none of its text. Made by" % commit,
           "# tools/ledger.py --write-names from the source; don't edit. Each file's line, FILE NAME WORDS n (n its",
           "# reserved words, IBMRES.ASM's Q and QF); its data labels, DATA name ...; then a line a routine: its code",
           "# labels, its head first, then after | what its code reaches, kind:name (c CALL, j a jump, r and r2",
           "# IBMRES.ASM's R and R2, dw a DW)."]
    for f in sorted(model):
        out.append("FILE %s WORDS %d" % (f, model[f]["words"]))
        if model[f]["data"]:
            out.append("DATA " + " ".join(model[f]["data"]))
        for labels, refs in model[f]["routines"]:
            out.append(" ".join(labels) + " |" + "".join(" %s:%s" % r for r in refs))
    open(path, "w", encoding="ascii").write("\n".join(out) + "\n")


def read_names(path):
    model, f = {}, None
    for n, l in enumerate(open(path, encoding="ascii"), 1):
        l = l.rstrip("\n")
        if not l or l.startswith("#"):
            continue
        m = re.match(r"^FILE (\S+) WORDS (\d+)$", l)
        if m:
            f = m.group(1)
            model[f] = {"words": int(m.group(2)), "data": [], "routines": []}
            continue
        if f is not None and l.startswith("DATA ") and "|" not in l:  # (a routine's line always has a |)
            model[f]["data"] += l.split()[1:]
            continue
        if f is None or "|" not in l:
            sys.exit("%s:%d: not a FILE line or a routine's" % (path, n))
        left, right = l.split("|", 1)
        model[f]["routines"].append((left.split(), [tuple(x.split(":", 1)) for x in right.split()]))
    return model


def check_source(entries, model):
    problems = []
    owner = {}  # every code label of the source: its file
    for f in model:
        for labels, _ in model[f]["routines"]:
            for label in labels:
                owner.setdefault(label.upper(), f)
    for (f, label), (n, kind, _, _) in sorted(entries.items(), key=lambda e: e[1][0]):
        if f not in model:
            problems.append("ledger.txt:%d: no %s in the source" % (n, f))
        elif not any(label.upper() == x.upper() for labels, _ in model[f]["routines"] for x in labels) and \
                label.upper() not in {x.upper() for x in model[f]["data"]}:
            problems.append("ledger.txt:%d: %s defines no %s" % (n, f, label))
    named = {label.upper() for (_, label) in entries}
    for (f, label), (n, kind, _, _) in sorted(entries.items(), key=lambda e: e[1][0]):
        if kind not in ("translated", "oem") or f not in model:
            continue
        for labels, refs in model[f]["routines"]:
            if labels[0].upper() != label.upper():
                continue
            own = {x.upper() for x in labels}
            for k, t in refs:
                u = t.upper()
                if u in owner and u not in named and u not in own:
                    problems.append("ledger.txt:%d: %s %s %s %s (%s), which has no line" % (n, f, label, HOW[k], t,
                                                                                         owner[u]))
                    named.add(u)
    return problems


def counts(model):
    c = {"r": 0, "r2": 0, "dw": 0, "words": 0}
    for f in model:
        c["words"] += model[f]["words"]
        for _, refs in model[f]["routines"]:
            for k, _ in refs:
                if k in c:
                    c[k] += 1
    return c


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
    ap.add_argument("--src", help="a clone of microsoft/GW-BASIC, read as text")
    ap.add_argument("--names", help="srcnames.txt, the source's names")
    ap.add_argument("--write-names", help="with --src: write the source's names here")
    ap.add_argument("--commit", default="edf82c2", help="with --write-names: the source's commit, for its header")
    ap.add_argument("--lbl", action="append", default=[], help="NAME=FILE: res=gwbasic.lbl, p1=p1.lbl, ...")
    a = ap.parse_args()
    root = os.path.dirname(os.path.abspath(a.ledger))
    entries, problems = parse(a.ledger)
    model = None
    if a.src:
        model = model_from_source(a.src)
        if a.write_names:
            write_names(model, a.write_names, a.commit)
    elif a.write_names:
        sys.exit("ledger.py: --write-names needs --src")
    if a.names:
        given = read_names(a.names)
        if model is not None and given != model:
            problems.append("%s isn't the source's names: make it again with --write-names" % a.names)
        model = given
    if model is not None:
        problems += check_source(entries, model)
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
    by_file = {}
    for (f, _), (_, kind, _, _) in entries.items():
        by_file.setdefault(f, {}).setdefault(kind, 0)
        by_file[f][kind] += 1
    kinds = ("translated", "oem", "later", "dropped")
    print("ledger: %d routines; %s" % (len(entries), ", ".join(
        "%d %s" % (sum(c.get(k, 0) for c in by_file.values()), k) for k in kinds)))
    for f in sorted(by_file):
        print("  %-12s %s" % (f, "  ".join("%s %d" % (k, by_file[f].get(k, 0)) for k in kinds)))
    if model is not None:
        c = counts(model)
        print("  the source's dispatch tables: %d R, %d R2 and %d DW targets, followed; %d Q and QF words, counted"
              % (c["r"], c["r2"], c["dw"], c["words"]))
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

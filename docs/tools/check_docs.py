#!/usr/bin/env python3
"""The books' drift check (docs/README.md, "Checking the books").

    python3 -I -B docs/tools/check_docs.py              # check; exit 1 on any failure
    python3 -I -B docs/tools/check_docs.py --coverage   # and list what isn't written yet
    python3 -I -B docs/tools/check_docs.py --write-index  # regenerate docs/index/*.json

Checks, against the sources the books describe:

  - every BDOS function number in oodos/include/oodos.inc has an entry in the
    Programmer's Guide, and every entry's number and name are there;
  - every I/O register equate in backplane/bios/backplane.inc (an address in
    $C000-$C7FF) has an entry in the Technical Reference, and every register
    entry's address and equates are an equate's, and its Module the module's
    ID and name (MODULES);
  - every utility (oodos/utils/NAME.s), every built-in command (ccp.s's
    BINAMES, by its long name) and each command PLANNED has a util- entry,
    and every util- entry is one of them; a line among oodos.inc's functions
    or in BINAMES that it can't read fails;
  - an entry not written yet is named in docs/tools/unwritten.txt, and a name
    there that is written now (or names nothing) fails, so the list shrinks;
  - entries follow their template's headings (docs/templates/), in order;
  - every anchor the indexes name exists; no anchor twice; every relative link
    in docs/ and AGENTS.md, and every one in hw/*.md and README.md into docs/,
    resolves, to a file and to its anchor;
  - a listing marked <!-- listing: path --> is that file, byte for byte, and
    a listed example under docs/examples has its expected output beside it;
    a marker that isn't one fails, and so does a code block under an entry's
    #### Example (or a deeper heading under it) with no marker before it;
    no example has a utility's name (oodos/Makefile would build the utility)
    or a name longer than 8 characters;
  - every section of the Technical Reference carries a status tag, and it
    examined some;
  - docs/index/*.json are what the entries generate.

It says how many of each it examined, and fails if it examined none of the
sources, files or entries. Python 3's standard library only.
"""

import argparse
import json
import os
import re
import sys

BOOKS = ("oodos-programmers-guide", "oo1-technical-reference", "oodos-users-guide")
SW_STATUS = ("built", "proposed")              # OODOS's functions and utilities
HW_STATUS = ("built", "emulated", "proposed")  # the machine's registers and sections
# Commands with no source yet, from the plan (hw/oodos.md, "Phases"): SUBMIT, GET and PUT, phase 5's, as RSXs.
# (PIP, phase 4b2's, is oodos/utils/pip.s now.)
PLANNED: tuple = ()
MODULES = {0x6: "serial", 0x7: "storage", 0x8: "DMA", 0x9: "video", 0xA: "sound", 0xB: "USB",
           0xC: "Disk II", 0xD: "north bridge", 0xE: "console"}

ANCHOR_LINE = re.compile(r'^<a id="([^"]+)"></a>\s*$')
ANCHOR_ANY = re.compile(r'<a\s+(?:id|name)="([^"]+)"')
ENTRY_ID = re.compile(r"^(bdos-\d+|reg-[0-9a-f]{4}|util-[a-z0-9]+)$")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)|!\[[^\]]*\]\(([^)\s]+)\)")
LISTING = re.compile(r"^<!--\s*listing:\s*(\S+?)(?::(\d+)-(\d+))?\s*-->\s*$")
LISTING_ANY = re.compile(r"^\s*<!--\s*listing")
STATUS_LINE = re.compile(r"^Status: \*\*(\w+)\*\*")
FN_HEAD = re.compile(r"^### (\d+) ([A-Z][A-Z0-9_]*): \S.*$")
REG_HEAD = re.compile(r"^### \$([0-9A-F]{4}) ([A-Z][A-Z0-9_]*): \S.*$")
UTIL_HEAD = re.compile(r"^### ([A-Z][A-Z0-9]*): \S.*$")


class Check:
    def __init__(self, root):
        self.root = root
        self.docs = os.path.join(root, "docs")
        self.failures = []
        self.counts = {}

    def fail(self, where, what):
        self.failures.append(f"{where}: {what}")

    def rel(self, path):
        return os.path.relpath(path, self.root)

    def read(self, path):
        with open(path, encoding="utf-8") as f:
            return f.read()

    # ---- the sources ------------------------------------------------------------------------

    def functions(self):
        """oodos.inc's BDOS function numbers: {number: name}, from its "BDOS functions" comment on. An equate there
        it can't evaluate fails."""
        path = os.path.join(self.root, "oodos", "include", "oodos.inc")
        out, on = {}, False
        for n, line in enumerate(self.read(path).splitlines(), 1):
            if line.startswith("; BDOS functions"):
                on = True
            elif on:
                m = re.match(r"^([A-Za-z_]\w*)\s*=\s*([^;]*?)\s*(?:;.*)?$", line)
                if m:
                    num = self.value(m.group(2), {})
                    if num is None:
                        self.fail(f"{self.rel(path)}:{n}", f"can't evaluate {m.group(1)} = {m.group(2)}")
                        continue
                    if num in out:
                        self.fail(f"{self.rel(path)}:{n}", f"function {num} twice ({out[num]}, {m.group(1)})")
                    out[num] = m.group(1)
                elif line.startswith(".endif"):
                    break
        return out

    def equates(self):
        """backplane.inc's equates whose value is an I/O address: {name: address}."""
        path = os.path.join(self.root, "backplane", "bios", "backplane.inc")
        values, out = {}, {}
        for n, line in enumerate(self.read(path).splitlines(), 1):
            m = re.match(r"^([A-Z][A-Z0-9_]*)\s*=\s*([^;]+?)\s*(?:;.*)?$", line)
            if not m:
                continue
            v = self.value(m.group(2), values)
            if v is None:
                self.fail(f"{self.rel(path)}:{n}", f"can't evaluate {m.group(1)} = {m.group(2)}")
                continue
            values[m.group(1)] = v
            if 0xC000 <= v <= 0xC7FF:
                out[m.group(1)] = v
        return out

    @staticmethod
    def value(expr, values):
        total = 0
        term_re = r"[A-Za-z_][A-Za-z0-9_]*|\$[0-9A-Fa-f]+|%[01]+|\d+"
        for sign, term in re.findall(rf"([+-]?)\s*({term_re})", expr):
            if term.startswith("$"):
                v = int(term[1:], 16)
            elif term.startswith("%"):
                v = int(term[1:], 2)
            elif term.isdigit():
                v = int(term)
            elif term in values:
                v = values[term]
            else:
                return None
            total += -v if sign == "-" else v
        stripped = re.sub(rf"[+\-\s]|{term_re}", "", expr)
        return None if stripped else total

    def utilities(self):
        """The transient utilities: each oodos/utils/NAME.s's NAME, sorted."""
        path = os.path.join(self.root, "oodos", "utils")
        return sorted(f[:-2] for f in os.listdir(path) if f.endswith(".s")) if os.path.isdir(path) else []

    def builtins(self):
        """The CCP's built-in commands, lower case: BINAMES in oodos/sys/ccp.s, each handler's first name, which is
        its long one (DIRSYS, not DIRS). A line there it can't read fails."""
        path = os.path.join(self.root, "oodos", "sys", "ccp.s")
        out, handlers, name, on = [], set(), None, False
        for n, line in enumerate(self.read(path).splitlines(), 1):
            if line.startswith("BINAMES:"):
                on = True
                line = line[len("BINAMES:"):]
            elif not on:
                continue
            body = line.split(";", 1)[0].strip()
            if not body:
                continue
            m = re.match(r'^\.byte\s+([^,"]+?)\s*,\s*"([A-Z0-9]+)"$', body)
            if m and self.value(m.group(1), {}) is not None:
                name = m.group(2)
                continue
            m = re.match(r"^\.word\s+(\w+)$", body)
            if m:
                if name and m.group(1) not in handlers:
                    handlers.add(m.group(1))
                    out.append(name.lower())
                name = None
                continue
            m = re.match(r"^\.byte\s+(\S+)$", body)
            if m and self.value(m.group(1), {}) == 0:
                break
            self.fail(f"{self.rel(path)}:{n}", f"BINAMES: '{body}' isn't '.byte length, \"NAME\"', '.word handler' "
                                               f"or the '.byte 0' that ends it")
        return out

    # ---- the books --------------------------------------------------------------------------

    def md_files(self):
        out = []
        for dirpath, dirnames, filenames in os.walk(self.docs):
            dirnames.sort()
            out += [os.path.join(dirpath, f) for f in sorted(filenames) if f.endswith(".md")]
        agents = os.path.join(self.root, "AGENTS.md")
        if os.path.exists(agents):
            out.append(agents)
        return out

    @staticmethod
    def lines_outside_code(text):
        """(line number, line, in a fence) for each line."""
        fence = None
        for n, line in enumerate(text.splitlines(), 1):
            m = re.match(r"^\s*(```+|~~~+)", line)
            if m:
                if fence is None:
                    fence = m.group(1)
                    yield n, line, True
                    continue
                if re.match(rf"^\s*{re.escape(fence[0])}{{{len(fence)},}}\s*$", line):
                    fence = None
                    yield n, line, True
                    continue
            yield n, line, fence is not None

    @staticmethod
    def slug(text):
        """GitHub's anchor for a heading's text."""
        text = re.sub(r"`([^`]*)`", r"\1", text)
        text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
        text = re.sub(r"<[^>]+>", "", text).strip().lower()
        text = "".join(c for c in text if c.isalnum() or c in " -_")
        return text.replace(" ", "-")

    def anchors(self, path, report=True):
        """A file's anchors: explicit ids and its headings' slugs. Duplicates fail when reporting."""
        if path in self._anchor_cache and not report:
            return self._anchor_cache[path]
        found, explicit, slugs = set(), {}, {}
        for n, line, code in self.lines_outside_code(self.read(path)):
            if code:
                continue
            for a in ANCHOR_ANY.findall(line):
                if a in explicit and report:
                    self.fail(f"{self.rel(path)}:{n}", f"anchor {a} twice (line {explicit[a]})")
                explicit.setdefault(a, n)
            h = HEADING.match(line)
            if h:
                s = self.slug(h.group(2))
                k = slugs.get(s, 0)
                slugs[s] = k + 1
                found.add(s if k == 0 else f"{s}-{k}")
        for a, n in explicit.items():
            if a in found and report:
                self.fail(f"{self.rel(path)}:{n}", f"anchor {a} is also a heading's")
        found |= set(explicit)
        self._anchor_cache[path] = found
        return found

    def templates(self):
        """Each kind's headings: [(heading, optional)], the #### lines of docs/templates/<kind>.md's skeleton
        (its fenced block)."""
        out = {}
        for kind in ("function", "register", "utility"):
            path = os.path.join(self.docs, "templates", f"{kind}.md")
            heads = []
            for _, line, code in self.lines_outside_code(self.read(path)):
                m = re.match(r"^#### (.+?)\s*(<!--\s*optional\s*-->)?\s*$", line)
                if m and code:
                    heads.append((m.group(1), bool(m.group(2))))
            if not heads:
                self.fail(self.rel(path), "no #### headings")
            out[kind] = heads
        return out

    def entries(self, path):
        """The entries in a book's file: dicts of id, line, heading, fields {heading: text}, order."""
        lines = self.read(path).splitlines()
        out, cur = [], None
        code = dict((n, c) for n, _, c in self.lines_outside_code("\n".join(lines)))
        for i, line in enumerate(lines):
            n = i + 1
            if code.get(n):
                if cur and cur["field"]:
                    cur["fields"][cur["field"]].append(line)
                continue
            a = ANCHOR_LINE.match(line)
            h = HEADING.match(line)
            if a and ENTRY_ID.match(a.group(1)):
                cur = {"id": a.group(1), "line": n, "heading": None, "fields": {}, "order": [], "field": None}
                out.append(cur)
                continue
            if cur and cur["heading"] is None:
                if line.strip() == "":
                    continue
                if line.startswith("### "):
                    cur["heading"] = line
                    continue
                self.fail(f"{self.rel(path)}:{n}", f"entry {cur['id']}: its ### heading must follow the anchor")
                cur = None
                continue
            if cur and h and len(h.group(1)) <= 4:
                if len(h.group(1)) == 4:
                    cur["order"].append(h.group(2))
                    cur["fields"][h.group(2)] = []
                    cur["field"] = h.group(2)
                    continue
                cur = None
                continue
            if cur and cur["field"]:
                cur["fields"][cur["field"]].append(line)
        for e in out:
            e["fields"] = {k: "\n".join(v).strip() for k, v in e["fields"].items()}
        return out

    # ---- the checks -------------------------------------------------------------------------

    def run(self, coverage=False, write_index=False):
        self._anchor_cache = {}
        fns = self.functions()
        regs = self.equates()
        addrs = {}
        for name, a in regs.items():
            addrs.setdefault(a, []).append(name)
        self.utils = self.utilities()
        builtins = self.builtins()
        commands = {f"util-{u}": f"oodos/utils/{u}.s" for u in self.utils}
        for b in builtins:
            commands.setdefault(f"util-{b}", "a built-in in ccp.s")
        for p in PLANNED:
            commands.setdefault(f"util-{p}", "planned")
        self.commands = commands
        templates = self.templates()
        files = self.md_files()
        self.counts.update(functions=len(fns), equates=len(regs), addresses=len(addrs), files=len(files),
                           utilities=len(self.utils), builtins=len(builtins))
        for what, n in (("function numbers in oodos.inc", len(fns)), ("I/O equates in backplane.inc", len(regs)),
                        ("utilities in oodos/utils", len(self.utils)), ("built-ins in ccp.s", len(builtins)),
                        ("Markdown files", len(files))):
            if n == 0:
                self.fail("check_docs", f"examined no {what}")

        # Entries, by book
        written = {}
        for book in BOOKS:
            for path in sorted(p for p in files if os.path.dirname(p) == os.path.join(self.docs, book)):
                for e in self.entries(path):
                    e["file"] = path
                    where = f"{self.rel(path)}:{e['line']}"
                    if e["id"] in written:
                        self.fail(where, f"entry {e['id']} twice ({self.rel(written[e['id']]['file'])}:"
                                         f"{written[e['id']]['line']})")
                        continue
                    written[e["id"]] = e
                    self.entry(e, where, book, templates, fns, regs, addrs)
        n_entries = len(written)
        self.counts["entries"] = n_entries
        if n_entries == 0:
            self.fail("check_docs", "examined no entries")

        # What should exist, what is allowlisted
        expected = {f"bdos-{n}" for n in fns} | {f"reg-{a:04x}" for a in addrs} | set(commands)
        unwritten = self.unwritten()
        for anchor in sorted(expected - set(written), key=sort_key):
            if anchor not in unwritten:
                self.fail("coverage", f"no entry {anchor} ({self.describe(anchor, fns, addrs)}), and it isn't in "
                                      f"docs/tools/unwritten.txt")
        for anchor, n in sorted(unwritten.items(), key=lambda kv: sort_key(kv[0])):
            if anchor in written:
                self.fail(f"docs/tools/unwritten.txt:{n}", f"{anchor} is written now: take it off the list")
            elif anchor not in expected:
                self.fail(f"docs/tools/unwritten.txt:{n}", f"{anchor} names nothing in the sources")
        self.counts["unwritten"] = len(unwritten)

        self.links_and_listings(files)
        self.status_tags()
        self.index(fns, regs, written, write_index)

        if coverage:
            for kind, prefix in (("functions", "bdos-"), ("registers", "reg-"), ("utilities", "util-")):
                done = sorted((a for a in written if a.startswith(prefix)), key=sort_key)
                todo = sorted((a for a in unwritten if a.startswith(prefix)), key=sort_key)
                print(f"{kind}: {len(done)} written, {len(todo)} to write")
                if done:
                    print("  written: " + " ".join(done))
                if todo:
                    print("  to write: " + " ".join(todo))
        return self.report()

    def describe(self, anchor, fns, addrs):
        if anchor.startswith("bdos-"):
            return fns.get(int(anchor[5:]), "?")
        if anchor.startswith("util-"):
            return self.commands.get(anchor, "?")
        return ", ".join(addrs.get(int(anchor[4:], 16), ["?"]))

    def unwritten(self):
        path = os.path.join(self.docs, "tools", "unwritten.txt")
        out = {}
        for n, line in enumerate(self.read(path).splitlines(), 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if not ENTRY_ID.match(line):
                self.fail(f"docs/tools/unwritten.txt:{n}", f"{line} isn't an entry's anchor")
            elif line in out:
                self.fail(f"docs/tools/unwritten.txt:{n}", f"{line} twice")
            out[line] = n
        return out

    def entry(self, e, where, book, templates, fns, regs, addrs):
        kind = {"bdos": "function", "reg": "register", "util": "utility"}[e["id"].split("-")[0]]
        home = {"function": BOOKS[0], "register": BOOKS[1], "utility": BOOKS[2]}[kind]
        if book != home:
            self.fail(where, f"{e['id']}: a {kind} entry belongs in {home}")
        if e["heading"] is None:
            self.fail(where, f"{e['id']}: no ### heading after the anchor")
            return
        # The template's headings, in order; the optional ones may be left out
        want = templates[kind]
        got, i = e["order"], 0
        for head, optional in want:
            if i < len(got) and got[i] == head:
                i += 1
            elif not optional:
                self.fail(where, f"{e['id']}: no '#### {head}' where the {kind} template has it "
                                 f"(it has {', '.join(got) or 'none'})")
                return
        if i < len(got):
            self.fail(where, f"{e['id']}: '#### {got[i]}' isn't the {kind} template's, or is out of order")
            return
        for head, _ in want:
            if head in e["fields"] and not e["fields"][head]:
                self.fail(where, f"{e['id']}: '#### {head}' is empty")
        status = self.status(e, where, HW_STATUS if kind == "register" else SW_STATUS)
        e["status"] = status
        if kind == "function":
            m = FN_HEAD.match(e["heading"])
            if not m:
                self.fail(where, f"{e['id']}: the heading isn't '### N NAME: what'")
                return
            num, name = int(m.group(1)), m.group(2)
            e["number"], e["name"] = num, name
            if e["id"] != f"bdos-{num}":
                self.fail(where, f"{e['id']}: the heading says function {num}")
            if num not in fns:
                self.fail(where, f"{e['id']}: function {num} isn't in oodos.inc")
            elif fns[num] != name:
                self.fail(where, f"{e['id']}: oodos.inc calls function {num} {fns[num]}, not {name}")
            since = e["fields"].get("Since", "")
            if status == "built" and not re.match(r"^OODOS \d+\.\d+\b", since):
                self.fail(where, f"{e['id']}: a built function's Since is 'OODOS major.minor'")
            e["since"] = since.split("\n")[0] if since else None
        elif kind == "register":
            m = REG_HEAD.match(e["heading"])
            if not m:
                self.fail(where, f"{e['id']}: the heading isn't '### $ADDR NAME: what'")
                return
            addr, name = int(m.group(1), 16), m.group(2)
            e["address"] = addr
            if e["id"] != f"reg-{addr:04x}":
                self.fail(where, f"{e['id']}: the heading says ${addr:04X}")
            if addr not in addrs:
                self.fail(where, f"{e['id']}: no equate in backplane.inc is ${addr:04X}")
                return
            named = re.findall(r"`([A-Z][A-Z0-9_]*)`", e["fields"].get("Equates", ""))
            e["equates"] = named
            if name not in named:
                self.fail(where, f"{e['id']}: {name}, the heading's, isn't in its Equates")
            for q in named:
                if q not in regs:
                    self.fail(where, f"{e['id']}: {q} isn't an I/O equate in backplane.inc")
                elif regs[q] != addr:
                    self.fail(where, f"{e['id']}: {q} is ${regs[q]:04X} in backplane.inc, not ${addr:04X}")
            for q in addrs[addr]:
                if q not in named:
                    self.fail(where, f"{e['id']}: {q} is ${addr:04X} too, and its Equates don't name it")
            module = e["fields"].get("Module", "")
            mod = (addr - 0xC000) >> 7
            if not re.match(rf"^{mod:X}\b", module):
                self.fail(where, f"{e['id']}: ${addr:04X} is module {mod:X}'s; its Module says '{module[:20]}'")
            elif mod not in MODULES:
                self.fail(where, f"{e['id']}: module {mod:X} has no name in check_docs.py's MODULES")
            elif MODULES[mod].lower() not in module.lower():
                self.fail(where, f"{e['id']}: module {mod:X} is the {MODULES[mod]} card; its Module says "
                                 f"'{module[:30]}'")
            e["module"] = f"{mod:X}"
        else:
            m = UTIL_HEAD.match(e["heading"])
            if not m:
                self.fail(where, f"{e['id']}: the heading isn't '### NAME: what'")
                return
            if e["id"] != f"util-{m.group(1).lower()}":
                self.fail(where, f"{e['id']}: the heading says {m.group(1)}")
            if e["id"] not in self.commands:
                self.fail(where, f"{e['id']}: no oodos/utils/{e['id'][5:]}.s, built-in or planned command")

    def status(self, e, where, allowed):
        text = e["fields"].get("Status", "")
        m = re.match(r"^\*\*(\w+)\*\*", text)
        if not m or m.group(1) not in allowed:
            self.fail(where, f"{e['id']}: its Status is **{'**, **'.join(allowed)}**, not '{text[:30]}'")
            return None
        return m.group(1)

    def links_in(self, path, lines, code, into=None):
        """Check a file's relative links (with into, only those to a file under it); how many."""
        # Links, whose text may run over lines: code blocks and code spans blanked first
        prose = "\n".join("" if code[n] else re.sub(r"`[^`]*`", "", line) for n, line in enumerate(lines, 1))
        links = 0
        for m in LINK.finditer(prose):
            target = m.group(1) or m.group(2)
            if re.match(r"^[a-z][a-z0-9+.-]*:", target):
                continue  # http:, mailto:
            if into:
                file = target.partition("#")[0]
                dest = os.path.normpath(os.path.join(os.path.dirname(path), file)) if file else path
                if not dest.startswith(into + os.sep):
                    continue
            links += 1
            self.link(path, prose.count("\n", 0, m.start()) + 1, target)
        return links

    def links_and_listings(self, files):
        links = listings = 0
        examples = os.path.join(self.docs, "examples")
        # The design record and the repository's README, for their links into the books only
        hw = os.path.join(self.root, "hw")
        others = [os.path.join(hw, f) for f in sorted(os.listdir(hw)) if f.endswith(".md")] if os.path.isdir(hw) else []
        others += [p for p in (os.path.join(self.root, "README.md"),) if os.path.isfile(p)]
        for path in others:
            text = self.read(path)
            links += self.links_in(path, text.splitlines(), {n: c for n, _, c in self.lines_outside_code(text)},
                                   into=self.docs)
        for path in files:
            text = self.read(path)
            self.anchors(path)  # duplicates
            lines = text.splitlines()
            code = {n: c for n, _, c in self.lines_outside_code(text)}
            links += self.links_in(path, lines, code)
            example = False  # under an entry's #### Example (and its deeper headings), whose code must be a listing
            for n, line in enumerate(lines, 1):
                if code[n]:
                    if example and not code.get(n - 1):
                        before = next((l for l in reversed(lines[:n - 1]) if l.strip()), "")
                        if not LISTING.match(before):
                            self.fail(f"{self.rel(path)}:{n}", "a code block under #### Example with no listing "
                                                               "marker: show a file in docs/examples, so it's tested")
                    continue
                h = HEADING.match(line)
                if h and len(h.group(1)) <= 4:
                    example = h.group(1) == "####" and h.group(2) == "Example"
                m = LISTING.match(line)
                if m:
                    listings += 1
                    self.listing(path, n, lines, m, examples)
                elif LISTING_ANY.match(line):
                    self.fail(f"{self.rel(path)}:{n}", "a listing marker that isn't '<!-- listing: path -->' or "
                                                       "'<!-- listing: path:A-B -->'")
        for name in sorted(os.listdir(examples)) if os.path.isdir(examples) else []:
            if not name.endswith(".s"):
                continue
            if not os.path.exists(os.path.join(examples, name[:-2] + ".out")):
                self.fail(f"docs/examples/{name}", "no expected output beside it (.out): it isn't tested")
            if len(name[:-2]) > 8:
                self.fail(f"docs/examples/{name}", "a name longer than 8 characters: the test puts it on M:, "
                                                   "whose names are 8.3")
            if name[:-2] in self.utils:
                self.fail(f"docs/examples/{name}", f"{name[:-2]} is a utility's name: oodos/Makefile builds "
                                                   f"oodos/utils/{name} for it, so the book would show this file "
                                                   f"and the test run that one")
        self.counts.update(links=links, listings=listings)
        if links == 0:
            self.fail("check_docs", "examined no links")
        if listings == 0:
            self.fail("check_docs", "examined no listings")

    def link(self, path, n, target):
        where = f"{self.rel(path)}:{n}"
        file, _, frag = target.partition("#")
        dest = os.path.normpath(os.path.join(os.path.dirname(path), file)) if file else path
        if not os.path.exists(dest):
            self.fail(where, f"link to {target}: no {self.rel(dest)}")
            return
        if frag:
            if not dest.endswith(".md") or os.path.isdir(dest):
                self.fail(where, f"link to {target}: an anchor in a file that isn't Markdown")
            elif frag not in self.anchors(dest, report=False):
                self.fail(where, f"link to {target}: no anchor {frag} in {self.rel(dest)}")

    def listing(self, path, n, lines, m, examples):
        where = f"{self.rel(path)}:{n}"
        src = os.path.normpath(os.path.join(os.path.dirname(path), m.group(1)))
        if not os.path.isfile(src):
            self.fail(where, f"listing of {m.group(1)}: no such file")
            return
        want = self.read(src).splitlines()
        if m.group(2):
            a, b = int(m.group(2)), int(m.group(3))
            if not 1 <= a <= b <= len(want):
                self.fail(where, f"listing of {m.group(1)}: lines {a}-{b} aren't in its {len(want)}")
                return
            want = want[a - 1:b]
        i = n
        while i < len(lines) and not lines[i].strip():
            i += 1
        if i >= len(lines) or not re.match(r"^```", lines[i]):
            self.fail(where, f"listing of {m.group(1)}: no fenced block after the marker")
            return
        j = i + 1
        while j < len(lines) and not lines[j].startswith("```"):
            j += 1
        got = lines[i + 1:j]
        if got != want:
            k = next((k for k in range(max(len(got), len(want)))
                      if k >= len(got) or k >= len(want) or got[k] != want[k]), 0)
            self.fail(where, f"listing of {m.group(1)} differs from the file at its line {k + 1}: "
                             f"{(got[k] if k < len(got) else '<end>')!r} against "
                             f"{(want[k] if k < len(want) else '<end>')!r}")
        if os.path.dirname(src) == examples and src.endswith(".s") and not os.path.exists(src[:-2] + ".out"):
            self.fail(where, f"listing of {m.group(1)}: an example with no expected output isn't tested")

    def status_tags(self):
        """Every ## and ### section of the Technical Reference starts with 'Status: **tag**'."""
        book = os.path.join(self.docs, BOOKS[1])
        sections = 0
        for name in sorted(os.listdir(book)) if os.path.isdir(book) else []:
            if not name.endswith(".md") or name == "README.md":
                continue
            path = os.path.join(book, name)
            text = self.read(path)
            lines = text.splitlines()
            code = {n: c for n, _, c in self.lines_outside_code(text)}  # the same lines, a trailing blank one too
            for i, line in enumerate(lines):
                h = HEADING.match(line)
                if code[i + 1] or not h or len(h.group(1)) not in (2, 3):
                    continue
                before = next((l for l in reversed(lines[:i]) if l.strip()), "")
                a = ANCHOR_LINE.match(before)
                if a and ENTRY_ID.match(a.group(1)):
                    continue  # an entry: its own #### Status
                sections += 1
                nxt = next((l for l in lines[i + 1:] if l.strip()), "")
                m = STATUS_LINE.match(nxt)
                if not m or m.group(1) not in HW_STATUS:
                    self.fail(f"{self.rel(path)}:{i + 1}", f"section '{h.group(2)}' doesn't start with "
                                                           f"'Status: **built**', '**emulated**' or '**proposed**'")
        self.counts["sections"] = sections
        if sections == 0:
            self.fail("check_docs", "examined no Technical Reference sections")

    def index(self, fns, regs, written, write):
        def where(e):
            return os.path.relpath(e["file"], self.docs) if e else None

        functions = []
        for num in sorted(fns):
            e = written.get(f"bdos-{num}")
            functions.append({"number": num, "name": fns[num], "documented": e is not None,
                              "anchor": e["id"] if e else None, "file": where(e),
                              "since": e.get("since") if e else None, "status": e.get("status") if e else None})
        registers = []
        for name, addr in sorted(regs.items(), key=lambda kv: (kv[1], kv[0])):
            e = written.get(f"reg-{addr:04x}")
            registers.append({"equate": name, "address": f"${addr:04X}", "module": f"{(addr - 0xC000) >> 7:X}",
                              "documented": e is not None, "anchor": e["id"] if e else None, "file": where(e),
                              "status": e.get("status") if e else None})
        for fname, data in (("functions.json", functions), ("registers.json", registers)):
            path = os.path.join(self.docs, "index", fname)
            text = json.dumps(data, indent=1) + "\n"
            if write:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
                print(f"wrote {self.rel(path)}")
                continue
            if not os.path.exists(path):
                self.fail(f"docs/index/{fname}", "missing: make -C docs index")
                continue
            if self.read(path) != text:
                self.fail(f"docs/index/{fname}", "isn't what the entries generate: make -C docs index")
            for rec in json.loads(self.read(path)):
                a = rec.get("anchor")
                if not a:
                    continue
                where = os.path.join(self.docs, rec["file"]) if rec.get("file") else None
                if not where or not os.path.isfile(where):
                    self.fail(f"docs/index/{fname}", f"anchor {a}'s file {rec.get('file')} isn't there")
                elif a not in self.anchors(where, report=False):
                    self.fail(f"docs/index/{fname}", f"anchor {a} isn't in {rec.get('file')}")

    def report(self):
        c = self.counts
        print(f"check_docs: {c.get('functions', 0)} function numbers, {c.get('equates', 0)} I/O equates at "
              f"{c.get('addresses', 0)} addresses, {c.get('utilities', 0)} utilities, {c.get('builtins', 0)} built-ins; "
              f"{c.get('files', 0)} Markdown files, {c.get('entries', 0)} "
              f"entries, {c.get('unwritten', 0)} unwritten, {c.get('links', 0)} links, {c.get('listings', 0)} "
              f"listings, {c.get('sections', 0)} Technical Reference sections")
        for f in self.failures:
            print(f"FAIL {f}")
        print("check_docs: " + (f"{len(self.failures)} failures" if self.failures else "OK"))
        return 1 if self.failures else 0


def sort_key(anchor):
    kind, _, rest = anchor.partition("-")
    return (kind, int(rest, 16) if kind == "reg" else int(rest) if rest.isdigit() else 0, rest)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                    help="the repository (default: this script's)")
    ap.add_argument("--coverage", action="store_true", help="list what is written and what isn't yet")
    ap.add_argument("--write-index", action="store_true", help="regenerate docs/index/*.json from the entries")
    args = ap.parse_args()
    return Check(os.path.abspath(args.root)).run(coverage=args.coverage, write_index=args.write_index)


if __name__ == "__main__":
    sys.exit(main())

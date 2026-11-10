#!/usr/bin/env python3
"""check_docs.py's own test (docs/README.md, "Checking the books").

    python3 -I -B docs/tools/test_check_docs.py

Copies what the check reads (docs/, hw/, AGENTS.md, README.md, the two
includes, the utilities' sources, ccp.s and the files the books link to)
into a scratch directory, checks the copy as it is (it must pass), then
breaks a fresh copy one way at a time and expects the check to fail, saying
what it found. Python 3's standard library only.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
CHECK = os.path.join(HERE, "check_docs.py")
PG7 = "docs/oodos-programmers-guide/07-function-reference.md"
TR12 = "docs/oo1-technical-reference/12-front-panel.md"
TR10 = "docs/oo1-technical-reference/10-storage.md"
INC = "oodos/include/oodos.inc"
BP = "backplane/bios/backplane.inc"
UNWRITTEN = "docs/tools/unwritten.txt"
UTILS = "oodos/utils"
CCP = "oodos/sys/ccp.s"

# A utility entry as the template has it, for a command that isn't one.
FROB = """
<a id="util-frob"></a>
### FROB: nothing

#### Forms

```
FROB
```

#### Options

None.

#### Output

None.

#### Errors

None.

#### Since

Not yet.

#### Status

**proposed**

#### CP/M 3

None.
"""


def links_out(root):
    """The files outside docs/ and hw/ that docs/ and AGENTS.md link to."""
    out = set()
    for dirpath, _, files in os.walk(os.path.join(root, "docs")):
        for f in files:
            if f.endswith(".md"):
                out |= targets(root, os.path.join(dirpath, f))
    out |= targets(root, os.path.join(root, "AGENTS.md"))
    return {p for p in out if not p.startswith(("docs" + os.sep, "hw" + os.sep))}


def targets(root, path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    out = set()
    found = re.findall(r"\]\(([^)\s#]+)(?:#[^)]*)?\)", text)
    # A listing's file too, if it is there: docs/README.md shows the marker itself, in a code block.
    found += [t for t in re.findall(r"^<!--\s*listing:\s*(\S+?)(?::\d+-\d+)?\s*-->\s*$", text, re.M)
              if os.path.isfile(os.path.join(os.path.dirname(path), t))]
    for t in found:
        if not re.match(r"^[a-z][a-z0-9+.-]*:", t):
            p = os.path.relpath(os.path.normpath(os.path.join(os.path.dirname(path), t)), root)
            if not p.startswith(".."):
                out.add(p)
    return out


def copy_tree(dest):
    ignore = shutil.ignore_patterns("build", "__pycache__")
    for d in ("docs", "hw", UTILS):
        shutil.copytree(os.path.join(REPO, d), os.path.join(dest, d), ignore=ignore)
    for f in sorted({"AGENTS.md", "README.md", INC, BP, CCP} | links_out(REPO)):
        src = os.path.join(REPO, f)
        if os.path.isdir(src):
            os.makedirs(os.path.join(dest, f), exist_ok=True)  # a link to a directory: it need only exist
            continue
        os.makedirs(os.path.dirname(os.path.join(dest, f)), exist_ok=True)
        shutil.copy2(src, os.path.join(dest, f))


def run(root, *args):
    p = subprocess.run([sys.executable, "-I", "-B", CHECK, "--root", root, *args],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def edit(root, rel, old, new, count=1):
    path = os.path.join(root, rel)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if old not in text:
        raise AssertionError(f"the case's own text isn't in {rel}: {old!r}")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text.replace(old, new, count))


def edit_re(root, rel, pattern, new):
    """The first match of `pattern` in `rel` replaced by `new` (re.sub's), so a case needn't name text, such as
    a version, that the books change."""
    path = os.path.join(root, rel)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if not re.search(pattern, text):
        raise AssertionError(f"the case's own pattern isn't in {rel}: {pattern!r}")
    with open(path, "w", encoding="utf-8") as f:
        f.write(re.sub(pattern, new, text, count=1))


def append(root, rel, text):
    with open(os.path.join(root, rel), "a", encoding="utf-8") as f:
        f.write(text)


def write(root, rel, text):
    with open(os.path.join(root, rel), "w", encoding="utf-8") as f:
        f.write(text)


def drop_line(root, rel, pattern):
    path = os.path.join(root, rel)
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines(keepends=True)
    kept = [l for l in lines if not re.match(pattern, l)]
    if len(kept) == len(lines):
        raise AssertionError(f"no line {pattern!r} in {rel}")
    with open(path, "w", encoding="utf-8") as f:
        f.writelines(kept)


def drop_listings(root):
    """Every listing marker in the books gone, wherever the books have them."""
    for dirpath, _, files in os.walk(os.path.join(root, "docs")):
        for f in files:
            if f.endswith(".md"):
                path = os.path.join(dirpath, f)
                with open(path, encoding="utf-8") as fh:
                    text = fh.read()
                cut = re.sub(r"^<!--\s*listing:[^\n]*-->\n", "", text, flags=re.M)
                if cut != text:
                    with open(path, "w", encoding="utf-8") as fh:
                        fh.write(cut)


def edit_entry(root, rel, anchor, old, new):
    """edit(), but only within the entry whose anchor is `anchor`, up to the next entry's."""
    path = os.path.join(root, rel)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    start = text.index(f'<a id="{anchor}"></a>')
    end = text.find('<a id="', start + 1)
    end = len(text) if end < 0 else end
    if old not in text[start:end]:
        raise AssertionError(f"the case's own text isn't in {rel}'s {anchor}: {old!r}")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text[:start] + text[start:end].replace(old, new, 1) + text[end:])


def drop_entry(root, rel, anchor):
    """An entry taken out of its chapter: from its anchor to the next anchor, or the end."""
    path = os.path.join(root, rel)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    start = text.index(f'<a id="{anchor}"></a>')
    end = text.find('<a id="', start + 1)
    end = len(text) if end < 0 else end
    with open(path, "w", encoding="utf-8") as f:
        f.write(text[:start] + text[end:])


# (name, what breaks the copy, a message the check must give; None: the check must pass)
CASES = [
    ("the tree as it is", lambda r: None, None),
    ("a function in oodos.inc with no entry",
     lambda r: edit(r, INC, "F_PARSE         = 152", "F_PARSE         = 152\nF_NEWONE        = 200"),
     "no entry bdos-200 (F_NEWONE)"),
    ("a function in hex in oodos.inc with no entry",
     lambda r: edit(r, INC, "F_PARSE         = 152", "F_PARSE         = 152\nF_NEWONE        = $C8"),
     "no entry bdos-200 (F_NEWONE)"),
    ("a function in oodos.inc the check can't read",
     lambda r: edit(r, INC, "F_PARSE         = 152", "F_PARSE         = 152\nF_NEWONE        = 2OO"),
     "can't evaluate F_NEWONE = 2OO"),
    ("oodos.inc renames a function an entry names",
     lambda r: edit(r, INC, "C_WRITESTR      = 9", "C_PRINTSTR      = 9"),
     "oodos.inc calls function 9 C_PRINTSTR, not C_WRITESTR"),
    ("an entry for a function oodos.inc hasn't",
     lambda r: drop_line(r, INC, r"^C_WRITESTR\s*="), "function 9 isn't in oodos.inc"),
    ("an I/O equate in backplane.inc with no entry",
     lambda r: edit(r, BP, "NB_ID           = NB + $7F", "NB_ID           = NB + $7F\nNB_NEW          = NB + $20"),
     "no entry reg-c6a0 (NB_NEW)"),
    ("a register with no entry, on the unwritten list",
     lambda r: (drop_entry(r, TR12, "reg-c77f"), append(r, UNWRITTEN, "reg-c77f\n"), run(r, "--write-index")), None),
    ("a written register put back on unwritten.txt",
     lambda r: append(r, UNWRITTEN, "reg-c77f\n"), "reg-c77f is written now: take it off the list"),
    ("a register entry at an address no equate has",
     lambda r: (edit(r, TR12, '<a id="reg-c77f">', '<a id="reg-c77e">'),
                edit(r, TR12, "### $C77F CONSOLE_ID", "### $C77E CONSOLE_ID")),
     "no equate in backplane.inc is $C77E"),
    ("a register entry naming an equate of another address",
     lambda r: edit_entry(r, TR12, "reg-c77f", "`CONSOLE_ID`", "`CONSOLE_ID`, `CONSOLE_IEN`"),
     "CONSOLE_IEN is $C718 in backplane.inc, not $C77F"),
    ("a register entry leaving out an equate of its address",
     lambda r: edit(r, BP, "CONSOLE_ID      = $C77F", "CONSOLE_ID      = $C77F\nCONSOLE_TWO     = $C77F"),
     "CONSOLE_TWO is $C77F too, and its Equates don't name it"),
    ("a register entry with the wrong module",
     lambda r: edit_entry(r, TR12, "reg-c77f", "E, the console card", "D, the north bridge"),
     "$C77F is module E's"),
    ("a register entry with the wrong module name",
     lambda r: edit_entry(r, TR12, "reg-c77f", "E, the console card", "E, the north bridge"),
     "module E is the console card"),
    ("a Disk II register entry with the wrong module name",
     lambda r: edit_entry(r, TR10, "reg-c67f", "C, the Disk II card", "C, the video card"),
     "module C is the Disk II card"),
    ("a written entry still on the unwritten list",
     lambda r: append(r, UNWRITTEN, "bdos-9\n"), "bdos-9 is written now: take it off the list"),
    ("an unwritten name that names nothing",
     lambda r: append(r, UNWRITTEN, "bdos-201\n"), "bdos-201 names nothing in the sources"),
    ("a utility with no entry and off the list",
     lambda r: edit(r, "docs/oodos-users-guide/04-built-in-commands.md", '<a id="util-type"></a>\n', ""),
     "no entry util-type (oodos/utils/type.s)"),
    ("a utility misspelled on the list",
     lambda r: append(r, UNWRITTEN, "util-setdeff\n"), "util-setdeff names nothing in the sources"),
    ("a new utility's source with no entry",
     lambda r: write(r, UTILS + "/newone.s", "; a new utility\n"), "no entry util-newone (oodos/utils/newone.s)"),
    ("a utility entry for a command that isn't one",
     lambda r: append(r, "docs/oodos-users-guide/05-utilities.md", FROB),
     "util-frob: no oodos/utils/frob.s, built-in or planned command"),
    ("a built-in with a hex length and no entry",
     lambda r: edit(r, CCP, '                .byte   2, "GO"',
                    '                .byte   $04, "HELP"\n                .word   B_HELP\n                .byte   2, "GO"'),
     "no entry util-help (a built-in in ccp.s)"),
    ("a line in BINAMES the check can't read",
     lambda r: edit(r, CCP, '                .byte   2, "GO"',
                    '                .byte   4 "HELP"\n                .word   B_HELP\n                .byte   2, "GO"'),
     "BINAMES: '.byte   4 \"HELP\"' isn't"),
    ("an entry missing a template heading",
     lambda r: edit(r, PG7, "#### Cycles\n\nNot measured.", "Not measured."), "no '#### Cycles'"),
    ("an entry's headings out of order",
     lambda r: edit_re(r, PG7, r"#### Since\n\n(OODOS \d+\.\d+)\n\n#### Status\n\n\*\*built\*\*",
                       r"#### Status\n\n**built**\n\n#### Since\n\n\1"),
     "no '#### Since' where the function template has it"),
    ("an entry with a heading the template hasn't",
     lambda r: edit(r, PG7, "#### CP/M 3\n", "#### Notes\n\nSome.\n\n#### CP/M 3\n"), "no '#### CP/M 3'"),
    ("an entry with an unknown status", lambda r: edit(r, PG7, "**built**", "**done**"), "its Status is"),
    ("an entry whose heading has another number",
     lambda r: edit(r, PG7, "### 9 C_WRITESTR:", "### 8 C_WRITESTR:"), "bdos-9: the heading says function 8"),
    ("a function entry in another book",
     lambda r: (edit(r, PG7, '<a id="bdos-2"></a>\n', ""),
                append(r, "docs/oodos-users-guide/06-errors.md",
                       "\n<a id=\"bdos-2\"></a>\n### 2 C_WRITE: a character\n")), "a function entry belongs in"),
    ("the same entry twice",
     lambda r: append(r, "docs/oodos-programmers-guide/appendix-a-errors.md",
                      "\n<a id=\"bdos-9\"></a>\n### 9 C_WRITESTR: again\n"), "entry bdos-9 twice"),
    ("an anchor twice in a file",
     lambda r: append(r, "docs/oodos-users-guide/06-errors.md", '\n<a id="here"></a>\n\n<a id="here"></a>\n'),
     "anchor here twice"),
    ("a link to a file that isn't there",
     lambda r: append(r, "docs/oodos-users-guide/06-errors.md", "\nSee [nothing](no-such-file.md).\n"),
     "link to no-such-file.md: no docs/oodos-users-guide/no-such-file.md"),
    ("a link to an anchor that isn't there",
     lambda r: append(r, "docs/oodos-users-guide/06-errors.md",
                      "\nSee [it](../oodos-programmers-guide/07-function-reference.md#bdos-201).\n"),
     "no anchor bdos-201 in docs/oodos-programmers-guide/07-function-reference.md"),
    ("a link to a heading of hw/ that isn't there",
     lambda r: append(r, "docs/oodos-users-guide/06-errors.md", "\nSee [it](../../hw/oodos.md#no-such-heading).\n"),
     "no anchor no-such-heading in hw/oodos.md"),
    ("a link from hw/ into docs/ to a file that isn't there",
     lambda r: append(r, "hw/oodos.md", "\nSee [nothing](../docs/no-such-file.md).\n"),
     "link to ../docs/no-such-file.md: no docs/no-such-file.md"),
    ("a link from README.md into docs/ to an anchor that isn't there",
     lambda r: edit(r, "README.md", "](docs/README.md)", "](docs/README.md#no-such-heading)"),
     "no anchor no-such-heading in docs/README.md"),
    ("a listing that isn't its file",
     lambda r: edit(r, "docs/examples/writestr.s", "; the function in Y", "; Y: the function"),
     "listing of ../examples/writestr.s differs from the file at its line 11"),
    ("a listing marker that isn't one",
     lambda r: edit(r, PG7, "<!-- listing: ../examples/writestr.out -->", "<!-- listing ../examples/writestr.out -->"),
     "a listing marker that isn't"),
    ("no listings", drop_listings, "examined no listings"),
    ("an example's code with no listing marker",
     lambda r: edit(r, PG7, "At the prompt, with WRITESTR.COM", "```\n        lda     #0\n```\n\nAt the prompt, with WRITESTR.COM"),
     "a code block under #### Example with no listing marker"),
    ("a heading under an entry's field is part of that field",
     lambda r: edit(r, PG7, "#### Errors\n\nNone.\n", "#### Errors\n\nNone.\n\n##### In each error mode\n\nThe same.\n"),
     None),
    ("an example's code under a deeper heading with no listing marker",
     lambda r: edit(r, PG7, "At the prompt, with WRITESTR.COM",
                    "##### Another\n\n```\n        lda     #0\n```\n\nAt the prompt, with WRITESTR.COM"),
     "a code block under #### Example with no listing marker"),
    ("a listed example with no expected output",
     lambda r: os.remove(os.path.join(r, "docs/examples/writestr.out")),
     "an example with no expected output isn't tested"),
    ("an example with a utility's name",
     lambda r: (write(r, "docs/examples/date.s", "; not DATE.COM\n"), write(r, "docs/examples/date.out", "")),
     "date is a utility's name"),
    ("a C example with no expected output",
     lambda r: write(r, "docs/examples/cnoout.c", "int main(void) { return 0; }\n"),
     "docs/examples/cnoout.c: no expected output beside it"),
    ("a C example and a ca65 one with one name",
     lambda r: (write(r, "docs/examples/writestr.c", "int main(void) { return 0; }\n")),
     "writestr.s and writestr.c: two programs"),
    ("an example with a name longer than 8",
     lambda r: (write(r, "docs/examples/longnamed.s", "; LONGNAMED\n"), write(r, "docs/examples/longnamed.out", "")),
     "a name longer than 8 characters"),
    ("a Technical Reference section with no status tag",
     lambda r: append(r, "docs/oo1-technical-reference/14-emulator-notes.md", "\n## Untagged\n\nText.\n"),
     "section 'Untagged' doesn't start with 'Status:"),
    ("no Technical Reference sections",
     lambda r: [os.remove(os.path.join(r, "docs/oo1-technical-reference", f))
                for f in os.listdir(os.path.join(r, "docs/oo1-technical-reference")) if f != "README.md"],
     "examined no Technical Reference sections"),
    ("an index edited by hand",
     lambda r: edit_re(r, "docs/index/functions.json", r'"since": "OODOS \d+\.\d+"', '"since": "OODOS 9.9"'),
     "docs/index/functions.json: isn't what the entries generate"),
    ("a register index edited by hand",
     lambda r: edit(r, "docs/index/registers.json", '"status": "emulated"', '"status": "proposed"'),
     "docs/index/registers.json: isn't what the entries generate"),
    ("an index naming an anchor that isn't there",
     lambda r: edit(r, "docs/index/functions.json", '"anchor": "bdos-9"', '"anchor": "bdos-999"'),
     "anchor bdos-999 isn't in oodos-programmers-guide/07-function-reference.md"),
    ("no function numbers in oodos.inc",
     lambda r: edit(r, INC, "; BDOS functions", "; (no functions)"), "examined no function numbers in oodos.inc"),
    ("no I/O equates in backplane.inc",
     lambda r: open(os.path.join(r, BP), "w").close(), "examined no I/O equates in backplane.inc"),
]


def main():
    failed = 0
    with tempfile.TemporaryDirectory(prefix="check_docs.") as tmp:
        for i, (name, breaks, want) in enumerate(CASES):
            root = os.path.join(tmp, str(i))
            copy_tree(root)
            breaks(root)
            code, out = run(root)
            if want is None:
                ok = code == 0 and "check_docs: OK" in out
            else:
                ok = code == 1 and want in out
            print(f"{'ok  ' if ok else 'FAIL'} {name}")
            if not ok:
                failed += 1
                print(f"     exit {code}; wanted {'a pass' if want is None else repr(want)}; it said:")
                print("     " + out.strip().replace("\n", "\n     "))
            shutil.rmtree(root)
    print(f"test_check_docs: {len(CASES)} cases, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

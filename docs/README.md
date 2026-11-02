# The OO-1's books

Three books, for people and for agents. [hw/](../hw/) is the design record: the why, the decisions, the options
turned down. These are the reference: the what, written from hw/'s as-built sections and the code, linking back to
hw/ for the reasons. Where a book and the code disagree, the code is right and the book is a bug.

| Book | For | Covers |
|---|---|---|
| [OODOS Programmer's Guide](oodos-programmers-guide/README.md) | anyone writing a `.COM` or `.PRL` program | the BDOS call, the file system, the console and the SCB, memory and banking, the program formats, every function |
| [OO-1 Technical Reference](oo1-technical-reference/README.md) | BIOS, driver and OOLIB writers; bare-metal and ROM programs | the backplane, banking, the boot, interrupts, DMA, each card's registers, Apple II mode, the emulator |
| [OODOS User's Guide](oodos-users-guide/README.md) | whoever types at `A>` | the command line, drives and files, the built-in commands, the utilities, errors |

The Programmer's and User's Guides follow the outlines of CP/M 3's two guides and keep the meaning of every
function number, but every word is ours: no Digital Research text or code goes into them, as none goes into OODOS
([hw/oodos.md, "Goals"](../hw/oodos.md#goals)). An agent starting on OO-1 code reads [AGENTS.md](../AGENTS.md)
first.

## Phases

The books are written in phases of their own (decided 2026-10-31):

- **G0:** this skeleton: every chapter's scope and sections, the templates, the indexes, the checks, the examples
  suite, and one entry written in full ([function 9](oodos-programmers-guide/07-function-reference.md#bdos-9)).
- **G1:** the Programmer's Guide for what OODOS phases 0-4 have built, and the User's Guide.
- **G2:** the Technical Reference, for what the emulator implements.
- **G3:** with the OOLIB phase, which settles [hw/oolib.md](../hw/oolib.md)'s open questions first: the
  Programmer's Guide's chapter 8.

After G3, an OODOS phase isn't done until its chapters, entries and indexes are.

**Open for G2:** where a register with no equate in backplane.inc gets its source of truth: the VIA at `$C340`,
the SID's registers, most of the video card's. The check refuses a register entry without an equate today, so
these can't be entries until that is settled.

## Status tags

Every section of the Technical Reference, and every entry, says how far what it describes has got:

- **built:** for hardware, on a real card; for OODOS, in OODOS.SYS (or on the card) and tested.
- **emulated:** in the emulator, not built yet (hardware only).
- **proposed:** designed, not built or emulated.

A section's tag is the line `Status: **tag**` straight after its heading; an entry's is its `#### Status`.

## Conventions

- **Numbers.** Hex with a `$`, in a code span: `$C680`, `$FF`. Decimal plain: function 9, 128 bytes. Binary with
  a `%`: `%1010`. Bits are numbered from 0, the least significant; "bit 7" is the top of a byte.
- **Registers.** The CPU's are A, X, Y, S, P and its flags (C, Z, I, D, V, N). A word in registers is "A (low)
  and X (high)", written A/X. A hardware register is called by its equate (`NB_WIN0`) and address
  (`$C680`), as [`backplane.inc`](../backplane/bios/backplane.inc) names it.
- **Names.** BDOS functions by number and [`oodos.inc`](../oodos/include/oodos.inc)'s name: 9 `C_WRITESTR`.
  Drives as `A:`, files as `NAME.TYP`, keys as ^C.
- **Anchors.** Every entry has an explicit HTML anchor on the line before its heading, so a reader or an agent can
  link to it and the indexes can name it: `bdos-` and the function number (`bdos-14`), `reg-` and the address in
  lower-case hex (`reg-c680`), `util-` and the command's name (`util-dir`). Link to an entry as
  `07-function-reference.md#bdos-14`; to a section by GitHub's heading anchor.
- **Prose.** As [hw/oodos.md](../hw/oodos.md): plain, short sentences, "ours" for what OODOS does its own way, no
  selling.

## Entries

Functions, registers and commands are written as entries, each from its template, with fixed headings in a fixed
order:

- [templates/function.md](templates/function.md): a BDOS function (Entry, Exit, Errors, Side effects, Cycles,
  Since, Status, CP/M 3; Example optional).
- [templates/register.md](templates/register.md): a hardware register (Equates, Module, Access, Bits, Reset,
  Status; Notes optional).
- [templates/utility.md](templates/utility.md): a built-in command or utility (Forms, Options, Output, Errors,
  Since, Status, CP/M 3; Examples optional).

**To add an entry:**

1. Copy the template's block into the right chapter, in order (functions by number, registers by address).
2. Fill in every heading. Take the facts from the code and its tests, and the reasons from hw/.
3. Take its anchor off [tools/unwritten.txt](tools/unwritten.txt).
4. If it shows code, put the program in [examples/](examples/) with its expected output (below), and show it
   with a listing marker.
5. `make -C docs index`, then `make -C docs check`.

## Indexes

[index/functions.json](index/functions.json) and [index/registers.json](index/registers.json) list every BDOS
function number and every I/O register equate, with its anchor, file and status once its entry is written
(`"documented": false` until then). An agent can find what it needs there without reading the books.

They are **generated**, by `make -C docs index` (`check_docs.py --write-index`), from the books' entries and the
two includes, and committed. One source of truth: a function's name, its number and its anchor are said once, in
the entry and oodos.inc, and the index can't drift from them, because `make check` fails when the committed index
isn't what the entries generate. Kept by hand, an index would be a third copy to fall behind.

## Examples

[examples/](examples/) holds every program a book shows. Each `NAME.s` is an OODOS `.COM`, built by
[examples/Makefile](examples/Makefile) through oodos's own rules (`oodos.inc`, `cfg/com.cfg`, the 65C816 rules
gate). `NAME` is 8 characters at most, as it goes on `M:` as `NAME.COM`, and isn't a utility's, which
oodos/Makefile would build instead. `NAME.out` beside it is what it prints when typed at the CCP.
`test_docs_examples`, in the emulator's native tests, puts each one on `M:`, runs it from the prompt and compares
what it prints with its `.out`.

A book shows a file with a listing marker, then a fenced block:

````
<!-- listing: ../examples/writestr.s -->
```
...the file, exactly...
```
````

`<!-- listing: path:10-20 -->` shows lines 10-20 only. The check fails a listing that differs from its file, a
listed example without a `.out`, a marker that isn't one of these two, and a code block under an entry's
`#### Example` with no marker before it, so a book can't show code that isn't tested.

## Checking the books

```sh
make -C docs check      # the check, then its own negative cases
make -C docs coverage   # what is written, and what isn't yet
make -C docs index      # regenerate index/*.json
```

[tools/check_docs.py](tools/check_docs.py) (Python 3's standard library, run as `python3 -I -B`) checks:

- every function number in `oodos/include/oodos.inc` has an entry, with oodos.inc's name, and every entry's
  number is there;
- every I/O register equate in `backplane/bios/backplane.inc` (an address in `$C000-$C7FF`) has an entry, and
  every register entry's address and equates are backplane.inc's, and its Module the right module's ID and name;
- every utility (`oodos/utils/NAME.s`), every built-in command (`BINAMES` in `oodos/sys/ccp.s`, by its long
  name) and every command planned with no source yet (`PLANNED` in check_docs.py: PIP, SUBMIT, GET, PUT) has an
  entry, and every command's entry is one of them;
- a line among oodos.inc's functions or in `BINAMES` that the check can't read fails it, so nothing is passed
  over;
- entries follow their template's headings, in order, with a known status;
- every anchor the indexes name exists, no anchor is there twice, and every relative link in docs/ and AGENTS.md,
  and every one in hw/*.md and README.md into docs/, resolves, to its file and its anchor (reference-style links
  aren't read);
- listings are their files, listed examples are tested, and an entry's Example, headings under it too, is a
  listing; an example's name is 8 characters at most and isn't a utility's;
- every Technical Reference section has a status tag;
- the indexes are what the entries generate.

It prints how many function numbers, equates, utilities, built-ins, files, entries, links and listings it
examined, and fails if it found no function numbers, equates, utilities, built-ins, Markdown files, links,
listings, entries or Technical Reference sections: a check that examined nothing hasn't passed.

**What isn't written yet** is listed in [tools/unwritten.txt](tools/unwritten.txt), one anchor a line. A missing
entry fails the check unless it's listed there; a listed one that has been written, or that names nothing in the
sources, fails it too; for a command, the sources are the utilities, the built-ins and `PLANNED`. So the list can
only shrink: G1 empties its functions and utilities, G2 its registers. An equate, function, utility or built-in
added to the sources fails the check until it gets an entry or a line on the list, which is the point: the books
can't fall behind the code without someone saying so. `make coverage` lists both sides.

[tools/test_check_docs.py](tools/test_check_docs.py) is the check's own test: it copies the tree, breaks the copy
one way at a time (a function or an equate with no entry, an entry for one that doesn't exist, a wrong heading, a
stale index, a broken link, a listing that isn't its file, and so on) and expects each to fail with its message.
`make check` runs both. In the emulator's native tests they are `test_docs` and `test_docs_check`, beside
`test_docs_examples` (`emulator/test/host/README.md`), so `ctest` catches the books drifting from the code.

# 2 The command line

Typing and editing a command, several on a line, and how the CCP finds what to run.

The design record, for the reasons: [hw/oodos.md, "The CCP, phase 4"](../../hw/oodos.md#the-ccp-phase-4).

## Typing a command

The CCP is the part of OODOS that shows the prompt and runs what is typed. A command is a keyword, then, after a
blank, its tail: what the command works on.

```
A>TYPE README.TXT
```

- **Return ends the line,** wherever the cursor is in it. A line holds up to 255 characters; one typed past that
  rings the bell and is dropped.
- **Letters are taken as capitals,** the tail's too: `type readme.txt` is `TYPE README.TXT`, and a program gets
  its tail in capitals.
- **Blanks before the keyword** don't matter, and blanks and tabs at the end of the line are dropped.
- **An empty line,** or one of blanks, gives the prompt again.
- **A tail** may be up to 126 characters. A longer one runs nothing and says `Command tail too long`.

A command that writes nothing has the next prompt on the next line:

```
A>B:
B>
```

## Editing the line

The cursor can go anywhere in the line, and what is typed goes in at the cursor. The keys:

| Key | What it does |
|---|---|
| DEL, ^H | the character before the cursor taken out |
| ^G | the character at the cursor taken out |
| ^A, ^F | the cursor one character left, one right |
| ^B | the cursor to the start of the line; at the start, to its end |
| ^K | everything from the cursor to the end taken out |
| ^X | everything before the cursor taken out |
| ^E | a new row on the screen; the line goes on there, from the cursor (the line itself is unchanged) |
| ^R | the line typed again on a new row |
| ^U | what is before the cursor kept as the previous line, and the line emptied, on a new row |
| ^W | in an empty line, the previous line back; else the cursor to the end |
| ^P | the printer echo turned on or off (below) |
| Return, ^J | the line ended |
| ^C | in an empty line: the warm boot (below); else a character, shown `^C` |

Each line ended with Return becomes the previous line, for ^W. These are the banked CP/M 3's keys, with two
differences: ^R and ^U leave no mark on the row they leave, and ^C is the warm boot only in an empty line, not in
one the cursor has merely been moved to the start of. Any other control character goes into the line as
it is and is shown as `^` and its letter: a tab is shown `^I`.

The line is shown on rows as wide as the console (80 columns), never writing the last column. A line longer than a
row goes on to the next; moving or deleting back past a row's start types the line again on a new row.

## Several commands on a line

**`!` separates commands:** `DIR!TYPE README.TXT` runs DIR, then TYPE. Each command after the first is shown after
a new prompt, as it was typed, then run:

```
A>DIR *.TXT!TYPE ONE.TXT
A: ONE      TXT : TWO      TXT
A>TYPE ONE.TXT
ONE

A>
```

An empty command between two `!`s is skipped. A program or a built-in stopped by ^C drops the rest of the line.

**`:` first runs a command only if the one before it succeeded.** A command fails when it says an error (`No File`,
a BDOS error, `NAME?`) or a program says it failed; a command skipped this way leaves things as they were, so in
`NOSUCH!:HELLO!:DIR` both HELLO and DIR are skipped. `:` works on the first command of a line too, after the one
before it.

**`;` first makes the command a comment:** it does nothing.

## Built-in commands and programs

The keyword runs to the first blank or tab. The CCP takes it as one of these, in order:

1. **A built-in command,** by its name alone, with no drive and no type: `DIR`, `DIRSYS` (`DIRS`), `ERASE` (`ERA`),
   `RENAME` (`REN`), `TYPE` (`TYP`), `USER` (`USE`), as in CP/M 3, and ours, `GO`, `CLEAR`, `CLS`, `WOZMON`,
   `APPLE2`. The CCP runs them itself ([chapter 4](04-built-in-commands.md)), from any drive and user. DIR, DIRSYS,
   ERASE, RENAME and TYPE hand the forms they can't do to their utility, `DIR.COM` and the rest.
2. **A drive change:** `B:`, or a user number and a drive, `5B:` ([chapter 3](03-drives-and-files.md#drives)).
   Nothing may follow it.
3. **A program:** `{d:}NAME{.COM}{;password}`, the file `NAME.COM`. A password is needed only for a protected
   program ([chapter 3](03-drives-and-files.md#passwords)).

A built-in's name with a drive runs the program of that name: `A:DIR` runs `DIR.COM` from A:, so a program with a
built-in's name, such as the `CLS.COM` many CP/M disks have, can still be run.

The CCP runs `.COM` files only. `NAME.SUB` says `NAME.SUB: no SUBMIT yet`: SUBMIT comes with OODOS phase 5
([hw/oodos.md, "Phases"](../../hw/oodos.md#phases)). Any other type, `.PRL` among them, and a keyword with `?` or
`*` or that isn't a file name, is the keyword and `?`: `HELLO.PRL?`.

## Where programs are found

**With a drive** (`B:HELLO`), the program is looked for on that drive only.

**Without one,** it is looked for on each drive of the drive search chain in turn. The chain is set by
[SETDEF](05-utilities.md#setdef); OODOS starts with the current drive alone in it. So the utilities on A: are found
from A:, but from B: they need `A:` before them (`A:SHOW`) unless SETDEF has put A: in the chain (`SETDEF *,A:`). A
built-in that hands its form to a utility looks for it the same way, and says `DIR.COM required` if it isn't found.

**On each drive,** the program is looked for among the current user's files, then among user 0's files that have
the SYS attribute. The utilities on the card are kept so, in user 0 as SYS files, so they are found from any user,
and DIR doesn't list them.

Not found anywhere, the CCP says the keyword and `?`:

```
A>NOSUCH
NOSUCH?
```

With SETDEF's `[DISPLAY]` on, the CCP says where it found a program before running it: `A:HELLO    COM`, and
` (User 0)` after it when it is user 0's SYS file run from another user.

## Control characters

Besides the line's editing keys, these work while a command writes:

- **^S** stops the output; **^Q** starts it again.
- **^C**, while the output is stopped by ^S, ends the program and goes back to the prompt. DIR, TYPE and the
  utilities that write a lot stop at ^C whenever it is typed, and at a page's stop.
- **^P** turns the printer echo on or off, at the prompt or while the output is stopped by ^S (anywhere else it
  is dropped): while it is on, what goes to the console goes to the printer (`LST:`) as
  well. The OO-1 has no printer yet, so `LST:` takes what it is sent and drops it.
- **^C at the prompt,** with the line empty, resets the drives: each is read again the next time it is used, as
  after changing the card's images. It is also how a read-only drive becomes read-write again.

**Paging.** DIR, TYPE and the utilities that write a lot stop when the screen is full (24 lines), with
`Press a key for more`; any key goes on, ^C stops. SETDEF's `[NO PAGE]` turns this off.

Any other key typed while a command writes is kept, one, for the next thing that reads the keyboard.

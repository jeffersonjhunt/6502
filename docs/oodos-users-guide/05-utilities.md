# 5 Utilities

The transient utilities, programs on A: (phase 4b): SET, SHOW, INITDIR, DATE, DEVICE, SETDEF, then PIP (4b2);
SUBMIT, GET and PUT come with the RSXs in phase 5. DIR's, ERASE's, RENAME's and TYPE's full forms are with the
built-ins, in [chapter 4](04-built-in-commands.md).

The design record, for the reasons: [hw/oodos.md, "Phases"](../../hw/oodos.md#phases).

## SET

Written in G1.

## SHOW

Written in G1.

## INITDIR

Written in G1.

## DATE

Written in G1.

## DEVICE

Written in G1.

## SETDEF

Written in G1.

## PIP

<a id="util-pip"></a>
### PIP: copy, join and send files

PIP copies files between drives and user numbers, renames them on the way, joins several into one, and sends files
or typed text to and from the devices.

#### Forms

```
PIP
PIP d:{[Gn]}=filespec{[options]}
PIP dest-filespec{[Gn]}=d:{[options]}
PIP dest-filespec{[Gn]}=filespec{[options]}{,filespec{[options]}...}
PIP device=source{[options]}{,source{[options]}...}
PIP dest-filespec{[Gn]}=device{[options]}
```

- **A drive as the destination** (`B:`): each source file keeps its name. A wildcard source copies every match and
  lists each under `COPYING -`. A drive as the source (`B:=A:`) copies all its files.
- **A file as the destination:** the sources are joined in order. A drive as the only source takes the
  destination's name (`X.TXT=B:`).
- **A file's password** goes after a `;`. Given on a source, it opens the file; given on the destination, the copy
  gets it in READ mode.
- **The devices:** `CON:` (the console), `AUX:` (the serial line), `LST:` (the printer), `PRN:` (the printer, with
  tabs every eighth column, line numbers and a new page every 60 lines), and two sources: `NUL:` (64 zero bytes)
  and `EOF:` (one ^Z). `DEVICE` says where `CON:`, `AUX:` and `LST:` go.
- **PIP alone** prints `OODOS PIP 0.2` and asks with `*` for one command a line; an empty line ends it.

#### Options

In square brackets after a source (the destination takes `[Gn]` alone); letters together or with blanks between, a
number straight after its letter:

- **A** only files not backed up since they last changed (archive bit clear); each is marked backed up after.
- **C** asks `COPY NAME.TYP (Y/N)?` before each file copied to a drive.
- **Dn** cuts each line after column n.
- **E** shows what is written on the console as it goes.
- **F** drops form feeds.
- **Gn** the source's user number (after the destination: the copy's), 0 to 15.
- **H** checks each line as an Intel hex record; **I** does too, and drops `:00` records (for joining hex files).
- **K** leaves out the `COPYING -` list.
- **L** and **U** make letters lower or upper case.
- **N** numbers each line (`     1: `); **N2** with zeros and a tab (`000001`).
- **O** an object file: no ^Z ends it.
- **Pn** a form feed at the start and every n lines (P alone: 60).
- **Qs^Z** stops after the text s; **Ss^Z** starts at the text s (both are kept).
- **R** copies SYS files too.
- **Tn** expands tabs to every nth column.
- **V** reads the copy back and checks it.
- **W** writes over a read-only file without asking.
- **Z** clears bit 7 of each character.

Any option but A, C, G, K, O, R, V and W, or a device, makes PIP copy by characters, to each source's first ^Z.

#### Output

Nothing for a single copy. For a wildcard, `COPYING -` and a line a file:

```
A>PIP B:=A:*.TXT
COPYING -
ONE.TXT
TWO.TXT
```

#### Errors

- `INVALID FORMAT`: a form PIP doesn't take, a wildcard where one name is needed, or a copy onto itself.
- `INVALID SEPARATOR`: something after a name that isn't `,`, `=` or an option list.
- `BAD PARAMETER`: an option PIP doesn't know, or one without its number or ^Z; any destination option but G.
- `INVALID USER NUMBER`: G over 15.
- `INVALID DESTINATION`, `INVALID SOURCE`: a device on the wrong side, or a name with `:` that isn't a device.
- `FILE NOT FOUND - NAME.TYP`; `R/O DISK`; `CAN'T DELETE TEMP FILE` (a read-only `NAME.$$$` in the way).
- `DESTINATION IS R/O, DELETE (Y/N)?`: N is `**NOT DELETED**`.
- `DISK WRITE ERROR - NAME.TYP`: the disk, or its directory, is full. `VERIFY ERROR - NAME.TYP`: V found a difference.
- `START NOT FOUND`, `QUIT NOT FOUND`: S's or Q's text wasn't in the source.
- `INVALID DIGIT`, `CHECKSUM ERROR`, `RECORD TOO LONG`, `UNEXPECTED END OF HEX FILE`, each with the file's name: H.
- `USER ABORTED`: ^C during a copy.
- A wrong password, and other disk errors, as OODOS reports them. Copying to a drive, PIP skips a file it can't
  open and goes on with the next.

A copy that fails leaves nothing behind: PIP writes to `NAME.$$$` and renames it only when it is complete.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

- The banner is OODOS's.
- H's error asks for no correction: the copy stops.
- A wildcard destination other than `*.*` is refused, and so is a wildcard source joined to a file.
- The text after S and Q is in capitals when typed on the `A>` line, as every command line is. At PIP's `*` it
  stays as typed.

#### Examples

```
A>PIP
OODOS PIP 0.2
*B:=A:*.BAS
COPYING -
GAME.BAS
*LETTER.TXT=CON:
Dear friend,^Z
*
A>
```

## SUBMIT

Written with OODOS phase 5.

## GET

Written with OODOS phase 5.

## PUT

Written with OODOS phase 5.

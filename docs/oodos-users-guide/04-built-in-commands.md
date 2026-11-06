# 4 Built-in commands

The commands the CCP runs itself: CP/M 3's DIR, DIRSYS, ERASE, RENAME, TYPE and USER, and our GO, CLEAR, CLS,
WOZMON and APPLE2. A built-in and the utility of the same name share one entry here, with the forms the utility adds.

The design record, for the reasons: [hw/oodos.md, "The CCP, phase 4"](../../hw/oodos.md#the-ccp-phase-4).

**Built-ins and their utilities.** A built-in runs from any drive and user, as it is part of the CCP. DIR, ERASE,
RENAME and TYPE do their short forms themselves and hand the rest (an option list, a wildcard where the built-in
takes none, several names, no name) to the utility of the same name on the card: `DIR.COM`, `ERASE.COM`,
`RENAME.COM`, `TYPE.COM`. DIRSYS hands its rest to `DIR.COM`, with SYS added. The utility is found as any program is
([chapter 2](02-command-line.md#where-programs-are-found)); if it isn't, the built-in says `DIR.COM required` (or
`ERASE.COM`, `RENAME.COM`, `TYPE.COM`). A built-in's name with a drive, `A:DIR`, runs the utility itself.

**What all of them share:**

- A form a built-in doesn't take (more names than it takes, a name that isn't one, something after a drive change)
  is the command as typed, in capitals, and `?`: `TYPE M:ONE.TXT TWO.TXT?`.
- An error the BDOS finds (a drive that isn't there, a read-only file, a wrong password) is said by the BDOS, as
  `OODOS Error On A: File R/O` and the function and file under it ([chapter 6](06-errors.md#the-bdoss-messages)).
- What writes a lot stops at each full screen with `Press a key for more`, and stops at ^C
  ([chapter 2](02-command-line.md#control-characters)).
- A command that failed fails a `:` command after it ([chapter 2](02-command-line.md#several-commands-on-a-line)).

**The utilities' options** go in square brackets, anywhere in the tail, separated by commas or blanks: `[FULL]`,
`[USER=ALL]`, `[DRIVE=(A,B)]`. An option may be shortened to any start of its name that is no other's: `[FU]` is
`[FULL]`, and `[F]`, which could be FF or FULL, is refused. `NO` and a blank before a name are one word: `[NO PAGE]`
is `[NOPAGE]`. One option list a command.

## DIR

<a id="util-dir"></a>
### DIR: list the files on a drive

DIR lists the current user's files, by name. The built-in lists them as the directory has them; DIR.COM sorts them,
and with options shows their sizes, attributes, protection and date stamps, on several drives and users at once.

#### Forms

```
DIR {d:}
DIR {filespec}
DIR filespec filespec {filespec...}
DIR {d:} [options]
DIR filespec {filespec...} [options]
```

- **The built-in** takes the first two forms: a drive, or one name (wildcards allowed). Neither is every file on
  the current drive. It lists files without the SYS attribute.
- **DIR.COM** takes the rest: several names, each with its drive, listed drive by drive in the order first given;
  or an option list. Without an option list its listing is the built-in's, sorted.

#### Options

DIR.COM's. With an option list the listing is FULL unless SIZE or DATE is given.

- **ATT** shows the user attributes F1-F4 too, in FULL.
- **DATE** each file's date stamps, with no other columns.
- **DIR** only files without the SYS attribute; **SYS** only those with it. Both, or neither, is both.
- **DRIVE=d**, **DRIVE=(d,d,...)**, **DRIVE=ALL** (or **DISK=**) the drives to list; ALL is every drive in use
  since the last reset, A: to P:. `*` is the current drive.
- **EXCLUDE** the files that match none of the names given.
- **FF** a form feed to the printer first, with the printer echo on (^P), and at every `LENGTH=` lines. Without
  `LENGTH=`, FF also turns the screen's paging off.
- **FULL** the full listing (below).
- **LENGTH=n** the column heading again every n lines, 5 to 65535.
- **MESSAGE** each drive and user searched named, with `No File` when nothing is found there.
- **NOPAGE** no stop at a full screen.
- **NOSORT** the files in the directory's order.
- **RO** only read-only files; **RW** only read-write ones. Both, or neither, is both.
- **SIZE** names and sizes only.
- **USER=n**, **USER=(n,n,...)**, **USER=ALL** the users to list, 0 to 15.

#### Output

**The short form** (the built-in's, and DIR.COM's without options): each line starts with the drive, and the
names follow, ` : ` between them, five to a line on an 80-column screen:

```
A>DIR M:
M: ONE      TXT : TWO      TXT : THREE    DAT : NOTYPE       : SIX      TXT
```

When files of the other kind match too (SYS files, for DIR), `SYSTEM FILE(S) EXIST` follows on its own line, or
alone if no file was listed.

**FULL**, for each drive and user with files, a heading, the files, then the totals:

```
Directory For Drive B:  User  0

    Name     Bytes   Recs   Attributes   Prot      Update          Create
------------ ------ ------ ------------ ------ --------------  --------------

README   TXT     4k      1 Dir RW       None   10/30/26 12:00  10/30/26 12:00

Total Bytes     =      4k  Total Records =       1  Files Found =    1
Total 1k Blocks =      1   Used/Max Dir Entries For Drive B:    2/ 768
```

- **Bytes** the space the file takes, in KB; **Recs** its 128-byte records.
- **Attributes** `Dir` or `Sys`, `RW` or `RO`, `A` when archived; with ATT, `1` to `4` for F1-F4 in their places
  (`Dir RO A1 3`).
- **Prot** the password mode: `Read`, `Write`, `Delete` or `None`.
- **Update** and **Create** the date stamps, `MM/DD/YY HH:MM`; the last column is `Access` when the drive keeps
  access stamps. Blank where there is no stamp; Prot and both stamps blank on a drive with no label.
- **Total 1k Blocks** the 1 KB blocks the records need; **Used/Max** the directory entries in use, of those for
  files.

**SIZE:** each line starts with the drive, then `NAME     TYP` and the size in KB, ` : ` between, as many to a
line as the console width over 21 (three at 80 columns), then the totals.

**DATE:** the name, and the update and the other stamp, under FULL's headings for them, then the totals.

Groups (a drive and a user) are a blank line apart. Under paging the column heading starts each page.

#### Errors

- `No File`: nothing matched.
- **DIR.COM's:** `Illegal Option or Modifier.` (an option it doesn't know, a prefix that could be two, a bad value, a
  value for an option that takes none); `Options not grouped together.` (a second option list); `Illegal command tail.`
  (a name that isn't one, a `,` with nothing after it, a `[` not closed, more than 64 names); `Illegal Format Value.`
  (FULL and SIZE, or DATE with either); `Illegal Global/Local Drive Spec Mixing.` (a drive in a name and DRIVE= both);
  `Not Enough Memory for Sort.` (a drive with more files than fit, about 1,190; nothing listed for it);
  `Date and Time Stamping Inactive` (DATE on a drive without date stamps).
- `DIR.COM required`: a form for DIR.COM, and DIR.COM not found.
- A drive that isn't there: the BDOS's `Invalid Drive`; DIR.COM goes on with the next drive.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

- The built-in lists in the directory's order; DIR.COM sorts, unless NOSORT, its short form too.
- The names a line are the console width / 15: five at 80 columns.
- DATE's layout is ours: the name and the two stamps.
- DRIVE=ALL is every drive logged in since the last reset, as CP/M 3's option table says; its guide's example
  speaks of the search chain instead, which OODOS doesn't follow.
- `Illegal Format Value.` for FULL with SIZE, or DATE with either.
- A drive and DIR (`A:DIR`) runs DIR.COM; `DIR.COM required` names the file.

#### Examples

```
A>DIR B:*.TXT
B: README   TXT : NOTES    TXT
A>DIR B:*.BAS
No File
A>DIR B: [SIZE]
Directory For Drive B:  User  0

B: NOTES    TXT    4k : README   TXT    4k

Total Bytes     =      8k  Total Records =       2  Files Found =    2
Total 1k Blocks =      2   Used/Max Dir Entries For Drive B:    3/ 768
```

## DIRSYS

<a id="util-dirsys"></a>
### DIRSYS: list the SYS files

DIRSYS, or DIRS, is DIR for the files that have the SYS attribute, which DIR leaves out.

#### Forms

```
DIRSYS {d:}
DIRSYS {filespec}
DIRSYS filespec filespec {filespec...}
DIRSYS {d:} [options]
DIRSYS filespec {filespec...} [options]
```

DIRS is the same command. The first two forms are the built-in's; the rest are handed to DIR.COM with `SYS` added
to the options (`[SYS,FULL]`, or `[SYS]` added to a tail with none).

#### Options

DIR.COM's ([DIR](#util-dir)), with SYS.

#### Output

As DIR's short form, for the SYS files. When files without SYS match too, `NON-SYSTEM FILE(S) EXIST` follows:

```
A>DIRSYS M:
M: SYSF     COM
NON-SYSTEM FILE(S) EXIST
```

Handed to DIR.COM, DIR.COM's listing of the SYS files: the form its options ask for (`DIRSYS M: [SIZE]` is SIZE's),
or FULL, DIR.COM's default with an option list, for several names and no options.

#### Errors

As DIR's: `No File`, and DIR.COM's when it is handed the command. `Command tail too long`: adding `SYS,` or
` [SYS]` would make the tail longer than 126 characters.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

CP/M 3's DIRSYS takes a drive or one name only. The forms with options or several names are ours: DIR.COM with
SYS added, so DIR.COM's options apply.

## ERASE

<a id="util-erase"></a>
### ERASE: delete files

ERASE, or ERA, deletes the current user's files that match a name. With a wildcard it asks first.

#### Forms

```
ERASE filespec
ERASE {filespec} [CONFIRM]
ERASE
```

- **The built-in** takes the first: one name, its password after it if it has one (`ERA SECRET.TXT;MYPASS`).
- **ERASE.COM** takes the rest: CONFIRM, and no name, when it asks `Enter filename: ` and reads one (an empty line
  ends it).

#### Options

- **CONFIRM** (or any start of it, `C`): each matching file asked about in turn, a single name too, and erased only
  on Y.

#### Output

Nothing for a single name. For a wildcard, a question first, the name as typed:

```
A>ERA B:*.TXT
ERASE B:*.TXT (Y/N)?
```

Y (or y) erases them; anything else erases nothing. With CONFIRM, a question a file, `ERASE M:ONE.TXT (Y/N)? `.

#### Errors

- `No File`: nothing matched.
- `Media Change`: on O: or P:, the floppy was changed within a second of the drive's last use; nothing
  erased.
- A read-only file, a read-only drive, a wrong password: the BDOS's (`File R/O`, `Read Only`, `Password Error`).
- **ERASE.COM's:** `Drive Read Only` (the drive is read-only: nothing more erased); `Missing delimiter or
  Unrecognized Option` (an option but CONFIRM, a second option list, a second name); `Invalid file specification.`
  (a name that isn't one); `Not Enough Memory` (with CONFIRM, a wildcard matching more names than fit).
- `ERASE.COM required`: CONFIRM, or no name, and ERASE.COM not found.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

- The question shows the name as typed, without its password.
- `Enter filename: ` with no name, and CONFIRM's question naming the file with its drive, are ours.

## RENAME

<a id="util-rename"></a>
### RENAME: give a file a new name

RENAME, or REN, gives a file a new name, on the same drive and user. The new name comes first.

#### Forms

```
RENAME new-filespec=old-filespec
RENAME
```

- **The built-in** renames one file: no wildcards. The drive is the one given with either name; given with both,
  it must be the same. The old name's password goes after it, `REN NEW.TXT=OLD.TXT;MYPASS`.
- **RENAME.COM** takes wildcards, and no tail, when it asks `Enter New Name: ` and `Enter Old Name: ` (an empty
  line ends it). With wildcards, the new name's `?`s must be where the old name's are (`*.BAK=*.TXT`); each matching
  file takes the new name with the old one's characters in those places.

#### Options

None.

#### Output

Nothing, for one file. When the new name is there already, a question:

```
A>REN NOTES.TXT=DRAFT.TXT
Not renamed: NOTES.TXT file already exists, delete (Y/N)?
```

Y deletes the old `NOTES.TXT` and renames; anything else renames nothing. RENAME.COM, for a wildcard or with no
tail, shows each file renamed on a line, `NOTES.BAK=NOTES.TXT`.

#### Errors

- `No File`: no file of the old name.
- `Media Change`: on O: or P:, the floppy was changed within a second of the drive's last use; nothing
  renamed.
- The new and the old name the same file: the command and `?` (RENAME.COM: `Incorrect file specification.`).
- A read-only file or drive, a wrong password: the BDOS's.
- **RENAME.COM's:** `Incorrect file specification.` (not the form `new=old`, a name that isn't one, two drives, an
  option list); `Invalid wildcard.` (the `?`s not in the same places); `Drive Read Only` (nothing renamed);
  `Not renamed, filespec read only.` (an old file that is read-only; RENAME goes on with the next); `Not Enough
  Memory`.
- `RENAME.COM required`: a wildcard, or no tail, and RENAME.COM not found.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

- Renaming a file onto itself is refused before anything is looked for, as a Y would delete the only copy.
- A wildcard rename's lines show the new name first, as the command is typed.

## TYPE

<a id="util-type"></a>
### TYPE: show a file

TYPE, or TYP, writes a file to the console, to its first ^Z or its end.

#### Forms

```
TYPE filespec
TYPE filespec [PAGE]
TYPE filespec [NO PAGE]
TYPE
```

- **The built-in** shows one file, its password after it if it has one.
- **TYPE.COM** takes the rest: a wildcard (each matching file in turn, its name first on a line of its own,
  `M:ONE.TXT`), the options, and no name, when it asks `Enter filename: ` (an empty line ends it).

#### Options

- **PAGE** stops at each full screen, whatever SETDEF says; **NOPAGE** never stops. Neither: as SETDEF says.

#### Output

The file's bytes as they are, tabs to every eighth column:

```
A>TYPE ONE.TXT
ONE

A>
```

A file that isn't text shows what it shows: a `.COM` file shows `OOD`, and stops at the ^Z after it.

#### Errors

- `No File`: no such file.
- `Media Change`: on O: or P:, the floppy was changed while TYPE waited at a page's end; the rest isn't
  shown.
- A wrong password, a drive that isn't there: the BDOS's.
- **TYPE.COM's:** `Try 'PAGE' or 'NO PAGE'` (any other option, a second list); `Invalid drive.` (a letter past P
  before a `:`); `Invalid file specification.` (a name that isn't one, a second name); `Not Enough Memory`.
- `TYPE.COM required`: a wildcard, an option, or no name, and TYPE.COM not found.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

- Bytes go out as they are, a binary file's too.
- TYPE.COM takes wildcards; PAGE and NOPAGE override SETDEF's paging.

## USER

<a id="util-user"></a>
### USER: change the user number

USER, or USE, makes another user number current.

#### Forms

```
USER n
USER
```

#### Options

None.

#### Output

Nothing; the prompt shows the new user. Without a number it asks:

```
A>USER
Enter User #: 5
5A>
```

#### Errors

- Anything but a number from 0 to 15: the command and `?`, as `USER 16?`. A wrong answer, or none, to
  `Enter User #: ` is `USER?` alone.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

Anything but a number from 0 to 15 is the command and `?`. A user given with a drive change, `5B:`, is as in
CP/M 3.

## GO

<a id="util-go"></a>
### GO: run the program in memory

GO runs the program already in memory at `$0300`, whose header is at `$0280`, with the tail typed after it. It is
ours, kept from OODOS's first CCP for the tests; a program loaded by the CCP doesn't leave its header there, so
from the prompt GO says `NO PROGRAM`.

#### Forms

```
GO {tail}
```

#### Options

None.

#### Output

What the program writes.

#### Errors

- `NO PROGRAM`: no OODOS program header at `$0280`.
- `BAD PROGRAM HEADER`: a header OODOS can't run (a `.PRL`, the wrong address).
- `Insufficient Memory`: the image and its BSS don't fit below the TPA's top.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

Ours: CP/M 3 has no GO. A `GO.COM` runs only with a drive (`A:GO`).

## CLEAR

<a id="util-clear"></a>
### CLEAR: clear the screen

CLEAR clears the screen, and a terminal on the serial line too.

#### Forms

```
CLEAR
```

#### Options

None; a tail is ignored.

#### Output

The screen cleared, then the prompt.

#### Errors

None.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

Ours: CP/M 3 has no CLEAR. A program of the name runs only with a drive (`A:CLEAR`).

## CLS

<a id="util-cls"></a>
### CLS: clear the screen and list the files

CLS clears the screen, then is DIR with the same tail.

#### Forms

```
CLS {d:}
CLS {filespec}
CLS filespec filespec {filespec...}
CLS {d:} [options]
CLS filespec {filespec...} [options]
```

#### Options

DIR.COM's ([DIR](#util-dir)).

#### Output

The screen cleared, then DIR's listing: `CLS [SYS]`, `CLS *.COM [SIZE]`.

#### Errors

DIR's.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

Ours: CP/M 3 has no CLS. A `CLS.COM`, as many CP/M disks have, runs only with a drive (`A:CLS`).

## WOZMON

<a id="util-wozmon"></a>
### WOZMON: leave OODOS for the monitor

WOZMON clears the screen and leaves OODOS for the ROM's Woz Monitor, as the Break key does. It doesn't come back:
from the monitor, `C80CR` (or a reset) starts OODOS again. Changed data is written to the card first, but a file a
program left open isn't closed.

#### Forms

```
WOZMON
```

#### Options

None; a tail is ignored.

#### Output

The monitor's prompt.

#### Errors

None.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

Ours: CP/M 3 has no such command. A program of the name runs only with a drive before it.

## APPLE2

<a id="util-apple2"></a>
### APPLE2: restart in Apple II mode

APPLE2 restarts the machine in Apple II mode, as a IIe starts at power-on, if the cartridge holds a IIe ROM. It
doesn't come back; only a reset ends Apple II mode. Changed data is written to the card first, but a file a program
left open isn't closed.

#### Forms

```
APPLE2
```

#### Options

None; a tail is ignored.

#### Output

The Apple IIe's start.

#### Errors

- `No IIe ROM in the cartridge (the emulator: Settings, ROMs)`: OODOS goes on.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

Ours: CP/M 3 has no such command. A program of the name runs only with a drive before it.

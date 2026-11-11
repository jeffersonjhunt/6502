# 5 Utilities

The transient utilities, programs on A: (phase 4b): SET, SHOW, INITDIR, DATE, DEVICE, SETDEF, then PIP (4b2);
GENCOM, GET and PUT (phase 5b), which work with RSXs; SUBMIT (phase 5c); DOS33 (phase 6d), for Apple disks; PLAYSID (OOLIB's L3), for C64 tunes; VIEW (L4), for pictures. DIR's, ERASE's, RENAME's and TYPE's full forms are with the
built-ins, in [chapter 4](04-built-in-commands.md).

The design record, for the reasons: [hw/oodos.md, "Phases"](../../hw/oodos.md#phases).

## SET

<a id="util-set"></a>
### SET: set a file's attributes and protection, a drive's mode and its label

SET changes what a file is (its attributes, its password and how the password protects it), makes a drive
read-only or read-write, names a drive's label and sets what the label turns on, and sets the default password.

#### Forms

```
SET filespec{;password} [options]
SET {d:} [options]
SET [DEFAULT=password]
```

- **A file specification** takes the file options. A wildcard sets every matching file of the current user, in
  the directory's order. A file that has a password is given it after a `;`; with a wildcard, the one password is
  tried on every file.
- **A drive, or none,** takes the drive options, for that drive or the current one.
- The option list is one list in brackets, the options separated by commas or blanks, each name shortened to
  any prefix that is not ambiguous (`[R]` could be RO or RW, and is refused).

#### Options

For a file:

- **DIR** and **SYS:** the file is listed by DIR, or is a system file, which DIRSYS lists and the CCP finds from
  any user if it is in user 0.
- **RO** and **RW:** read-only or read-write.
- **ARCHIVE=ON** or **ARCHIVE=OFF:** the archive attribute, which marks a file as backed up since it last
  changed (PIP's A option copies the files without it, and sets it).
- **F1=ON** to **F4=ON** (or `=OFF`): the four attributes left to programs. F5 to F8 are cleared.
- **PASSWORD=pw:** a new password, 1 to 8 characters. `PASSWORD=` with nothing after it doesn't remove a file's:
  it is `Assign a Password to this file.` (`PROTECT=NONE` removes it).
- **PROTECT=READ**, **WRITE**, **DELETE** or **NONE:** what the password guards, on a drive whose label has protection
  on. READ: the file can't be opened without it; WRITE: it opens, but read-only; DELETE: it opens as usual. Whatever the
  mode, deleting it, renaming it or changing its attributes needs the password. NONE: nothing, and the password is
  deleted unless `PASSWORD=` gives a new one in the same list. A file given a password with no mode gets READ if it has
  never had a password, and keeps its mode otherwise (a file set to NONE stays NONE).

For a drive:

- **RO:** the drive is read-only until it is reset (^C at the prompt, or `SET [RW]`).
- **RW:** the drive is reset, and read-write again unless its card unit is write-protected.
- **NAME=name.typ:** the label's name. A drive with no label gets one, named `LABEL` if NAME isn't given.
- **PASSWORD=pw** and **PASSWORD=:** the label's password, or none.
- **PROTECT=ON** or **PROTECT=OFF:** whether the drive's file passwords are enforced. Off, the drive's files
  are open to anyone.
- **CREATE=ON|OFF**, **ACCESS=ON|OFF**, **UPDATE=ON|OFF:** which date stamps the drive keeps: when a file is
  made, or when it was last read, and when it last changed. CREATE and ACCESS share one place, so turning one
  on turns the other off. The drive needs room for stamps in its directory first: see [INITDIR](#util-initdir).

And with neither:

- **DEFAULT=pw:** the default password, tried for any file that is given no password or a wrong one. Nothing is
  written to a disk; it lasts until it is changed or OODOS starts again.

Everything is checked before anything changes. A drive's options are done in the order DEFAULT, RW, the label's
options in the order given, then RO; a file's attributes before its password and mode.

When a drive's label has a password, SET asks for it, once, before it changes the label:

```
Password: 
```

#### Output

A line for each file or each thing changed, nothing for DEFAULT:

```
M:ONE.TXT set to System (SYS), Read Only (RO)
M:ONE.TXT set to Archive=ON, F1=ON, F2=OFF
M:ONE.TXT Protection = Write
M: set to Read Only (RO)
M: set to Read Write (RW)
M: Label = WORK.DAT
M: Label Password set
M: Label Password removed
M: Password Protection = ON
M: Create = ON
M: Access = OFF
M: Update = ON
```

A file can get two lines: one for its attributes and one for its protection. The attributes are named in the order DIR,
SYS, RW, RO, Archive, F1 to F4, whatever order they were typed in. The `Protection =` line says `Read`, `Write`,
`Delete` or `None`; the password itself is never shown.

#### Errors

- `No options specified`: no option list, or an empty one.
- `Invalid option`: an option SET doesn't know, a value it doesn't take, an ambiguous prefix, a password longer
  than 8 characters, a second option list, or a `[` not closed.
- `Invalid file specification.`: a name SET can't take, a second name, or `a=b`.
- `Cannot set both RO and RW.`, `Cannot set both SYS and DIR.`, `Cannot have both create and access time stamps`.
- `Option only for drives.`: a drive option, or DEFAULT, given with a file.
- `Option requires a file reference.`: a file option given without a file.
- `Cannot label a drive with a file referenced`: NAME given with a file.
- `File not found.`: nothing matches the specification.
- `Assign a Password to this file.`: a protection mode for a file that has no password and is given none, or
  `PASSWORD=` with nothing after it, for a file.
- `Password protection is off: SET [PROTECT=ON]`: a file's password or mode set on a drive whose label doesn't
  enforce passwords (or that has no label). Nothing is changed: turn protection on first.
- `Wrong Password`: the label's password, asked for, didn't match.
- `Directory needs to be reformatted for date/time stamps.`: CREATE, ACCESS or UPDATE turned on, on a drive without
  room for stamps. Run INITDIR on it first.
- `Drive Read Only`: a label option on a drive that is read-only, without RW in the same list.
- `Not Enough Memory`: too many files match a wildcard.
- `Directory full`: no free entry for a new label or for a file's password.
- Any other error (a drive that isn't there, a file's wrong password, a read-only file) as OODOS reports it
  ([chapter 6](06-errors.md#the-bdoss-messages)). With a wildcard, SET goes on with the next file.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

- The lines SET writes for each change are OODOS's own, and the password is never shown.
- A file given a password and no mode is protected for READ only if it has never had a password.
- An unknown option is `Invalid option`.
- SET refuses a file's password or mode while the drive's protection is off, rather than setting one that would
  not be enforced.

#### Examples

```
A>SET M: [PROTECT=ON]
M: Password Protection = ON
A>SET M:LETTER.TXT [PASSWORD=SECRET]
M:LETTER.TXT Protection = Read
A>SET M:*.BAS [RO, SYS]
M:GAME.BAS set to System (SYS), Read Only (RO)
M:TOOLS.BAS set to System (SYS), Read Only (RO)
A>SET M: [RW]
M: set to Read Write (RW)
```

## SHOW

<a id="util-show"></a>
### SHOW: show a drive's space, label, users and make-up

SHOW says how much room is left on a drive and whether it is read-only, shows its label, which user numbers have
files there, how many directory entries are free, and the numbers that describe the drive.

#### Forms

```
SHOW {d:}
SHOW {d:}[SPACE]
SHOW {d:}[LABEL]
SHOW {d:}[USERS]
SHOW {d:}[DIR]
SHOW {d:}[DRIVE]
```

One option, which may be shortened to any prefix that isn't ambiguous (`[L]`, `[DR]`; `[D]` could be DIR or
DRIVE, and is refused). Blanks may go between the drive and the list. Without a drive, the current one; but SPACE
without a drive shows every drive in use (logged in) since the last reset, A to P.

#### Options

- **SPACE** (or no option, or `[]`): the drive's access, `RW` or `RO`, and its free space in kilobytes.
- **LABEL:** the drive's label: its name, whether its passwords are enforced, which stamps it keeps, and when the
  label itself was made and last changed.
- **USERS:** the current user number, each user number that has files on the drive and how many (system files
  counted too), and the free directory entries.
- **DIR:** the free directory entries. Entries kept for date stamps are not counted as free.
- **DRIVE:** the drive's characteristics.

#### Output

SPACE, the number right-aligned in 9 with commas:

```
A: RW, Space:     7,952K
M: RW, Space:       120K
```

LABEL:

```
Label for drive M:

Directory     Passwds  Stamp   Stamp   Label Created   Label Updated
Label         Reqd     Create  Update
------------  -------  ------  ------  --------------  --------------
WORK.DAT      on       on      on      10/30/26 12:00  10/30/26 12:05
```

The third column's heading is `Access` instead of `Create` when the label asks for access stamps. A stamp that
was never made is blank.

USERS, each count right-aligned in 4 under its user number (here M: holds six files; an empty M: has 191 free
entries, as in the example below):

```
Active User :  0
Active Files:    0   3  12
M: # of files:   3   2   1
M: Number of free directory entries: 185
```

DIR (the same six files on M:):

```
M: Number of free directory entries: 185
```

DRIVE, each number right-aligned in 7 with commas (here M:, the memory drive):

```
    M: Drive Characteristics
  1,024: 128 Byte Record Capacity
    128: Kilobyte Drive Capacity
    256: 32 Byte Directory Entries
      0: Checked Directory Entries
    256: Records / Directory Entry
     16: Records / Block
     64: Sectors / Track
    512: Bytes / Physical Record
```

The record capacity and kilobytes are the drive's data area; the checked entries are 0 for a drive that can't be
changed under OODOS (all of OODOS's are so far); sectors a track count 128-byte records, and the physical record is
the drive's sector, 512 bytes on every OODOS drive, M: too.

#### Errors

- `Unrecognized drive.`: a drive letter past P.
- `Unrecognized option.`: an option SHOW doesn't know, an ambiguous prefix, a value (`[LABEL=X]`), or two options.
- `Unrecognized input.`: anything else on the line: a second drive, a name, a second list, or a `[` not closed.
- `No directory label exists.`: LABEL on a drive without one.
- A drive that isn't there (`H:`) as OODOS reports it: `OODOS Error On H: Invalid Drive`.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

- The numbers are laid out OODOS's way: free space with commas in a field of 9, USERS's counts in columns of 4,
  DRIVE's numbers with commas in 7.
- USERS counts system files too.
- A label with no type is shown as its name alone, without a dot.

#### Examples

```
A>SHOW
A: RW, Space:     7,952K
A>SHOW M:
M: RW, Space:       120K
A>SHOW M:[DIR]
M: Number of free directory entries: 191
```

## INITDIR

<a id="util-initdir"></a>
### INITDIR: make room in a directory for date stamps

A drive keeps date and time stamps only if its directory has room for them: every fourth entry set aside to hold
the stamps of the three before it. INITDIR sets that room aside, or, on a drive that has it, gives it back or
clears the stamps in it. Which stamps are kept is then the label's, set by [SET](#util-set) (`[CREATE=ON]`,
`[ACCESS=ON]`, `[UPDATE=ON]`).

#### Forms

```
INITDIR d:
```

The drive is required, alone.

#### Options

None. INITDIR asks what to do:

- **A drive without room for stamps:**

  ```
  INITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.
  Do you really want to re-format the directory: M (Y/N)? 
  ```

  `Y` sets every fourth entry aside. Files, passwords or a label already in those places are moved to free
  entries first, so nothing is lost. The stamps start empty. Anything but `Y` changes nothing.
- **A drive with it:**

  ```
  Directory already re-formatted.
  Do you wish to recover date/time directory space (Y/N)? 
  ```

  `Y` frees the entries again and turns the label's stamps off. Any other answer asks a second question:

  ```
  Do you want the existing date/time stamps cleared (Y/N)? 
  ```

  `Y` empties every stamp; the files' password modes kept there stay.

The drive is reset before and after, so OODOS reads its new directory the next time it is used.

#### Output

The questions above, and nothing when the work is done.

#### Errors

- `Invalid drive.`: no drive, a drive past P, or anything more on the line (a name, a second drive, an option).
- `Drive Read Only`: the drive is read-only. `SET d: [RW]` first, if it was made so by SET; a drive marked RO in
  `DRIVES.TXT`, or on a write-protected unit, stays read-only.
- `Not enough room in directory.`: more than three quarters of the directory is in use, so the entries in the
  places to set aside have nowhere to go. Nothing is changed: erase some files first.
- `Not Enough Memory`: too many entries to move for INITDIR to hold. Nothing is changed.
- `Disk Read Error`, `Disk Write Error`: the card failed to read or write a directory sector.
- `Directory partly re-formatted: run INITDIR again.`: a write failed after part of the work was done. No entry
  is lost (a moved one may be in two places); run INITDIR on the drive again, before anything else uses it, and
  it finishes the job.
- `Directory partly re-formatted.`: said by that next INITDIR, before its question, when it finds the work half
  done.
- A drive that isn't there as OODOS reports it: `OODOS Error On H: Invalid Drive`.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

- A missing or bad drive is `Invalid drive.`
- Giving the room back also turns the label's stamps off, so the label never asks for stamps the drive can't
  keep.
- Clearing the stamps keeps the password modes stored beside them.
- A label's password isn't asked for.
- An INITDIR stopped by a disk error is finished by the next one; the read and write errors and the "partly
  re-formatted" messages are OODOS's.

#### Examples

```
A>INITDIR B:
INITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.
Do you really want to re-format the directory: B (Y/N)? Y
A>SET B: [CREATE=ON, UPDATE=ON]
B: Create = ON
B: Update = ON
A>
```

## DATE

<a id="util-date"></a>
### DATE: show and set the date and time

DATE shows the date and time of the OO-1's clock, and sets the clock. OODOS stamps files from it.

#### Forms

```
DATE
DATE C
DATE MM/DD/YY HH:MM:SS
DATE SET
```

- **DATE** shows the date and time once.
- **DATE C** (`CONTINUOUS`, or any start of it: `C`, `CONT`) shows them again on the same line every second,
  until a key is pressed. The key is taken, not passed on to the next command.
- **DATE MM/DD/YY HH:MM:SS** sets the clock. Each number has one or two digits (`2/9/28 8:05:00`), with no blank inside
  the date or the time and one or more blanks or tabs between them, and the year is two digits, 20YY. DATE checks it all
  (a month of 1 to 12, a day that is in the month, 29 February only in a leap year, hours 0 to 23, minutes and seconds 0
  to 59), then asks for a key, so the clock can be set on the second:

  ```
  Press any key to set time
  ```

- **DATE SET** asks for each part:

  ```
  Enter today's date (MM/DD/YY): 
  Enter the time (HH:MM:SS): 
  ```

  An empty answer keeps that part as the clock has it. Then the key, as above.

#### Options

None.

#### Output

The day of the week, the date and the time to the second:

```
Sat 10/31/26 12:00:00
```

After setting the clock, DATE shows the time it set.

#### Errors

- `Illegal date/time specification.`: anything DATE doesn't take: a part missing or out of range, a four-digit
  year, the wrong separators, or more on the line. In `DATE SET`, the first bad answer ends it.
- `No clock`: the clock can't be read (it says a year past 2155, which OODOS can't keep).

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

- The two-digit year is 2000 to 2099, where CP/M 3 counts it from 1900: OODOS's clock starts at 2000.
- `CONTINUOUS` may be shortened to any start of it, not only to `C`, and the key that ends it is taken.
- `No clock` is OODOS's.

#### Examples

```
A>DATE 11/04/26 09:30:00
Press any key to set time
Wed 11/04/26 09:30:00
A>DATE
Wed 11/04/26 09:30:12
```

## DEVICE

<a id="util-device"></a>
### DEVICE: show and change where the character devices go

OODOS's console, auxiliary and list devices (CON:, AUX: and LST:) are logical: each goes to one or more of the
machine's physical devices, or to none. DEVICE shows the physical devices and where each logical one goes, changes
that, and sets the console's width and page length.

The physical devices:

- **CRT:** the screen and the USB keyboard, input and output.
- **TTY:** the serial line, input and output, at 19,200 baud.
- **LPT:** the printer, output only. There is no printer port yet: what is sent to it is dropped.

When OODOS starts, CON: is CRT and TTY both, AUX: is TTY and LST: is LPT.

#### Forms

```
DEVICE
DEVICE NAMES
DEVICE VALUES
DEVICE physical {[options]}
DEVICE logical
DEVICE logical = physical {[options]} {,physical {[options]}...}
DEVICE logical = NULL
DEVICE CONSOLE [PAGE]
DEVICE CONSOLE [COLUMNS=n, LINES=n]
```

- **DEVICE** shows the devices and the assignments, then asks for one change, a line taken as what would follow
  `DEVICE` (an empty line changes nothing):

  ```
  Enter new assignment or hit RETURN: 
  ```

- **NAMES** shows the physical devices, **VALUES** the assignments; a physical device's name (`DEVICE TTY`) shows
  its line, a logical one's (`DEVICE CON:`) its assignments.
- **The logical names,** each with or without its colon: `CONIN:` (the console's input), `CONOUT:` (its output),
  `AUXIN:`, `AUXOUT:`, `LST:`; `CON:` or `CONSOLE:` for both of the console's, `AUX:` or `AUXILIARY:` for both
  of the auxiliary's, `KEYBOARD` for CONIN:, `PRINTER` for LST:.
- **logical = physical, ...** sends it to those devices, separated by commas: output goes to each, input comes from
  any of them. **= NULL** to none. A device must be able to do what is asked of it: LPT can't be
  an input. Sending CONOUT: to LPT alone, or CONIN: to NULL, takes the console away until OODOS starts again.
- **CONSOLE [PAGE]** shows the console's width and page length; **[COLUMNS=n]** (1 to 256) and **[LINES=n]**
  (0 to 255) set them, then show them. The page length is where DIR, TYPE and the utilities stop with `Press a
  key for more`; under 2, they don't stop.

The keywords NAMES, VALUES and NULL, and the device names, are written in full. A change writes nothing (so a
blank line comes before the next prompt).

#### Options

For a physical device, in brackets after its name:

- **XON:** XON/XOFF flow control. No OODOS device has it: refused.
- **NOXON:** no flow control. Taken: so it is.
- **A baud rate** (`[9600]`): no OODOS device can have its rate set: refused.

An option may be shortened to any start that isn't ambiguous, and `NO XON` is `NOXON`.

For CONSOLE: **PAGE**, **COLUMNS=n**, **LINES=n**, above.

#### Output

DEVICE alone, at the start:

```
Physical Devices:
I=Input,O=Output,S=Serial,X=Xon-Xoff
CRT     NONE IO      TTY    19200 IOS     LPT     NONE O

Current Assignments:
CONIN:  = CRT TTY
CONOUT: = CRT TTY
AUXIN:  = TTY
AUXOUT: = TTY
LST:    = LPT

Enter new assignment or hit RETURN: 
```

Each physical device: its name, its baud rate (`NONE` for a device that isn't serial), and its letters. A logical
device sent nowhere is `Null Device` (`LST:    = Null Device`). CONSOLE:

```
Console width set to 80 columns
Console page set to 24 lines
```

#### Errors

- `Bad Logical Device Assignment`: a name before `=` that isn't a logical device (a physical one among them), or
  `=` with nothing before it.
- `Invalid Physical device.`: a name that isn't a device, a device that can't do what the assignment needs, NULL
  with other devices, or `=` with nothing after it.
- `Invalid delimiter.`: devices not separated by commas, or a device without `=`.
- `Invalid option.`: an option DEVICE doesn't know, an option list on a logical device but the console, or a `[`
  not closed.
- `Invalid number.`: a rate, a width or a length that isn't a number in range, or one left out.
- `Baud rate cannot be set for this device`, `Xon-Xoff cannot be set for this device`.
- `End of line expected`: more on the line after a complete command. The command is done, the rest ignored.

After any but the last, nothing is changed.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

- The devices are OODOS's three; none can have its rate or flow control set, so a rate and XON are refused.
- `DEVICE CONSOLE [PAGE]` shows its two lines, and COLUMNS and LINES take OODOS's ranges.
- A logical name may be typed without its colon. The device names, NAMES, VALUES and NULL can't be shortened.
- An assignment to a device that can't take it is refused (`Invalid Physical device.`).

#### Examples

```
A>DEVICE AUX:=CRT,TTY

A>DEVICE VALUES
Current Assignments:
CONIN:  = CRT TTY
CONOUT: = CRT TTY
AUXIN:  = CRT TTY
AUXOUT: = CRT TTY
LST:    = LPT
A>DEVICE CONSOLE [COLUMNS=40, LINES=16]
Console width set to 40 columns
Console page set to 16 lines
```

## SETDEF

<a id="util-setdef"></a>
### SETDEF: set where programs are looked for, and the CCP's modes

SETDEF sets the drives the CCP looks on for a program typed without a drive, the temporary drive, and whether the
console stops at each page and the CCP shows where a program came from. Then, and when typed alone, it shows them.

#### Forms

```
SETDEF
SETDEF d:{,d:{,d:{,d:}}} {[options]}
SETDEF [options]
```

- **The drives,** up to four, separated by commas, are the search chain, in order; `*` is the current drive,
  whichever it is at the time. When OODOS starts the chain is the current drive alone.
- The options go together in one list, in brackets, each name shortened to any start that isn't ambiguous.

#### Options

- **TEMPORARY=d:** the drive for temporary files (`*` the current drive): SUBMIT writes its expanded file there.
- **ORDER=(typ{,typ})**, or `ORDER=typ`: the file types the CCP looks for when a command has none, in order, COM
  and SUB: `(COM)`, the default, `(COM,SUB)`, `(SUB,COM)` or `(SUB)`. The first type is looked for on every drive of
  the search chain before the second. With `(SUB)` alone no `.COM` is run without its type typed:
  `SETDEF.COM [ORDER=(COM)]` puts it back.
- **DISPLAY** and **NODISPLAY** (or **NO DISPLAY**): whether the CCP shows the drive and name of each program it
  runs, before it runs it (`M:ARGS     COM`, with ` (User 0)` when it was found in user 0 from another user).
- **PAGE** and **NOPAGE** (or **NO PAGE**): whether DIR, TYPE and the utilities stop at each page.

An option given twice: the last wins.

#### Output

After `SETDEF *,M:`:

```
Drive Search Path:
1st Drive            - Default
2nd Drive            - M:

Search Order         - COM
Temporary Drive      - Default
Console Page Mode    - On
Program Name Display - Off
```

`Default` is the current drive (`*`). The search order reads `COM`, `COM, SUB`, `SUB, COM` or `SUB`.

#### Errors

- `More than four drives specified.`
- `Drive defined twice in search Path`: a drive, or `*`, given twice.
- `Invalid drive.`: a drive letter past P, a name that isn't a drive, a comma with nothing after it, or a list for
  TEMPORARY.
- `Invalid delimiter.`: drives not separated by commas, or anything after the option list.
- `Invalid option.`: an option SETDEF doesn't know, an ambiguous prefix, TEMPORARY or ORDER without a value, a
  value where none is taken, or a `[` not closed.
- `Invalid type for ORDER option.`: a type other than COM and SUB, a type twice, more than two, an empty ORDER, or
  a list with no `)`.

After any, nothing is changed.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

- SETDEF always shows the settings after a change, and all of them.
- The options go in one list.

#### Examples

```
A>SETDEF *,M: [NO PAGE]
Drive Search Path:
1st Drive            - Default
2nd Drive            - M:

Search Order         - COM
Temporary Drive      - Default
Console Page Mode    - Off
Program Name Display - Off
```

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
  gets it in READ mode. A copy to a drive alone gets the source's password and mode, when the source's was given.
- **The devices:** `CON:` (the console), `AUX:` (the serial line), `LST:` (the printer), `PRN:` (the printer, with
  tabs every eighth column, line numbers and a new page every 60 lines), and two sources: `NUL:` (64 zero bytes)
  and `EOF:` (one ^Z). `DEVICE` says where `CON:`, `AUX:` and `LST:` go.
- **PIP alone** prints `OODOS PIP 0.8` and asks with `*` for one command a line; an empty line ends it.

#### Options

In square brackets after a source (the destination takes `[Gn]` alone); letters together or with blanks between, a
number straight after its letter:

- **A** only files whose archive bit is clear, files changed since they were last backed up; each is marked backed
  up (the bit set) after. A large file has its bit in each part (each directory entry), and a write clears only the
  part's it wrote, so any part clear is enough. Only for a copy to a drive: a file-to-file copy of a file with the
  bit set says `FILE NOT FOUND`.
- **C** asks `COPY NAME.TYP (Y/N)?` before each file copied to a drive, and leaves out the `COPYING -` list.
- **Dn** cuts each line after column n.
- **E** shows what is written on the console as it goes.
- **F** drops form feeds.
- **Gn** the source's user number (after the destination: the copy's), 0 to 15.
- **H** checks each line as an Intel hex record; **I** does too, and drops `:00` records (for joining hex files).
- **K** leaves out the `COPYING -` list.
- **L** and **U** make letters lower or upper case.
- **N** numbers each line (`     1: `); **N2** with zeros and a tab (`000001`).
- **O** an object file: no ^Z ends it.
- **Pn** a form feed at the start and every n lines (P alone, P0 or P1: 60).
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
- `BAD PARAMETER`: an option PIP doesn't know, or one without its number or ^Z; a number over 255; N other than 1
  or 2; an empty S or Q text; a `[` with no `]`; any destination option but G.
- `INVALID USER NUMBER`: G over 15.
- `INVALID DESTINATION`, `INVALID SOURCE`: a device on the wrong side, a name with `:` that isn't a device, or a
  name that isn't a file name (too long, a character a name can't have).
- `FILE NOT FOUND - NAME.TYP`: no such file; for a wildcard that matched nothing, the name with its `?`s
  (`FILE NOT FOUND - ????????.TXT`).
- `R/O DISK`: the destination drive is read-only.
- `CAN'T DELETE TEMP FILE`: a read-only `NAME.$$$` in the way.
- `NO TEMP FILE NAME - NAME.TYP`: every name PIP could write the copy under first is taken (below).
- `Not Enough Memory`: the names a wildcard or a drive matches don't fit in memory.
- `DESTINATION IS R/O, DELETE (Y/N)?`: N is `**NOT DELETED**`.
- `DISK WRITE ERROR - NAME.TYP`: the disk, or its directory, is full. `VERIFY ERROR - NAME.TYP`: V found a difference.
- `CAN'T DELETE - NAME.TYP`, `CAN'T RENAME TEMP FILE - NAME.TYP`, `CAN'T SET ATTRIBUTES - NAME.TYP`: a step at the
  end of a copy failed without OODOS saying why: the old destination wasn't deleted, the finished copy couldn't be
  given its name (it is left under its temporary name, the one the message gives), or its attributes weren't set.
- `MEDIA CHANGE - NAME.TYP`: the floppy in O: or P: was changed during the copy, the destination's or (with a
  source's name) the source's. Nothing more is written, and nothing on the new floppy is touched; the
  destination's `NAME.$$$` is left on the changed floppy, or deleted when the destination's floppy didn't change.
- `START NOT FOUND`, `QUIT NOT FOUND`: S's or Q's text wasn't in the source.
- `INVALID DIGIT`, `CHECKSUM ERROR`, `RECORD TOO LONG`, `UNEXPECTED END OF HEX FILE`, each with the file's name: H.
- `USER ABORTED`: ^C during a copy.
- A wrong password, and other disk errors, as OODOS reports them. Copying to a drive, PIP skips a file it can't
  open and goes on with the next.

A copy that fails leaves nothing behind: PIP writes to `NAME.$$$` and renames it only when it is complete. The one
exception is a failed rename at the very end: the finished copy is kept under its temporary name, and the message
names that file.
A `NAME.$$$` already there is taken for one an earlier copy left, and deleted first. When `NAME.$$$` is the
destination itself (`PIP B:X.$$$=A:X.TXT`) or one of the sources on the destination's drive and user
(`PIP X.TXT=X.$$$`), PIP writes to `NAME.$$0` instead, or the next of `NAME.$$1` to `NAME.$$9` and `NAME.$$A` to
`NAME.$$Z` that isn't there; a file already under one of those names is left alone.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

- The banner is OODOS's.
- A file copied alone keeps its source's RO, SYS and F1-F4, as in CP/M 3, but its archive bit is clear, so it is
  backed up again; a joined file takes no attributes. With `[A]` the attributes are those of the first part of the
  file that isn't archived, which are the whole file's unless something set its parts' differently.
- H's error asks for no correction: the copy stops.
- `D0` and `T0` are refused, and so is an empty S or Q text.
- More than 16 sources are `INVALID FORMAT`.
- A wildcard destination other than `*.*` is refused, and so is a wildcard source joined to a file.
- The text after S and Q is in capitals when typed on the `A>` line, as every command line is. At PIP's `*` it
  stays as typed.

#### Examples

```
A>PIP
OODOS PIP 0.8
*B:=A:*.BAS
COPYING -
GAME.BAS
*LETTER.TXT=B:
*
A>
```

## GENCOM

<a id="util-gencom"></a>
### GENCOM: attach RSXs to a program

GENCOM puts resident system extensions (`.RSX` files) into a program's `.COM` file, so that loading the program
loads them too; takes them out again; adds SCB settings the program is to start with; and makes a `.COM` that holds
only RSXs. What an RSX is, and the file's layout: the Programmer's Guide,
[chapter 6](../oodos-programmers-guide/06-program-formats.md#rsxs).

#### Forms

```
GENCOM name rsx {rsx...} {[LOADER | SCB=(offset,value){,(offset,value)...}]}
GENCOM name [LOADER | SCB=(offset,value){,(offset,value)...}]
GENCOM rsx {rsx...} [NULL {SCB=(offset,value)...}]
GENCOM name
```

- **name** is the program, `NAME.COM` when no type is given; each **rsx** is an RSX file, `NAME.RSX` when no type
  is given. A drive may go before any of them; no wildcards.
- **Attaching:** the RSXs go into the program in the order given. On a program GENCOM has made before, an RSX of a
  name already in it is replaced, and a new one is added after the others.
- **name alone** takes out everything GENCOM put in: the program is as it was.
- **NULL** makes a `.COM` with no program, only the RSXs, named after the first one (`GENCOM SAVE [NULL]` makes
  `SAVE.COM` from `SAVE.RSX`). Run, it loads them and goes back to the prompt; they stay for the next program.
- GENCOM writes the new file as `NAME.$$$`, then deletes the old `NAME.COM` and gives the new one its name.

#### Options

- **SCB=(offset,value)** sets that byte of the SCB as the program starts, both numbers in hex, the offset
  `00`-`63`. More than one: `SCB=(34,02),(06,5A)` or `SCB=(34,02),SCB=(06,5A)`. An offset given again replaces
  the earlier value.
- **NULL** an RSX-only `.COM`, as above.
- **LOADER** is taken and does nothing (OODOS's loader is always there); `GENCOM name [LOADER]` leaves the file as
  it was.

A program holds at most 15 RSXs and SCB settings together.

#### Output

Nothing, unless an RSX is replaced or named twice:

```
A>GENCOM MYPROG MYRSX
Duplicate RSX in header. Replacing old by new
```

#### Errors

- `First submitted file must be a COM file`: the first file isn't a `.COM` (without NULL).
- `COM file found and NULL option`: with NULL, the first file is typed `.COM`.
- `Duplicate input RSX`: the same RSX named twice; it is taken once.
- `Invalid RSX type`: an RSX given another type, or a file that isn't an RSX.
- `Invalid wild card in the FCB name or type field`: a wildcard.
- `Total file size exceeds 64K`: the new file would be bigger.
- `No more space in the header for RSXs or SCB initialization.`: more than 15.
- `Unrecognized option`, `No modifier for this option` (`NULL=` or `LOADER=`), `Missing left parenthesis.`,
  `Missing right Parenthesis`, `Missing SCB value.`, `Invalid SCB offset` (over `63`).
- `Read only`: the program's file is read-only. `Cannot delete file`: its old file couldn't be deleted (a
  password, say); the new one is not kept.
- `Invalid RSX type` too for an RSX in the old file whose header isn't an RSX's.
- `NAME.TYP No File`: a file that isn't there. Other errors (a password, a full disk) are the BDOS's.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

The same forms and messages. Ours: the table of RSXs follows the program's last record, not a header page before
it, and holds the SCB settings as entries of their own; SCB offsets stop at `63`, where OODOS's SCB ends; LOADER
does nothing; `No File` for a missing file.

## SUBMIT

<a id="util-submit"></a>
### SUBMIT: run a file of commands

SUBMIT runs the commands in a `.SUB` file one after another, as if typed, with the words given after the file's name
put in for `$1`, `$2` and so on. Lines that start with `<` are typed into the program the line before runs.

#### Forms

```
SUBMIT
SUBMIT filespec {argument ...}
filespec {argument ...}
```

- **filespec** is the file, `NAME.SUB` when no type is given, on its drive or along the search chain, and its
  password after a `;` if it has one. Typing its name as a command (`NAME.SUB`, or `NAME` with SETDEF's `ORDER`
  taking SUB, with `;password` if it needs one) runs SUBMIT with it.
- **SUBMIT alone** asks `Enter File to Submit: ` for the rest of the line.
- **The arguments** are words separated by blanks: the first is `$1`, the ninth `$9`; one not given is nothing,
  and ones past what the file uses are ignored. `$0` is the file's name as typed. `$$` is a `$`.
- **`^` and a letter** (or `@ [ \ ] _`) is that control character (`^C` is ^C); `^^` is a `^`. Before anything else
  `^` is kept as it is.
- **A line starting with `<`** is input for the program the line before runs (or for a built-in's question, such
  as USER's), as if typed: its line reads, then its keys, a key at a time with a CR at the end; `<` alone is a CR.
  A line longer than the program's line buffer is cut to fit, as a typed line is, and the rest dropped.
  When the program asks for more than the `<` lines give, it reads the keyboard. `<` lines left over when it ends
  are skipped, and `WARNING: Program input ignored` said once, before the next prompt.
- **Each command** is shown after the prompt as if typed; lines starting with `;` are comments and `:` runs a
  command only after a success, as at the prompt.
- **A SUBMIT in a SUBMIT file** runs its own file first, then the first goes on. So does a GET in one.
- **^C** to a program ends it and every SUBMIT running, and the prompt reads the keyboard.
- A line ends at CR LF, a CR or an LF; the file at a ^Z or its end. SUBMIT writes the file with its arguments put
  in to `SUBnn.$$$` in user 0 on the temporary drive (SETDEF's TEMPORARY), and deletes it at the end; one left by a
  reset is deleted by the next SUBMIT.

#### Options

None.

#### Output

What the commands show, each after a prompt:

```
A>SUBMIT START SAM
A>ERA SAM.BAK
No File
A>DIR SAM
A: SAM      TXT
```

#### Errors

- `No 'SUB' file found`: the file isn't on its drive or the search chain.
- `Error on line n:` and one of: `Parameter Error.` (a `$` before anything but a digit or `$`), `Invalid ASCII
  character` (a byte `FF`), `Line too long` (over 128 characters with the arguments in: ours). Nothing runs.
- `Directory full`: no temporary file could be made. `Disk Write Error`: it couldn't be written. `Disk Read
  Error`: it couldn't be opened again to run (gone, or unreadable); nothing runs.
- `Insufficient Memory`: no room for SUBMIT's RSX (too many SUBMITs within SUBMITs: the CCP says it).

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

The same, and `$0` the file's name (ours: the Guide allows `$0` and says no more). Ours: the temporary files'
names, `SUBnn.$$$` in user 0; `Line too long`; `^` before anything but a letter and `@ [ \ ] ^ _` kept; a ^C ending
every level; the warning said once for each run of skipped `<` lines.

## GET

<a id="util-get"></a>
### GET: take console input from a file

GET makes the next command, or with SYSTEM everything from now, read its console input from a text file instead of
the keyboard.

#### Forms

```
GET {CONSOLE INPUT FROM} FILE filespec {[ECHO | NO ECHO | SYSTEM]}
GET {CONSOLE INPUT FROM} CONSOLE
```

- **FILE filespec:** the next command typed (a program, or a built-in that asks for something) reads from the
  file: each line as a line typed, each character as a key. When the file runs out, it reads the keyboard again;
  when the command ends first, the rest of the file is left.
- **CONSOLE** takes input from the keyboard again at once, ending the newest GET. In a file read with SYSTEM, a
  `GET CONSOLE` line ends that file.
- A GET in a file GET is reading (with SYSTEM) reads its own file first, then the first goes on.
- What GET shows of what it reads (ECHO) goes on to the console as if typed: a PUT given before the GET writes it
  to its file too, one given after it doesn't (it sees only what the program itself writes).
- A program that reads a line with its buffer at the DMA address (function 10 with A/X = 0) keeps the characters
  already there as typed first, as with the keyboard.
- The words are typed in full. The file is read to its end, or a ^Z; an LF after a CR is dropped.

#### Options

- **ECHO** (the default) shows what is read as if it were typed.
- **NO ECHO** doesn't; what the program writes, and the prompts, are shown as always.
- **SYSTEM** reads the CCP's command lines from the file too, from now, and every program's input, until the file
  ends or a `GET CONSOLE` line in it. SYSTEM and ECHO or NO ECHO don't go together.

A program that asks whether a key is waiting gets an answer set by its console mode (the Programmer's Guide,
[function 109](../oodos-programmers-guide/07-function-reference.md#bdos-109)): by default "no", unless it asks twice
in a row, so a program waiting for a key gets one and a program looking for ^C now and then isn't stopped.

#### Output

```
A>GET FILE ANSWERS.TXT
Getting console input from file: ANSWERS.TXT
```

#### Errors

- `Invalid command.`: words that make no form, or no file.
- `Invalid delimiter.`: a second option list, or one not closed.
- `Invalid option.`, `Invalid option modifier.` (a value, two options that don't go together).
- `End of line expected`: more after the command.
- `Auxiliary device redirection not implemented`: AUXILIARY.
- `Invalid file specification.`: a wildcard, or a name OODOS refuses. `No File`: the file isn't there.
- `Insufficient Memory`: no room for GET's RSX (the CCP says it before GET runs).

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

The same. Ours: the words in full only; `No File` for a missing file.

## PUT

<a id="util-put"></a>
### PUT: send console or printer output to a file

PUT makes the next command, or with SYSTEM everything from now, write its console output, or its printer output,
to a text file as well as, or instead of, the device.

#### Forms

```
PUT CONSOLE {OUTPUT TO} FILE filespec {[options]}
PUT PRINTER {OUTPUT TO} FILE filespec {[options]}
PUT CONSOLE {OUTPUT TO} CONSOLE
PUT PRINTER {OUTPUT TO} PRINTER
```

- **FILE filespec:** the next command typed, a built-in such as DIR too, writes to the file; it is closed when the
  command ends. The file starts with the line feed the prompt owes after the command line.
- **... TO CONSOLE**, **... TO PRINTER** end the PUT of that kind and close its file.
- A file of the name there already: `File already exists Delete it? (Y/N)`. N ends PUT; with Y the old file is
  replaced once the new one is complete. A read-only one is refused.
- With a PUT of the same kind already writing to a file: `Do you want another file? (Y/N)`. Y: both files get the
  output; N: the first file is closed, and the new one takes over.
- A second PUT command isn't "the next command" for a PUT given just before it: that one waits for the command
  after, so neither file gets the second PUT's own words. This holds for either kind: a PRINTER PUT given just
  before a CONSOLE PUT command waits past it too.
- GET's echo of what it reads (with ECHO) goes into the file of a PUT given before the GET, not of one given after
  it.
- The words are typed in full.

#### Options

Any of these together, separated by commas or blanks:

- **ECHO** the output also goes to the device: the default for the console.
- **NO ECHO** to the file only: the default for the printer. A program that asks for input then shows `PUT>`
  first. ^S, ^Q and ^C aren't looked at while it writes.
- **FILTER** writes control characters other than CR and LF as `^` and a letter (ESC as `^[`, a tab as `^I`), and
  DEL as `^?`.
- **NO FILTER** (the default) writes them as they are; a tab stays a tab.
- **SYSTEM** everything from now, the CCP's prompts and messages too, until `PUT ... TO CONSOLE` (or `PRINTER`).

What OODOS itself writes isn't caught: the echo of a typed line, its error messages, and ^P's copy to the printer.

#### Output

```
A>PUT CONSOLE OUTPUT TO FILE DIR.PRN
Putting console output to file: DIR.PRN
```

#### Errors

As GET's, and:

- `Read only.`: the file there already is read-only.
- `PUT ERROR: FILE ERASED.`: the file was deleted while PUT wrote it, so it couldn't be closed.

#### Since

OODOS 0.3

#### Status

**built**

#### CP/M 3

The same. Ours: the words in full only; `Do you want another file?` is asked when a PUT of the kind already
writes to a file; an old file is replaced only when the new one is complete; the file keeps the prompt's line feed.


## DOS33

<a id="util-dos33"></a>
### DOS33: files to and from a DOS 3.3 disk

DOS33 reads and writes an Apple II DOS 3.3 disk: a floppy in O: or P:, or a 140 KB `.dsk` image on the card as an SD
drive (in `DRIVES.TXT`, or mounted with MOUNT). It lists the disk, takes a file off it, puts one on it, and deletes one.
It works on the disk's own sectors, so the drive needn't be (and isn't) a CP/M drive while it does; the next CP/M
command on it reads the disk again, and on a DOS 3.3 disk says `Invalid Drive`: such a disk is never a CP/M drive
([chapter 3](03-drives-and-files.md)).

#### Forms

```
DOS33 d:
DOS33 GET d:NAME {e:}FILE.TYP {T | B | R}
DOS33 PUT {e:}FILE.TYP d:NAME {T | B{=addr}}
DOS33 ERA d:NAME
```

- **`DOS33 d:`** lists the disk's catalog: for each file a `*` if it's locked, its type letter (T text, I and A BASIC
  programs, B binary, S, R; `?` for a type byte DOS 3.3 doesn't make), the sectors it takes (three digits), and its
  name, a control character in it shown as `^G` and the like; then the free sectors.
- **GET** takes the file NAME off the disk into the CP/M file. T: the text to its first 0 byte as stored, or its
  first sector never written, bit 7 of each character cleared, each CR written as CR LF, and ^Z after. A
  random-access text file keeps a 0 after each record, and has sectors never written between, so T stops at the
  first: take one with R. B: the bytes DOS keeps first (the load address and the length) dropped, the length's
  bytes written (a sector never written inside the length as zeros); the address is shown; a B file whose first
  sector was never written has neither, so nothing is written. R: every sector of the file as it is on the disk, a
  sector never written as zeros, to the last one written. Without a letter, T for a T file, B for a B file, R for
  any other. ^C stops GET.
- GET writes a `.$$$` file first and gives it the name only when it is whole, so a CP/M file already there is
  replaced only then; a GET that fails, or is stopped by ^C, leaves it as it was, and no `.$$$` file, but for
  three cases: a failed rename (the message says the data is there), Break or a reset in the middle (the `.$$$`
  file is left, and the next GET refuses it until you ERA it), and a changed floppy (the `.$$$` file is on the
  old disk; nothing on the new one is touched). Before it reads anything, GET refuses a target of type `$$$`, a
  read-only drive (a unit mounted `RO` too), a read-only file, one protected for writing by a password, and a
  `NAME.$$$` already there (yours: it isn't deleted). Only your user number's files count: a user-0 system file
  seen from another user isn't the target.
- **PUT** puts the CP/M file on the disk as NAME. T: bit 7 set, CR LF made a CR and a lone LF a CR too, NUL bytes
  dropped, to the first ^Z, a 0 after. B: the load address (`$2000` unless `B=addr` gives it, in hex) and the length
  (the file's records × 128, so at most 511 of them) first. Without a letter, T for a `.TXT` file, B for any other. A
  name that's on the disk already is refused: ERA it first.
- **ERA** deletes NAME, its sectors free again. A locked file is refused.
- A name has 1 to 30 characters; one with blanks is typed in quotes: `DOS33 GET O:"MY FILE" M:MY.TXT`. The command
  line comes in capitals, quotes and all, so a file whose name has small letters or control characters, as DOS 3.3
  allows, can be listed but not reached by GET or ERA.
- The CP/M file mustn't be on the Apple disk's own drive: GET and PUT refuse it.
- Before GET or ERA does anything, the file's track/sector lists are read through: each must start where the file
  says (the k-th at its sector 122 × k, so a list chained in a loop is found, while a random-access file's empty
  lists pass), and each sector must be on the disk and not on the catalog's track. A file that isn't so is refused,
  nothing written.

#### Options

None in brackets: the conversion is the letter after the names (T, B or R), as above.

#### Output

```
A>DOS33 O:
O: DOS 3.3 disk, volume 254
 T 002 README
 B 005 GAME
489 sectors free
A>DOS33 GET O:GAME M:GAME.BIN
Address $6000, 1000 bytes
```

#### Errors

- `Use: DOS33 d: | GET ... | PUT ... | ERA d:NAME`: a form DOS33 doesn't have.
- `Not an Apple disk drive.`: d: isn't O:, P: or a 140 KB SD drive (or its unit is no longer mounted). `Not a DOS
  3.3 disk.`: its VTOC (track 17, sector 0) isn't one DOS 3.3 would take (a CP/M disk, a blank one). `The Apple
  disk was changed.`: a PUT or ERA's write found the SD drive's unit mounted again since DOS33 started (another
  image swapped in on the card): nothing more is written. A GET reads on from whatever is mounted then.
- `File not found.`, `File exists.`, `File locked.`, `Bad name.`: the Apple disk's file, or its name.
- `Bad file: its sector lists aren't a DOS 3.3 file's.` (a list chained in a loop, a sector past the disk);
  `Bad catalog: its chain loops.`: a damaged disk.
- `Disk full.`, `Catalog full.` (105 files): the catalog and the VTOC are as they were, so the file isn't there and
  the sectors PUT had written to are still free.
- `File too big for B.`: a B file of more than 511 records.
- `Disk error.`: a sector couldn't be read or written. `Disk write-protected.`: the floppy's notch is covered, or the
  drive is set read-only (SET `[RO]`).
- `The CP/M file can't be on the Apple disk.`: e: is d:.
- `GET won't write a .$$$ file.`, `CP/M file read-only.`, `CP/M file write-protected (its password).`, `CP/M drive
  read-only.`, `Its .$$$ file is there already: ERA it first.`: GET's refusals before it starts.
- `No File`, `CP/M disk full.`, `CP/M directory full.`, `CP/M read error.`, `CP/M file not closed.`, `CP/M error.`:
  the CP/M file. `The CP/M disk was changed.`: O: or P:'s floppy was changed while GET wrote to it (Media
  Change); its `.$$$` file is left on the old floppy.
  `GET stopped.`: after an error OODOS has said (`Read Only`, `File R/O`, ...). `CP/M file not renamed: the data is
  in its .$$$ file.`: the last step failed, so the new data is kept under that name.

Each sets the return code to `$FF00`. A disk DOS33 has written passes DOS 3.3's own `VERIFY` and loads with `BLOAD`.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no such utility.

## PLAYSID

<a id="util-playsid"></a>
### PLAYSID: play a C64 .sid tune

PLAYSID plays a `.sid` file, a Commodore 64 tune with its own player code, on the sound card's 6510 and its two
SIDs, until you press a key. The card does all the work, so the tune keeps time on its own clock. PLAYSID is built
on OOLIB's linked library ([the Programmer's Guide, chapter 8](../oodos-programmers-guide/08-game-services.md#sid-tunes)),
which plays the tune as the emulator's own Play .sid does.

#### Forms

```
PLAYSID filespec {song}
```

- **filespec** is the tune's file; `.SID` if you leave the type out. It is read whole into memory, so it must fit
  in the TPA, about 40 KB.
- **song** is the song to play, from 1 to the tune's count (256 at most), in decimal. Without it, the tune's own
  start song.

#### Options

None.

#### Output

```
A>PLAYSID TUNE 2
Playing song 2 of 3: Test Tune, Nobody
Press a key to stop.
```

The name and the author are the tune's own, from its header. The SIDs are the card's own model (a jumper on the card,
not a register), whatever the tune asks for. Any key stops it: the 6510 stops, the SIDs go quiet,
and PLAYSID ends. Ending any other way (^C, Break) stops the tune too: the warm boot quiets the card.

#### Errors

- `PLAYSID filespec {song}`: no file named. `Invalid file specification.`: a name OODOS can't take, or a song that
  isn't a number from 1 to 65,535. `No File`: no such file.
- `Too big.`: the file doesn't fit in memory.
- `Song n of m?`: a song past the tune's count.
- `Not a .sid file.`, `Unknown .sid version.`, `Bad data offset.`, `No code in the file.`, `The code runs past the
  6510's 64 KB.`, `Bad song count.`: the file isn't a .sid file PLAYSID can read.
- `An RSID tune: PLAYSID plays PSID only.`: an RSID tune expects a whole C64 (its video chip, its second CIA, its
  ROMs), which the card hasn't got.
- `A Compute! MUS file needs a player of its own.`, `The tune covers the 6510's vectors.`, `No free page for the
  player's stub.`: a tune the card's player can't take.
- `The sound card is in use, or not there.`: another program or an RSX has the 6510, or there is no sound card.
  `The sound card's RAM wasn't loaded.`: a load into it was refused.

Each sets the return code to `$FF00`.

#### Since

OODOS 0.4

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no such utility.

## VIEW

<a id="util-view"></a>
### VIEW: show a picture

VIEW shows an asset file, a picture made for the video card (a bitmap, tiles and their map, sprites, with their
palette), on the screen until you press a key, then puts the console's screen back. The files are made on Linux from
PNG images by `oodos/tools/mkasset.py`. VIEW is built on OOLIB's linked graphics library ([the Programmer's Guide,
chapter 8](../oodos-programmers-guide/08-game-services.md#graphics)), which loads each part of the file from the SD
card into the video card's own memory (*proposed*: video card revision 2, which VIEW needs).

#### Forms

```
VIEW filespec
```

- **filespec** is the asset file; `.OGX` if you leave the type out.

#### Options

None.

#### Output

```
A>VIEW PICTURE
Press a key to stop.
```

The picture covers the screen at once; the line goes to the console's own screen, behind it, and shows when VIEW
ends. A palette in the file sets the colours; a bitmap or a tile map shows on the screen's first layer, as the file
says it was made; a sprite table turns the sprites on. Any key ends it. Ending any other way (^C, Break) puts the
console's screen back too: the warm boot does.

#### Errors

- `VIEW filespec`: no file named. `Invalid file specification.`: a name OODOS can't take. `No File`: no such file, or
  one that can't be read.
- `Not an asset file.`: the file isn't an asset file of format 4 (*proposed*; an older file, of format 3, needs
  making again with `mkasset.py`).
- `Not enough video memory.`: the file's parts need more of the card's memory than there is.
- `Needs OODOS's interface 8.`: an older OODOS, or no revision 2 video card. `Video memory W1 can't reach.` and
  `The asset wasn't loaded.`: a part OODOS refused.

Each sets the return code to `$FF00`.

#### Since

OODOS 0.5

#### Status

**built**

#### CP/M 3

OODOS's own: CP/M 3 has no such utility.

# 3 Drives and file names

Drives and what they are on the card, user numbers, file names and wildcards, attributes and passwords.

The design record, for the reasons: [hw/oodos.md, "Drives"](../../hw/oodos.md#drives).

## Drives

A drive is a letter and a colon, `A:` to `P:`. OODOS has these:

| Drive | What |
|---|---|
| A:–G: | image files on the SD card, as `/OODOS/DRIVES.TXT` names them. A: is the system drive, which OODOS starts from |
| M: | the RAM disk, 128 KB, made empty at each start: what is on it is lost at power-off |
| O:, P: | the Apple II drives 1 and 2, through the Disk II card: 5.25" floppies in the SoftCard's Apple CP/M format, 128 KB each |

O: and P: are there whenever the machine has the Disk II card, a disk in the drive or not. With no disk in, or with
no disk the card can read, using the drive gives `OODOS Error On O: Disk I/O` after a second or two. The emulator on
the website says the same unless you have given it the Disk II's sequencer PROM, with your IIe ROMs (Settings,
ROMs): without it the card reads nothing.

**Changing a floppy.** Wait for the drive to stop (a second after the last access), then change the disk: the next
command that uses the drive sees the new disk, reading its directory again. A file a program had open on the old
disk is refused from then on (the program gets Media Change, and should open it again), and data it hadn't
written yet is dropped rather than written onto the new disk. Don't change a disk while a program is using it: a
disk changed within a second of the drive's last access, in the middle of a copy between O: and P:, can take a few
of the copy's sectors before the change is seen. A disk taken out while a program's data for it waits to be written gives `Disk I/O`
when that data's turn comes (at the program's end at the latest), and the data isn't written unless the disk is
back by then; the other drives' is, as usual. A disk whose directory is exactly the old one's (a copy of it, its files changed in place) isn't
seen as another disk: change a disk only when the drive has stopped and no program has a file open on it. Data
waiting for a floppy that turns out to be write-protected is dropped, all of it, with `Read Only` on O: or P:
(perhaps while the program is using another drive); taking the tab off afterwards doesn't bring it back.

**`DRIVES.TXT`** has a line for each drive the card holds: the letter and colon, blanks, the image's path on the
card, and `RO` after it for a drive that can be read but not written. A line starting `#` is a comment:

```
# A is the system drive.
A: /OODOS/A.IMG
B: /OODOS/WORK.IMG
C: /DISKS/CPM.DSK  RO
```

A drive not named isn't there. An image is 8 MB or 32 MB (the `oodos-8m` and `oodos-32m` formats of
[`oodos/diskdefs`](../../oodos/diskdefs)), and is made on a PC by cpmtools's `mkfs.cpm`
([oodos/README.md](../../oodos/README.md)).

**An Apple CP/M disk** can be a drive too, B: to G: but never A:: a 140 KB `.dsk` image of a 5.25" disk in the
SoftCard's CP/M format, which cpmtools calls `apple-do` (`mkfs.cpm -f apple-do`, `cpmcp -f apple-do` on a PC). It
holds 128 KB in 1 KB blocks, 64 directory entries, and no date stamps until INITDIR gives it some (which takes a
quarter of its entries). Name it in `DRIVES.TXT` as any image; `C: /DISKS/CPM.DSK  RO` above is one. An image of
any other size is refused at start: `DRIVES.TXT LINE 3: /DISKS/X.DSK IS NOT 8 MB, 32 MB OR 140 KB`, and an Apple
disk on A: `... IS AN APPLE DISK, NOT FOR A:`.

**The current drive** is the prompt's. A command or file name without a drive uses it. To change it, type the drive
alone:

```
A>B:
B>
```

A letter from A to P with no drive behind it is the BDOS's error, and the current drive stays:

```
A>H:
OODOS Error On H: Invalid Drive
BDOS Function = 14

A>
```

**A read-only drive** (`RO` in `DRIVES.TXT`, or made so by `SET d: [RO]`) can be read but not written: a write is
`OODOS Error On B: Read Only`. One made so by SET is read-write again after `SET d: [RW]`, or ^C at the prompt; one
marked `RO` in `DRIVES.TXT` stays read-only.

## User numbers

Each drive's files are in 16 groups, user numbers 0 to 15, so several people or projects can share a drive. OODOS
starts in user 0. `USER 5` makes user 5 current; so does `5A:`, with drive A:. The prompt shows the user when it
isn't 0: `5A>`.

A command sees only the current user's files, with one exception: a file of user 0 with the SYS attribute (below)
is found from any user when it is run or opened, read-only. The utilities on the card are kept so, in user 0 as
SYS files, so they run from every user. DIR shows the current user's files; DIR.COM's `[USER=ALL]` shows every
user's.

## File names

A file is named `NAME.TYP`: a name of up to 8 characters and a type of up to 3, which says what the file holds
(`.COM` a program, `.TXT` text). The type may be left out. A drive may go before the name, and a password after
it:

```
{d:}NAME{.TYP}{;password}
```

- **Letters are capitals:** the command line is taken in capitals, so `readme.txt` is `README.TXT`.
- **Not in a name:** blanks, tabs, control characters, and `; = < > . : , | [ ]`, which separate names and options
  on a command line. Nor characters past `$7E`. Letters and digits are safest: `*` and `?` are wildcards, `!`
  separates commands, and some commands give other marks a meaning of their own.
- **A password** is up to 8 characters (below).

## Wildcards

Where a command takes several files, two characters in a name stand for others:

- **`?`** stands for any one character in its place, or a blank: `A?.TXT` matches `AB.TXT` and `A.TXT`.
- **`*`** stands for the rest of the name, or of the type: `*.TXT` is every file of type `TXT`, `HELLO.*` every type
  of `HELLO`, `*.*` every file.

`A*.T?T` is every name starting `A` with a type of `T`, any one character, `T`. A name with no type matches only
files with no type: `DIR NOTES` doesn't show `NOTES.TXT`; `DIR NOTES.*` does.

DIR, ERASE, TYPE.COM, RENAME.COM, SET and PIP take wildcards. ERASE asks before it erases with a wildcard, even if
one file matches.

## Attributes

Each file has attributes, kept with it in the directory:

- **SYS or DIR:** a SYS file isn't shown by DIR (DIRSYS shows them), and user 0's are found from every user (above).
  DIR is the other kind, a file DIR shows.
- **RO or RW:** a read-only file can be read, but not written, erased or renamed: those are
  `OODOS Error On A: File R/O`.
- **Archive:** marks a file as backed up. PIP's `[A]`, copying to a drive, copies only files without it, and sets it on
  each after the copy; a copy PIP makes has it clear. OODOS clears it when a program writes to the file, as CP/M 3
  does, so a file changed since its backup is copied again; `SET [ARCHIVE=OFF]` clears it too.
- **F1 to F4:** for the user's own use; OODOS doesn't look at them.

[SET](05-utilities.md#set) sets them (`SET README.TXT [RO]`), and DIR.COM's `[FULL]` shows them.

## Passwords

A drive can protect its files with passwords. It is off on OODOS's drives until it is turned on: `SET d:
[PROTECT=ON]`. Then a file given a password by SET has one of three modes:

| Mode | The password is needed to |
|---|---|
| `READ` | read the file at all, or change it |
| `WRITE` | write it, erase or rename it, or change its attributes; it can be read without |
| `DELETE` | erase or rename it, or change its attributes; it can be read and written without |

Give a password after the name and a `;`: `TYPE SECRET.TXT;MYPASS`. Without it, a protected file is
`OODOS Error On A: Password Error`. A password set by `SET [DEFAULT=pw]` is tried for every file, so it needn't be
typed each time.

The drive's label may have a password too; SET asks for it before it changes the label. With protection off, a
file's password and mode are kept but not asked for.

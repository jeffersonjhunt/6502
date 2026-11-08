# 6 Errors

Every message the CCP, the utilities and the BDOS can show, what causes it, and what to do.

## The CCP's messages

The CCP says these itself, for a command line it can't run or a built-in command it can't do. Each takes the
place of the command's output, on a line of its own, and the prompt follows.

| Message | What causes it | What to do |
|---|---|---|
| `NAME?` | The keyword, in capitals, and `?` (for a built-in typed in a wrong form, the whole command). No program of that name on the drives searched (the drive given, or the search chain: [SETDEF](05-utilities.md#util-setdef)); a type other than `.COM` (a `.PRL` among them); a name with `?` or `*`, or one that can't be a file name; a built-in typed in a form it doesn't take (too many names, `REN X.TXT=X.TXT`, something after `B:`); `USER` with a number that isn't 0 to 15. | Check the spelling and the drive. A program on another drive needs the drive typed (`B:NAME`), or that drive in the search chain. |
| `SUBMIT?` | A `.SUB` file was found, but SUBMIT.COM, which runs it, wasn't. | Put SUBMIT.COM's drive in the search chain. |
| `DIR.COM required` (or `ERASE.COM`, `RENAME.COM`, `TYPE.COM`) | A built-in typed in a form only its utility does (`DIR [FULL]`, `ERA` alone, `REN *.X=*.Y`, `TYPE *.TXT`), and the utility isn't found on the search chain. | Put the utility's drive in the search chain, or type the command with the drive: `A:DIR [FULL]`. |
| `Command tail too long` | More than 126 characters after the command's name. Nothing is run. | Shorten the line. |
| `Password Error` | The program's file is protected, and its password wasn't given or didn't match. | Type it after the name: `NAME;PASSWORD`. |
| `READ ERROR` | The program's file couldn't be read: a disk error, a file shorter than its header says, or a drive typed (or the current one) that isn't there. | Check the drive; copy the program again. |
| `NO PROGRAM` | The file isn't an OODOS program: it doesn't start with an OODOS program header. A CP/M program for another processor is one. | |
| `BAD PROGRAM HEADER` | An OODOS program the CCP won't run: a `.PRL` or an RSX renamed `.COM`, flags it doesn't know, or RSXs attached that it can't load. | |
| `Insufficient Memory` | The program, with any RSXs attached to it, doesn't fit in the memory programs have, or 15 RSXs are loaded already. (Ours: CP/M 3 says this for other utilities.) | Run it without the RSXs loaded before it, or after a reset. |
| `No File` | A built-in found no file to match: DIR, DIRSYS, ERASE, RENAME's old name, TYPE. | Check the name, the drive and the user number. |
| `Media Change` | ERASE, RENAME or TYPE on O: or P: found another floppy in the drive: changed within a second of the drive's last use, or while TYPE waited at a page's end. Nothing was deleted or renamed. A program's name typed with access stamps on its drive too: it isn't run. | Wait for the drive to stop before changing a floppy; type the command again for the new disk. |
| `SYSTEM FILE(S) EXIST` | Not an error: after DIR's list, files of the other kind matched too ([DIRSYS](04-built-in-commands.md#util-dirsys) lists them). DIRSYS says `NON-SYSTEM FILE(S) EXIST`. | |
| `No IIe ROM in the cartridge (the emulator: Settings, ROMs)` | APPLE2 found no Apple IIe ROM in the cartridge. OODOS goes on. | In the emulator, choose a IIe ROM in Settings, ROMs. |
| `Press a key for more` | Not an error: the screen is full. Any key goes on; ^C stops the command. [SETDEF](05-utilities.md#util-setdef)'s `[NO PAGE]` turns the stops off. | |

The questions the built-ins ask (`ERASE B:*.TXT (Y/N)?`, `Not renamed: NEW.TXT file already exists, delete
(Y/N)?`, `Enter User #: `) are in their entries in [chapter 4](04-built-in-commands.md). Each utility's own messages
are in its entry, under **Errors**, in [chapter 5](05-utilities.md); a utility says the BDOS's errors as below,
unless its entry gives it a message of its own for one.

## The BDOS's messages

A disk error found by the BDOS, OODOS's file system, is said in two lines:

```
OODOS Error On B: Disk I/O
BDOS Function = 15 File = LETTER  .TXT
```

The first names the drive and the error; the second the function the program called (its number in the
[Programmer's Guide](../oodos-programmers-guide/07-function-reference.md)) and, for a function on a file, the
file, its name and type padded as they are in the directory. What happens next depends on the program: by
default it is ended, and the prompt comes back. A built-in command, a utility, or a program that asks to see its
errors, says the message and goes on (with the next file, if there is one).

| Error | What causes it | What to do |
|---|---|---|
| `Disk I/O` | The card couldn't read or write a sector: the card was taken out, or failed. A changed sector that couldn't be written is kept, and written when it can be. On O: or P:: no disk in the drive, one in another format, a damaged sector, or (on the website) no Disk II sequencer PROM given. | Put the card back. A ^C at the empty prompt resets the drives and reads the card again, but drops what couldn't be written. On O: or P:, put a disk in, an Apple CP/M one. |
| `Read Only` | A write to a drive that is read-only: set so by [SET](05-utilities.md#util-set) `[RO]`, or a card unit that is write-protected; on O: or P:, a write-protected floppy (its notch covered). | `SET d: [RW]`, or a ^C at the empty prompt, which makes every drive read-write again (a write-protected unit, or floppy, stays read-only). |
| `File R/O` | A write, a delete or a rename of a read-only file. | `SET name [RW]`. |
| `Invalid Drive` | A drive that isn't there: one OODOS has no unit for (`H:`), or a card unit that couldn't be read. Also an Apple drive (an SD unit of 140 KB, O: or P:) holding a DOS 3.3 disk, which isn't a CP/M disk ([chapter 3](03-drives-and-files.md)). | Use a drive the boot's status lines list ([chapter 1](01-introduction.md#starting-oodos)). For a DOS 3.3 disk, use [DOS33](05-utilities.md#util-dos33). |
| `Password Error` | A protected file opened, deleted or renamed without its password, or with the wrong one. | Give the password after the name: `TYPE LETTER.TXT;SECRET`. |
| `File Exists` | A file made, or a file renamed, to a name another file has already. | Erase or rename the other first. |
| `? in Filename` | A wildcard where one exact name is needed, as for a file made or renamed. | |

## A program's return code

Every command leaves a return code, which a later command on the same line can test. A program sets it with
[function 108](../oodos-programmers-guide/07-function-reference.md#bdos-108); OODOS sets it in these cases:

| Code | Means |
|---|---|
| 0 | Set as each program starts (one not chained to): success, unless the program says otherwise. A built-in or a utility leaves it 0 when it did what was asked. |
| `$FF00` | A built-in or a utility said an error (`No File` among them), the CCP couldn't run the program (`NAME?`, `READ ERROR` and the rest above), or RENAME's question was answered N. |
| `$FFFD` | The BDOS ended the program for a disk error. |
| `$FFFE` | The program, or a built-in, was stopped by ^C. |

A code from `$FF00` to `$FFFF` is a failure; any other is a success.

**Several commands on a line** are separated by `!` ([chapter 2](02-command-line.md#several-commands-on-a-line)). A
command that starts with `:` runs only if the one before it succeeded: in `PIP B:=A:LETTER.TXT ! :ERA A:LETTER.TXT`,
the file is erased only if PIP copied it. A command skipped so leaves the code as it was, so every `:` command
after a failure is skipped. A ^C drops the rest of the line.

A comment, a command that starts with `;`, does nothing and leaves the code alone.

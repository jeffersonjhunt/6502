# 3 The file system

Files on OODOS's drives: how they are named and found, protected and stamped, and read and written a record or many
records at a time.

The design record, for the reasons: [hw/oodos.md, "Disk format"](../../hw/oodos.md#disk-format), ["The BDOS, phase
3"](../../hw/oodos.md#the-bdos-phase-3).

## Drives

OODOS has up to eight drives, named by a letter. A program names one as functions 14 and 101 do, 0 for A: to 15 for
P:, or in an FCB's byte 0, 0 for the current drive and 1 for A: to 16 for P:.

| Drive | What |
|---|---|
| A:–G: | the SD card's units 1–7: image files on the card's FAT file system, as `/OODOS/DRIVES.TXT` names them. A: is the system drive |
| M: | the RAM disk, 128 KB in banks C–D, made empty at each cold boot |
| H:–L:, N:–P: | not drives: Invalid Drive (4) |

An SD drive is 8 MB (`oodos-8m`: 4 KB blocks, 1,024 directory entries) or 32 MB (`oodos-32m`: 8 KB blocks, 2,048
entries); M: has 2 KB blocks and 256 entries. These have 512-byte sectors, 16 a track, and no boot tracks. B:–G: may
also be a 140 KB Apple CP/M disk image (cpmtools's `apple-do`, the SoftCard's format): 1 KB blocks, 128 of them, 64
entries, 3 boot tracks, and 8 sectors of 512 bytes a track (SPT 32, OFF 3; the BIOS makes each of two of the disk's
256-byte sectors). The format is CP/M 3's, byte for byte, so cpmtools reads and writes the images on a PC
([hw/oodos.md, "Disk format"](../../hw/oodos.md#disk-format)). A drive's DPB is CP/M 3's, and function 31 returns
the address of a copy in common memory.

A drive is logged in the first time a function uses it after a reset: the BDOS reads its whole directory once, to
make the allocation vector and the hash it searches by, and reads the label's mode. A drive `DRIVES.TXT` doesn't
name, or one unmounted, is Invalid Drive. A drive is read-only if its image is mounted so (`RO` in `DRIVES.TXT`, or
bit 0 of function 74's flags), or after function 28, until the next reset (13, 37). A write to it is Read Only (2).

The BDOS notices an image swapped under a drive: it reads the card's list of units again at each reset, at each warm
boot and at a login, and drops a changed drive's buffers without writing them to the new image. Function 74 mounts
and unmounts B:–G: while OODOS runs; A: can't be changed.

Data is written back from OODOS's 32 sector buffers when a buffer is wanted for another sector, by a close, by
function 48, by a reset and at each warm boot. Directory changes are written at once.

## User numbers

Each drive's directory is shared by 16 users, 0 to 15. A file belongs to one user, and a program sees the current
user's files only: function 32 reads or sets the user, and the CCP's USER command sets it at the prompt. Two users
can each have a file of the same name.

One exception: a file in user 0 with the SYS attribute (t2') can be opened from any other user, if that user has
no file of the name. It is opened read-only, and f8' is set in the FCB to say where it came from
(function [15](07-function-reference.md#bdos-15)). Searches (17, 18) don't find it.

Directory entries use the user byte for more than files: 16 + the user marks a file's XFCB, `$20` the directory
label, `$21` an SFCB (the date stamps), and `$E5` a free entry.

## File attributes

The top bit of each of the eleven name and type bytes in an FCB is an attribute, called by its byte with a prime:
f1' is bit 7 of byte 1, t1' bit 7 of byte 9. The rest of each byte is the character, so a program masks it with
`$7F` before comparing a name.

| Attribute | Where it is kept | What |
|---|---|---|
| f1'–f4' | the directory | the user's: OODOS gives them no meaning; SET sets them |
| f5' | the FCB only | asks function 19 to delete only the file's XFCBs |
| f6' | the FCB only | asks function 30 to set the byte count, and function 22 to make an XFCB (passwords on) |
| f7' | the FCB only | set by Open (15): opened without a write-protected file's password, so read-only |
| f8' | the FCB only | set by Open: opened from user 0, so read-only |
| t1' | the directory | read-only: a write is File R/O (3), and so are delete and rename |
| t2' | the directory | SYS: DIR leaves the file out, and other users may open it from user 0 |
| t3' | the directory | archive: set once the file has been backed up, cleared by a write |

Function 30 sets f1'–f4' and t1'–t3' on every extent of a file, from the FCB, and clears f5'–f8' there; Open copies
the directory's attributes into the FCB. Make (22) clears them all, on the disk and in the FCB. SET and PIP's A option
set t3'. A write (21, 34, 40) clears t3' in the directory entry of the extent it writes, and in the FCB, as CP/M 3's
BDOS does; so does truncate (99), in the entry that ends at the new last record. Only that extent's entry changes,
so a file has been backed up and not changed since only if every one of its entries has t3': a backup program reads
them all, by function 17 with `?` in EX, as PIP's A option does.

With f6', function 30 also puts the FCB's CR into the entry's byte count (S1), the bytes used in the file's last
record; Open with CR `$FF` gives it back in CR.

## Passwords

A drive's passwords are off unless its directory label has bit 7 of its mode set (function 100): then a file with an
XFCB whose mode has any of bits 7–5 is protected. Without such a label, a drive behaves as if no file had a password,
and OODOS drives come without ([Directory labels](#directory-labels)).

| Mode bit | Open (15) without the password |
|---|---|
| 7 read | Password Error (7) |
| 6 write | opens, with f7' set: a write is File R/O (3) |
| 5 delete | opens as usual |

Whichever bits are set, delete (19), rename (23), set attributes (30) and truncate (99) of a protected file are
Password Error without its password.

A program gives a password in the first 8 bytes of the DMA buffer (function 26's address, never the segment's), or
once for all with function 106, the default password, which the BDOS tries second. A password is 8 bytes, padded
with blanks; 8 blanks, or 8 zero bytes, is none, and anything matches it. Otherwise the bytes must match exactly:
no case is folded (ours).

The functions that check a password: Open (15), delete (19), rename (23), set attributes (30), truncate (99), make
a label (100, for the label's own) and set an XFCB (103). Searches and function 102 never need one. A password error
is the last thing a function checks, so a call that fails for another reason fails as it would without passwords.

Function 103 gives a file its mode and password; Make (22) with f6' does it as the file is made, from the DMA
buffer's bytes 0–7 (the password) and byte 8 (the mode). On the disk a password is kept as CP/M 3 keeps one, so
cpmtools reads it: in bytes 16–23 of the XFCB or label, in reverse order, each byte XORed with byte 13, which is
the 8 bytes' sum ([hw/oodos.md, 3b1](../../hw/oodos.md#the-bdos-phase-3)).

## Extended FCBs

A file's password and protection mode are kept in an XFCB, an extended FCB: a directory entry of its own, beside the
file's, with the user byte 16 + the file's user and the file's name and type. Its byte 12 is the mode (bit 7 read,
6 write, 5 delete), byte 13 the password's key and bytes 16–23 the password. Function 103 makes or changes one,
function 102 reads its mode, and function 22 with f6' makes one with the file.

An XFCB goes with its file: delete (19) deletes it with the file's extents (and with f5' deletes only it, leaving
the file unprotected), and rename (23) renames it. The mode is copied too into the date stamps' SFCB slot of the
file's extent-zero entry, as CP/M 3 keeps it, where cpmtools's `cpmls -F` shows it.

An XFCB whose file is gone, as a system without XFCBs can leave, is left alone (ours): Make with f6' takes it over,
without its old password.

## Directory labels

A drive's directory label is a directory entry with the user byte `$20`: a name and type for the drive (bytes
1–11), a mode byte (12), a password (13, 16–23) and the label's own creation and update stamps (24–31). It is the
first `$20` entry the directory has; a drive without one has no stamps and no passwords.

The mode byte's bits:

| Bit | What |
|---|---|
| 7 | passwords on: XFCBs protect files |
| 6 | access stamps (in the first stamp field, shared with creation) |
| 5 | update stamps |
| 4 | creation stamps |
| 0 | a label is there (OODOS sets it) |

OODOS's drives are made with a label of mode `$31`: creation and update stamps, no passwords. INITDIR makes the
same by default; a drive made by cpmtools's `mkfs.cpm -t` has `$11` until its byte 12 is set.

Function 101 reads the mode, and function 100 makes the label or changes it; the drive's stamps and password checks
follow a new mode from that call on. Stamps need SFCBs (every fourth directory entry, as `mkfs.cpm -t` or INITDIR
makes them): asking for them on a directory without is refused. A label may have a password of its own, which
function 100 then needs whatever bit 7 says.

## Date stamps

A file can carry two stamps: one for its creation or its last access (the two share a field), and one for its last
update. They are kept in the SFCB that follows each three directory entries, in the slot of the file's extent-zero
entry, as CP/M 3 keeps them; the label's mode says which are made.

Each stamp is CP/M 3's date and time, 4 bytes: the day as a word, 1 January 1978 being day 1, then the hour and the
minute in BCD. Functions 104 and 105 set and read the clock in the same form.

- **Creation:** made by Make (22) of extent 0, when the mode asks.
- **Update:** made by Make of extent 0, by the first write (21, 34, 40) since the file was opened, and by truncate
  (99), when the mode asks. Only once an open: the writes after it, a close and a move to another extent make none,
  and a write after Make of extent 0 none either, Make having made it.
- **Access:** made by Open (15) of extent 0 on a drive that isn't read-only, when the mode asks. It overwrites the
  creation stamp.

Function 102 returns a file's stamps, zeros for one not made. cpmtools stamps every extent of a file it writes;
OODOS reads only the first extent's, as cpmtools does.

## Random records

A file is a run of 128-byte records, numbered from 0. Besides reading and writing in order (20, 21), a program can
name a record: FCB bytes 33–35, R0–R2, hold its number, R0 the low byte. R2 is at most 3, so a file reaches 262,144
records, 32 MB.

- **33** reads the record R0–R2 names, and **34** writes it; R0–R2 doesn't move on. **40** writes as 34 does; on
  OODOS every block a write takes is zeroed anyway, so the two are the same (CP/M 3 leaves such records undefined).
- **35** puts the file's size in R0–R2: the number of the record after its last.
- **36** puts in R0–R2 the record a sequential read or write would do next.
- **99** cuts a file after the record R0–R2 names.

A random read or write sets the FCB's extent and CR to its record, so a sequential call after it goes on from there.
A random write can make extents out of order, and leaves holes where nothing was written. Reading a record there
gives one of three answers:

- **zeros, and 0,** for a record below the extent's RC in a block a write took: every block a write takes is
  zeroed first;
- **1,** for a record in a block no write took, or at or past the extent's RC;
- **4,** for a record in an extent with no directory entry.

Any of them can instead be **3** when the read must move to another extent and the one the FCB is at can't be
closed first.

Past 32 MB, 6. A sequential read (function 20) stops with 1 at the first record in a block no write took, even
when the file has data after it.

## Multi-sector I/O

Functions 20, 21, 33, 34 and 40 move as many records a call as the multi-sector count says, 1 to 128: function
44 sets it (and 49 reaches it in the SCB at `$4A`). The CCP sets it to 1 at each warm boot.

With a count of N, the records go to or from the DMA address, the DMA address + 128, and so on to + 128 × (N − 1);
the DMA address itself doesn't change. The FCB is read once and written back once, so N records a call cost less
than N calls: on M:, 4,321 cycles a record read 128 at a time, against 4,723 a call for one.

- The call stops at the first record that doesn't succeed. The error is returned as it would be for that record,
  and X says how many records were done before it (for an error other than a physical one).
- A random call (33, 34, 40) puts R0–R2 back to the first record's.
- Before the first record, the BDOS checks that the whole span is the program's memory: not past `$FFFF`, not in the
  I/O hole, not on flash, and for a read not in a read-only window. If any of it isn't, A = X = `$FF` and nothing is
  done (ours: a record at a time would have done those before it).

With a DMA segment (function 51) the span is in physical memory, and the same check is made there
([chapter 5](05-memory-and-banking.md#the-dma-segment)).

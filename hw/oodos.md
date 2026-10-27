# OODOS: One Off DOS #

Draft, 2026-10-26. The plan for the OO-1's disk operating system: CP/M Plus (CP/M 3) written again for the
W65C02S and the backplane. As in [design.md](design.md), each section separates what is **decided** from what is
**proposed**; open questions are collected at the end.


## Goals ##

**Decided**

- **The CP/M 3 interface, kept exactly where it can be:**
  - the on-disk format: DPBs, extents, directory labels, SFCB date stamps, password entries, disks up to 512 MB and
    files up to 32 MB;
  - the BDOS function numbers and what they do;
  - the banked structure: the BDOS deblocks, the BIOS reads and writes physical sectors only, and RSXs chain on the
    BDOS entry.
- **cpmtools interoperability with Linux:** an OODOS drive is an image file that `mkfs.cpm`, `cpmls`, `cpmcp` and
  `fsck.cpm` work on with a diskdef from this repo.
- **Apple II disks:** OODOS reads and writes them, in the real drive and as image files.
- **RSXs,** loaded and chained as CP/M 3 does.
- **Banked,** using the north bridge's windows and the 1 MB.
- **The first application is GW-BASIC,** ported from Microsoft's 1983 source (MIT licence, released 2020).
- **The name is OODOS.** It's a new implementation written from Digital Research's CP/M 3 manuals: no DR source
  or binaries go into this repo or the website.

**Decided (2026-10-26): no 8080 or Z80 CP/M programs,** now or by a later card. The CPU is different; OODOS
programs are 6502 programs written to the same interface.


## Disk format ##

**Decided: CP/M 3's, unchanged.** A drive is described by a DPB as CP/M 3 defines it (SPT, BSH, BLM, EXM, DSM, DRM,
AL0/AL1, CKS, OFF, PSH, PHM), and the directory holds CP/M 3's entries: files, XFCBs (passwords), the directory
label, and an SFCB in every fourth entry when the drive has date stamps.

- **Date stamps:** two per file (creation or access, and update), each 2 bytes of days from 1 January 1978, then the
  hour and minute in BCD. The directory label chooses which are kept, as in CP/M 3.
- **Decided (2026-10-26): creation and update stamps.** OODOS drives have the directory label's mode byte (entry
  0, byte 12) at 0x31: a label, creation stamps, update stamps; no access stamps, no passwords. INITDIR's default
  is the same. `mkfs.cpm -t` writes 0x11 (creation only), so a drive made on Linux has the byte set after it.
- **Directory hashing** is CP/M 3's, kept in memory only (the DPH's HASH table, 4 bytes an entry). It never
  touches the disk, so it costs interoperability nothing. With 1 MB it's on for every drive.
- **cpmtools.** The repo carries [`oodos/diskdefs`](../oodos/diskdefs) with OODOS's formats, so a card is made on
  Linux with, for example ([oodos/README.md](../oodos/README.md) has more):

  ```
  mkfs.cpm -f oodos-8m -t -L OODOS A.IMG
  printf '\061' | dd of=A.IMG bs=1 seek=12 conv=notrunc
  cpmcp -f oodos-8m A.IMG HELLO.TXT 0:
  cpmls -f oodos-8m -F A.IMG
  ```

**Decided (phase 0, 2026-10-26): the formats**

| diskdef | Sector | Size | Block | Directory | Boot tracks | For |
|---|---|---|---|---|---|---|
| `oodos-8m` | 512 | 8 MB (1024 tracks of 16) | 4 KB | 1024 | 0 | the usual SD drive |
| `oodos-32m` | 512 | 32 MB | 8 KB | 2048 | 0 | a large SD drive |
| `oodos-ram` | 512 | 128 KB, banks C–D | 2 KB | 256 | 0 | M:, made at boot, never on a card |
| `apple-do` | 256 | 140 KB, 35 × 16 | 1 KB | 64 | 3 | Apple CP/M (SoftCard) disks, cpmtools's own diskdef |

- No boot tracks on SD drives: the loader is `/BOOT.BIN` on the FAT card (below), so a drive image is all file
  system.
- `apple-do` is a CP/M 2.2 format. CP/M 3 reads and writes those; they just have no date stamps.
- **Checked with cpmtools 2.23** ([oodos/test/check_diskdefs.sh](../oodos/test/check_diskdefs.sh)): `mkfs.cpm -t`
  writes a directory label and an SFCB in every fourth entry, the label's mode is set to 0x31 (creation and update
  stamps, as INITDIR sets it) and checked, `cpmcp` stamps what it writes, `cpmls -F` reads the stamps back, and
  `fsck.cpm` passes, for all three formats; and each DPB is legal (at most 16 directory blocks, DSM within 16 bits,
  under 256 for 1 KB blocks). cpmtools writes both stamps whatever the mode, so only OODOS's BDOS (phase 3)
  will honour it. Each reading the other's drives is tested both ways from phase 1 ([Testing](#testing)).


## Drives ##

**Proposed**

| Drive | What |
|---|---|
| A–G | SD units 1–7: image files on the FAT card, mounted with MOUNT. A is the system drive |
| M | the RAM disk, in banks C–D (128 KB) |
| O, P | the Apple drives 1 and 2, through the Disk II card |

Which image each SD drive holds is set by a file on the card, `/OODOS/DRIVES.TXT`, read by the loader, and can be
changed while running by a utility that does MOUNT and resets the drive.

**Decided (2026-10-26): `DRIVES.TXT`** (the test card has one, [oodos/card/DRIVES.TXT](../oodos/card/DRIVES.TXT)):

```
# A is the system drive.
A: /OODOS/A.IMG
B: /OODOS/WORK.IMG
c:	/DISKS/CPM.DSK  ro      # read-only
```

Each line is one of these, and anything else is an error:

| Line | What |
|---|---|
| blank | nothing, or only spaces and tabs |
| a comment | optional spaces and tabs, then `#` and anything to the end of the line |
| a drive | optional spaces and tabs; a drive letter, `A`–`G` in either case (SD units 1–7); `:`; one or more spaces or tabs; the image's path; optionally one or more spaces or tabs and `RO` in any case; optionally spaces and tabs; optionally `#` and a comment |

- **The path** is the image's as MOUNT takes it: from the card's root, so it starts with `/`; `/`-separated, long
  or 8.3 names in any case (fat_volume.h). It has no spaces, tabs, `#` or control characters, and is at most 509
  bytes, what MOUNT's sector holds after the unit and the flags with its NUL. **Decided (2026-10-27):** no `//`,
  `/./` or `/../` in it, which would give one image two paths; as built, no name in it is empty, `.` or `..`, so a
  `/` at the end is refused too (Proposed).
- **Lines end** in LF or CR LF, so the file can be edited on any PC; the last may have no ending. A CR anywhere
  else, in a comment too, is an error. A UTF-8 byte order mark at the start of the file is skipped. The file is
  read to its size (LIST gives it), not to a NUL.
- **Errors** stop the loader, which says which line, by number, and why, and goes back to the monitor: a line
  that fits none of the forms (a word after the path that isn't `RO`, such as a mistyped `R0`, or a second `RO`;
  a space in the path), a drive named twice (not the later line winning), and the same path on two drives
  (compared in any case, as FAT names are). **Decided (2026-10-27):** once they're mounted, two drives whose files
  IDENTIFY's units table names the same (its 24 bytes of the name) are refused too, by the later line: an 8.3
  alias of a long name passes the paths' compare. The table is all the card says of a unit, so two images of the
  same name in different directories are refused as well: give them different names.
- A drive not named isn't mounted. Without an A line, or with a path MOUNT can't find, the loader says which and
  goes back to the monitor.


## Memory ##

### What a program sees ###

**Proposed**

| CPU range | Window | What |
|---|---|---|
| `$0000–$00FF` | W0 | zero page (below) |
| `$0100–$01FF` | W0 | the program's stack |
| `$0200–$02FF` | W0 | the **base page**: CP/M's page 0, moved up a page with the same offsets |
| `$0300`–top | W0–W2 | the TPA, about 47 KB, less any RSXs at its top |
| `$BFFD–$BFFF` | W2 | the BDOS's entry: a `JMP` to the stub in common memory, the TPA's top (phase 2, below) |
| `$C000–$C7FF` | — | the I/O hole, always there |
| `$C800–$F7FF` | W3 | the **code window** (12 KB): OODOS's resident code, or a program's banked code during a far call |
| `$F800–$FFFF` | W3 | **common memory** (2 KB): the BDOS entry stub, the BIOS jump table, the far call table, interrupt entry |

**The base page** keeps CP/M's offsets, so ported code keeps its constants:

| Address | CP/M | What |
|---|---|---|
| `$0200` | `$0000` | `JMP` warm boot |
| `$0203` | `$0005` | `JMP` to the BDOS, or the top RSX. Its operand, `$0204–$0205`, is the top of the TPA + 1, as CP/M's address 6 |
| `$025C` | `$005C` | the default FCB |
| `$026C` | `$006C` | the second FCB |
| `$0280` | `$0080` | the default DMA buffer and the command tail |

**Calling the BDOS:** `JSR $0203` with the function in Y and the parameter in A (low) and X (high). The result comes
back in A (low) and X (high), as CP/M 3 returns it in A and HL.

### Zero page ###

**Decided (2026-10-26): a fixed split.**

- **`$00–$EF` belongs to the program,** at fixed addresses. The BDOS and BIOS never touch it, because they run on
  their own zero page (below).
- **`$F0–$FF` is for RSXs.** The loader gives each RSX its own slice, through the RSX's zero page relocation
  bitmap ([below](#rsxs-and-programs)), so RSXs never share bytes and needn't save them before passing a call down
  the chain. An RSX that doesn't fit in what's left is refused, not overlapped.
- **Interrupt handlers use none of the program's zero page.** They work with absolute and indexed addressing in
  common memory; a driver that needs `(zp),Y` swaps W0 to the system's zero page, as the BDOS and BIOS do.
- **Why not the loader handing out zero page to programs too** (as CP/M-65 does): it costs nothing at run time,
  but needs every program to be relocatable, about 80 ms to load a 24 KB one against 4 ms, and gains nothing on a
  65C816 (below), whose direct page register does the same in hardware.

### Banking ###

**Proposed physical layout**

| Where | What |
|---|---|
| Block 60 | the banked BDOS |
| Block 61 | the OODOS BIOS and drivers |
| Block 62 | W3 while a program runs: OODOS's resident code and common memory; VEC points here too |
| Block 63 | the ROM BIOS and Monitor, as now, so Break still reaches the monitor |
| Block 31 (the top of bank 7) | the system's zero page, stack and data |
| Rest of banks 6–7 | deblocking buffers (BCBs), directory buffers, hash tables |
| Banks C–D | the M: RAM disk |
| Banks 1–5 | blocks programs can ask for (GW-BASIC's banked code, below) |

**Common memory is in every block W3 maps.** Whatever block is in W3, its top 2 KB is the same copy of common
memory, put there by OODOS when the block is allocated for code. So the BDOS entry, the interrupt entry and the far
call table are always at the same addresses, and a far call can swap W3 without losing them. Common memory keeps
no state of its own: that lives in the system's zero page block.

**Entering the BDOS.** The stub in common memory saves the program's stack pointer and window registers, points
W0 at the system's zero page and stack, and W1–W2 at the BDOS and BIOS. On the way out it puts them back and does
RTS. That's a handful of cycles at 7 MHz.

**The program's memory from the BDOS.** FCB and DMA addresses are the program's CPU addresses. The BDOS turns
them into physical ones through the saved window registers and moves data with the DMA controller's memory to
memory copy (CP/M 3's `MOVE` and `XMOVE`), which runs as a block (BIOS 0.9). Sectors come from the SD card by DMA
straight into a buffer's physical address and are never mapped into the CPU's view.

**Allocating blocks.** CP/M 3 has no memory manager, so OODOS adds CP/M-86's (functions 51–58, under
[BDOS](#bdos)), handing out 16 KB blocks. Calling code in a block goes through common memory, not the BDOS (see
[Far calls](#far-calls)). GW-BASIC is the first user.


## BDOS ##

**Decided (2026-10-26): CP/M 3's functions, banked, with CP/M-86's memory functions (51–58) and OODOS's own from
72.** The details below are proposed. Numbers follow
Digital Research's systems wherever one has the function; no number means one thing here and another there.

### Registers ###

- **In:** the function in Y. A byte parameter (CP/M's E) in A; a word parameter (CP/M's DE) in A (low) and X (high).
- **Out:** CP/M's A and L in A, its B and H in X. A word result is A (low) and X (high), as CP/M 3's HL.
- **Errors** follow CP/M 3: A = `$FF` and X = the physical error code (CP/M 3's H) when the error mode
  (function 45) returns errors.
- Y and the flags are not kept. The decimal flag is clear on return.

### CP/M 3's functions ###

All of these do what CP/M 3's do, with the registers above.

| Numbers | What |
|---|---|
| 0–37 | the CP/M 2.2 set, with CP/M 3's changes: 7 and 8 are AUX status, 12 returns `$0031`, 35 returns errors |
| 40 | write random with zero fill |
| 42, 43 | lock and unlock record: succeed and do nothing, as CP/M 3 |
| 44–50 | multi-sector count, error mode, free space, chain, flush, the SCB, direct BIOS calls |
| 59, 60 | load overlay, call an RSX |
| 98–112 | free blocks, truncate, directory labels, date stamps and XFCBs, date and time, passwords, serial number, return code, console mode, output delimiter, print and list block |
| 152 | parse filename |

Not in CP/M 3, and not here: 38, 39 and 41 (MP/M's drive access and test-and-write).

**Where the 6502 changes things:**

- **12** returns `$0031`, as CP/M 3. Programs that need OODOS itself use function 72.
- **27** (allocation vector): in banked CP/M 3 the vector is in the system bank, and here it is too. It returns
  `$FFFF`; function 46 gives the free space.
- **31** returns the address of a copy of the DPB in common memory.
- **49:** the SCB keeps CP/M 3's offsets for every field CP/M 3 documents.
- **50:** the BIOS parameter block is OODOS's: +0 the BIOS function, +1 A, +2 X, +3 Y, +4–5 and +6–7 two words
  (for MOVE and XMOVE's addresses and counts).
- **59** loads an overlay at the FCB's random record address, which must be a page's start, or the call is
  refused. A `.PRL` is relocated there; a `.COM` loads only where it was linked. An overlay has no zero page of
  its own (its header's zero page size must be 0, or it's refused): it uses its caller's, by importing the
  caller's zero page symbols when it's linked ([The .PRL file](#the-prl-file)).
- **104, 105** use CP/M 3's DAT (days from 1 January 1978, hour and minute in BCD); 105 returns the seconds, BCD, in
  A. Both go to the DS3231.

### Memory: CP/M-86's functions ###

CP/M 3 has no memory manager. CP/M-86's suits the OO-1 exactly: it works in paragraphs of 16 bytes, so a 16-bit
paragraph number names any byte boundary of the 1 MB, as an 8086 segment does (and as GW-BASIC's DEF SEG
assumes).

| No. | CP/M-86 name | What |
|---|---|---|
| 51 | F_DMASEG | set the DMA "segment": the paragraph the DMA address counts from, so a read or write reaches any physical memory. `$FFFF`, the default, means the DMA address is the program's CPU address, as in CP/M 3 |
| 52 | F_DMAGET | return the DMA segment and address |
| 53 | MC_MAX | the largest free run of memory, in an MCB |
| 54 | MC_ABSMAX | the same, at a given base |
| 55 | MC_ALLOC | allocate memory: the MCB's length in, its base out |
| 56 | MC_ABSALLOC | allocate memory at a given base |
| 57 | MC_FREE | free one allocation |
| 58 | MC_ALLFREE | free everything the program allocated |

- **The MCB** is CP/M-86's: base (a paragraph, 2 bytes), length (paragraphs, 2 bytes), and an extra byte.
- **Granularity is a 16 KB block,** 1024 paragraphs: lengths round up, bases are block boundaries. Only the blocks
  that are user memory (banks 1–5 and what's left of the others) are handed out.
- **Warm boot frees** everything a program allocated, as 58 does, so a program that stops early leaks nothing.
- MP/M's memory functions (128–130) were the alternative. They describe memory segments of a multi-process
  system, and fit less well.

### OODOS's own ###

From 72, a range no Digital Research system uses (CP/Net has 64–71).

| No. | What |
|---|---|
| 72 | OODOS's version, the BIOS's version, the memory fitted and free, and the address of the far call table |
| 73 | translate a CPU address in the program's map to a physical address (the paragraph and the offset), for programs that program the DMA controller themselves |
| 74 | mount an SD image as a drive, or unmount one: a drive and a path, as MOUNT does |
| 75–97 | reserved for OODOS |

### Far calls ###

**Proposed.** Calling code in an allocated block can't wait for a BDOS call; GW-BASIC makes thousands a second. So
common memory holds a small table, found through function 72, beside the BIOS's jump table:

- **FARCALL:** map a block into W3's code window (the program names the block and an address in `$C800–$F7FF`),
  JSR there, and put W3 back on the way out. The block's code may call FARCALL again; the table keeps a stack of
  the blocks. The program's own memory, `$0000–$BFFF`, stays mapped throughout, so banked code sees all of it.
- A block allocated for code gets common memory copied into its top 2 KB, so 12 KB of each is the program's.
- **FARPEEK, FARPOKE, FARMOVE:** bytes and runs of bytes at physical addresses, for DEF SEG, PEEK, POKE, BLOAD and
  BSAVE. FARMOVE uses the DMA controller.


## BIOS ##

**Proposed.** The 6502 BIOS has CP/M 3's entry points in a jump table in common memory: BOOT, WBOOT, CONST,
CONIN, CONOUT, LIST, AUXOUT, AUXIN, HOME, SELDSK, SETTRK, SETSEC, SETDMA, READ, WRITE, LISTST, SECTRN,
CONOST, AUXIST, AUXOST, DEVTBL, DEVINI, DRVTBL, MULTIO, FLUSH, MOVE, TIME, SELMEM, SETBNK, XMOVE.

- **SD drives:** LBA = track × SPT + sector; a READ programs DMA channel 0 to the buffer's physical address and
  issues the SD card's READ. About a hundred lines.
- **Console:** the video card's 80×30 text screen and the USB keyboard. AUX is COM1 (or COM2), LST the parallel
  port. CP/M 3's device table lets DEVICE move them.
- **TIME:** reads and sets the DS3231.

**Proposed (phase 2)**

- **Drives A–G** are SD units 1–7 as mounted: 16,384 sectors is `oodos-8m`, 65,536 `oodos-32m`. A read-only
  unit's WRITE returns 2, as CP/M 3's write-protect error. SELDSK gives a DPH with the DPB and no translation;
  its buffers (CSV, ALV, BCBs, HASH) come with the BDOS in phase 3.
- **M:** `oodos-ram` in banks C–D (`$C0000–$DFFFF`), READ and WRITE by DMA memory-to-memory copies. Cold boot
  formats it as `mkfs.cpm -t` would, with the label's mode 0x31.
- **SETBNK** names whose memory SETDMA's address is in: 0 the system's view, 1 the program's (through the windows
  the BDOS stub saved). The BIOS turns either into a physical address for the DMA.
- **The devices:** CRT (the text screen and the USB keyboard), TTY (the serial card, the 6551 for now) and LPT
  (nothing yet: no parallel port in the emulator; it takes and drops). CON: is CRT and TTY, in and out, AUX: is
  TTY, LST: is LPT.
- **The terminal** is a VT52 subset: CR, LF, BS, BEL, `ESC A B C D` (cursor), `ESC H` (home), `ESC J` and `ESC K`
  (erase to the end of the screen and of the line), `ESC E` (clear) and `ESC Y` row column (each + 32).
  Lowercase stays lowercase.
- **TIME** with A = 0 reads the bridge into the SCB's date (days from 1 January 1978, day 1 that day), hour,
  minute and second (BCD); with A = `$FF` it sets the bridge from them, the day of the week worked out.
- **Interrupts:** none enabled in phase 2; the console is polled. BRK, and Break (NMI), go to the ROM's monitor
  as BIOS 0.9's Break does (W3 and VEC back to block 63). Any other IRQ returns.
- **Apple drives:** below.

**Proposed (phase 2, as built, 2026-10-27)** (`oodos/sys/bios.s`, `crt.s`):

- **Registers:** CP/M 3's C or E in A, BC in A/X, and MOVE's DE and HL (SECTRN's table too) in two words of the
  system's zero page, function 50's +4–5 and +6–7; a result in A, or A/X for HL. `bios.s`'s header has every
  entry. SELDSK's E (bit 0: selected before) is X; a first select reads IDENTIFY's units table again, so a unit
  mounted since shows up. As CP/M 3's: MOVE copies BC bytes from DE (+4–5, the source) to HL (+6–7, the
  destination) and returns HL past them; XMOVE's C, the source bank, is A, and its B, the destination bank, is X.
- **Banks:** 0 is the system's view (31, 60, 61, 62; from 3a W2 the block its caller had there, `CALLW2`, below),
  1 the program's (the windows the stub saved); any other is taken as 0. SETBNK names SETDMA's; SELMEM sets the bank MOVE copies within, and moves no window (the BDOS reaches
  the program's memory by physical address, never by switching under itself); XMOVE's two banks are for the next
  MOVE only. Cold boot and every warm boot set SETBNK's and SELMEM's banks to 0 and drop a pending XMOVE, so
  nothing a program left reaches the next.
- **What isn't memory:** an address in a window's I/O hole (`$C000–$C7FF`), a window on flash (`NB_ROM`), or a
  read-only window as the destination, has no physical address the BIOS may use: READ and WRITE return 1 and MOVE
  moves nothing and returns A = X = `$FF`, all checked before a byte moves. The BDOS's functions stop at a program
  address in the hole and return A = X = `$FF`.
- **The DPBs:** worked out from `oodos/diskdefs`, with CKS `$8000` (a drive not changed under the system: no
  checksums), OFF 0, PSH 2 and PHM 3. SELDSK's DPHs have no translation and no buffers until phase 3.
- **READ and WRITE** check the sector (0–15) and the drive, then move one sector by DMA (channel 0 for the card,
  3 for M:); a sector past a drive's end, a card error, or no card returns 1, and the card's own write-protect
  (a unit made read-only since the units table was read) returns 2 as the table's flag does. MULTIO takes the count
  and READ and WRITE still do one sector each; FLUSH has nothing to write out.
- **The card's commands** wait for it to be idle first (a command written while BUSY is ignored), and give a
  command about a second to end (an SD card may stay busy hundreds of milliseconds programming a write); then CTRL's
  RESET abandons it, the units staying mounted, and READ or WRITE returns 1. A command that ends without channel 0
  at its terminal count (the 512 bytes not all moved) returns 1 too. The units table's IDENTIFY waits the same way.
- **M:'s label** is `mkfs.cpm -t`'s, UNLABELED, mode 0x31, both stamps the time cold boot read; the data area is
  left as it is, as mkfs.cpm leaves it.
- **TIME:** the bridge holds 2000–2199 and the SCB's days reach 2155. Leap years are the DS3231's, every fourth, so
  2100 has a 29 February in both and they always agree; a date before 2000 or past 2155 leaves the clock as it was.
  A clock past 2155 reads as no time: TIME returns carry set and leaves the SCB as it was, cold boot's banner shows
  no date, and function 105 returns A = X = `$FF`. Setting it keeps RTCCTL's TIE and waits for BUSY to clear.
- **The devices:** DEVTBL is `CRT` (input and output), `TTY` (serial, 19,200: code 15) and `LPT` (output). The SCB's
  vectors start with CON: in and out on CRT and TTY, AUX: on TTY, LST: on LPT. TTY waits out a character after each
  (the 6551's stuck transmit flag), as the ROM's ECHO does; LPT's input is ^Z.
- **The screen:** cold boot sets the font (from flash block 30), the palette and the card's registers as the ROM's
  VINIT does. The cursor is the cell's attribute while it's there, black on light grey and blinking, so the
  character under it stays; a character in the last column wraps; ESC E clears and homes; ESC Y past the edge
  stops at it; BEL and DEL do nothing for now.


## Real-time clock ##

**Decided: a DS3231 on the CPU card.** Date stamps need the time.

**Decided (2026-10-26): the north bridge owns it** ([design.md](design.md#real-time-clock)).

- The bridge keeps the time in registers, `$C690–$C696`, in the DS3231's BCD; reading the seconds latches them all.
  TIME is a handful of loads, and setting the time is seven stores and SET. No I²C in the BIOS.
- A CR2032 keeps it running with the power off.
- RTCCTL's TICK, once a second, can be an interrupt (bit 5 of IRQH, ID `0xD`) for a seconds tick; alarms set it too.
- The emulator counts it in emulated time, from the host's clock at power-on; the WebUI steps it back into line
  with the host's clock when it falls 2 s out (design.md).


## Loading ##

**Proposed**

1. The BIOS's `C80CR` boots `/BOOT.BIN` from the FAT card, as now. For OODOS that's **OOLDR**, a loader in the
   spirit of CPMLDR.
2. OOLDR mounts the drives from `/OODOS/DRIVES.TXT` and reads **`OODOS.SYS`** from drive A's own CP/M file system,
   with a small read-only BDOS of its own.
3. It puts the banked BDOS, the BIOS and common memory in their blocks, sets the windows and VEC, and starts the
   BDOS's cold boot, which runs the CCP.

A machine with no OODOS card still boots to the monitor as now.


## The system, phase 2 ##

**Decided (2026-10-27)**

- **Until phase 3's BDOS, OODOS.SYS has a stand-in BDOS and CCP:** the console, version, time and direct BIOS
  functions and 72, and an `A>` prompt that runs `NAME.COM` from drive A, user 0. Phase 3 replaces both.
- **Common memory has the full FARCALL** (the spike's), and the BDOS stub saves and restores W3 as well as W0–W2,
  so a home block stays possible ([Open Questions](#open-questions)).
- **M:, the RAM disk, is the BIOS's from phase 2.**

**Proposed (phase 2)**

**The windows.** A program's memory, `$0000–$BFFF`, is blocks 0–2; the system's is block 31.

| | A program running | In the BDOS or the BIOS |
|---|---|---|
| W0 `$0000–$3FFF` | block 0: its zero page, stack, base page, TPA | block 31: the system's zero page, stack, data and the SCB |
| W1 `$4000–$7FFF` | block 1 | block 60: the BDOS |
| W2 `$8000–$BFFF` | block 2 | block 61: the BIOS |
| W3 `$C000–$FFFF` | block 62, or a far call's block | block 62: OODOS's resident code and common memory |
| VEC | block 62 | block 62 |

- **The BDOS's entry** is a `JMP` at `$BFFD`, the last three bytes of the TPA, to the stub in common memory, and
  `$0203` jumps there: so `$0204` is `$BFFD`, the top of the TPA + 1, as CP/M's address 6 is. RSXs load below it.
  The TPA is `$0300–$BFFC`, 48,381 bytes. (A `JMP $F8xx` at `$0203` would make `$0204` claim the I/O hole and
  the code window.)
- **The BDOS stub** saves the program's S, P and WIN0–WIN3, switches to the system's windows and stack, calls the
  BDOS, and puts them back. With interrupts off while it switches; what it keeps before the switch goes on the
  program's own stack, so common memory is never written and may be read-only in a program's W3.
- **The program's memory from the system:** through the saved windows, as physical addresses, by DMA
  (MOVE/XMOVE, or a byte at a time where that's all it is).
- **Programs call the BIOS through function 50 only.** The jump table in common memory is the BDOS's.

**Common memory** (`$F800–$FFFF`, the same in every block W3 maps) holds:

| Address | What |
|---|---|
| `$F800` | the BIOS jump table: CP/M 3's 30 entries, in CP/M 3's order, 3 bytes each |
| after it | the BDOS stub, the warm boot entry, FARCALL, the interrupt and NMI entries |
| `$FFFA–$FFFF` | the NMI, reset and IRQ vectors (VEC is block 62) |

The addresses after the jump table aren't fixed: programs find FARCALL through function 72.

**OODOS.SYS** is a file on drive A, user 0. Its first 128-byte record is a header:

| Offset | What |
|---|---|
| +0 | the magic number `4F 4F 53 59` ("OOSY") |
| +4 | OODOS's version, major then minor. OOLDR refuses a major version it doesn't know |
| +6 | the cold boot's entry, a CPU address in block 62 |
| +8 | the number of segments, 1–8 |
| +9–+15 | reserved, 0 |
| +16 | 8 bytes a segment: its physical address (3 bytes), its length in bytes (2, at most 16 KB), the record of the file it starts at (2), reserved (1) |

Each segment's bytes start on a record. A segment must lie inside block 31 or blocks 60–62 and not overlap
another, or OOLDR refuses the file.

**OOLDR** is `/BOOT.BIN`, loaded at `$1000` by the BIOS's `C80CR`:

1. It reads `/OODOS/DRIVES.TXT` (its size from LIST of `/OODOS`, its bytes through a unit mounted for the
   purpose) and checks it all by the grammar above before mounting anything.
2. It mounts each drive line's image as its unit (A is 1, so `/BOOT.BIN` is replaced, being in memory). An
   image must be 16,384 sectors (`oodos-8m`) or 65,536 (`oodos-32m`); any other size, or a path MOUNT can't
   find, stops it with the line's number.
3. It finds OODOS.SYS in drive A's directory, user 0, reads it by its extents, checks the header and puts each
   segment at its physical address.
4. It sets W1, W2, W3 and VEC to blocks 60, 61, 62 and 62 (W0 is still block 0), interrupts off, decimal off,
   and jumps to the cold boot's entry.

Any failure says what, and returns to the monitor. The BIOS learns the drives from the card itself: IDENTIFY's
units table gives each unit's sectors and read-only flag, so nothing is handed over.

**Proposed (phase 2): OOLDR's details,** as [`oodos/ldr/ooldr.s`](../oodos/ldr/ooldr.s) has them:

- **The CP/M reader** is [`oodos/sys/cpmread.s`](../oodos/sys/cpmread.s), linked into OOLDR and, later, the stand-in
  CCP: CR_INIT (a unit; its size picks `oodos-8m` or `oodos-32m`), CR_FIND (an 8 + 3 name, user 0), CR_SEEK (a
  record: OODOS.SYS's segments start where the header says, not in order) and CR_READ (the next record to a CPU
  address). Carry and a code in A on failure, the end of the file told apart. It reads through DATA by CPU loop,
  never DMA, and has no fixed addresses: its zero page and buffers are segments (`CRZP`, 4 bytes; `CRBSS`, about
  1 KB) the linker config places. A file may have 16 directory entries (512 KB on `oodos-8m`), each but the last
  full; an entry missing, twice, part full or naming a block outside the data area is a damaged directory.
- **DRIVES.TXT:** a CR at the very end, with no LF after it, is a CR not before LF. A file of 16 MB or more is
  refused (DRIVES.TXT IS TOO BIG). The drives are mounted in order, A to G, each with its size checked as it's
  mounted, then the units table is read for two drives on one file. Units 2–7 that no line names are unmounted
  first, so a unit left from an earlier boot doesn't become a drive.
- **The header:** the entry must be in `$C800–$FFFF` (block 62 through W3, past the I/O hole) and in a segment;
  each segment must not be empty, must start after the header (record 1 on) and lie inside the file (its record
  and length within the file's records); a segment may run from one of blocks 60–62 into the next; the reserved
  bytes aren't looked at, by OOLDR or by `mksys.py --check`, which refuses exactly what OOLDR does (so a later
  minor version may use them). Everything is checked before anything is loaded.
- **The card:** every wait for it, OOLDR's and the CP/M reader's, is bounded at about a second; then the command is
  abandoned (CTRL's RESET, units staying mounted), OOLDR says SD TIMEOUT, and the reader returns `CRE_TIMEOUT`
  (`system.inc`), which the CCP shows as READ ERROR. A drive the reader finds gone from the units table, or of
  neither size, as OOLDR goes to read OODOS.SYS, is said by its line, as when mounting.
- **Loading** maps W1 to a segment's first block and W2 to the next, and copies each record through a buffer, so a
  last part record writes only the segment's bytes. On a failure W1 and W2 go back to blocks 1 and 2.

**The stand-in BDOS** (block 60) has functions 0, 1, 2, 6, 9, 10, 11, 12, 25, 50, 72, 104 and 105, as CP/M 3's
(and 72 as above); any other returns A = X = `$FF`. **The stand-in CCP** prompts `A>`, reads a line (function
10), and runs `NAME.COM` from drive A, user 0, through a read-only reader shared with OOLDR: it checks the
program header's magic, loads the image at `$0300`, sets the base page and jumps there with the program's
windows and a fresh stack. An unknown name prints `NAME?`, as CP/M does. Warm boot comes back to the prompt.

**Proposed (phase 2, as built, 2026-10-27)** ([`oodos/sys/`](../oodos/sys/), [Testing](#testing)):

- **OODOS.SYS** is one ld65 link (`oodos/sys/sys.cfg`) whose parts are files of their own, made into the file by
  `oodos/sys/mksys.py`: four segments, the BDOS at `$F0000` (block 60), the BIOS at `$F4000` (61), block 62's
  resident code from `$F8800` (`$C800`) and common memory at `$FB800` (`$F800`, the whole 2 KB, to the vectors).
  Nothing is loaded into block 31: cold boot clears and sets it. Its entry is cold boot's first byte, `$C800`.
- **The system block (31)** in the BDOS's and BIOS's view: `$00–$EF` zero page, `$F0–$FF` the stub's copy of the
  program's A, X, S and WIN0–WIN3 and the BIOS's two word parameters, `$0100` the stack, the SCB at `$0200`, the
  BDOS's and BIOS's variables from `$0300`, a sector's buffer at `$0800` and the way to the monitor at `$0A00`.
- **The stub** leaves interrupts off for the whole call in phase 2 (no interrupt is enabled) and sets S to `$FF` at
  each entry. It writes nothing in common memory: on the way in it pushes A, X and Y on the program's stack after
  its P and reads them back through W1 (on the program's W0 block) once W0 is the system's; on the way out, with W1
  free once the BDOS has returned, it puts A and X there under its P and pulls them after switching back.
  Measured: 126 cycles from `JSR $0203`'s target to the BDOS's first instruction, 90 from the BDOS's return to the
  program's next instruction.
- **The program's memory from the BDOS** a byte at a time: its block, by the saved window, mapped into W2 for the
  byte and the BIOS's put back. The BIOS's sectors and MOVEs go by DMA to the physical address, a window at a time,
  so a buffer may cross windows mapped anywhere (READ and WRITE then go through the system block's buffer).
- **FARCALL** is the spike's code unchanged: 281 cycles over a JSR/RTS on the emulator, as in the spike.
- **Break and BRK:** common memory is in W3 and can't move W3 and go on, so it maps the system block into W0,
  writes four instructions there (W3 and VEC to block 63, read-only, then `JMP ($FFFA)`, block 63's NMI vector)
  and jumps to them. The monitor's BREAK puts W0–W2 back on blocks 0–2.
- **The stand-in CCP** runs in the code window as a program does, with the program's windows, zero page and stack.
  Its command word is made capitals; the tail goes to `$0280` as typed, its leading space too; the FCBs are blanked,
  not parsed (phase 4). A program starts with A, X and Y 0, D clear, interrupts off, and the warm boot's address on
  a fresh stack, so its RTS ends it. Its built-in `GO` runs the image at `$0300` whose first record is at `$0280`,
  as a loader leaves them, after the loader's checks (magic, no flags, link page `$03`, the image and BSS ending by
  `$BFFD`); the tail then goes over that header. `NAME.COM` comes from drive A, user 0, through OOLDR's reader
  (`cpmread.s`, linked into block 62) a record at a time: the header, the file's first 16 bytes, to `$0280` and
  checked before anything else is written, then exactly the image's size, which follows it in the file, to
  `$0300`; nothing past the image changes. A name it can't find is `NAME?`, one over 8 characters too; a file that
  ends inside its image, or a reader error, is `READ ERROR`.
- **The stand-in BDOS's choices:** 11 returns 1 for a waiting character (CP/M 3's); 6 with `$FE` returns `$FF` or 0;
  10 with A/X = 0 reads into the DMA address (from the SCB, `$0280` at boot) but doesn't edit what is there, takes
  BS and DEL, ^X, and ^C at the start of a line (the warm boot), ignores other control characters and echoes the
  end as CR; 2 expands a tab to the next eighth column by the SCB's column. 72 takes the address of a 10-byte block
  it fills: OODOS's version (major, minor), the BIOS's, the memory fitted and the memory for programs in KB (1,024
  and 320, banks 1–5, for 53–58 later), FARCALL's address; and returns that address in A/X. 104 sets the seconds
  to 0; 105 returns them in A, X 0. A function 50 of a BIOS function past XMOVE returns A = X = `$FF`, and so does
  one of BOOT, which would run cold boot again and format M: (a program ends by the warm boot).



## The BDOS, phase 3 ##

**Decided (2026-10-27): three parts, each reviewed and merged on its own.** 3a the file system core; 3b CP/M 3's
extras (the SCB, multi-sector I/O, labels, reading stamps, XFCBs and passwords, chain, the full console set,
107, 108, 152); 3c the memory functions 51–58 and OODOS's 73–74. **A randomized model test** comes with 3a: a model
of CP/M 3's file system, ours, in Python, written from the manuals, that random sequences of file operations run
against, compared with what the BDOS does on the emulator.

**Proposed (3a)**

- **Functions:** 13–37, 40, 42–43 (succeed, do nothing), 45, 46, 48, 98 and 99, as CP/M 3's, with the registers
  above, the stand-in's console, version, time, 50 and 72 kept. 27 returns `$FFFF`, 31 the address of a copy of
  the DPB in common memory. 44 (multi-sector) is 3b's: until then the count is 1.
- **Date stamps from 3a:** every OODOS drive's label asks for creation and update stamps (mode 0x31), so making a
  file stamps its SFCB slot's creation, and closing a written one its update, from TIME, as the label's mode says.
  Reading them (102), labels (100, 101), XFCBs and passwords are 3b's.
- **Where it lives.** The BDOS's code is block 60, and what doesn't fit goes in block 62's code window (about 10 KB is
  free there). Its variables and the drives' allocation vectors (CP/M 3's two bits a block: 1 KB at most for an
  `oodos-32m` drive) are in the system block. The hash tables (4 bytes a directory entry: 4 KB an `oodos-8m`
  drive, 8 KB an `oodos-32m`, 1 KB M:) are in blocks 24–27, and the directory and data buffers (BCBs, 512 bytes
  each) in block 28: banks 6–7, as [Banking](#banking) planned. Blocks 29–30 are spare.
- **Reaching them:** the BDOS maps a hash or buffer block into W2 while it works on it. Each entry of the BIOS's
  jump table puts W2 back on the BIOS (block 61) for the call and returns it as it was, so the BDOS needn't care
  which block W2 holds when it calls the BIOS.
- **The hash** is in memory only and ours: per entry the user (or a free/other mark), a hash of the name and the
  extent number, so a search compares four bytes before it reads the entry.
- **Deblocking:** the BDOS reads and writes 512-byte physical sectors through the BCBs, least recently used first,
  and writes a changed buffer back when it's reused, at close, function 48, a drive reset (13, 37) and a warm boot.
- **Login** (the first select after a reset): the directory is read once to build the allocation vector and the
  hash, and the label read for the stamp mode. An SD drive is fixed media (CKS `$8000`, no checksums).
- **Errors** as CP/M 3's, with the error mode of 45: by default the BDOS says `OODOS Error On A: Disk I/O` (or Read
  Only, File R/O, Invalid Drive) and the function and file, and ends the program; in return mode it returns
  A = `$FF` and X = the code (CP/M 3's H).
- **The stand-in CCP** loads `NAME.COM` through the BDOS (Open, Read), not OOLDR's reader, so every program run
  exercises it; OODOS.SYS no longer links `cpmread.s`. The real CCP is phase 4.
- **The model test:** [`oodos/test/`](../oodos/test/)'s model and a driver program (a `.COM`) that runs a script of
  operations from a file and writes what each returned; a ctest runs the same seeded scripts on the model and on
  OODOS in the emulator and compares the results and the drives' files and directories. The model's images are
  checked by cpmtools too, at verify.

**Proposed (3a, as built, 2026-10-27)** ([`oodos/sys/bdos.s`](../oodos/sys/bdos.s), [`bdosfile.s`](../oodos/sys/bdosfile.s),
[`bdosdisk.s`](../oodos/sys/bdosdisk.s); the choices the manuals leave open are ours, marked so for the model test):

- **Where it lives:** all of it fits block 60 (6,167 bytes of 16 KB), so nothing is in block 62's code window yet;
  `sys.cfg` has a `BDOS2` segment there, after the CCP's code and before its BSS, for when it doesn't. The system
  block holds its zero page (the BDOS's and the BIOS's together, 180 of the 240 bytes), its variables (the FCB's copy,
  the search's state, a table a drive, the buffers' tags: 402 bytes of `SYSBSS`'s 1,280 with the BIOS's) and the
  allocation vectors at `$1000–$2FFF`, 1 KB a drive's slot (A–G slots 0–6, M: 7); the hash is 8 KB a slot in blocks
  24–27, the buffers block 28.
- **The jump table:** every entry but BOOT and WBOOT goes through its own few instructions in common memory, which
  keep W2 as the caller had it (on the stack, and in `CALLW2`, `$FC`), put W2 on block 61, call the BIOS, and put
  W2 back, keeping A, X, Y and the carry (N and Z follow A). Bank 0's `$8000–$BFFF`, to the BIOS's READ, WRITE and
  MOVE, is `CALLW2`'s block, so the BDOS reads a sector straight into the buffer it has in W2. The BIOS calls none of
  them itself. Measured: **51 cycles** a call over the JMP alone (28 in, 23 out). Common memory holds 1,107 of its
  2,048 bytes, the BIOS's DPBs among them (function 31 returns one).
- **Buffers:** 32 sectors, directory or data, the least recently used taken (an empty one first) by a 16-bit clock.
  A directory change is written at once; a data sector when its buffer is taken, at close (the drive's), by 48, 13
  and 37, and at the warm boot (the CCP calls 48 and 98 each time, in error mode `$FF`). A write that fails drops its
  buffer, so the error isn't met again on the next flush.
- **A block a write takes is zeroed** (its sectors become new buffers of zeros, changed): records never written in
  it read as zeros, not as what the block held before, so function 40 is 34. CP/M 3 leaves them undefined.
- **Allocation:** the lowest free block, not CP/M 3's nearest to the file's last; simpler, and the same on every run.
- **Login:** SELDSK as a first select (the BIOS reads the units again), the DPB from common memory, then the whole
  directory read once through the buffers: 64 sectors on `oodos-8m`, 128 on `oodos-32m`, 16 on M:. The first label's
  mode byte decides the stamps (none without a label). A read-only unit (IDENTIFY's flag) is a read-only drive from
  its login; a unit made read-only later is when its WRITE returns 2. Files are looked for up to the last entry that
  isn't free or an SFCB (a high-water mark); free entries, and 17's every entry, to the directory's end. The DPHs'
  CSV, ALV, BCB and HASH fields stay 0: the BDOS places its own, as above.
- **The hash** is FIND's first look at each entry: its first byte, the name's 16-bit hash (each byte's low 7 bits
  XORed into a word turned left a bit before each) and the extent (EX's 5 bits, S2's low 3); then the entry is
  read and compared in full, so two names with one hash (HASHAG.TXT and HASHBA.TXT) are told apart.
- **Directory codes:** Open, Make, Close, Search, Rename and Set Attributes return the entry's place in its record,
  0–3 (the first entry's, for those that change several); Delete and Truncate 0; not found, or the directory full,
  `$FF` with X 0. A closed file that wasn't written returns 0 without looking.
- **Wildcards:** `?` in the name or type is ? in Filename (9) for 15, 22, 23 (either name), 30, 35 and 99; 17, 18
  and 19 take it. `?` as 17's drive byte: every entry of the current drive, to its end, free ones too, nothing
  compared. 18 goes on from 17's pattern, kept (not the FCB read again), until 13 or 37.
- **Make** refuses a name and extent there already (File Exists, 8); the entry gets f1'–f4' and t1'–t3' as the FCB
  has them, f5'–f8' clear, no blocks. **Open** looks in the current user, then (from another user) for user 0's
  entry with t2': opened from there, f8' set, and the file read-only (File R/O on a write; its later extents are
  user 0's too). It copies f1'–f4' and t1'–t3' from the entry, clears f7', and with f6' set gives the entry's byte
  count (S1, as 30 sets it with f6') in CR. **Delete** checks every match first (File R/O: none deleted), then
  deletes every extent and the file's XFCBs (user + 16); with f5', the XFCBs only. **Rename** takes the new name's
  low 7 bits and keeps each entry's attributes, its XFCBs renamed too. **30** sets every extent's f1'–f4' and
  t1'–t3', clears f5'–f8' on the disk, and with f6' sets S1 to CR.
- **Extents** as CP/M's: moving past one closes it (if written) and finds the next, made for a write; a read that
  finds none returns 1 (4 for 33) and leaves the FCB at the new extent, empty, so a write after it appends, its entry
  made at the close if need be. Each entry is stamped as a file: creation when it's made (by Make or as the file
  grows into it), update when it's closed after a write (by 16, or as the file moves on), so all a file's entries
  have both, as cpmtools writes them. Make leaves the update 0.
- **Truncate (99):** R0–R2 is the new last record; it must be before the file's end and in an extent with an entry,
  else `$FF`. Later extents' entries are deleted; the one with R gets R's extent, RC (R mod 128) + 1, the pointers
  after R's block freed and cleared, and the update stamp.
- **13** writes back and drops every buffer, logs every drive out and makes it read-write, A: current, the DMA
  address `$0280`; the user number stays. **37** does the first three for the drives in A/X. **28** makes the current
  drive read-only until one of them; **46** is (DSM + 1 − the blocks in use) × the records a block, from the current
  vector; **48** with `$FF` drops the buffers too; **98** makes each logged drive's current vector its permanent one.
- **Errors:** the codes are CP/M 3's (1 Disk I/O, 2 Read Only, 3 File R/O, 4 Invalid Drive, 8 File Exists, 9 ? in
  Filename). The message is `OODOS Error On A: Disk I/O`, then `BDOS Function = 15` and, for a function with an FCB,
  ` File = README  .TXT` (the name's 8 and the type's 3 as the FCB has them), CR LF after each.
- **The stand-in CCP** sets user 0, opens `A:NAME.COM` and reads record 0 into its buffer: the header to `$0280`,
  checked, the image's first 112 bytes (or fewer) to `$0300`; then whole records straight to their place by the DMA
  address, and a last part record through its buffer. Open's `$FF` with X 0 is `NAME?`, any other error `READ ERROR`.
  A program starts in the default error mode with the DMA address `$0280`.
- **UNITS** (SELDSK's first select) resets a card it finds busy, as READ and WRITE do, so a login after a stuck card
  works.
- **Measured** on `oodos-8m`, BDOSENT to its return: Open 5,213 cycles; a sequential read of a record in a buffered
  sector 4,570, of one whose sector it reads 12,632; a write to a buffered sector 7,344, one that takes a new block
  77,663, and 92,909 when that block's buffers are written back first. The stub adds 216. Most of a buffered read is
  three DMA copies through MOVE: the FCB in and out, and the record.
- OODOS.SYS is still 0.2: the version's minor isn't changed here (the coordinator's to decide).

## RSXs and programs ##

**Decided (2026-10-26): CP/M 3's split. `.COM` files load at a fixed address; `.PRL` files are relocatable.**

- **`.COM`:** a short header, then code linked to run at **`$0300`**. That address and the base page's layout are
  permanent. Most programs are these: no relocation step to build, nothing to do at load.
- **`.PRL`:** the same header, then code that relocates by page: a bitmap marks every byte that is the high byte
  of an address, and a second bitmap marks zero page operands. The bitmaps are made by linking twice with ld65, a
  page apart (and a zero page apart), and comparing, as Digital Research's tools did. RSXs are `.PRL`s, and so is
  anything that must load elsewhere: a debugger that moves itself up out of the way, overlays (function 59).
- **The header:** a magic number, a flag for relocatable, the code size, the zero page an RSX needs, any attached
  RSXs. The loader checks the magic number first and refuses a file without it, so an 8080 `.COM` copied over by
  mistake can't run.
- **RSXs** load below the BDOS and chain on `$0203` exactly as CP/M 3's do, each with CP/M 3's RSX prefix (serial
  number, the next entry, the previous entry, the remove flag, the non-banked flag, the name), and a slice of
  `$F0–$FF`.
- **GENCOM** attaches RSXs to a program, as in CP/M 3. GET, PUT and SUBMIT are RSXs.

### The program header ###

**Decided (2026-10-26).** 16 bytes at the start of every `.COM` and `.PRL`, in the file only: the image after it is what
goes into memory. `OODOS_HEADER` in [`oodos/include/oodos.inc`](../oodos/include/oodos.inc) writes it, with the
values from the link.

| Offset | Size | What |
|---|---|---|
| 0 | 4 | the magic number, `$4F $4F $44 $1A`: "OOD", then ^Z, so TYPE shows OOD and stops |
| 4 | 1 | flags: bit 0 relocatable (a `.PRL`, bitmaps follow), bit 1 an RSX (the image starts with the RSX prefix), bit 7 breaks the 65C816 rules (OODOS's tools never set it); the rest 0, and a file with any of them set is refused |
| 5 | 1 | the link page: the image is linked to run at this page × 256. `$03` for a `.COM`, and the only page it loads at |
| 6 | 2 | the image's size, in bytes |
| 8 | 2 | BSS: bytes the program needs above its image, not in the file and not cleared |
| 10 | 1 | the zero page base: where the image's own zero page segment is linked |
| 11 | 1 | the zero page size: that segment's bytes; at most `$F0` for a program, 16 for an RSX (the slice it needs from `$F0–$FF`), 0 for an overlay |
| 12 | 1 | the number of attached RSXs (GENCOM), 0 for none |
| 13 | 1 | reserved, 0 |
| 14 | 2 | the record (128 bytes, from the file's start) of the attached RSX table, 0 for none |

- **The loader** reads the first record into the DMA buffer and refuses the file if bytes 0–3 aren't the magic
  number or a flag bit it doesn't know is set. It refuses a `.COM` whose link page isn't `$03`, a `.PRL` at a page
  below `$03`, an image and BSS that don't fit below the top of the TPA (`$0204`), and a zero page that doesn't
  fit (below). Then it reads **exactly the image's size bytes** of image: the first record's 112 bytes after the
  header, or as many as the image has if it's smaller, are moved to the load address; the following whole records
  can be read straight to their place; and a last part-record goes through the DMA buffer, only the image's bytes
  of it moved. Nothing after the image (a `.PRL`'s bitmaps, the attached RSX table, the attached RSXs) is ever
  read into the image or past it: BSS is not cleared, and memory above the image is left as it was.
- **Sizes are exact.** CP/M files are whole records, so the end of the last one is padding, ignored.
- **The attached RSX table** (GENCOM, phase 5) is a record-aligned run of 16-byte entries after the program: the
  record the RSX's own `.PRL` starts at (2 bytes), a flags byte (bit 0: load only in a non-banked system, as CP/M
  3's), the name (8 bytes, space padded), 5 reserved. Each attached RSX is a whole `.PRL`, header and all, starting
  on a record.
- **The RSX prefix** keeps CP/M 3's offsets, in the RSX's image: 0 the serial number (6 bytes), 6 `JMP` to the
  RSX's entry, 9 `JMP` to the next RSX or the BDOS, 12 the previous entry's address, 14 the remove flag, 15 the
  non-banked flag, 16 the name (8 bytes), 24 the loader's flag, 25–26 reserved. A 6502 `JMP` is 3 bytes as an
  8080's is, so every offset holds.

### The .PRL file ###

**Decided (2026-10-26).** The header (flag bit 0 set), the image as linked at the header's page and zero page
base, then two bitmaps of a bit for each byte of the image, the first byte in bit 7 of the bitmap's first byte.
Each bitmap starts on a record, so a record of it covers 1,024 bytes of the image:

| Record | Size | What |
|---|---|---|
| 0 | 16 | the header, as above |
| 0 | size | the image, from byte 16: linked at the link page, its zero page segment at the zero page base; zeros to the end of its last record |
| p = ⌈(16 + size) / 128⌉ | (size + 7) / 8 | the page bitmap. 1: the byte is the high byte of an address in the image. Zeros to the end of its last record |
| p + ⌈((size + 7) / 8) / 128⌉ | (size + 7) / 8 | the zero page bitmap. 1: the byte is the address of a byte in the image's zero page segment. Zeros to the end of its last record |

The attached RSX table, if any, follows on a later record (its record is in the header).

- **Loading at page P with zero page base Z:** the image is read as above, then the page bitmap is read a
  record at a time through the DMA buffer, and P − the link page added to every byte of the image it marks; then
  the zero page bitmap the same way, adding Z − the zero page base; both modulo 256. Bits past the image's last
  byte are padding, ignored. A byte is in one bitmap or neither.
- **Zero page.** A `.PRL` program (not an RSX) is given Z = `$00`, its own zero page, and the loader refuses one
  with Z + its zero page size past `$F0`. An RSX is given a slice of `$F0–$FF`, and refused if its zero page is
  more than what's left of it. An overlay (function 59) has none of its own: the loader refuses one whose zero
  page size isn't 0. It uses its caller's zero page by importing the caller's symbols, which don't move, as a
  program's zero page is always at its link address. An overlay's load address must be a page's start.
- **The zero page is BSS only:** no initialized data in it (ld65 drops it with only a warning, and OODOS's build
  fails on any ld65 warning). In OODOS's link configs it ends at `$EF` (`$F0` − the zero page base), so a `.PRL`
  linked at zero page base 5, as the third link is, has at most `$EB` bytes of it.
- **Made with [`oodos/tools/mkprl.py`](../oodos/tools/mkprl.py)** from three links of the same objects: the build's
  are page 3 with the zero page at `$00`, page 4 at `$02` and page 5 at `$05`. Every byte must move in the second
  and third links as one of three classes: not at all; 1 and 2 (the high byte of an address in the image); or the
  zero page offsets, 2 and 5 (a zero page address). Anything else is refused, with the offset, so a byte that is
  some other function of an address (`>(label*2)` moves by 2 and 4) is refused rather than taken for a
  relocation. The zero page bases may be any offsets but ones that move as nothing does, or as a page does.
- **No 16-bit pointers to zero page,** rule 2 below: mkprl.py refuses a zero page address that isn't the operand
  of a zero page or immediate instruction in `CODE` (it disassembles `CODE` from the first link's map), so a
  `.word ptr` or `.byte ptr` in data, or an absolute operand, can't relocate quietly. That covers pointers to the
  program's own `ZEROPAGE` segment only, the addresses that move between the links: pointers to an overlay's
  zero page (imported from its caller, so it doesn't move) and to absolute zero page addresses, in `CODE` or the
  data segments, rely on the author. mkprl.py refuses a zero page size over `$F0` for a program, and over 16 for
  an RSX.

### Rules for programs: the 65C816 ###

**Decided (2026-10-26).** A later machine may have a 65C816, whose direct page (zero page) and stack can be put
anywhere. An OODOS program that keeps these rules would run on it unchanged, in emulation mode, with its own
direct page; OODOS's build tools and examples keep them, and the loader may later refuse a program marked as
breaking them.

1. **No `RMB`, `SMB`, `BBR` or `BBS`.** They are the 65C02's only; on the 65C816 the same opcodes are other
   instructions.
2. **Zero page variables only through zero page operands:** no absolute-mode access to them (`LDA $0012`), and no
   16-bit pointers to them. Both break once the direct page moves.
3. **The stack only through the stack instructions:** no reading it in page 1 (`TSX`, then `LDA $0103,X`).
4. **The BDOS only through `$0203`,** with the registers as above.

OODOS's build checks them: `oodos/tools/rules.py` disassembles `CODE` (only `CODE`: data in other segments isn't
looked at) of the `.COM` and of the `.PRL`'s first link, for rules 1, 3, 4 and rule 2's absolute operands, and
mkprl.py finds rule 2's 16-bit pointers to zero page in any segment, by the zero page bitmap ([above](#the-prl-file)),
but only those to the program's own `ZEROPAGE` segment: pointers to an overlay's imported zero page or to absolute
zero page addresses, in `CODE` or the data segments, rely on the author.

## Apple II disks ##

**Decided (2026-10-26): two jobs, in this order.** The details are proposed.

1. **First, Apple CP/M disks as OODOS drives,** in cpmtools's `apple-do` format (the SoftCard's):
   - **as images on the SD card**, `.dsk` files mounted as units: two 256-byte sectors to an SD sector;
   - **in the real drive,** through the Disk II card in native mode: a 6502 driver for the address and data fields
     (6-and-2 GCR), stepping with the phases, and writing with the 224-cycle loop in design.md, with interrupts
     and DMA off while it writes. The motor goes off a second after the last access, timed by the RTC's tick.
2. **Then DOS 3.3 disks:** an exchange utility, like the SoftCard's APDOS, that lists such a disk and copies
   files between it and an OODOS drive. Not a drive format. ProDOS disks could follow in the same utility; that
   isn't decided.

**First, in the emulator:** the Disk II card in native mode, which it doesn't do yet (its backlog). The driver is
tested against it with our own disk images.


## GW-BASIC ##

**Decided: GW-BASIC, from [microsoft/GW-BASIC](https://github.com/microsoft/GW-BASIC)** (MIT).

**What the source is.** 34,474 lines of 8088 assembly in 35 files, dated February 1983, with no build files.
Microsoft machine-translated it from their 8080 source ("translation created 12-Jan-83"), so it still works in 8080
register pairs (HL in BX, DE in DX, BC in CX). That maps onto 6502 zero page pseudo-registers more naturally than
true 8086 code would.

**What fits.**

- **Files:** it uses MS-DOS 1's FCB calls (functions 5, 6, 13–17, 19, 20, 22, 23, 25, 26, 33, 34), which are
  the CP/M BDOS's numbers, because DOS 1 copied them. Only 39 (random block read) and 41 (parse filename) differ;
  they become CP/M 3's multi-sector I/O and function 152.
- **DEF SEG:** an 8086 segment and offset make a 20-bit address, as the OO-1's physical addresses are. PEEK,
  POKE, BLOAD and BSAVE with DEF SEG reach the whole 1 MB, video memory included, through a DMA copy or a spare
  window.

**What we write.** The machine-specific layer isn't in the release: about 217 symbols are used and never defined,
many of them the OEM's routines for the screen, graphics, keyboard, sound, light pen, joystick and serial ports
(SCROUT, SETC, READC, MAPXYC, KEYINP, DONOTE, RDSTIK, INICOM and others). We'd write those for the OO-1 anyway:
the video card's text and bitmap modes for SCREEN, the SIDs for SOUND and PLAY, the USB keyboard, the game port,
COM1/COM2 and the parallel port.

**Size.** The released source is estimated at about 40 KB of 8086 code (40,035 bytes, 39.1 KB) and 4,412 bytes
(4.3 KB) of tables, and 6502 code will be larger. It won't fit in the TPA beside a useful workspace, so the cold
parts (graphics, PLAY, RENUM, the editor) live in allocated blocks and are called through common memory (FARCALL),
12 KB at a time in W3's code window. The program and its variables have the TPA, and as much of the interpreter as
the 32 KB leaves room for.
**Decided (2026-10-26): at least 32 KB free for BASIC.** That's a minimum, not a target.

**Measured (phase 0, 2026-10-26): it fits, just.** The spike ([`oodos/spike/gwbasic/`](../oodos/spike/gwbasic/))
hand-translated about 2,530 of the 8086 instructions (parts of MATH1 and MATH2, the statement loop, CHRGET, GOTO,
IF, LET, the formula evaluator) to 6502, linked as a `.COM` at `$0300`, and checked them on the emulator's W65C02S
core.

- **The ratio:** 6,637 bytes of 6502 for 5,551 of 8086, 1.20. Math is 1.16, interpreter code 1.27, text
  scanning the worst at 1.36.
- **The whole:** 48.9 KB of 6502 (51.9 KB at the worst ratio), plus 4.3 KB of tables, plus the OO-1's OEM layer
  (estimated 12–20 KB): 65–73 KB.
- **The TPA:** a resident core of about 10 KB (the statement loop, LET, IF, GOTO, GOSUB, FOR and NEXT, the
  evaluator, variables, the string core and functions, INKEY$, single and integer math, console output) and 4 KB of
  fixed RAM leave **34,213 bytes free**; 32,599 in the pessimistic case, 169 bytes short, closed by keeping the file
  and device buffers out of the TPA. The rest goes in 7 or 8 banked blocks of 12 KB: math with PRINT and INPUT,
  program entry and LIST, the other statements, files, graphics and devices, the screen editor, the OO-1's drivers.
- **No far calls on the core statement path.** A FARCALL costs 281 cycles over a JSR; one pass of
  `I%=I%+1 : IF I%<1000 THEN …` is about 4,700 and printing a single 16,000–20,000. A loop of LET, IF, GOTO,
  FOR/NEXT, single and integer arithmetic, string functions and INKEY$ stays resident; PRINT and INPUT (once a
  statement), double precision, banked functions (SIN, VAL...), graphics (PSET and LINE once a point), I/O
  statements and typed lines make a far call each time.
- **Correct:** 93,289 math vectors bit for bit against a Python model written from the 8086 source (`mbf.py`),
  and seven BASIC programs; the translated segments pass the 65C816 rules ([`oodos/tools/rules.py`](../oodos/tools/rules.py)). One finding for phase 7 to
  check against PC-BASIC: the 1983 source prints 1/3 as `.3333334`.

For phase 7: keep the BASIC stack in the TPA, zero page pseudo-registers without the 8086's push and swap glue,
dispatch tables that carry the block, and a build that gates the resident size with `sizes.py`.

**Testing.** PC-BASIC, an independent reimplementation that aims to match GW-BASIC, is the oracle for what
programs print and how numbers are formatted. It runs only in a throwaway container. The port keeps Microsoft's
copyright and MIT notice.


## Testing ##

**Done (phase 1, 2026-10-26)** ([oodos/README.md](../oodos/README.md) has the commands)

- **The build:** ca65 and ld65, as the BIOS. [`oodos/Makefile`](../oodos/Makefile) links each program as a `.COM`
  and, three times, a `.PRL`; the `CODE` of the `.COM` and of the `.PRL`'s first link passes the 65C816 rules
  (`oodos/tools/rules.py`), mkprl.py's checks pass, and ld65 gives no warning, or the build fails. `make check`
  tests the tools: relocated `.PRL`s against direct links, the loader's steps (only the image's bytes change in
  memory full of sentinels, with images under 112 bytes, over 1,024, and RSXs attached) and refusals, mkprl's
  refusals, the magic number, and the rules gate on planted violations; and runs hello on the emulator's W65C02S.
  The test card's `/BOOT.BIN` is built with ca65 and ld65 too.
- **Card images:** [`oodos/tools/make_card.sh`](../oodos/tools/make_card.sh) makes the test card with cpmtools,
  mkfs.fat and mtools in a throwaway container, from our own files, under faketime so the same sources make the
  same card. It's committed as a sparse image, `emulator/test/host/sd/oodos.sparse` (oodos/README.md has its
  size): a card (16 MB in phase 1, 32 MB from phase 2), FAT16 in an MBR partition, with `/BOOT.BIN`, `/OODOS/DRIVES.TXT` and `/OODOS/A.IMG` (`oodos-8m`, label mode
  0x31, files in users 0 and 3, one over two directory entries).
- **Interoperability, both ways:**
  - **cpmtools writes, the OO-1 reads:** `test_oodos_card`, in ctest, mounts A.IMG through the card's own MOUNT
    (after reading DRIVES.TXT by its grammar, as OOLDR will) and finds every entry, SFCB stamp and record in the
    n-th sector of its block b, at LBA = b × (BLS / 512) + n (the unit's sectors are 512 bytes, the drive has no
    boot tracks), by the DPB worked out from `oodos/diskdefs`.
  - **The OO-1 writes, cpmtools checks:** in the same test a stand-in for the BDOS writes a file through the card's
    WRITE and saves the card; [`oodos/test/interop.sh`](../oodos/test/interop.sh) takes A.IMG off it with mtools and
    checks it with `fsck.cpm -n`, `cpmls -F` (both stamps), the file's raw SFCB slots, and `cpmcp`. It checks the
    committed card the same way. A wrong allocation or misplaced stamps, made on purpose (in an SFCB of mkfs.cpm's
    `$E5`s or of zeros), fail both.
  - **fsck.cpm by its report:** it exits 0 after some errors, a bad stamp for one, so `check_diskdefs.sh` and
    `interop.sh` fail on any Error or Warning line it prints (`oodos/test/cpmtools.sh`). One exception, in
    `interop.sh` only: **cpmtools 2.23's fsck.cpm compares the stamps in an SFCB's first slot backwards.** For a
    file entry E with E mod 4 = 0 it warns "Modification date earlier than creation date (extent=S/E)" when the
    update is on a later day than the creation, and says nothing when it's earlier; the other slots are compared
    right. So `oodos/test/sfcb.py` reads every file entry's stamps itself and fails one updated before it was
    made, and only then is that warning let through, for exactly the first-slot entries it found a day or more
    apart and in order. The OO-1's test file is made a day before it's updated, so the case is always exercised.
  - **Decided (2026-10-26): ctest stays hermetic,** with no skips, on committed images; the checks that need
    Docker are scripts like `check_diskdefs.sh` and `interop.sh`, run by hand and at each phase's verify.
  - Phase 3 puts the BDOS where the stand-in is, running in the emulator; the checks on both sides stay.

**Done (phase 2: the loader, 2026-10-27)** ([oodos/README.md](../oodos/README.md) has the commands)

- **OOLDR and the CP/M reader:** [`oodos/ldr/`](../oodos/ldr/) builds `/BOOT.BIN` from `ooldr.s` and
  `oodos/sys/cpmread.s`, and a test OODOS.SYS from `testsys.s`: six segments in block 31 and blocks 60–62, one of
  them over the first directory entry's end (record 256) and one over a logical extent's (record 128) and from
  block 60 into 61 (over `$F4000`, so W2 must be the next block), and two touching, the earlier loaded later, so a
  last part record written whole shows. Its cold boot writes the windows
  and P it found to block 31 and stops. ld65 writes the header's records from the file offsets it gives.
- **The card:** `make_card.sh` builds both in its container and puts them on the card, which is now 32 MB:
  `/BOOT.BIN` is OOLDR, A.IMG holds OODOS.SYS beside phase 1's files, and `/OODOS/B.IMG` is an empty `oodos-8m`
  drive, label mode 0x31; DRIVES.TXT names A: and B:. (At integration A: got OODOS itself as OODOS.SYS, the test
  system as TESTSYS.SYS, which test_oodos_boot renames in its copy, and HELLO.COM and HELLO.PRL: below.)
- **`test_oodos_boot`, in ctest,** boots the committed card through C80CR in a Session and checks every segment
  byte for byte at its physical address, the bytes either side as they were, the entry (W0 still block 0, W1–W3
  and VEC 60, 61, 62, 62, I set, D clear), and units 1 and 2 mounted as A.IMG and B.IMG with their flags. Then every
  refusal, each with its message, and DRIVES.TXT's with the line's number: each form the grammar refuses (a word
  after the path, `R0`, `ROM` or a second `RO`; a space in the path; a drive named twice; the same path in another
  case, either way round; a CR not before LF, in a comment too and at the end; a drive outside A–G, no blank after
  the colon, a path without `/`, a control character in it, one over 509 bytes), the forms it allows (a BOM, CR LF, blank and comment
  lines, tabs, lower case, `RO` before a comment, a last line with no ending), no A line, a path not on the card,
  an image of the wrong size, no DRIVES.TXT, and a unit left mounted unmounted; OODOS.SYS missing from user 0, a
  bad magic number, an unknown major version, 0 or 9 segments, an entry outside block 62, a segment over 16 KB,
  outside block 31 and blocks 60–62 (below, past and between them, into block 63, past 1 MB), overlapping by a
  byte or wholly, or past the file's end; the reader's damaged directories (an entry missing, twice, part full, an
  RC over 128, an EX over 31, a block of the directory's or past DSM), too many entries and an empty file; and
  copies of OODOS.SYS's entry where the reader mustn't look (deleted, user 3, an XFCB, the label's and an SFCB's
  first bytes). An `oodos-32m` drive, its two entries last extent first, boots too. Those cards are made in the
  test, FAT16 on the whole card, from the committed card's files; nothing more is committed.
- **Each check can fail:** OOLDR built with a deliberate bug and spliced into the card fails the test, one bug at a
  time: every last record written whole, W2 on block 62, CLI, SED, entries of any user read, a CR in a comment
  let by, a CR at the end let by, a drive named twice let by, paths compared in one case only, no overlap check,
  any image size taken, `oodos-32m` with `oodos-8m`'s EXM, block 63 allowed, a segment past the file's end let by,
  the BOM not skipped, nothing unmounted, segments over 16 KB let by, part full entries let by.
- `test_oodos_card` follows the card (its size, the directory's entries, OO1.BIN's two entries, now 14 and 16:
  below) and `interop.sh` passes on it unchanged.

**Done (phase 2: together, 2026-10-27)**

- **The card** carries the whole system: `/BOOT.BIN` is OOLDR, and A: holds OODOS.SYS (OODOS itself),
  TESTSYS.SYS (OOLDR's test system, which `test_oodos_boot` names OODOS.SYS in its own copy of the card),
  HELLO.COM and HELLO.PRL.
- **`test_oodos_e2e`, in ctest,** boots the committed card as its owner would: the monitor, `C80CR`, OOLDR, OODOS
  and `A>`; `hello world` runs HELLO.COM from A: through the CCP's reader, with its tail, its image byte for byte
  at `$0300` and the bytes past its BSS (set by the monitor first) untouched, and runs again after the warm boot;
  a name not on A:, a `.SYS` not a `.COM` and a name over 8 characters say `NAME?`. Seen to fail with the image
  loaded from the file's start instead of after its header.
- `test_oodos_card` follows the card: ten entries before its own file, which lands in entries 14 and 16, so an
  SFCB's first slot, the one fsck.cpm 2.23 compares backwards, is still exercised (HELLO.PRL is there partly for
  that).

**Done (phase 2: the system, 2026-10-27)** ([oodos/README.md](../oodos/README.md) has the commands)

- **The build:** `oodos/sys/Makefile` links OODOS.SYS once with ld65 and makes it with `mksys.py`, and links the
  test programs as `.COM`s with `oodos/cfg/com.cfg`, through the rules gate; any ld65 warning fails it. CMake runs
  it for ctest. `make check` tests `mksys.py`: the build's OODOS.SYS read back against ld65's parts, the header and
  records byte by byte, the edges OOLDR takes (a whole 16 KB segment, one across blocks 60 and 61, eight), and every
  refusal, by its maker, by its reader and on the command line.
- **`test_oodos_sys`,** in ctest, boots OODOS.SYS on `backplane.machine` as OOLDR will leave it (the ROM's monitor
  in block 63 first, units mounted through the card, the segments loaded from the file's header, the windows and
  flags of OOLDR's jump) and checks:
  - the banner, with the clock's time, and `A>`, on the serial line and the screen; the base page and `$BFFD`;
  - **M:'s directory byte for byte** against `mkfs.cpm -f oodos-ram -t` with the mode set to 0x31, made by
    cpmtools in a throwaway container under faketime (`oodos/sys/test/make_mdisk.sh`, committed as
    `emulator/test/host/sd/oodos-ram.sparse`), the clock set to the same minute;
  - HELLO by `GO`, its tail, and the warm boot back to the prompt; the BDOS stub's cycles;
  - **BIOSTEST** (every BIOS entry through function 50): drives A–G and M from the units table, each DPB against
    `oodos/diskdefs` (8 MB, 32 MB from a FAT16 card the test makes, a mounted file of neither size not a drive), a
    sector out and back on A: and on M: (and on the card and in banks C–D afterwards), a READ into a buffer across
    two windows that aren't one run of memory, WRITE on a read-only unit returning 2, M:'s errors, a read past a
    drive's end, TIME, the devices' statuses, DEVTBL and DRVTBL copied out of the system bank by XMOVE, SECTRN,
    MULTIO, FLUSH, MOVE, CONOUT, AUXOUT and LIST; and the card's own write-protect and a missing card (WPTEST);
  - **the stub** (STUBTEST): all four windows moved, W3 a home block with common memory copied in, kept across BDOS
    calls that reach the program's memory through W1, W2 and W3, S kept, D clear on return;
  - **FARCALL** (FARTEST) into blocks 20 and 21 mapped by hand, nested, with the spike's checks (A, X, Y and P,
    D set among them, both ways; W3 back each time), and its cycles: 281;
  - **TIME** against the emulator's clock: a leap day, the last second of 2099 into 2100, setting it back from the
    SCB by function 104 (the day of the week, the century bit), and a date before 2000 left alone;
  - **the VT52 subset and lowercase** on the video card's text memory, scrolling, and the cursor;
  - keys from the USB keyboard and the serial line, function 10's line editing, the console functions (LINETEST),
    the CCP's refusals (no magic, flags, the link page, too big by a byte; exactly to `$BFFD` runs), `NAME?`;
  - **Break and BRK** into the ROM's monitor with W3 and VEC on block 63, and an IRQ that returns.

  Every check was seen to fail with the code broken on purpose (the stub not putting W3 back, nor A and X, D left set,
  FARCALL's CLD gone, Break leaving VEC, the label's mode, the leap years, the day of the week, the century bit,
  a DPB, the card's write-protect, the window split, ESC K, lowercase, scrolling, PGET's windows, ^X, the CCP's
  flag check, CON:'s TTY, the label's sector).

**Done (phase 2: the review's fixes, 2026-10-27)**

- **No skips, and a fresh card:** configuring the native tests fails without cc65, python3 and make, and
  `test_oodos_sys` fails, not skips, without OODOS.SYS. CMake builds OOLDR, the test system and HELLO.PRL as it
  builds OODOS.SYS, and `test_oodos_boot` fails ("the card is stale: run oodos/tools/make_card.sh") unless the
  committed card holds exactly those, HELLO.COM and `oodos/card/DRIVES.TXT`. The host's cc65 and the container's
  (Debian's 2.19 both) make the same bytes.
- **`test_oodos_boot`** adds: `//`, `/./`, `/../`, a `/` at the end and `/` alone refused by line; names of dots
  that aren't `.` or `..` taken; an 8.3 alias of a long name, and the same name in two directories, refused as
  two drives on one file, the alias alone a drive; DRIVES.TXT of 16 MB and 16 bytes; an empty segment, one in
  the header, an entry in no segment; a card that stays busy as OOLDR starts and as it reads OODOS.SYS (SD
  TIMEOUT); A: unmounted or mounted again as a file of neither size just before (by its line); the staleness
  check; and came_up() checks a segment crosses `$F4000`. The changes are made while OOLDR runs, at its labels
  (from the build tree's `ooldr.lbl`), an instruction at a time.
- **`test_oodos_sys`** adds: BIOSTEST's WRITE from a buffer across two windows (BOUNCE's write leg), checked on the
  card; STUBTEST with the home block in W3 read-only (`NB_RO`), common memory unchanged; BANKTEST, SETBNK 1, SELMEM 1
  and an XMOVE left by one program and a READ and a MOVE in bank 0 by the next, and function 50 of BOOT refused
  with M:'s directory kept; HOLETEST, function 9 of a string in the hole, READ to the hole, to flash and to a
  read-only window, MOVE to the hole and from flash, all refused with memory as it was, and a WRITE from a
  read-only window taken; READ while the card is busy with a FLUSH (waited for), with channel 0 short of its
  terminal count (1), with a command that never ends and a card busy from before (1 after 1.01 s, then reads
  again); the CCP's reader on a busy card (READ ERROR after about 1.3 s); a clock in 2156 (no date, function
  105 `$FF`, a 2156 date not set) and 2155 still read. XMOVE's banks are CP/M 3's in BIOSTEST.
- `test_oodos_card`'s DRIVES.TXT reader refuses a CR at the very end and empty, `.` and `..` names, as OOLDR;
  `check_tools.py` links a `.COM` to `$BFFC` and refuses one to `$BFFD` (com.cfg's TPA ends at `$BFFC`).
- **Each check can fail:** each was seen to fail with the fix undone (XMOVE's banks, the idle wait, the terminal
  count check, a 0.1 s timeout, each of the three resets at warm boot, BOOT let through, the stub's old scratch,
  PGET's and PHYSOF's hole, flash and read-only checks one at a time, BOUNCE's write copy, the year check, the
  reader's timeout, OOLDR's name check, units-table compare, empty, record 0 and entry checks, the size's top
  byte, CRE_UNIT's and CRE_SIZE's messages, its timeout, W2 two blocks on, the staleness check by a changed
  banner, test_oodos_card's CR and names, com.cfg's old size). Not testable on the emulator: UNITS waiting for
  IDENTIFY's end (the card ends it with the last byte read), SAY_PATH's high byte (`slots` doesn't cross a page),
  and PPUTY keeping X (no caller needs it yet).

**Done (phase 3a: the BDOS, 2026-10-27)** ([oodos/README.md](../oodos/README.md) has the commands)

- **Test programs** ([`oodos/sys/test/`](../oodos/sys/test/), `.COM`s through the rules gate, with `fs.inc`'s
  helpers), run by `test_oodos_sys` on the emulator, each line a result it checks:
  - **FSTEST** on A: (`oodos-8m`), on an `oodos-32m` drive and on M: (byte pointers): 300 records out and back over
    three extents, the last sector not on the disk until the close writes it back, at most a BIOS READ a sector
    reading back (77: 75 sectors and the directory's as two extents open), random reads in the file, past its end
    (1), into an extent with no entry (4) and past 32 MB (6), a sparse write at record 1000, a record of its block
    zeros, 40, the size and 36, delete and the free space back; and the BDOS's cycles.
  - **DIRTEST** on M:: 17 and 18 with `?` in the name, the extent and the drive (all 256 entries, the label and 64
    SFCBs counted); users, a SYS file opened from user 3 and refused a write; read-only files refused delete, rename
    and write; f1', t3' and f6' set and read back; rename (attributes kept), File Exists and ? in Filename from make,
    rename and open; two names with one hash; a two-entry file renamed and deleted; the directory full (186 makes)
    and emptied by `F???????.TXT`; the disk full (02, sequential and random); a block taken again zeroed; 98 freeing
    an unclosed file's blocks; truncate; 28, 29 and 37; 24, 25, 27, 31's DPB, 12, 42, 43, 46 and 14 of drives that
    aren't; 48; 13.
  - **ERRTEST:** each error code's path in mode `$FF` (returned), `$FE` (said, then returned) and the default (said,
    and the program ended: what it prints after never shows): Read Only (a read-only unit, read-only from its login;
    and a unit made read-only under a written buffer, whose write-back fails), File Exists, ? in Filename, File R/O,
    Invalid Drive, and Disk I/O with the card taken out.
  - **STAMPTEST** on M: and A:: every SFCB byte checked against the clock the test set at each step: creation at
    make, update at the close after a write (a file closed unwritten has none), access at open on M: with the label's
    mode made 0x61 behind the BDOS's back, none on A: (0x31).
  - **LOGINTEST:** A:'s login reads its 64 directory sectors once and a second select none; M:'s 16; an entry put in
    M:'s directory behind the BDOS's back isn't seen until 37 logs it out; an XFCB renamed and deleted with its file
    (f5': it alone); 4 records written and not closed are on M: only after the warm boot, whose 98 frees their block.
- **The CCP** loads through the BDOS in every run (`hello` in `test_oodos_e2e`, from the card, with the bytes past
  its image untouched); a card that stays busy as it logs A: in is `READ ERROR`, and the next load works.
- **Interoperability:** `test_oodos_card` boots the committed card in a Session and runs **WRITEOO1** (on the card)
  at the prompt: `0:OO1.BIN` written by the real BDOS, the clock set by function 104 to its two stamps; its
  entries are now 16 and 17 (WRITEOO1.COM took 14), the first in an SFCB's first slot; 66 sectors change (the
  directory's second and 65 of records). `interop.sh --written` passes on the card it saves (PASS 9), and on the
  committed card (PASS 6). `OODOS_CARD_FAULT` still runs the stand-in writer, wrong, to show interop.sh fail.
- **Each check can fail:** the BDOS built with a bug, one at a time, fails the tests named: no full compare after the
  hash (DIRTEST), close not writing back (FSTEST, LOGINTEST), no buffer found again (FSTEST: the READ count), the jump
  table not putting W2 back or bank 0's W2 the BIOS's (nearly all), no update or creation stamp (STAMPTEST), 37 not
  logging out (LOGINTEST, STAMPTEST), the default mode returning (ERRTEST), any user 0 file opened from another
  user or none (DIRTEST), 98 freeing nothing (DIRTEST, LOGINTEST), a new block not zeroed (FSTEST, DIRTEST), a
  read-only unit writable at login (ERRTEST), truncate not freeing, 18 repeating an entry, delete not checking
  read-only files, make taking a name there, rename taking the new name's attributes (DIRTEST), RC 0 for an
  extent the entry goes past (FSTEST), UNITS not resetting a stuck card (the busy card), the CCP not calling 48
  (LOGINTEST); and the CCP reading a last part record straight to its place, on a card made with it
  (`test_oodos_e2e`). Each was put back and the card made again, byte for byte the committed one.

**Proposed**

- **The emulator** runs OODOS in native tests (BDOS functions, the CCP, RSXs, the Apple driver) and in the WebUI's
  e2e tests.
- Nothing of Digital Research's, or any other copyrighted software, goes into the tests or onto the website.


## Phases ##

Each phase is its own branch.

0. **This document,** its open questions settled, the `diskdefs` file, and the GW-BASIC size spike.
1. **Tooling:** the build, card images, the interoperability tests, the DS3231 in the emulator.
2. **OOLDR and the BIOS:** common memory, the banking stub, the SD drives, the console, TIME.
3. **The BDOS:** the file system, deblocking, hashing, multi-sector I/O, the SCB, date stamps, labels and
   passwords.
4. **The CCP and utilities:** the built-in commands; DIR, TYPE, ERA, REN, PIP, SUBMIT, DATE, INITDIR, SET, SHOW,
   DEVICE.
5. **RSXs:** the loader, chaining, GENCOM; GET, PUT and SUBMIT as RSXs.
6. **Apple disks:** native Disk II in the emulator, the driver, `apple-do` drives; then the DOS 3.3
   exchange utility.
7. **GW-BASIC.**
8. **The website:** an OODOS card in the WebUI with OODOS, GW-BASIC and demo programs, all ours or MIT.


## Open Questions ##

- **A home block for GW-BASIC.** Letting a program keep one block mapped in W3 between far calls, and FARCALL
  return to it rather than to nothing, would move 4–6 KB of GW-BASIC out of the TPA. It changes FARCALL's design
  above; for phase 2, not needed for the 32 KB.

  What it would cost (estimates from the spike's measured figures; the home block itself isn't built or timed):
  - **Unchanged:** the core statement path stays resident in the TPA with no far calls (`I%=I%+1 : IF I%<1000
    THEN 10` stays about 4,700 cycles a pass), and FARCALL already saves and restores whatever W3 holds, so it
    stays 281 cycles.
  - **Faster:** PRINT and INPUT in the home block make no far call: 281 cycles a statement, 1.4–1.8% of printing
    a number.
  - **Slower:** with GW-BASIC's block in W3, every BDOS entry and every interrupt handler in OODOS's block must
    map that block in and back out (save, store, restore the window register): about 20–30 cycles each. Console
    output is a BDOS call a character, so printing a number gains roughly 1–2% back, about cancelling the
    PRINT saving. Interrupts at 60 Hz are about 0.02% of the CPU.
  - **The hazard:** while a banked page is in W3 the home block is not, so a call from banked code into home code
    is a far call. Single math in the home block would cost `$FOUT` (banked) 281 cycles per multiply or divide,
    about 15% a number printed at ten of them. So the home block may hold only code the banked pages don't call;
    what they share (the math core, the string core, variables) stays in the TPA. The 4–6 KB must be counted again
    under that rule.

  For phase 2: prototype it in the spike harness and time, with `check.py --timing`, the BDOS map-in stub, a
  PRINT through the home block and `$FOUT` under the placement rule.
- **A lean FARCALL**, keeping only A and the flags across the call, would save an estimated 100–140 of its 281
  cycles (not measured: reading the inline bytes, the return frame and the two window writes stay). Under 1% on
  PRINT, more on a graphics loop's far call a point, nothing on the core statement path. Against it:
  - X and Y no longer reach the target or come back. The GW-BASIC port names numbers in zero page by X, so a
    banked math entry taking one would need X passed through zero page: a second convention at the bank
    boundary, and some of the saving back.
  - FARCALL is OODOS's interface for every program (function 72), not GW-BASIC's. cc65's C passes and returns
    16-bit values in A/X, so a banked C function would lose the high byte. Two variants cost common memory.
  - A routine that works by JSR can break by FARCALL with nothing to say so; the full one's "whatever works by
    JSR works by FARCALL" is what the spike's checks test, and they found real bugs (X lost, D set).

  Leaning (2026-10-27): keep the full FARCALL as the interface; if phase 2's timing shows far calls matter
  somewhere (graphics), a lean variant private to GW-BASIC's dispatchers, whose targets take nothing in X or Y.
  Time a lean prototype with `check.py --timing` first.

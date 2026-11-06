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
| `$0250` | `$0050` | the drive the program was loaded from, 1–16 (phase 4) |
| `$0251` | `$0051` | the first command-tail specification's password: its address (2 bytes), then its length (`$0253`) |
| `$0254` | `$0054` | the second's, the same way (`$0254–$0256`) |
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
call table are always at the same addresses, and a far call can swap W3 without losing them. *(Not built yet: 3c's
53–56 allocate blocks but copy nothing into them, so a program puts common memory in a block it far calls
itself; deferred to phase 7, below.)* Common memory keeps
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
- Y is not kept. The flags come back as they were before the call (the stub's `PLP`), but with the decimal flag
  clear (ours: CP/M 3 keeps no 8080 flags either).

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

Not in CP/M 3: 38, 39 and 41 (MP/M's drive access, free drive and test-and-write). 38 and 39 return 0, as CP/M 3
answers them (2026-11-04: they returned `$FF` before; oodos.inc's `DRV_ACCESS`, `DRV_FREEDRV`); 41 returns A =
`$FF` with X 0, as CP/M 3 answers it (2026-11-04: A = X = `$FF` before, as a function OODOS hasn't).

**Where the 6502 changes things:**

- **12** returns `$0031`, as CP/M 3. Programs that need OODOS itself use function 72.
- **27** (allocation vector): in banked CP/M 3 the vector is in the system bank, and here it is too. It returns
  `$FFFF`; function 46 gives the free space.
- **31** returns the address of the drive's DPB in common memory: the BIOS's own, not a copy (ours, so it is
  readable from any bank; a program mustn't change it).
- **49:** the SCB keeps CP/M 3's offsets for every field CP/M 3 documents.
- **50:** the BIOS parameter block is OODOS's: +0 the BIOS function, +1 A, +2 X, +3 Y, +4–5 and +6–7 two words
  (for MOVE and XMOVE's addresses and counts).
- **59** loads an overlay at the FCB's random record address, which must be a page's start, or the call is
  refused. A `.PRL` is relocated there; a `.COM` loads only where it was linked. An overlay has no zero page of
  its own (its header's zero page size must be 0, or it's refused): it uses its caller's, by importing the
  caller's zero page symbols when it's linked ([The .PRL file](#the-prl-file)).
- **104, 105** use CP/M 3's DAT (days from 1 January 1978, hour and minute in BCD); 105 returns the seconds, BCD, in
  A. Both go to the DS3231. 104 puts the DAT in the SCB, then sets the clock from it: a date before 2000 or past
  2155 sets the SCB but not the clock (ours: the clock holds 2000–2155), and returns 0 all the same; the SCB has it
  until the BDOS next reads the clock (a stamp, 105).

### Memory: CP/M-86's functions ###

CP/M 3 has no memory manager. CP/M-86's suits the OO-1 exactly: it works in paragraphs of 16 bytes, so a 16-bit
paragraph number names any byte boundary of the 1 MB, as an 8086 segment does (and as GW-BASIC's DEF SEG
assumes).

| No. | CP/M-86 name | What |
|---|---|---|
| 51 | F_DMASEG | set the DMA "segment": the paragraph the DMA address counts from, so a read or write reaches any physical memory. `$FFFF`, the default, means the DMA address is the program's CPU address, as in CP/M 3 |
| 52 | F_DMAGET | return the DMA segment and address |
| 53 | MC_MAX | allocate the largest free run of memory, in an MCB |
| 54 | MC_ABSMAX | the same, at a given base |
| 55 | MC_ALLOC | allocate memory: the MCB's length in, its base out |
| 56 | MC_ABSALLOC | allocate memory at a given base |
| 57 | MC_FREE | free an allocation, or its top part (from a later block to its end) |
| 58 | MC_ALLFREE | free everything the program allocated |

- **The MCB** is CP/M-86's: base (a paragraph, 2 bytes), length (paragraphs, 2 bytes), and an extra byte.
- **Granularity is a 16 KB block,** 1024 paragraphs: lengths round up, bases are block boundaries. Only banks 1–5
  are handed out, 20 blocks (decided 2026-10-30, [phase 3c](#the-bdos-phase-3)).
- **Warm boot frees** everything a program allocated, as 58 does, so a program that stops early leaks nothing.
- MP/M's memory functions (128–130) were the alternative. They describe memory segments of a multi-process
  system, and fit less well.

### OODOS's own ###

From 72, a range no Digital Research system uses (CP/Net has 64–71).

| No. | What |
|---|---|
| 72 | OODOS's version, the BIOS's version, the memory fitted and the total for programs (not what is free), and the address of the far call table |
| 73 | translate a CPU address in the program's map to a physical address (the paragraph and the offset), for programs that program the DMA controller themselves |
| 74 | mount an SD image as a drive, or unmount one: a drive and a path, as MOUNT does |
| 75 | leave OODOS for the ROM BIOS, not to come back: A = 0 its Woz Monitor (as Break goes there); A = 1 Apple II mode (the ROM's `$C806`), if the cartridge holds a IIe ROM (`$FBB3` = `$06`, as the ROM checks), else A = X = `$FF` and OODOS goes on. Either way the changed buffers are written back first (as 48), but a file left open isn't closed: a program closes its files first. Any other A is A = X = `$FF`, nothing written back (ours). A program can't do it itself: W3 is OODOS's, and the rules let it call only the BDOS. The CCP's WOZMON and APPLE2 use it (built-ins since 2026-11-03; `.COM`s from 2026-11-02) |
| 76 | an Apple disk's DOS 3.3 sector, read or written: the drive, track, sector, op and a 256-byte buffer; the drive's buffers written back and dropped and it logged out first, as 37 (6d) |
| 77–97 | reserved for OODOS |

### Far calls ###

**Proposed.** Calling code in an allocated block can't wait for a BDOS call; GW-BASIC makes thousands a second. So
common memory holds a small table, found through function 72, beside the BIOS's jump table:

- **FARCALL:** map a block into W3's code window (the program names the block and an address in `$C800–$F7FF`),
  JSR there, and put W3 back on the way out. The block's code may call FARCALL again; the table keeps a stack of
  the blocks. The program's own memory, `$0000–$BFFF`, stays mapped throughout, so banked code sees all of it.
- A block allocated for code gets common memory copied into its top 2 KB, so 12 KB of each is the program's.
  *(Not done yet: 3c allocates blocks without it; deferred to phase 7, with the home block and the lean FARCALL,
  under [Open Questions](#open-questions).)*
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
  Lowercase stays lowercase. **The serial line gets the same as ANSI (2026-11-03),** which a PC's terminal program
  reads and VT52 isn't: TTY's output turns `ESC A`–`D`, `H`, `J` and `K` into `ESC [` and the letter, `ESC E` into
  `ESC [H ESC [2J`, and `ESC Y` into `ESC [`row`;`col`H` (from 1, past the screen's edge its last, as the screen
  has it), and drops another ESC pair, as the screen does. The WebUI's Terminal reads that ANSI; the native app
  passes it to the host's terminal.
- **TIME** with A = 0 reads the bridge into the SCB's date (days from 1 January 1978, day 1 that day), hour,
  minute and second (BCD); with A = `$FF` it sets the bridge from them, the day of the week worked out.
- **Interrupts:** none enabled in phase 2; the console is polled. BRK, and Break (NMI), go to the ROM's monitor
  as BIOS 0.9's Break does (W3 and VEC back to block 63). Any other IRQ returns.
- **Apple drives:** below.

**Proposed (phase 2, as built, 2026-10-27)** (`oodos/sys/bios.s`, `crt.s`):

- **Registers:** CP/M 3's C or E in A, BC in A/X, and MOVE's DE and HL (SECTRN's table too) in two words of the
  system's zero page, function 50's +4–5 and +6–7; a result in A, or A/X for HL. `bios.s`'s header has every
  entry. SELDSK's E (bit 0: selected before) is X; a first select reads IDENTIFY's units table again, so a unit
  mounted since shows up; a unit whose entry changed is marked in `DRVCHG`, for the BDOS (2026-10-27), and a table
  that can't be read changes nothing (that select has no drive). As CP/M 3's: MOVE copies BC bytes from DE (+4–5, the source) to HL (+6–7, the
  destination) and returns HL past them; XMOVE's C, the source bank, is A, and its B, the destination bank, is X.
- **Banks:** 0 is the system's view (31, 60, 61, 62; from 3a W2 the block its caller had there, `CALLW2`, below),
  1 the program's (the windows the stub saved); any other is taken as 0. SETBNK names SETDMA's; SELMEM sets the bank MOVE copies within, and moves no window (the BDOS reaches
  the program's memory by physical address, never by switching under itself); XMOVE's two banks are for the next
  MOVE only. Cold boot and every warm boot set SETBNK's and SELMEM's banks to 0 and drop a pending XMOVE, so
  nothing a program left reaches the next.
- **What isn't memory:** an address in a window's I/O hole (`$C000–$C7FF`), a window on flash (`NB_ROM`), or a
  read-only window as the destination, has no physical address the BIOS may use: READ and WRITE return 1 and MOVE
  moves nothing and returns A = X = `$FF` with the carry set, all checked before a byte moves. A move that is
  done clears the carry (phase 3c's review): its end can itself be `$FFFF`, a move to `$FF7F` of 128 bytes, so
  the BDOS goes by the carry. The BDOS's functions stop at a program
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

1. The BIOS boots `/BOOT.BIN` from the FAT card: by itself at power-on, once the splash is over (BIOS 0.10; a key
   during the splash is the monitor instead), or with `C80CR` from the monitor. For OODOS that's **OOLDR**, a loader in the
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
  Function 50 gives the program its own flags back, so MOVE's carry (and TIME's) stops at the BDOS: a program's
  MOVE that returns `$FFFF` either was refused or ended at `$FFFF`, and the program tells which by whether its
  destination + count is `$FFFF`.

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
  and 320, banks 1–5, the total 53–58 hand out, fixed: not what is free), FARCALL's address; and returns that address in A/X. 104 sets the seconds
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
  the DPB in common memory (as built, the BIOS's own DPB there, not a copy: the BDOS section above). 44
  (multi-sector) is 3b's: until then the count is 1.
- **Date stamps from 3a:** every OODOS drive's label asks for creation and update stamps (mode 0x31), so making a
  file stamps its creation and update, and closing a written one its update, from TIME, as the label's mode says,
  in the SFCB slot of the file's extent-zero entry only (the Programmer's Guide, 2.3.8: a slot is valid only for an
  extent-zero entry).
  Reading them (102), labels (100, 101), XFCBs and passwords are 3b's.
- **Where it lives.** The BDOS's code is block 60, and what doesn't fit goes in block 62's code window (about 10 KB is
  free there). Its variables and the drives' allocation vectors (CP/M 3's two bits a block: 1 KB at most for an
  `oodos-32m` drive) are in the system block. The hash tables (4 bytes a directory entry: 4 KB an `oodos-8m`
  drive, 8 KB an `oodos-32m`, 1 KB M:) are in blocks 24–27, and the directory and data buffers (BCBs, 512 bytes
  each) in block 28: banks 6–7, as [Banking](#banking) planned. Block 30 is spare; block 29's first 4 KB is the
  BIOS's `apple-do` track buffer (6b), the rest spare.
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
  exercises it; OODOS.SYS no longer links `cpmread.s`. The real CCP is phase 4a, which loads the same way.
- **The model test:** [`oodos/test/`](../oodos/test/)'s model and a driver program (a `.COM`) that runs a script of
  operations from a file and writes what each returned; a ctest runs the same seeded scripts on the model and on
  OODOS in the emulator and compares the results and the drives' files and directories. The model's images are
  checked by cpmtools too, at verify.

**Proposed (3a, as built, 2026-10-27)** ([`oodos/sys/bdos.s`](../oodos/sys/bdos.s), [`bdosfile.s`](../oodos/sys/bdosfile.s),
[`bdosdisk.s`](../oodos/sys/bdosdisk.s); the choices the manuals leave open are ours, marked so for the model test):

- **Where it lives:** all of it fits block 60 (6,564 bytes of 16 KB), so nothing is in block 62's code window yet;
  `sys.cfg` has a `BDOS2` segment there, after the CCP's code and before its BSS, for when it doesn't. The system
  block holds its zero page (the BDOS's and the BIOS's together, 180 of the 240 bytes), its variables (the FCB's copy,
  the search's state, a table a drive, the buffers' tags: 627 bytes of `SYSBSS`'s 1,280 with the BIOS's, its copy of
  the units table among them) and the
  allocation vectors at `$1000–$2FFF`, 1 KB a drive's slot (A–G slots 0–6, M: 7); the hash is 8 KB a slot in blocks
  24–27, the buffers block 28.
- **The jump table:** every entry but BOOT and WBOOT goes through its own few instructions in common memory, which
  keep W2 as the caller had it (on the stack, and in `CALLW2`, `$FC`), put W2 on block 61, call the BIOS, and put
  W2 back, keeping A, X, Y and the carry (N and Z follow A). Bank 0's `$8000–$BFFF`, to the BIOS's READ, WRITE and
  MOVE, is `CALLW2`'s block, so the BDOS reads a sector straight into the buffer it has in W2. The BIOS calls none of
  them itself. Measured: **51 cycles** a call over the JMP alone (28 in, 23 out). Common memory holds 1,107 of its
  2,048 bytes, the BIOS's DPBs among them (function 31 returns one).
- **Buffers:** 32 sectors, directory or data, the least recently used taken (an empty one first) by a 16-bit clock.
  A directory change is written at once; a data sector when its buffer is taken, by 48, 13 and 37, at the warm boot
  (the CCP has the BIOS read the units, then calls 48 and 98, each time, in error mode `$FE`), and (the drive's)
  before a close or an extent change writes the entry that points at it. A data write that fails (Disk I/O) keeps
  its buffer changed and marked, so a close whose entry would point at that sector (a block in the FCB's
  pointers) fails again rather than put in an entry that points at a sector never written (2026-10-27, the
  review's second round), before the directory is touched; the close of another file on the drive, written or
  only read, doesn't fail on it (the third round): its entry doesn't point there, or did before. The least
  recently used buffer taken is one not marked, so one sector that can't be written doesn't stop the others
  coming and going. With all 32 marked, a sector not in a buffer can't be had: each try writes buffer 0 again,
  Disk I/O until the card writes again or 13, 37 or the warm boot drops them. A read-only unit's failed write (2)
  drops the buffer and makes the drive read-only (a close then fails Read Only); a failed directory write drops
  its buffer too, so the directory in memory stays what the disk has. 48 writes every changed buffer it can,
  keeps the ones that fail marked (with `$FF` drops them all the same) and returns the first error after. 13 and
  37 drop a buffer they can't write, log the drive out all the same and return the first such error after. 98
  (the warm boot's) drops the marked buffers of the blocks it frees, those not in the drive's permanent vector: an
  unclosed file's new blocks, whose program has ended without a close that could write them, so they don't block
  the next program's closes. A marked buffer of a block the directory has (a closed file's record written again in
  place, by a program that ended without closing it) is kept, marked (2026-10-28, the review's fourth round): the
  data is the file's, so the next warm boot's 48, a program's 48 or a close of the file tries it again, until the
  card writes it or 13 or 37 drop it. One such buffer doesn't stop anything else: GETBUF passes it by, and only a
  close of a file with that block fails on it. A sector before the data area is in no block, and is kept; nothing
  marks one (the BDOS writes none), nor a directory sector (a failed directory write drops its buffer). The warm
  boot says a failed write-back as the BDOS says its errors (`OODOS Error On B: Disk I/O`, `BDOS Function = 48`)
  and goes on to the prompt; before, the CCP's error mode `$FF` dropped it unsaid. A buffer kept so, of a sector
  that stays bad, fails every later program's 48 until 13 or 37 drops it: a ^C at the prompt is 13 (below).
- **The order of writes** (2026-10-27, from the review): data, then the entry that points at it (above); the
  entry, then the blocks freed, for delete and truncate (their pointers kept until the entry is on the disk), so a
  failed directory write leaves no entry on the disk whose blocks are free. A close merges F's block pointers with
  the entry's only once it has checked them all: two different blocks for one place (two FCBs writing one file)
  fail it, 3 to a random read or write that moves on (33, 34, 40), 1 to a sequential one (20, 21: the Guide lists
  no 3 for them; ours), and leave F and the entry as they were. A drive isn't logged out
  on a failed directory write: the buffer is dropped, the hash and the vectors not yet changed, so they still
  match what the disk has.
- **A block a write takes is zeroed** (its sectors become new buffers of zeros, changed): records never written in
  it read as zeros, not as what the block held before, so function 40 is 34. CP/M 3 leaves them undefined.
- **Allocation:** the lowest free block, not CP/M 3's nearest to the file's last; simpler, and the same on every run.
- **Login:** SELDSK as a first select (the BIOS reads the units again), the DPB from common memory (EXM's mask kept
  with the drive's other DPB values, a drive at a time), then the whole
  directory read once through the buffers: 64 sectors on `oodos-8m`, 128 on `oodos-32m`, 16 on M:. The first label's
  mode byte decides the stamps (none without a label). A read-only unit (IDENTIFY's flag) is a read-only drive from
  its login; a unit made read-only later is when its WRITE returns 2. Files are looked for up to the last entry that
  isn't free or an SFCB (a high-water mark); free entries, and 17's every entry, to the directory's end. The DPHs'
  CSV, ALV, BCB and HASH fields stay 0: the BDOS places its own, as above.
- **A rescan of the units** (a login's SELDSK, or a program's) leaves a drive alone whose unit is as it was: the
  BIOS keeps the last units table and marks each unit whose entry (its size, flags or file's name) changed
  (`DRVCHG`); the BDOS logs those drives out if they were logged in, their buffers dropped unwritten (the card's
  unit isn't what they came from), after the login's SELDSK, at each select and at 48; 13 and 37 read the units
  again first (a first select of A:), so a disk changed and then reset, as the Guide has it, has its old buffers
  dropped, not written to the new one. So does the warm boot: the CCP has the BIOS read the units (function 50's
  SELDSK of A:, a first select) before its 48, so an image swapped while a program ran, under a record it wrote
  and didn't close, isn't written to. It costs the warm boot one IDENTIFY: 17,687 cycles from WBOOT to the
  prompt, 2.5 ms at 7.16 MHz, against 3,544 without (2026-10-28, two units mounted); 17,989 with the error mode
  `$FE` for the rescan, 48 and 98 and `$FF` after (one call more, 302 cycles). A failed IDENTIFY (a card busy
  for a second, or none) changes nothing: that select has no drive, the others keep theirs. Two images of one
  name and size swapped under a drive aren't told apart: the table is all the card says.
- **The hash** is FIND's first look at each entry: its first byte, the name's 16-bit hash (each byte's low 7 bits
  XORed into a word turned left a bit before each) and the extent (EX's 5 bits, S2's low 3); then the entry is
  read and compared in full, so two names with one hash (HASHAG.TXT and HASHBA.TXT) are told apart.
- **Directory codes** (the Guide, 2.3, after Table 2-8; changed 2026-10-27, from the review): the searches return
  the entry's place in its record, 0–3; every other function 0 on success; not found, or the directory full, `$FF`
  with X 0. A close of an FCB not written since it was opened or last closed writes the drive's changed buffers
  back (a failure there ignored: nothing of this close's depends on it) and returns 0 without looking in the
  directory. f5' (the Guide's partial close) is ignored: every close leaves the FCB open, as a partial close does
  (ours). **9 and 10 are never returned** (ours) by 20, 21, 33 and 34: a close that fails leaves the FCB as it was,
  still usable (the Guide's 9 is an FCB a failed close invalidated), and a changed medium is found when the units
  are read again (13, 37, a login, the warm boot), its drive's buffers dropped, not by the FCB (the Guide's 10).
- **Wildcards:** `?` in the name or type is ? in Filename (9) for 15, 22, 23 (either name), 30, 35 and 99, after the
  FCB's drive is selected (ours: the Guide lists the errors in no order), so a drive that isn't there is Invalid
  Drive (4) first, and one that is is logged in before the name is looked at; 17, 18 and 19 take it. `?` as 17's drive byte: every entry of the current drive, to its end, free ones too, nothing
  compared. 18 goes on from 17's pattern, kept (not the FCB read again), until 13 or 37.
- **Bytes 0–12** are the program's (the Guide, 15 and 22): Open, Make and Search (EX not `?`) take S2 as 0.
- **Make** refuses a name the user has in any extent (File Exists, 8); the entry, of extent EX, and the FCB get
  every attribute 0 (the Guide, 2.3.4 and 22), no blocks. **Open** looks in the current user, then (from another
  user) for user 0's entry with t2': opened from there, f8' set, and the file read-only (File R/O on a write; its
  later extents are user 0's too). It copies f1'–f4' and t1'–t3' from the entry, clears f7', and with CR `$FF`
  gives the entry's byte count (S1, as 30 sets it with f6') in CR. **Delete** checks every match first (File R/O:
  none deleted), then deletes every extent and the file's XFCBs (user + 16); with f5', the XFCBs only. **Rename**
  takes the new name's low 7 bits and keeps each entry's attributes, its XFCBs renamed too. **30** sets every
  extent's f1'–f4' and t1'–t3', clears f5'–f8' on the disk, and with f6' sets S1 to CR.
- **Extents** as CP/M's: moving past one closes it (if written) and finds the next, made for a write; a read that
  finds none returns 1 (4 for 33) and leaves the FCB at the new extent, empty and marked (S2's `$40`; `$80` is "not
  written"), so a write there makes its entry at once and a full directory is the write's error (1, or 5 for 34
  and 40), not the close's. A read that must close a written extent on a read-only drive is Read Only (2).
- **Stamps** (2026-10-27, after the model test; the Guide decides them: 2.3.8 and functions 15, 22): only in the
  SFCB slot of a file's extent-zero entry. Make of extent 0 stamps the creation and the update (as the label's mode
  asks); closing a written FCB, moving a written FCB on to another extent, and truncate stamp the update there
  whichever extent they changed; Open of extent 0 stamps the access (in the creation's field, which they share) if
  the mode asks, not on a read-only drive. A new entry's slot is cleared, so later extents' slots stay 0.
  cpmtools stamps every entry of a file it writes; both read the first entry's. **Changed (2026-11-04, item 10):**
  the update is the first write's, not the close's ([below](#the-archive-bit-and-the-update-stamp-item-10)).
- **Truncate (99):** R0–R2 is the new last record; it must be before the file's end and where the file has data
  (the Guide: an entry reaching R's extent, R below RC if that's the entry's own, R's block there), else `$FF` (R2
  over 3 before the directory is read). Later extents' entries are deleted; the one with R gets R's
  extent, RC (R mod 128) + 1, the pointers after R's block freed and cleared, and (from 2026-11-04) its archive
  attribute t3' cleared; the update is stamped in the extent-zero entry's slot (the Guide doesn't say; a truncate
  changes the file).
- **File size (35)** leaves R0–R2 0 for a file that isn't there (as CP/M 2.2's BDOS did).
- **Rename (23)** checks the new name first: File Exists (8) before the old name's not found or File R/O (the Guide
  lists the errors in no order). **31** selects the current drive, logging it in (the Guide has it return `$FFFF`
  on an error, so it reads the disk).
- **13** reads the units again (above), writes back and drops every buffer, logs every drive out and makes it
  read-write, A: current, the DMA address `$0280`; the user number stays. **37** does the first four for the drives
  in A/X. A buffer either can't write is dropped, and the first such error returned after the reset is done. **28** makes the current
  drive read-only until one of them; **46** is (DSM + 1 − the blocks in use) × the records a block, from the current
  vector; **48** with `$FF` drops the buffers too; **98** makes each logged drive's current vector its permanent one.
- **Errors:** the codes are CP/M 3's (1 Disk I/O, 2 Read Only, 3 File R/O, 4 Invalid Drive, 8 File Exists, 9 ? in
  Filename). The message is `OODOS Error On A: Disk I/O`, then `BDOS Function = 15` and, for a function with an FCB,
  ` File = README  .TXT` (the name's 8 and the type's 3 as the FCB has them), CR LF after each.
- **The stand-in CCP** sets user 0, opens `A:NAME.COM` and reads record 0 into its buffer: the header to `$0280`,
  checked, the image's first 112 bytes (or fewer) to `$0300`; then whole records straight to their place by the DMA
  address, and a last part record through its buffer. Open's `$FF` with X 0 is `NAME?`, any other error `READ ERROR`.
  The user is put back as it was before the program runs and after `NAME?` or an error. A program starts in the
  default error mode with the DMA address `$0280`.
- **^C at the prompt** resets the disk system, as the Programmer's Guide says CP/M 3's CCP does (1.6.2; the warm
  boot doesn't), 2026-10-28 from the review: a ^C while the prompt's line is empty, which is function 10's warm boot, as anywhere.
  The CCP marks the prompt's line read (`ATPROMPT`, set only while its function 10 runs), and the warm boot that
  finds it marked runs 13 in error mode `$FE` instead of the rescan, 48 and 98, which ran before that prompt; then
  the drive that was current is selected again (from 3b2, the CCP's own drive, made current through the SCB) and the
  prompt comes back. 13 writes what it can, drops what it can't (a kept buffer of a bad sector too) and says the
  first failure (`OODOS Error On B: Disk I/O`, `BDOS Function = 13`), and makes every drive read-write. Both
  keyboards and the serial line give ^C as `$03`. A ^C in a program's own line input is the warm boot, as before
  (from 3b2, a ^C while the line isn't empty is a character of the line, as the Guide says); Break is the monitor's,
  as before.
- **Function 50** forgets which drive the BIOS has selected, so the BDOS selects its own again before its next
  sector: a program's SELDSK can't send the BDOS's reads to another drive.
- **UNITS** (SELDSK's first select) resets a card it finds busy, as READ and WRITE do, so a login after a stuck card
  works.
- **The DPBs** are checked as they're assembled (`bios.s`'s DPB macro): SPT 64, BSH 3–7, DSM under 4,096, DRM under
  2,048, the drive's sectors in 16 bits, EXM as CP/M 3 works it out, AL0 and AL1 the directory's blocks: what the
  BDOS and the BIOS assume of every drive.
- **Measured** on `oodos-8m`, BDOSENT to its return: Open 5,327 cycles; a sequential read of a record in a buffered
  sector 4,585, of one whose sector it reads 12,649; a write to a buffered sector 7,380, one that takes a new block
  79,766, and 165,186 for the cheapest that writes buffers back first (one moving to the next extent, which writes
  the drive's data back before the entry; before that order a write found changed buffers to take, 93,201). The
  stub adds 216. Most of a buffered read is three DMA copies through MOVE: the FCB in and out, and the record.
- OODOS.SYS stayed 0.2 here (the version's minor wasn't changed in phase 3); 0.3 (2026-11-05) is phase 5's end.

**Decided (2026-10-28): 3b is two parts, each reviewed and merged on its own:** **3b1 the file extras** (44, labels,
102, 103, 106, XFCBs and passwords) and **3b2 the system extras** (the SCB, the full console set, 47, 107, 108,
152), built side by side from 3a's merge. **Passwords are enforced** as the Programmer's Guide says (read, write
and delete modes in the XFCB, the default password of 106, the password in the DMA buffer on the protected
functions), turned on per drive by its directory label, so a drive without it behaves exactly as now. **The model
and the random comparison grow to cover 3b1's file functions;** 3b2's console, SCB, chain and parse get directed
tests.

**Proposed (3b1, as built, 2026-10-28)** ([`oodos/sys/bdosxtra.s`](../oodos/sys/bdosxtra.s), with changes to
`bdos.s`, `bdosfile.s`, `bdosdisk.s` and `ccp.s`; the Guide: 2.3.6–2.3.8, 2.3.10, 2.3.13 and functions 15, 19–23,
30, 33, 34, 40, 44, 99–103, 106; where it is silent the choice is ours, marked so):

- **On the disk, byte for byte CP/M 3's** (cpmtools reads both): the label is `$20`, the name and type, the data
  byte (12), the password's key (13), its bytes 14 and 15 (0 in a new one), the password (16–23), its creation and
  update stamps (24–31). An XFCB is 16 + the user, the file's name and type (their low 7 bits, ours), the mode (12:
  bit 7 read, 6 write, 5 delete), the key (13), 0 (14, 15), the password (16–23), 0 (24–31). The password is kept as
  cpmtools decodes it (CP/M 3's): its 8 bytes in reverse order, each XORed with the key, the 8 bytes' sum. The
  password mode is also in the file's extent-zero SFCB slot (its byte 8), equal to the XFCB's whenever the file
  has an extent-zero entry with a slot: set by make with f6', 103, and 19 with f5' (0); and when the extent-zero
  entry is made after the XFCB (make at a later extent, with f6' or then 103, and a write of extent 0), the new slot
  takes the XFCB's mode, whether the label's passwords are on or off then (ours: an XFCB is made only with them on;
  the Guide, 2.3.8: 0 only for a file not protected). A later extent's slot is 0, so a file with an XFCB but no
  extent-zero entry has its mode in no slot (cpmls -F, which reads the lowest extent's, shows none).
- **The label (100)** (ours where marked): the first `$20` entry, or a new one in the first free entry (`$FF` if
  there's none). Its name and type are the FCB's bytes 1–11 as they are; its data byte is FCB byte 12's bits 7–4
  and bit 0 set (ours); stamps asked for (bits 6–4) on a directory without SFCBs (its entry 3 isn't one, ours) are
  `$FF`, nothing done. A label's password, if it has one, must be given (Password Error, 7, else), whatever bit 7
  says; with byte 12's bit 0 the new one is the DMA buffer's bytes 8–15, else the old one is kept. 100 stamps the
  label's update, and its creation when it makes it, whatever the mode (ours: the Guide says only that the label is
  stamped). The drive's stamp and password modes are the new byte's from then on. A read-only drive is Read Only
  (2). The order: the drive (4), Read Only (2), stamps without SFCBs, the password, no room.
- **101** returns the label's byte 12 as the disk has it, 0 without a label (A = `$FF`, X 4 for no drive).
- **No password** (ours): a password of 8 blanks, or 8 zero bytes (as `mkfs.cpm` leaves a label's), is none, and
  anything given matches it; it is written as 8 blanks, by 100, 103 and make (cpmtools's `fsck.cpm` wants a
  printable password once a mode or bit 7 is set). Otherwise the 8 bytes must match exactly (no case folded,
  ours). The default password is none from the cold boot, kept until 106 replaces it (the warm boot keeps it: the
  Guide). The password given is read from the DMA address at the call, 16 bytes (a new one too).
- **Protected files,** with the label's bit 7 set and an XFCB whose mode has any of bits 7–5, unless the DMA
  buffer's first 8 bytes or the default password match: Open (15) of a read-protected file is Password Error;
  of a write-protected one it opens with f7' set, kept as the FCB moves to other extents, and a write is File R/O
  (3), as the Guide has it. Delete (19, with f5' too), rename (23), 30 and truncate (99) of a protected file are
  Password Error. Searches aren't affected, nor 102. **The order** (ours): Password Error is the last check, so a
  call that fails for another reason fails as before (delete: a read-only match is 3 before a protected one is
  7; truncate: R where the file has no data is `$FF` first). Delete and rename check the XFCBs their name matches.
  Open from another user of user 0's SYS file checks user 0's XFCB.
- **Make (22) with f6'** and passwords on makes the file's XFCB too: the password the DMA buffer's bytes 0–7, the
  mode its byte 8 (bits 7–5, ours), in the first free entry after the file's. It needs two free entries, or an
  XFCB of the name to use again, else `$FF` and nothing made (ours). Without passwords on, f6' is ignored.
- **An orphaned XFCB** (one whose file is gone, as a non-banked CP/M 3, which has no XFCBs, can leave) is left
  alone (ours): rename's File Exists check of the new name looks at files only, so renaming a file that has an
  XFCB onto an orphan's name leaves two XFCBs of the name (the first found counts), and make with f6' takes an
  orphan over without its password.
- **103** makes or changes the file's XFCB: the mode FCB byte 12's bits 7–5, a new password (with bit 0) the DMA
  buffer's bytes 8–15; without bit 0 a new XFCB's password is none, an old one's kept (ours). A protected file's
  password must be given. `$FF` with X 0: no label or passwords off, the file not found, no free entry. The order
  (ours): the drive (4), `?` (9), a read-only drive (2), passwords off, not found, Password Error, no room.
- **102** puts the XFCB's mode in FCB byte 12 (0 without one, whatever the label's bit 7 says: an XFCB is a password
  assigned) and the file's extent-zero SFCB slot's stamps in bytes 24–31 (zeros without one), and writes back the
  FCB's 33 bytes; the current user's file only (no user 0 SYS file from another), any extent found (ours). The
  Guide's 102 lists the delete mode as bit 4 where 2.3.6 and 103 have bit 5; OODOS returns the byte the XFCB holds
  (bit 5), taking 4 for a slip.
- **Stamps** as 3a's, by the label's mode now that 100 can change it: create and access share the first field
  (both may be asked for; `fsck.cpm` calls that an error, the Guide doesn't), open of extent 0 stamps the access.
  **Changed from 3a:** a made entry's SFCB slot is cleared whatever the label asks (3a left it when the mode asked
  for no stamps), since 102 reads it: the Guide has zeros for a stamp not made and a mode of 0 for a file not
  protected (2.3.8, 102), and a slot left as it was would give another file's. Found by cpmtools at verify
  (`fsck.cpm` read `mkfs.cpm`'s `$E5`s as stamps).
- **The multi-sector count (44)** is the SCB's byte at `$4A`, CP/M 3's offset, its one home (`SCB_MULTI` in
  `sys.inc`, with the other SCB offsets); 1 to 128, else A = `$FF` and the count kept. The CCP sets it to 1 at
  each warm boot, before its own loads and each program (the Guide, 44); a count of 0 (never set) is taken as 1. 20, 21, 33, 34 and 40 with a count N
  are N calls, the FCB read once and written back once, record k at the DMA address + 128 × k (the DMA address
  itself unchanged): they stop at the first that doesn't succeed, and an error but a physical one (A = `$FF`)
  returns X = the records done before it (the Guide's H). 33, 34 and 40 put R0–R2 back to the first record's; EX
  and CR are the last record tried's (ours: N calls leave them so). A physical error (DERR) returns as with a count
  of 1 (X its code), and once a record is done the FCB is written back as the call left it (ours: N calls would
  keep the records done): 20 and 21 at the record after the last done, 33, 34 and 40 with R0–R2 put back and CR
  the last record whose place was set: the one that failed if it failed being read or written, the last one done
  if it failed moving to a new extent (XTRATEST Q: 64 records from 64, then extent 1's move fails, CR `$7F`).
  **The memory first** (ours): before the first record, after the FCB's drive and, for a write (21, 34, 40), the
  R/O drive and R/O file checks (`FCBSEL`, `WCHECK`, then `MRANGE`), the whole of it, the DMA address to + 128 ×
  N, must be the program's memory, else A = X = `$FF` with nothing done and the FCB as it was, the error a record
  there gets (PIN and POUT's, "What isn't memory"): past `$FFFF`, in the I/O hole, on flash, or for a read (20,
  33) in a read-only window. N calls would do the records before it; refusing it whole keeps the BIOS's rule that
  all is checked before a byte moves, and a write never takes a block it doesn't keep. A count of 1 is checked the
  same way, so a read to memory that isn't is `$FF` even at the file's end.
- **Where it lives:** block 60, now 8,277 bytes of 16 KB (6,868 before 3b1), so block 62's `BDOS2` is still unused;
  the zero page 203 of the 240 bytes (184 before), `SYSBSS` 676 of 1,280 (627 before): the default password, the
  DMA buffer's 16 bytes, an entry's password decoded, `XCLEAR`'s and `NEWMODE`'s save of the search; the
  multi-sector loop's state in the zero page. The CCP's code is 7 bytes more (block 62's code window: 1,147 bytes). The label's place is
  found when 100 needs it, not kept.
- **Measured** (`oodos-8m`, BDOSENT to its return, FSTEST with a count of 1, against 3a's): Open 5,359 cycles
  (5,327); a sequential read of a record in a buffered sector 4,679 (4,585), of one whose sector it reads 12,763
  (12,649); a write to a buffered sector 7,490 (7,380), one that takes a new block 77,965, the cheapest that writes
  buffers back first 164,188. The count's loop costs about 100 cycles a call, the record's address and an indirect
  call; passwords off cost a `BIT` in open and nothing elsewhere. With a count of 128 on M:, a read is 553,186
  cycles, 4,321 a record (against 4,723 for one buffered record a call: the FCB is copied in and out once a call),
  and a write 779,555, 6,090 a record. The check of the records' memory first costs about 45 cycles a page they
  cover (about 3,000 for 128 records, 550,107 and 776,646 before it; about 100 for one record, not in the
  figures above). The warm boot makes one more BDOS call (44): the stub's 216 cycles and a
  few more (not measured).

**Proposed (3b2, as built, 2026-10-28)** ([`oodos/sys/bdos.s`](../oodos/sys/bdos.s), [`bdossys.s`](../oodos/sys/bdossys.s),
[`ccp.s`](../oodos/sys/ccp.s); where the Guide is silent the choice is ours, and says so):

- **The SCB (49)** is at `$0200` in the system block, CP/M 3's 100 bytes at CP/M 3's offsets, and it is where the
  BDOS keeps that state: there is no second copy. Cold boot sets: the version `$31` (05); the console width 79
  (1A) and page length 24 (1C); the redirection vectors (22–2B, [BIOS](#bios)); page mode 0 (2C); ^H a
  backspace (2E, 0) and rub/del as ^H (2F, `$FF`), so DEL erases as it has since phase 2 (ours: CP/M 3's
  GENCPM offers both, and the Guide gives no default); the console mode 0 (33–34); the delimiter `$` (37); printer
  echo off (38, 0); the DMA address `$0280` (3C); drive A: (3E) and user 0 (44); the multi-sector count 1 (4A,
  `SCB_MULTI`: function 44 is 3b1's, which reads and sets it there); the default error mode (4B); the drive search
  chain the default drive alone (4C–4F: 0, then `$FF`); the temporary file drive the default (50, 0); the BDOS
  flags `$80` (57: the long error messages, as OODOS always gives); the date and time (58–5C, the clock as cold
  boot read it); the common memory base `$F800` (5D–5E). The error drive (51) is the drive of the last error
  DERR reports. The user flags (06–09) are the program's; the rest of the reserved bytes are 0 but `$17`,
  OODOS's: function 47's request to the CCP (bit 7 a command waits at `$0280`, bit 6 with the program's drive and
  user).
- **49's parameter block** is CP/M 3's: the offset, the operation (0 get, `$FF` set a byte, `$FE` set a word), the
  word. A get returns the word at the offset in A/X (A the byte, as CP/M 3's A and L; X the next, as H; 0 at `$63`);
  a set returns A = X = 0 and a word set at `$63` sets only that byte (ours: the SCB ends there). An offset past
  `$63` does nothing and returns A = X = 0, as the Guide says. The operations `$01–$FD`, reserved in the Guide, are
  taken as a get (ours). **Every byte may be set,** as in CP/M 3 (the Guide warns, but doesn't refuse), and because
  the BDOS has no other copy, a set takes effect at once: the DMA address is 26's (here it does work, unlike CP/M
  3's), the drive and user are 14's and 32's (the drive not checked: one that isn't fails Invalid Drive when a
  function uses it; the user kept to 0–15 as 32 keeps it, its low four bits, 2026-10-29 from the review: a user of
  16 or more would make directory entries that aren't files), the error mode is 45's, the console mode 109's, the
  delimiter 110's, the width function 10's, the vectors the BIOS's devices. The date and time are the clock as last
  read: the BDOS reads the clock into them for each stamp and for 105, so a set lasts until then; 104 sets the
  clock. 12 returns `$0031` whatever 05 holds.
- **CP/M 3's two current drives and users** (the Guide, 1.7 and 2.3.5, against Appendix A): the SCB's 3E and 44 are
  the BDOS's current drive and user, as Appendix A and functions 14, 25 and 32 have them, and the CCP keeps its
  own, which 1.7 calls the CCP's and the prompt shows: a program's 14 or 32 changes the BDOS's, and the warm boot
  puts the CCP's back (2.3.5: "the CCP's user number is restored"). Until 3b2 the stand-in CCP took whatever a
  program left; now it starts with A:, user 0, and only function 47 changes them (phase 4's CCP has `B:` and
  `USER`). There is no copy of them in the base page (CP/M 2's byte at `0004`): OODOS's base page has the BDOS's
  `JMP` where CP/M's 0003–0004 are.
- **The page length and page mode** (1C, 2C) are set and kept for the CCP and the utilities (phase 4); the BDOS
  doesn't use them, as CP/M 3's doesn't.
- **The console mode (109),** CP/M 3's bits: 0, function 11 reports only ^C; 1, no ^S, ^Q or ^P handling in
  functions 1, 2, 9, 11 and 111; 2, raw output (no tab expansion, no printer echo) for 2, 9 and 111; 3, no ^C
  ending a program. Bits 8 and 9 (for a redirecting RSX) and the rest are kept and not looked at. 109 with
  `$FFFF` returns the mode in A/X; any other value sets it and returns 0.
- **^S, ^Q and ^P** (the Guide, 2.2.1): before each character functions 2, 9 and 111 write, unless mode bit 1 is
  set, the BDOS looks at the console: no key, it writes; a ^S stops it until a ^Q; a ^Q or a ^P on its own is
  dropped; any other key is kept (one), for the next input function, and while one is kept the output doesn't
  look again (as CP/M 2.2's did; ours). Stopped, it reads keys: ^Q goes on; ^C is the warm boot (unless mode bit
  3), the return code `$FFFE`; ^P turns printer echo over (a BEL when it goes on, as the Guide says) and stays
  stopped (ours: the Guide has output go on at ^Q only); anything else is a BEL and dropped. Function 1 handles a
  key the same way and waits on (a ^S, ^Q or ^P is never returned unless mode bit 1); function 11 too (a ^S
  stops it, then it returns 0). A ^C not after a ^S is a key like any other in these functions, kept by the
  output's look (ours: the Guide names ^C only after ^S).
- **The kept key** is taken first by 1, 6 (`$FF` and `$FD`; `$FE` reports it), 10 and 11 (ours: the Guide's
  function 6 bypasses the BDOS's handling, but a key it ignored would sit there unread). The warm boot doesn't
  drop it, so a key typed during a program's output reaches the next prompt, as typed ahead. A ^C kept so,
  typed ahead or found by 11 in mode bit 0 and left unread by a program that then ends, is the prompt's ^C: a
  second warm boot, with 13 (accepted 2026-10-30 from the review, as CP/M 2.2 leaves such a key in the BIOS).
- **Printer echo** is the SCB's list output flag (38): while it's on (non-zero), what functions 1, 2, 9, 10 and
  111 echo or write to the console goes to LST: too, but not in raw mode (bit 2) for 2, 9 and 111; 6's output
  never. ^P in function 10, anywhere in the line, or after a ^S, turns it over, to 1 or 0.
- **Function 1** echoes graphic characters (`$20`–`$7E`), CR, LF, BS and tab (expanded), as the Guide lists; other
  characters come back unechoed and take no column: control characters, ^C among them, DEL and bytes over `$7F`.
  **2** expands a tab to the next eighth column (not in raw mode); every character counts in the SCB's column, BS
  back one, CR to 0. **6:** `$FF` a key or 0, `$FE` `$FF` or 0, `$FD` a key waited for, anything else written as it
  is (no column, no handling). **9** writes to 110's delimiter; **111** writes the CCB's (address, length)
  characters, as 9 does; **112** sends them to LST: as they are. **11** returns 1 if a key waits (kept, or in the
  BIOS: then read and kept, after the ^S/^Q/^P handling), 0 if not; with mode bit 1 it is the BIOS's status, nothing
  read. With mode bit 0 it reads waiting keys until a ^C, which it returns as 1 and keeps, so 11 stays 1 and the
  next 1 or 6 reads the ^C; the others are dropped (ours, 2026-10-29 from the review: the Guide says only that 11
  returns 01H when a ^C has been typed; a kept key that isn't ^C would stop the next ^C being seen, and DIR and
  TYPE, which use the mode, want only ^C). **110** with `$FFFF` returns the delimiter in A, else sets it from A.
  **12** returns `$0031`.
- **AUX: and LST:** (3, 4, 5, 7, 8, 112), as the BIOS has them: AUX: is TTY, the serial line, which is also part
  of CON:, so function 3 and the console read the same line and a key goes to whichever asks first; LST: is LPT,
  which drops what it's sent and is always ready, as the Guide has an output device that isn't there do nothing.
  7 and 8 return `$FF` or 0. The BIOS's input from a vector with no input device (none, or LPT alone, as a
  program may set with 49) returns ^Z at once, as the Guide has it, where it waited for ever before.
- **Function 10** is the banked CP/M 3's (the Guide's Table 3-3), on a line kept in the system block (`$0B00`, and
  the previous line at `$0C00`, 255 bytes each): the cursor anywhere in the line, typing inserting at it. rub/del
  and ^H (backspace: the character before the cursor taken out; with 2E `$FF` or 2F 0, at the line's end, rub/del's
  echo of the taken character instead, as the Guide says), ^A and ^F left and right, ^B to the start (or from there
  to the end), ^E the line on from the cursor shown on a new row, ^G the character at the cursor taken out, ^K
  everything from it, ^X everything before it, ^R the line typed again on a new row, ^U the line to the cursor made
  the previous line, the line emptied and a new row, ^W the previous line back into an empty line (or the cursor to
  the end), ^P printer echo, CR and LF end it (the whole line, wherever the cursor: it becomes the previous line).
  Any other control character is part of the line and shown as `^X`; ^C while the line is empty (at its start,
  nothing typed or all of it taken out again) is the warm boot (return code `$FFFE`, unless mode bit 3: then it's a
  character; ours: the Guide has a ^C at the line's start end the program, and a line with characters after the
  cursor at its start isn't empty here). A character past the buffer's size is dropped with a BEL; a size of 0 is 1. With A/X = 0 the buffer is
  at the DMA address and its characters, to a 0 (or the size), are taken as typed first, so a CR among them returns
  at once. The end echoes CR only, as since phase 2. **Ours, where the Guide is silent on the screen:** the line is
  shown on rows of the console width (SCB 1A, columns 0–79 by default), never writing the width's last column (the
  Guide says typing there mustn't advance the terminal, and the OO-1's screen does); a row starts where the cursor
  was, the first one at function 10's start; typing at the end of a full row starts a new one (CR LF), typing in the
  middle pushes what doesn't fit off the row's right, and a move or deletion to before the row's start types the
  line again on a new row, from as far back as fits; a deleted or moved character is erased with BS, space and BS.
  ^U and ^R print no `#` (ours: CP/M 3's, as CP/M 2.2's, put a `#` at the cursor before the new row, as its User's
  Guide shows; the Programmer's Guide's Table 3-3 names none, and the new row shows the change). A ^C in a line that isn't empty is a character, shown
  `^C` (the Guide); it isn't the warm boot. Rub/del's echo (2F 0, or ^H with 2E `$FF`) keeps to the rows as typing
  does: an echo that doesn't fit starts a new row.
- **Function 47** takes E = `$FF` (the program's drive and user to the CCP) or anything else (the CCP's), sets the
  SCB's `$17` and ends the program by the warm boot. The command is the program's at `$0280`, to its 0, as the
  Guide says, at most 126 characters (the tail's room; more are cut off, ours). The CCP (the stand-in until 4a), at
  the warm boot after its usual work, shows its prompt and the command, as if typed (ours: the Guide doesn't say),
  and runs it.
  With `$FF` the CCP's own drive and user become the program's, and stay so after the chained program (ours: the
  Guide has the CCP set the default drive and user to the program's for the chained program; here they are the
  CCP's own from then on, so the prompt after it shows them too); otherwise it keeps its own. The chained program gets the return code as it was.
- **108:** the SCB's return code (10–11): `$FFFF` returns it in A/X, any other value sets it. The CCP sets it to 0
  as it starts a program that wasn't chained to; the BDOS sets `$FFFD` when it ends a program for an error (the
  default error mode) and `$FFFE` for a ^C (function 10's, after ^S); the warm boot keeps it, for the next
  program. The CCP's conditional commands (`:`, phase 4a) read it.
- **107** returns OODOS's serial number, six bytes, the same in every copy (ours): `4F 4F 44 00 00 01`, "OOD"
  (the program header's magic) and serial 1.
- **The CCP at each warm boot** (the Guide: what it initializes when it loads a program, and its own strings need
  `$`): the console mode 0, the delimiter `$` and the multi-sector count 1 (by 44, first, since the merge with 3b1);
  its drive and user made current again for
  the prompt (the drive through the SCB, ours: 14 would log it in, and the warm boot has logged in nothing
  since 3a); then 47's command, if one waits. A program starts with the
  return code 0, unless chained to.
- **152** parses `{d:}filename{.typ}{;password}` (the Guide: no user number, so `5A:X` is the name `5A`, ended by
  the colon, its third character) into bytes 0–23 of the FCB: the drive (0 if none; `A`–`P`, either case, 1–16),
  the name and type in capitals, blank-padded, `*` filling the rest of its field with `?`; 12–15 zero; the
  password, in capitals, blank-padded, at 16–23. Bytes 24–35 aren't touched (the Guide reserves 24–31; ours). Blanks
  and tabs first are skipped; the delimiters are the Guide's: space, tab, CR, NUL, `;` (but before the password),
  `=`, `<`, `>`, `.` (but between name and type), `:` (but after the drive), `,`, `|`, `[`, `]`. The string is
  read to 128 bytes at most, as if a NUL followed. **Errors** (A = X = `$FF`, the FCB left as it was; ours: the
  Guide doesn't say what an error leaves): a name over 8, a type over 3, a password over 8, a character in a field
  after its `*` (it is full), a control character 1–31 that isn't a delimiter, DEL or a byte over `$7F`, and a
  colon second after a character that isn't a drive letter, or after `Q`–`Z`. **The result** is the Guide's: 0 if
  only blanks and tabs, then NUL or CR, follow; the delimiter's address if one follows them; else the address of
  the first blank after the name. A string of blanks to NUL or CR returns 0 and the FCB blank (drive 0). An empty
  name (`=X`, `.TXT`) isn't an error: the fields are blank.
- **Where it lives:** the console functions, 47, 49, 107–112 and 152 are `bdossys.s`, in block 60 with the rest:
  2,132 bytes, block 60 now 8,819 of 16,384 (3a's 6,868), so `BDOS2` is still unused. 25 bytes of `SYSBSS` (652
  of 1,280 now), no zero page (182 of 240, two fewer: the old function 10's), and the two lines, 512 bytes at
  `$0B00–$0CFF` of the system block, outside `SYSBSS`, which cold boot clears with the rest. The CCP grows by 189
  bytes (1,329 of block 62's code window); common memory is as it was (1,107 of 2,048).
- **Measured** (`test_oodos_sys`, BDOSENT to its return): function 2, 5,706 cycles a character to the screen and the
  serial line, of which the look at the console is 265 (the serial card's wait for its character, 3,840, is most of
  the rest); function 11 with no key, 323; function 10, a key into the line (INSERT to the next key), 5,994 on
  average over SYSTEST's 154, most of it the echo's BIOS CONOUT.

**3b1 and 3b2 together (merged 2026-10-30):**

- **One dispatch table:** FJUMPS has 64 handlers, the most its offsets allowed then (the `.assert`, 0–126): 3a's 44,
  3b2's 14 at offsets 88–114 (3, 4, 5, 7, 8, 47, 49, 107–112, 152), 3b1's 6 at 116–126 (44, 100–103, 106); NFUNC
  153. A function past these needed another dispatch: 3c's widened it (below).
- **One multi-sector count,** the SCB's `$4A` (`SCB_MULTI`, in `sys.inc`): 44 sets it (1–128), 49 reads and sets it
  (any byte, as CP/M 3; a count over 128 set so is taken as it is, 0 as 1), and the CCP sets it to 1 once at each
  warm boot, by 44, before its own loads (3b2's set by 49 later in the warm boot dropped). XTRATEST K: 44's 5 read
  by 49, 49's 3 the records one 21 writes.
- **A physical error** in the default error mode ends the program with the return code `$FFFD` (3b2); in mode `$FE`
  or `$FF`, a multi-sector call's records done are kept (3b1's `MERR`) before it returns.
- **Where it lives:** block 60 10,227 bytes of 16,384 (`BDOS2` still unused); the zero page 201 of 240; `SYSBSS`
  701 of 1,280; the CCP's code window 1,323 bytes; common memory 1,107 of 2,048, as it was.

**Decided (2026-10-30): phase 3c, the memory functions (51–58) and OODOS's 73 and 74.**

1. **Allocatable memory is banks 1–5 only:** 20 blocks of 16 KB, 320 KB, as function 72 already reports. The
   granularity is a 16 KB block (1,024 paragraphs): lengths round up, bases are block boundaries.
2. **Function 51's DMA segment applies to disk transfers only,** as in CP/M-86: the records of 20, 21, 33, 34 and
   40 (single and multi-sector), and the directory record 17 and 18 return in the DMA buffer. Console strings (9,
   10, 111 ...) and FCBs stay CPU addresses. `$FFFF`, the default, set at cold boot and every warm boot, means the
   DMA address is the program's CPU address, as before. 3b1's range check (`MRANGE`) checks the physical range
   when a segment is set.
3. **Function 74 refuses A:,** the system drive. It mounts and unmounts B:–G: only, and logs that drive out so the
   next access sees the new image.
4. **Testing:** directed tests for every function; the random model comparison gains a DMA segment, so file I/O
   through physical addresses is compared with the model; 53–58, 73 and 74 get directed tests only.

**Proposed (3c)** (the choices where this document and CP/M-86 are silent are ours, and say so):

- **The dispatch** is widened first: the "no handler" test is `CPX #$FF` (it was `BMI`, which capped FJUMPS at 64
  handlers, all used); FJUMPS may hold 127. Two cycles a call more.
- **51** takes the segment in A/X and returns A = X = 0; any value is taken, and checked when a record moves. The
  physical address is segment × 16 + the DMA address (26's, the SCB's `$3C`). **What a segment may reach** (ours):
  any RAM but OODOS's own: banks 0–5, 8–B (video memory) and E (native mode may use bank E: the BIOS's `C806R`
  copies the IIe ROM into it from the cartridge each time Apple mode starts); refused are banks 6–7 (the hash, the buffers, the
  system block), C–D (M:) and blocks 60–63 (the BDOS, the BIOS, block 62, the ROM BIOS's copy), and anything past
  `$FFFFF`. Flash has no physical address (a window's ROM bit), so it can't be named. A DMA address whose records
  run past `$FFFF` is refused as without a segment. Refused memory is the error any memory that isn't gets: A = X =
  `$FF` and nothing done, checked before the first record for 20, 21, 33, 34 and 40 (after the FCB's drive and a
  write's R/O checks, as `MRANGE` is now), and at the move for 17 and 18 (their search goes on, as with a DMA
  address in the I/O hole now). Function 13 sets the DMA address to `$0280` and keeps the segment (ours). What else
  uses the DMA address stays the program's CPU address: 46's free space, the passwords 15, 19, 22, 23, 30, 99, 100
  and 103 read there, and 10's buffer with A/X = 0 (ours: decision 2's "disk transfers only").
- **The BIOS's XMOVE** takes a physical bank, `$80` + A16–A19 (ours): MOVE's address on that side is then the
  physical address's low 16 bits. The BDOS moves a segment's records so. Function 50 passes it on, as it passes
  bank 0, the system's view, now.
- **52** takes the address of 4 bytes it fills (ours): the DMA address (+0–1, CP/M-86's BX) and the segment (+2–3,
  its ES); A = X = 0.
- **The MCB** is CP/M-86's: +0–1 M_BASE (a paragraph), +2–3 M_LENGTH (paragraphs), +4 M_EXT. Results (ours, as
  OODOS maps CP/M's): A = 0 done, A = `$FF` with X 0 no memory (CP/M-86's AL `$FF`), A = X = `$FF` an MCB that
  isn't memory (the I/O hole), nothing written. A length is rounded up to whole blocks (0 is none: `$FF`); a
  length out is the blocks' (× 1,024). A base must be a block's first paragraph in banks 1–5 (`$1000`, `$1400`,
  ... `$5C00`), else `$FF`.
  - **53** (MC_MAX): the largest run of free blocks, at most the length asked (rounded up to whole blocks),
    allocated, as CP/M-86's: M_BASE its base (the lowest of equal runs), M_LENGTH its length, M_EXT 1 if a free
    block is left outside it, else 0. (3c first allocated nothing; review, 2026-10-30: CP/M-86's 53 and 54
    allocate the run they report.)
  - **54** (MC_ABSMAX): the same from M_BASE: the run of free blocks starting there, at most the length asked,
    allocated; shorter if a block in it is taken (ours), `$FF` if the base's own block is.
  - **55** (MC_ALLOC): the lowest run of free blocks of the length; M_BASE its base, M_LENGTH the length rounded.
  - **56** (MC_ABSALLOC): the length from M_BASE, all of it free, else `$FF` and nothing allocated (ours: no
    shrinking); M_LENGTH the length rounded.
  - **57** (MC_FREE): M_EXT `$FF` frees everything, as 58 (CP/M-86's); otherwise M_BASE an allocation's base
    frees that whole allocation, whatever M_LENGTH; M_BASE a later block of one, with M_LENGTH (rounded up to
    blocks) reaching the allocation's end, frees from M_BASE to that end and keeps its head allocated, as CP/M-86
    frees a region's top part. The rule where CP/M-86 is silent is ours: a length past the end frees only to it;
    a shorter one, a length of 0, a free block or any other base is `$FF`, nothing freed.
  - **58** (MC_ALLFREE) frees every allocation; A = X = 0.
- **Who owns an allocation** (ours): the running program. The warm boot frees all, as 58, and sets the segment
  back to `$FFFF`, in common memory's WARM, so a program that ends any way leaks nothing. The map is a byte a
  block in `SYSBSS`, 20 bytes: 0 free, `$81` an allocation's first block, 1 a later one.
- **73** takes the address of 4 bytes (ours): +0–1 a CPU address in, the program's; out +0–1 the offset (0–15)
  and +2–3 the paragraph, so the physical address is paragraph × 16 + offset (and 51 with the paragraph and 26
  with the offset name it), through the windows the stub saved (W3 a far call's block, if one is mapped). A = X
  = 0. An address in the I/O hole, or in a window on flash, is A = X = `$FF`, the 4 bytes as they were; a
  read-only window translates (it is RAM).
- **74** takes the address of a block laid out as the card's MOUNT sector, with a drive for the unit (ours): +0
  the drive (0–15, as 14's: 1–6 are B:–G:), +1 the flags (bit 0 read-only), +2 the path to its NUL, as DRIVES.TXT
  has it: from the root, `/`-separated, no name empty, `.` or `..` (so no `//` and no `/` at the end), no space,
  tab, `#`, control character or DEL, at most 509 bytes. An empty path unmounts the drive. In order: the drive
  (B:–G:), the path, then the drive's changed buffers written back: one that fails refuses the call, nothing
  mounted and the buffers kept (marked, as a write-back that fails keeps them now). Then the drive's buffers are
  dropped and it is logged out, read-write; the card's MOUNT (or UNMOUNT); the units read again (a rescan, as
  13's; a rescan that fails, the units table not read, unmounts the unit again and is 1, the card failed); then
  the new unit checked: 16,384 or 65,536 sectors, and no other unit on a file of the same name (the
  units table's 24 bytes, DRIVES.TXT's rule). A unit that fails those is unmounted again: the drive has no image.
  A drive unmounted (or left so) is no drive: a later access is Invalid Drive (4), as a drive DRIVES.TXT doesn't
  name. A = 0 done; else A = `$FF` and X (ours; 74 returns its errors whatever the error mode, and says nothing):
  1 a write-back or the card failed (no card, no FAT, a timeout), 4 not a drive 74 changes (A:, M:, H:–P:), `$10`
  the path refused, `$11` not found (or a directory), `$12` the image's size, `$13` another drive's file. Unmounting
  a drive with nothing mounted is done (A = 0).
- **The BIOS's mount** (ours) is `SDMNT`, a routine of block 61 the BDOS calls directly (with W2 on block 61),
  not through the jump table, so programs reach it only by 74: the card's MOUNT of the system block's sector
  buffer, or UNMOUNT of a unit, through `SDIO`, which now takes its command from a table by `DOP` (0 READ, 1 WRITE,
  2 UNMOUNT, 3 MOUNT; bit 0 the DMA's direction) and lets UNMOUNT end with nothing moved. Six instructions more.
- **The model comparison:** `cpm3model.py` gains 51, 50's XMOVE and MOVE for the forms DRIVER uses, and physical
  memory of its own; a segment's records go there, refused as the BDOS refuses them. A new operation, SMULTI,
  sets a segment and a DMA address, runs a transfer, clears them, and DRIVER moves the records between its buffer
  and the physical address by function 50 before and after, so their bytes are compared. The generator makes
  them from a fourth random stream, so a seed's script is as before up to its first.

**Proposed (3c, as built, 2026-10-30)** ([`oodos/sys/bdosmem.s`](../oodos/sys/bdosmem.s), with changes to `bdos.s`,
`bdosfile.s`, `bdosdisk.s`, `common.s` and `bios.s`; the list above is what was built, and these are the details):

- **The dispatch:** FJUMPS has 74 handlers, 3c's 10 at offsets 128–146 (51–58, 73, 74); NFUNC is still 153. The
  `.assert` allows 127 and fires on 128 (checked on a scratch copy with 63 and 64 more). Measured, every call
  2 cycles more: Open 5,340 cycles to 5,342, function 11 with no key 327 to 329.
- **The records** go through `DOUT` and `DIN` (`bdos.s`), which are `POUT` and `PIN` without a segment; with one,
  `DSEG` (`bdosmem.s`) works out segment × 16 + the DMA address, checks the record's 128 bytes (`SEGOK`: the end
  past `$FFFFF`, each block from the first to the last against OODOS's own, `BLKOK`: 49 can set a multi-sector
  count of 255, a span of three blocks), and has the BIOS's MOVE copy them
  with XMOVE's physical bank. `MRANGE` checks the whole multi-sector run the same way first. 17 and 18's record
  (`DIRREC`) goes by `DOUT`.
- **The map** is `AMAP`, 20 bytes of `SYSBSS`; `MBLK` holds the program's MCB (or 52's and 73's 4 bytes), read
  whole by `PGET` before anything is written, so one in the I/O hole is refused with nothing changed.
- **74** reads the drive, the flags and the path by `PGET` straight into the system block's sector buffer
  (`SECBUF`, MOUNT's sector: the unit, the flags, the path, zeros after), checking the path as it goes; then
  `FLUSHD` for the drive (its buffers never cross a window, so the write-back doesn't use `SECBUF`), `PURGED`, the
  drive's bits out of the login and read-only vectors (and a search on it ended), `SDMNT`, `RESCAN`, and the new
  unit's size and name from the BIOS's copy of the units table (`UNITSAV`). The card's ERROR says not found (8)
  and nothing to unmount (6).
- **Where it lives:** block 60 11,588 bytes of 16,384 (10,227 at 3b's merge, 11,558 before the review's fixes;
  `BDOS2` still unused); the zero page 210 of 240 (201: a physical address and its end, 74's pointer); `SYSBSS`
  737 of 1,280 (701: the segment, the map, the MCB, 74's and the memory functions' state); the BIOS 3,276 bytes
  (3,240: `XBANK`, MOVE's A16–A19 and its carry, `SDMNT`, `SDIO`'s table); common memory 1,123 of 2,048 (1,107:
  WARM's 16 bytes); the CCP's code as it was (1,323).
- **Measured** (`test_oodos_sys`, BDOSENT to its return, on M:): a buffered record by 33, 7,938 cycles to the
  CPU's DMA address and 8,176 through a segment (7,890 and 8,196 before the review's fixes, which put a read to
  `$1FF7F` before it): the segment costs about 250–300 cycles a record (its address, the
  range check, and XMOVE's physical bank).
- **Found while testing:** 74 tested `PGET`'s flags for the path's NUL, and they are X's (its `PLX`): MEMTEST's
  paths all came back not found. It compares A now.
- **The review's fixes (2026-10-30):** the BIOS's MOVE says by its carry whether it moved (`PCOPY` took a
  segment's record ending at `$FFFF`, physical `$1FF7F` on, for a refusal and ended the function); 53 and 54
  allocate their run; 57 frees an allocation's tail; `SEGOK` checks every block of the span; 74 refuses a mount
  whose rescan failed (`RESCAN` gives a carry: SELDSK's 0; that is also A: with no image, so a card with
  nothing on A: refuses every 74, which can't happen while OODOS runs from A:: accepted at round 2); the warm boot's segment reset is tested from a
  segment MEMTEST D leaves set. Each fix's test fails without it, but `SEGOK`'s: OODOS's own blocks come in runs
  of 4 or 8 (24–31, 48–55, 60–63), so no span of at most three blocks has an allowed first and last block around
  a refused one, and the first-and-last check refused the same spans. Common memory in an allocated block is
  deferred (phase 7, [Open Questions](#open-questions)).
- OODOS.SYS stayed 0.2 here; 0.3 (2026-11-05) is phase 5's end.

### The archive bit and the update stamp (item 10) ###

**Decided (2026-11-04): as CP/M 3 has them**, found by G1 where the code differed from the Guide.

- **The archive attribute (t3')** (the Guide, 2.3.4: the file system resets it in a directory FCB that has been
  issued a write): a write, 21, 34 or 40 (each record of a multi-sector call), clears t3' in the directory entry of
  the extent it writes, at that write, and in the FCB. The FCB's t3', which Open and each move to another extent
  copy from the entry (`OPENEXT`), says whether the entry has it, so the first write of an extent whose entry is
  archived finds the entry, clears it and writes it at once (`WROTE`), and the writes after cost a `BIT`. The entry
  written so has only its t3' changed, so it points at no data not yet on the disk. Only that extent's entry: an
  archive program reads every entry of a file (17 with `?` in EX), as the Guide says, and a file is archived only
  if all of them are; PIP's `[A]` does so ([PIP](#pip-phase-4b2)). A read, a close or a move to another extent
  leaves it. **Truncate (99)** clears it in the entry that ends at R (ours: the Guide names writes; a truncated file
  has changed, and a backup must take it again). Make (22) clears every attribute already; 30 sets it.
- **The update stamp** (the Guide, 21 and 34: a write stamps it if the label asks for update stamps and the file
  "has not already been stamped for update by a previous Make or Write"): at Make of extent 0, as before, and at
  the first write (21, 34, 40) since the FCB was opened (15), once; no later write of that open stamps it, whatever
  its extent, and neither does a close or a move to another extent (before item 10 the close and each move of a
  written FCB did, so a file written and never closed had no new update stamp). A Make of a later extent (EX not 0)
  makes no stamp, so its first write does. The FCB's S1, which the Guide reserves for the system, holds it: bit 7
  (`S1_STAMPED`) set once the stamp is made; Open clears S1, Make of extent 0 sets it. A close keeps it (ours: the
  FCB stays open after a close, so writing it again without an Open isn't a new open). Truncate stamps the update
  as before. The time is TIME's at the write. An FCB a program puts together itself from a directory entry,
  without Open, has whatever S1 it was given (an entry's byte count of 128, `$80`, reads as stamped): the books
  say to open or make a file before writing it (2026-11-04, from the review). The model writes S1 as the BDOS does.
- **A write can fail on its directory write** (2026-11-04, from the review, accepted): clearing an entry's t3' or
  the stamp's SFCB is a directory write, at the write, after the record is in its buffer, so 21, 34 and 40 can now
  say Disk I/O (or Read Only, for a unit found read-only) that before only a later close could; in return mode the
  FCB's CR hasn't moved on. The cost, a directory sector written at a file's first write after each open and at
  the first write of each archived extent, is accepted too.
- **The model** (`cpm3model.py`) does both; its cases in `test_model.py` were the close's moment and are the first
  write's now, with the archive's added (two extents, a read, writes to each, a truncate). The random comparison
  covers both (30 sets t3' in about a third of its calls, and the clock moves between operations).
- **Tested:** DIRTEST's `AR` lines (archived by 30, kept by a read, cleared in the entry and the FCB by a write),
  STAMPTEST (make's update stands through a write and the close; the first write after an open stamps, a later
  write and the close don't), WRITEOO1 (the clock set later before its second write and close: `test_oodos_card`
  and `interop.sh --written` find the first write's time) and PIP's `[A]` of a file whose second entry isn't
  archived. FSTEST's check that a close's entry follows its data ends at `CLOSEX`'s return: the stamp's SFCB and a
  cleared t3' come after it in the same call and point at nothing new, which DIRTEST's run checks: every byte of
  M:'s directory that `WROTE` changes is an entry's t3' cleared or an SFCB slot's update stamp (DIRTEST sets the
  clock to 2027 before AR.TXT's write, so its stamp changes bytes).
- **Measured again** (2026-11-04, `test_oodos_sys`, BDOSENT to its return, as 3a and 3b1 measured them: FSTEST's
  first run on `oodos-8m`, the least of each kind; the multi-sector test on M:). 3b1's figures (above) predate 3c's
  dispatch and the phases since; on main before item 10 (08efa8a), then with it: Open 6,759, 6,694; a sequential
  read of a record in a buffered sector 4,915, 4,914, of one whose sector it reads 13,071, 13,069; a write to a
  buffered sector 7,794, 7,818 (`WROTE`'s two `BIT`s and its call, 24), one that takes a new block 78,427, 78,451,
  the cheapest that writes buffers back first 167,132, 161,986 (a move to the next extent no longer stamps). With a
  count of 128 on M:, a read 563,099, 562,907 (4,397 a record), a write 789,453, 786,294 (6,142 a record). The stub
  is 220 (130 in, 90 out: 5a's door for 59 is 4 of them).

## The CCP, phase 4 ##

**Decided (2026-10-30)**

1. **Phase 4 is two parts:** 4a the real CCP and its built-in commands; 4b the transient utilities (DIR's full
   form, TYPE, ERASE, RENAME, PIP, DATE, INITDIR, SET, SHOW, DEVICE). **SUBMIT moves to phase 5,** with GET and
   PUT: in CP/M 3 it is an RSX ([Phases](#phases)).
2. **ca65,** as the rest of OODOS. What the CCP and 4b's utilities both need (decimal input and output, a file's
   name shown, a Y/N question, paged output with ^C) is one include library, not two copies.
3. **The User's Guide's forms, in our own words:** every built-in's forms and options as the CP/M 3 User's Guide
   gives them, its output's layout followed. What can't apply to OODOS is refused, and said here as ours.
4. **Testing:** directed emulator tests for each command and option, the output compared line for line; what ERA
   and REN leave on a card checked by cpmtools (`cpmls`, `fsck.cpm`, `cpmcp`), as phase 3 did. No model of the
   CCP.

**Decided (2026-10-31)**

1. **4b is two branches:** **4b1** DIR's full form, TYPE, ERASE, RENAME, SET, SHOW, INITDIR, DATE, DEVICE and
   SETDEF; **4b2** PIP alone, every form and option of the User's Guide, its devices mapped to OODOS's CRT, TTY and
   LPT, what can't apply refused and said here.
2. **Phase 4's decisions hold for both:** ca65; shared code in `cmd.inc` (or include files beside it), not copied
   per utility; the User's Guide's forms and options in full, in our own words, its output's layout followed; what
   can't apply to OODOS refused and said here as ours; directed tests line for line, and cpmtools for what lands
   on a card.

**Proposed (4a)** (the User's Guide, sections 1.2, 2.3–2.6, 4 and 5's DIR, ERASE, RENAME, SETDEF, TYPE and USER;
where it is silent the choice is ours, and says so):

- **Where it lives:** block 62's code window, resident, in place of the stand-in (`oodos/sys/ccp.s`), running as a
  program does (the program's windows, stack and BDOS entry). It uses none of the program's zero page (ours: its
  pointers are in its code, as the stand-in's were), so a program's zero page is the program's alone. The shared
  code is `oodos/include/cmd.inc`, assembled into the CCP and, in 4b, into each utility.
- **The prompt** is CP/M 3's: the CCP's user number when it isn't 0, its drive, `>`: `A>`, `5A>`, `12M>`. The CCP
  keeps its own drive and user (`CCPDRV`, `CCPUSR`, 3b2) and makes them current at each warm boot.
- **The line** is function 10's, up to 255 characters (ours: the most 10 takes), every letter taken as a capital
  (the Guide, 1.2): the command and its tail, so a program's tail is in capitals. Blanks may come before the
  command; blanks and tabs after it are dropped, so they aren't the tail's (ours). A 0 (^@) in the line ends it
  (ours: so the tail at `$0280` holds no 0 inside its length). An empty line, or blanks, is the prompt again. A ^C at the prompt with the line empty resets the disk
  system (function 13), as since 3a.
- **The layout** is the Guide's: a command that writes nothing has the next prompt on the next line (`A>B:`, then
  `B>`), with no blank line between. Function 10 echoes only the CR that ends a line, and the CCP owes the LF: it
  is written before the first thing the command writes (a program's output, a built-in's, a message), and is the
  prompt's own CR LF when there is nothing (ours: `cmd.inc`'s `CM_LFDUE`). A program's output that ends with CR LF
  is followed by the prompt's CR LF, so a blank line, as in the Guide's examples.
- **Several commands, `!`:** a line `A!B!C` runs A, then B, then C (the Guide, 4.5). Each one after the first is
  shown after a new prompt, as it was typed (its case kept, its leading blanks and tabs not shown), then run. A
  `!` with nothing after it but blanks, tabs and more `!`s leaves nothing to run, and an empty command between two
  `!`s (`A!!B`, `A! !B`) is skipped (ours: no empty prompt). The rest of the line is kept across a
  program's warm boot. A program ended by ^C (the return code `$FFFE`) drops the rest, and so does a built-in
  stopped by ^C (ours). Function 47's command takes the place of what is left of a line (ours: the Guide doesn't
  say), and may hold `!` itself.
- **Conditional commands, `:`** (the Guide's SUBMIT, and the Programmer's Guide's 108): a command whose first
  character is `:` runs only if the return code says the previous command succeeded: anything but `$FF00–$FFFF`
  (ours: `$FFFF`, which 108 can't set, counts as a failure too). It applies to any command of a line, the first
  or one after `!`. A skipped command leaves the code as it was, so `:A!:B` skips both after a failure.
- **The return code** (108, the SCB's `$10`): 0 as a program not chained to starts (3b2); `$FFFD` and `$FFFE` from
  the BDOS for an error's end and a ^C. **Ours:** a built-in sets it too, 0 when it did what was asked and `$FF00`
  when it said an error (`No File`, a BDOS error, a command refused with `?`); so does the CCP for `NAME?`, `READ
  ERROR` and a program it won't run. A built-in stopped by ^C sets `$FFFE`. Empty lines, comments and skipped
  commands leave it.
- **Comments:** a command whose first character is `;` does nothing (ours: as in a SUBMIT file).
- **The keyword** runs from the first character to a blank, a tab or the command's end. In order:
  - `GO`: the stand-in's (phase 2), kept for the tests: the image at `$0300` whose header is at `$0280`.
    **Ours,** a seventh built-in that CP/M 3 doesn't have: it shadows a `GO.COM`, which runs only with a drive
    typed (`A:GO`), and it runs whatever image is at `$0300` once the header passes the loader's checks.
  - a built-in's name, with no drive or type: `DIR`, `DIRSYS` or `DIRS`, `ERASE` or `ERA`, `RENAME` or `REN`,
    `TYPE` or `TYP`, `USER` or `USE` (the Guide's Table 4-1 and its short forms), and ours, `CLEAR` and `CLS`
    (2026-11-03, "Clearing the screen" below);
  - `d:`, or a user number and a drive, `5B:`: the drive change (below);
  - else a program: `{d:}NAME{.COM}{;password}`. A type but `COM` is refused: `.SUB` with
    `NAME.SUB: no SUBMIT yet` until phase 5 (ours), any other type, `.PRL` among them, as `NAME?` (the CCP runs
    `.COM`s only, as CP/M 3's; a `.PRL` is phase 5's loader's). A keyword 152 refuses, or with `?` or `*`, is
    `NAME?`.
  - **Ours:** a built-in's name with a drive (`A:DIR`) runs the program (`DIR.COM` from A:), so a utility of a
    built-in's name can always be reached; a user number goes only with a drive change (`5B:HELLO` is
    `5B:HELLO?`).
- **The tail:** what follows the keyword, its leading blank too, at `$0280` (the length, the characters, a 0
  after them). At most 126 characters, the tail's room; a longer one is `Command tail too long` (ours), nothing
  run. **The default FCBs** are parsed from it by 152, as CP/M 3's CCP does: the first file specification's
  drive, name and type at `$025C`, the second's at `$026C`, bytes 12–15 of each 0, and `$027C–$027F` 0 (ours). A
  delimiter after the first (`=`, `,` and the rest of 152's) is passed over before the second, so `NEW=OLD` and
  `B:=A:X` fill both. An option list in brackets, `[...]`, is passed over and isn't a specification (ours). One
  that 152 refuses leaves its FCB blank and stops the parse (ours: the CCP doesn't check a program's tail, the
  Guide, 1.2). **Passwords:** `$0251–$0252` the address of the first specification's password in the tail
  (`$0280`'s copy), `$0253` its length, `$0254–$0256` the second's; 0 and 0 for none, as CP/M 3's base page has
  them. **`$0250`** is the drive the program was loaded from, 1–16 (CP/M 3's `0050H`).
- **Running a program:** without a drive, the SCB's drive search chain (`$4C–$4F`: 0 the default drive, 1–16
  A–P, `$FF` the end; cold boot's is the default drive alone, 3b2), each drive by one Open, which tries the
  current user and then user 0's SYS file (3a), so the Guide's four places for a chain of two (4.4.2) are two
  Opens. With a drive, that drive only. A chain drive the BDOS can't select (Invalid Drive) is passed over; the
  drive given, or the CCP's own, is `READ ERROR` (ours: Invalid Drive is also a card that failed a login, as the
  stand-in said). The password, if given, is in the DMA buffer at the Open (3b1). Not found: the keyword and `?`
  (`NAME?`; ours, from the Guide's examples in 1.2, `DJR?` and `DIRCOPYSYS.COM?`, though its text says the
  command line is echoed with the `?`, in 1.2 and again in 4.4); a Password Error: `Password Error`; any other error, or a file that ends inside its
  image: `READ ERROR`, as the stand-in. Then the loader's checks of the header (magic, flags, page, size: `NO
  PROGRAM`, `BAD PROGRAM HEADER`, as the stand-in), the image, the tail, the return code, the error mode, the
  DMA address `$0280`, and `$0300`. **The display mode** (SETDEF's `[DISPLAY]`, the Guide): when the SCB's
  `$18` bit 7 is set (ours, in a reserved byte; SETDEF, 4b or later, sets it), the CCP says where the program came
  from before it runs: `A:HELLO    COM`, and ` (User 0)` when it was user 0's SYS file opened from another user.
  The filetype search order (SETDEF's `[ORDER]`) is `.COM` alone until SUBMIT, phase 5, and so is
  `PROFILE.SUB` at cold boot (the Guide, 4.5): the CCP doesn't look for it yet.
- **The drive change** (`B:`, `5B:`; the Guide, 2.4–2.5): the drive selected by 14 in error mode `$FE`, so one
  that isn't a drive is said as the BDOS says it (`OODOS Error On H: Invalid Drive`, a drive with no image) and the
  CCP's drive stays; a letter past P (`Q:`) is no drive change but a command, `Q:?`.
  Then it is the CCP's drive, and the user number, if given (0–15), its user.
- **USER** `{n}` (the Guide's USER): 0–15, the CCP's user from then on. Without a number it asks
  `Enter User #: ` and reads a line (the Guide). Anything but a number 0–15 is the command and `?` (ours).
- **DIR** `{d:}` or `{filespec}` and **DIRSYS** (`DIRS`): the names of the current user's files on the drive,
  DIR those without the SYS attribute and DIRSYS those with it, in the directory's order (the built-in doesn't
  sort; DIR.COM's options do), each file once (its extent-zero entry). A line starts with the drive, `A: `, and
  the names follow as `NAME     TYP`, 8 and 3 characters, `" : "` between, as the Guide's examples show; as many
  as fit in the console width (the SCB's `$1A`) without its last column, `$1A` / 15 of them, 5 at 80 columns
  (ours). No drive and no name is every file; a name with no type matches a blank type (ours: the FCB as 152 makes
  it). Nothing matched: `No File`. Files of the other kind matched too: after the list, on its own line,
  `SYSTEM FILE(S) EXIST` (DIR) or `NON-SYSTEM FILE(S) EXIST` (DIRSYS), as the Guide has them. An error (a drive
  that isn't) is the BDOS's, in error mode `$FE`.
- **Paging** (the Guide: DIR and TYPE stop when the screen is full, SETDEF's `[PAGE]`): when the SCB's page mode
  (`$2C`) is 0, the default, the built-in stops after the page length (`$1C`, 24) less one lines, shows
  `Press a key for more` (ours: the Guide says only that it waits), waits for a key, rubs the message out and
  goes on; a ^C there stops it. The message and its rubout are written in console mode bit 2 (raw), so with
  ^P on they stay off the printout (ours, `cmd.inc`'s, every program's that pages with it). Page lengths under 2
  don't stop (ours). A ^C typed while DIR or TYPE writes stops
  them too: they run in console mode bit 0, where 11 reports only ^C (3b2), and look before each line (DIR) and
  each character (TYPE). ^S and ^Q are the BDOS's, as for any output.
- **ERA** `filespec` (`ERASE`): the current user's matching files deleted (19), its password, if given, in the
  DMA buffer. A name with `?` or `*` asks first, as the Guide does: `ERASE B:*.TXT (Y/N)?`, the specification as
  typed (its password not shown, ours), and a line is read: `Y` or `y` first deletes, anything else doesn't (the
  return code 0: done as asked, ours). A single name isn't asked about (the Guide asks for wildcards only). Nothing matched: `No File`. A file or drive
  that is read-only, or a password that doesn't match, is the BDOS's error, said in mode `$FE`.
- **REN** `new=old` (`RENAME`): `old`'s drive, if given, or `new`'s; given both, they must be the same, else the
  command and `?`. `old` not there: `No File`. `new` there already: `Not renamed: NEW.TYP file already exists,
  delete (Y/N)?` (the Guide's RENAME, in our words), and a `Y` deletes it (with `new`'s password, if given) and
  renames; anything else renames nothing (the return code `$FF00`: not renamed, ours). Then 23, `old`'s password in the DMA buffer; its errors are the BDOS's.
  `new` and `old` the same file once the drive is decided (`REN X.TXT=X.TXT`, or `REN B:X.TXT=X.TXT` from any drive, since `old` is on `new`'s drive) is
  the command and `?` (ours), before anything is looked for: else `new` would be found there already, and a `Y`
  would delete the only copy.
- **TYPE** `filespec` (`TYP`): the file's bytes to the console (2, so tabs are expanded) to the first ^Z or its
  end, paged as above, counting lines by LF; its password, if given, in the DMA buffer at the Open. Not there:
  `No File`. Each byte goes out as it is (ours: a binary file shows what it shows; a `.COM` shows `OOD`, its
  magic number, and stops at the ^Z after it).
- **Handed to a utility** (the Guide, 4.2: options a built-in can't do need `DIR.COM`, `ERASE.COM`,
  `RENAME.COM`, `TYPE.COM`): `DIR` or `DIRS` with an option list (`[FULL]`) or more than one specification;
  `ERA` with an option list (`[CONFIRM]`) or none; `REN` with no tail or a wildcard; `TYPE` with an option list
  (`[NO PAGE]`), a wildcard, or none. The utility is found and run as any program, with the tail as typed; `DIRSYS`'s gets
  `SYS` added to its options (`[SYS,FULL]`, or ` [SYS]` after the tail with none; ours, so DIR.COM lists what
  DIRSYS would). Not found: `DIR.COM required` (the Guide's message names no file; ours names it). 4b writes them.
- **Errors in a built-in's form** (more specifications than it takes, a specification 152 refuses, `REN` whose
  new and old are the same file, a tail after `d:`): the command as it was given, in capitals, and `?` (ours, as
  `NAME?`).
- **Error modes:** the CCP's own calls return their errors (`$FF`), as the stand-in's; the built-ins' file calls
  say them (`$FE`), so `OODOS Error On A: File R/O` and its function and file are the message.

**Proposed (4a, as built, 2026-10-30)** ([`oodos/sys/ccp.s`](../oodos/sys/ccp.s),
[`oodos/include/cmd.inc`](../oodos/include/cmd.inc); the list above is what was built, and these are the details):

- **No BDOS or BIOS change.** The CCP uses 152 for the keyword and the tail's specifications, 17 and 18 for DIR,
  15 and 20 for TYPE and loading, 19 and 23 for ERA and REN, 49 for the SCB's width, page mode and length, search
  chain and display flag (`SCB_CCPDISP`, `$18`, in `sys.inc`), 108 for the return code. The stand-in's warm boot
  work (the rescan, 48, 98; 13 for a ^C at the prompt; 47's request) is kept as it was.
- **Where it lives:** block 62's code window, resident: cold boot and the CCP 4,773 bytes, `$C800–$DAA4` (the CCP 4,485: 38 more since 4b1's `CM_EOL`, 26 for its stops kept off the printer echo; its
  shared `cmd.inc` among them; the stand-in's was 1,323), then its BSS, 984 bytes (the line of 255, the command,
  the tail, three FCBs, a record), 5,757 of the window's 12,288 (`BDOS2` still unused). No zero page (the BDOS's
  and BIOS's 210 of 240 as they were), `SYSBSS` 737 of 1,280, common memory 1,123 of 2,048 and block 60 11,588 as
  they were. Its state across a program (the rest of a `!` line, `ATPROMPT`, `PENDING`, `CCPDRV`, `CCPUSR`) is in
  block 62, which is W3 while a program runs: only the TPA rule (a program keeps to `$0300–$BFFC`) keeps it
  safe, as CP/M programs are trusted not to overwrite the BDOS.
- **`cmd.inc`** is included into the CCP's `RES` segment with its data there too (`CMD_DATA`, `CMD_BSS`, and
  `CMD_BREAK`, where a ^C during paged output goes, are the includer's); a utility includes it into `CODE` with its
  data in `RODATA` and `BSS`, as `tools/rules.py` wants. Its pointers are in its own instructions, so it uses no
  zero page and keeps the 65C816 rules. `CM_LFDUE` is the LF owed after a line read (above). `CM_PAGE` reads the
  console mode (109), keeps it and sets bit 0 in it; `CM_UNPAGE` puts the kept mode back, so a utility leaves a
  mode its caller set as it was. A flag, `CM_PAGED`, in the library's constant data so that it starts 0 with the
  image (a program's BSS isn't cleared), makes a `CM_UNPAGE` without a `CM_PAGE` do nothing and a second `CM_PAGE`
  keep the first's mode; the CCP clears it whenever it sets mode 0. A kept mode that ORs with 1 (`CM_PAGE`) or 5 (a
  stop) to `$FFFF` would be a get to 109, so it isn't set: it needs bits CP/M 3 leaves undefined (accepted). `CM_BRK` takes a waiting key only in console mode bit 0, where 11 reports only
  ^C; in another mode it leaves the key alone, so it is safe without `CM_PAGE`. The CCP sets mode 0 after each
  built-in in any case. `CMDTEST` (`oodos/sys/test/cmdtest.s`) tests both in `test_oodos_ccp`.
- **The test card:** `0:HELLO.COM` is a SYS file now (`cpmchattr`, `make_card.sh`), so the CCP finds it from any
  user, as CP/M 3 keeps its programs.
- **The User's Guide** (`cpm3-usr.txt`, OCR'd) decided the forms, the messages it gives (`No File`, `SYSTEM FILE(S)
  EXIST`, `NON-SYSTEM FILE(S) EXIST`, `ERASE spec (Y/N)?`, `Enter User #:`, `NAME?`), the prompt, the layout of
  DIR's lines and of `!`'s commands, the search order and the display mode's line; the rest, marked ours above,
  is OODOS's.

### The utilities, phase 4b ###

**Proposed (4b1)** (the User's Guide: 2.7, 2.10, 3.5, 4.2–4.4, section 5's DATE, DEVICE, DIR, ERASE, INITDIR,
RENAME, SET, SETDEF, SHOW and TYPE, and Appendix A's messages; pages cited as the Guide numbers them, 5-19; where
it is silent the choice is ours, and says so):

**For all of them**

- **Where they live:** each a `.COM`, `oodos/utils/NAME.s`, built by `oodos/Makefile` (`make utils`: through the
  65C816 rules gate and with no ld65 warning, as every program) and put on the card's A: by `make_card.sh`, in
  user 0 with the SYS attribute (ours, as `HELLO.COM`: the CCP finds them from any user, and DIR doesn't list
  them). The CCP's hand-offs (`DIR [FULL]`, `ERA` with none, `REN *.X=*.Y`, `TYPE *.TXT`) find them by the search
  chain, as any program.
- **Shared code:** `cmd.inc`, as the CCP has it, and `oodos/include/util.inc` beside it for what only the
  utilities need (the CCP doesn't include it): the option list's parser, numbers of 24 bits with commas and in a
  field, dates and times, the directory read by 17 with `?`, the label, a drive's DPB, a file specification by
  152. A routine two utilities need is there, not in either.
- **The layout:** a utility's lines are separated, not ended, by CR LF (ours: `cmd.inc`'s `CM_EOL`, a line owed
  and written before the next output, counted for paging there), so its last line is ended by the prompt's CR
  LF, with no blank line, as after a built-in: `DIR.COM`'s short form and the built-in's are the same bytes. TYPE
  writes the file's bytes as they are, as the built-in.
- **Options** (the Guide, 5.2 and DIR's 5-21): in brackets, anywhere in the tail, separated by commas or blanks;
  `name`, `name=value` or `name=(v,v,...)`; any unambiguous prefix of a name (an exact name wins: DIR's `DIR`
  isn't ambiguous with `DISK`); `NO` then a blank before a name is one word (`[NO PAGE]` is `[NOPAGE]`, ours). One
  option list a command; a second, a `[` not closed, an unknown name or an ambiguous prefix is the utility's error
  (below). The tail is in capitals already (the CCP's).
- **The return code** (108, ours, as the built-ins): 0 done as asked, `$FF00` an error said, `$FFFE` stopped by
  ^C (at a page's stop, or while writing). **Errors of the BDOS** (a drive that isn't, a read-only drive or file,
  a password that doesn't match) are said by the BDOS in error mode `$FE`, as the built-ins' are, except where
  the Guide gives the utility a message of its own for them (named below).
- **Paging** as the built-ins: the SCB's page mode (`$2C`, 0 on) and length (`$1C`); `Press a key for more` and a
  key, ^C there or while writing stops it (`cmd.inc`). A utility's `[NOPAGE]`, where it has one, turns it off; its
  `[PAGE]` turns it on whatever the SCB says (ours).
- **Drives:** a drive is given in the FCB's byte where a call takes one (TYPE, ERASE, RENAME); a utility that needs
  a drive current for its calls (DIR, SET, INITDIR, SHOW) selects it (14); and every utility, as it ends, selects the
  drive current at its start again (14), so the current drive is as it was (ours; the CCP's warm boot puts its own
  back in any case).
- **Dates** are `MM/DD/YY`, the year's last two digits (the Guide), from CP/M 3's days (1 January 1978 day 1, a
  Sunday); times `HH:MM` (DIR, SHOW) or `HH:MM:SS` (DATE). A stamp of 0 days is no stamp: blanks.

**DIR** (5-19 to 5-28; `DIR.COM`): `DIR {d:} [options]`, `DIR {filespec} {filespec} ... [options]`.

- **Without an option list** (two specifications or more, as the CCP hands them, or `A:DIR` typed): the
  built-in's listing and messages, each drive given in turn, the names sorted (ours: DIR.COM sorts unless
  `NOSORT`; the built-in doesn't).
- **With one, the format is FULL** unless SIZE or DATE is given (the Guide: FULL is the default with options; FULL
  and SIZE both, or DATE with either, `Illegal Format Value.`, ours where the Guide gives the message no case). The
  options (Table 5-5): `ATT`, `DATE`, `DIR`, `DRIVE=ALL|d|(d,...)` (`DISK=` too), `EXCLUDE`, `FF`, `FULL`,
  `LENGTH=n`, `MESSAGE`, `NOPAGE`, `NOSORT`, `RO`, `RW`, `SIZE`, `SYS`, `USER=ALL|n|(n,...)`.
- **What is listed:** for each drive (the specifications' own, in the order first given; or `DRIVE=`'s; or the
  current), each user (`USER=`'s, in order 0–15; or the current), the files matching any specification (none
  given: all), or with `EXCLUDE` matching none. `DIR` and `SYS` keep only files of that kind, `RO` and `RW` only
  those; both of a pair, or neither, is both (ours). A file is its extent-zero entry, as the built-in's: its
  records and kilobytes are counted over all its entries. `DRIVE=ALL` is every logged-in drive (function 24; the
  Guide's table says "all accessed drives", which settles its example's looser "drives in the drive search
  chain", ours), A to P. A drive in a specification and `DRIVE=` together: `Illegal Global/Local Drive Spec
  Mixing.` A drive that isn't: the BDOS's error (mode `$FE`), and the next drive.
- **Sorted** by the name and type, the attribute bits dropped (ours: by byte), unless `NOSORT`: the directory's
  order. The files of a drive are read into memory first (17 with `?`, every entry): a drive with more than fit
  is `Not Enough Memory for Sort.` (the Guide), nothing listed for it.
- **Each drive and user with files found** has its heading, `Directory For Drive A:  User  0` and a blank line
  (the Guide's examples, as `cpmls -F` writes it); with `MESSAGE`, every one searched has it, and `No File` after
  it when nothing is there (the Guide: MESSAGE shows where it is searching and the file not found message).
- **FULL** (the Guide's sample, its columns as `cpmls -F` has them):
  ```
      Name     Bytes   Recs   Attributes   Prot      Update          Create
  ------------ ------ ------ ------------ ------ --------------  --------------

  README   TXT     4k      1 Dir RW       None   10/26/26 12:00  10/26/26 12:00
  ```
  The name and type (12), kilobytes allocated (6, `k`), records (6: the last entry's extent × 128 + its RC),
  attributes (12: `Dir` or `Sys`, `RW` or `RO`, `A` when archived; with `ATT` the user attributes, F1–F4 as `1`–`4`
  in their places: `Sys RO A1 3 `), the password mode (6: `Read`, `Write`, `Delete` or `None`, from the file's
  XFCB), the update stamp and, under `Create` or `Access` as the label's mode says (`Create` when it asks for
  neither, ours), the other (14 each, blank without a stamp); Prot and both stamps blank when the drive has no
  label (the Guide). The heading again every `LENGTH=` lines (5–65535, ours: 16 bits) and, when paged, at each page's top
  (the Guide: one screen), so a page or sheet starts with it.
- **SIZE:** the drive, then `NAME     TYP` and the kilobytes (6, `k`), `" : "` between, as many as fit in the
  console width without its last column (the built-in's rule), each line starting `A: `.
- **DATE** (ours, the Guide shows none): the name and type, the update stamp and the other, under the FULL
  layout's last two headings; `Date and Time Stamping Inactive` (the Guide) on a drive without SFCBs.
- **Totals,** after each drive and user's FULL, SIZE or DATE list (the Guide: for that drive and user area):
  ```
  Total Bytes     =      8k  Total Records =       2  Files Found =    2
  Total 1k Blocks =      2   Used/Max Dir Entries For Drive A:   12/ 768
  ```
  `Total Bytes` the kilobytes allocated, `Total 1k Blocks` the 1 KB blocks the records need (each file's records
  / 8, rounded up: the Guide), `Used/Max` the drive's entries that aren't free or SFCBs, of all but the SFCBs
  (ours: a drive with date stamps has three quarters of its entries for files).
- **FF:** a form feed to LST: (function 5) first when the printer echo is on (the SCB's `$38`), and with
  `LENGTH=` every `LENGTH=` lines (the Guide), each sheet starting with the heading; without `LENGTH=` FF turns paging off, with it paging stays
  on (the Guide), its stops on the screen only (as every paged program's: `cmd.inc`, above).
  **NOPAGE:** no stops.
- **Errors** (Appendix A): `Illegal Option or Modifier.` (an unknown or ambiguous option, a bad value),
  `Options not grouped together.` (a second list), `Illegal command tail.` (a specification 152 refuses, a
  delimiter with nothing after it but blanks and the option list, as `DIR M:, [FULL]`, a `[` not closed), `No
  File`; the return code `$FF00` after any, and after `No File`.

**TYPE** (5-114; `TYPE.COM`): `TYPE {filespec {[PAGE]|[NO PAGE]}}`.

- **One file:** the built-in's: its bytes to the first ^Z or its end, by function 2, paged by LF, its password
  given after the name (`;pw`), `No File`. `[PAGE]` stops at each page whatever the SCB's page mode, `[NOPAGE]`
  never (ours); neither: the SCB's, as the built-in.
- **A wildcard** (ours, the Guide is silent): each matching file of the current user in turn, in the directory's
  order, its name on a line of its own first (`M:ONE.TXT`), then its bytes.
- **No file specification:** `Enter filename: ` and a line read (the Guide), then as above; an empty line ends it.
- **Errors:** `Try 'PAGE' or 'NO PAGE'` for any other option (the Guide), `Invalid drive.` for a drive other
  than A to P (Appendix A, which gives TYPE this one), `Invalid file specification.` for any other one 152 refuses
  (Appendix A), else the BDOS's (`Password Error` among them).

**ERASE** (5-38; `ERASE.COM`): `ERASE {filespec} {[CONFIRM]}`, `CONFIRM` or any prefix (`C`).

- **Without CONFIRM** the built-in's: a single name deleted (19), a wildcard asked about first
  (`ERASE M:*.TXT (Y/N)?`), `No File`, the BDOS's errors.
- **CONFIRM:** each matching file asked about in turn, `ERASE M:ONE.TXT (Y/N)? ` (ours: the Guide says each is
  shown with a question mark), `Y` deletes it (19, with the password given), anything else keeps it; the names are
  read first (17, 18), then asked about. A single name with CONFIRM is asked about too.
- **No file specification:** `Enter filename: ` (ours, as TYPE), an empty line ends it.
- **Errors:** `Missing delimiter or Unrecognized Option` (Appendix A) for any other option or a second name,
  `Invalid file specification.` for one 152 refuses, `Drive Read Only` (Appendix A) for a read-only drive (29,
  before each 19), else the BDOS's.

**RENAME** (5-81 to 5-83; `RENAME.COM`): `RENAME {new-filespec=old-filespec}`.

- **Names without wildcards:** the built-in's: the drive rule, `No File`, `Not renamed: NEW.TYP file already
  exists, delete (Y/N)?`, the same file refused; for a read-only drive `Drive Read Only` and for an old file
  that is read-only `Not renamed, filespec read only.` (Appendix A gives RENAME both; the built-in leaves them
  to the BDOS), else the BDOS's errors.
- **Wildcards** (the Guide): the new name's `?`s (after `*` is filled out) must be where the old name's are, else
  `Invalid wildcard.` (Appendix A); each matching file of the current user becomes the new name with the old
  one's characters in the `?` places. The names are read first, then renamed one by one, each asked about if the
  new name is there already, as above. Each rename done is shown on a line, `NEW.TYP=OLD.TYP`, as the command
  is typed (ours: the Guide's example after the prompts shows the old name first, `Y       PRN=X               PRN`,
  in padded name and type fields; we kept the typed order and the built-in's `NAME.TYP`).
- **No tail:** `Enter New Name: ` and `Enter Old Name: ` (the Guide), each a line read, then as above, its line
  shown; an empty line ends it.
- **Errors:** `Incorrect file specification.` (Appendix A) for a form that isn't `new=old` or a specification
  152 refuses, `No File`, and the two read-only messages above.

**SET** (5-86 to 5-94; `SET.COM`): `SET [options]`, `SET d: [options]`, `SET filespec [options]`.

- **File options** (with a file specification; Table 5-16): `DIR`, `SYS`, `RO`, `RW`, `ARCHIVE=ON|OFF`,
  `F1=ON|OFF` to `F4=ON|OFF` (30, every other attribute as the entry has it); `PASSWORD=pw` and
  `PROTECT=READ|WRITE|DELETE|NONE` (103: a new password, or the mode; a file given a password without a mode is
  `READ` if it had no XFCB, keeps its mode if it had: ours; `NONE` makes the mode 0, so the file is unprotected,
  and deletes its password (Table 5-17) unless `PASSWORD=` gives a new one;
  `READ`, `WRITE` or `DELETE` on a file with no password and none given: `Assign a Password to this file.`). A
  wildcard is each matching file of the current user, read first; the file's password, if it has one, after the
  name (`SET X.TXT;OLD [PASSWORD=NEW]`). Each file done: `M:ONE.TXT set to System (SYS), Read Only (RO)`, the
  attributes asked for in the table's order (`Directory (DIR)`, `System (SYS)`, `Read Write (RW)`, `Read Only
  (RO)`, `Archive=ON` or `OFF`, `F1=ON` ...; ours), and for 103 `M:ONE.TXT Protection = Write` (`Read`, `Write`,
  `Delete`, `None`; the password isn't shown, ours).
- **Drive options** (no file specification; `d:` or the current drive): `RO` (14, then 28, the current drive
  selected again) and `RW` (37: the drive reset, read-write unless its unit is read-only), `M: set to Read Only
  (RO)` and `Read Write (RW)`; `NAME=name.typ` (100: the label's name), `PASSWORD=pw` and `PASSWORD=` (the
  label's password, or none), `PROTECT=ON|OFF` (the label's bit 7: passwords enforced on the drive), `CREATE=`,
  `ACCESS=` and `UPDATE=ON|OFF` (the label's bits 4, 6 and 5: `ACCESS=ON` clears CREATE and `CREATE=ON` ACCESS,
  the Guide). Each label option changes the label by one 100, its other bits kept (101 read first; a drive with no
  label gets one, named `LABEL` as the Guide's default when `NAME=` isn't given, ours), then one line: `M: Label =
  NAME.TYP`, `M: Label Password set` or `removed`, `M: Password Protection = ON`, `M: Create = ON` (`Access`,
  `Update`) (ours). A label with a password asks for it, `Password: `, and a line is read (the Guide: SET always
  prompts); a wrong one is `Wrong Password` (Appendix A). Stamps asked for on a drive without SFCBs: `Directory
  needs to be reformatted for date/time stamps.` (Appendix A), nothing changed.
- **`DEFAULT=pw`** (106): the default password, no file or drive (ours: nothing written).
- **Errors** (Appendix A): `No options specified` (none, or no list), `Cannot set both RO and RW.`, `Cannot set
  both SYS and DIR.`, `Cannot have both create and access time stamps`, `Option only for drives.` (a drive option
  with a file), `Option requires a file reference.` (a file option without one), `Cannot label a drive with a
  file referenced` (`NAME=` with a file), `File not found.`, `Invalid file specification.`, and for an unknown
  option or value `Invalid option` (ours: Appendix A gives SET none). Password protection off when a file's
  password or mode is set (the label's bit 7 clear, or no label): `Password protection is off: SET [PROTECT=ON]`
  (ours), nothing changed.

**SHOW** (5-99 to 5-102; `SHOW.COM`): `SHOW {d:}{[SPACE|LABEL|USERS|DIR|DRIVE]}`, one option, any unambiguous
prefix.

- **SPACE** (or none): `A: RW, Space:     8,064K` (the access mode by 29, the free kilobytes by 46, with commas,
  right in 9, ours), for `d:` or, without one, each logged-in drive (24), A to P (the Guide).
- **LABEL:** `Label for drive A:`, a blank line, the Guide's two heading lines, dashes, and the label: its name
  and type as `NAME.TYP` (12), `Passwds Reqd` `on` or `off` (bit 7), `Stamp Create` (or `Access`, as the label
  asks for; `Create` when neither, ours) and `Stamp Update` `on` or `off`, the label's own creation and update
  stamps (`MM/DD/YY HH:MM`). No label: `No directory label exists.` (Appendix A).
- **USERS:** `Active User :  n` (the current), `Active Files:` and the users that have files, `A: # of files:` and
  each one's count under it (4 a column, ours; a file is its extent-zero entry, as DIR), then the free entries
  (as DIR's line below).
- **DIR:** `A: Number of free directory entries: 24` (entries that are free, not SFCBs).
- **DRIVE:** `    A: Drive Characteristics` then, each number right in 7 with commas (ours), the record capacity
  ((DSM+1) × BLS / 128), the kilobytes ((DSM+1) × BLS / 1024), the directory entries (DRM+1), the checked ones
  (CKS's low 15 bits × 4, the check vector having a byte for each directory record of 4 entries: 0, fixed media),
  records an entry ((EXM+1) × 128), records a block (BLM+1), sectors a track (the DPB's SPT, 128-byte sectors,
  ours) and bytes a physical record (128 × 2^PSH), with the Guide's words.
- **Errors** (Appendix A): `Unrecognized drive.` (a letter past P), `Unrecognized option.`, `Unrecognized
  input.` (anything else); a drive that isn't (`H:`) is the BDOS's error.

**INITDIR** (5-52; `INITDIR.COM`): `INITDIR d:`, the drive required (`Invalid drive.` without one, ours).

- **A directory without SFCBs:** `INITDIR WILL ACTIVATE TIME STAMPS FOR SPECIFIED DRIVE.` and `Do you really
  want to re-format the directory: M (Y/N)? ` (the Guide); `Y` makes every fourth entry an SFCB: the entries in
  those places (files, XFCBs, the label) are moved to free entries first, written before the SFCBs over them
  (ours: a failure between leaves two copies, never none, and the next INITDIR finishes the job). Each SFCB's
  stamps are 0, and its password modes those
  of the files' XFCBs (as the BDOS keeps them, 3b1). Not room for them (more entries in use than three quarters of
  the directory): `Not enough room in directory.` (the Guide), nothing changed. The label's stamp bits aren't set:
  SET does that (the Guide).
- **A directory with them:** `Directory already re-formatted.` and `Do you wish to recover date/time directory
  space (Y/N)? `; `Y` makes every SFCB free and clears the label's stamp bits (ours: a label mustn't ask for stamps
  where there are no SFCBs, as 100 refuses it). `N`: `Do you want the existing date/time stamps cleared (Y/N)? `;
  `Y` zeroes every SFCB's stamps, their password modes kept (ours).
- **How:** the directory's sectors read and written by the BIOS through function 50 (SELDSK, SETTRK, SETSEC,
  SETBNK 1, SETDMA, READ, WRITE: no BDOS or BIOS change), the drive reset (37) before, so its buffers are written
  back, and after, so the BDOS reads the new directory at its next login. The entries to move and the XFCBs'
  modes are kept in memory: a directory with more than fit is `Not Enough Memory` (Appendix A), nothing changed.
  A read-only drive (29) is `Drive Read Only` (Appendix A's words); a sector the BIOS can't read or write, `Disk
  Read Error` or `Disk Write Error` (ours). No label password is asked (ours: the Guide names none).

**DATE** (5-11 to 5-13; `DATE.COM`): `DATE {CONTINUOUS}`, `DATE {time-specification}`, `DATE SET`.

- **DATE:** `Sat 10/31/26 12:00:00`, the day from the days (105), then the date and the time with seconds. `DATE
  C` (`CONTINUOUS` or any prefix, ours) writes it again on the same line (CR first) each second until a key, which
  is taken (ours).
- **`DATE MM/DD/YY HH:MM:SS`:** checked (month 1–12, the day in the month, leap years every fourth as the clock's,
  hour 0–23, minute and second 0–59; the year 20YY, ours: OODOS's clock holds 2000 on), then `Press any key to
  set time`, a key, the clock set (the SCB's date, hour, minute and second by 49, then the BIOS's TIME by 50, so
  the seconds are kept: 104 would make them 0), and the date and time shown as above.
- **`DATE SET`:** `Enter today's date (MM/DD/YY): ` and `Enter the time (HH:MM:SS): `, each a line read, an empty
  one keeping that part (the Guide), then as above.
- **Errors:** `Illegal date/time specification.` (Appendix A) for anything else; a clock past 2155 (105's `$FF`):
  `No clock` (ours).

**DEVICE** (5-14 to 5-18, 3.5; `DEVICE.COM`): over the BIOS's DEVTBL, read by function 50's DEVTBL and its 24
bytes copied by MOVE (XMOVE from bank 0, where W2 is the BIOS's block in a function 50 call), and the SCB's
redirection vectors (`$22`–`$2B`, by 49: bit 15 CRT, 14 TTY, 13 LPT).

- **DEVICE:** `Physical Devices:`, `I=Input,O=Output,S=Serial,X=Xon-Xoff`, the devices three a line (the name in
  6, the baud rate in 5 or `NONE`, the letters it has, in a field of 21, ours), a blank line, `Current
  Assignments:` and a line each for `CONIN:`, `CONOUT:`, `AUXIN:`, `AUXOUT:` and `LST:` (`CONOUT: = CRT TTY`, or
  `Null Device`), a blank line, then `Enter new assignment or hit RETURN` and a line read, taken as a DEVICE
  command's tail (an empty one: nothing).
- **NAMES:** the physical devices only; **VALUES:** the assignments only; **a physical device** (`DEVICE TTY`):
  its line; **a logical one** (`DEVICE CON:`): its lines. The logical names and their others (the Guide):
  `CONIN:`, `CONOUT:`, `AUXIN:`, `AUXOUT:`, `LST:`, `CON:` and `CONSOLE:` (both console vectors), `KEYBOARD`
  (CONIN:), `AUX:` and `AUXILIARY:` (both auxiliary ones), `PRINTER` (LST:); the colon optional (ours).
- **`DEVICE logical = physical {[options]} {,physical {[options]}...}`** sets the vector to those devices;
  `= NULL` to none. A device without input in an input vector, or without output in an output one, is `Invalid
  Physical device.` (ours).
- **Options of a physical device** (Table 5-4; also `DEVICE physical [options]`): a baud rate, `XON`, `NOXON`.
  OODOS's BIOS sets neither (no device has CP/M 3's soft-baud bit, and none does XON/XOFF): a baud rate is `Baud
  rate cannot be set for this device` (Appendix A), `XON` `Xon-Xoff cannot be set for this device` (ours),
  `NOXON` is taken (it is so already, ours).
- **`DEVICE CONSOLE [PAGE]`:** `Console width set to 80 columns` and `Console page set to 24 lines` (ours, the
  Guide shows none): the SCB's `$1A` + 1 and `$1C`. `[COLUMNS=n, LINES=n]` sets them (columns 1–256, lines 0–255,
  ours: under 2 is no stops, as the built-ins) and shows them so.
- **Errors** (Appendix A): `Bad Logical Device Assignment`, `Invalid Physical device.`, `Invalid option.`,
  `Invalid number.`, `Invalid delimiter.`, `End of line expected`.

**SETDEF** (5-95 to 5-98; `SETDEF.COM`): `SETDEF {d:{,d:{,d:{,d:}}}} {[TEMPORARY=d:] | [ORDER=(typ{,typ})]}`,
`SETDEF [DISPLAY|NO DISPLAY]`, `SETDEF [PAGE|NO PAGE]`; the options together in one list (ours).

- **The drive chain** (the SCB's `$4C`–`$4F`: 0 the default drive, 1–16 A–P, `$FF` the end): up to four, `*` the
  default drive (the Guide). `TEMPORARY=d:` is `$50` (0 the default, `*`). `DISPLAY`/`NODISPLAY` is the CCP's
  display flag (`$18` bit 7, 4a), `PAGE`/`NOPAGE` the page mode (`$2C`: 0 on, `$FF` off). `ORDER=(COM)` is taken;
  a `SUB` in it is `ORDER=(SUB): no SUBMIT yet` (ours: the CCP runs `.COM`s only until phase 5).
- **SETDEF** alone, and after a change, shows them (ours, the Guide shows none):
  ```
  Drive Search Path:
  1st Drive            - Default
  2nd Drive            - M:

  Search Order         - COM
  Temporary Drive      - Default
  Console Page Mode    - On
  Program Name Display - Off
  ```
- **Errors** (Appendix A): `Drive defined twice in search Path`, `More than four drives specified.`, `Invalid
  drive.`, `Invalid option.`, `Invalid type for ORDER option.`, `Invalid delimiter.`

**Testing:** `test_oodos_utils` (`oodos_rig.h`, as `test_oodos_ccp`): each utility put on M: as a SYS file behind
OODOS's back, run from the CCP (the search chain or `M:`), every form and option and error above compared line for
line, and what each leaves on M: read back; one case from the committed card's A:, where `make_card.sh` put them.
What ERASE, RENAME, SET and INITDIR leave on a card is checked by cpmtools too (`interop.sh --utils`, the card
saved by `OODOS_UTILS_OUT`).

**Proposed (4b1, as built, 2026-10-31)** ([`oodos/utils/`](../oodos/utils/),
[`oodos/include/util.inc`](../oodos/include/util.inc); the plan above is what was built, and these are the details,
the choices it left open marked ours):

- **No BDOS or BIOS change.** Sizes are the `.COM`s', the 16-byte header among them: DIR 7,868, TYPE 4,147, ERASE
  4,091, RENAME 4,452, SET 6,766, INITDIR 5,610, SHOW 5,382, DATE 4,436, DEVICE 5,579, SETDEF 4,631 (after the first
  review: 69 bytes each for `UT_SPEC`'s trailing-delimiter check and `UT_ISRO`, and DIR's, SET's, INITDIR's, SHOW's,
  DEVICE's and SETDEF's own fixes besides; after the second, DIR 58 more for its heading after a page stop or form
  feed and its quiet stops, SET 36 for its read-only check, INITDIR 50 for its stopped recover, and the others 26
  each when the quiet stops became `cmd.inc`'s for every program; after the third, DIR 240 more for its lines' one
  routine, `LINE`, and INITDIR 13 for leaving a stopped re-format's copies to the re-format, not a recover; after the
  fourth, DIR 20 more for the heading at a sheet's top that doesn't show and on a short page's sheets). ld65 links all
  of `util.inc` into each, its sections a utility doesn't call among them (a later phase may split it, or link it
  as a library). Each fits the TPA with tens of KB to spare; what a utility reads into memory (names, a drive's
  directory, entries to move) goes from the end of its BSS to the TPA's top (`$0204`'s word, `UT_MEM`), so an RSX
  below the BDOS shrinks it.
- **`util.inc`** grew a section per area: *memory* (`UT_MEM`: from the end of BSS to the TPA's top, the room in
  A/X); *files* (`UT_FDRIVE`, the drive a specification names; `UT_ISWILD`; `UT_PWSET`, a password to the DMA
  buffer; `UT_NAMES` and `UT_NNEXT`, a wildcard's names read into memory first, then taken one by one, since a file
  call between 17 and 18 ends the search; `UT_SAID`, after an error the BDOS said, so nothing more is owed; `UT_ISRO`, a drive read-only by 29, for ERASE's and RENAME's `Drive Read Only`);
  *the system* (`UT_OAT`, an option list wherever it is in the tail; `UT_OCLOSE`; `UT_SCBGET`, `UT_SCBSET`,
  `UT_SCBSETW`/`UT_SCBW`, the SCB by 49; `UT_BIOS`/`UT_BPB`, a BIOS call by 50).
- **For all of them (ours):** the tail is parsed and checked before anything is changed; the return code is `$FF00`
  after any message, `No directory label exists.` and `No File` among them; a specification whose delimiter has
  nothing after it but blanks and option lists (`ERASE X.TXT,`, `RENAME A=B=`) is refused as a bad one, the
  utility's message for a specification 152 refuses (`UT_SPEC`); TYPE, ERASE and RENAME give a drive in the FCB's
  byte and leave the current drive alone, DIR, SET, INITDIR and SHOW select the drive they work on (14), and every
  utility's end (`UT_END`) selects the one current at its start again (14); after a BDOS error a utility goes on with the next file, if there is one, and
  the code is `$FF00` (SET too: a file whose 30 is refused isn't given its 103); a ^C is `$FFFE`.
- **DIR** (functions 14, 17, 18, 24, 25, 26, 31, 32, 45, 49 for `$1A`, `$1C`, `$2C`, `$38`, 5, 108, 109). Its records
  go from its BSS to the TPA's top, about 40 KB, some 1,190 files a drive. **Ours:** `$FF00` after any error, any
  `No File`, `Date and Time Stamping Inactive`, `Not Enough Memory for Sort.` and a drive the BDOS refused; the
  last `No File` only when nothing was found anywhere and `MESSAGE` isn't given; the groups (a drive and user)
  apart by a blank line, and a BDOS error followed by one; for a refused drive, or a directory read that fails,
  the line owed is left to the BDOS's CR LF. Every line DIR writes (a file's, each heading line, the blanks,
  totals, messages) begins in one routine (`LINE`), which counts it for the page (paging's stops) and for
  `LENGTH=`'s sheet (its lines over every group, the heading's own among them), so neither holds more than its
  lines: a page the SCB's page length less one, a sheet `LENGTH=`. A sheet ends when its count is reached, with a
  form feed under `FF` and the printer echo on. Lines that belong together go to the next page or sheet when they
  don't fit the rest of this one (ours): a group's `Directory For` line, its blank, its column heading and first
  file; the column heading and a file; the blank and the two totals lines. On a new page or sheet, within a FULL or
  DATE group (its files and totals), the column heading comes first, shown or not, so it is never written twice
  running; the blank before the totals is left out at such a top, the heading's own blank standing for it, so
  a group's totals come out the same with the printer echo on or off (ours). The blank between groups is left
  out at a top that shows, after a stop or a form feed, and kept on a sheet that doesn't show (`LENGTH=` without
  `FF`, or the echo off), so there the later sheets start a line later than with the echo on (ours). On a
  page of 5 (4 lines and the stop) the heading and the two totals lines don't fit together: the heading wins,
  so the totals are split over two pages, each under the heading (ours). With
  `LENGTH=` and paging both, the heading starts each page and each sheet, and the printer has the screen's
  lines (ours); when exactly 5 lines are left on the page at a sheet's top, the page stops one line early, its
  room counted for the totals' blank the heading now stands for (accepted, 2026-11-01: never over-full). Without `LENGTH=` there is no sheet count (`NOPAGE`, `FF` without `LENGTH=`, the SCB's page mode
  off: no repeats); SIZE and the short form have no column heading, so their pages and sheets only hold their
  lines. A page under 5 lines (4 or fewer and the stop's) is too short for the heading and a file, so the heading
  isn't written again at its stops (ours); `LENGTH=`'s sheets on such a page still start with it. The page's stop
  for lines kept together is `cmd.inc`'s `CM_STOP`
  (`CM_LINE`'s own stop, a label added for DIR; no other program's bytes changed). A line the BDOS writes (a
  refused drive's error, a directory read that fails), and the line owed that its CR LF ends, isn't counted, so
  a page or sheet after one can hold that many lines more (ours). DATE shows
  the stamps on a drive without a label too (FULL doesn't, the Guide). **Speed:** each entry's record is found
  by a search of those made so far, and the pointers are sorted by insertion, so both grow as the square of the
  files. Measured on M: (60 and 190 files in random order): the lookup about 64 n² cycles, the sort about
  43 n², the BDOS's scan about 4,600 an entry; so a full `oodos-8m` drive (768 files) takes some 9–10 s at
  7.16 MHz before its first line, and a full `oodos-32m` (about 1,190 files, what fits) some 22 s (the review
  estimated 5 s and 15–20 s). Accepted for 4b1; a ^C
  is looked for every 16 entries read and every 16 pointers placed, so either can be stopped (`$FFFE`). `FF` sends its form feed to LST:
  only with the printer echo on (`$38`), first and at each sheet's end. SIZE's item is `NAME TYP` and the
  kilobytes in 5 and `k`, `" : "` between, the width / 21 items a line (at least 1). DATE's layout is FULL's first
  12 heading columns and its last two; DATE on a drive without SFCBs is `Date and Time Stamping Inactive` for that
  drive only. FULL's lines are of fixed width (trailing blanks kept). A file's extent-zero entry is the one with S2
  0 and EX without EXM's bits 0, its attributes and stamps the file's; a file without one, or an XFCB without its
  file, isn't listed. Prot comes from the XFCB's mode bits (`Read`, `Write`, `Delete`, else `None`); the last
  column is `Access` when the label's bit 6 is set. `USER=` takes ALL or 0–15; `DRIVE=` ALL, `d`, `d:` or `*`, a
  drive given twice listed once. `EXCLUDE` with no specification is `No File`. `NOSORT` is the extent-zero
  entries' order, the same memory message. At most 64 specifications, else `Illegal command tail.` **Departure:**
  `Illegal command tail.` for a specification 152 refuses is reached only by `M:DIR` typed, as the CCP refuses it
  first; for a delimiter with nothing after it, by the CCP's hand-off too (`DIR M:, [FULL]`: the CCP takes `M:,`).
  A page stop's message and its rubout are written in console mode bit 2 (raw, no printer echo: `cmd.inc`'s,
  for every program that pages with it, the built-ins too), so a printout made with ^P has no stop on it.
  **Decided (2026-10-31, the user's):** `DRIVE=ALL` is the logged-in drives (function 24): the Guide's Table
  5-5 says "all accessed drives" and its example the drives of the search chain, and the table was followed. It is
  one routine, `ALLDRVS` in `dir.s`.
- **Every utility's calls** through `cmd.inc` and `util.inc`: 25 and 45 at the start, 14 at the end, 108 for the
  return code; 2, 6, 9, 11 and 109 for writing, paging and a ^C, 49 for the SCB's page mode and length, 10 for a
  line read (a prompt, a Y/N answer), 152 for a file specification, 26 and 17, 18 for a wildcard's names. The
  lists below are each utility's own besides.
- **TYPE** (15, 20, 26, 49). **Ours:** `[PAGE=x]`, a second
  list or one not closed is `Try 'PAGE' or 'NO PAGE'`; of `PAGE` and `NOPAGE` the last given wins; a second
  specification is `Invalid file specification.`; `Invalid drive.` only for a character other than A to P before
  a `:` (`Q:`, `@:`; a drive that isn't, `H:`, is the BDOS's `Invalid Drive`); asked for (`Enter filename: `)
  once, an empty line ending it, and options typed in that line taken; a wildcard's names are read first, the
  current user's SYS files among them, each name line with its drive; a file that doesn't end in LF owes a line before the next name; the password given
  is used for every file; a page length under 2 never stops.
- **ERASE** (19, 26, 29). **Ours:** `[CONFIRM=1]`, a second list or one not closed is `Missing delimiter or
  Unrecognized Option`; the drive is always in CONFIRM's question, which ends with a blank (the built-in's wildcard
  question has none); a single name with CONFIRM is asked about too; `Drive Read Only` is checked by 29 just before
  each 19 (after a wildcard's question, at CONFIRM's first Y) and ends ERASE, so it covers a drive made read-only
  by 28 (SET) or found so by an earlier write; a write-protected unit not yet written is still the BDOS's `Read
  Only`.
- **RENAME** (17, 19, 23, 26, 29). **Ours:** any option list (in the tail or a line asked for), a blank name, two
  drives, more or fewer than two names, a specification 152 refuses, and new the same file as old (`*.TXT=*.TXT`
  too) are `Incorrect file specification.` (where the built-in says the command and `?`); an N leaves `$FF00`; the
  `NEW=OLD` line has no drive; a wildcard's `?` places must be exactly the same, else `Invalid wildcard.`; for each
  file, before anything else, a read-only drive (29) is `Drive Read Only` and ends RENAME, and an old file with
  t1' set (17) is `Not renamed, filespec read only.`, on to the next; any other refusal of 23 is the BDOS's.
- **SET** (152, 14, 17, 18, 26, 28, 29, 30, 31, 32, 37, 49, 100, 101, 103, 106, 108, 109). **Ours:** everything
  checked before anything changes (the label's SFCB check and password among it, and a label option on a drive
  read-only by 29 without `RW` is `Drive Read Only` before the password is asked, so a failed one leaves the default
  password too; a unit read-only itself, with `RW`, is still the BDOS's refusal after `DEFAULT=`); a drive's options done in the order `DEFAULT`, `RW`, the label's options as given, `RO`;
  a file's 30 before its 103; one 100 for each label option, an option given twice taking its first place and last
  value, a new label password used for the 100s after it; the label's password asked once, and only when the label
  has one (8 blanks or zeros are none), at most 8 characters, in capitals, compared here; `PROTECT=` takes
  `READ`–`NONE` for a file and `ON`/`OFF` for a drive, the wrong one `Option only for drives.` or `Option requires
  a file reference.`; values are whole words; `Assign a Password to this file.` when a mode isn't 0 and there is no
  password (`[PASSWORD=]` on a file without an XFCB refused so); the `Protection =` line names the highest mode bit;
  `DEFAULT=` takes no file; `Invalid option` also for a second list or one not closed, `Invalid file
  specification.` for a second specification or `a=b`, `Not Enough Memory` for a wildcard's names that don't fit,
  `Directory full` when 100 or 103 return `$FF` with X 0 (that one ends SET); 30 clears F5'–F8'; a wildcard's
  lines are paged. `PROTECT=NONE` without `PASSWORD=` writes 8 blanks as the new password (103's bit 0): the
  password is deleted. A file's mode and whether it has a password come from its first XFCB in directory order,
  as the BDOS takes it. On a drive without SFCBs every label option writes the label without its stamp bits (6–4),
  as 100 refuses them there: a label made elsewhere is repaired, not `Directory full`.
- **INITDIR** (14, 17, 18, 26, 29, 37, 50: SELDSK, SETTRK, SETSEC, SETDMA, SETBNK 1, READ, WRITE; 108). It assumes
  512-byte sectors, 16 a track, as OODOS's own drives have; an `apple-do` drive (6b) has 8 a track, but its
  directory's 4 sectors are all in its first track, so the arithmetic holds there too. **Ours:** the tail is `d:` alone, else `Invalid drive.`;
  29 before 37; room and memory checked before the question; room is the entries in use at most three quarters of
  the directory, an SFCB (`$21`) already in its place not moved; moved entries go to the lowest free entries, in
  their order; a slot's mode is that of the first XFCB of its file in directory order, `AND $E0`; clearing the
  stamps zeroes a slot's bytes 0–7 and keeps 8, 9 and 31; recovering writes 32 `$E5`s over each SFCB and ANDs the
  label's mode with `$8F`; a BIOS error resets the drive (37) and fails. The 37 before the first write is checked
  (the BDOS says its error; nothing written). The moves are made once without writing first: if the BIOS's sectors
  haven't the free entries for every one (the BDOS's view and the disk's differing), `Not enough room in
  directory.` and nothing written. A BIOS error once a re-format or a recover has written something adds `Directory
  partly re-formatted: run INITDIR again.` (clearing the stamps doesn't), and the next INITDIR finishes it: an entry in an SFCB's place with an exact
  copy outside them (the first try's) isn't moved again nor counted for room, an SFCB in its place isn't counted,
  and a directory whose entry 3 is an SFCB but another place isn't is `Directory partly re-formatted.`, then made
  as one without SFCBs (the question asked). A recover writes sector 0 first, so one stopped leaves entry 3 free
  and later places SFCBs: a directory with an SFCB in a place but not in entry 3 is `Directory partly
  re-formatted.` and `Do you wish to recover date/time directory space (Y/N)? `, `Y` finishing the recover, `N`
  going on to the re-format's question; but if entries in places also have copies outside them (a re-format stopped
  after such a recover), only `Directory partly re-formatted.` and the re-format's question, since a recover would
  leave both copies. The copies are looked for comparing from byte 0 up (the user and name differ first). **Accepted (2026-11-01, the user's):** that look is every entry outside the SFCB places against every listed one, so on a nearly full 32M drive (1,536 entries) the question comes after about 11 s with nothing shown; comparing the block pointer first was measured at about 8 s on a real drive and slower on one of empty entries, and not taken. A drive used between the two tries may hold two different copies of
  an entry: run INITDIR again first. Cold boot's M: has SFCBs holding `$E5`
  past their first byte, so after `stamps cleared` a slot's bytes 8, 9 and 31 stay `$E5` (as designed).
- **SHOW** (14, 17, 18, 24, 25, 26, 29, 31, 32, 45, 46, 49, 108, 109). **Ours:** `[]` is SPACE; an option with a
  value, or a second, is `Unrecognized option.`; anything after the list, a second list, one not closed or a drive
  after it, `Unrecognized input.`; USERS counts SYS files; the extent-zero test is S2 0 and EX without EXM's bits
  (`AND $1F`) 0; a label with a blank type is shown `NAME`, not `NAME.`
- **DATE** (6, 10, 49, 50, 105). It sets the clock by the SCB's `$58` word and `$5A`–`$5C` (BCD, by 49), then the
  BIOS's TIME with A `$FF` (50). **Ours:** numbers of one or two digits, blanks between parts allowed; `SET` in
  full, `CONTINUOUS` any prefix; a `DATE SET` line of only blanks and tabs is an empty one; the parts kept by
  `DATE SET`'s empty lines are read at the key; a bad line ends `DATE SET` with the error; `No clock` only when a
  kept part needs the clock; any key, a ^C too, ends CONTINUOUS with 0; DATE isn't paged (console mode bit 0
  would hide the key).
- **DEVICE** (10, 49, 50: DEVTBL, XMOVE from bank 0 to 1, MOVE 24 bytes, so at most 3 devices). **Ours:** `End of
  line expected` after the command is done, then `$FF00`; `NAMES`, `VALUES`, `NULL` in full, device names exact
  (`CONSOL` refused); an unknown word is `Invalid Physical device.`, or `Bad Logical Device Assignment` with `=`, as
  is a physical device's name with `=` (`DEVICE CRT = TTY`);
  `NULL` alone (`NULL,CRT` refused); commas between devices, else `Invalid delimiter.`; an option list on a logical
  device but the console is `Invalid option.`; `COLUMNS` or `LINES` without a value, or with a list, is `Invalid
  number.`;
  assignments write nothing; NAMES is the two headings and the devices, VALUES the heading and five lines; a list
  not closed is `Invalid option.`; no padding after a line's last field. **Departure:** the prompt is `Enter new
  assignment or hit RETURN: `, with a colon, as the Guide prints it (the plan had none).
- **SETDEF** (49, 108, 109). It keeps `$18`'s other bits; the chain is `$4C`–`$4F`, the temporary drive `$50`, the
  page mode `$2C`. **Ours:** commas between drives, else `Invalid delimiter.`; a trailing comma or a bad drive
  `Invalid drive.`, as is a list for `TEMPORARY`; `*` and the current drive's letter are distinct entries; a
  second list, or anything after the list, `Invalid delimiter.`; a list not closed, a stray value, `TEMPORARY` or
  `ORDER` without one, `Invalid option.`; an option given twice, the last wins; `SUB` in
  `ORDER` is `ORDER=(SUB): no SUBMIT yet`. SHOW, DEVICE and SETDEF are paged.
- **The layout, decided (2026-10-31, the user's):** the CCP ends the command's line (the LF it owes, `CM_LFDUE`,
  by `RUNTAIL`'s `CM_FLUSH`) before any program runs, so a utility that writes nothing (ERASE of one name, a DEVICE
  assignment, `SET [DEFAULT=pw]`) leaves one blank line before the next prompt (`\r\n\r\nA>` after the line
  typed) that the built-ins don't (`\r\r\nA>`). Accepted: the plan's "the same bytes" holds when the command
  writes something.
- **The card:** `make_card.sh` builds the ten (`make utils`) and puts them on A: in user 0 as SYS files, after the
  files it had, then two one-line text files, `SPACER1.DAT` and `SPACER2.DAT` (ours; typed DAT so that a `dir *.txt` on A:, as test_oodos_ccp runs, doesn't list them): 21 files in 23 entries, so
  WRITEOO1's `0:OO1.BIN` lands in entries 32 and 33, the first in an SFCB's first slot, as 16 was before; the ten
  alone would have put it in 29 and 30, and fsck.cpm 2.23's backwards first-slot case would no longer be exercised
  by `--written` (with the spacers its sums warn of 1, let through as before). *(2026-11-02: WOZMON and APPLE2, two utilities more,
  take the spacers' two entries, and the spacers are gone: 21 files, 23 entries still.)* `test_oodos_boot` checks each utility on the card against the
  build and its SYS attribute.
- **Tests:** a suite an area (`test_oodos_utils_set`, `_files`, `_dir`, `_sys`), the utilities put on M: and the
  search chain M: first, then the default drive, so the build's copies run, not the card's; `X.COM required` with
  the chain B: alone. Each suite has a case for the card's own copies on A: (TYPE on README.TXT, DIR's FULL of A:,
  SHOW, SETDEF, DEVICE and DATE), and `test_oodos_card` runs ERASE, RENAME, SET and INITDIR from the card on A:
  for `interop.sh --utils` (`OODOS_UTILS_OUT`): `fsck.cpm`, the label, `cpmls -F`'s attributes, protections and
  stamps, the SFCBs and the XFCBs INITDIR moved, `cpmcp`; seen to fail on wrong images. cpmtools lists an XFCB as
  a file of user 16 and more, with no bytes, and shows a stamp of 0 days as `12/31/77 00:00`.
- **For later phases:** `oodos-32m` (2,048 entries) can hold more files than DIR's ~1,190, so `Not Enough Memory
  for Sort.` there; DIR's lookup and sort could be made faster than the square of the files (a sorted insert
  with a binary search, a shell sort: DIR's as-built note has the times); `put_m` tests must clear SFCB slots (MFORMAT leaves `$E5`, which reads as dates); SETDEF's
  `ORDER` and `SUB` wait for SUBMIT (phase 5); DEVICE's baud rates and XON wait for a BIOS that sets them;
  `CONOUT:=LPT` or `CONIN:=NULL` takes the console away, as in CP/M 3.

### PIP, phase 4b2 ###

**Proposed (4b2, 2026-11-04)** (the User's Guide's PIP, 5-63 to 5-76, and Appendix A's messages; where it is
silent the choice is ours, and says so). `oodos/utils/pip.s`, a utility as 4b1's are (`cmd.inc`, `util.inc`, SYS
in user 0 on the card); the owner chose to implement every option, H and I among them, and the banner
`OODOS PIP 0.2`.

- **Forms:** `dest=src{,src}`, where a destination is a file specification (a password after `;`), a drive `d:`,
  or a device, with `[Gn]` its only option; a source a file specification, a drive (the short form), or a
  device, each with an option list.
  - **One file:** `d:=src`, `dest=d:` (the name from the other side), `dest=src` (renamed on the way).
  - **Wildcards:** `d:=*.TYP`, each match copied under its own name, `COPYING -` and a line a name. A wildcard
    destination is refused (ours) unless it is all wildcards (`B:*.*`, the same as `B:`); a wildcard source with a
    file destination too (ours: what it would mean isn't the Guide's).
  - **Joining:** `dest=a,b,c`, left to right. A source of the destination's own drive, user and name is refused
    (ours), and so is a copy onto itself (`B:=B:X`, the same user): `INVALID FORMAT`.
  - **No tail:** `OODOS PIP 0.2`, then a `*` prompt a command, until an empty line (or ^C) ends it. The line is
    taken in capitals, except the strings of S and Q (ours: from the tail they are in capitals, as the CCP makes
    every tail).
- **Devices:** `CON:` (in: function 1, typed and echoed, ^Z ends it; out: 2), `AUX:` (3 and 4), `LST:` (5),
  `PRN:` (LST: with `[T8 N P60]` on each source), `NUL:` (a source: 64 zero bytes), `EOF:` (a source: one ^Z). A
  device as a destination of NUL: or EOF:, or LST: or PRN: as a source: `INVALID DESTINATION`/`INVALID SOURCE`. On
  OODOS CON: is CRT and TTY, AUX: TTY, LST: LPT (DEVICE moves them; LPT takes and drops, no parallel port yet).
- **How a file is written:** to `NAME.$$$` on the destination's drive and user (one there deleted first; a
  read-only one: `CAN'T DELETE TEMP FILE`, the end), then, closed: the destination deleted if there (read-only:
  `DESTINATION IS R/O, DELETE (Y/N)?`, Y deletes it, N is `**NOT DELETED**` and the temporary file's deletion;
  `[W]` deletes it without asking), and the temporary file renamed. A failed write (a full disk) is `DISK WRITE
  ERROR - NAME.TYP` and the temporary file deleted; a read-only drive `R/O DISK` before anything is written.
- **Attributes:** a file copied alone takes its source's RO, SYS and F1-F4 (and its archive bit clear, so it is
  backed up again: ours); a joined one none (ours). **Passwords:** a source's after `;` is given to the BDOS to
  open it; a destination with one gets it in READ mode (the XFCB, 103); `d:` alone gives the copy its source's
  password and mode, when the source's was given (ours: one not given can't have opened it).
- **Block or character:** a transfer is by records unless a source has an option other than A, C, Gn, K, O, R,
  V and W, or a device is on either side; then by characters, to the source's first ^Z, a ^Z after the last
  character written. Joined by records, a text source's last record is cut at its first ^Z (`[O]`: not).
- **Options** (letters, packed or with blanks between, a number straight after its letter; S and Q a string to
  a ^Z): **A** only files whose archive bit is clear (in any of their entries, from 2026-11-04: the BDOS clears it
  in the entry of the extent written, so every entry is read, a file named once, by its first entry without t3',
  whose attributes a single copy takes: extent 0's too, as 30 sets every extent), the bit set on each source
  after its copy; **C** `COPY
  NAME.TYP (Y/N)?` before each file copied to a drive (ours: the Guide gives no words, and joins none); **Dn** a line cut after column n; **E** what is
  written echoed to the console; **F** form feeds dropped; **Gn** the source's user (the destination's, after
  it), 0-15, else `INVALID USER NUMBER`; **H** each line an Intel hex record checked (`:`, hex digits, the length,
  the checksum): `INVALID DIGIT`, `CHECKSUM ERROR`, `RECORD TOO LONG` (over 80 characters) or `UNEXPECTED END OF HEX
  FILE`, each with the file's name, the copy ended (ours: the Guide's `CORRECT ERROR, TYPE RETURN OR CTRL-Z`
  is for a paper-tape reader's second try, which a file can't give); **I** H, and `:00` records dropped; **K** no
  `COPYING` list (ours: the Guide names K among the options that keep a record transfer, and no more); **L** and
  **U** letters to lower or upper case; **N** a line number at each line's start, `     1: ` (six places and a
  colon: ours), **N2** `000001` and a tab; **O** no ^Z ends a file; **Pn** a form feed at the start and every n
  lines (P or P1: 60); **Qs** the copy ends after the string s, **Ss** it starts at s (both kept), else `QUIT NOT
  FOUND`/`START NOT FOUND` at the source's end; **R** SYS files read (found by wildcards too); **Tn** tabs to
  every nth column; **V** the destination read back and compared (a CRC of what was written, ours), `VERIFY ERROR
  - NAME.TYP`; **W** read-only destinations deleted without asking; **Z** bit 7 of each character cleared. In what
  order (ours): Z, S and Q on the source's characters, then F, L or U, D, T, N and P on what is written, E last.
- **Errors** (Appendix A's): `INVALID FORMAT`, `INVALID SEPARATOR`, `BAD PARAMETER`, `INVALID USER NUMBER`, `FILE
  NOT FOUND - NAME.TYP`, `R/O DISK` (a read-only destination file is the question above), `USER ABORTED` (^C,
  between records or characters: the temporary file deleted); a full directory, `DISK WRITE ERROR - NAME.TYP`
  too (ours: the Guide's PIP has no message of its own for it); a wrong password, and the BDOS's other errors,
  the BDOS's own message (error mode $FE, as 4b1's utilities). In a copy to a drive a file the BDOS won't open is
  skipped and the copy goes on with the next, as the Guide says of a wrong password; any other error ends the
  command. After
  an error in the `*` mode the next command is asked for; from a tail, the end. The return code is $FF00 after any.

**Proposed (4b2, as built, 2026-11-04)** ([`oodos/utils/pip.s`](../oodos/utils/pip.s); the plan above is what was
built, and these are the details):

- **No BDOS or BIOS change.** PIP.COM is 8,661 bytes. It keeps its pointers in zero page (four bytes; six from 2026-11-04, `seen`'s own), unlike
  `cmd.inc` and `util.inc`, which write theirs into their own code.
- **One pipeline.** The command is split at `=` and `,` outside option lists (an S or Q string may hold either),
  each entry's options a byte a letter, its password kept apart from its FCB (an open overwrites FCB bytes 16-31).
  By records, a source is read a record ahead, so its last record is known (joined text is cut at its ^Z there);
  by characters, each byte goes through Z, S, H or F/L/U, then N, T, D and P, then E. Everything a file
  destination gets goes into a record buffer, written to `NAME.$$$` with a CRC kept for V.
- **Users.** Each entry's user is its G, else the one PIP started in. PIP sets the BDOS's current user before every
  call, since the BDOS finds a file's next extent in the current user, and puts the starting user back at the end.
- **^C:** PIP sets console mode bit 0 while it runs, as `CM_PAGE` does, so function 11 reports only ^C and function
  6 then reads it. In mode 0, 11 keeps a waiting key for function 1, and 6 would never see it. It puts the mode back
  at the end. A ^C typed while the CCP is still loading PIP waits for the CCP, as in CP/M.
- **Tests:** `test_oodos_utils_pip`, ten cases. They compare PIP's output line for line, and read what it leaves on
  M: back from M:'s directory and blocks: copies and joins, wildcards and their list, users, attributes, the
  read-only question, V, A, C, passwords both ways, the `*` mode, every device, every character option, H and I,
  a file past an extent, a full disk, a read-only `NAME.$$$`, and ^C. `test_oodos_card` runs PIP from the card on
  A: and saves the card for `interop.sh --pip`, where cpmtools reads the copies back with `cpmcp`. Both are seen
  to fail on a fault put in on purpose.
- **Found on the way:**
  - OODOS's console takes a typed LF as a CR, so `CON:`'s "CR and LF for a new line" gives CR CR.
  - The User's Guide lists K among the options that keep a transfer by records, and says nothing more about it.
    PIP takes K as leaving out the `COPYING -` list (ours).

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

### Leaving OODOS: WOZMON and APPLE2 ###

**Done (2026-11-02); built-ins since 2026-11-03.** Two commands of ours, for the machine OODOS runs on rather than
CP/M's: **WOZMON** clears the screen (as CLEAR, below) and leaves OODOS for the ROM BIOS's Woz Monitor, and
**APPLE2** restarts the machine in Apple II mode, the cartridge's IIe ROM started as a IIe starts at power-on. Both
call function 75 (above, "OODOS's own"): a program can't jump to the ROM itself (W3 is OODOS's block 62, and the
rules let a program call only the BDOS and the warm boot), and only OODOS may map the cartridge to look for a IIe
ROM. Neither comes back: the monitor starts afresh (a reset, or `C80CR` with the card in, boots OODOS again), and
only a reset ends Apple II mode. Without a IIe ROM, APPLE2 says "No IIe ROM in the cartridge (the emulator:
Settings, ROMs)", the return code `$FF00`, and OODOS goes on. They were `.COM`s in `oodos/utils` for a day; the
owner asked for them in the CCP, with CLEAR and CLS, so leaving OODOS works on any card, from any drive and user
(112 bytes in the CCP, APPLE2's message 59 of them). A program of the same name on a card runs only with a drive
before it (`A:WOZMON`), as for any built-in. Tested by `test_oodos_utils_rom`, with a stand-in IIe ROM the test makes (its
ID byte and a loop at reset), not Apple's.

### Clearing the screen: CLEAR and CLS ###

**Done (2026-11-03).** Two built-ins of ours, asked for by the owner, in the CCP beside DIR and the rest (as GO is
ours): no `.COM` on the card, so they work from any drive and user. **CLEAR** clears the screen: it writes `ESC E`,
so the text screen is cleared and a terminal on the serial line gets ANSI's clear (above, "The terminal"). **CLS**
clears it too, then is DIR with its tail: the built-in's listing, or, for an option list or more than one
specification, DIR.COM's, handed the tail as DIR hands it (`CLS [SYS]`, `CLS *.COM [SIZE]`). They were `.COM`s
for a day; the owner asked for built-ins, like DIRSYS's `DIRS`. The CCP grew 36 bytes. A program of the same
name, such as the `CLS.COM` many CP/M disks have, runs only with a drive before it (`A:CLS`): a built-in's name
alone is the built-in, a drive and the name the program (above, "The CCP"). Tested by `test_oodos_ccp`; WOZMON's
clear by `test_oodos_utils_rom`.

### The boot status ###

**Done (2026-11-03),** asked for by the owner, who chose the layout: three lines under the cold boot's banner,
before the first prompt, on the serial line and the screen alike (`oodos/sys/cold.s`, STATUS):

```
OODOS 0.2  2026-11-03 12:00:00
BIOS 0.10   47K TPA
A: 8M   B: 8M   M: 128K
Cards: video, sound, SD, Disk II, USB keyboard, DMA, panel
```

- **BIOS:** the word after `BACKPLANE BIOS ` in the ROM BIOS's own greeting, found in its copy in RAM block 63
  (where the ROM copies itself at reset) through W1, then W1 the BDOS's again before anything is printed (the
  screen's output takes W1). So the ROM says which it is and no BIOS changed for it; `?` when there's no greeting
  there (a ROM of the owner's without one). The owner chose this over a version at a fixed address, a BIOS
  release.
- **TPA:** what a program gets, `$0300` to the BDOS's entry (`BDOS_ENTRY`, `$BFFD`), in whole KB: 47K.
- **Drives:** each the BIOS's drive table has, A-P, its size from its DPB, (DSM + 1) blocks of `128 << BSH`
  bytes, in MB when it is whole MB (8M, 32M), else in KB (M:'s 128K).
- **Cards:** each whose ID register reads its card's ID, in that order: the video card, the sound card, the SD
  card, the Disk II controller (its native-mode ID at `$C67F`, added 2026-11-04), the USB keyboard, the DMA
  controller, the front panel; `none` if none. The serial card has no ID
  register, and the console runs on it.

587 bytes of cold boot (574, and 13 for the Disk II from 2026-11-04), in block 62's code window. Tested by
`test_oodos_sys`: the banner and the status on both outputs, the BIOS's version as `bios.rom`'s greeting has it, a
machine with only the SD card and the DMA controller and drives of 8 MB and 32 MB, and a block 63 with no greeting.

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
  below `$03`, an image and BSS that don't fit below the top of the TPA (`$0204`), and an overlay with a zero page
  of its own, or an RSX whose zero page doesn't fit in what is left of `$F0–$FF` ([The RSXs, phase
  5](#the-rsxs-phase-5)). A program's zero page base and size aren't checked (ours, as built: com.cfg links its
  segment in `$00–$EF`, which ld65 enforces). Then it reads **exactly the image's size bytes** of image: the first record's 112 bytes after the
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

## The RSXs, phase 5 ##

**Proposed (phase 5, 2026-11-04; revised the same day after a review against the manuals)** (the Programmer's
Guide, 1.6.2–1.6.5, 2.2.1, 3's functions 49, 59 and 60, 4.4; the User's Guide's GENCOM, GET, PUT and SUBMIT, 3.4,
and Appendix A's messages; where they are silent the choice is ours, and says so). Nothing of it is built. The
design it fills in is [RSXs and programs](#rsxs-and-programs), [The program header](#the-program-header) and
[The .PRL file](#the-prl-file), decided 2026-10-26. Each stage reads its part of the Guides again at its start, as
4a and 4b did, and says here what changed.

**What there is now (main, de9866a)**

- **Function 47** is built (3b2): the command at `$0280` to the CCP, `$FF` for the program's drive and user, the
  return code kept, the CCP showing and running the command at the warm boot (`CHAINF`, `CCP_CHAINDU`). Phase 5
  adds only what RSXs and SUBMIT change around it (below).
- **Functions 59 and 60** have no handler: the BDOS's dispatch returns A = X = `$FF` for both (`FIDX`).
- **The CCP's loader** (`LOADCOM`, `CHECKHDR` in `oodos/sys/ccp.s`) loads a `.COM` at `$0300` and refuses any
  header flag: no `.PRL`, no RSX, and the attached RSX fields (12, 14) are not looked at, so a GENCOM'd `.COM`
  would run today without its RSXs. Its size check is against the constant `$BFFD`, not `$0204`. An image of size
  0 jumps to whatever `$0300` holds.
- **The warm boot** (common.s's `WARM`, then the CCP) writes `$0203`'s `JMP $BFFD` again each time, so nothing can
  stay linked on `$0203` across a program; function 0 and a program's `RTS` go to the CCP without passing `$0203`.
- **The tools:** `mkprl.py` makes `.PRL`s and loads them as OODOS will, RSXs (`--rsx --zp`) and overlays
  (`--overlay`) among them, and refuses an RSX's zero page over 16; `oodos.inc` has the `RSX_*` prefix offsets and
  the `OH_*` header; `check_tools.py` tests both. There is no link config for an RSX, no prefix macro and no GENCOM.
- **Room:** block 62's code window holds cold boot and the CCP, 5,509 bytes, and their BSS, 984: 6,493 of 12,288.
  Common memory 1,164 of 2,048. Block 60 (the BDOS) 11,632 of 16,384. The system's zero page 232 of 240.

**The loader (ours: one, resident)**

- **CP/M 3's CCP is a transient** and loads programs through the LOADER, an RSX of its own at the chain's end;
  OODOS's CCP is resident in block 62, so the loader is too (ours: no LOADER RSX, decided 2026-11-04). It is CCP
  code, in block 62's code window, and runs in the program's context (its windows and stack, W3 on block 62),
  calling the BDOS at `$BFFD`, the chain's end, so no RSX sees its reads. **Function 59 is its door,** and the CCP
  loads every program through it as well, so there is one loader, not two. The CCP's `LOADCOM` and `CHECKHDR`
  become the loader's.
- **How 59 reaches it:** `$BFFD`'s `JMP` goes to a few bytes in common memory ahead of the stub: Y = 59 puts W3 on
  block 62 (the caller's block kept on its stack), calls the loader, and puts it back; any other function goes on to
  the stub. So an RSX above can still take 59 first, and the BDOS never runs the loader inside itself. About 4
  cycles more a BDOS call. Since the loader is always there, 59 never gives CP/M 3's "no loader" answer (A = `$FF`,
  H = 0), and any program may call it: CP/M 3 lets only a program with an RSX header call 59, to keep its LOADER
  resident; GENCOM's `[LOADER]` is accepted and does nothing (ours).
- **What 59 does** (the Guide's: A/X the FCB, opened by the caller; the load address in its R0–R1, low then high):
  - A `.PRL` loads at any page start from `$0300`, relocated as [The .PRL file](#the-prl-file) says. A `.COM` loads
    only at `$0300`, its link page: **ours,** CP/M 3 loads an absolute file anywhere from `0100H`, but OODOS's
    absolute file names its one page, and `.PRL` is how a file loads elsewhere (decided 2026-11-04). An `.RSX` is
    refused (RSXs come attached, below).
  - The image and BSS must end at or below the lowest RSX's first byte (`$0204`'s word − 6), or `$BFFD` with none:
    the Guide's, a load never overlays an RSX. Ours: a `.PRL` needs room for its image and BSS only, not for its
    header and bitmaps too as CP/M 3's does, since OODOS's loader reads the bitmaps through its own buffer.
  - An overlay has no zero page of its own (its header's size 0), as decided.
  - **The caller's state is put back:** its DMA address and DMA segment (51), error mode, multi-sector count and
    current user are saved, the loader's own set (its record buffer in block 62, `$FFFF`, returned errors, 1),
    and all put back before 59 returns (ours: CP/M 3's LOADER runs in the caller's BDOS state too, but the Guide
    doesn't say what it leaves).
  - **Returns** (the Guide's): A = 0 done; A = `$FE` an address the loader won't take or no room for the file; else
    function 20's: A = 1 the file ended inside its image, A = `$FF` and X the physical error. **Ours:** a header the
    loader refuses (magic, flags, page) and a zero page that doesn't fit are `$FE` too, with X saying which (0 the
    address or the room, 1 the header, 2 zero page). A refusal leaves nothing past the image changed.
  - **Removal:** the Guide has the LOADER take out the RSXs whose remove flag is set at each call of 59 (4.4.1),
    and the CCP calls it at each warm boot. So every 59 does that first (but the one case below); **59 with A/X = 0**
    (ours) loads nothing and only does that, so a program can drop an RSX it is done with at once.
- **Attached RSXs** (header bytes 12 and 14, the table in [The program header](#the-program-header)): loaded before
  the program's image, in the table's order, each below the one before; the last loaded is the first `$0203`
  reaches, as the Guide has the most recently loaded first. An RSX whose non-banked flag (offset 15) is `$FF` is
  passed over: OODOS is banked (the Guide's, 4.4.1). **An RSX of a name already in the chain is loaded again,
  below it** (the Guide's 1.6.5: a SUBMIT file may hold more SUBMITs and GETs, each with its RSX; decided
  2026-11-04: a GET inside a GET file stacks too), and the newer one serves first. If any refusal stops the load,
  the RSXs it had linked are taken out again and `$0204` is as it was (ours).
- **SCB entries** (GENCOM's `[SCB=(offset,value)]`, decided 2026-11-04): an entry of the attached RSX table whose
  flags byte has bit 1 set (ours) is no RSX but an SCB byte, its offset and value in the entry's first two bytes;
  the loader sets each, by 49, after the CCP's warm boot settings (console mode 0 and the rest) and before the
  program starts, so a program can be given, say, console mode bits 8 and 9 for GET (the Guide's 2.2.1). The
  offset must be `$00`–`$63` (OODOS's SCB ends at `$63`; the Guide's GENCOM allows to `$64`, ours), and the
  BDOS's own rules for 49 hold (a word at `$63` its one byte).
- **An RSX-only `.COM`** (GENCOM's `[NULL]`: image size 0, RSXs attached): the RSXs are loaded and the CCP goes back
  to its prompt, the return code 0. **The warm boot right after it removes nothing** (the Guide's 1.6.4), so an
  RSX loaded so with its remove flag `$FF` stays for the next program, which can call it, and goes at the warm boot
  after that program. A `.COM` of size 0 with no RSXs is `NO PROGRAM` (ours).

**Where RSXs go, and the chain**

- **In memory:** each RSX's image and BSS at the highest page start where they end at or below the bottom of what
  is above: the lowest RSX's first byte, or `$BFFD` for the first. So a first RSX of 253 bytes or less sits at
  `$BF00` and ends by `$BFFC`; RSXs never move once loaded.
- **The chain:** `$0203`'s `JMP` goes to the lowest RSX's offset 6 (its prefix's first `JMP`), so `$0204` is that
  RSX's first byte + 6, as the Guide has location 6 point just past the serial number with or without RSXs; its
  offset 9 `JMP`s to the next RSX's offset 6, and the highest RSX's to `$BFFD`. An RSX makes its own BDOS calls,
  and passes on what it doesn't take, through its offset 9, never `$0203`, which would reach it again (the
  Guide's). The TPA's top + 1 is the lowest RSX's first byte (the Guide's 1.6.2, step 5), so a program loaded never
  covers an RSX's serial number; a program that sizes itself from `$0204` can still reach those six bytes, as in
  CP/M 3.
- **The prefix** ([The program header](#the-program-header)), the 27 bytes the Guide's 4.4.1 gives, which the RSX
  sets three of: the **remove flag** (14), the **non-banked flag** (15) and the **name** (16). The loader writes the
  rest: the serial number (107's six bytes), offset 9's operand, **offset 12** and the **loader flag** (24), 0 in
  every RSX it loads, as the Guide's LOADER does (only CP/M 3's LOADER has `$FF` there, and OODOS has none). Offset
  12 is the address of the preceding entry: `$0203`, the base page's `JMP`, for the first RSX in the chain (the
  Guide's location 5, moved up as the base page is: **decided here,** not `$0204`, the operand), and for the others
  the offset 9 `JMP` of the RSX before it in the chain (ours: the Guide says only "the preceding RSX"; that `JMP`
  is what `$0203` is for the first). **The remove flag is the RSX's own,** never set or cleared by the loader: an
  RSX that serves one program has `$FF` in its file, so the warm boot after that program removes it; one that must
  stay has 0 and sets `$FF` itself when it is done (as GET does at its file's end). `RSX_PREFIX name, entry,
  remove` in `oodos.inc` writes it, the remove flag an argument.
- **The loader's table (ours):** block 62 keeps a list of what it loaded (each RSX's first page, its pages, its
  zero page slice, its name), up to 15 RSXs, as many as one GENCOM header can attach, about 200 bytes. The warm
  boot builds `$0203`, `$0204` and every offset 9 and 12 again from it, so a program that changed `$0203` (the TPA
  rule forbids it, but CP/M programs have done it) doesn't break the chain for the next. A sixteenth RSX is refused
  (`$FE`, ours).
- **Zero page:** each RSX gets the lowest free run of `$F0–$FF` of its header's size, relocated there by its zero
  page bitmap; one that doesn't fit is refused (`$FE`, X = 2), not overlapped, as decided. A removed RSX's slice is
  free again. OODOS's own RSXs (GET, PUT, SUBMIT) use none (their pointers in their own code, as `cmd.inc`'s), so
  16 bytes stay for others, and stacked GETs and SUBMITs never run out of it.
- **Removal:** at each warm boot (but the one after an RSX-only `.COM`) and each 59, every RSX whose remove flag is
  `$FF` is taken out of the chain and the table, its zero page freed. Memory comes back only from the bottom: when
  the lowest goes, the TPA's top rises to the next one, or `$BFFD`; a removed RSX above a lower one that stays
  keeps its memory until that one goes too (the Guide's 1.6.4). Cold boot starts with none.
- **Every end of a program reaches the RSXs (ours, decided 2026-11-04):** the warm boot, however it comes (a
  program's `RTS` or `JMP $0200`, function 0, a ^C, the BDOS ending a program for an error, 47), calls `$0203` with
  function 0 once, in the program's windows and a fresh stack, before the removal and the CCP; the BDOS's function
  0, reached so, goes straight on to the CCP (a flag in block 62). So PUT can close its file and GET stop when the
  program they serve ends.
- **What the CCP is doing (ours):** one of the SCB's bytes the Guide reserves for the system (`$12`–`$19`, beside
  `$17` and `$18`, which OODOS already uses; never `$06`–`$09`, the program's own) holds two things for redirecting
  RSXs: bit 7 set while the CCP runs (its prompt, its built-ins) and clear while a program runs, and in bits 0–6 a
  count the CCP steps as it starts each command, a built-in or a program, from the prompt, a `!` line, 47, SUBMIT or
  GET. GET and PUT without `[SYSTEM]` serve "the next command" (the User's Guide's, DIR among them in its 3.4) by
  that count: from the step after they were set up to the next step, or the program's function 0.

**Function 60** (call an RSX, the Guide's): A/X the RSX parameter block: a function byte, a count of words, the
words. The BDOS, reached because no RSX took it, returns A = X = `$FF` (as now). An RSX takes the calls whose
function byte is its own and passes the rest on. Function bytes 0–127 are for programs' RSXs, 128–255 the system's
(the Guide's): **OODOS's own RSXs use 128–255** (each listed in its source and in bdos-60's entry), and that is how
GET.COM, PUT.COM and SUBMIT.COM give their RSX its file.

**Function 47** (the rest of it): a chained command can name a program with RSXs attached, a `.SUB` (5c), or an
RSX-only `.COM`, and each loads as typed; with SUBMIT running, the chained command runs first and the SUBMIT file
goes on after it, which falls out of the CCP's order: 47's command, then the next line read. No BDOS change.

**Writing an RSX** (ours, for the Programmer's Guide):

- `oodos/cfg/rsx.cfg`: `prl.cfg` with the RSX flag (`$03`) and a `RSXHDR` segment first in the image;
  `RSX_PREFIX name, entry, remove` in `oodos.inc` writes the 27 bytes. `make -C oodos` builds `NAME.RSX` by
  mkprl.py's three links, as a `.PRL`.
- **The file type is `.RSX`** (the Guide's; decided 2026-11-04), the `.PRL` format with flag bit 1; mkprl.py
  checks the prefix (offset 6 a `JMP` into the image, offset 9 a `JMP`, the remove and non-banked flags 0 or `$FF`,
  the name printable and space padded) and refuses an RSX without it.
- The 65C816 rules hold, rule 4 read for an RSX as: the BDOS only through its own offset 9, and `$0200`. An RSX
  runs in the caller's context: the caller's stack (a few bytes of it), zero page only its slice. Its own BDOS calls
  (a file of its own) must save and put back what the caller had (the DMA address and segment, the user, the error
  mode, the multi-sector count): `oodos/include/rsx.inc` does it once for all of OODOS's RSXs.

**GENCOM** (5b; `oodos/utils/gencom.s`, a utility as 4b's; and `oodos/tools/gencom.py` for the build, which does
the same to the byte, ours). The User's Guide's forms:

- `GENCOM name rsx {rsx...} {[LOADER | SCB=(offset,value),...]}`: `NAME.COM` with `RSX.RSX` attached, in the order
  given; on a `.COM` GENCOM made already, an RSX of a name attached there replaces it
  (`Duplicate RSX in header. Replacing old by new`) and a new one is added.
- `GENCOM name [LOADER | SCB=(offset,value),...]`: a header with no RSXs, only its SCB entries (or nothing, for
  `LOADER`).
- `GENCOM rsx {rsx...} [NULL {SCB=(offset,value),...}]`: an RSX-only `.COM`, named after the first RSX.
- `GENCOM name`: what GENCOM added taken off, the `.COM` as it was.
- **The header** has room for 15 entries, the RSXs and the SCB pairs together (the Guide's); OODOS's table takes
  15 entries of 16 bytes, which is two records. The loader's 15 RSXs (above) mean any one file's RSXs load into an
  empty chain; with RSXs already there, the sixteenth is refused.
- **Messages** (Appendix A's, in OODOS's layout): `First submitted file must be a COM file`, `COM file found and NULL
  option`, `Duplicate input RSX` (the same RSX named twice: one used), `Invalid RSX type`, wildcards refused,
  `Total file size exceeds 64K`, `No more space in the header for RSXs or SCB initialization`, `Unrecognized
  option`, `No modifier for this option`, the SCB option's missing parenthesis, missing value and `Invalid SCB
  offset`, `Read only`, `Password Error`, `Cannot delete file`, a file not found.
- It writes `NAME.$$$` and renames it, as PIP does. GET.COM, PUT.COM and SUBMIT.COM are made on the build by
  gencom.py, and GENCOM.COM's output compared with gencom.py's byte for byte in a test.

**GET** (5b): console input from a file. GET.COM is a program with OODOS's input RSX attached (remove flag 0: it
stays until its file is done); the program parses the command, opens the file, and gives the RSX the file by 60.

- `GET {CONSOLE INPUT FROM} FILE filespec {[ECHO | NO ECHO]}`: the next command's console input (functions 1, 6's
  input, 10, and 11's and 6's status) comes from the file, while it runs; the CCP's prompt still reads the keyboard.
  When the file runs out first, the program reads the keyboard; when the program ends first, its input is the
  keyboard's again. Either way the RSX then sets its remove flag. `[ECHO]`, the default: what is read is shown as
  if typed; `[NO ECHO]`: not (the program's output and the prompts still are).
- `GET {CONSOLE INPUT FROM} FILE filespec [SYSTEM]`: from now, the CCP's command lines and every program's input
  from the file, until its end or a `GET CONSOLE` line in it. `[SYSTEM]` and `[ECHO]`/`[NO ECHO]` are one or the
  other (the Guide's form).
- `GET {CONSOLE INPUT FROM} CONSOLE`: the keyboard again now; in a GET file it ends that file's input.
- **A GET in a GET file** loads a second input RSX below the first (stacked, decided 2026-11-04): the newer file
  serves until it ends, then the older goes on.
- **Status** (11, and 6's `$FE`) while a file serves input goes by console mode bits 8 and 9 (the Guide's 2.2.1):
  0 and 0 conditional (false, but true for a status call straight after another, so a program waiting on status
  gets its character and one only looking for a stop key doesn't stop); bit 9 alone always false; bit 8 alone
  always true; both: no redirection for that program at all. GENCOM's SCB entries can set them for a program.
- **Messages:** the sign-on `Getting console input from file: NAME.TYP` (the User's Guide's 3.4); `Invalid command`,
  `Invalid delimiter`, `Invalid option`, `Invalid option modifier`, `End of line expected`, `Insufficient Memory`,
  `Auxiliary device redirection not implemented` (AUX isn't redirected), and a file not found, before anything
  changes.

**PUT** (5b): console or printer output to a file. PUT.COM, with OODOS's output RSX attached. CONSOLE or PRINTER
is always named:

- `PUT CONSOLE {OUTPUT TO} FILE filespec {[options]}`: the next command's console output (functions 2, 9, 111 and
  6's output), a built-in's too (the User's Guide's 3.4 puts DIR's listing in a file so), goes to the file; the
  file is closed when that command ends.
- `PUT PRINTER {OUTPUT TO} FILE filespec {[options]}`: the same for LST: (5 and 112).
- `PUT CONSOLE {OUTPUT TO} CONSOLE`, `PUT PRINTER {OUTPUT TO} PRINTER`: back to the device now, the file closed.
- **Options,** which combine (`[ECHO,SYSTEM]`): `[ECHO]` the output to the device as well, the default for the
  console; `[NO ECHO]` to the file only, the default for the printer; `[FILTER]` control characters written to the
  file as `^` and a character (ESC as `^[`); `[NO FILTER]`, the default, as they are; `[SYSTEM]` everything from now,
  the CCP's output too, until the `PUT ... TO CONSOLE` (or `PRINTER`) form.
- **Under `[NO ECHO]`** a program that asks for console input gets the prompt `PUT>` first, so the user knows it
  waits (the Guide's).
- **The file:** one there already is asked about, `File already exists Delete it? (Y/N)`: Y deletes it, N ends PUT;
  a read-only one is an error. `Do you want another file? (Y/N)` offers output to another file (when PUT asks it
  is settled at 5b's start: the Guide gives the question, not the moment). Ours: written as `NAME.$$$` and renamed
  at the close, so a reset leaves the old file, not half a new one. `PUT ERROR: FILE ERASED` when the file can't be
  closed because it was deleted under PUT.
- **Messages:** the sign-on `Putting console output to file: NAME.TYP` (3.4; the printer's alike), and GET's
  command errors above.
- **Not caught (ours, as an RSX above the BDOS can't see them):** what the BDOS writes itself: function 10's echo,
  its error messages (`OODOS Error On ...`) and ^P's printer echo.

**Decided (5b, 2026-11-04, the owner, after 5b's plan check against the manuals and 5a as built):**

1. **PUT asks `Do you want another file? (Y/N)`** when a PUT of the same kind (console, or printer) already sends
   output to a file: Y keeps the old file and adds the new (both get the output), N closes the old and the new
   replaces it (ours: the Guide gives the question, not the moment).
2. **A command runs: bit 6 of the SCB's `$19`** (ours), set at the count's step, cleared when the command ends,
   before the next prompt (a program's end clears it at the warm boot). GET's and PUT's RSXs act on the CCP's 49
   writes to `$19` as they pass down the chain: the next command is the one whose start they see after their own,
   and it ends when bit 6 clears. PUT's file keeps the CCP's owed CR LF at its start (ours, said in util-put).
3. **`Insufficient Memory`** (ours) is what the CCP says whenever the loader refuses a program for lack of room
   (59's X = 0: the table full, or the TPA), for any program, in place of `BAD PROGRAM HEADER`.
4. **The command words in full,** as the Guide writes them (`CONSOLE`, `PRINTER`, `INPUT`, `OUTPUT`, `FROM`, `TO`,
   `FILE`; ours: the Guide doesn't say whether shorter forms are taken); options by any unambiguous prefix, as 4b's
   utilities; `AUXILIARY` only to say `Auxiliary device redirection not implemented`. `Invalid command`: words that
   make no form; `Invalid delimiter`: a misplaced `[`, `]` or `,`; `Invalid option`: an unknown option; `Invalid
   option modifier`: an option of the wrong kind (FILTER to GET, ECHO with SYSTEM); `End of line expected`: text after
   a whole command.
5. **GET's file as input:** functions 10 and 1 take its bytes, an LF after a CR dropped, to a ^Z or its end; with
   ECHO, 10's line is shown and ended with CR as typed, 1 echoes as 1 does; 6's input is raw, never echoed; under
   `[SYSTEM]` a `GET CONSOLE` line ends the file.
6. **PUT's `[FILTER]`** keeps CR and LF; a tab and every other control character become `^` and a letter.
7. **PUT's file gets what the program sent:** a tab as a tab, 9's string to its delimiter (the BDOS's own tab
   expansion comes after the RSX). Under `[NO ECHO]` the BDOS never sees the output, so ^S, ^Q and ^C aren't looked
   at while it writes (said in util-put).
8. **GENCOM's SCB entries** as `[SCB=(o,v),(o,v)]` or `[SCB=(o,v),SCB=(o,v)]`, the numbers in hex; `GENCOM name
   [LOADER]` alone leaves the file as it was, and is accepted.

Also (5b's plan check, accepted): GET's and PUT's RSXs have the remove flag `$FF` in their files, so they go after
GET.COM's or PUT.COM's own run unless the program's 60 call that sets the redirection up clears it to 0 (the RSX
sets it again when done); so a GET with a syntax error leaves no idle RSX, and `GET CONSOLE` is the newest RSX
passing its 60 call on to the active one. OODOS's RSX function bytes are from 128-255, listed in `rsx.inc` and
bdos-60. PUT's file is `NAME.$$$` from a one-record buffer in the RSX, renamed at the close. `interop.sh --rsx`:
cpmtools reads GENCOM's `.COM` back (equal to gencom.py's) and PUT's file, sees no `$$$` left, and `fsck.cpm`
passes.

**Proposed (5b, as built, 2026-11-04)** ([`oodos/rsx/`](../oodos/rsx/), `oodos/utils/get.s`, `put.s`, `gencom.s`,
[`oodos/include/rsx.inc`](../oodos/include/rsx.inc), `redir.inc`; the plan and the decisions above are what was
built, and these are the details):

- **The RSXs:** `rsx/getrsx.s` (1,131 bytes of image: five pages below the lowest RSX while it serves) and
  `rsx/putrsx.s` (1,360: six), each its file's 128-byte record buffer and FCB in its image (an RSX's BSS isn't
  cleared, so its variables are in its file). The Makefile builds `rsx/*.s` as `.RSX`s and attaches `getrsx` to
  GET.COM and `putrsx` to PUT.COM by gencom.py (`ATTACHED`); `make utils` builds the RSXs too, and make_card.sh
  copies `rsx/`. GET.COM is 6,272 bytes, PUT.COM 6,912, GENCOM.COM 6,855 (each with `cmd.inc` and `util.inc`).
- **`rsx.inc`:** `RX_SAVE` and `RX_RESTORE` keep the caller's DMA address and segment, user, error mode and
  multi-sector count across an RSX's own file call, which is made by its offset 9; `RX_PB49` reads a 49 call's
  block for a write to `$19`. The function bytes (`RXF_*`) and `$19`'s bits are in `oodos.inc`.
- **The CCP's `$19` writes go through `$0203`** (`CCPSET`; 5a's went to `$BFFD`, past the chain, so no RSX saw
  them): found when GET first ran. The count is bits 0–5 now, beside bit 6.
- **PUT's file at a command's end:** with `[SYSTEM]` it is closed (16) at each, so the warm boot's 98 doesn't free
  the blocks it has written, and goes on being written after; at the end of what it serves, its last record
  `^Z`-padded, closed, NAME.$$$ looked for (a close of a file nothing was written to may not look, so its absence is
  seen here: `PUT ERROR: FILE ERASED.`), NAME.TYP deleted and NAME.$$$ renamed.
- **Under `[NO ECHO]`,** `PUT>` is shown when a call for input (1, 10, 6's `$FF` and `$FD`) follows output (mine:
  once per run of input calls, not before each character).
- **Both files get the output** (another file, Y): the newer PUT passes every call on after writing it; the older
  one's options then decide the console echo (mine).
- **GENCOM.COM** reads the old file, the RSX files and writes NAME.$$$ a record at a time by 33 and 21, so the
  64 KB file needs no memory but its 16 specifications, 15 entries and a table of two records; it copies the old
  file's records as they are (the image's last record's padding too), so `test_oodos_utils_rsx` compares it with
  gencom.py's on files padded as gencom.py pads. With NULL the first file is an RSX (`NAME.RSX` with no type),
  and only a first file typed `.COM` is `COM file found and NULL option`. A file not found is `NAME.TYP No File`
  (ours).
- **After 5b's review (2026-11-04, the owner's triage):** a PUT armed for the next command doesn't take a PUT
  command as that command: PUT.COM's first call, 60 `RXF_PUTCMD` (`$93`), arms again one its start made active,
  what it took (the owed LF) dropped. GET's echo goes down the chain by its offset 9, so a PUT older than the GET
  (given before it) writes it to its file and a newer one doesn't (said in util-get and util-put). GET's 10 with
  A/X = 0 keeps the DMA buffer's characters as typed first, as the BDOS's 10. FILTER writes DEL as `^?`. GENCOM
  deletes the old `.COM` in error mode `$FF`, so `Cannot delete file` is its only message, and checks an attached
  RSX's header (`Invalid RSX type`) before trusting its length. Tests wait for what a program asks for (the rig's
  `calls`: the BDOS entered for a function) or prints, not a fixed time; the ^C at the prompt's drive reset is
  tested while GET waits with its file open and while PUT's file is being written.
- **`Insufficient Memory`** in the CCP: 59's X = 0 and GO's image too big (test_oodos_sys's two GO cases and
  test_oodos_rsx's four changed with decision 3).
- **Tests:** `test_oodos_utils_rsx`, 17 cases on IOTEST (`oodos/sys/test/iotest.s`): GET's forms and options, its
  file to a ^Z, the four status modes, SYSTEM and GET CONSOLE, a GET in a GET file, its refusals and no idle RSX
  left; PUT's built-in and program output, NO ECHO's `PUT>`, FILTER, the printer, SYSTEM across programs, both
  questions, a file erased, its refusals; the program's state across both RSXs' file calls; GENCOM.COM's every
  form against gencom.py's bytes (`iota`, `iotab`, `iots`, `iotn`, `iotx`, made by the sys Makefile) and every
  message. `test_oodos_rsx` checks `$19`'s bit 6 through a program and DIR. `test_oodos_card` runs PIP, GENCOM, PUT
  and TYPE on the card's A: and saves it (`OODOS_RSX_OUT`) for `interop.sh --rsx`: H2.COM equal to gencom.py's,
  PUTOUT.TXT's text, no NAME.$$$ (PASS 7). PIP's M: reader moved to `oodos_rig.h`.

**SUBMIT** (5c): a file of commands. SUBMIT.COM, with the input RSX attached (the same one as GET's, ours: a
SUBMIT file is console input for the CCP).

- `SUBMIT`, `SUBMIT filespec`, `SUBMIT filespec arg ...`; the type `.SUB` if none is given. `SUBMIT` alone asks
  `Enter File to Submit: ` for the rest. Not found: `No 'SUB' file found`.
- **Parameters:** `$1` to `$9` are the arguments; one not given is nothing, arguments past the file's last are
  ignored. `$0` is valid too (the Guide allows `$0`–`$9` and says no more: ours, decided 2026-11-04, the file's name as given);
  any other character after `$` is `Parameter Error`. `$$` is `$`; `^` and a letter is that control character, `^^`
  a `^`. A line holds at most 128 characters. A byte `$FF` in the file is `Invalid ASCII character`. Errors are
  shown `Error on line nnnnn:` and the message, nothing run.
- **The lines:** SUBMIT.COM writes the expanded lines to a temporary file of type `$$$` on the temporary drive (the
  SCB's `$50`; the Guide's), and the RSX gives them to the CCP's function 10 one at a time, each shown after the
  prompt as if typed (the RSX echoes it, so `cmd.inc`'s owed LF still holds). Its errors: no directory space on
  the temporary drive, `Disk write error`, `Random Read` (the temporary file unreadable).
- **Program input:** a line starting with `<` is input for the program running, not a command: its function 10
  (and 1) take it; `<` alone is a CR. A program that wants more than the `<` lines give reads the keyboard, and the
  next command line waits until it ends; `<` lines left when it ends are skipped with the warning `WARNING: Program
  input ignored`. `;` and `:` lines are the CCP's comments and conditionals, as now.
- **SUBMIT inside SUBMIT:** CP/M 3 stacks a second SUBMIT RSX with a temporary file of its own, and the inner file's
  lines run first, then the rest of the outer. **Ours:** the same, stacked as GET is; too many levels is
  `Insufficient Memory` (the Guide's), at the loader's 15 RSXs or a full temporary drive. (The first plan's one
  temporary file rewritten for each level is dropped: stacking is what the Guide describes, and GET in a SUBMIT
  file stacks the same way.)
- **The end:** the last line read, the temporary file is deleted and the RSX sets its remove flag. A program ended
  by ^C (the return code `$FFFE`) ends every SUBMIT level, their temporary files deleted (ours, decided 2026-11-04,
  as a `!` line's rest is dropped). A temporary file a reset left is deleted by the next SUBMIT, never resumed (ours).
- **The CCP:** `NAME.SUB` typed, or found by the search order, is run as `SUBMIT NAME.SUB` and the tail (the
  Guide's 1.6.2), replacing `NAME.SUB: no SUBMIT yet`; SUBMIT.COM is then found by the CCP's usual search (the drive
  chain, user 0's SYS file), and not found is `SUBMIT?` as for any command. **The search order** (SETDEF's
  `[ORDER=(COM)]`, `(COM,SUB)`, `(SUB,COM)`, `(SUB)`): the first type is tried on every drive of the chain, each
  with user 0's SYS file, before the second type on any (the Guide's). It is two bits of a system-reserved SCB
  byte, `$12`–`$19` (ours; which, at 5c's start), and SETDEF takes `SUB` instead of `ORDER=(SUB): no SUBMIT yet`.
  **`PROFILE.SUB`** on the default drive at cold boot (user 0, since cold boot starts there) is run as `SUBMIT
  PROFILE` before the first command is read (the Guide's 1.6.2; decided 2026-11-04). The demo card gets none, so it
  boots as now.

**Decided (5c, 2026-11-04, the owner, after 5c's plan check against the manuals and 5a and 5b as built):**

1. **SUBMIT's own RSX,** `rsx/subrsx.s`, not GET's; GET's file reading (the record buffer, an LF after CR dropped, the
   ^Z) moves into an include both use, not copied.
2. **The prompt** is a function 10 call with `$19` bit 7 set and bit 6 clear, and gets the next command line; a
   built-in's question (bit 6 set: USER's, ERA's) and a program's input take `<` lines.
3. **The temporary files** are `SUBnn.$$$`, the first free from 00, in user 0 on the temporary drive (the SCB's `$50`;
   ours). Stale `SUB??.$$$` there are deleted by SUBMIT.COM only when no SUBMIT RSX is active (asked by 60).
4. **A ^C's end:** at the CCP's next prompt read each SUBMIT RSX reads the return code (108); `$FFFE` ends it, its
   file deleted, its remove flag set, and the prompt reads the keyboard.
5. **`^`** before a letter or `@ [ \ ] ^ _` is that control character (`^^` stays `^`); before anything else the two
   characters are kept. An expanded line over 128 characters is `Error on line nnnnn: Line too long`, and nothing
   runs (ours).
6. **Command lines** are shown after the prompt as if typed; `<` lines are shown as typed too.
7. **`WARNING: Program input ignored`** once for each run of `<` lines skipped, just before the next command line is
   shown.
8. **The search order** is bits 0–1 of the SCB's `$18` (0 COM, 1 COM then SUB, 2 SUB then COM, 3 SUB; ours); SETDEF
   shows `Search Order - ...` (corrected 2026-11-05 by the owner: SETDEF only; SHOW has no such display, in OODOS or
   CP/M 3).
9. **`PROFILE.SUB`** at the first prompt, from the default drive, user 0: shown as `SUBMIT PROFILE` as if typed, then
   run; without SUBMIT.COM, `SUBMIT?` and on. A reset is a cold boot, so it runs then too.
10. **With SETDEF's DISPLAY,** both lines: the `.SUB` file's and SUBMIT.COM's own.
11. **Status and single characters** while `<` lines serve a program, as GET's.
12. **The `.SUB` file's lines** as GET reads its file.

**Proposed (5c, as built, 2026-11-05)** ([`oodos/rsx/subrsx.s`](../oodos/rsx/subrsx.s), `oodos/utils/submit.s`,
[`oodos/include/rxfile.inc`](../oodos/include/rxfile.inc), `oodos/sys/ccp.s`, `oodos/utils/setdef.s`; the
decisions above are what was built, and these are the details):

- **SUBMIT's RSX** is its own, `rsx/subrsx.s` (1,588 bytes of image after the reviews' fixes: seven pages below the lowest RSX
  while it serves), with GET's reading of a file moved to `rxfile.inc`, which both include (GET's RSX the same size as
  before). It holds a line of 130 (SUBMIT.COM's 128 and a `<`). SUBMIT.COM is 7,296 bytes with it attached; the
  Makefile has its own rule for it, as `ATTACHED`'s pattern names `getrsx` and `putrsx` only.
- **The CCP** grew 416 bytes (`$CB6F-$DF0E`, of the window to `$F7FF`): the search order's table (`ORDERS`, two
  types for each of `$18`'s four values), a `.SUB` found or typed turned into `SUBMIT d:NAME.SUB` and its tail
  (`RUNSUB`), and the PROFILE.SUB look at the first prompt (`COLDF`, set in the file, cleared once looked).
  OODOS.SYS is 25,472 bytes (main's 25,088). SETDEF.COM is 4,732 (4,631).
- **Decision 8 corrected** (the owner, 2026-11-05): SETDEF shows the search order; SHOW doesn't (nor does CP/M 3's).
- **The PROFILE.SUB look logs A: in at the first prompt,** so existing tests that counted logins or used the system
  block where A:'s allocation vector now is changed (the owner's answer (a)): `test_oodos_sys`'s DIRTEST LOGIN
  (`01 10`), BANKTEST's buffers (`$3000` and `$3800`, past the allocation vectors) and its two cases, and three
  cases that need A: logged out first (the busy card's two, the login count's), which begin with a ^C at the prompt.
  Others for the plan: `test_oodos_ccp`'s `.SUB` (`M:ARGS.SUB?`) and its CLS (page mode off: A:'s SYS files now
  fill more than a page); `test_oodos_utils_sys`'s SETDEF (`$18`'s other bits poked as `$04`; SUB orders taken).
- **The rig's `calls()`** returns too when the BDOS is already in the function asked for (`FUNC`, with the system's
  windows in): a program waiting at the keyboard makes no new entry.
- **After the 5c review** (the owner's triage, 2026-11-05):
  - fixed: a `.SUB` run by name with `;PW` passes the password to SUBMIT (`RUNSUB` adds it to the tail; `SUBPRE`
    25 bytes); the warning comes before the prompt (the RSX watches 49's writes to `$19` and skips a command's
    leftover `<` lines at its end, bit 6 clear, but not after a ^C, which the prompt ends); SUBMIT.COM's reopen
    checked (`Disk Read Error`, the temporary file deleted, no RSX left); a function 10 buffer of size 0 taken as 1
    by the RSX, as our 10 does (`bdossys.s`, `F_LINE`), now tested with IOTEST's `Z`; `$93` in the Programmer's
    Guide.
  - accepted: a `<` line longer than the program's buffer is cut to fit and the rest dropped, as function 10 does
    with a typed line (the User's Guide says so).
  - accepted: every cold boot logs A: in (64 directory reads, oodos-8m) before the first prompt, for the
    PROFILE.SUB look; CP/M 3's CCP looks for PROFILE.SUB at its first prompt the same way.
  - accepted: the look depends on the SCB as a cold boot leaves it. `cold.s` clears `$0000-$0CFF` (the SCB at
    `$0200` among it) and then sets `SCBINIT`'s bytes: the search chain `SCB_DRVCHAIN` (`$4C-$4F`) is `$4C` 0, the
    default drive, and `$4D` `$FF`, so SUBMIT PROFILE finds PROFILE.SUB on the drive the CCP looked on; the user
    (`$44`) is 0, the user looked in.
- **After the second review** (the owner's triage, 2026-11-05):
  - fixed: a `!` line's first program ended no longer skips the `<` lines meant for the second. Not by leaving
    bit 6 set at a program's end, as first asked: GET's and PUT's RSXs take that write as their command's end,
    and PUT closes its file there so the warm boot's 98 doesn't free its blocks. Instead the CCP's `PROMPT`
    writes `$19` once more with bit 6 clear (after clearing it, for a built-in), and SUBMIT's RSX skips only
    on a write of bit 6 clear while it is already clear, which only the way to the prompt makes. The prompt's
    own skip (`line`) is unreachable in effect now, kept so no command is given from a `<` line.
  - fixed: `$0` is the file's specification without its `;password`, typed or run by name.
  - fixed: the reopen test makes its fault in the FCB's extent byte, after SUBMIT.COM clears it, so the
    cleanup deletes the real `SUB00.$$$`, which the test checks.
  - accepted: a disk error at the reopen, in the default error mode, ends SUBMIT.COM with the BDOS's message
    before its own check, and leaves the temporary file behind (the next SUBMIT deletes it as stale).
  - accepted (third review): a `.SUB` that runs another can say the warning twice at one prompt, once from
    each SUBMIT's RSX (both see the way to the prompt, each skips its own `<` lines). Tested since: a built-in
    first on a `!` line and in its middle (`SAYPROMPT` from `NEXTCMD` clears bit 6, the next program gets its
    `<` line).
  - accepted: the warning's CR LF before it can make a blank line after a program whose output ended with
    one (cosmetic). The Programmer's Guide says bits 6 and 7 of `$19` are the CCP's.
- **`test_oodos_card`** makes A:PROFILE.SUB with PIP from CON: and boots again (saved for `interop.sh --submit`:
  PROFILE.SUB's text, no `SUBnn.$$$` left), and on a fresh card with SUBMIT.COM erased sees `SUBMIT?`.

**Stages (decided 2026-11-04: three branches, each merged on its own)**

| Stage | Branch | What | Size (estimates) |
|---|---|---|---|
| **5a** | `feat/oodos-rsx-loader` | `rsx.cfg`, `RSX_PREFIX`, mkprl.py's RSX checks, gencom.py; the resident loader with 59 (`.COM`, `.PRL` overlays, attached RSXs stacked, SCB entries, A/X = 0), the chain, the table, zero page slices, removal (and the RSX-only exception) and function 0 at the warm boot; the CCP loading through 59, its checks against the lowest RSX, RSX-only `.COM`s, `NO PROGRAM` for an empty one; 60's RSX parameter block; the CCP's SCB byte | loader 1.8–2.3 KB in block 62 (of 5.8 KB free), its table about 200 bytes; common memory +40–60 bytes; BDOS +0–50 |
| **5b** | `feat/oodos-get-put` | GENCOM.COM; `rsx.inc`; the input RSX and GET.COM; the output RSX and PUT.COM; the card gets them | GENCOM 2.5–3.5 KB; input RSX about 1.2 KB and GET.COM 1.5 KB; output RSX 1 KB and PUT.COM 2 KB |
| **5c** | `feat/oodos-submit` | SUBMIT.COM on the input RSX; the CCP's `.SUB`, search order and `PROFILE.SUB`; SETDEF's `SUB` | SUBMIT.COM 2–2.5 KB; the CCP +200–300 bytes; SETDEF +50 |

Tests are about as much again as the code, in lines, as in 4b: of the order of 2,500–3,000 lines of C++ over the
three and a dozen small test programs and RSXs in `oodos/sys/test`.

**Testing** (as phases 3 and 4: directed tests line for line, cpmtools for what lands on a card, and each check
seen to fail on a fault put in on purpose):

- **5a, `test_oodos_rsx`** (a new suite, on `oodos_rig.h`'s `CcpRig`), with test RSXs of its own: one that counts a
  function's calls and passes them on, one that answers 60, one with remove flag 0 that sets it itself, one with
  `$FF`, one with zero page, one that records function 0. Cases: one, two and three RSXs loaded (each's page,
  `$0204`, every offset 9 and 12, the serial number, the loader flag 0, the remove flag as the file had it, read
  back from memory and compared with mkprl.py's `--load` of the same file at the same page and zero page, byte for
  byte); calls passing down the chain in order; the CCP's own calls through them; zero page slices given,
  relocated and freed, and a seventeenth byte refused; a sixteenth RSX refused; the same RSX attached to two
  programs run one after the other, stacked; an RSX or a program too big, and a program that fits alone but not
  with its RSXs (the RSXs taken out again, `$0204` as it was); non-banked entries passed over; SCB entries set
  after the CCP's settings; an RSX-only `.COM` back to the prompt, its `$FF` RSX still there for the next program
  and gone after it; an empty `.COM` `NO PROGRAM`; removal of the lowest, the highest and a middle one at the warm
  boot (the TPA's top rising only from the bottom); function 0 seen once at each kind of end (`RTS`,
  `JMP $0200`, function 0, ^C in 10, ^C after ^S, an error's end, 47); a `$0203` a program overwrote mended at the
  warm boot; the CCP's byte (bit 7 and the count) through a prompt, a `!` line, a built-in and a program; 59
  loading a `.PRL` overlay (at two pages, its bytes as mkprl.py has them), and refusing each way (not a page, below
  `$0300`, over an RSX, zero page, a bad header, a short file, a `.COM` not at `$0300`, an `.RSX`), with A's and X's
  values; the caller's DMA address, DMA segment, error mode, multi-sector count and user the same after 59 as
  before; 59 removing flagged RSXs, and with 0 only that; 60 with no RSX and with one; 47 to a program with RSXs;
  `UT_MEM` (4b's utilities) seeing the lower top. `check_tools.py`: `rsx.cfg`'s links, mkprl.py's prefix checks
  and refusals, gencom.py's add, replace, remove, `[NULL]`, `[SCB=]` and the 15 entries.
- **5b, `test_oodos_utils_rsx`:** GENCOM's every form and message, its files read back from M: and compared with
  gencom.py's; GET's and PUT's every form, option and message, line for line, with a test program that reads by 1,
  6, 10 and 11 and writes by 2, 6, 9, 111, 5 and 112; the four status modes of bits 8 and 9; `[SYSTEM]` on the CCP's
  lines, ended by the file and by a `GET CONSOLE` line; a GET inside a GET file; PUT of a built-in (DIR) and of a
  program, the file closed at its end; `PUT>`; the delete question both ways and read-only; the end at the file's
  end and at the program's; the caller's state across an RSX's own file calls (as for 59); what PUT left on M:
  read back.
- **5c, in the same suite:** SUBMIT's arguments, `$0`, `$$`, `^`, `^^`, a bad parameter, `$FF`, a line of 128 and of
  129, `<` lines and `<` alone, leftover `<` lines' warning, a program wanting more than they give, `;` and `:`
  lines, `SUBMIT` alone, nesting (two levels and the RSXs stacked, too many levels), a GET in a SUBMIT file, 47
  inside a SUBMIT, a ^C's end, the search order every way (the first type on every drive before the second),
  SETDEF's `ORDER`, `PROFILE.SUB` at cold boot and its absence, a temporary file a reset left, a full temporary
  drive; the temporary files gone from M: after.
- **cpmtools:** `test_oodos_card` runs GENCOM, PUT and SUBMIT on the committed card and saves it; `interop.sh` gains
  a `--rsx` set where `cpmcp` reads GENCOM's `.COM` back (equal to gencom.py's), PUT's file (its text), and `cpmls`
  shows no temporary file left; `fsck.cpm` clean.
- **Timing** (`test_oodos_rsx`, as 3b2's): a BDOS call's cost through no, one and three pass-through RSXs, and a
  GENCOM'd program's load against a plain one's.
- **The checks of a stage:** staging builds get `tools/check.sh quick` only; the sanitizer suite and
  `tools/check.sh full` only for a production release candidate (the owner's rule).

**The books.** After the stage that builds it, each entry is written in docs/ on that stage's branch, once G1 has
merged (so the two never edit the same files at once): 5a the Programmer's Guide's chapter 6 RSXs and Chaining
sections, `bdos-59` and `bdos-60`, `bdos-47`'s note on RSXs and SUBMIT (G1 writes the entry), appendix C's new SCB
bits, appendix G's differences (the resident loader, 59 open to every program, `.COM` overlays only at `$0300`,
function 0 at every end, the table, the CCP's SCB byte); 5b `util-gencom` (a new name: check_docs.py's `PLANNED`
and `unwritten.txt` gain it, docs/'s change), `util-get`, `util-put`; 5c `util-submit`, the User's Guide's `.SUB`
and `PROFILE.SUB` in its command line chapter, and SETDEF's `ORDER`.

**Risks**

- **An RSX's own calls disturbing the program** (its DMA address and segment, user, error mode, multi-sector count,
  console mode): the commonest way such code goes wrong. One `rsx.inc` for all of them, and tests that compare the
  program's state before and after.
- **"The next command"** for GET and PUT rests on the CCP's SCB byte being stepped at every way a command starts;
  one missed (a chained command, a `!` line) and redirection lasts a command too long. Each way has a case.
- **The warm boot through the chain:** an RSX that hangs on function 0 hangs every warm boot. Break (NMI) still
  reaches the monitor, and a reset is a cold boot with no RSXs.
- **Block 62's state** (the table, the line, the CCP's state) and the RSXs themselves are in memory a program can
  reach, kept only by the TPA rule, as now.
- **Zero page:** 16 bytes, handed out first fit, can fragment. OODOS's RSXs take none.
- **The Guides' gaps,** marked ours above: offset 12's value past the first RSX, `$0`'s meaning, when PUT asks for
  another file, 59 with no FCB. A stage that reads the Guides otherwise follows them and says so here.
- **Cost:** each pass-through RSX adds its filter (about 15–30 cycles) to every BDOS call, and 59's door 4 cycles to
  all; measured in 5a.
- **The books in parallel:** phase 5's entries wait for G1's merge, or the two branches conflict in docs/.

**Proposed (5a, as built, 2026-11-04)** ([`oodos/sys/loader.s`](../oodos/sys/loader.s), `ccp.s`, `common.s`;
[`oodos/tools/gencom.py`](../oodos/tools/gencom.py); the plan above is what was built, and these are the details):

- **Where it lives:** the loader is 1,968 bytes of block 62's code window and 285 of its BSS (the table, a record,
  the headers, the caller's state); with cold boot and the CCP, 7,388 bytes and 1,269 of BSS, 8,657 of 12,288.
  Common memory 1,186 of 2,048 (59's door, `LDOOR`, 22 bytes: `STUB` starts with `CPY #59`, so `$BFFD` still
  jumps to `STUB`). The BDOS 11,637 (function 0's 5). No zero page anywhere: the loader's pointers are in its own
  instructions.
- **59's X codes** are 0 an address or no room, 1 a header, 2 zero page, 4 RSXs attached to a program's file
  (below), and **3 not a program** (no magic number,
  an empty file, or a size of 0 with nothing attached; mine: the CCP needs it to say `NO PROGRAM`). Called by the
  CCP (the SCB's `$19` bit 7, kept in block 62 as `CCPST`) the loader takes a `.COM` only, so `NAME.COM` holding a
  `.PRL` is `BAD PROGRAM HEADER` as before. Bit 7 of the flags (breaks the 65C816 rules) is refused, as the CCP
  refused it.
- **Records are read by 33,** one at a time, R0–R2 set for each, so the image, the bitmaps, the table and each RSX
  are read where they are, with no sequential position to keep (mine). The DMA goes to the record's place in the
  TPA for whole records, else to the loader's buffer in block 62 (a CPU address in W3, which the stub saves).
- **The order:** the header checked; the attached table read entry by entry (SCB entries kept, non-banked ones
  passed over, RSXs loaded and relocated, their serial number by 107 and loader flag 0); `LINK`; then the program's
  fit against the lowest RSX, its image, and a `.PRL`'s relocation. An RSX whose prefix alone says non-banked is
  found so only after its image is read below the lowest RSX; that memory is free TPA, so nothing is lost but the
  read (mine).
- **The chain:** `LINK` writes `$0203`'s `JMP`, `$0204`, and each RSX's offsets 9–13 from the table at every
  warm boot and after every load or removal. An RSX's name in the table is its file's (gencom.py's, as GENCOM's
  replace by name); the loader keeps no names.
- **Function 0:** the CCP's warm boot sends it down the chain once when there are RSXs; the BDOS's function 0
  sets `WBF` before its warm boot, so a program's own function 0, which the RSXs saw on its way, isn't sent
  again (mine): every end is seen exactly once. Then the removal, then the CCP's usual warm boot work.
- **`KEEP`:** 1 once an RSX-only `.COM` is loaded (no warm boot follows it: the CCP goes on to its prompt), 2
  once the next program starts; the warm boot removes nothing while it is 1, and a program's 59 nothing while it
  isn't 0. So the RSXs stay through built-ins and a ^C at the prompt, serve the next program, and go after it.
- **The SCB's `$19`** is `CCPST`: bit 7 set at each warm boot, cleared as a program starts; bits 0–6 stepped as
  each command starts (a keyword reached: not an empty line, a comment or a skipped `:` command).
- **SCB entries** from a CCP load are set by `APPLYSCB` as the program starts, after the CCP's own settings; an
  RSX-only `.COM`'s at once; a program's 59's before 59 returns.
- **mkprl.py** checks the prefix when it makes an RSX (`make`), not when it loads one (`--load`): the 6502
  loader checks offsets 6 and 9 are `JMP`s and the non-banked flag, and the rest is the build's.
- **Measured** (`test_oodos_rsx`): `JSR $0203` to the BDOS's first instruction is 130 cycles with no RSX (126
  before the door's `CPY`), 165 through one pass-through RSX, 240 through three.
- **Tests:** `test_oodos_rsx`, 21 cases (the plan's list above, and the review's fixes), with five test RSXs
  (`oodos/sys/test/trsx.inc`), RSXQ (a program that does what its tail says) built plain and with RSXs by
  gencom.py, OVLT (59 by a program) and two overlays. `check_tools.py` gains rsx.cfg's links, the prefix's
  refusals and gencom.py's table, replace, strip, `[NULL]`, SCB entries and limits (PASS 119). Each check was seen
  to fail on a fault put in on purpose: 33 in the system (each failing the case it should), 10 in the tools; two
  that passed at first (the RSX-only exception at a ^C's warm boot, the table's non-banked flag alone) got cases
  of their own. One check changed: an RSX without its prefix, which mkprl.py made before, is now refused.

**After the review of 5a (2026-11-04, the owner's triage):**

- **59's door clears the decimal flag** before the loader (the stub did, but only for the BDOS). A caller's D was in
  fact harmless, since the loader's first BDOS call clears it, but nothing should rest on that.
- **A program's 59 removes its own one-program RSX,** as the Guide has it, kept: an RSX meant to outlive its
  program's overlay loads starts with its remove flag 0 and sets it when done, as CP/M 3's do (bdos-59 says so).
- **`KEEP` marks the RSXs it keeps** (`TKEEP`, `KEEPNEW`): the RSX-only `.COM`'s, so the next program's 59 and
  the warm boots before it remove the other flagged RSXs as usual; the warm boot after that program clears the
  marks (`UNKEEP`) and removes them.
- **Only the CCP's loads attach RSXs:** a program's 59 of a file with RSXs attached is refused, A = `$FE`,
  **X = 4** (ours), before anything is loaded.
- **mkprl.py `--load`** checks an RSX's offsets 6 and 9 and refuses a non-banked one, as the 6502 loader does.
- **The warm boot drops an RSX a program wrote over** (offset 6 or 9 not a `JMP`: `CHECKRSX`, before `LINK`), so it
  can't hang the warm boot's function 0.
- **Measured, a load** (the CCP's search's Open to `$0300`, `test_oodos_rsx`): RSXQ, 544 bytes, 78,266 cycles; with
  one 130-byte RSX attached 138,688, with two 215,481; SYSTEST, 3,971 bytes, 343,142 (10,723 a record). 4b2's
  CCP, reading the image sequentially, took 60,635 for RSXQ and 311,608 for SYSTEST: 59's state kept and put
  back costs about 17,000 cycles a load, the random reads about 10% on a larger file. 48 ms for SYSTEST at
  7.16 MHz: not slow, so the reads stay random.
- **Where it lives now:** block 62's code window 8,762 of 12,288 (7,477 of code, 1,285 of BSS); common memory
  1,187 of 2,048.
- **The version stayed 0.2** through 5a-5c (the owner: one bump, after 5c). 0.3 (2026-11-05) is phase 5's end:
  OODOS.SYS, OOLDR, PIP's banner and function 72 say 0.3, all from system.inc's SYS_MAJOR and SYS_MINOR (mksys.py
  reads them there), and the books' Since lines say OODOS 0.3 for the functions, built-ins and utilities phase 5
  and item 10 added or changed (a Since line is the version that first had the entry as it is now: 10 and 49
  stay 0.2, as the change was in the RSXs and the SCB's bytes).

**Decided (2026-11-04, the owner, on this plan):** the loader in block 62 with the CCP, 59 through common memory;
every end of a program reaching the RSXs as function 0; three branches, 5a–5c; SUBMIT's lines in temporary files,
nesting stacked (ours, as marked above); a ^C ending a program ending the SUBMIT; `PROFILE.SUB` from the default
drive at cold boot, and none on the demo card; GENCOM's `[SCB=...]` supported, as entries of the attached RSX
table, counted in the 15; the file type `.RSX`; a `.COM` overlay anywhere but `$0300` refused (ours); a GET inside
a GET file stacking, as in CP/M 3.

**Decided (2026-11-04, the owner, on the revised plan):**

1. **A typed `NAME.PRL` at the prompt stays refused,** `NAME?` as since 4a (ours: the Guide's CCP loads it as a
   `.COM`, 1.6.2, step 3; on OODOS a `.PRL` is for 59, and its zero page rules differ from a program's).
2. **`$0` in a SUBMIT file is the SUB file's name as given in the command** (`START`, or `B:START.SUB`; ours: the
   Guide allows `$0` and says no more).

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

### The plan, phase 6 ###

**Proposed (phase 6, 2026-11-05).** Each stage reads its part of the Guides again at its start, as 4a–5c did,
and says here what changed.

**Decided (2026-11-05, the owner):** the plan as written, and the questions at its end as recommended: question 1
by (a), the owner's sequencer PROM loaded from the fixtures at run time and the tests skipping without it, and never
a sequencer table of our own; 2–12 as their recommendations. 6b first (it needs nothing of 6a).

**What there is now (main, 7b691fc)**

- **The emulator's card** (`emulator/components/devices/disk2_card.h`, `.cpp`) is the Disk II in Apple II mode:
  slot 6's sixteen switches through AppleIo, the boot ROM at `$C600`, two drives with heads stepped by the
  phases, a motor that runs on for 1 s once switched off, WOZ, `.dsk`/`.do` and `.po` images (written out as DOS
  3.3 formats a disk; `DiskImage::to_sectors()` reads them back). In native mode it answers only its ID, `$C1` at
  `$C67F`; every other byte of its block reads `$FF`, and a write does nothing.
- **The sequencer is the owner's PROM.** The card runs the 341-0028's table at each step (two a CPU cycle, Apple
  II mode's clock); with no PROM loaded every step is command 0, clear, and nothing reads. test_disk2 checks the
  wiring with tables of its own made from rules (load, shift, count), none of them a sequencer that reads a disk;
  test_iie boots disks on the owner's PROM from `fixtures/apple2e`, and skips without it.
- **The BIOS** (`oodos/sys/bios.s`, block 61, 3,445 of 16,384 bytes) has A–G as SD units of 16,384 or 65,536
  sectors and M:; a unit of any other size isn't a drive. Cold boot lists the Disk II among the cards (`cold.s`'s
  `CARDTAB`, by its ID) and does nothing else with it.
- **The BDOS assumes 512-byte sectors, 16 a track:** `OFFS` is OFF × 16 (`bdosdisk.s`'s login), and DISKOP gives
  the BIOS the sector / 16 and the sector & 15. Every drive has CKS `$8000`: no directory checksums, so no media
  change is ever seen. Eight drive slots (A–G, M:): the hash 8 KB a slot in blocks 24–27, the allocation vectors
  1 KB a slot in the system block from `$1000`. Blocks 29 and 30 are spare.
- **`DRIVES.TXT`** already takes any path, `/DISKS/CPM.DSK` among them; OOLDR mounts it and SELDSK then has no
  drive for it.
- **cpmtools's `apple-do`** (cpmtools 2.23): 256-byte sectors, 35 tracks of 16, 1 KB blocks, 64 directory entries,
  3 boot tracks, CP/M 2.2, and a skew table for the `.dsk` file's DOS order: CP/M's sector *n* of a track is the
  image's sector `0 6 12 3 9 15 14 5 11 2 8 7 13 4 10 1`[*n*].

**The drives (ours)**

- **What the BDOS sees:** an `apple-do` drive has 512-byte sectors, as every OODOS drive, eight a track; the BIOS
  makes each of them from two of the disk's 256-byte sectors, CP/M's sectors 2*h* and 2*h* + 1 of the track, each
  through the skew table. Its DPB: SPT 32, BSH 3, BLM 7, EXM 0, DSM 127, DRM 63, AL0 `$C0`, AL1 0, OFF 3, PSH 2,
  PHM 3; CKS `$8000` on an SD unit, 16 on O: and P: (below).
- **The BDOS learns other tracks** (6b): OFFS is OFF × (SPT / 4), and DISKOP's split is by the drive's sectors a
  track (8 or 16, a shift kept in the drive's CD from its login). Every other part of the BDOS already works in
  the drive's flat sector numbers. About 40–80 bytes; the 1 KB block (BSH 3) is new to it and gets its own cases.
- **SD units** (6b): a unit of 280 sectors (143,360 bytes, a `.dsk` or `.do`) is an `apple-do` drive on its letter,
  B–G, as `DRIVES.TXT` or 74 mounts it. The file is taken to be in DOS order whatever its name (`.po`: a question
  below). A: stays an OODOS format: OOLDR refuses an `apple-do` A: and says so.
- **O: and P:** (6c) are drives 1 and 2 of the Disk II card, when cold boot found its ID; with no card, SELDSK has
  no drive for them (`Invalid Drive`). They get drive slots 8 and 9 in the BDOS: their hashes (256 bytes each) and
  their allocation vectors (32 bytes each), placed at 6c's start where the system block and block 29 have room.
- **A track buffer** (6b; a question below): READ of an `apple-do` drive brings in the whole Apple track, 4 KB,
  and later READs on that track come from it. From an SD unit that is 8 SD sectors in a row; from the Disk II one
  revolution and a little, every sector decoded as it passes, where sector by sector costs a revolution or more
  each (the skew puts a 512-byte sector's two halves anywhere on the track). WRITE writes through: the buffer
  and the disk at once, the SD sector or the Disk II sector it changed, so nothing is held back and FLUSH has
  nothing to do. Three buffers, O:, P: and one shared by the SD units, 12 KB in block 29, reached by DMA copies
  as M: is.
- **Media change** (6c, O: and P:): a floppy can be changed under the system, and the Disk II has no signal that
  says so. CP/M 3's answer is the directory checksums (the Guide's 2.3.11): CKS 16, a byte for each 128-byte
  record of the directory, made at login and compared at each directory read; a difference is a changed disk. The
  drive is then logged in again, and a function on an FCB opened before the change returns error 10, Media Change,
  as the Guide's file functions list it. The System Guide, with the BIOS's own media flag, isn't among the
  references, so the BIOS's side is ours: none, the checksums alone. New to the BDOS: about 150–250 bytes.

**The emulator's card in native mode (6a)**

- **The switches:** `$C600–$C60F` are slot 6's `$C0E0–$C0EF`, the same sixteen in the same order, a read of an even
  one the data register and of an odd one `$FF`, a write putting its byte on the card's bus, as design.md says.
  `$C610–$C67E` still read `$FF`, `$C67F` the ID. A peek changes nothing. /RESET clears the latch in either mode.
- **The clock:** the sequencer runs from OSC ÷ 7 in both modes, so in native mode, OSC ÷ 2 a CPU cycle, it is 2
  steps every 7 cycles (3.5 a step), the remainder carried, and a disk byte 224 cycles. Apple II mode stays 2 a
  cycle. The ratio follows the bridge's mode (`clock_hz()`), and the card catches up to the cycle the mode changes
  at the old ratio. The motor's run-on is counted in steps (1 s), not cycles, so it is a second in both modes.
- **Unchanged:** the PROM, the drive, the images and Apple II mode; test_iie must pass as it does.
- **The WebUI** needs nothing for the drive itself (its disk bar inserts disks in either mode); its native-mode
  text is checked at 6a's start, and changed if it says only Apple things.

**The driver (6c)** (`oodos/sys/disk2.s`, block 61, and `oodos/sys/disk2.inc`)

- **The switches by name** in `disk2.inc`, as offsets from `DISK2` (`D2_PH0OFF` = 0 … `D2_Q7ON` = `$0F`), not as
  addresses in `backplane.inc` (a question below says why).
- **Only plain absolute loads and stores** touch the card: an indexed access can make an extra read of the
  address before, and a read-modify-write two accesses, and either changes a switch.
- **Reading:** the motor on and the drive selected; an address field found (`D5 AA 96`, then the volume, track,
  sector and their checksum in 4-and-4, then `DE AA`); its data field (`D5 AA AD`, 342 disk bytes and a checksum
  byte of 6-and-2, `DE AA`) decoded as it comes into the track buffer, the next sector after it, until all 16 are
  in. At 7.16 MHz a byte waits about 224 cycles in the data register, so the loop has room to decode on the fly.
  A bad checksum or a missing field is tried again for a few revolutions; then once more after a recalibration;
  then READ returns 1. With no disk, or no card answering, that is a second or two, not a hang. **Every wait for
  bit 7 is bounded** by a count of polls or cycles (6a's review): a card with no sequencer PROM (the WebUI's, for
  one) still answers its ID but never sets bit 7, so a wait without one would hang.
- **Writing a sector:** its address field found, then, with interrupts off (`PHP`/`SEI`, `PLP` after) and the DMA
  controller's master enable cleared (put back after), Q7 on, the sync bytes, the data field's prologue, the 342
  bytes and the checksum, the epilogue, each loaded exactly 224 cycles after the one before (a sync byte's ten
  bits 280), Q7 off. The loop and its tables are page-aligned and asserted at build time not to cross a page, so
  no branch or index costs a cycle it isn't counted for. Write-protect is sensed first (Q6 on, Q7 off, bit 7 of the
  data register): WRITE returns 2 and the disk isn't touched. No read-after-write check (DOS 3.3 makes none).
- **Stepping:** the phases in turn, a half track at a time, the one before turned off, with the motor on (the drive
  powers its stepper only then, and the emulator moves the head only then); a fixed time a half track and a
  settling time after the last, from the drive's data at 6c's start. The emulator moves the head at once, so only
  the real drive (the Floppy Emu) tests those times. Recalibration steps out 40 tracks, onto the stop, at the first
  access after cold boot and after a failed READ; after that the driver keeps each drive's half track, and checks
  it against each address field's track.
- **Sector order:** CP/M's sector to the image's (the skew), then the image's DOS order to the physical sector on
  the track, by DOS 3.3's interleave, the same table the emulator writes `.dsk` files out with. So a `.dsk` made
  by cpmtools, inserted in the drive, reads as the same files as on an SD unit.
- **The motor** (a question below): switched off at the end of every READ and WRITE; the card keeps it turning a
  second more, so the next access within that second finds the disk at speed, and the motor stops a second after
  the last. Whether it is still turning is seen from the data register changing. From stopped, a read just starts
  (the address fields aren't found until the disk is at speed); a write waits a spin-up time first.
- **Interrupts and DMA during a read:** interrupts are off for a whole sector or track operation too, the flag put
  back after; DMA isn't used by OODOS while the BIOS runs, so a read leaves the controller as it is. Break (NMI)
  can't be held off: a write it lands in loses that sector, and the NMI handler turns the card's write mode and
  motor off and the DMA master back on before the monitor (6c's review), so the rest of the track is kept. A
  halt by the console's RDY in the middle of a write keeps the card writing (the CPU stops, the card doesn't):
  that sector and whatever the head passes over while halted are lost.
- **Room:** the driver about 2–2.5 KB and the `apple-do` layer 0.8–1.2 KB in block 61 (12.9 KB free); its scratch
  in the BIOS's own zero page, at most 4 bytes more of the system's (232 of 240 used at phase 5's plan;
  counted again at 6c's start).

**The DOS 3.3 exchange utility (6d)**

- **`DOS33.COM`** (the name a question below; `oodos/utils/dos33.s`), a utility as 4b's:
  - `DOS33 d:`: the disk's catalog as DOS 3.3's CATALOG shows it (the lock, the type, the sectors, the name) and the
    free sectors from the VTOC.
  - `DOS33 GET d:NAME [e:]FILE.TYP [T|B|R]`: a file from the Apple disk. T a text file: the high bit cleared, CR to
    CR LF, to the first 0, then ^Z; B binary: the four-byte address and length dropped (the address shown); R raw,
    every sector of the file as stored. The default by its type: T for T, B for B, R for the others (A, I, S, R).
  - `DOS33 PUT [e:]FILE.TYP d:NAME [T|B[=addr]]`: a file to the Apple disk, the other way: T sets the high bit, CR
    LF to CR, ending at ^Z; B with a load address (`$2000` unless given). A name already there, or a locked one,
    is refused.
  - `DOS33 ERA d:NAME`: delete, so a file can be replaced. A locked file is refused.
  - A DOS 3.3 name has up to 30 characters and may hold spaces: in quotes then. `d:` is O:, P:, or any SD drive
    whose unit is a 140 KB image; anything else, or a disk with no VTOC DOS 3.3 would take, is refused with a
    message.
- **Function 76, an Apple disk's sector** (ours, from OODOS's reserved range; a question below): A/X a block with
  the drive, the track, the DOS 3.3 sector number, read or write, and the address of 256 bytes in the program's
  memory. Before its first sector on a drive the BDOS writes back and drops that drive's buffers and logs it out,
  as 37, so the drive's next CP/M use logs it in again. Returns 0, 1 an error, 2 write-protected, `$FF` not an
  Apple drive. On an SD unit the sector is at (track × 16 + sector) × 256 in the file, on the Disk II at its
  physical place by the interleave.
- **`oodos/tools/dos33.py`** (ours, written from the format: the VTOC on track 17 sector 0, its bitmap, the catalog
  chain, the track/sector lists): makes DOS 3.3 images for the tests (tracks 0–2 marked used and left empty, so
  there is no DOS code on them and the image isn't bootable), lists them, takes files out, and checks one
  (every sector used by exactly one file or the catalog, or free in the bitmap). It is the tests' oracle.
- **The other oracle is DOS 3.3 itself** (a question below): the emulator's IIe boots the owner's DOS 3.3 disk from
  the fixtures in drive 1, with our image in drive 2, and `CATALOG,D2`, `VERIFY` and `BLOAD` read what DOS33 wrote,
  and `BSAVE` writes what DOS33 then reads back. Tests work on copies in memory and print only what they computed.
- **ProDOS** is not in phase 6 (not decided); the utility is laid out so a ProDOS reader could join it.

**Stages (proposed: four branches, each merged on its own)**

| Stage | Branch | What | Size (estimates) |
|---|---|---|---|
| **6a** | `feat/disk2-native` | the emulator's card in native mode: the switches at `$C600–$C60F`, the 2-in-7 clock, the run-on in steps, the mode change; EMULATOR_PLAN.md's backlog item and design.md's "doesn't do this yet" | `disk2_card` +60–100 lines; tests +300–400 |
| **6b** | `feat/oodos-apple-sd` | the BDOS's sectors a track and OFF; the BIOS's `apple-do` layer (DPB, skew, track buffer, SD backend); 280-sector units as drives; OOLDR's refusal of an `apple-do` A:; the test card gets `/DISKS/CPM.DSK` | BDOS +40–80 bytes; BIOS +0.8–1.2 KB |
| **6c** | `feat/oodos-disk2` | the Disk II driver; O: and P:, their slots; the directory checksums and error 10; the motor | BIOS +2–2.5 KB; BDOS +150–250 bytes |
| **6d** | `feat/oodos-dos33` | function 76; DOS33.COM; dos33.py; the card gets DOS33.COM and `/DISKS/DOS33.DSK` | DOS33.COM 4–6 KB; BDOS +100–150 bytes |

6b needs nothing of 6a and can go first or beside it; 6c needs both; 6d needs 6b (its SD half) and 6c (its O:
half). The version stays 0.3 through 6a–6c and becomes 0.4 after 6d, as 0.3 came after 5c (a question below).

**Done means,** for every stage: its tests below pass, each must-fail case seen to fail with its fault put in;
`tools/check.sh quick` passes with the fixtures present; its as-built section is written here (what changed from
this plan, and what was measured); its book entries are written; and the owner has merged it. And for each:

- **6a:** native software can drive every switch of the card, and a sector written at 224 cycles a byte reads
  back in the emulator; Apple II mode is as it was (test_iie).
- **6b:** a cpmtools `apple-do` image named in `DRIVES.TXT` is a drive whose files OODOS reads and writes, and
  cpmtools reads what OODOS wrote (`interop.sh --apple`, which the owner runs with the full tier).
- **6c:** the same image in the emulated drive is O: (or P:), with the same results byte for byte as on an SD
  unit; a changed floppy is seen; the drive's times are written down for the Floppy Emu's check at bring-up.
- **6d:** DOS33 lists, takes and puts files on a DOS 3.3 disk, in the drive and as an SD image, and DOS 3.3 in
  the emulated IIe reads what it wrote, and the other way.

**Testing** (as phases 3–5: directed tests line for line on `oodos_rig.h`'s `CcpRig`, cpmtools for what lands on
an `apple-do` disk, dos33.py and DOS 3.3 for what lands on a DOS 3.3 disk, and each check seen to fail on a fault
put in on purpose, with `OODOS_SYS_DIR`). Every image is ours: `apple-do` ones made with cpmtools in a throwaway
container (`oodos/sys/test/make_appledisk.sh`, as `make_mdisk.sh`, committed as `.sparse` beside the card's), DOS
3.3 ones by dos33.py. The suites that need the owner's PROM or DOS 3.3 disk load them from the fixtures at run
time and skip without them (exit 77), as test_iie does; `tools/check.sh` already refuses to run without the
fixtures, so the quick tier runs them.

- **6a, `test_disk2`** (no fixtures, its own rule tables): each of the sixteen switches in native mode, the odd
  reads, the rest of the block `$FF`, a peek changing nothing; a counting table steps 2,000 times in 7,000 native
  cycles and 14,000 in 7,000 Apple ones; the run-on still turning 0.99 s of native cycles after the motor is
  switched off and stopped at 1.01 s; a mode change mid-run, the disk's place continuous across it; /RESET
  in native mode. **Must fail:** the card left at Apple II mode's two steps a cycle in native mode (the counting
  case), and the run-on counted in cycles (a seventh of a second in native mode), each with the fault put in.
  **`test_disk2_native`** (a new suite, the owner's PROM): the test drives the switches itself at chosen cycles
  on a `.dsk` written out by `from_sectors`: the 16 address fields of a track in order and each data field equal
  to the image's sector (a 6-and-2 decoder of the test's own); a sector written at 224 cycles a byte, which
  `to_sectors()` reads back. **Must fail:** the same write at 223 and at 225 cycles a byte doesn't read back: the
  timing design.md promises is what the test holds. Time: under 1 s and 2–4 s.
- **6b, `test_oodos_apple`** (a new suite; a card with `/DISKS/CPM.DSK` from cpmtools on C:): SELDSK's DPB for a
  280-sector unit, read back by 31 and compared with the values worked out from the diskdef; DIR and TYPE of
  cpmtools's files, every byte; PIP from M: to C: and back, and between C: and A:; ERA and REN; the disk filled
  (127 blocks: Disk Full) and the directory filled (64 entries: Directory Full); 1 KB blocks' allocation;
  the boot tracks unchanged after all of it; a sector's write leaving the other half of its SD sector as it was;
  `RO` in `DRIVES.TXT` (Read Only); 74 mounting a `.dsk` while running and the drive logged in again; an `apple-do`
  A: refused by OOLDR with its message; a unit of 281 sectors not a drive. The existing suites pass unchanged
  (`oodos-8m`, `oodos-32m`, M:). **Must fail:** OFF taken as 16 sectors a track (the BDOS today: cpmtools's
  files read as garbage), and no skew (the same): each with the fault put in. cpmtools: `test_oodos_card`
  writes a file on C: and saves the card; `interop.sh --apple` (Docker, the full tier) has `cpmls`, `cpmcp` and
  `fsck.cpm -f apple-do` read it. Time: 25–40 s.
- **6c, `test_oodos_disk2`** (a new suite, the owner's PROM; split in two if it passes 60 s): the same cpmtools
  image in the emulated drive as O:, then DIR, TYPE and PIP each way; the image after, by `to_sectors()`,
  compared with the same PIPs done on an SD unit: the two must be the same bytes; P: and copies between O: and
  P:; no card (`Invalid Drive`), no disk (`Disk I/O` within 3 s of emulated time), write-protected (Read Only, the
  disk unchanged); a data field's checksum spoiled in the track's bits (retried, then `Disk I/O`) and a sector's
  address field spoiled (the others still read); the head moved by the test to a wrong track (recalibrated and
  read); every track 0–34 reached, by the card's status; the motor stopping a second after the last access and the
  next access within the second not waiting; the disk changed between two DIRs (the second shows the new disk) and
  under an open file (error 10, and the new disk not written). **Must fail, each with its fault put in:** the
  write loop a cycle long (a `NOP`: the sector doesn't read back; the test also times every load of the data bus,
  224 and 280, from the bus); the `SEI` taken out with the VIA's timer interrupt set to land mid-write (the
  sector bad, as OOLIB's tick will make it); the checksums off (the changed disk's directory overwritten, the
  real failure they exist for). Measured: a 32 KB file from O: and to it, and a whole disk read, in emulated
  seconds. cpmtools: `interop.sh --apple` reads the O: image saved by the test too. Time: 40–70 s.
- **6d, `test_oodos_dos33`** (a new suite; dos33.py's images on an SD unit and in the drive): DOS33's every form
  and message, line for line; GET in T, B and R and PUT in T and B, the bytes compared with dos33.py's; a name with
  spaces; a locked file; ERA; a full disk, a full catalog (past its first sector); not an Apple drive (A:); a CP/M
  disk (`apple-do`) refused as not DOS 3.3; dos33.py's check on every image after; function 76's refusals, its
  write-protected answer, and a CP/M drive's buffers dropped by it (a directory read after 76 writes comes from the
  disk). **Must fail:** a VTOC bitmap not updated (dos33.py's check catches it; so does DOS 3.3's `VERIFY` of the
  next file saved) and text's high bit left clear (DOS 3.3's `READ` of it). `check_tools.py`: dos33.py's images,
  listing, extraction and its check refusing a spoiled bitmap and a looped track/sector list. **With DOS 3.3**
  (the owner's disk; in test_iie's rig): `CATALOG,D2` shows DOS33's files with their types and sectors, `VERIFY`
  passes for each, `BLOAD` puts a B file's bytes at its address; `BSAVE` of bytes the test put in memory, then
  DOS33 GET has the same bytes. Time: 30–50 s.

The quick tier's wall time grows less than the sum (ctest runs the suites side by side): about 20–40 s, if no new
suite runs past the longest now (test_oodos_utils_dir, 57 s). Tests are about as much again as the code in lines:
2,000–2,500 lines of C++ over the four stages, `make_appledisk.sh`, dos33.py and its checks, and two or three test
programs in `oodos/sys/test`.

**The books.** Each stage writes its entries on its branch: 6b the User's Guide's drives (`.dsk` units in
`DRIVES.TXT` and MOUNT) and the Programmer's Guide's note on `apple-do` DPBs; 6c O: and P: and a changed floppy in
the User's Guide, error 10 in the Programmer's Guide's file functions; 6d `util-dos33` in the User's Guide and
`bdos-76` in the Programmer's Guide (written on the branch, so `unwritten.txt` isn't touched). The Technical
Reference's native Disk II pages, the BIOS's `apple-do` layer and the driver come after both G2 and phase 6 have
merged, with the switches' equates (below).

**Risks**

- **The write loop's timing** is all or nothing: one stray cycle and every sector written is bad. The bus-timed
  test and the build's page assertions hold it; the real card is the last check.
- **The real drive's times** (a half track's step, the settling, the spin-up) can't be tested in the emulator,
  which moves the head at once and spins up at once. They are proposed from the drive's data and checked on the
  Floppy Emu (which steps as quickly as it is told) and, if one is fitted, a real Disk II drive.
- **The card catching up across a mode change** at the wrong ratio would move the disk on by the wrong amount; 6a
  tests it.
- **Media change** rests on the checksums. A changed disk whose directory is the same (two copies of one disk) is
  taken for the same disk, as in CP/M 3; its files are the same too, until one is written.
- **Copyright:** the tests need the owner's PROM and DOS 3.3 disk, and must never put a byte of either in the repo,
  in a log or on the site. A test prints only what it computed.

**Questions for the owner** (each with a recommendation)

1. **The sequencer (blocking 6a's PROM suite and all of 6c).** Reading or writing a disk in native mode, in the
   emulator, needs the 341-0028's contents: the card runs the PROM's table, and with none loaded nothing reads.
   test_disk2's own tables check the wiring but none reads a disk. Three ways:
   (a) the tests load the owner's PROM from `fixtures/apple2e` at run time and skip without it, as test_iie does;
   nothing of it is in the repo; the WebUI's drive works with a cartridge that has the sequencer, as Apple II mode
   does now;
   (b) a sequencer table of our own design, made by the tests from rules as test_disk2's are, that reads and writes
   a disk; it would do what Apple's table does, so it may count as reproducing it;
   (c) a byte-level model of the card when no PROM is loaded, written from what the card does rather than its
   table, as some emulators have; it couldn't hold the 224-cycle timing to the bit.
   **Recommended: (a) only.** It needs nothing new, puts nothing of Apple's in the repo, and tests against the
   real table. Not (b) or (c) unless you say so.
2. **The motor's second.** The decision says the motor goes off a second after the last access, timed by the RTC's
   tick. The card itself does that: switched off, it runs on for a second (the emulator models it, as the Disk II
   card has it). The tick would need the interrupt handler OODOS doesn't have yet (OOLIB's dispatcher). **Recommended:**
   switch the motor off after every READ and WRITE and let the card's run-on time the second; no interrupt, no
   RTC. A longer run-on can come with OOLIB's tick if it is wanted. *(6c's fourth review: the stall check in
   `RECENT` relies on the run-on outlasting the second from the access's stamp, which is taken before the I/O.
   The emulator's run-on is 0.998 s after the motor-off, plus the I/O; a real card's 556 tolerance is to be
   measured. A run-on a little short gives a false stall in the last ms of a second, which costs one directory
   read.)*
3. **The switches' equates.** `docs/tools/check_docs.py` fails any I/O equate in `backplane.inc` that has no
   Technical Reference entry, and phase 6 doesn't touch the Technical Reference or `registers.json`.
   **Recommended:** the driver names the switches as offsets from `DISK2` in its own `oodos/sys/disk2.inc`
   (`D2_PH0OFF`–`D2_PH3ON` = `$00`–`$07`, `D2_MOTOROFF`/`ON` `$08`/`$09`, `D2_DRIVE1`/`2` `$0A`/`$0B`,
   `D2_Q6OFF`/`ON` `$0C`/`$0D`, `D2_Q7OFF`/`ON` `$0E`/`$0F`), so `backplane.inc` is unchanged in phase 6 and
   G2 and phase 6 merge in either order; the sixteen equates (`DISK2_PH0OFF` = `DISK2 + $00` … `DISK2_Q7ON` =
   `DISK2 + $0F`) go into `backplane.inc` with their Technical Reference entries after both have merged, and the
   driver moves to them then.
4. **Raw sectors for DOS33: function 76** (OODOS's own range, 76–97 reserved), with the BDOS dropping the drive's
   buffers and logging it out; or BIOS function 30 through 50 (CP/M 3's slot for the implementor), where the
   utility must reset the drive itself first. **Recommended: 76**: the BDOS owns the buffers, so it is the one to
   drop them.
5. **The utility's name and forms.** **Recommended: `DOS33.COM`**, with DIR as the bare form, GET, PUT and ERA, as
   above. Not the SoftCard's name; ours is not its program.
6. **A track buffer.** **Recommended: yes**, 4 KB each for O:, P: and the SD units, in block 29, write-through.
   Without it a 512-byte read from the drive costs a revolution or more (about 2.5 KB/s); with it a track
   reads in about a revolution (about 18 KB/s). Holding writes back until FLUSH would be faster still, but loses
   them if the disk is changed first; not in phase 6.
7. **Directory checksums on O: and P:** (CKS 16, logging in again on a change, error 10 for an FCB opened before
   it). **Recommended: yes, in 6c.** Without them a changed floppy gets the old disk's allocation and directory
   written over it. SD units keep CKS `$8000`: MOUNT already says when a unit changes.
8. **`.po` images and `apple-po`.** **Recommended: not in phase 6.** A 140 KB unit is DOS order whatever its name;
   `apple-po` (ProDOS order, its own skew in cpmtools) can follow, told apart by the name in the units table.
9. **ProDOS disks in the exchange utility.** Not decided, and not proposed for phase 6. **Recommended:** decide
   after 6d, with what 6d shows of the utility's size; DOS33's layout leaves room for it.
10. **DOS 3.3 as the oracle.** The 6d interop cases boot the owner's DOS 3.3 disk from `fixtures/disks` in the
    emulated IIe, as test_iie does now, on copies in memory. **Recommended: yes**; dos33.py alone would check
    our reading of the format against our own reading of it.
11. **Text conversion.** **Recommended:** as above: T files' high bit and CR, A, I and S files raw by default,
    B files without their header. Applesoft and Integer BASIC listings (detokenising) are not in phase 6.
12. **The version.** **Recommended:** 0.3 through 6a–6c, 0.4 after 6d, one bump as for phase 5.

**Proposed (6b, as built, 2026-11-05)** (`oodos/sys/bios.s`, `bdosdisk.s`, `cpmread.s`, `oodos/ldr/ooldr.s`,
`oodos/tools/make_card.sh`; the plan above is what was built, and these are the details):

- **The BIOS:** UNITS takes a unit of 280 sectors as `apple-do` (`DRVAPL`, the DPH's DPB `DPBAPL`); READ and WRITE of
  such a drive go to `APPLEIO`. The track buffer is block 29's first 4 KB, one for every SD unit, tagged with its
  drive and track (`TBTAG`); 6c's O: and P: buffers go after it. A READ copies its two 256-byte halves out by DMA
  (COPY, so a buffer that crosses a window works as for any drive); a WRITE copies them in, then writes the SD
  sector of each half (the skew never puts both halves of a 512-byte sector in one SD sector, so that is always
  two). Any failure, a refused address included, empties the buffer, so it never holds what the card doesn't; so do
  every rescan of the units (each first select, each warm boot's) and MOUNT and UNMOUNT (`SDMNT`). A WRITE to a
  unit marked read-only is 2, as on the other drives. +353 bytes in block 61 (3,798 of 16,384).
- **The BDOS:** `TSH`, a byte a slot, is the sectors a track as a shift, from the DPB's SPT at login (4 for SPT 64,
  3 for SPT 32); OFFS is OFF shifted by it, and DISKOP gives the BIOS the buffer's sector shifted by its drive's
  TSH as the track and the bits below as the sector. CD had no room (16 bytes, all used), so TSH is beside DTAB.
  +29 bytes in block 60.
- **Function 74** takes a 280-sector image too (`bdosmem.s`, after MOUNT: 16,384, 65,536 or 280 sectors, else
  `$12` and the unit unmounted again); B–G only, as before. Found in 6b's review: as first built it refused one.
- **INITDIR is unchanged** but for its comment: it works by tracks of 16, and an `apple-do` directory is 4 sectors,
  all in its first track, so OFF + SECT / 16 and SECT mod 16 are right there. (Made general first, then taken
  back: no test could tell the two apart.)
- **OOLDR and the CP/M reader:** CR_INIT says `CRE_APPLE` (8) for 280 sectors. OOLDR takes that on B–G and
  refuses it on A: (`DRIVES.TXT LINE n: PATH IS AN APPLE DISK, NOT FOR A:`). **Changed from before:** another size
  on B–G is now `... IS NOT 8 MB, 32 MB OR 140 KB` (A: keeps `... IS NOT 8 MB OR 32 MB`), so test_oodos_boot's two
  cases of it changed their text.
- **The test card** (`make_card.sh`): `/DISKS/CPM.DSK`, made by cpmtools's `mkfs.cpm -f apple-do -b` (its boot
  tracks 12,288 bytes of a pattern, so a stray write there shows) and `cpmcp` (0:README.TXT, 0:PATTERN.BIN of
  20,000 bytes in two entries, 2:SMALL.BIN); `/DISKS/BLANK.DSK`, empty; `/DISKS/ODD.DSK`, 281 sectors. cpmtools
  runs them in a directory with no `./diskdefs`, so it takes its own `apple-do`; `oodos/diskdefs` is unchanged.
  The test card's `DRIVES.TXT` is unchanged (A: and B:); the tests mount unit 3 themselves.
- **Found:** cpmtools keeps a CP/M 2.2 file's byte count, so `cpmcp` gives back its own files to the byte, while
  OODOS reads them, as CP/M does, as whole records, the last padded with the zeros cpmtools wrote. A full
  directory is PIP's `DISK WRITE ERROR - NAME`, as a full disk is.
- **Tests:** `test_oodos_apple`, 9 cases, about 7 s: the DPB byte for byte and C:'s DPH pointing at it, and SHOW's
  sums (128 KB, 64 entries, 32 records a track: its `Sectors / Track` line); cpmtools's files read by TYPE and PIP, every byte, and the same by
  the test's own reading of the image (the skew, 1 KB blocks, 8-bit pointers); PIP of a file of one entry and one of
  two onto C:, read back by the oracle and, after a ^C reset, by OODOS; every 256-byte sector that changed is the
  directory's or the new files' (96 here, none else), the boot tracks unchanged; ERA and REN; a full disk and a
  directory filled to 64; RO; another image mounted under the unit and ^C (the old directory not seen again);
  281 sectors Invalid Drive; INITDIR on an `apple-do` drive (SFCBs in every fourth entry, the file still there);
  function 74 (MEMTEST's case A) mounting CPM.DSK over an 8 MB C:, then BLANK.DSK on the same unit (C: logged in
  again, `No File`), ODD.DSK refused (`$12`, the unit unmounted), CPM.DSK again; two `apple-do` drives at once,
  CPM.DSK as C: and BLANK.DSK as D:, a file copied C: to D: and back and read through M:, and on each image only
  the directory's and the new file's sectors changed; a write that fails (the card's writes failing, MEMTEST's
  case Y through the BIOS by 50, as INITDIR works) and the same sector read again in the same program, giving the
  card's bytes. The BDOS's own paths can't show the last: each that drops a failed buffer reads the units again,
  which empties the track buffer anyway.
- **The plan's tests done otherwise:** SELDSK's DPB read back by 31 became the DPB read from common memory by its
  label (`DPBAPL`) and C:'s DPH checked to point at it; PIP between C: and A: became C: and M: (and A: to C: in
  test_oodos_card); "1 KB blocks' allocation" is the oracle's reading of every file's 1 KB blocks, and the full
  disk; the other half of an SD sector is the stray-sector check (any 256-byte sector changed outside the
  directory and the new files fails it); a unit of 281 sectors is checked by the BIOS (Invalid Drive) and by
  OOLDR, the full directory's message is PIP's `DISK WRITE ERROR`, not `Directory Full`.
  test_oodos_boot: CPM.DSK as C: read-only boots, ODD.DSK on G: and both on A: refused, each by its message.
  test_oodos_card: PIP onto CPM.DSK as C: from the booted card, saved by `OODOS_APPLE_OUT`; `interop.sh --apple`
  (`tools/check.sh full`) has cpmtools's own `fsck.cpm -f apple-do` pass it and `cpmcp` give back OODOS's two
  files and make_card.sh's three; run by hand once, `PASS 3`.
- **Must fail, each seen to fail with its fault put in** (`OODOS_SYS_DIR`): OFF as tracks of 16 (all 6 cases
  then, 17 assertions); no skew (4 cases); DISKOP's track and sector by 16 whatever the drive (all 6, 24); the track
  buffer not emptied at a rescan (the remount case). After the review: 74's size check as it was (the 74 case, 7
  assertions); TRKLOAD's tag not comparing the drive (`LDA TBTAG / BEQ @read`: the two-drive case, 4); the buffer
  kept after a failed write (`@failed: NOP`: the failed-write case, `YSAME N`).
- **Measured:** PIP of PATTERN.BIN, 20,096 bytes, from C: to M:, 12.4 million cycles, 1.73 s. `tools/check.sh
  quick` 193 s, all passed (test_oodos_apple 8.6 s in the parallel run; test_oodos_card about 3 s more, for its
  new case); the native step's wall time didn't grow. After the review: 185 s, all passed; test_oodos_apple, 9
  cases, 9.6 s in the parallel run.

**Proposed (6a, as built, 2026-11-05)** (`emulator/components/devices/disk2_card.h`, `.cpp`, `apple_io`,
`backplane_bridge.cpp`, `machine.cpp`; the plan above is what was built, and these are the details):

- **The switches:** the card's own block (its `Device` read, write and peek) has slot 6's sixteen at `$00–$0F`, an
  even one's read the data register and an odd one's `$FF`, a write's byte on the card's bus; `$10–$7E` read
  `$FF`, `$7F` the ID. A peek changes nothing. The boot ROM isn't in the block.
- **The clock:** `Disk2Card::set_clock(Clock::Apple | Clock::Native)` catches up at the clock the card had, then
  counts by the new one: Apple II mode two steps a cycle, as before; native mode two every seven at 7.16 MHz, the
  remainder carried. **After the review:** when the emulator runs native mode's CPU at another speed (the WebUI's
  1.02, 1.79 and 3.58 MHz settings), a cycle is longer, so native mode's steps are cycles × (OSC / 7) / the
  CPU's speed, `set_cpu_hz()` (0: 7.16 MHz), which Session's `clock_cards` and the terminal app's clock change
  call as they do the other cards'; exactly two in seven at 7.16 MHz, 2,045,454 steps a second at every speed.
  Apple II mode's two a cycle don't change with it. A card alone (test_disk2's rig) starts with Apple II mode's, so its earlier cases are as they
  were. On the backplane the bridge sets it through AppleIo's `clock_changed`: at the machine's build, at every
  hard reset (native again, after the card's own reset has caught up) and when MODE's Apple II bit is written.
  `Status::steps` is the sequencer's clock, steps since the card was made, the disk turning or not.
- **The run-on** is `RUN_ON_STEPS`, 2 × `MOTOR_DELAY` steps: Apple II mode's second as it was (the emulator's two
  steps a cycle, which leaves out the long cycle), 0.998 s in native mode at any of its speeds. It now counts from the cycle before
  the switch's access (the card's catch-up to it), one cycle earlier than before; test_disk2's case of it holds.
- **The WebUI ships no PROM:** without a sequencer every step is command 0, clear, so in native mode, as in
  Apple II mode, the switches work (the motor, the phases, the head) and the data register reads 0: no byte is
  ever read, and nothing fails or waits. Nothing in native mode uses the drive until 6c; the disk bar is as it
  was. But a driver's wait for bit 7 would never end: 6c bounds every one (the plan's Reading, above). The
  Devices tab's hint names the native switches too.
- **Found, for 6c's write loop:** the table loads the byte to write in one state of its eight-step loop, so Q6
  must stay on (load) for a whole loop, 8 steps, 28 native cycles, before Q6 off (shift): the Apple's STA then
  ORA cover it in their 4 cycles at two steps a cycle, but 4 native cycles are about one step and the load is
  missed, nothing written. **Changed from the plan:** a byte loaded a cycle late (225) still reads back: the
  table waits for its next loop, and now and then a byte has a ninth bit, a 0, which a reader passes over as it
  does in a sync byte; only early (223) loses bits. So 225 is a case that reads back, not a must-fail one, and a
  load held 4 cycles is the must-fail case in its place. (How late a loop may run before the gaps reach the
  amplifier's noise, past three 0 bits, is 6c's to measure.)
- **No equates:** 6a has no 6502 code but the tests' few hand-assembled bytes, so `oodos/sys/disk2.inc` waits
  for 6c. `backplane.inc`'s comment that the switches are still to come in the emulator is G2's to change.
- **Tests:** test_disk2, 17 cases (4 new, under 1 s): the whole block, read and peeked, with a boot ROM loaded
  (the switches, odd and even, `$FF`, the ID, the ROM only in slot 6's `slot_rom`), the sixteen switches each
  way and a peek; 7,000 native cycles 2,000 steps, Apple II mode's 14,000, the remainder carried across a
  catch-up between 3 cycles and 4, a clock change no one looked at across, a byte (64 steps) 224 native cycles of
  the disk; at 1.02, 1.79, 3.58 and 7.16 MHz 2,045,454 steps a second and the run-on a second, Apple II mode
  two a cycle whatever it is told; the run-on still turning at 0.99 s and stopped at 1.01 s; /RESET in native
  mode. test_disk2_native (new, 1 case, no fixtures): the W65C02S on backplane.machine, at power-on native, its
  LDA and STA reaching every switch and the ID, 7,000 cycles of its code 2,000 steps, MODE's bit giving the card
  Apple II mode's clock and a hard reset native mode's. test_disk2_prom (new, 4 cases, 0.1 s; **after the
  review** a suite of its own, which exits 77 without the owner's sequencer PROM, so CTest says Skipped, as
  test_iie does and the plan's Testing says): the PROM loaded from the fixtures at run time (nothing of it in the
  repo or printed), the test polling the register every 7 cycles: track 0, every sector once with
  its address field (volume 254, track, sector) and data by a 6-and-2 decoder of its own, then the head stepped
  by the phases to track 1 and the same; a sector written at 224 cycles a byte (sync 280) that `to_sectors()`
  and the sequencer both read back, only it changed; 223, and a 4-cycle load, not; 225, read back. test_iie and
  the rest of Apple II mode unchanged.
- **Must fail, each seen to fail with its fault put in the card:** Apple II mode's ratio in native mode
  (test_disk2's steps and run-on cases, test_disk2_native's machine case and both write cases that read back);
  the run-on counted in cycles (`MOTOR_DELAY` native cycles, a seventh of a second: the run-on case); no
  catch-up at the old clock before a change (the steps case). After the review: the CPU's speed ignored (native
  steps always two in seven: the speeds case, 6 assertions); the remainder dropped, `rest_ = 0` (the steps
  case, and two of test_disk2_prom's write cases).
- **Measured:** `tools/check.sh quick` 206 s, all passed; test_disk2 0.06 s, test_disk2_native 0.10 s, test_iie
  7.5 s (as before). After the review: 191 s, all passed; test_disk2 0.12 s, test_disk2_native 0.02 s,
  test_disk2_prom 0.13 s, test_iie 8.2 s.

**Proposed (6c, as built, 2026-11-05)** ([`oodos/sys/disk2.s`](../oodos/sys/disk2.s), `disk2.inc`, `bios.s`,
`bdosdisk.s`, `bdosfile.s`; the plan above is what was built, and these are the details):

- **The driver** (`disk2.s`, block 61; the switches by name in `disk2.inc`, offsets from `DISK2`, so
  `backplane.inc` is unchanged): D2START selects the drive, sees whether its disk turns, and switches the motor
  on; D2TRACK seeks (recalibrating first if the head isn't known: 80 half tracks out) and reads a whole track
  into a 4 KB buffer of block 29, mapped into W1 while it works, each data field's 342 values decoded as they
  come and put together after; D2WSEC writes one sector; D2STOP switches the motor off at the end of every READ
  and WRITE, and the card keeps it turning a second (question 2). The file's sector f is physical sector
  `FILE2PHYS[f]`, DOS 3.3's interleave, the one the emulator writes a `.dsk` out with.
- **Bounded, every wait** (6a's review): a disk byte 256 polls (about 1,800 cycles, eight bytes' time), an
  address field 13,000 bytes (two turns), a track three tries. A card with no sequencer PROM (the website's)
  answers its ID and works its switches but never sets bit 7, so a read is Disk I/O after about 0.4 s, not a
  hang (`test_oodos_disk2_noprom`).
- **A track that partly reads** keeps what it read: the BIOS has each buffer's good sectors (`TBMASK`), and a
  READ of a 512-byte sector whose halves weren't both read is Disk I/O, while the rest of the track serves.
  (The plan had one bad field fail the whole track's reads.)
- **The write loop** (`WLOOP`, one page, `align = $100` on the BIOS's segment): Q6 already on from the
  write-protect sense, the first sync byte loaded by the write to Q7 on, four more 280 cycles apart, then
  `NIBBUF`'s 349 bytes (`D5 AA AD`, the field, `DE AA EB`) 224 apart; each load held 28 cycles (a whole loop of
  the sequencer's eight steps, as 6a found) before Q6 off; every delay counted to the cycle by a macro (`DLY`).
  Interrupts off (`PHP`/`SEI`, `PLP`) and the DMA controller's master enable off around it. The bytes are made
  first, from the buffer (`PRENIB`).
- **The BIOS** (`bios.s`): O: and P: (drives 14 and 15) are the card's drives 1 and 2 when cold boot finds its
  ID, apple-do with CKS 16 (`DPBD2`). The apple-do layer is 6b's, a track buffer for each of O: and P: in block
  29 ($1000 and $2000 on); a disk that had stopped turning when an access starts has its buffer emptied, and
  MEDIA (called by the BDOS, as SDMNT is) says so.
- **The BDOS:** O: and P: are slots 8 and 9: their hashes in block 29 ($3000 and $3100), their allocation
  vectors in the system block at $3000 and $3400 (BANKTEST's scratch there is unused by any drive in its test).
  At login a removable drive's 16 directory records are checksummed (`CSV`, `DSUM`); at every later select,
  if MEDIA says the disk had stopped, its directory sectors are dropped from the buffers (a directory write
  goes at once, so they are never left changed), read again from the disk and checksummed: the same, on; not,
  another disk (`MEDIACHK`): its buffers dropped unwritten, its changes counted (`DGEN`), logged in again.
- **Error 10, Media Change:** an FCB opened or made on O: or P: carries its drive's count in S1's bits 0–6
  (S2 has no bit to spare: bits 6 and 7 are the BDOS's; S1's bit 7 stays `S1_STAMPED`); close (16), the
  sequential reads and writes (20, 21) and the random ones (33, 34, 40) of an FCB whose count isn't the drive's
  return A = 10, as the Guide's file functions have it, and touch nothing.
- **Decided (2026-11-05, the owner):** O: and P: are listed at cold boot whenever the card is there, a disk in
  or not (`O: 128K   P: 128K`, the size the format's). With no disk in, or on the website with no sequencer
  PROM, they answer Disk I/O, as an empty drive does on real hardware. So the tests that had P: as "not a
  drive" now use N: (DIRTEST's SELBAD, SYSTEST's ERR and case E, ERRTEST's case P, SETDEF's chain), approved
  one by one; a sweep of every test and test program found no other.
- **Not changed:** INITDIR (6b's comment holds); `backplane.inc`. The 65C816 rules tool reads the BIOS as a
  program, which it isn't (main's own gives 46 complaints, tables read as code and calls into common memory;
  this one 68 the same way): the driver has no RMB, SMB, BBR or BBS and no absolute operand in zero page, and
  reaches the phases by an absolute,X load inside the card's page, which the W65C02S makes once.
- **Room:** block 61 8,515 of 16,384 bytes (the driver 4,278 of it, 2,486 of that its buffers and tables);
  block 60 12,138 (+362).
- **Tests:** `test_oodos_disk2` (9 cases, about 27 s; exits 77 without the owner's sequencer PROM): cpmtools's
  files read from O:; the same PIPs, ERA and REN on O: and on C: (the same image as an SD unit) leave the two
  images the same, 0 of 560 sectors different, and read back after a ^C; O: to P: and back; no disk (Disk I/O
  in 2 s) and a write-protected one (Read Only, the disk unchanged); the disk changed after the motor stopped
  (seen: `No File`), and not changed (kept); a file open across the change (MEMTEST's case O: its read and
  close 10, a new open not found, nothing of the old disk written on the new); the head where the driver
  doesn't think it is (recalibrated); a data field spoiled (the track's other sectors read, that one Disk I/O);
  tracks 0–34 through the BIOS (MEMTEST's case T, the head at 136); the motor (turning after an access, stopped
  a second after, a second access within it served from the buffer, after it read again); every load of the
  data bus from the bus, 280 then 224, the DMA master off at each and back after; an interrupt asked for mid-
  write with the I flag cleared at the driver's entry. `test_oodos_disk2_noprom` (2 cases, no fixtures): no PROM
  (Disk I/O, 0.4 s, OODOS goes on) and no card (Invalid Drive).
- **The interrupt case, strengthened:** a taken interrupt only makes one load late by its handler's time, and
  the disk reads a late byte back (6a: a 0 bit or two more), so the readback alone passed with the SEI taken
  out. The case also times every load of every field (5 at 280, 349 at 224), which the fault fails (26 late).
- **Must fail, each seen with its fault put in** (`OODOS_SYS_DIR`): the write loop a cycle long (the timed
  loads: 255 late; the readback passes, as 6a found); the SEI taken out (the interrupt case's loads); the
  checksums off (the changed floppy seen as the old one; the motor case's read after a stop); Q6 held 6 cycles
  (four cases: nothing written).
- **Measured:** 20 KB from O: to M: 3.5 s; 5 KB from M: to O: 2.8 s, about 40 ms a sector waiting for its
  address field (the skew puts a 512-byte sector's halves three physical sectors apart). `tools/check.sh quick`
  198 s, all passed; test_oodos_disk2 28.7 s and test_oodos_disk2_noprom 0.6 s in the parallel run, so no split.
- **The books:** the drive tables of both Guides name O: and P:, and after the review the approved entries
  (below).

**After 6c's review (2026-11-05, the owner's decisions; main merged in first, G2 and sound step 1):**

- **F1, the checksums kept by the BDOS's own writes:** DIRPUT sums the record it wrote into CSV (`CSVPUT`), as
  CP/M 3 keeps its checksums, so a make, a close or an erase is never taken for another disk.
- **F2, the card's one motor:** the driver keeps the drive the card last selected (`D2LAST`); the other has
  stopped, whatever the data register does.
- **The owner's rule for the run-on:** a drive whose own last access was in the RTC's same second (its second,
  minute, hour and date; a second's edge counts as older, so it errs toward checking) is taken as unchanged, with
  no directory re-read at a select (`RECENT`, the BIOS's `D2STAMP`); one idle a second or more is checked. The RTC,
  not the card or a cycle count: the bridge has it in registers, and the BIOS needs no counter of its own. **The
  trade-off, recorded in the User's Guide:** a disk swapped within a second of its drive's last access, in the
  middle of an O:-P: copy, can take a few data sectors before the next directory write sees the change.
- **F3, write-backs (the owner chose the lazy way, with close):** O:'s and P:'s changed buffers wait for a close,
  48, 13, 37, 75, the warm boot or an eviction. Every write-back not preceded by its own drive's check in the
  same call is checked first (WBACK's guard, `DISKCHK`: MEDIA, then the directory read past the buffers): a
  changed disk drops the data, never receives it. A write-back at every call's end, then at another drive's
  select, were tried first and dropped: PIP writes a record a call and alternates drives, so each wrote every
  sector four times (M:→O: 5 KB 9.3 s).
- **F4:** ADDR reads the epilogue's AA before a write starts, so the write begins past it.
- **F5:** the NMI handler turns the Disk II's write mode and motor off and puts the DMA master back (`D2WRIT`,
  `D2DMAS`) before the monitor.
- **F6:** the first directory write of each call reads the directory past every buffer (`VERIFYD`, the BIOS's
  directory-track buffer forgotten) and compares it; another disk ends the call with 10, nothing written.
- **F7:** a READ whose sector wasn't read has the track read again once, and the miss isn't kept.
- **F8:** every byte ADDR and RDATA read is counted, a run of D5s too.
- **F11, F12:** the comments (RBYTE's poll 11 cycles, 256 of them about 2,800; disk2.inc allows an absolute,X in
  the card's page); D2WSEC's interrupts off from the search on, the flag back on every way out.
- **Each drive's directory track has a buffer of its own** (block 30, $0000 and $1000), so a check of the
  directory doesn't evict the data track; the BIOS keeps a drive's buffers the BDOS checked in the call (`VOUCH`).
  Block 29 still has the SD unit's and O:'s and P:'s data buffers and the hashes; block 30, spare before, has these.
- **Tests:** `test_oodos_disk2` (12 cases) gains every address field's DE AA after PIPs, a Break mid-write (15 of
  the track's 16 sectors kept, Q7 and the motor off, DMA as it was), a sector read again once the disk reads, and
  a track of D5s (Disk I/O in 1.6 s); `test_oodos_disk2_media` (new, 5 cases; the rig shared in `disk2_rig.h`):
  the BDOS's own directory writes not a change (MEMTEST's K, the review's nochange.com: MK, WR, WR2, CL all 00,
  DGEN as it was), a changed P: seen however recently O: ran (the review's case), a change under a pending write
  (MEMTEST's V, dirty.com: the new disk unchanged after the warm boot), a closed file on the image with no warm
  boot since (MEMTEST's C), and a swap within the second seen at the first directory write (nothing written).
- **Must fail, each seen with its fault put in:** CSVPUT off (2 media cases and 3 others); the other drive's
  register trusted (the P: case); WBACK's guard off (the pending-write case); no write-back at close (the close
  case); the AA not read (the epilogue case); the NMI leaving the card (the Break case); no check before the first
  directory write (the within-the-second case); a missing sector kept (the re-read case); the D5s uncounted (the
  D5 case, which hangs to its 20 s: its track had first to be put in step with sync bytes, or a reader took the
  D5s as ABs and the fault's path was never run).
- **Room:** block 60 12,535 of 16,384 bytes, block 61 8,771; blocks 29 and 30 as above.
- **Measured** (emulated): M:→O: 5 KB 3.8 s, O:→M: 20 KB 3.0 s, O:→P: 20 KB 22.0 s (94.6 s before the owner's
  rule, 158 s with a write-back at every call's end).

**After 6c's second review (2026-11-06, fixed as the review recommended):**

- **R2F1:** PURGED has its own drive (`PDRV`). It shared FDRV with FLUSHD, so a guard that found another disk in
  O: inside the warm boot's 48 (FDRV 0, every drive) left FLUSHD writing back O:'s buffers only: C:'s record was
  skipped, and dropped by the 13 after.
- **R2F2:** the guard's check (`DISKVER`, VERIFYD's caller) gives its result as a code, not an error: 1 another
  disk (GONE done), 2 not read. For a write-back, not read is a write that failed, as WBM says: kept and marked,
  or dropped (13, 37), or raised. A select's or a directory write's still says Disk I/O (`VDIO`). And an eviction
  whose victim is another removable drive's buffer that won't write marks it and takes another, so P:'s missing
  disk doesn't fail O:'s writes.
- **R2F3:** `RECENT` trusts a drive only once the RTC has been seen to go forward (`TICKED`; `NOW` keeps the
  latest time read, `STAMPH`): after the cold boot, and after the clock goes back, when every drive's last access
  is forgotten too, so it can't match by chance when the clock comes round to it again. The month is compared as
  well (`D2STAMP` 8 bytes a drive, 5 used). A clock that never ticks has every select with the motor stopped read
  the directory.
- **R2F4:** a close, 48, 13, 37, 75 and the warm boot (WFLUSH, WBM bit 5) read the drive's directory past the
  buffers before writing any of its data, unless this call already has (`DVERD`), however recent the last access;
  an eviction reads it too unless the call has already checked that drive (`CHKD`, or `DVERD`): it never takes
  the motor or the clock's word, as a select does, but a copy's evictions come in calls whose select has checked
  the drive, so they don't read the directory again for every buffer the copy fills. A close whose
  flush found another disk returns 10 (GENCHK after FLUSHD; the first directory write's check would say 10 anyway,
  but FIND and MAKEENT no longer run on the old disk's state).
- **R2F5:** the Programmer's Guide gives 10 for 19, 22, 23, 30, 99, 100 and 103 too, with the warning that a test
  for `$FF` takes it for success.
- **R2F6, tests:** `test_oodos_disk2_media` 5 → 16 cases, through MEMTEST's new cases U (O:'s and C:'s records
  waiting), R (U, then 13), E (P:'s record waiting, then 160 records to O:), X (19 and 22 after a change within the
  second) and Q (O:'s directory through the BIOS by 50).
- **Must fail, each seen with its fault put in the fixed code:** PURGED on FDRV (the two swap cases); a missing
  disk raised (the disk-out and eviction cases); every eviction raised (the eviction case); no TICKED (the frozen
  clock and set-back cases); no forgetting at a step back (the set-back case); no month (the month case); a flush
  not reading first (the close-within-the-second case); VOUCH not cleared at a call's start (the BIOS read case).
  The GENCHK after a close's flush has none: the directory write's own check returns 10 without it.

**After 6c's third review (2026-11-06, the owner's decisions):**

- **R3F1:** a drive whose directory couldn't be read (no disk) is tried once a call (`UNRD`, the BDOS's, beside
  `CHKD`, cleared at each call's start), and the failure marks every changed buffer of the drive, so evictions in
  later calls pass them over. Each try had taken about 1.8 s (emulated), once for each waiting buffer: with 16 of
  P:'s waiting and its disk out, 160 records to O: then took 38.4 s and the warm boot 29.7 s more. Now 10.6 s and
  1.9 s (13.8 s and 0.0 s with P: there); 13.2 s when P:'s buffers alternate in age with O:'s (26.2 s without the
  marking).
- **R3F2:** a clock that ticked once and then stalled stayed trusted. `MEDIA` now looks at the card's drive
  (`D2LAST`) when RECENT trusts: if its motor has stopped (so its access was more than a second ago) while its
  stamp is still this second, the clock has stalled, and nothing is trusted until it goes forward again
  (`UNTICK`, which forgets every drive's last access, as a step back does). The card's own run-on timer is the
  second clock; no cycle counting.
- **R3F3, not changed:** a disk whose directory is byte for byte the old one's is taken as the same disk (CP/M
  3's checksums have the same limit): the BDOS's and the BIOS's buffers of the old disk are read, and its waiting
  data written onto the new one. It needs two disks with identical directories and different data. Recorded in
  the books.
- **R3F5:** an eviction whose victim, another removable drive's buffer, fails with Read Only (a write-protected
  disk) says it, on the call that wanted the buffer, as WBACKX would; only Disk I/O (kept, marked) goes on to
  another victim.
- **R3F4, R3F6, R3F8 (books):** 15 with access stamps returns 10 too; an eviction's check is said as it is; X
  isn't the records done after a 10 (the program opens the file again). R3F7: TICKED's comment on its line.
- **Tests:** `test_oodos_disk2_media` 16 → 21 cases, MEMTEST's new F (64 records waiting on P:) and G (the same,
  alternating with O:'s): the two timing cases (each held to 3 s over the no-disk-waiting time), round 2's
  stalled-clock reproduction, a stalled clock inside a program (MEMTEST's O: the read is 10), and a
  write-protected disk found by an eviction (OFAIL 02: the record's buffer and its block's other, zeroed sector).
- **Must fail, each seen with its fault put in:** no `UNRD` (both timing cases: the warm boot 29.7 s); no
  marking (the alternating case, 26.2 s); MEDIA's stall check gone (both stalled-clock cases); the eviction's
  Read Only ignored (the write-protect case).

**After 6c's fourth review (2026-11-06, the owner's decisions):**

- **R4F1:** the stall check is in `RECENT` itself, so the BIOS's own use of it (APPLEIO, a read by 50 with no
  BDOS select) gets it too; APPLEIO asks RECENT before turning the motor on (`ARECENT`), while the card's drive is
  as it was. MEDIA's copy is gone.
- **R4F2, accepted, not fixed:** with a stalled clock, a change on one drive goes unseen while the other drive's
  motor turns: the check can only see a stall through the card's drive, and an O:-P:-O: within one real second
  looks the same. It would need a second time source; a DS3231 on Vcc doesn't stall.
- **R4F4:** a Read Only found by a write-back (`WBACK`'s, any drive) drops every changed buffer of the drive at
  once (it is read-only now: none can be written), so a write-protected P: is said once, not once a buffer. An
  eviction's Read Only for another drive is held (`PERR`, `PEDRV`) and said at the call's end (`RETAX`), the
  function's work done, so a close, a delete or a rename of another drive is never stopped part-way by it.
- **R4F3, R4F5, R4F6 (books and comments):** appendix A and 20's entry say a read can get Read Only for O: or P:
  this way; the Programmer's Guide says the clock is distrusted after a stall, and that an eviction finding no
  disk says nothing; the User's Guide says waiting data is written if the disk is back in time; RECENT's comment
  and question 2 give the run-on's margin.
- **Tests:** `test_oodos_disk2_media` 21 → 23 cases: the review's BIOS read with a stalled clock (MEMTEST's Q:
  QB E5), and 64 records waiting on a write-protected P: (MEMTEST's F: OFAIL 01, BIG.DAT whole). The R3F5 case
  now expects one failure (OFAIL 01), as R4F4 decided.
- **Must fail, each seen with its fault put in:** APPLEIO's RECENT without the stall check (the stalled BIOS
  read); RECENT asked after the motor-on, as before (the stalled BIOS read: QB 00; the emulated drive turns at
  full speed the moment its motor is on, as a real one's read pulses start at once, so SPIN sees it turning and
  the stall goes unseen: the order is load-bearing); the drop-all gone (the 64-record case); the eviction's Read Only raised at once, not held (the
  64-record case: the failing write's record lost). Round 3's code fails all three cases.

**After 6c's fifth review (2026-11-06, the owner's decisions):**

- **R5F1:** an eviction's write-back left EDRV at the victim's drive, so a later error in the call named P:, and
  the held Read Only replaced the call's own error (a make of an existing file on O: returned `FF 02`, File
  Exists lost). GETBUF now puts the call's EDRV back (`GEDRV`, `VEDRV`). When a held Read Only meets the call's
  own error, DERR returns the call's own, with its drive; in display and default mode it says the held one first,
  so the default mode's end never leaves P:'s loss unsaid.
- **R5F2, R5F3, R5F5 (books):** a write that gets the held Read Only has its record written and CR moved on (21,
  34, 40, appendix A); the held case is the other floppy's buffer only (the call's own drive's is said at once),
  and the order when the call fails too; the stall wording names the card's drive; the User's Guide says a
  write-protected floppy's waiting data is dropped, and the tab off doesn't bring it back.
- **Tests:** `test_oodos_disk2_media` 23 → 24: the review's heldx as MEMTEST's H, in the three error modes.
- **Must fail:** no EDRV put back (ERRDRV 0F); the held Read Only not said first (FF 02); round 4's code.
- **The books** (approved): the Programmer's Guide's chapter 3 (ten drives, removable drives), 14's and 37's
  drives, 16's, 20's, 21's, 33's, 34's and 40's Media Change, 31's DPB, chapter 2's S1, appendix A's 10, appendix
  G's choices; the User's Guide's changing a floppy and O:'s and P:'s Disk I/O and Read Only.


**Proposed (6d, as built, 2026-11-06)** (`oodos/sys/bdosmem.s`, `bios.s`, `oodos/utils/dos33.s`,
`oodos/tools/dos33.py`, `make_card.sh`; the plan above is what was built, and these are the details):

- **The commits,** on `feat/oodos-dos33`: function 76 (BDOS and BIOS); DOS33.COM and dos33.py with the card's part
  in `make_card.sh`; `test_oodos_dos33` and MEMTEST's 76 cases; `test_oodos_dos33_iie`; `check_tools.py`'s dos33.py
  checks; the books (`bdos-76`, `util-dos33`, the appendices) and this record; the version 0.4 with every place it is
  checked; the cards rebuilt.
- **Function 76** (`F_SECT`, bdosmem.s): the 6-byte block read by PGET; an SD drive's units read again first
  (`RESCAN`), since a unit mounted by the card's own MOUNT isn't in DRVAPL until they are (the first version
  answered "not an Apple disk" for C: until a CP/M select had read them); O: and P: by SELDSK (no Disk II, no DPH).
  A logged-in drive is flushed (a buffer that won't write: A = 1, nothing dropped), then dropped and logged out by
  `DROPD`, which 74 now shares. The BIOS is called through its jump table as for any sector, with `RAWF` set:
  `APPLE1` then moves one 256-byte half (`NHALF` 1), the file's own sector (no skew), sectors 0-15, through the
  same track buffers, write-through as before; a CP/M drive's `DISKIO` refuses it (`$FF`). The 256 bytes go between
  SECBUF and the program by one MOVE (`R256`); memory MOVE refuses ends the function as PIN and POUT do.
- **DOS33.COM** (ours): the forms as planned. Decided here: GET replaces the CP/M file; PUT's default is T for a
  `.TXT` file and B for any other; a B file's length is the CP/M file's records × 128, at most 511 records (DOS's
  length is a word); an R GET writes a file's never-written sectors as zeros when data follows them (dos33.py's
  `raw` the same). Allocation as DOS 3.3 goes: from track 18 out to 34, then 16 down to 0, each track from sector
  15 down, so DOS's own next SAVE finds the sectors it expects free. A PUT reads the VTOC, finds a free entry,
  writes the data and the lists, then the entry and the VTOC last: a Disk full in the middle leaves the disk as it
  was (sectors written are free in the bitmap). Its messages are ours. The catalog's chain is read at most 255
  sectors, so a looped one can't hang it.
- **dos33.py** (ours, from the format's layout): `new`, `ls`, `put`, `get`, `era`, `lock`, `check` (every sector one
  owner's, or free in the bitmap; the chains without a loop; each file's sector count). Its images have tracks 0-2
  marked used and zeros. `/DISKS/DOS33.DSK` on the card is its image with a README of our words; the suite checks
  tracks 0-2 are zeros (no DOS image, no Apple bytes).
- **Tests:** `test_oodos_dos33` (13 cases, about 8 s; dos33.py run by the suite through Python): the card's image, the
  catalog line for line against dos33.py's `ls`, GET in T, B, R and the default, PUT in T and B (a name with blanks),
  ERA and a locked file, files of two and three track/sector lists, every message, a CP/M disk refused, a full disk (nothing changed) and the catalog past its
  first sector and full (105), O: in the drive, and 76 through MEMTEST's S (its refusals, a read, a write), L (a
  logged-in CP/M drive's buffers dropped: the next 17 finds a name 76 wrote) and J (2 on a write-protected
  floppy); dos33.py's check after each. `test_oodos_dos33_iie` (4 s): DOS33 PUTs a T and a B file; the IIe boots
  the owner's DOS 3.3 disk from the fixtures with the image in drive 2: `CATALOG,D2` lists both with DOS33's sector
  counts, `VERIFY` passes, `BLOAD` puts the bytes at $4000, a program READs the text's lines; `BSAVE` writes 256
  bytes, after which `BLOAD` of DOS33's file still gives its bytes and dos33.py checks DOS's disk clean; DOS33 GET
  takes DOS's file back byte for byte. `check_tools.py`: dos33.py's images, listing, extraction, its 64 KB B limit,
  and its check finding a spoiled bitmap and a looped list.
- **Must fail, each seen with its fault put in:** the VTOC bitmap not updated (`test_oodos_dos33`: dos33.py's check,
  "used by NOTE and NOTE", in 3 cases; with DOS 3.3: DOS's BSAVE took DOS33's sectors, so `BLOAD` gave other bytes,
  and READ failed); text's bit 7 left clear (`test_oodos_dos33`'s PUT case; with DOS 3.3: READ didn't get the lines).
- **Sizes against the plan:** the BDOS +266 bytes (block 60 12,768 → 13,034; the plan said 100-150: the parameter
  checks, the rescan, the flush and drop, and the BIOS calls); the BIOS +41 bytes, inside its page alignment;
  DOS33.COM 6,407 bytes (the plan 4-6 KB): its own code about 2.3 KB, `cmd.inc` and `util.inc` about 4 KB as in
  every utility (TYPE.COM is 4,147).
- **Not tested:** the real Disk II (the emulator's drive only); DOS 3.3 on a floppy DOS33 wrote in O: and then
  moved to the IIe (the IIe case uses the SD image's sectors, the same bytes); ProDOS (not in phase 6); a file of
  more than one track/sector list read by DOS 3.3 itself (DOS33 and dos33.py agree on them).

**After 6d's first review (2026-11-06, the owner's decisions, all 15 as recommended):**

- **F1-F3, a bad file:** GET and ERA read the file's lists through first (`chkfile`): at most five (a disk's 560
  sectors), each list and each data sector on tracks 1-34 but 17, sectors 0-15, a pair of track 0 none. Anything
  else is `Bad file`, before a sector is written or freed: a looped list hung GET and ERA, and a pair on track 63
  made ERA set bits past the bitmap (the VTOC then said 17 sectors a track).
- **F4:** GET writes `NAME.$$$`, closes it, then deletes the old file and renames it; any failure (`failsay`)
  closes and deletes the `$$$` file and leaves the old one. **F5:** GET and PUT refuse a CP/M file on the Apple
  drive itself (`samedrv`).
- **F6:** PUT writes the VTOC before the catalog entry: a failure between leaks sectors instead of leaving a file
  on free ones. **F7:** 76 refuses a write to a read-only drive (28, SET `[RO]`) with 2, and keeps the drive
  read-only when it logs it out (`RRO`).
- **F10:** GET's T ends at a stored 0, tested before bit 7 is cleared; PUT's T makes a lone LF a CR as CR LF, and
  drops NULs (dos33.py's `text` the same). **F11:** the catalog shows a control character as `^G` and DEL as `?`;
  the CCP's capitals keep a name with small letters out of GET's and ERA's reach (said in the User's Guide).
- **F12, the Since lines:** every entry 6a-6d changed says OODOS 0.4 (the functions 14, 15, 16, 19-23, 30, 31,
  33, 34, 37, 40, 72, 74, 99, 100, 103, PIP, and the new 76 and DOS33), as the rule is: the version that first had
  the entry as it is now. `functions.json` regenerated.
- **F13:** the warm boot clears `RAWF`. **F14:** 76 reads the SD units again only when the drive isn't known as an
  Apple disk yet (it did at every call, an IDENTIFY a sector); O: and P: are selected once (no DPH: not an Apple
  disk); one helper sets BW1 and BW2 either way. The BIOS calls stay beside DISKOP's (its buffer is a BCB, 76's
  SECBUF). The BDOS is 13,077 bytes in block 60 (+309 on main: F7's read-only bit took what F14 saved).
- **F15:** the books' words for `Not a DOS 3.3 disk` (the VTOC) and a full disk (the sectors written stay free);
  PUT keeps VTOC +$30/$31 (the last track allocated, the direction) as DOS does; a catalog chained to itself is
  `Bad catalog` (and the 255-sector bound says so too); a CP/M read error is `CP/M read error.`, not the end;
  the type letter is the code's, `?` for others, as dos33.py's.
- **F9, the oracles:** dos33.py's check holds each list's +5 (the file's sector it starts at: 122 × k). With DOS
  3.3: a B file of 32,000 bytes (two lists) BLOADs whole; ten entries, the last in the catalog's second sector;
  a file DOS33 ERAs is gone from CATALOG, and DOS's BSAVE takes its entry; VERIFY of all nine after DOS's BSAVE and
  WRITE; DOS LOCKs a file and DOS33 sees `*` and refuses ERA; DOS WRITEs a text file and DOS33 GETs it as T.
- **Tests:** `test_oodos_dos33` 13 → 21 cases, its two O: cases skipping without the PROM, the rest running on the
  SD card alone (F8): looped lists, pairs past the disk and on track 17, a failed GET keeping the old file (a bad
  pair, and the CP/M disk filling part-way), the Apple drive as the CP/M side, a read-only drive, the text rules,
  the catalog's control characters, types, VTOC +$30/$31 and a looped catalog. `check_tools.py` 138.
- **Must fail, each seen with its fault put in:** no list bound (the looped-list case); no pair check (the
  pair-past-the-disk case); the old file deleted before the GET (the CP/M-disk-full case); no same-drive check;
  T ending after bit 7 is cleared, and a lone LF dropped (the text case); 76 ignoring a read-only drive (the
  read-only case). (Each utility fault's run also failed the read-only case, for want of SET.COM in its build.)
- **DOS33.COM** is 6,903 bytes now (the checks, the `$$$` file, the messages).

**After 6d's second review (2026-11-06, the owner's decisions, all 6 as recommended):**

- **R2F1:** round 1's bound of five lists refused the sparse files DOS 3.3 itself writes (`OPEN RND,L250`,
  `WRITE RND,R700`: six lists, five of them empty). `chkfile` now holds each list to its +5 (the k-th starts at
  the file's sector 122 × k, 16 bits): a loop comes back to a list with the wrong one, so it is still found,
  and past 16 bits is refused. The pair checks stay. dos33.py takes lists by the same rule.
- **R2F2, R2F3:** before reading anything GET refuses a `.$$$` target, a read-only CP/M drive (29), a read-only
  target file, and a `NAME.$$$` already there (the user's, not deleted). Its CP/M calls go through `cpmres`:
  0 on; 10 `The CP/M disk was changed.`; an error the BDOS said `GET stopped.`; `$FF` the call's own message
  (directory full, not closed, not renamed); a write's 1 and 2. Make, close and rename took only `$FF` for a
  failure, so a 10 passed as success (the same in the CCP's ERASE and RENAME and in PIP is todo item 17).
  A rename that fails leaves the new data as `NAME.$$$` and says so.
- **R2F4:** 76 reads the units again before every write, and a write to a unit that changed since they were
  last read is refused, `$FF` (the BIOS keeps a 140 KB unit's DRVAPL across a swap to another 140 KB image, so
  the change bit, DRVCHG, is the test, taken before CHANGED clears it). Reads keep F14's cache.
- **R2F5, R2F6 (books):** 0, 13, 48 and 75 say OODOS 0.4 and the check-first write-back on O: and P:, as 37
  does; 72's table says (0, 4); 48's Read Only drops all the drive's changed buffers; 76's bullets rewrapped, and
  the read-only bit outliving the logout. **The rule for the Since lines:** a change in behaviour that many
  functions share through one routine bumps the entries whose text describes it (the write-backs: 0, 13, 37,
  48, 75); the functions that only pass through it (17, 18, 46 and the like, which can now meet a held Read
  Only or accept O: and P:) keep their line, and the books say it once, in appendix A and chapter 3.
- **Tests:** `test_oodos_dos33` 21 → 25: a sparse file of six lists (GET, ERA, dos33.py's check), GET's four
  refusals, 76's write after a remount behind OODOS's back ($FF, MEMTEST's I), and a disk change on O: under
  GET with the clock held, so the close is the call that returns 10. `test_oodos_dos33_iie` 1 → 2: DOS 3.3
  writes the sparse file far out; DOS33 GETs its sectors and ERAs it; DOS then BSAVEs and VERIFYs on that disk.
- **Must fail, each seen with its fault put in:** five lists again (the sparse cases, in both suites); each of
  GET's four refusals left out (its case); 76's write without the change test (the remount case); make, close
  and rename taking only `$FF` (the disk-change case: `not renamed` instead of `changed`), and the close alone
  the same.
- **Sizes:** the BDOS 13,117 bytes in block 60 (+349 on main); DOS33.COM 7,443 bytes.

**After 6d's third review (2026-11-06, the owner's decisions, all 9):**

- **R3F1, R3F2, R3F7, the mount count:** the BIOS counts each SD unit's mount changes (`MNTGEN`), in UNITS
  whenever a unit's entry changed, however the units were read (a select, 13, 37, 74, 76), and nothing clears
  it; UNITS empties the SD track buffer only then. 76 reads the units at a program's first call on a drive
  (`RSEEN`, cleared by the warm boot) and keeps the count (`RGEN`); a write is refused, `$FF`, if the count has
  moved since. So a swap at A> before the program is taken as the disk (round 2's change bit refused the first
  PUT), and a swap in the program is refused even when another call read the units in between (the change bit
  was used up by it). DOS33 then says `The Apple disk was changed.` A read after a swap mid-program reads the new
  image unchecked (the books say so).
- **Not as asked, one conflict:** the owner asked to drop the read of the units before each write and to have
  R2F4's probe refused. Only that read can see a swap with no other call between (the card has no signal for a
  remounted unit). Kept, so the probe is refused: PUT of 32 KB 1.40 s (1.62 s with the track buffer emptied at
  each read, 1.15 s with no read at all, when that probe's write would go in). The owner's call.
- **R3F3, GET's sparse walk:** T ends at the first sector never written; B fills one with zeros only inside its
  length, and a B file whose first sector was never written gives nothing (`bleft` starts 0); R writes zeros
  only up to the last sector written; the zeros owed stop at once when `done` is set; GET polls ^C at each list
  and each owed sector (CM_PAGE, CM_BRK) and `dbreak` deletes its `$$$` file. A chain of 528 lists with one
  data pair: GET T 1.0 s, a B file whose length ends inside the owed zeros 2.1 s.
- **R3F4:** a 10 clears `gtemp` first: the `$$$` file is the old disk's, and nothing on the new disk is touched
  (it deleted the new disk's own `NAME.$$$`). The User's Guide says the old disk keeps it.
- **R3F5, R3F6:** cpmok opens the target first (which logs its drive in, so a unit mounted read-only is
  read-only for the vector check after), takes a file with f8' (user 0's, from another user) as not there,
  and refuses one with f7' (write-protected by a password); the `NAME.$$$` probe skips f8' too.
- **R3F8:** the duplicated `CP/M directory full.`, the dead `setmsg`, the drive-bit loop (DRVBIT) and RESCAN's
  copied start gone; the mount count added more than they saved. **R3F9:** the books as above.
- **Tests:** `test_oodos_dos33` 25 → 32: a swap at A> then PUT; MEMTEST's I with no call, with B: and A:
  selected, and with 37 on B: between (all `$FF`, nothing changed); PUT's time; the 528-list chain (T, B, a ^C in
  R, ERA, the loop); a B length inside the owed zeros; B and R holes; the new floppy's own `BIG.$$$` kept after a
  change; user 5 with user 0's SYS files; a unit mounted read-only; a password write-protected target.
- **Must fail, each seen with its fault put in:** the count not compared (the MEMTEST I cases); T not ending at a
  hole (the 528-list case); the owed zeros not stopping at `done` (the B-length case: 30 s); 10 still deleting
  the `$$$` (the change case); f8' ignored (the user-5 case).
- **Sizes:** the BDOS 13,130 bytes (+362 on main); DOS33.COM 7,570 bytes: CODE $19CE with `cmd.inc` and
  `util.inc` (about 4 KB of it, as in every utility), RODATA $3B4. The plan's 4-6 KB didn't count the includes.

**After 6d's fourth review (2026-11-06, the owner's decisions; merged after it):**

- **The read of the units before each 76 write stays** (PUT of 32 KB 1.40 s; R2F4's probe refused).
- **R4F1:** gbyte set `done` with INC, so GET's owed zeros, which call it once a byte, wrapped it to 0 after 256
  when a B length ended exactly on a sector's end (L 252 or 508: 137 s); it is set to 1 now, so the zeros stop
  at the length at once (2.1 s for the 526-list case).
- **R4F2:** a B file whose stored length runs past its last sector written gets zeros to the length, so the CP/M
  file is what the address line says ("1000 bytes": 1,000, in 8 records).
- **R4F3:** cpmres's `CP/M error.` is said (it fell into `dbreak`). **R4F4:** BINIT empties every track buffer at
  the cold boot.
- **Tests:** `test_oodos_dos33` 32 → 34: L 252 and 508 on a sector's end with a time bound, and a B length past
  its last sector. **Must fail:** INC again (both lengths, 30 s, the bound); no zeros past the last sector (the
  1,000-byte case).
- **Sizes:** the BDOS 13,130 bytes; the BIOS 9,027 in block 61 (BINIT's loop took it past a page); DOS33.COM
  7,604 bytes.

**Error 10 in the CCP and PIP (todo item 17, 2026-11-06):** the CCP's ERASE and RENAME and PIP's make, close
and rename took only `$FF` for a failure, so a 10 (O:'s or P:'s floppy changed within the second, found at the
first directory write) passed as success: ERASE and RENAME said nothing and did nothing. Each now takes 0 for
success, `$FF` as before, and 10 as `Media Change` (the CCP; TYPE's read too, which failed silently) or `MEDIA
CHANGE - NAME` (PIP: its make, close, rename, the old file's delete, the attributes, the password's 103, a write,
and a source's open and read). PIP leaves its `$$$` file alone then: it is the old floppy's. Tested by
`test_oodos_err10`: ERASE, RENAME and a PIP copy with O:'s disk changed within the second, TYPE's after a page;
each fails against main's CCP and PIP. PIP's make check alone can't fail on its own: a make that returned 10 is
followed by a write that does too. Left as they are (each can't give 10, or already fails with a message): the
CCP's loader open (10 only with access stamps; its read then fails, `READ ERROR`), TYPE's open (the same, its
read then says it), the searches (17, 18: no 10), PIP's V read-back (a 10 there is `VERIFY ERROR`), PIP's
cleanup close and delete of its `$$$` file (results not used by design).

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
- **The demo card** (2026-11-03): `make_card.sh` makes a second card in the same run, `sd/demo.sparse`, which the
  WebUI puts in its SD slot at every visit, so a visitor's power-on boots OODOS: the same layout, A: only OODOS.SYS,
  HELLO.COM, the utilities and a README of what to try, B: empty, nothing of the tests'. Its files are stamped with
  the date of the last commit to what goes on it (the `oodos/` sources make_card copies in, and `backplane.inc`;
  not oodos/'s tests or README), not the test card's fixed moment, so a visitor sees when it was made; the card is
  committed after its sources, so the same commit still makes the same card. `test_oodos_boot` checks it
  is what today's sources build and holds nothing else; `test_oodos_e2e` boots it from power-on and runs its
  README's examples.
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
    `interop.sh` fail on any Error or Warning line it prints (`oodos/test/cpmtools.sh`). **cpmtools 2.23's fsck.cpm
    compares the stamps in an SFCB's first slot crosswise.** For a file entry E with E mod 4 = 0 it warns
    "Modification date earlier than creation date (extent=S/E)" when the update is on a later day than the
    creation, and says nothing when it's earlier; the other slots are compared right. `oodos/test/sfcb.py` reads
    every file entry's stamps itself and fails one updated before it was made. Until 2026-11-03 that warning was
    then let through, for exactly the first-slot entries sfcb.py found a day or more apart and in order. **Since
    2026-11-03 the tests' fsck.cpm is fixed** (`oodos/test/fsck_fixed.sh`, `cpmtools-fsck-stamps.patch`): the
    owner asked, once the source showed why. `fsck.cpm.c` takes each day's low byte from the other stamp for the
    first slot only; every cpmtools to date is 2.23 (Debian bookworm 2.23-4, trixie and sid 2.23-7, upstream), and
    no report of it was found, so the patch is offered upstream. Nothing is let through now. The OO-1's test file
    is made a day before it's updated, in a first slot, so the fixed fsck.cpm must pass the very case.
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
- **`test_oodos_e2e`, in ctest,** boots the committed card as its owner would: power on with the card in, the BIOS
  boots it after the splash (BIOS 0.10), OOLDR, OODOS and `A>`; and from the monitor with `C80CR`; `hello world` runs HELLO.COM from A: through the CCP's reader, with its tail, its image byte for byte
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
    zeros, 40, the size and 36, delete and the free space back; no entry written by a close or an extent change
    while the drive has data not written back; and the BDOS's cycles.
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
  - **STAMPTEST** on M: and A:: every SFCB byte checked against the clock the test set at each step: creation and
    update at make, which a write after it and the close keep (a file closed unwritten keeps make's too); the
    update at the first write after an open, which a later write and the close keep (from 2026-11-04: the close's
    before); access at open on M:
    with the label's mode made 0x61 behind the BDOS's back, none on A: (0x31), and none at extent 1 or on a
    read-only drive.
  - **LOGINTEST:** A:'s login reads its 64 directory sectors once and a second select none; M:'s 16; an entry put in
    M:'s directory behind the BDOS's back isn't seen until 37 logs it out; an XFCB renamed and deleted with its file
    (f5': it alone); 4 records written and not closed are on M: only after the warm boot, whose 98 frees their block.
  - **RULETEST** (2026-10-27, the review's findings, each against the Guide): on M:, the directory codes (0 but for
    the searches); open with CR `$FF` giving the byte count, f6' not; S2 left set taken as 0 by open and search;
    make clearing f1', f6', t1', t2' and S2 in the FCB and the entry (then writable), and File Exists for a file
    there in another extent; two FCBs with different blocks for one record: the second's write at CR 128 and read
    fail (1) with CR 80 and EX 0 kept, its random write 3 with CR the record's, nothing merged into it or (after the
    sector's next write) into the entry on the disk; truncate to four records where the file has no data refused,
    then to one where it has. On B: made read-only behind the BDOS's back: delete and truncate fail (Read Only)
    with the free space as it was. On B: with the card busy as A: logs in: A: Invalid Drive, B:'s close after
    still written; another image as B:'s unit: B: logged out at the next rescan, then read from the new one, and
    a record left in a buffer of B:'s at the swap dropped (the card byte for byte as at the swap); a rescan with
    nothing changed keeps B:. The same record dropped by 37 of B: after the swap, and by the warm boot's 48 after a
    program's own SELDSK, and by the warm boot's own rescan with none. The card's writes failing under a close:
    each close fails (Disk I/O), BIG.DAT's 160 records still read meanwhile, and once the card writes again the
    close writes the record and the entry; 37 with the card failing drops the buffer (Disk I/O) and the next 37
    has nothing to write. One sector that won't write: its file's close fails, and again, while a file only read
    and a file whose data is on the card close; after the warm boot B:'s closes work and the sector is never
    written. Two changed buffers, the first failing: 48 writes the second and returns Disk I/O, 48 with `$FF`
    drops both. A closed file's record written again in place and not closed, the card failing it at the warm
    boot: the console says `OODOS Error On B: Disk I/O`, the buffer is kept, and the next warm boot, the card
    writing, puts the record on the card. What the comparison
    with the model found, a test each: 35 of no file zeroing R0-R2; a file's second entry's SFCB slot 0 on M:
    (stamps only in extent zero's); after a read found no entry for extent 2, with the directory full, a write's
    1 (21) or 5 (34, 40) at once; and on an `oodos-32m` A: (EXM 3) after M: (EXM 1) logged in, 384 records in one
    entry, not two. And the CCP: the user 5 kept across `HELLO` and `NOSUCH?`.
- **The CCP** loads through the BDOS in every run (`hello` in `test_oodos_e2e`, from the card, with the bytes past
  its image untouched); a card that stays busy as it logs A: in is `READ ERROR`, and the next load works.
- **Interoperability:** `test_oodos_card` boots the committed card in a Session and runs **WRITEOO1** (on the card)
  at the prompt: `0:OO1.BIN` written by the real BDOS, the clock set by function 104 to its two stamps (and, from
  2026-11-04, later again before its second write and close, which don't stamp); its
  entries are now 16 and 17 (WRITEOO1.COM took 14), the first in an SFCB's first slot; 66 sectors change (the
  directory's second and 65 of records). Its stamps are in entry 16's slot, entry 17's is 0 (the stamps rule above),
  as test_oodos_card, interop.sh and `sfcb.py` check: a file's first entry stamped and in order, a later extent's
  slot 0 or (cpmtools's) in order. `interop.sh --written` passes on the card it saves (PASS 9), and on the
  committed card (PASS 6). `OODOS_CARD_FAULT` still runs the stand-in writer, wrong, to show interop.sh fail.
- **Each check can fail:** the BDOS built with a bug, one at a time, fails the tests named: no full compare after the
  hash (DIRTEST), close not writing back (FSTEST, LOGINTEST), no buffer found again (FSTEST: the READ count), the jump
  table not putting W2 back or bank 0's W2 the BIOS's (nearly all), no update or creation stamp (STAMPTEST), 37 not
  logging out (LOGINTEST, STAMPTEST), the default mode returning (ERRTEST), any user 0 file opened from another
  user or none (DIRTEST), 98 freeing nothing (DIRTEST, LOGINTEST), a new block not zeroed (FSTEST, DIRTEST), a
  read-only unit writable at login (ERRTEST), truncate not freeing, 18 repeating an entry, delete not checking
  read-only files, make taking a name there, rename taking the new name's attributes (DIRTEST), each of the
  review's fixes (RULETEST, FSTEST's write order: 280 entries written early on A: without it), the second
  round's: 37 not reading the units first, 48 not running CHANGED, CHANGED writing back, a failed write-back
  dropping its buffer, GETBUF taking a marked buffer first (RULETEST); the third round's: a close of a file not
  written failing on a marked buffer, a close failing on another file's, a close not checking its own (the one-sector
  case; the whole-card one passes without it, its directory write failing too), 98 not dropping them, 48
  stopping at the first failure, the warm boot not reading the units (RULETEST); the fourth round's: 98 dropping a
  marked buffer of a closed file's block, the warm boot not saying its 48's error (RULETEST); the comparison's finds: EXM's mask one
  for every drive, no S2 `$40`, 35 leaving R0-R2, a later extent's entry stamped at make or at close
  (RULETEST), function 50 not
  making the BDOS select its drive again (FSTEST after BIOSTEST), RC 0 for an
  extent the entry goes past (FSTEST), UNITS not resetting a stuck card (the busy card), the CCP not calling 48
  (LOGINTEST); and the CCP reading a last part record straight to its place, on a card made with it
  (`test_oodos_e2e`). Each was put back and the card made again, byte for byte the committed one.

**Done (phase 3a: the model, 2026-10-27)** ([oodos/README.md](../oodos/README.md) has the commands)

- **The model** ([`oodos/test/cpm3model.py`](../oodos/test/cpm3model.py), Python's standard library only): CP/M 3's
  file system at the level of functions 13–37, 40, 46, 48, 98 and 99 on an `oodos-8m`, `oodos-32m` or `oodos-ram`
  drive, the DPB worked out from `oodos/diskdefs`. Its state is the drives' images, laid out as CP/M 3 lays them
  out (the label, an SFCB every fourth entry, the entries, the blocks), so an image compares byte for byte with
  one the BDOS wrote and cpmtools reads it. User numbers, attributes, extents and EXM, random records and sparse
  files, a full directory and a full disk, rename and delete across extents, `?` searches, read-only drives and
  files, SYS files in user 0, temporary blocks (98) and date stamps by the label's mode.
- **The script** ([`oodos/test/modelscript.py`](../oodos/test/modelscript.py) has the format): `SCRIPT.BIN`, a
  16-byte header (`OOMS`, version 1, the drive, the count and length of the operations, the seed) and
  operations of an opcode and its arguments: set an FCB (one of 8), its rename half or its random record; call
  a function on an FCB; 18; read or write a record (20, 33; 21, 34, 40), the written one a pattern of a 16-bit
  tag; n sequential writes or reads; 17 and 18 to the end; n makes and closes; any other function; 46; 31;
  104. `RESULT.BIN` has, for each, the opcode, A, X, the FCB's EX, CR and R0–R2, and what the call left (a
  record's checksum, the entry found, the free space, the DPB).
- **The generator** makes a seed's script by running each candidate operation on the model and keeping it,
  aiming at the edges: extent and entry boundaries, records 0, 65,535, 262,143 and 262,144, a directory filled
  to its last entry and a disk to its last block, `?` in names, other users' files and user 0's SYS files,
  read-only drives and files, a select of a drive that isn't. The time is set (104) before what stamps, a
  minute apart or days. Its profiles: mixed, sparse, users, dirfill, diskfill (`oodos-ram` only).
- **DRIVER.COM** ([`oodos/test/driver/`](../oodos/test/driver/)): reads `A:SCRIPT.BIN`, runs it through `JSR
  $0203` with error mode `$FF`, keeps the results in memory and writes `A:RESULT.BIN` at the end. Only CP/M 3's
  documented functions; a `.COM` through the rules gate. `modelscript.Driver` is the same steps on the model.
- **`test_oodos_model`, in ctest:** DRIVER.COM on the emulator's W65C02S with the model as its BDOS
  (`modeltest.py serve`, over a pipe), 9 seeds over the three formats, compared with the model's own run:
  driver, formats and reference driver agree. The model made wrong on purpose (no update stamp, an RC one
  short, open's directory code 1, a byte of data, an attribute bit in A:'s directory) fails the comparison,
  which names the seed and the operation. DRIVER.COM runs under OODOS.SYS from A: and says `DRIVER OK`, and the harness's images
  are checked: A:'s entries off the card, M:'s directory as cold boot made it, the model's mkfs at that minute.
  `test_oodos_model_py` is the model's own tests: the DPBs, mkfs against `sd/oodos-ram.sparse`, and hand-worked
  cases from the manual's rules, each a scenario and the directory bytes it leaves; the script format, the
  generator's determinism and the comparison finding a changed result, block, RC and stamp; `modeltest.py
  sparse` letting a hole through but not a block past RC; the default seeds reaching every edge.
- **Against OODOS's BDOS:** with `OODOS_MODEL_BDOS` on (the CMake option, on since 2026-10-27; `OODOS_MODEL_BDOS=0`
  or `1` for one run), each seed's A: (and B:, `oodos-8m` or `oodos-32m`, or M:) goes on a card, OODOS.SYS boots
  on `backplane.machine`, `DRIVER` runs at the CCP, and the results and the drive under test are compared with
  the model's (`--compare renumber`, X compared), then A:'s directory, SFCBs and files but RESULT.BIN's records
  (the results, compared by their rules). Whatever stops a comparison (no RESULT.BIN, a script the model can't
  predict, an image missing) is a FAIL. ctest runs seeds 1, 4, 6, 10, 14, 24, 41 and 45 (from 3b1: 1, 2, 4, 14,
  24, 30, 34 and 42) (`modeltest.py seeds`:
  the three formats; between them, which
  `test_model.py` checks, a write's 2 on a full disk, make's $FF on a full directory on M: and on B:, and X 2 from a
  read-only drive), about 6 minutes under ASan (TIMEOUT 1200). The BDOS bugs the comparison found each have a
  directed test in RULETEST, which fails with its fix undone; the seeds were chosen before the generator's
  second stream, so which of them would still find each isn't claimed. With the comparison off, its case says it's
  skipped and the test exits 77, which ctest counts as skipped. Seeds 1–600 pass, run by hand
  (`oodos/test/model_seeds.sh`, a process a seed in parallel, counting each seed's exit status and PASS line:
  [oodos/README.md](../oodos/README.md)). `OODOS_MODEL_CUT=N` runs a seed's first N operations, to find where a
  difference starts.
- **What the comparison found** (2026-10-27, each settled by the Guide or, where it is silent, the BDOS's way, in
  the BDOS's and the model's Proposed lists): stamps only in a file's extent-zero slot (both had stamped every
  entry; the BDOS's make left the update 0); 35 of no file left R0–R2 as they were (BDOS); rename's error order
  (model); 31 not logging in (model); EXM's mask not kept a drive at a time, so after another drive's login an
  extent group's entry wasn't found and a second was made (BDOS); a write after a read that found no entry made
  it only at the close (BDOS); truncate's R2 check order and a read closing a written extent on a read-only drive
  (model); a program's own FCB of the open one's bytes taken for it (model: S2's state bits).
- **The review's fixes** (2026-10-27, the Guide decides each; both the BDOS and the model): the directory codes
  (0 but for 17 and 18), open's byte count with CR `$FF`, make's attributes cleared and File Exists for any
  extent, S2 taken as 0 by open, make and search, truncate refused where there's no data (the model had left it
  open), 30's byte count in S1 (the model hadn't it). The generator makes these from a second random stream, so
  a seed's script is as it was up to its first such operation. Seeds 1–600 pass again.
- **cpmtools at verify** ([`oodos/test/model_cpmtools.sh`](../oodos/test/model_cpmtools.sh), Docker): the
  model's empty drives against `mkfs.cpm -t`'s, byte for byte; then each seed's drive through `fsck.cpm`,
  `cpmls -F` (every file's stamps) and `cpmcp` (every file's contents). fsck.cpm 2.23 takes a sparse file's
  last extent for a bad record count (it wants RC's records in that many blocks); those entries, and only
  those, are let through, as interop.sh lets its first-slot warning: a hole in the blocks RC needs. A block
  past them is an RC too small, and fails.
- **Each check can fail:** seen to fail with DRIVER.COM's pattern step wrong, the model's EXM from 1 KB
  blocks, no update stamp, CR advanced by a random write, user 0's files opened without SYS, the comparison
  skipping records or RC, and (cpmtools) block numbers high byte first and an update stamp a minute out.

**Done (phase 3b2: the system extras, 2026-10-28)** ([oodos/README.md](../oodos/README.md) has the commands)

- **SYSTEST** ([`oodos/sys/test/systest.s`](../oodos/sys/test/systest.s)), a case by the tail's first letter, run by
  `test_oodos_sys`, which types at its questions:
  - **the SCB (49):** each field cold boot sets read at its offset (the version, the DMA address, the width and page
    length, ^H and rub/del, the delimiter, the multi-sector count, the search chain, the flags, the common base);
    past `$63` (`$64`, `$FF`) nothing and 0, a word set at `$63` only that byte (`$0264` never written), a reserved
    operation a get, the user flags; and each set's effect: the error mode set by 49 returning an open's Invalid
    Drive (with the error drive, `0F`), 45's mode read back, 26's DMA address read back, a search's record landing
    at an address 49 set, the drive and user (25 and 32 see them; a user of `$25` kept as 05), the console mode (109
    sees it), the delimiter (9 stops at it), AUXIN on nothing and on LPT (^Z), the date as 105 gives it, the column,
    the version byte changed with 12 still `$0031`. And what a program leaves (the delimiter `#`, mode 8, count 7)
    the CCP has put back for the next.
  - **the console,** from the serial line and, in a second run, the USB keyboard: 109 and 110 read and set, 9
    stopping at 110's delimiter, 111 with a tab expanded, raw (mode bit 2), and empty, 5 and 112 with LST: put on
    the serial line by 49 (as they are) and printer echo (each character twice there, once on the screen, none in
    raw mode), 4 on AUX:, 7 and 8, 3 reading AUX:, a key kept by 2's look and then seen by 11 and 6 and read by 1,
    ^S stopping output (nothing more until a key; a BEL for x; ^Q goes on), ^S ^P ^Q turning printer echo on (a BEL)
    and off, ^Q and ^P alone dropped by 1, ^S read by 1 in mode bit 1, 11 in mode bit 0 dropping a and b and keeping
    ^C (6 reads it), 11 dropping ^Q and keeping w, ^S ^C a BEL in mode bit 3; ^S ^C ending a program, and function
    10's ^C as the first key, with the return code `$FFFE`; 1 of DEL, `$C1` and a echoing only the a (the column
    03).
  - **function 10,** from both: 32 lines, each its count and bytes, and on the serial line what the screen got: ^A,
    ^B both ways, ^G, ^K, ^X, BS in the middle, ^F, other control characters as `^X`, ^C after the first key, ^W
    both ways, ^U then ^W, ^E, ^R, ^P (a BEL), DEL as BS, a full buffer (a BEL each), DEL at the start, a buffer of
    3 and of 0, the DMA buffer's string (typed on, a CR in it, a ^A in it), rub/del's echo (SCB 2F 0) and ^H as
    rub/del (2E `$FF`), a width of 9 (a new row; ^B back over a row's start typing the line again; rub/del's echo on
    a new row), ^C a character in mode bit 3.
  - **152:** 33 strings, each the result and the FCB's bytes 0–25 (24 and 25 untouched): drives (`b:`, `p:`, `m:`,
    `q:`, `1:`, `@:`, `5a:`), `*` in the name and type and after it, too long name, type and password, control
    characters, DEL and `$80`, every delimiter case (`=`, `,`, `|`, `.` after the type, `:` third, a tab, blanks
    then a word), CR and NUL, blanks only, empty, an empty name, a password with `*`, 140 blanks (the 128 read).
  - **107** (`4F4F44000001`), **47** (a program's M: not outlasting it; chain with 0, the CCP's A: and user 0 kept;
    a command of 127 shown and run as 126; with `$FF`, M: and user 5 the CCP's from then on, HELLO still loaded
    from user 0), **108** (set and read; kept across the warm boot; 0 for a program the CCP starts, the code as it
    was for a chained one; `$FFFD` for the default error mode's end).
- **Each check can fail** (2026-10-28): OODOS.SYS built with each of 70 faults, one at a time, fails
  `test_oodos_sys`: 49 gone; no look before output; ^Q and ^P not dropped; ^S ^C not ending; ^P not turning echo
  over; 6 not taking the kept key; 11's ^C-only mode ignored; mode bit 3 ignored, in the ^S scan and in function 10;
  raw mode ignored; printer echo ignored, or sent to the console; 110's set, 109's set and 49's byte set ignored; 9
  ignoring the delimiter; 111 not expanding tabs and 112 expanding them; 3, 4, 5 and 7 on the console's devices, 8
  never ready; 1 echoing nothing; each of function 10's ^A, ^B, ^F, ^G, ^K, ^X, ^W, ^E, ^R a no-op, ^U keeping no
  previous line, the line never becoming the previous one, rub/del never echoing, ^H always a backspace, no new
  row at the width, no initial string, no BEL when full, control characters not shown `^X`, ^C after the first
  key dropped, ^C at the start a character, a size of 0 taken as 0; 152 without `*`, without the 128 limit,
  taking `Q:`, returning the delimiter for the first blank, without the control character error, without `|`;
  107 wrong; 49 writing past `$63` and taking an offset past it; 47's drive and user not the CCP's, a command cut
  at 127, not shown; the CCP not putting its drive, its user, the delimiter, the console mode or the count back,
  not zeroing the return code, or zeroing a chained one; no `$FFFD`, no `$FFFE`, no error drive; the BIOS's
  AUXIN waiting on no device; cold boot without the count's or rub/del's default; ESAY's messages changed. Four
  that passed at first (4 and 5 on the console, ^R's row, ^U's previous line) and 7's got checks of their own.
- **The model comparison** is 3a's, unchanged by 3b2: seeds 1–600 pass, DRIVER.COM under the new console and CCP.
  The test card is made again (OODOS.SYS); `interop.sh` passes both ways (PASS 9, PASS 6).
- **Tests changed** because the Guide changes what they asserted: function 10's ^A is the cursor left (was
  ignored), other control characters are the line's (were ignored), a ^C after a character is a character
  (was ignored), a dropped key is a BEL; LINETEST's DMA buffer gets a 0 at +2 (with A/X 0 the buffer's string is
  typed first); the ^C-at-the-prompt test sets the CCP's drive (was the SCB's current drive); the CCP's user
  test has a program's user end with it, and 47's `$FF` give it to the CCP (a program's 32 lasted before).
- **The review's fixes** (2026-10-29): 11 in mode bit 0 keeps the ^C, so SYSTEST's CC? has 6 read it (03, was
  nothing); rub/del's echo at the width starts a new row; 49 keeps the user to 0–15; function 1 echoes neither DEL
  nor a byte over `$7F`; an `.assert` makes a 65th FJUMPS handler a build error. Each new check fails with its fix
  taken out (the four BDOS fixes out together: 5 failures in 5 cases, each the new check), and the `.assert` fires
  on 65 handlers and not on 64.

**Proposed (phase 3a: the model's choices,** where the manual leaves a behaviour open; `cpm3model.py`'s
header has the full list):

- **Allocation:** lowest free block first, or (a parameter) the nearest after the block before; the first
  free directory entry. The comparison takes the blocks' numbering as it finds it (`--compare renumber`) or,
  if the BDOS allocates the same way, byte for byte (`--compare exact`).
- **Stamps** (the Guide decides, 2.3.8 and function 22; settled with the BDOS 2026-10-27): only in the slot of
  a file's extent-zero entry. Make (of extent 0) stamps creation and update; an entry made by a write gets a
  cleared slot; writing a written FCB's entry back (close, or moving to another extent) and truncate stamp
  the update in the extent-zero entry's slot; open of extent 0 the access, with `$40`, not on a read-only
  drive. Delete, rename and 30 don't stamp. A mode without stamps left the SFCB until 3b1, which clears a made
  entry's slot whatever the mode (above).
- **Results:** 17 and 18 return the entry's directory code, the others 0 on success (the Guide, 2.3; settled
  2026-10-27); X is 0 on success (`--loose-x` doesn't compare it); 35 returns 0 or `$FF`; 13 returns 0 and logs no
  drive in; 31 logs the current drive in. Errors in the order drive (4), `?` (9), read-only drive (2), not
  found, read-only file (3), exists (8); rename checks the new name first (exists, then not found, then
  read-only), as the BDOS does. Delete refuses all if any match is read-only. Rename keeps the attributes.
  35 of no such file leaves R0–R2 0. A read that must close a written extent on a read-only drive is 2.
- **Truncate** (99) past the end, or where the file has no data (the Guide), is `$FF`, and R2 over 3 is `$FF`
  before a read-only file's 3.
- **The FCB's S2** holds what OODOS's BDOS keeps there (`$80` not written, `$40` no entry for the extent yet),
  so a program's own FCB of the same name and place isn't the open one (an FCB the program changed).
- **What isn't decided** (an FCB used after its file was deleted or renamed or its drive reset, two FCBs on
  one file with one writing, sequential I/O after an error, the user changed under an open file) the model
  refuses, and the generator keeps out of the scripts.

**Done (phase 3b1, 2026-10-28)** ([oodos/README.md](../oodos/README.md) has the commands)

- **XTRATEST** ([`oodos/sys/test/xtratest.s`](../oodos/sys/test/xtratest.s), test_oodos_sys's cases), each against
  the Guide: on M:, 101 of the cold boot's label; 100 turning passwords on with the label's own password, refused
  without it (7), given in the DMA buffer or by the default; 103 in read mode, then write mode, its XFCB and the
  file's slot byte for byte (the key and the password reversed, as cpmtools decodes them); 102's mode and stamps;
  open refused in read mode (7), opened read-only in write mode (f7': a write 3, a read 0), still read-only after a
  read moves it on to extent 1; delete, rename, 30 and truncate refused without the password; delete with f5' by
  the default password, the slot's mode 0 after; make with f6' (delete mode: opened and written without the
  password, deleted only with it, File R/O before Password Error); the label's password off and on, a
  read-protected open said in error mode `$FE` (`OODOS Error On M: Password Error`, then FF 07); a label asking
  for stamps without SFCBs (`$FF`), a new label in the first free entry with its stamps and a password of zeros
  kept as blanks; a new file's slot cleared with the label asking for none. The multi-sector count: 44's range;
  8 and 128 records written, read back, a read cut short by the file's end (01 08), 33 cut short (01 02) with
  R0–R2 put back and CR the record tried, 34 and 40 of 3 and 2; File R/O as a code, not a count; M: filled 128
  records a call, the last call's X what was left; on B: with the card's writes failing, a 128-record write (21)
  and one by 34 failing (FF 01) after 64 records with the FCB as they left it; and a program ending with 44 at 128
  leaving the next at 1. **Added by the review** (H, E, V): records not all program memory (32 from `$B800`
  into the I/O hole, 17 from `$B800`, 2 from `$FF80` past `$FFFF`, a read of 8 half in a read-only window)
  refused at once, no block taken, the FCB and memory as they were, and 16 from `$B800` to `$BFFF` written; a
  file's extent 0 made after its XFCB (make at extent 16, then 103; make at extent 16 with f6'; then a 34 of
  record 0; and make at extent 16, 103, the label's passwords off, then the 34) with the XFCB's mode in its
  slot; delete with f5' of `V?.DAT` taking two XFCBs (each slot's mode 0) and leaving W1.DAT's; user 0's
  read-protected SYS file opened from user 1, refused without its password (FF 07) and opened with it (f8').
- **The model** (`cpm3model.py`) has 44, 100–103 and 106, the multi-sector count on 20, 21, 33, 34 and 40, the
  protection on 15, 19, 22, 23, 30 and 99, XFCBs followed by delete and rename; hand-worked cases in
  `test_model.py` (a password's bytes worked out by hand, the label's and an XFCB's 32 bytes, each protection, 102,
  44's range, 20 and 33 cut short, records past `$FFFF` refused, after the R/O file's File R/O, extent 0's slot
  made after the XFCB by 103 and by make with f6', and with the label's passwords off). The model's memory has
  its own addresses (MBUF at `$C000`), so it has no I/O hole or flash: only past `$FFFF` is refused there. **The
  script** has three new operations: DMA16 (the DMA buffer's first 16
  bytes: a password, a new one, a mode), PASSWD (106) and MULTI (a call with a count of 1–32 from DRIVER.COM's
  own 4 KB buffer, 44 set back to 1 after); FCALL takes 100, 102 (with the FCB's bytes 24–31) and 103. DRIVER.COM
  sets 44 to 1 before it writes RESULT.BIN. **The generator** makes them from a third random stream (12% of the
  steps: labels with passwords on and off and every stamp mode, XFCBs in each mode, 102, the default password,
  each protected function with the right password or another, make with f6', 1–32 records by each of the five
  functions near a file's end and R's edges, 44's edges; from the review: delete with f5' of a wildcard that
  matches two or more of the user's XFCBs, two protected files made first if it has fewer; user 0's protected
  file opened from another user, made SYS and the user changed first if need be; make at extent 16, with f6' or
  then 103, and a 34 of record 0, so extent 0 comes after the XFCB), so a seed's script is 3a's up to its first; with
  passwords on a 3a make's f6' is cleared (the DMA buffer's bytes would be its password). The default seeds are
  1, 2, 4, 14, 24, 30, 34 and 42: the third stream moved what 6, 10, 41 and 45 reached, and the new edges are a
  Password Error and a multi-sector call cut short (the review's operations left them on every edge). A candidate
  the model refuses has its memory put back as well as its log (a WRITEN's first record left in the DMA buffer
  could match a password later, and seed 348's script then didn't replay).
- **The comparison** of OODOS's BDOS with the model passes seeds 1–600 (`model_seeds.sh`, a Release tree); it found
  no difference in 3b1's operations, the model and the BDOS written from the same reading of the Guide. Over seeds
  1–100 the scripts have (counted again after the review's operations, by a count that gives the generator before
  them 184, 10, 63, 44, 136, 390, 37 and 45) 183 labels set and 9 refused for their
  password, 71 XFCBs written by 103, 47 Password Errors (15: 12, 19: 11, 23: 7, 30: 7, 99: 1, 100: 9), 162 writes
  refused File R/O (f7', t1' or user 0's), 369 multi-sector calls (43 cut short, X the records done), and
  passwords on at the end of 50. Over seeds 1–600: a delete with f5' taking two or more XFCBs in 29 seeds, an
  open of user 0's protected SYS file from another user in 50, and a protected file's extent 0 made after its
  XFCB in 20.
- **cpmtools at verify** (`model_cpmtools.sh`): `cpmls -F`'s Prot (cpmtools reads the slot's byte 8) for every file
  against the mode of its XFCB in the model's image (`modeltest.py files` reads the XFCB, not the slot, so a slot
  that drifts from its XFCB fails: the model with the extent-zero fix undone fails seed 26); `fsck.cpm` decodes
  each XFCB's password, and the label's with passwords on, by the key, and fails one that isn't printable; its "Bit 4 and 6" error for a label asking for creation and access stamps is
  let through (`modeltest.py label`); `sfcb.py` works out fsck.cpm's date warnings by its own sums and checks the
  stamps' order only where the label always asked for creation and update. It found the slot a make left on a
  drive whose label asked for no stamps (`mkfs.cpm`'s `$E5`s, read as a bad hour and minute): both the BDOS and the
  model now clear it. cpmtools 2.23 lists an XFCB as a file of user 16–31 and has no way to copy out its
  `[passwd]`, so the passwords themselves are checked by fsck.cpm's decoding and, byte for byte, by the comparison
  (the model's bytes are cpmtools's formula, `cpmfs.c`'s).
- **Each check can fail:** the BDOS built with each of these undone, one at a time, fails the case named (XTRATEST's
  in test_oodos_sys): no FCB kept on a physical error mid-call (P, Q: CR 40 not 80), R0–R2 not put back, X 0 for
  the records done, 44 taking 0, writes ignoring f7', f7' cleared at an extent move (W), delete checking no
  password, the password before File R/O, the password not reversed, no default password, the label's password not
  checked, stamps set without SFCBs (N), f5' and 103 not setting the slot's mode, make with f6' making no XFCB, the
  CCP leaving the count (C, D), a slot left as it was with no stamps asked for (Z), none kept as zeros (N), 102
  without its mode or stamps, open ignoring read mode, 100 taking no new password, the next record's DMA address
  not moved on; from the review: no check of the records' memory first (H: blocks taken, memory overwritten), a
  new extent-zero slot's mode 0 (E), the open of a SYS file checking the current user's XFCB, the search not put
  back after f5' clears a slot (V, and L's f5' and LOGINTEST's XFCBs). The model made wrong three ways (a password not reversed, X 0 for the records
  done, 101's bit 7 dropped) fails the comparison on most seeds with passwords or multi-sector calls (26, 13 and 8 of 40). And
  cpmtools: the model's Prot all None fails `cpmls -F`'s check (seed 2), a key byte unlike the one the password
  was XORed with fails `fsck.cpm` (a password not printable); a key that isn't the sum, used consistently, it
  can't see (it decodes by the byte), which the hand-worked case in `test_model.py` and the comparison do.

**Proposed (phase 3b1: the model's choices,** as the BDOS's, in its Proposed list above; `cpm3model.py`'s header
has them): the multi-sector count's N calls, its physical errors not modelled (the model has no I/O errors); a
run that would pass the largest file is refused (the generator keeps out); an orphan XFCB (none whose file was
made with it) is never made, so make with f6' reusing one and rename onto one aren't in the scripts.

**Done (phase 3c, 2026-10-30)** ([oodos/README.md](../oodos/README.md) has the commands)

- **MEMTEST** ([`oodos/sys/test/memtest.s`](../oodos/sys/test/memtest.s), test_oodos_sys's cases), against the 3c
  list above: on M:, 4 records written from `$1000:0` and read back to the CPU; 2 read by 33 to `$1401:$00F0`, 1 to
  `$1FFC:0` across banks 1 and 2, 1 by 20 to `$1FF7:$000F` (MOVE's end `$FFFF`: the FCB moved on), 17's record to `$1800:0`, each with the bytes around them as they were, and
  test_oodos_sys comparing the physical bytes with M:'s records read behind OODOS's back; refused, A = X = `$FF`
  with nothing moved: a record from block 23 into 24, banks 6, C and F, past `$FFFFF`, an offset past `$FFFF`, 17
  records from `$5F80:0` (16 reach `$5FFFF` exactly: 4 read, then the end), a write and a 17 on M:'s banks (the
  FCB as it was); 49's multi-sector count of 255 over three blocks, read and refused; 46 with a segment to the
  CPU's buffer; 13 keeping the segment; the warm boot clearing the one MEMTEST D leaves. 53–58
  through 46 calls (each rounding, base, taken, partial, M_EXT and free case above; 53 and 54's run allocated, so
  55 can't have it and 57 frees it; 57 of a tail, the head kept, and of one short of the end), an MCB in the I/O
  hole, and a block left allocated freed by the warm boot. 73 in each window, W3 on block 62, a window on block 37, on flash
  (refused) and read-only. 74 on a card with A: alone: A:, M: and H: refused (4); nine paths DRIVES.TXT refuses
  (`$10`), 510 bytes refused and 509 taken (then not found); not found and a directory (`$11`); `/BOOT.BIN` (`$12`)
  and A:'s own image (`$13`), each leaving C: no drive; B.IMG mounted as C: (in lower case), a file written there;
  a directory entry put in its first sector behind OODOS's back and the same image mounted again: 17 finds it, the
  drive was logged out; unmounted (C: then Invalid Drive), unmounted again (done); read-only (make: 2); a record
  written in place and the card's writes failing: the unmount refused (1), C: still there, then done once they
  work, and the record on the card; the card taken out at a mount's rescan: refused (1). XMOVE's physical bank through function 50, there and back.
- **The model** (`cpm3model.py`) has 51, 52 and 50's XMOVE and MOVE, and physical memory of its own;
  `test_model.py` has hand-worked cases (records to and from `$10000`, `$14100` and `$18000`, the seven refusals
  with nothing moved, a 17 refused and its search gone on, 46 to the CPU's buffer, 13 keeping the segment, 50 both
  ways, the edges of what a segment may reach, SMULTI through the reference driver, and seed 1's script as without
  segments up to its first SMULTI). **The script** has SMULTI (above, and `modelscript.py`'s header); **the
  generator** makes it from a fourth stream, 4% of the steps: a read, a write (20, 21, 33, 34, 40, 1–32 records) or
  a search (17, 18), three in four at a physical address in banks 1–5 (their first and last bytes, across a 64 KB
  bank and a block, anywhere), the rest where a segment may not reach (into bank 6, M:'s banks, block 60, past 1 MB,
  an offset past `$FFFF`). The default seeds are as they were; between them they reach every edge, and the two new
  ones (records moved through a segment, a segment refused). Over seeds 1–100: 434 SMULTIs in 99 seeds, 128 done
  (61 of more than a record, 63 across a 64 KB bank), 98 refused, 9 cut short by a file's end. The `rc` fault of
  `test_oodos_model` moved to seed 4: on seed 1's new script the faulty model meets what CP/M 3 leaves open and
  DRIVER can't finish.
- **The comparison** passes seeds 1–600 (`model_seeds.sh`, a Release tree); it found no difference.
- **cpmtools at verify** (`model_cpmtools.sh`, seeds 1–60): PASS 63.
- **Each check can fail:** OODOS.SYS built with each of these, one at a time, fails the case named (MEMTEST's, in
  test_oodos_sys): 51 not storing the segment's high byte (D: GET1, DW4, DRD, DR2); 52's bytes swapped (GET1,
  GET13); a segment reaching bank 6 (DRST, DRB6, DRM17), or `SEGOK` checking only the first block (DRST, DRM17);
  `MRANGE` checking no segment (DRM17: `DSEG` still refuses each record); 17 and 18 ignoring the segment (D17CHK,
  D17M); 53's M_EXT always 0 (MAX1, AM3, MAX3); 54 not kept to the length (AM3); 55 marking only an allocation's
  first block (AL2, AB2, AM2); 56 shrinking (AB8, added for it: AB6's run had its first block taken, so the first
  try passed); 57 freeing from a later block with a length of 0 (FR1, FR2); 58 freeing nothing (MAX4, AL4, AL5); the warm boot freeing
  no block (W) or keeping the segment (W, D and five other cases: the cold boot's 0 sends the CCP's own loads to
  physical memory); 73's offset not 0–15 (P4123, P8FFF, P37) or a window on flash translated (PFL); 74 taking A:
  (NA, NSAME, NC), `//` and a `/` at the end (NP2, NP5, NP6), a tab (NP9), any length (NLONG), any size (NSZ),
  another drive's file (NSAME), not logging the drive out (NCY), mounting after a failed write-back (NWB, NWBSEL,
  NWBCHK), or calling the unmount of nothing an error (NUC2); XMOVE taking no physical bank (D and B), MOVE without
  its A16–A19 (D, B); 13 clearing the segment (GET13). In the comparison: the BDOS letting a segment reach bank 6
  fails seeds 1 and 34 (their SMULTI's A); the model's records ignoring the segment fails seeds 14 and 24 of 1, 2,
  4, 14 and 24 (their mirrored SMULTI's sums). The review fixes' own: PCOPY deciding by A = X = `$FF` (DRF,
  DRFCR); 53 marking nothing (MAXM, MXA, M1AM); 57 freeing only at a base (TB, TCM, TE), or refusing a tail that
  exactly reaches the end (TXE, TXC); 74 ignoring the rescan's carry (NRS); the warm boot keeping a segment
  MEMTEST D left set (WGET); BEND one byte off (BFROM).

**Done (phase 4a: the CCP, 2026-10-30)** ([oodos/README.md](../oodos/README.md) has the commands)

- **`test_oodos_ccp`,** a new suite, boots OODOS.SYS as `test_oodos_sys` does (their machinery moved, unchanged, to
  `emulator/test/host/oodos_rig.h`), types command lines and compares what each says, line for line, with the
  Guide: the line (capitals, blanks, comments, 255 characters, a tail of 126 and 127); `!` with each command shown,
  `:` after a failure (NAME?, a built-in's, a program's 108) and a success, a skipped one keeping the code, a ^C's
  end dropping the rest, 47's command taking its place (the chained command seen), the next command shown past a
  tab, a trailing `!` running nothing more; `d:`, `nd:`, users 0–15 and 16, a tail after `d:`, Q:, an unmounted H:; USER, USE and
  USER's question; DIR and DIRSYS on M: (five names a line, two at a width of 40, one at 15; SYS files' messages
  both ways, No File, `*` and `?`, a blank type, another user's files, a drive that isn't, a specification 152
  refuses); a page of 4 stopping DIR and TYPE (a key goes on, ^C at the stop ends the line with `$FFFE`), page
  mode off and a page of 1 not stopping, ^C while TYPE and DIR write; TYPE (tabs, ^Z, a `.COM`'s `OOD`, user 0's
  SYS file from user 4, a wildcard handed to TYPE.COM, two names refused); ERA (one name, a wildcard answered N, then Y, nothing
  left, read-only, the password not shown in the question); REN (both drives' forms, No File, a name there
  answered N, then Y, new the same file as old refused with nothing asked (both drives' forms, from A: and M:),
  two drives, its forms, read-only); passwords given to TYPE, ERA, REN and a program (M:'s
  label's passwords on and XFCBs put there behind OODOS's back); each built-in's utility not found and found (ARGS.COM,
  [`oodos/sys/test/args.s`](../oodos/sys/test/args.s), as DIR.COM, TYPE.COM, ERASE.COM, RENAME.COM: the tail as
  given, DIRSYS's SYS added), `d:DIR` the program; programs by drive, the current drive, the user and user 0's SYS
  files, `.COM`, `.SUB`, `.PRL`, wildcards, a long name, `5B:NAME`; the search chain (the default drive first or
  M:, drives that aren't passed over, a given drive alone); the display mode with and without ` (User 0)`; the
  tail, both FCBs (passwords, `=`, `,`, `B:=A:*.C`, an option list, nothing, one 152 refuses), the passwords'
  places and lengths, the load drive, the drive and user, the return code 0 at the start; trailing blanks and tabs
  not in the tail, a 0 ending the line, a tail of 126 with blanks after it run. `cmd.inc` by CMDTEST
  ([`oodos/sys/test/cmdtest.s`](../oodos/sys/test/cmdtest.s), by `GO`): the console mode `CM_PAGE` found kept and
  put back by `CM_UNPAGE`, a key left by `CM_BRK` in mode 0, a ^C taken after `CM_PAGE`.
- **cpmtools:** `test_oodos_card` runs the CCP on the committed card (`REN README.DOC=README.TXT`, `REN
  PATTERN.DAT=PATTERN.BIN` of two entries, `ERA SMALL.BIN` in user 3, `ERA *.PRL` answered Y), checks the image,
  and with `OODOS_CCP_OUT` saves it; `interop.sh --ccp` passes on it (PASS 7: `fsck.cpm` clean, the stamps kept by
  the renames, the erased files gone from `cpmls -F`, the contents by `cpmcp`), and fails on a card without them.
- **Each check can fail:** OODOS.SYS built with each of 30 faults, one at a time (`OODOS_SYS_DIR`), fails
  `test_oodos_ccp`, each in the case named: DIR listing SYS files with the rest, or five names a line whatever the
  width (DIR's, and paging's); ERA not asking, or showing the password; REN not looking for the new name, not
  asking, or taking two drives; TYPE past the ^Z (TYPE's and three more); USER taking 16; `d:` not checked by 14;
  `!`'s commands not shown; `:` ignored; the search chain ignored; the display mode ignored; no second FCB; a
  password's place one off; no page stops; no ^C looked for; no hand-off; a ^C's end not dropping the rest; no
  capitals; the LF owed not written before a program (8 cases); a built-in's failure as a success; a tail of 127
  taken; DIRSYS's SYS not added; the load drive 0; `.SUB` as any type; an unreadable given drive passed over;
  option lists not seen; no user in the prompt. The review's fixes each fail it undone, one at a time: REN of a
  file onto itself asking (and so deleting); 47's command not run (the chained output now checked); a wildcard
  TYPE refused; a 0 not ending the line; trailing blanks in the tail; a tab shown before the next command; an
  empty prompt after a trailing `!`; `cmd.inc`'s old `CM_PAGE`, `CM_UNPAGE` and `CM_BRK`.
- **Tests changed** because the Guide changes what they asserted (each listed in the commit): a tail is in capitals
  (`test_oodos_sys`'s GO and `test_oodos_e2e`'s HELLO print ` WORLD`); a line holds 255 characters, not 126; a
  command that writes nothing has the next prompt on the next line (the VT52 test's scroll takes six empty lines,
  not three); the prompt shows the user (`5M>`); a program comes from the current drive and user or user 0's SYS
  file, so SYSTEST's chain is to `A:HELLO` and the card's HELLO.COM is SYS (`test_oodos_e2e` reads its entry with
  the attributes off).

**Proposed**

- **The emulator** runs OODOS in native tests (BDOS functions, the CCP, RSXs, the Apple driver) and in the WebUI's
  e2e tests.
- Nothing of Digital Research's, or any other copyrighted software, goes into the tests or onto the website.


## Phases ##

Each phase is its own branch.

**The books (decided 2026-10-31):** OODOS's reference is three books in [docs/](../docs/README.md), written in
phases of their own: G0 the skeleton and its checks; G1 the Programmer's Guide for phases 0–4 and the User's Guide;
G2 the OO-1 Technical Reference; G3 with the OOLIB phase. After G3, no phase below is done until its chapters,
entries and indexes in docs/ are.

0. **This document,** its open questions settled, the `diskdefs` file, and the GW-BASIC size spike.
1. **Tooling:** the build, card images, the interoperability tests, the DS3231 in the emulator.
2. **OOLDR and the BIOS:** common memory, the banking stub, the SD drives, the console, TIME.
3. **The BDOS:** the file system, deblocking, hashing, multi-sector I/O, the SCB, date stamps, labels and
   passwords.
4. **The CCP and utilities,** two parts (decided 2026-10-30): **4a** the CCP and its built-in commands; **4b** the
   transient utilities, two branches (decided 2026-10-31): **4b1** DIR's full form, TYPE, ERASE, RENAME, SET,
   SHOW, INITDIR, DATE, DEVICE and SETDEF; **4b2** PIP.
5. **RSXs:** the loader, chaining, GENCOM; GET, PUT and SUBMIT as RSXs (SUBMIT moved here from phase 4,
   2026-10-30). Proposed in three stages, 5a–5c: [The RSXs, phase 5](#the-rsxs-phase-5).
6. **Apple disks:** native Disk II in the emulator, the driver, `apple-do` drives; then the DOS 3.3
   exchange utility.
   Proposed in four stages, 6a–6d: [The plan, phase 6](#the-plan-phase-6).
   *Proposed (2026-10-30), not yet numbered:* the game and device services ([oolib.md](oolib.md)) as a phase
   before GW-BASIC, whose SCREEN, PLAY, STICK and ON TIMER are written on them: the interrupt dispatcher and the
   VIA's system tick first. Its open questions are the owner's to settle first.
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
- **Common memory in an allocated block** (deferred to phase 7, 2026-10-30, with the two questions above). FARCALL
  needs common memory in the top 2 KB of a block it maps into W3 ([Far calls](#far-calls)), but 3c's 53–56
  allocate blocks and copy nothing into them. Phase 7 decides how a block is marked for code (a flag in the MCB,
  or a function of OODOS's own), who copies common memory in (OODOS at allocation, or FARCALL's first use), and
  how that fits a home block and a lean FARCALL. Until then a program copies common memory into a block itself
  (test_oodos_sys puts it in FARTEST's and STUBTEST's blocks by hand).

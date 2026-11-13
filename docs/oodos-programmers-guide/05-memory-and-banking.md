# 5 Memory and banking

The 1 MB beyond a program's 64 KB: 16 KB blocks, allocating them, calling code in them, and reading and writing
records anywhere in physical memory.

The design record, for the reasons: [hw/oodos.md, "Banking"](../../hw/oodos.md#banking), ["Memory: CP/M-86's
functions"](../../hw/oodos.md#memory-cpm-86s-functions), ["Far calls"](../../hw/oodos.md#far-calls).

## Blocks

The OO-1 has 1 MB of memory, 64 blocks of 16 KB, numbered 0 to 63; four blocks make a 64 KB bank, so bank 1 is
blocks 4–7. The CPU sees 64 KB at a time through four windows of 16 KB, each mapped to a block by the north bridge:
W0 `$0000–$3FFF`, W1 `$4000–$7FFF`, W2 `$8000–$BFFF` and W3 `$C000–$FFFF`, with the I/O hole at `$C000–$C7FF` always
in front of W3.

While a program runs, W0–W2 hold blocks 0–2, its 48 KB, and W3 holds block 62: OODOS's resident code in the code
window, `$C800–$F7FF`, and common memory at `$F800–$FFFF`. The windows belong to OODOS: a program doesn't write the
window registers ([AGENTS.md](../../AGENTS.md)). It reaches the rest of memory by the functions in this chapter. The
one exception is OOLIB's: its linked library may put an allocated block, a block of banks 9–B or the video card's
aperture slot 1 in W1, under [the W1 contract](#the-w1-contract).

| Blocks | Bank | What |
|---|---|---|
| 0–2 | 0 | the program: zero page, stack, base page and TPA |
| 3 | 0 | not allocated by 53–58; a DMA segment may reach it |
| 4–23 | 1–5 | the blocks programs allocate (53–58): 20 blocks, 320 KB |
| 24–31 | 6–7 | OODOS's: the directory hashes, the sector buffers, the system block |
| 32 | 8 | the video card's aperture, slot 0: the console's card memory, OODOS's (since OODOS 0.9, revision 2: [below](#video-memory-on-revision-2)) |
| 33 | 8 | the aperture's slot 1: the program's card memory, through OOLIB |
| 34–35 | 8 | the aperture's slots 2 and 3, OODOS's |
| 36–47 | 9–B | ordinary RAM, the running program's (not allocated by 53–58); OOLIB stages its asset loads in `$BF000–$BFFFF` unless the program moves `GX_STAGE` |
| 48–55 | C–D | M:, the RAM disk |
| 56–59 | E | not allocated: Apple II mode copies the IIe ROM here each time it starts; a DMA segment may reach it |
| 60–63 | F | the BDOS, the BIOS, OODOS's resident block, and the ROM BIOS's copy |

The design record has the layout and why: [hw/oodos.md, "Banking"](../../hw/oodos.md#banking).

## Allocating memory

CP/M 3 has no memory manager, so OODOS takes CP/M-86's, functions 51–58, and hands out its 20 blocks in banks 1–5.

The functions take a memory control block, an MCB, of 5 bytes at A/X:

| Offset | Name | What |
|---|---|---|
| +0–1 | M_BASE | the first paragraph (a 16-byte step: the physical address ÷ 16) |
| +2–3 | M_LENGTH | the length, in paragraphs |
| +4 | M_EXT | 53 and 54: 1 if a free block is left; 57: `$FF` frees everything |

Memory comes in whole blocks of 1,024 paragraphs. A length is rounded up to whole blocks, and one returned is a
multiple of 1,024. A base must be a block's first paragraph in banks 1–5: `$1000`, `$1400`, and so on to `$5C00`.

- [53 MC_MAX](07-function-reference.md#bdos-53): the longest free run, at most the length, allocated.
- [54 MC_ABSMAX](07-function-reference.md#bdos-54): the same, starting at the base.
- [55 MC_ALLOC](07-function-reference.md#bdos-55): the length, the lowest free run that has it.
- [56 MC_ABSALLOC](07-function-reference.md#bdos-56): the length at the base, all of it free.
- [57 MC_FREE](07-function-reference.md#bdos-57): an allocation, or its top part from a later block to its end.
- [58 MC_ALLFREE](07-function-reference.md#bdos-58): everything.

Each returns A = 0 when it is done, and A = `$FF` with X = 0 when there is no memory to give or the MCB asks for what
can't be (a length of 0, a base that isn't a block's). A = X = `$FF` is an MCB in the I/O hole; the MCB is read
whole before anything changes, so nothing has.

The blocks belong to the running program. The warm boot frees them all, as function 58 does, so a program that
ends any way leaks nothing; nothing allocated survives into the next program.

To use a block, a program reads and writes it through the DMA segment (51) or BIOS function 50's MOVE, or calls code
in it with FARCALL (below).

## OODOS's own functions: 72-74

Functions from 72 are OODOS's own, a range no Digital Research system uses
([hw/oodos.md, "OODOS's own"](../../hw/oodos.md#oodoss-own)).

- **[72 O_VERSION](07-function-reference.md#bdos-72)** fills 10 bytes at A/X: OODOS's version (+0 major, +1 minor),
  the BIOS's (+2, +3), the memory fitted in KB (+4–5: 1,024), the memory programs may allocate in KB (+6–7: 320, the
  total, not what is free now) and FARCALL's address (+8–9), which also comes back in A/X. Function 12 returns
  CP/M 3's `$0031`, the same on every system; 72 is how a program knows it runs on OODOS, and which.
- **[73 O_PHYSADDR](07-function-reference.md#bdos-73)** turns one of the program's CPU addresses into its physical
  address, a paragraph and an offset ([below](#physical-addresses)).
- **[74 O_MOUNT](07-function-reference.md#bdos-74)** mounts an image file on the card as B:–G:, or unmounts one, as a
  line of `DRIVES.TXT` does at boot.

75 (O_ROM), leaving OODOS for the ROM's monitor or Apple II mode, is OODOS's too; the CCP's WOZMON and APPLE2 use it.
So is [76 (O_SECTOR)](07-function-reference.md#bdos-76), an Apple disk's DOS 3.3 sector, which DOS33 uses. 77–97
are kept for OODOS.

## Far calls: FARCALL and the far call table

Code in an allocated block runs in W3's code window, `$C800–$F7FF`. A program calls it through FARCALL, a routine in
common memory whose address function 72 returns. A call is three bytes after the `JSR`:

```
        jsr     FARCALL         ; the address from function 72
        .byte   20              ; the block, for W3
        .word   $C800           ; the address to call in it
```

FARCALL maps the block into W3, calls the address, and maps the caller's block back when the code returns with
`RTS`. A, X, Y and the flags reach the code as the caller left them, and come back as the code left them. The old
block is kept on the 6502's stack, so code in one block may far call another. The program's own memory,
`$0000–$BFFF`, stays mapped throughout. `test_oodos_sys` measures a far call at 281 cycles more than a `JSR` and
its `RTS`.

The block must have common memory in its top 2 KB, `$F800–$FFFF`, since FARCALL itself, the BDOS's entry and the
interrupt entry are there whatever W3 holds. OODOS doesn't put it there yet: 53–56 allocate a block and copy nothing
into it, so for now a program copies common memory into a block it far calls itself. Which block is marked for code, and
who copies common memory, is left to phase 7 ([hw/oodos.md, "Open Questions"](../../hw/oodos.md#open-questions)).

FARCALL is the far call table's one entry so far. FARPEEK, FARPOKE and FARMOVE, for bytes at physical addresses, are
proposed ([hw/oodos.md, "Far calls"](../../hw/oodos.md#far-calls)), not built: a program reaches physical memory by
the DMA segment or function 50's MOVE.

## The DMA segment

Function [51 F_DMASEG](07-function-reference.md#bdos-51) sets a DMA segment, a paragraph, as CP/M-86 does. With one
set, the records the disk functions move (17 and 18's directory record, and the records of 20, 21, 33, 34 and 40)
go to and from physical memory at the segment × 16 + the DMA address, not the program's CPU address. So a program
can read a file straight into an allocated block, or into banks 9–B. Function
[52 F_DMAGET](07-function-reference.md#bdos-52) returns the DMA address and the segment.

`$FFFF` is no segment: the DMA address is then a CPU address, as in CP/M 3. It is the default, and the warm boot sets
it back, so a segment never reaches the next program. Function 13 sets the DMA address back to `$0280` and keeps the
segment.

The segment is for those records only. FCBs, console strings and buffers, function 46's answer and the passwords
read from the DMA buffer are always CPU addresses.

A segment may reach any RAM but OODOS's own: banks 0–5, 8–B and E. Banks 6–7, C–D (M:) and F, and anything past
`$FFFFF`, are refused, and so is a record that would run past a DMA address of `$FFFF`. A refused record is A = X =
`$FF` with nothing done, checked before the first record of a multi-sector call. A record through a segment costs
238 cycles more than one to a CPU address: a buffered record on M: by function 33, 8,176 cycles against 7,938, as
`test_oodos_sys` measures. A record read into an aperture window that shows OODOS's card memory, slot 0's
`$80000–$83FFF` or slots 2 and 3's `$88000–$8FFFF`, is refused too ([below](#video-memory-on-revision-2)). For a
file into memory, [function 83's](07-function-reference.md#bdos-83) `DM_LOAD` is the faster way: whole sectors
straight from the SD card; and into card memory, `DM_LOAD` into a buffer, then `DM_VWRITE`.

## Physical addresses

A physical address is 20 bits, `$00000–$FFFFF`. OODOS writes one as a paragraph and an offset, as an 8086 does: the
paragraph × 16 + the offset, the offset 0 to 15. A block's first paragraph is its number × 1,024.

Function [73 O_PHYSADDR](07-function-reference.md#bdos-73) gives the physical address of one of the program's CPU
addresses, through the windows it has when it calls: `$0000–$BFFF` in blocks 0–2, and `$C800–$FFFF` in W3's block.
The paragraph suits function 51 and the offset function 26, so the two name the same byte; a program that programs a
device's DMA itself needs the address too.

What has no physical address is refused, A = X = `$FF`: the I/O hole, `$C000–$C7FF`, and a window on flash. Every
function that takes an address treats the I/O hole the same way: a function stops at a program address there and
returns A = X = `$FF`.

The BDOS reaches a program's memory two ways, both through the windows the BDOS entry saved. Records, FCBs and other
blocks move by their physical addresses with the DMA controller (the BIOS's MOVE and XMOVE). Strings and parameter
blocks read or written a byte at a time (function 9's string, 10's line, the blocks of 49, 50, 72, 104, 105 and 152, the
MCBs, among others) are reached by mapping the program's block into the BDOS's own W2 for each byte. Neither changes the
program's windows.

## The W1 contract

OOLIB is OODOS's own extension, there so that programs don't go around the rules; where it does what a program may
not, OODOS says so here, as part of the interface, and checks it by version. From OOLIB's interface version 4
([function 80's](07-function-reference.md#bdos-80) `FT_INFO`, OODOS 0.5), **OOLIB may write W1** (`NB_WIN1`,
`$C681`) with any block [function 83's](07-function-reference.md#bdos-83) `DM_INFO` puts in its W1 map. From
interface version 8 (OODOS 0.9, video card revision 2) that is block 1, the blocks the program has allocated, block
33 (aperture slot 1's window) and blocks 36–47 (banks 9–B); never 32, 34 or 35, the windows of OODOS's slots. And
**OOLIB may write `SLOT1`** (`VIDEO_SLOT1`, `$C4B2`) with any of the program's card banks, 16–111, so that
block 33 shows the card memory it draws in. OODOS keeps W1 and `SLOT1`:

- **across a BDOS call:** the entry reads W1 itself and puts that value back, and the BDOS reaches the program's
  `$4000–$7FFF` through it (a record read there lands in the mapped block);
- **across an interrupt:** the dispatcher borrows W1 for the event area and puts back what it found;
- **in a hook:** a hook runs with the windows the program had when it set it (W1 among them), and the program's are
  put back after;
- **at the warm boot:** block 1 goes back, whatever W1 had, and slot 1 goes to card bank 16 (`VM_PROG_BANK`), the
  program's first.

A single `STA NB_WIN1` can't be split by an interrupt, and costs 4 cycles against about 450 for a BDOS call, which
matters to a draw that crosses blocks. The library checks `FT_INFO`'s version at its start (`GX_INIT`) and refuses to
map below 8 (interface versions 4–7 were revision 1's: video memory's blocks 33–47 in W1); a change to the
contract comes with a new version. Only W1: W2 and W3 stay OODOS's (`$0203` jumps to
`$BFFD` in block 2; FARCALL needs common memory in W3's block).

While another block is in W1, the program's own `$4000–$7FFF` is out of its reach, and of the BDOS's: its code, its
event area, the FCBs and parameter blocks it passes, and what it draws from stay in `$0300–$3FFF` or `$8000–$BFFC`.
A hook that draws maps W1 itself. A program that doesn't use OOLIB keeps the old rule: the windows are OODOS's.

## Video memory on revision 2

Since OODOS 0.9, OODOS and OOLIB are built for revision 2 of the video card (its ID `$92`), from interface version 8:
the card draws from 2 MB of its own memory instead of banks 8–B
([the Technical Reference, chapter 6](../oo1-technical-reference/06-video.md#revision-2-card-memory)). On an older
card, or none, OODOS has no console screen and the card operations are refused. For a program:

- **Banks 8–B.** Bank 8, blocks 32–35, is the card's aperture: four 16 KB slots, each showing a 16 KB card bank that
  its slot register chooses. Slot 0 (block 32) always shows card bank 0, the console's; slots 2 and 3 (blocks 34
  and 35) are OODOS's, borrowed by `DM_VCOPY`; slot 1 (block 33) is the program's, through OOLIB. Banks 9–B, blocks
  36–47, are ordinary RAM, the running program's as video memory was: in the W1 map, reachable by DMA segments and
  function 83, but not handed out by functions 53–58.
- **Card memory.** Its addresses are 24 bits, `$000000–$1FFFFF`, not physical addresses. OODOS owns
  `$000000–$03FFFF` (the console's palette at `$000000`, its font at `$000400` and its text at `$002000`, what units
  0–12 were, and the rest kept) and Apple II mode's `$1C0000–$1FFFFF`. A program has `$040000–$1BFFFF`, 1.5 MB, card
  banks 16–111, which it allocates through OOLIB ([chapter 8](08-game-services.md#video-memory)); the warm boot takes
  it all back, as it does the blocks. A program may read OODOS's part (a card source of function 83) but never write
  it. `oodos/include/vmem.inc` names the parts.
- **Reaching it.** The CPU reaches the program's card memory through slot 1 in W1, under the W1 contract
  ([above](#the-w1-contract)); in bulk, function 83's card operations (`DM_VWRITE`, `DM_VREAD`, `DM_VFILL`,
  `DM_VCOPY`) go through the card's data port or the borrowed slots, by DMA. A program writes neither `NB_WIN1` nor
  any slot register itself, nor the port's registers (`PADDR`, `PDATA`, `PSTEP`, `PCTL`), which are OODOS's: OOLIB
  writes `NB_WIN1` and `SLOT1` alone.
- **What is refused.** No physical destination of function 83, and no record read through a DMA segment, may touch
  `$80000–$83FFF` or `$88000–$8FFFF`, the windows of OODOS's slots. No card destination may leave the program's
  part. The card operations need the DMA controller.

The rest of this chapter (allocation, FARCALL, the DMA segment, physical addresses) is unchanged. A program tells
the revisions apart by OOLIB's interface version ([function 80's](07-function-reference.md#bdos-80) `FT_INFO`, 8)
and `DM_INFO`'s A, the card's memory (0 with no revision 2 card).

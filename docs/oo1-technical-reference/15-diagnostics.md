# 15 Diagnostics

The ROM's diagnostics: how they are entered, the stages and what each tests, what they never touch, the lights,
the screen and the serial line, how a run ends and the way back to the boot or the monitor, and how long a run
takes. They are in the BIOS's flash, so they test a machine that can't boot, and from OODOS's `A>` they are one
command away.

The design record, for the reasons: [hw/diag.md, plan 2](../../hw/diag.md#plan-2-every-diagnostic-in-the-rom).
The code: [`diag23.s`](../../backplane/bios/diag23.s), [`diag22.s`](../../backplane/bios/diag22.s),
[`diag21.s`](../../backplane/bios/diag21.s) and [`diag.inc`](../../backplane/bios/diag.inc), with the hardware
library in [`backplane/oohw/`](../../backplane/oohw/).

## The flash and the core

Status: **emulated**

Since BIOS 0.14: running on the emulator; no card is built. The diagnostics are three blocks
of ROM A ([chapter 2](02-memory-and-banking.md#physical-memory)), each mapped into W2 (`$8000–$BFFF`) while it
runs:

| Block | What |
|---|---|
| 23 | the core: its header and the BIOS's way in, the reset's prompt, stages 0–6 (the flash, zero page and stack, the CPU and all of RAM), the end and its put-back, Break |
| 22 | stages 7–10: the north bridge and the clock, DMA, the video card, the sound card (by its command records) |
| 21 | stages 11–14: storage, USB, serial and the panel, the interactive tests, the summary |

Block 23 starts with a signature, `OOHWDIAG`, the diagnostics' version and, at `$801C`, the version of the BIOS
they were linked with. The BIOS enters them only if all three are this build's: with no signature it says `NO
DIAGNOSTICS IN THE FLASH`, and with another build's `THE DIAGNOSTICS IN THE FLASH ARE ANOTHER BUILD'S: WRITE THE
FLASH AGAIN`. Blocks 22 and 21 are checked the same way before each is entered: one that is erased has its stages
`NOT IN THE FLASH`, and the run still ends (block 22 ends it itself if block 21 is missing).

The core owns the machine while it runs: it starts with interrupts off and every card quiet, uses no RAM it hasn't
tested, and writes all of RAM. Nothing in RAM is kept, OODOS's memory and M: included; the boot afterwards copies
the BIOS into block 63 again, and OODOS loads afresh from the card.

## The three entries

Status: **emulated**

| Entry | How | It asks | Options |
|---|---|---|---|
| the reset | switch 19 up, then a hard reset or START, with the front panel fitted | `DIAGNOSTICS: PRESS CONT OR Y WITHIN 5 S`: CONT on the panel or `Y` on the serial line or the USB keyboard enters; anything else, or nothing within 5 s, boots as usual | the switches |
| `$C812` | `C812R` at the monitor | `ALL OF MEMORY WILL BE OVERWRITTEN. Y TO GO ON`: anything but `Y` is the monitor again | the switches |
| `$C815` | a program, with A = `$A5`, Y = `$5A` and X the options; OODOS's `DIAG` at `A>` | nothing: the program has asked | X |

**The reset's prompt** comes before the BIOS copies itself, and nothing is written before CONT or `Y`: answered
no, or not at all, the boot finds RAM as it was. The lamps show `$A5` while it waits. A CONT pressed before the
reset doesn't count. The 5 s are counted by the DS3231's second; if the clock chip doesn't tick, the CPU counts
about 7 s itself and says `THE CLOCK CHIP DIDN'T TICK: 7 S COUNTED BY THE CPU`, then boots.

**The options.** At the reset's entry and `$C812`'s, the panel's switches: 18 up a full run, 17 up a loop, 16 up the
interactive tests (stage 13). At `$C815` only X counts: bit 0 full, bit 1 interactive, and no loop; switches left
up (after a LOAD ADRS, say) can't change a run a program asked for. `$C815` with anything else in A or Y is
`$C812`, question and all, so `C815R` typed at the monitor still asks; with no diagnostics in the flash it returns,
carry set, A, X, Y and W2 as they were.

**From `A>`,** `DIAG` ([the User's Guide](../oodos-users-guide/04-built-in-commands.md#util-diag)) asks first, then
calls function 75 with A = 2 ([the Programmer's Guide](../oodos-programmers-guide/07-function-reference.md#bdos-75)).
OODOS looks at block 23 through W2 (the signature, and the BIOS version at `$801C` against the one the cold boot
read from the greeting), and without them returns A = `$FF`, X = `$FC` and goes on. Otherwise it writes the changed
buffers back, stops the Disk II (its write mode and motor off, and the DMA controller's master enable put back if a
write had it off), turns its interrupts off (the VIA's timer 1, the video, USB and sound cards' enables), puts W0–W2
on RAM blocks 0–2 and S at `$FF`, then W3 and VEC on block 63, the BIOS's RAM copy, and jumps to `$C815`, with
`$C800`'s address on the stack below it: should `$C815` refuse after all (a build of the diagnostics whose own
version isn't the running BIOS's, which OODOS can't check), its return restarts at `$C800`, as `C800R` does, and
boots OODOS from the card. What
OODOS doesn't stop, the core's stage 0 does, as for every entry.

## The stages

Status: **emulated**

Each stage needs only what the stages before it proved; a stage whose card isn't there says `SKIP` and the run goes
on. Stages 0–2 use no RAM: stage 2 is what finds the zero page and stack the rest use.

**All of RAM, since BIOS 0.17** (*proposed*, B7). On a revision 2 north bridge (ID `$D2`) the diagnostics clear
MAP's AP8 as they start, whatever video card is fitted, so bank 8 is RAM and stages 2–6 test all 64 blocks, blocks
32–35 among them; V4–V7 reach the card's aperture at its own place, `$E00000`, through W1 with WIN1H `$0E`. The boot
after the run sets MAP back by the BIOS's rule ([chapter 11](11-north-bridge-and-rtc.md#reg-c6a5)). On a `$D1`
bridge they pass over blocks 32–35 while a revision 2 card makes bank 8 the aperture, as before.

| Stage | Block | What it tests |
|---|---|---|
| 0 | 23 | nothing: SEI and CLD; every card made quiet as the BIOS's Break does (every DMA channel aborted, all eight, each CARD put back to its reset value, the SD card's command abandoned, every card's interrupt enable off, the sound card's every sound stopped by CTRL's RESET, the VIA's IER off, the clock's TIE off); W3 on flash block 31 and VEC on block 23, the core's own vectors, so Break is the core's; the lamps on; the 6551 at 19,200 baud; the text screen, if the video card is revision 2 |
| 1 | 23 | the flash: the sums of blocks 21–31, each against the sum recorded when the ROM was built |
| 2 | 23 | zero page and stack: a march over the first 512 bytes of a RAM block for W0, from block 0 up, the block also proved to have no other address landing on them; a block that fails is passed over |
| 3 | 23 | the CPU (it must not answer the 65C816's XBA: the design's W65C02S); the data lines, walking 1s and 0s at each RAM module's first block (0, 16, 36, 48). A module whose two reads of a written 0 are both `$FF`, or differ, is `NOT ANSWERING`, and its blocks are left out of stages 4–6 |
| 4 | 23 | the address lines and the blocks: every block tagged, then all read back. A wrong tag names a line (`ADDRESS LINE A5`), two lines, two blocks as one, or a block answering as W0's |
| 5 | 23 | address in address over all of RAM |
| 6 | 23 | March C− over all of RAM, background `$00` (a full run: `$00`, `$55`, `$33`, `$0F`); W0's own first 512 bytes last, from another block that passed |
| 7 | 22 | the north bridge and the clock: NB1 its ID and revision (since BIOS 0.17, revision 2, `$D2`, and since BIOS 0.20 revision 3, `$D3`: a `$D1` bridge is `NOT KNOWN TO THIS BUILD` and the stage says `SKIP`); NB2 W0's window bits, each block as itself; NB3 IRQL and IRQH, and DMA's line; NB4 the clock chip (BUSY, SEC stepping a second); NB5 the CPU's clock by the DS3231 and NB6 by 60 video frames; NB7 WAIT0–3 and VEC read back; NB8 a read-only window keeping its RAM; since BIOS 0.20, on a revision 3 bridge (`$D3`; on `$D2` each `SKIP`s): NB9 the random number chip (a locked chip, its pool not one value nor one 4-byte group repeated, a fresh draw within 150 ms), NB10 the timebase (MSF and IRQH bit 5, the counter across a second, MSS at three second changes, SYNC) and NB12 the common hole (a tested block's top 2 KB at `$F800–$FFFF`, RO, and W3's again with HOLE 0). NB11 is kept for the deferred WINnH and MAP readback |
| 8 | 22 | DMA, revision 2 (ID `$82`; a revision 1 controller is skipped): D1 the ID, and each channel's registers through CONTROL's SELECT, 0–7, CARD among them, then each CARD put back to its reset value, and CONTROL's BURST and SELECT bits (a channel whose window reads `$FF` and drops a pattern counts as not fitted, and the count is on D1's line: 8 since B3 (4 in B2)); D2–D4 on channel 3, selected again after D1; D2 a fill of all of RAM, checked by the CPU; D3 a copy between two blocks, up and down; D4 a 2 KB fill's time; D5 the terminal count and an abort part way |
| 9 | 22 | the video card: V1 its ID and MEMSIZE; V2 every register that reads back; V3 the beam's lines and a frame's cycles; V4 all of card memory, address in address (a full run adds the march); V5 past MEMSIZE; V6 the data port against the aperture; V7 DMA channel 1 to the card and back |
| 10 | 22 | the sound card, revision 3 (`$A3`; another revision is `NOT KNOWN TO THIS BUILD`), driven only by its command records, the card reset first by CTRL's RESET and its store emptied after (FREEALL): S1 its ID and the firmware READY within 250 ms (S2–S6 skip if it isn't); S2 IDENTIFY, its card type, MEMSIZE, FEATURES and version as the registers say; S3 SELFTEST of the store, wave RAM, the SIDs and the codec's loop, each part that fails named; S4 DMA channel 2, 4 KB of a pattern into DATA as a sample's load, then CHECKSUM: the card's CRC-32 of what came against the host's of what was sent; S5 the event path: with IEN's EVT on, a NOP's REPLY pulls IRQH bit 2 low, and reading it lets the line go; S6 a raw PCM stream at 8,000 Hz, its frames played counting 8,000 a second within 0.5% |
| 11 | 21 | storage, read only: SD1 the SD card's ID and the card in the slot; SD2 IDENTIFY; SD3 LBA 0 by the CPU and by channel 0, which must agree; SD4 the read speed; DK1 the Disk II by its ID alone |
| 12 | 21 | USB, serial and the panel: U1 the USB card's ID and devices; U2 the paddles settling; SR1 the serial card's ID; SR2 the VIA's timers 2 and 1; SR4 the TL16C552A's loop (not in this build); P1 the panel's ID and switches; P2 a bit walked across the lamps |
| 13 | 21 | the interactive tests, asked for: V8 a test card on the screen; S10 the sound card's own sounds by command, a music block's note on a SID voice and a looped sample effect on the ES5503; U3 a key on the USB keyboard; U4 the mouse and the game port's buttons; U5 the speaker; SR3 both joysticks; P3 the breakpoint, a read of `$BD1A5` (RAM block 47, which stage 6 must have passed) halting the CPU until CONT. Otherwise `SKIP` |
| 14 | 21 | the summary: the tests passed, failed, skipped and measured, and the result |

A question in stage 13 is answered `Y`; Esc on the terminal skips U3, U4 and SR3, and 30 s with nothing fails them.
P3 holds the CPU: until CONT nothing runs, no interrupt is taken, and the USB card's FIFO can fill. A run's end,
and Break, write BPCTRL 0 and clear HIT when BPADDR still holds `$BD1A5`, as the BIOS's reset does
([chapter 12](12-front-panel.md#reset)).

## What they never do

Status: **emulated**

- **Write the SD card.** Stage 11 reads only: IDENTIFY and reads. A card that stays busy is reset (CTRL's RESET,
  which abandons the command and writes nothing) and given no further command.
- **Write the flash.** A write that reaches one of ROM A's two SST39SF020A chips may be a step of one of its
  command sequences ([chapter 2](02-memory-and-banking.md#reprogramming-the-flash)), so no window onto the flash is
  written; stage 7's read-only check is a write through a read-only window onto RAM.
- **Write `NB_MODE`,** so never Apple II mode: only a hard reset leaves it
  ([chapter 13](13-apple-ii-mode.md)).
- **Touch the Disk II's switches,** which move its stepper and motor: DK1 reads its ID byte, nothing else.
- **Keep anything in RAM,** or in card memory: all of both is written. Apple II mode writes its memory afresh each
  time it is entered.

## The lights

Status: **emulated**

With the front panel fitted the lamps show DISPLAY (CTRL's bit 3 set; [chapter 12](12-front-panel.md#reg-c705)):

| Lamps | Meaning |
|---|---|
| `$A5` | the reset's prompt, waiting for CONT or `Y` |
| `$00`–`$0E` | stage n running |
| `$FF`, steady | the end: everything passed |
| `$C0` + n, steady | the end: n stages failed |
| `$80` + n, then the block, then the bits wrong, 0.5 s each, round again | stage n failed so that the run can't go on: stage 1 with block 23, the core's own, wrong, or stage 2 with no block left for a zero page and stack. It stays so until a reset |

## The screen and the serial line

Status: **emulated**

**The serial line** (19,200 baud, 8 bits, no parity, as the BIOS sets it) has the whole report: the title, a line
for each stage with its result at column 60, and under each card stage a line for each test, `PASS`, `FAIL`, `SKIP`
with why on the line under it, or `INFO` for a measurement, each failure's detail under it. A card stage's tests come
before its own line. The run's form follows stage 2: `BIOS 0.20`, the pass, and `QUICK` or `FULL`, with `LOOP` and
`ASK` as asked. Then `TESTS: n PASSED, n FAILED, n SKIPPED, n MEASURED`, the `RESULT` line (`ALL PASSED`, or the
stages failed, and the lamps' code) and the keys:

```
 6  MARCH, ALL RAM, BACKGROUND 00 ......................... PASS
    NB1  ID $D3; STATUS'S REVISION 3 ...................... PASS
    ...
    NB10 THE TIMEBASE ..................................... PASS
    NB12 THE COMMON HOLE .................................. PASS
 7  NORTH BRIDGE AND CLOCK ................................ PASS
    ...
13  INTERACTIVE ........................................... SKIP
      SWITCH 16, OR DIAG [ASK] AT A>
TESTS: 32 PASSED, 0 FAILED, 2 SKIPPED, 4 MEASURED
RESULT: ALL PASSED                                      LIGHTS FF
CONT OR RETURN: BOOT        EXAM OR ESC: MONITOR
```

**The screen,** with video card revision 2, is a fixed form: the title and the form on row 0, stage n's line on
row n + 2, the run's first failure's detail on row 18 (the serial line has every detail), the tests' line on row
19, the result on row 20, then the keys.
While V4–V7 have card memory the screen is off; it is drawn again, font and palette, after V7. With no video card
the report is on the serial line only; with no front panel there are no lamps.

## The end and the way back

Status: **emulated**

With switch 17 down (and always from `$C815`), the run waits for a key: on the serial line, the USB keyboard, or the
panel's CONT or EXAM (ignored at LOCK). Then it puts back what it changed to what /RESET leaves (the video card's
slots and port, DMA CONTROL `$70` and every channel's CARD its reset value, the sound card reset (CTRL's RESET), the VIA's ACR, IER and IFR 0 and timer 1 still,
every wait state 0, BPCTRL as above, DISPLAY and CTRL 0, W0–W2 on blocks 0–2 and VEC on flash block 31), and:

- **CONT, RETURN or any other key: the boot,** at the BIOS's `BOOT`, past the reset's switch check: the BIOS copied
  into block 63, the splash, then the SD card's `/BOOT.BIN`. On the OODOS card that is OODOS, so a run from `A>`
  comes back to `A>` as after a power-on.
- **EXAM or Esc: the monitor,** at `MONBOOT`: the BIOS copied and the greeting, with no splash and no SD boot.

The two ways back are fixed jumps at `$FFF4` (`BOOT`) and `$FFF7` (`MONBOOT`), under the vectors, so the core
reaches them whatever else the BIOS's build moves.

With switch 17 up the run loops from stage 3, counting passes on the form's line, and a key ends it after the pass
under way. After a fatal failure it never ends. **Break** during a run says `BREAK`, puts back as the end does,
and boots. With switch 19 still up, the next hard reset asks again.

## How long a run takes

Status: **emulated**

Measured on the emulator at 7,159,090 Hz, from CONT (or `$C815`) to the keys' line, on `backplane.machine` with
every card: **a quick run 60.3 s and a full run 171.6 s with BIOS 0.21** (stage 10 on sound card revision 3; BIOS
0.20 on north bridge revision 3 took 61.6 s for a quick run, BIOS 0.19 58.7 s, and BIOS 0.19's full run 171.8 s).
The prompt, unanswered, waits 5.97 s. Both were measured
with BIOS 0.17's test of all 64 RAM blocks (B7, *proposed*), 2.2 s and 6.2 s longer than with bank 8 passed over
(56.5 s and 165.6 s, with DMA revision 2: B2, *proposed*). On real hardware
the cards' wait states (/WAIT), and the SD card's read speed, aren't known yet.

Where the time goes: stages 1–6, the flash and all of RAM, about 28 s of a quick run (address in address and one
background of the march, with stage 6's own test of W0's first 512 bytes); NB5's and NB6's measurements about 7 s
together (three windows each, back to back: four changes, and of their six pairs the one whose bigger gap, over the
windows between them, is least, the wider of equals, gives the span, since BIOS 0.16; BIOS 0.15 kept the middle
window); V4's 2 MB of card memory about 10 s; P2's walk across the
lamps 1.6 s; the report on the serial line, each character waiting for the 6551 as ECHO does, about 2 s. A full run
adds three backgrounds of the march over all of RAM, the march over card memory, and S2's and S8's second pass. A
loop repeats from stage 3 at the same cost a pass.

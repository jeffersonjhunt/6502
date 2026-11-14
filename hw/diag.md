# DIAG and SYSINFO: the OO-1's diagnostics and system information #

Nov 13, 2026 · @Jefferson

**Status: plan 2, the owner's revision of 2026-11-13: every diagnostic in the ROM. Proposed, for the owner's
approval before D3a starts; design only.** [Plan 2](#plan-2) is the plan from here on. D1 (the library and
SYSINFO) is merged and stays as built. D2 (DIAG.COM) was built on `feat/diag-d2` and **doesn't merge**: under
OODOS it grew to 22 KB, half of it OODOS's way round the hardware, and still couldn't reach OODOS's memory, the
console's card memory or a DMA channel of its own; so the owner put all of it in the ROM, where the core owns the
machine, and `DIAG` at `A>` only drops into it. D2's record is kept ([D2: as built](#d2-as-built)), and its code is
the source D3 carries forward.

*Plan 1's status (approved 2026-11-13, the owner, after review round 1):* the owner's decisions of 2026-11-13
(below) and [answers](#the-owners-answers) to questions 1–9 stood, answer 1 amended by the review (CC6), answer 2
replaced by the owner's option A (the prompt), review round 1's findings CC1–CC6 fixed in place. The sections after
plan 2 are plan 1's, kept as the record; each says where plan 2 replaces it.

Every register and ID named here was checked against [`backplane.inc`](../backplane/bios/backplane.inc) and the
emulator's devices (`emulator/components/devices/`), the reference until there is hardware; plan 2's again on
2026-11-13 (main 3562a9e). Where the real hardware's behaviour isn't known, this says so.


## Goals ##

Two system tools that share their code through a linked library:

1. **DIAG**, a diagnostics program in the dead-test style: it finds each card, checks what the card does, and
   tests memory. (Plan 2: it is all in the ROM, and `DIAG` at `A>` drops into it.)
2. **SYSINFO**, which lists what is installed: the cards and their revisions, memory, the clock speed measured,
   the peripherals connected, the firmware's versions.


## The owner's decisions (2026-11-13) ##

**The redesign (2026-11-13, the owner), which replaces the first two decisions below:**

- **The diagnostics live in the ROM only.** D3's core grows into all of them: the no-RAM stages, every memory test
  over all of RAM (OODOS's memory included, since OODOS isn't running) and every card test. It is entered three
  ways: switch 19 up at reset, confirmed by CONT or `Y` within 5 s (as decided); `C812R` at the monitor, with its
  `Y` prompt; and `DIAG` at `A>`, which confirms and then drops into the core, "kind of like how we load Apple2 or
  WozMon", and doesn't come back: the machine boots again afterwards, as it must after Apple II mode.
- **DIAG.COM as built in D2 doesn't merge;** `feat/diag-d2` is kept as the source to carry forward.
- **Safe by default no longer applies:** the ROM owns the machine. Answer 5 stands: the ROM never writes the SD
  card, so the card tests are read-only and `/WRITE` goes.
- **SYSINFO stays under OODOS,** as merged in D1. Answer 1's exception now covers SYSINFO alone, and whatever the
  DIAG launcher needs.
- **Unchanged:** the CONT entry, the flash for the core (block 23, or more blocks), BIOS 0.14, serial ID `$61`.

*Plan 1's decisions, as they were:*

- **Diagnostics run in two places.** *(Replaced by the redesign: all in the ROM.)*
  - **A ROM core in the BIOS**, dead-test style. It runs with no working RAM assumed, so it can find a bad bank
    that would keep OODOS from booting. It reports on the screen, on the serial line and on the front panel's
    lights. It is entered by a `C8xxR` command, and by a front-panel hold at power-on.
  - **`DIAG.COM` under OODOS**, for the deeper card tests: video, sound, SD, USB, serial, RTC, DMA and the north
    bridge.
- **Safe by default.** *(No longer applies: the ROM owns the machine.)* Under OODOS the default tests are
  non-destructive: RAM that OODOS doesn't own, card memory outside the console's, and registers and loopbacks it
  can put back. Destructive tests (all of RAM, writing an SD card) run only from the ROM core, or with an explicit
  option and a confirmation.
- **The shared code is a linked ca65 library,** `oohw.lib`: card discovery by module ID, revision and register
  probes, clock measurement, report formatting. It adds no OODOS or OOLIB interface, so the rule that interfaces only
  grow isn't touched. *(Stands: SYSINFO links it under OODOS, and the ROM builds it into each of its blocks, [plan
  2](#plan2-rom-space).)*


<a id="plan-2"></a>
## Plan 2: every diagnostic in the ROM ##

**Proposed (2026-11-13), for the owner's approval before D3a starts.** This section is the plan from here on;
where it and plan 1's sections after it differ, it wins. What it keeps of plan 1 it names; what it doesn't name is
replaced.

**Kept from plan 1:** the [card discovery](#card-discovery) rule and CC5's double read; the reset's
[prompt](#the-rom-core) (switch 19, CONT or `Y` within 5 s, nothing written before the confirmation); `C812R` and
its `Y` prompt; the core's signature check; stages 0–2 in the no-RAM style; the [memory test
algorithms](#the-memory-tests); the [lights](#the-rom-core); BIOS 0.14; [SYSINFO](#sysinfo) and the library's
OODOS build, as D1 built them.

**Replaced:** [DIAG.COM](#diagcom) and the safe-by-default rules; plan 1's [ROM space](#rom-space) (one block, 23);
its stages 7–12; its [interfaces](#the-interfaces-it-touches) (now the launcher's too); its [D3](#d3-the-rom-core-and-bios-014),
split into D3a–D3c below; answer 7 (`WOZMON`, then `C812R`), replaced by `DIAG` at `A>`.


<a id="plan2-stages"></a>
### Stages and order ###

One program, entered as plan 1's core was, from the no-RAM stages out to every card. Each stage needs only what the
ones before it proved; a stage whose card isn't there says `SKIP` and the run goes on.

| Stage | Flash block | What | Needs | No RAM? |
|---|---|---|---|---|
| 0 | 23 | SEI, CLD; every card made quiet by macros, as the BIOS's QUIET does it (every DMA channel aborted and STATUS cleared; the SD card's command abandoned and SD_IEN off; USB_IEN, VIDEO_IEN, SOUND_IEN and CONSOLE_IEN off; the 6510 stopped and the SIDs silent; on sound revision 2 the stream flushed and the ES5503 frozen; the VIA's IER off; the clock's TIE off); W3 on flash block 31 and VEC on block 23; the lights on, DISPLAY `$00` (stage 0); the 6551 at 19,200 baud; the screen, if the video card is `$92` (plan 1's stage 0) | the CPU, the north bridge, the flash | yes |
| 1 | 23 | the flash's sums: blocks 21–31 (plan 1's stage 1, two more blocks) | stage 0 | yes |
| 2 | 23 | zero page and stack (plan 1's stage 2) | stage 0 | yes |
| 3 | 23 | the CPU (CC1's XBA test: a W65C02S, not a 65C816); the data lines, walking 1s and 0s at each RAM module's first block (0, 16, 36, 48) | stage 2 | |
| 4 | 23 | the address lines and the blocks: every RAM block tagged, then all read back | stage 3 | |
| 5 | 23 | address in address over every RAM block | stage 4 | |
| 6 | 23 | the march, background `$00` (full: four) | stage 5 | |
| 7 | 22 | the north bridge and the clock | stage 6 | |
| 8 | 22 | DMA, every channel driven directly | stage 6 | |
| 9 | 22 | the video card | stages 6, 8 | |
| 10 | 22 | the sound card; the 6510 program loaded by the core | stages 6, 7, 8 | |
| 11 | 21 | storage, read-only: the SD card; the Disk II by its ID alone | stages 6, 8 | |
| 12 | 21 | USB and the game port; the serial card and the VIA; the front panel | stage 7 | |
| 13 | 21 | the interactive tests (switch 16, or `DIAG [ASK]`): `SKIP` otherwise | stages 9, 10, 12 | |
| 14 | 21 | the summary; the loop, or the end | | |

**Every RAM block** means all 60 with video revision 2 (bank 8, blocks 32–35, is the card's aperture and is tested
as card memory in stage 9), 64 with revision 1. Entered from `A>`, that includes all of OODOS's: block 31 (its zero
page, stack and SCB), 48–55 (M:, which every start empties anyway), 60–63 (the BDOS, its BIOS, the resident code
and the ROM's RAM copy). Nothing in RAM is kept; the boot afterwards copies the BIOS into block 63 again and OODOS
loads afresh from the card.

**What the core never does:** write `NB_MODE` (APPLE is undone only by a hard reset), touch the Disk II's switches
(they move its stepper and motor), write the SD card (answer 5), or write through a flash window. Plan 1's stage 10
wrote through one to see that nothing changed; a write that reaches the SST39SF040 is a step of one of its command
sequences, so the core sends none. Its read-only check is a write through an `NB_RO` window onto RAM (stage 7).


<a id="plan2-rom-space"></a>
### The flash: blocks 21–23 ###

The flash now (BIOS 0.13, unchanged since plan 1 measured it at 8877ba7): blocks 0–23 erased (384 KB); 24–28 the
splash's wave image; 29 the video demo; 30 the font; 31 the BIOS, with 7,632 bytes free.

**Recommended ([question P2-3](#plan2-questions)): the diagnostics take blocks 21, 22 and 23; 0–20 stay erased
(336 KB).** The BIOS's blocks (24–31) and its jump table stay where they are. `bios.cfg`'s `PAD` becomes
`$0000–$53FFF` (21 blocks), followed by three areas, `DIAG21`, `DIAG22` and `DIAG23`, each at `$8000`, `$4000`
long, so `bios.rom` stays 512 KB with nothing else moved.

**Sizes,** estimated from D2's map (DIAG.COM's own code 12,120 bytes and its text 6,170; the library's modules
2.4 KB; the 6510 program 799 bytes), plan 1's core, and what changes in the ROM (no claims, event queue, W1
contract or ^C; direct DMA, sound and screen instead):

| Block | Holds | Estimate |
|---|---|---|
| 23 | the header and gate (below); the reset's prompt 0.3 KB; stages 0–2 by macros 1.45 KB, stage 0's quiet included; the console (screen, serial, lights, keys, the form) 1.2 KB; stages 3–6 and the window mapping 1.5 KB; the library's memory and report modules 1.6 KB; text 0.8 KB; the sums 64 bytes; the end and Break 0.2 KB | about 7.1 KB |
| 22 | the header and gate; the console 1.2 KB; the library's report, memory, port, clock and RTC modules 2.0 KB; the north bridge and clock 1.2 KB, DMA 1.6 KB, video 1.35 KB, sound 1.45 KB; text 2.1 KB; the 6510 program 0.8 KB | about 11.7 KB |
| 21 | the header and gate; the console 1.2 KB; the library's report, probe, names and IDENTIFY modules 1.6 KB; SD 0.9 KB, USB 0.8 KB, serial 0.45 KB, panel 0.6 KB, the interactive tests' prompts 0.5 KB, the summary and the end 0.4 KB; text 1.4 KB | about 7.9 KB |
| 31 | the BIOS's part: the reset's switch and signature checks, `C812R`'s prompt, `$C815`'s check of its token, the way back to the monitor | about 0.2 KB more |

About 27 KB in 48 KB, 21 KB to grow in. **Why three blocks, not two:** each block carries its own console and
report routines (below), so blocks 21 and 22 in one would be about 17 KB, over the 16 KB a block holds.

**How code in three blocks runs: in W2, one block at a time, through a gate.** All three are seen at
`$8000–$BFFF` through W2, as plan 1's block 23 was: W0 is the core's zero page and stack, W1 the block under test,
W3 the BIOS's flash block. Only one is mapped at a time, so code can't call into another block: switching W2 takes
the caller out from under itself. So every block starts with the same header:

| Address | Bytes | What |
|---|---|---|
| `$8000` | 8 | `OOHWDIAG`, the signature the BIOS and the hand-offs check |
| `$8008` | 1 | the block's place: 0 for block 23, 1 for 22, 2 for 21 |
| `$8009` | 1 | the build's version, the same in all three, so a flash written in parts is seen |
| `$800A` | 6 | **the gate:** `STA NB_WIN2`, then `JMP ($8010,X)` |
| `$8010` | 16 | this block's entries, eight words |
| `$8020` | 5 | **the Break stub:** `LDA #$D7` (flash block 23, read-only), `STA NB_WIN2`; block 23 goes on at `$8025` to its handler |

The gate's bytes are the same in every block, so after its `STA NB_WIN2` the next fetch, the `JMP ($8010,X)`,
comes from the block just mapped and takes that block's entry X (the W65C02S's `JMP (abs,X)`). It needs no RAM.
A hand-off is `LDA #NB_ROM | NB_RO | 22` (`$D6`), `LDX #entry`, `JMP $800A`. Before it the caller reads the next block's
signature and version through W1, so a flash with that block erased, or from another build, is said (`STAGES 7-10:
NOT IN THE FLASH`) and passed over, never jumped into. The BIOS enters block 23 the same way from block 31 (it runs
in W3, so it needs no gate: it maps W2 and jumps through `$8010,X`).

The stages hand on in order: block 23 runs 0–6 and hands to 22; 22 runs 7–10 and hands to 21; 21 runs 11–14 and,
for the loop, hands back to 23's stage 3. No block calls another's code. What the run keeps (the switches and the
options, the pass, each stage's result and counts, the first failure for the lights, the chosen W0 block) is at
fixed addresses in the core's zero page, named by one include, `diag.inc`, which all three blocks assemble with.

**Break (NMI) and BRK.** VEC stays on block 23 while the core runs, so a vector is read from block 23's end
whichever block W2 holds. Its NMI and IRQ vectors are `$8020`, the Break stub, which is the same in every block:
it maps block 23 into W2, and block 23 goes on at `$8025` to plan 1's handler: `BREAK` on the screen and the
serial line, then the end as a key would (below). It doesn't RTI: the stack may be the one thing that is bad.

**One link, the library in each block.** `bios.rom` stays one ld65 link, so the core names the BIOS's labels
(`BOOT`, `MONBOOT`, below) and the BIOS the core's. Each block's unit (`diag23.s`, `diag22.s`, `diag21.s`) includes
the library's sources it needs, built with `-D OOHW_ROM`, which turns their `.export`s off: so each block has its
own copy under its own names, and ld65 never sees a name twice. The library's state that must cross a hand-off
(`HW_TALLY`, the failure) is at `diag.inc`'s fixed addresses under `OOHW_ROM`, not in a BSS of its own. The copies
cost about 3 KB of flash and nothing else. *Considered and not chosen:* one copy in block 23, called from the
others by a far call that maps W2 and back (it needs RAM, and every string a caller passes is in the caller's
block, out of sight once W2 changes, so each would be copied to RAM first); and the card tests copied into RAM and
run there (W0 and W1 are the tests' own).

**The sums:** `romsum.py`'s table, at block 23's end (before the vectors), covers blocks 21–31; stage 1 sums
176 KB, about 0.2 s.


<a id="plan2-card-tests"></a>
### The card tests, as ROM tests ###

D2's tests ([the table](#diagcom), [as built](#d2-as-built)), with what changes now that nothing belongs to OODOS.
Each card is found by [`HW_PROBE`'s rule](#card-discovery); a card that isn't there makes its stage `SKIP`. The
names are D2's where the test is the same.

**Stage 7, the north bridge and the clock.** Interrupts are off for the whole run, so every measurement polls.

| Test | Plan 2 | From D2 |
|---|---|---|
| NB1 | ID `$Dx`; STATUS bits 4–7 equal the ID's revision | as D2 |
| NB2 | the window bits: stage 4 has tagged every block through W1; here W0's bits too, a tagged block mapped into W0 for a few instructions with no stack used, then W0 put back | D2's through `GX_BLOCK` |
| NB3 | IRQL and IRQH: with every card quiet, every bit 1; then a line that works: channel 3's terminal count with MODE's IRQ bit pulls IRQH bit 0 to 0, and clearing STATUS lets it go | D2 only read the lines |
| NB4 | the RTC: BUSY clears within 1 ms; SEC steps by one a second; LOST clear (a warning: the time must be set) | as D2 |
| NB5 | the CPU clock against the DS3231: PHI2 cycles between two changes of `NB_SEC`, by VIA timer 1, free-running (the core's own now), and its wraps | D2 used timer 2 under OODOS's tick |
| NB6 | the CPU clock against the video frame: cycles over 60 frames, STATUS's vertical blank polled | D2 counted OODOS's frame interrupts, which other interrupts could hold off |
| NB7 | WAIT0–3 and VEC written and read back, then put back (WAIT3 read first: [the options](#plan2-launcher) from `A>`) | plan 1's stage 10 |
| NB8 | a write through an `NB_RO` window onto a tested RAM block leaves it as it was | plan 1's stage 10, less the flash write |

NB4's three seconds and NB5's measurement share their changes of `NB_SEC`, about 3 s in all. How polling SEC meets
the bridge's own I²C reads on real hardware is still unknown (plan 1's stage 11).

**Stage 8, DMA,** driven directly: every channel is the core's, with no function 82 or 83 between.

| Test | Plan 2 | From D2 |
|---|---|---|
| D1 | ID `$81`; each channel's ADDR, COUNT, MODE and DEST written with two patterns and read back; CONTROL's burst bits | D2 read the ID alone |
| D2 | channel 3, memory to memory with HOLD: every RAM block filled with a pattern but W0's first 512 bytes, the CPU checks it (960 KB, about 1.9 s) | D2 filled 2 KB |
| D3 | channel 3: a copy between two blocks, up, and down with MODE's DEC bit (bit 4; `backplane.inc` gains `DMA_DEC`) | as D2, and down too |
| D4 | D2's fill of 2 KB timed by VIA timer 2: 2 cycles a byte | as D2, less the call's cost |
| D5 | STATUS's terminal count; ABORT part way through a copy, COUNT reads what was left | plan 1's stage 7 |

D2's D5 (the SD card's DMA), D6 (the video port's) and D7 (the sound card's) move to their cards' stages, after
those cards' own memory is tested.

**Stage 9, the video card** (revision 2; revision 1 is listed and skipped, as BIOS 0.13 doesn't drive it). All of
card memory is the core's, the console's and Apple II mode's parts too; the screen is drawn again at the end.

| Test | Plan 2 | From D2 |
|---|---|---|
| V1 | ID `$92`; MEMSIZE reported (32 on a 2 MB card) | D2 asked at least 28 |
| V2 | every register that reads back as written ([video-rev2.md](video-rev2.md)'s map), with layer 1 off, in one vertical blank, then put back | D2 only those a program may write |
| V3 | the beam: LINE goes 0–524, a vertical blank every 119,437 cycles ± 0.1% | as D2, polled |
| V4 | card memory, all of it to MEMSIZE: block 32 (slot 0) in W1, SLOT0 stepped over each 16 KB card bank, address in address (full: the march); 2 MB in about 9.7 s (32 s) | D2: the program's 1.5 MB through slot 1 |
| V5 | a card address past MEMSIZE reads `$FF` and drops a write | D2 left it to the ROM |
| V6 | the port against the aperture: 64 KB written through PDATA read through the aperture, and the reverse, with PSTEP 1 and stepping down (`VIDEO_PCTL_DOWN`) | D2's V4 |
| V7 | channel 1: 4 KB into card memory through the port (PCTL's DRQ) and back (PCTL's FROM) into a tested RAM block; the aperture agrees | D2's D6 |
| V8 (interactive) | the test card on layer 1 (colour bars, a grid): Y or N | D2's V6 |

**Stage 10, the sound card.** No claim: the card is the core's. The core loads its 6510 program itself, through
RAMADDR and RAMDATA, as the splash loads its own and starts it with CTRL's RUN and RESET.

| Test | Plan 2 | From D2 |
|---|---|---|
| S1 | ID `$A1` or `$A2`; CODECS `$83` on revision 2 | as D2, without `SN_INFO` |
| S2 | the card's RAM, all 64 KB, through RAMADDR and RAMDATA with the 6510 stopped: address in address (full: the march); 0.3 s (1 s) | new: D2's 6510 couldn't test its own pages, so the host now tests all of them |
| S3 | channel 2: 4 KB into the card's RAM by DMA (DRQSEL 0, RAMDATA), read back through RAMDATA | D2's D7 |
| S4 | the 6510 program (`card6510.s`) loaded at the card's `$0200` with its reset vector, then run: `$A5` in MBIN within 0.5 s (STATUS's MBIN polled) | D2's S2, polled, not through the event queue |
| S5 | the mailbox both ways: 256 bytes, each echoed | D2's S3 |
| S6 | the 6510 sums S3's 4 KB and sends the sum | D2's D7's other half |
| S7 | the SIDs, by the 6510: voice 3's OSC3 and ENV3 on SID A and SID B | D2's S5 |
| S8 | revision 2: wave RAM, all 128 KB, through DOCDATA with AUTOINC and DOCBANK, the ES5503 frozen: address in address (full: the march); 0.6 s (2 s) | D2's S6, then `/FULL` only; the splash loads its samples again at the next boot |
| S9 | revision 2: a 4 KB raw PCM stream at 8,000 Hz (silence): STATE goes buffering, playing, ended; POS counts 8,000 frames a second ± 0.5% against VIA timer 1, which NB5 held to the DS3231 | D2's S7, against OODOS's millisecond counter |
| S10 (interactive) | a tone on each SID and on the ES5503: Y or N | D2's S8 |

D2's S4 (the card's RAM tested by the 6510 at its 1 MHz, 8 s for the march) goes: S2 covers all of it from the
host, faster. The 6510 program keeps its RAM test routine only if D3b finds a use for it; the rest of it (the start
byte, echo, sum, the SIDs) is D2's as built.

**Stage 11, storage, read-only** (answer 5: nothing is written to the SD card).

| Test | Plan 2 | From D2 |
|---|---|---|
| SD1 | ID `$71`; STATUS's PRESENT and READY | as D2 |
| SD2 | `IDENTIFY`: the type 1–3, the size not 0, the FAT flag | as D2 (`HW_SD_ID`) |
| SD3 | LBA 0 of unit 0, the card itself, read by the CPU through DATA and again by channel 0 into a tested block: the two agree | D2's D5 read DIAG.COM's records two ways |
| SD4 | read speed: 64 KB from unit 0 by channel 0, timed: `INFO`, KB a second | D2's SD3 through `DM_LOAD` |
| DK1 | the Disk II: ID `$C1`, and nothing else | as D2 |

D2's SD4 (`/WRITE`, a file on A: written, read back and erased) goes with DIAG.COM.

**Stage 12, USB, serial and the panel.**

| Test | Plan 2 | From D2 |
|---|---|---|
| U1 | ID `$B1`; DEVICES: `INFO` | as D2 |
| U2 | the paddles settle: PTRIG, then PADDLE0–3 read twice, 20 ms apart | D2 read OODOS's event area |
| SR1 | the serial card's ID `$61`; the stand-in 6551: `SKIP` (no ID register) | as D2 |
| SR2 | the VIA's timers: timer 2 across a 1,000-cycle loop less across none; timer 1 likewise | D2 timer 2 only |
| SR4 | once the TL16C552A exists: COM2 in its internal loopback, 256 bytes; COM1 is the console's line | as D2 |
| P1 | ID `$E1`; the switches and HALT/ENABLE shown: `INFO` | as D2 |
| P2 | a walking bit on DISPLAY, 0.2 s a step: `INFO` (interactive: Y or N) | as D2; nothing to put back but the stage's own code |

**Stage 13, the interactive tests,** only with switch 16 up or `DIAG [ASK]`: V8 and S10 above; U3, a key on the USB
keyboard (USB_KEY polled); U4, the mouse moved and each button pressed (MBTN, MX and MY), and each paddle button
(USB_GAME); U5, the speaker toggled at 1 kHz by the CPU through USB_SPKR (there is no `SN_BEEP`): Y or N; SR3, each
joystick direction and fire on both ports (the VIA's ports A and B, their DDRs left as inputs); and P3, the
breakpoint, as D2 built it (armed, then RETURN, then the read that halts until CONT; STATUS's HIT, LADDR and LDATA
checked; BPCTRL 0 and HIT cleared). In the ROM, CC3's ^C can't happen; Break goes to the core's handler, which puts
BPCTRL back as every way out does. A reset during P3 still keeps BPADDR and BPCTRL
([chapter 12](../docs/oo1-technical-reference/12-front-panel.md#reset)), and the next program to read that address
would halt. So D3b chooses P3's address where neither the BIOS's boot nor OODOS reads it, if there is such a place
(every RAM block may be a program's, and the breakpoint sees DMA cycles too); whether there is one is not known
yet. The Technical Reference says, as plan 1's User's Guide entry would have, to turn the breakpoint off
(`C70B: 00` at the monitor) after a reset during P3.


<a id="plan2-memory"></a>
### Memory tests: all of RAM ###

The algorithms are plan 1's ([The memory tests](#the-memory-tests)): walking 1s and 0s, the address tags, address
in address (about 33 cycles a byte for both passes), March C− (about 109 cycles a byte a background), and the DMA
fill. What plan 2 changes is the range: all of it, from every entry.

| Module | Blocks | Physical | Size |
|---|---|---|---|
| RAM A | 0–15 | `$00000–$3FFFF` | 256 KB |
| RAM B | 16–31 | `$40000–$7FFFF` | 256 KB |
| RAM C | 36–47 (32–35 are bank 8, the video card's aperture, with revision 2) | `$90000–$BFFFF` | 192 KB |
| RAM D | 48–63 | `$C0000–$FFFFF` | 256 KB |

960 KB with video revision 2; 1,024 KB with revision 1 (bank 8 is then RAM C's). Block 63's first 2 KB, under the I/O
hole when the CPU reaches the block through W3, is plain RAM through W1. W0's block is tested through W1 too, its
first 512 bytes last, from another block that passed (plan 1's rule). A module that isn't there fails all its blocks
alike and is named once ("RAM B (blocks 16–31): not answering").

**Time on the real 7.16 MHz** (7,159,090 cycles a second; counted from the instructions, so estimates; the cards'
wait states on real hardware aren't known, except the aperture's, which has none):

| What | Size | Address in address | March, 1 background | 4 backgrounds |
|---|---|---|---|---|
| a zero page and stack candidate (stage 2) | 512 B | | 8 ms (`$00`, `$55`) | |
| the flash's sums (stage 1) | 176 KB | about 0.2 s | | |
| all RAM, video revision 2 | 960 KB | 4.5 s | 15.0 s | 60 s |
| all RAM, revision 1 | 1,024 KB | 4.8 s | 16.0 s | 64 s |
| DMA fill and check of every RAM block (D2) | 960 KB | 1.9 s | | |
| video card memory (V4) | 2,048 KB | 9.7 s | 32 s | |
| the sound card's RAM through RAMDATA (S2) | 64 KB | 0.3 s | 1.0 s | |
| wave RAM through DOCDATA (S8) | 128 KB | 0.6 s | 2.0 s | |

**A quick run is about 40 s:** the memory 19.5 s, DMA 2 s, video 10 s, sound 2 s, the clock 3 s, the panel's
lights 1.6 s, the rest under a second each (the SD card's read speed on real hardware isn't known). **A full run
(switch 18, or `DIAG [FULL]`) is under 2 minutes:** the march with four backgrounds adds 45 s, the march on card
memory 22 s, on the sound card's RAM and wave RAM 2 s. The loop (switch 17) repeats from stage 3 at the same
cost a pass. `test_bios_diag` reports the emulator's time for a quick run against this table.


<a id="plan2-output"></a>
### Output, the end, and the way back ###

**The screen** is plan 1's fixed form, a line a stage, filled in as each ends, with the first failure's detail
under its stage; **the serial line** (19,200 baud, 8-N-1, as the BIOS sets it) gets the same stage lines and,
under each, a line a test in D2's style (name, dots, `PASS`, `FAIL`, `SKIP` with its reason, `INFO`), so a terminal
has the whole report. With no video card it is serial only; with no console card there are no lights.

```
OO-1 ROM DIAGNOSTICS 0.1          BIOS 0.14         PASS 1         QUICK
 1  FLASH BLOCKS 21-31 ..................................... PASS
 2  ZERO PAGE AND STACK: BLOCK 0 ........................... PASS
 3  CPU W65C02S; DATA LINES, RAM A B C D ................... PASS
 4  ADDRESS LINES, 60 BLOCKS ............................... PASS
 5  ADDRESS IN ADDRESS, 960 KB ............................. PASS
 6  MARCH, 960 KB, BACKGROUND 00 ........................... FAIL
      BLOCK 41 +0123 (A4123): READ 08, WROTE 00, BIT 3
 7  NORTH BRIDGE; CLOCK 7159090 HZ ......................... PASS
 8  DMA, 4 CHANNELS ........................................ PASS
 9  VIDEO $92, 2048 KB ..................................... PASS
10  SOUND $A2: RAM, 6510, SIDS, WAVE RAM, PCM .............. PASS
11  SD $71 (READ ONLY), DISK II $C1 ........................ PASS
12  USB $B1, SERIAL (NO ID), PANEL $E1 ..................... PASS
13  INTERACTIVE: SWITCH 16 OR DIAG [ASK] ..................... SKIP
RESULT: 1 FAILED                                        LIGHTS C1
CONT OR RETURN: BOOT        EXAM OR ESC: MONITOR
```

**The lights** (DISPLAY, with CTRL's bit 3 set; plan 1's codes, the stages renumbered):

| Lights | Meaning |
|---|---|
| `$A5` | the reset's prompt, waiting for CONT or `Y` (at most 6 s) |
| `$00`–`$0E` | stage n running |
| `$FF`, steady | the end: everything passed |
| `$C0` + n, steady | the end: n stages failed |
| `$80` + n, then the block, then the bits wrong, 0.5 s each, round again | stage n failed so that the core can't go on: stage 1's block 23 (its own code), or stage 2 with no block left for a zero page and stack. It stays so until a reset |

**The summary** is the `RESULT` line: the stages failed, the tests passed, failed, skipped and measured (D2's
count), and the pass with the loop.

**How the run ends.** With switch 17 down, the core waits for a key: on the serial line or the USB keyboard, or on
the panel CONT or EXAM (latched in KEYS while the CPU runs, as the prompt's CONT is; ignored at LOCK). Then it puts
back what it changed to the values /RESET leaves (plan 1's list: SLOT0–3 0–3, PSTEP 1, PCTL 0; DMA CONTROL `$70`;
the sound card's CTRL with RUN off; WAIT0–3 0, which also clears the options; BPCTRL 0 if P3 armed it; DISPLAY 0
and CTRL 0; W0–W2 on blocks 0–2; VEC on flash block 31), and:

- **CONT, RETURN or any other key: the boot,** at the BIOS's `BOOT`, just past the reset's switch check: the BIOS
  copied into block 63, the splash, then the SD card's `/BOOT.BIN`, which on the OODOS card is OODOS. So from `A>`
  the machine comes back to `A>` as after a power-on, and a key during the splash is the monitor, as always.
- **EXAM or Esc: the monitor,** at a new label, `MONBOOT`, after the BIOS's copy into block 63 and the greeting,
  with no splash and no SD boot ([question P2-5](#plan2-questions)). The copy is needed: all of RAM was rewritten,
  block 63 included, and the monitor, Break and OODOS's way back to the ROM all run from that copy.

Switch 17 loops without a key or a confirmation, counting passes; a key ends the loop after the pass under way.
After a fatal failure it never ends. With switch 19 still up the next hard reset shows the prompt again, and passes
on to the boot after 5 s.


<a id="plan2-launcher"></a>
### The DIAG launcher at `A>` ###

**How WOZMON and APPLE2 are built today** (checked: `oodos/sys/ccp.s`, `bdos.s`, `common.s`): both are CCP
built-ins (the `BINAMES` table), and both call function 75, `O_ROM`, with A = 0 or 1. Function 75 writes the
changed buffers back (`CHANGED`, `FLUSHD`, as function 48), then leaves: A = 0 through `NMI`'s tramp (the Disk II's
write mode and motor off, the DMA controller's master enable put back if a Disk II write had it off, W0 on the
system block, then W3 and VEC on block 63 and the ROM's Break); A = 1 through `ROMAPPLE` (SEI, `CLKOFF`, W0–W2 on
blocks 0–2, S `$FF`, then a tramp that puts W3 and VEC on block 63 and jumps to `$C806`). For A = 1 it first checks
the cartridge through W2 (on flash block 32) and returns A = `$FF` with X = `$FD`, `$FE` or `$FF` if something is
missing, and OODOS goes on.

**The options:**

- **(a) A small `DIAG.COM`** (about 0.5 KB): it asks `Y`, writes the buffers back with function 48, then does the
  leaving itself: SEI, the cards' interrupt enables and the VIA's IER off, W0–W2 on blocks 0–2, and a tramp that
  puts W3 and VEC on block 63 and jumps to `$C815`. **No OODOS change.** But it writes W3, VEC, the VIA's IER and the
  cards' enables from a program, all of which AGENTS.md keeps for OODOS, so answer 1's exception must grow to cover
  them; it repeats what `NMI` and `ROMAPPLE` know of OODOS's own state (the Disk II's write flag, the DMA master),
  or leaves it to the core; it can't see whether the flash holds the core without writing a window (W2 onto block
  23), so it would trust function 77's ROM version (0.14 or later), which a flash with only block 31 rewritten
  would get wrong; and it is a file the card must have. (WOZMON's own way out can't be reused: function 75 with
  A = 0 doesn't return.)
- **(b) A CCP built-in, `DIAG`,** like WOZMON and APPLE2, which is how those are built. It needs (c) underneath, or
  the CCP doing 75's work itself.
- **(c) Function 75 with A = 2,** the diagnostics: as A = 1 does for the IIe ROM, OODOS first looks for the core
  (block 23's signature and its entry table's version, through W2, put back on `BIOS_BLOCK` after) and returns
  A = `$FF`, X = `$FC` if it isn't there, and OODOS goes on; otherwise it writes the buffers back and leaves as
  `ROMAPPLE` does, its tramp jumping to `$C815` with A = `$A5` (the token) and X the options. **An OODOS change.**

**Recommended ([question P2-1](#plan2-questions)): (b) on (c), the built-in `DIAG` calling function 75 with
A = 2.** It is the owner's "like Apple2 or WozMon" exactly; OODOS leaves the machine itself, as it already does two
ways; no program writes a window, a vector or an interrupt enable, so answer 1's exception narrows to SYSINFO alone
(the owner's decision); and it is in OODOS.SYS, on every card, with no file. *Alternative:* (a), with no OODOS
change, at the cost above.

**What (b) on (c) costs:**

- **OODOS:** in the BDOS, function 75's A = 2 (the signature check reuses A = 1's look through W2; `ROMAPPLE`'s
  tramp takes its target and A and X instead of a fixed `$C806`), about 60 bytes; in the CCP, the built-in (its
  name, the prompt, the tail's words, three messages), about 150. **It is an interface addition:** a new value of
  A for an existing function, so OOLIB's interface version, which marks OODOS's own additions too (EE3), goes from
  9 to 10, and 75's entry in the Programmer's Guide gets the value with a Since line naming interface 10 (EE1). An
  OODOS before 10 answers 75 with A = 2 as any other A: A = X = `$FF`, and goes on, so a program can tell.
- **The cards** are made again (OODOS.SYS changes), and test_oodos_utils_rom gains DIAG's cases.
- **The User's Guide:** `DIAG` in chapter 4, with WOZMON and APPLE2.

**The built-in.** `DIAG`, `DIAG [FULL]`, `DIAG [ASK]`, `DIAG [FULL,ASK]` (the utilities' option form, chapter 4). It prints what will happen (OODOS stops,
all of memory is tested and overwritten, M:'s files are lost, the machine boots again at the end) and asks for
`Y`; any other key is `A>` again with nothing changed. Then function 75 with A = 2 and X the options (bit 0 full,
bit 1 interactive). If the flash has no core, it says so (`No diagnostics in the ROM: BIOS 0.14 or later`), the
return code `$FF00`, and OODOS goes on.

**What OODOS puts back or shuts down before the jump** (as `ROMAPPLE` and `NMI` do between them):

- the changed buffers written back (75 does it first for every A); a file left open isn't closed, as for 0 and 1;
- interrupts: SEI, then `CLKOFF` (the VIA's timer 1 interrupt, the video card's, the USB card's and the sound
  card's enables off);
- the Disk II: its write mode and motor off, and the DMA controller's master enable put back if a write had it off
  (`NMI`'s first lines);
- the windows: W0–W2 on RAM blocks 0–2, S `$FF`; the tramp puts W3 and VEC on block 63, the ROM's copy, and jumps to
  `$C815` there.

What OODOS needn't do, because the core does it at stage 0 for every entry (`C812R` and the reset need it too):
stop the DMA channels (channel 1's vertical-blank copies and channel 2's sound stop with their interrupts anyway),
stop the sound card's 6510 and silence its parts, put back the video card's state. Nothing OODOS keeps in RAM
matters: all of it is overwritten, and OODOS boots afresh after.

**`$C815`, the entry for a program** ([question P2-2](#plan2-questions)): the last spare in the jump table. With
A = `$A5` it takes X as the options, puts them in WAIT3's two bits for module F (bits 6 and 7: no card answers as
module F, so its wait states slow nothing; the north bridge keeps WAIT3 as written, and every /RESET clears it), maps
W2 on block 23 after the signature check, and enters at stage 0 with no prompt: OODOS has asked already. With any
other A it is `$C812`, prompt and all, so `C815R` typed at the monitor still asks. The options go through WAIT3
because nothing else survives stages 0–2, which keep every register busy and trust no RAM; NB7 reads them before
it tests WAIT0–3. The panel's switches 18 and 16 are ORed in. *Alternative:* OODOS jumps to `$C812`, which asks
`Y` a second time, the jump table grows no further, and the options come from the switches alone.


<a id="plan2-faults"></a>
### The emulator's faults ###

D2's test-only faults ([answer 8](#the-owners-answers)) carry into the ROM's suite, `test_bios_diag`:

- **`FaultyMemory`** (a device mapped over a range after the machine is built, forwarding to the RAM under it but
  forcing a bit or aliasing an address line; DMA sees it too, through the same mappings) moves from
  `test_oodos_diag.cpp` on `feat/diag-d2` into a shared test header, `emulator/test/host/fault_devices.h`, with D1's
  `FloatingBus` (CC5, now in `test_oodos_sysinfo.cpp`) beside it. Over RAM it fails stages 3–6 and D2; over bank 8 it
  makes the aperture and the port disagree (V6). No emulator code changes.
- **`SoundCard::set_ram_fault()`,** compiled only with `EMU_FAULT_HOOKS` (the native build and its tests, never the
  WebUI's), moves with D3b as D2 built it, with its own case in `test_sound`. The host's RAMDATA path (S2) and the
  6510 (S6) see it alike. It is the one emulator change, so D3b runs `test_sound` under the sanitizers.
- **Nothing new:** a missing card is the machine's text without its line; a damaged flash is a copy of `bios.rom`
  through `MachineOptions::rom_override` (a byte changed, a block blanked).


<a id="plan2-what-moves"></a>
### What moves from feat/diag-d2 ###

Nothing is merged or cherry-picked in this step. In D3a and D3b, each piece is taken from `feat/diag-d2`'s tip
(4fdc4f2) as said, so main never gets DIAG.COM or its build:

| Piece | How | When |
|---|---|---|
| `backplane/oohw/hwmem.s` (walking 1s and 0s, the tags, address in address, March C−), `hwline.s` (the report lines), and `oohw.inc`'s D2 additions | moved file by file (`git checkout 4fdc4f2 -- path`), then given the `OOHW_ROM` build | D3a |
| `backplane/oohw/hwport.s` (address in address through a port) | moved file by file, then `OOHW_ROM` | D3b |
| `backplane/oohw/card6510.s` and `card6510.cfg` | moved file by file; the BIOS's Makefile assembles it and block 22 includes the binary | D3b |
| `diag.s`'s card tests, NB1–P3 | re-done in place in `diag22.s` and `diag21.s`, with D2's code as the reference: every OODOS and OOLIB call in them changes | D3b |
| `FaultyMemory` | moved into `fault_devices.h` | D3a |
| the SoundCard hook: `sound_card.h`, `sound_card.cpp`, `devices/CMakeLists.txt`'s `EMU_FAULT_HOOKS`, `test_sound`'s case | moved file by file (the hunks, as main may have moved on) | D3b |
| `test_oodos_diag`'s cases | re-done as `test_bios_diag`'s, each with its must-fail fault | D3a, D3b |
| `oodos/utils/diag.s`, `oodos/cfg/diag.cfg`, the Makefile's DIAG rules, `golden/diag.txt`, the User's Guide's DIAG utility entry | not moved | |

`feat/diag-d2` is kept until D3b merges ([question P2-6](#plan2-questions)).


<a id="plan2-phases"></a>
### Sub-phases ###

Three branches, merged in order, each with the gate `tools/check.sh changed main`, the quick tier and `make -C
docs check`. Each reads its part of the books again at its start and adds its "as built" here.

#### D3a: the core, its entries, the memory tests ####

- **What:** `bios.s` (the reset's switch and signature checks; `$C812`'s prompt; `$C815`'s token and WAIT3; the
  `BOOT` and `MONBOOT` labels; the greeting `BACKPLANE BIOS 0.14`); `bios.cfg` (`PAD` 0–20, `DIAG21`–`DIAG23`); the
  Makefile (the three units, the library's ROM build, `romsum.py` after ld65); `backplane/bios/diag.inc`;
  `diag23.s` (the header and gate, the prompt, stages 0–6, the end and its put-back, Break); `diag22.s` and
  `diag21.s` with their headers and gates and the hand-offs, stages 7–13 each `SKIP` ("D3b"), and stage 14, so a run
  is whole from the start; `hwmem.s` and `hwline.s` from D2; `fault_devices.h`; `test_bios_diag`. `make -C docs check`
  holds chapter 3's listings to `bios.s`, so D3a updates those listings; the books' new text is D3c's.
- **Size:** block 23 about 7.1 KB of 16; blocks 22 and 21 about 2.5 KB each (the header, the console, the report);
  block 31 about 0.2 KB more of its 7,632 free bytes.
- **Tests,** `test_bios_diag`, on `backplane.machine` with the console card's switches set before the reset; each
  must-fail case is seen failing once on a copy with its one fault put in, and the report says which:
  - the prompt (plan 1's cases): CONT, `Y` on the serial line and on the USB keyboard enter; no answer boots within
    about 6 s with RAM as the test filled it; `N` boots at once; CONT pressed before the reset doesn't count; START
    at the prompt is a hard reset and the prompt again; switch 19 down, no prompt;
  - `C812R` with `N`, then `Y`; `C815R` with A not `$A5` asks as `$C812` does; `$C815` with the token and X = 1 runs
    full (the form says `FULL`);
  - golden serial output of a good quick run (`emulator/test/host/golden/romdiag.txt`), the screen's text read from
    card memory, DISPLAY `$FF`; then a key boots (the greeting, the SD card's boot), Esc is the monitor (and block 63
    holds the BIOS again: `C809R` answers);
  - the faults: a stuck bit in block 0's first 512 bytes (W0 moves to block 1, stage 6 reports block 0); a stuck bit
    in block 41 (stage 6); **a stuck bit in block 62, OODOS's resident block** (stage 6: plan 2 tests OODOS's
    memory); an address line aliased (stage 4 names the line); every RAM block bad (`$82`, the block, the bits,
    sampled over time); RAM B's line left out ("not answering"); a byte of block 30 changed in a copy of `bios.rom`
    (stage 1 fails block 30); block 22 blanked (stages 7–10 "NOT IN THE FLASH", and the run still ends); block 23
    blanked (`NO DIAGNOSTICS IN THE FLASH`, then the boot); no video card (serial only); no console card (no lights;
    the BIOS boots as now);
  - CC5's floating bus: no Disk II found;
  - the time of a quick run in emulated cycles, against [the table](#plan2-memory);
  - `test_bios`, `test_bios_sd`, `test_rom_boot`, `test_apple`, `test_iie`, `test_oodos_apple`, `test_oodos_utils_rom`:
    unchanged and passing.
- **Test map:** `backplane/bios/` and `backplane/oohw/` are mapped already (`bios docs web`; `bios oodos docs`);
  `test_bios_diag.cpp` is `self`. New rules: `emulator/test/host/fault_devices.h  test_bios_diag test_oodos_sysinfo`
  and `emulator/test/host/golden/romdiag*  test_bios_diag`, above the general ones. The suite's labels in
  `EMU_TEST_LABELS`: `bios bridge console video sound es5503 sid usb dma sd rtc backplane` (test_bios's, and `rtc`).
- **Moves:** `hwmem.s`, `hwline.s` and `oohw.inc`'s additions file by file; `FaultyMemory` into the header. No
  emulator code changes, so no sanitizer run beyond the gate's.

#### D3b: the card tests ####

- **What:** `diag22.s` (stages 7–10), `diag21.s` (11–13), `hwport.s` and `card6510.s` from D2, `DMA_DEC` in
  `backplane.inc`, the SoundCard hook; `test_bios_diag`'s card cases.
- **Size:** block 22 about 11.7 KB, block 21 about 7.9 KB.
- **Must-fail cases** (each seen failing once):
  - a stuck bit in the sound card's RAM (the hook): S2 names the card address and bit (the fault that makes it
    fail: S2's check pass left out);
  - an aperture fault (`FaultyMemory` over block 33): V6 (V6's compare left out);
  - a stuck bit in a RAM block under DMA: D2 names it as stage 6 did (the CPU's check after the fill left out);
  - DMA's down copy (MODE's DEC not set: D3 must fail);
  - the 6510 program not loaded (its start byte never comes: S4 fails within 0.5 s, and S5–S7 `SKIP`);
  - no sound card: stage 10 `SKIP`; no DMA controller line: stage 8 `SKIP`, and V7, S3 and SD3 `SKIP` with it;
  - **SD read-only:** the card's image compared byte for byte before and after a full run (shown failing with one
    write to the card added);
  - the clock: a wrong frame constant (119,000 cycles) fails NB6, as D1's case did for `HW_CLOCK`;
  - P3: the read halts the CPU, CONT goes on; Break during P3 leaves BPCTRL 0.
- **Test map:** `emulator/components/devices/` sound files are mapped already; the `EMU_FAULT_HOOKS` line is in
  `devices/CMakeLists.txt` (`all`). The emulator change runs `test_sound` and `test_sound_doc` under the
  sanitizers, at `-j4`.
- **Moves:** `hwport.s`, `card6510.s` and `.cfg`, and the hook's hunks file by file; the card tests re-done in place.

#### D3c: the launcher and the books ####

- **What:** `oodos/sys/ccp.s` (the `DIAG` built-in), `bdos.s` (75 with A = 2, the interface version 10),
  `common.s` (`ROMAPPLE`'s tramp with a target), `oodos.inc` (`O_ROM`'s comment); the cards made again; AGENTS.md's
  exception narrowed to SYSINFO.COM, its list cut to what SYSINFO reads and writes (the owner's decision); the books
  ([below](#plan2-books)); hw/design.md's Boot (BIOS 0.14, proposed).
- **Size:** about 210 bytes of OODOS; the build says whether OODOS.SYS still fits.
- **Tests,** in `test_oodos_utils_rom` (with WOZMON's and APPLE2's): `DIAG` and `N`: `A>` again, the card's image
  unchanged; `DIAG` and `Y`: the core's first lines on the serial line, then after its run a key and the boot brings
  OODOS's `A>` back; **a file written just before `DIAG` is on the card after the boot** (must fail with the
  write-back left out); a flash without the core (block 23 blanked): "No diagnostics in the ROM", return code
  `$FF00`, and a program's 75 with A = 2 comes back with A = `$FF`, X = `$FC`; `DIAG [FULL]` gives the form's `FULL`.
- **Test map:** `oodos/` and `docs/` are mapped; `test_oodos_utils_rom`'s labels (`oodos bios sd backplane`) cover
  it.

<a id="plan2-books"></a>
### The books ###

**The Technical Reference:**

- **Chapter 2:** the flash's table: blocks 0–20 erased, 21–23 the diagnostics.
- **Chapter 3:** the entry points' table: `$C812`, the diagnostics after `Y`; `$C815`, the diagnostics for a
  program that has asked already (A = `$A5`, X the options; any other A is `$C812`); "Reset and the flash": switch 19
  and the prompt; the listings of `bios.s` moved with it.
- **A new chapter 15, "Diagnostics"** (recommended over a section of chapter 3, which would double its length): the
  three entries, the stages, what each tests and never does, the lights, the screen and serial output, the end and
  the way back, the times. Chapter 14 (the emulator's notes) gets its line: the WebUI's panel sets switch 19.
- **Chapter 12:** switches 16–19 as the diagnostics read them; DISPLAY's codes; under Reset, that a breakpoint
  armed by P3 survives a reset.
- **Chapter 13, Apple II mode: no change,** checked against plan 2: the core never writes MODE, and nothing changes
  `$C806` or `apple.s`; the two ways into the mode (`C806R`, function 75 with A = 1) are as they were; Ctrl-Reset
  takes the IIe ROM's vector, never the flash's, so it can't reach the switch check; "only a hard reset leaves the
  mode" still holds, and a hard reset with switch 19 up shows the prompt before the boot, which is chapter 3's to
  say. The memory the core overwrites (all of card memory, the IIe's character ROM image at `$1E0000` with it, and
  RAM) is written afresh by `$C806` each time the mode is entered.

**The Programmer's Guide:** function 75's entry gains A = 2 and X = `$FC`, since interface 10.

**The User's Guide:** chapter 4 gains `DIAG`, beside `WOZMON` and `APPLE2` (forms, the prompt, what is lost, the
boot after, the error with no core), and in the chapter's opening list; chapter 2's list of the built-ins too. There is no DIAG utility entry: D2's
goes with DIAG.COM. SYSINFO's entry is as D1 left it.


<a id="plan2-questions"></a>
### Questions for the owner ###

- **P2-1, the launcher.** *Recommended:* the CCP built-in `DIAG` calling function 75 with A = 2 (OODOS's interface
  version to 10). *Alternative:* a small `DIAG.COM` and no OODOS change, with answer 1's exception grown to let it
  write W3, VEC and the interrupt enables.
- **P2-2, `$C815`.** *Recommended:* the last spare becomes the program's entry: the token `$A5` in A, the options in
  X, no prompt; any other A asks as `$C812` does. *Alternative:* OODOS jumps to `$C812`, which asks a second time;
  the table grows no further and the options come from the switches alone.
- **P2-3, the flash.** *Recommended:* blocks 21–23 for the diagnostics (about 27 KB used of 48), 0–20 erased.
  *Alternative:* blocks 20–23, a block spare for a later card's tests, so `PAD` needn't shrink again.
- **P2-4, the options.** *Recommended:* switch 16 up for the interactive tests (18 full and 17 loop, as decided);
  from `A>`, `DIAG [FULL]` and `DIAG [ASK]`, carried through WAIT3's bits for module F. *Alternative:* the panel's
  switches only, so from `A>` without a console card the run is always quick and not interactive.
- **P2-5, the way back.** *Recommended:* a key boots (OODOS comes back from the card); Esc or EXAM is the monitor,
  through a new internal label after the BIOS's copy, with no splash and no SD boot (not a jump-table entry).
  *Alternative:* the boot only; the monitor by a key during the splash, as after any reset.
- **P2-6, `feat/diag-d2`.** *Recommended:* kept until D3b merges, then deleted by the coordinator; its record is
  [D2: as built](#d2-as-built) here. *Alternative:* kept for good.
- **P2-7, the books' chapter.** *Recommended:* a new chapter 15, "Diagnostics", in the Technical Reference.
  *Alternative:* a section of chapter 3.

D2's questions D2-1 (DIAG.COM's size), D2-2 (the cards) and D2-3 (S4's march) are moot with DIAG.COM.


## What the hardware gives ##

Checked against `backplane.inc` and the emulator (2026-11-13, main 8877ba7).

**ID bytes.** Every module with registers has an ID byte at `$C000 + ID × $80 + $7F`: the module ID in the high
nibble, the revision in the low ([design.md](design.md#north-bridge-registers)). An address nothing answers reads
`$FF` in the emulator (`Decoder::UNMAPPED_READ`); on the backplane the data lines' weak pull-ups (for /IACK) should
give `$FF` too, but that isn't measured.

| ID | Module | ID byte | Value now | Equate | Notes |
|---|---|---|---|---|---|
| 0–3 | RAM A–D, 256 KB each | none | | | memory only: their I/O blocks `$C000–$C1FF` are unused. Found by memory tests alone |
| 4, 5 | ROM A (flash), ROM B (cartridge) | none | | | memory only (`$C200–$C2FF` unused). ROM B blank reads `$FF` |
| 6 | serial, parallel and VIA | `$C37F` | **none defined** | | the TL16C552A card isn't built. The emulator's stand-in 6551 mirrors its four registers through the block, so `$C37F` reads the 6551's control register ([answer 6](#the-owners-answers)). The VIA at `$C340` has no ID of its own |
| 7 | mass storage (SD) | `$C3FF` | `$71` | `SD_ID_V1` | |
| 8 | DMA controller | `$C47F` | `$81` | `DMA_ID_V1` | |
| 9 | video | `$C4FF` | `$91`, `$92` | `VIDEO_ID_V1`, `VIDEO_ID_V2` | BIOS 0.13 and OODOS drive revision 2 alone; `backplane.machine` has `rev=2` |
| A | sound | `$C57F` | `$A1`, `$A2` | `SOUND_ID_V1`, `SOUND_ID_V2` | the BIOS takes any `$Ax` (`SOUND_TYPE`) |
| B | USB, game port, speaker | `$C5FF` | `$B1` | `USB_ID_V1` | |
| C | Disk II controller | `$C67F` | `$C1` | `DISK2_ID_V1` | only `$C600–$C60F` are switches; `$C67F` is the ID alone (`disk2_card.h`) |
| D | north bridge (no card) | `$C6FF` | `$D1` | `NB_ID` (no value equate) | `BackplaneBridge::ID`, revision 1; STATUS bits 4–7 repeat the revision |
| E | console (front panel) | `$C77F` | `$E1` | `CONSOLE_ID_V1` | |
| F | free | | | | nothing is defined at `$C780–$C7FF` |

**Other registers the tools read for SYSINFO,** all plain reads with no side effect in the emulator:

| Register | What it says |
|---|---|
| `NB_STATUS` (`$C686`) | bit 0 booted from RAM (/BOOTSEL), bit 1 the last reset was power-on, bits 4–7 the bridge's revision |
| `NB_RTCCTL` (`$C687`) | `NB_RTC_LOST`: the DS3231's oscillator stopped (OSF), so the time is invalid. This is the nearest thing to a battery flag: the DS3231 has no battery-low bit. `NB_RTC_BUSY` |
| `NB_RTCREG`/`NB_RTCDATA` | the DS3231's temperature (`$11`, `$12`) and aging (`$10`). Writing RTCREG starts an I²C read (BUSY). The emulator's temperature is always 25.00 °C |
| `VIDEO_MEMSIZE` (`$C4BF`) | card memory in 64 KB units: 32 for 2 MB. Card memory past it reads `$FF` and ignores writes ([video-rev2.md](video-rev2.md)) |
| `SOUND_CODECS` (`$C55D`) | revision 2: `$83`, bit 7 an ES5503 (`SOUND_CODEC_DOC`), bits 0–1 raw PCM and MP3 |
| `USB_DEVICES` (`$C5B2`) | bit 0 a keyboard, bit 1 a mouse, bit 2 a gamepad (reserved) |
| `SD_STATUS` (`$C381`) | `SD_PRESENT`, `SD_READY`, `SD_BUSY` |
| SD `IDENTIFY` (`SD_IDENTIFY`, `$EC`) | one sector: the card's sectors, type (SDSC, SDHC, SDXC), write-protect and FAT-found flags, the firmware's version, a name, and units 1–7 with their sizes, flags and file names (`sd_card.h`) |
| `CONSOLE_SWR`, `CONSOLE_PANEL` | the 20 switches; HALT/ENABLE and S INST/S BUS CYCLE. The keyswitch's LOCK isn't readable |

**Time bases.** The CPU clock can't be measured against itself, so the VIA's timers (which count PHI2) don't
measure it. Two independent ones do:

- **The DS3231's second** (its own 32 kHz crystal, about ±2 ppm). `NB_SEC` changes once a second. In the emulator
  the RTC counts emulated cycles, so a measurement there reads exactly 7,159,090 Hz.
- **The video frame:** 800 × 525 pixel clocks at 25.175 MHz, 16.683 ms, 59.94 Hz; 119,437 CPU cycles at
  7,159,090 Hz (`video_card.h`: `LINE_CLOCKS`, `LINES`, `PIXEL_HZ`). The pixel oscillator's tolerance isn't known
  (a typical crystal oscillator is ±50 ppm).

**What can't be detected,** because the hardware defines no way to: a digital joystick (each switch closes to
ground and pull-ups hold the rest at 1, so an empty port reads as a stick with nothing pressed), a paddle (an
unplugged one is expected to read 255 steadily, but that isn't known), the VIA apart from the serial card, a RAM or
ROM module except by its memory, and the keyswitch's position.


## Card discovery ##

One rule, in the library and the ROM core alike: **a card is fitted when its ID byte's high nibble is its module
ID; its revision is the low nibble.**

- **Only the ID byte is read** to find a card. A card whose ID doesn't match is never touched again: its block
  isn't read or written. Nothing is inferred from side effects (a register that reads back, a mirror, a timing).
- **An empty slot can't be mistaken for a card:** `$FF` has the high nibble `$F`, and module F has no card.
- **A known revision** gets that revision's tests. **An unknown one** (a later card) is listed with its revision,
  and its tests are skipped with "revision n: not known to this DIAG"; nothing past the ID byte is touched.
  Revision 1 of the video card is listed and skipped the same way, since BIOS 0.13 and OODOS no longer drive it.
- **No I/O block without an ID byte is read:** not `$C000–$C2FF` (RAM and ROM modules have none) nor `$C780–$C7FF`.
- **The serial card** is the exception: it has no ID yet. Its `$C37F` is read like any other; a high nibble other
  than 6 is reported as "the stand-in 6551 (no ID register)", and nothing else of the block is touched beyond what
  the console already does. From D1 the card's ID is `$61` ([answer 6](#the-owners-answers)), and a real card is found by the rule.
- **Memory modules** are found by the ROM core's memory tests: a RAM module that isn't there reads `$FF` and drops
  writes in the emulator, so all 16 of its blocks fail the same way, which the core reports as "RAM B (blocks
  16–31): not answering" rather than 16 failures. On the backplane a missing module's reads float to what the
  pull-ups give; that isn't measured. Under OODOS the fitted memory is function 72's figure, not a probe.
- **The CPU (review CC1):** only the ROM core tests it. `$EB` is XBA on a 65C816 and a one-byte, one-cycle NOP on
  the W65C02S (WDC's datasheets; the emulator's W65C02S does the same): `LDA #1`, `XBA`, `LDA #2`, `XBA` leaves
  A = 1 on a 65C816 and 2 on a W65C02S. The OS build can't use it: `oodos/tools/rules.py` gives `$EB` the mode
  `bad` (an undocumented NOP), so the rules gate would fail DIAG's and SYSINFO's builds. SYSINFO names the CPU the
  design has, the W65C02S, and the BIOS isn't gated, so the core keeps the test.
- **Reading an ID byte twice (review CC5).** An empty slot may float, and a floating bus tends to return the last
  byte the CPU fetched, the operand's high byte: `LDA $C67F` on an empty Disk II slot would then read `$C6`, which
  looks like the Disk II at revision 6. So each ID byte is read twice by instructions whose last fetched byte
  differs, and a card is accepted only if both reads agree: `LDA base+$7F` (last fetched byte the address's high
  byte), then `LDA base+$7F-$FF,X` with X = `$FF`. For a module with an even ID (ID byte at `$xx7F`) the second
  read's operand is in the page before and crosses into it, and on the W65C02S the extra cycle of a page crossing
  reads the last instruction byte again (WDC's datasheet), so the bus last held the page before's high byte. For an
  odd ID (`$xxFF`) both reads end on the same high byte, but every such byte is `$Cn` and no odd module is `$C`, so
  a floating read fails the nibble test anyway. Only the Disk II (module C) is at risk, and the second read catches
  it. Whether the backplane floats at all, or the pull-ups give `$FF`, isn't measured.

| Routine | Reads |
|---|---|
| `HW_PROBE` | the ID bytes of modules 6–E, each twice as above, nothing else |
| the core's `CPUTEST` (ROM only) | the XBA sequence above |


## SYSINFO ##

*Plan 2: unchanged, as D1 built it ([D1: as built](#d1-as-built-2026-11-13)).*

`SYSINFO.COM` under OODOS. It writes nothing anywhere but its own TPA and the console, so it is always safe to run.
It prints one report, paged at the console's 30 lines (a key for the next page); `SYSINFO /ALL` prints it without
pausing (for `SUBMIT`, redirection and the tests), and `SYSINFO section` prints one of `SYSTEM`, `CARDS`, `VIDEO`,
`SOUND`, `STORAGE`, `INPUT`, `CLOCK`, `PANEL`.

Where each line comes from:

| Line | Source | Touches the hardware? |
|---|---|---|
| CPU and its clock | the W65C02S, the design's CPU (no opcode test: CC1); `HW_CLOCK` against the DS3231 and against the event area's frame counter (function 80) | VIA timer 2, the program's; `NB_SEC` read |
| ROM BIOS | the word after `BACKPLANE BIOS ` in the ROM's copy in RAM block 63, copied into the TPA by function 83's `DM_COPY` (a physical source may be anything) and searched, as OODOS's boot status does | no |
| OODOS, its BIOS, memory | function 72 (`O_VERSION`) | no |
| OOLIB's interface | function 80's `FT_INFO` | no |
| OOUI.BNK | `A:OOUI.BNK`'s record 0: `'OUB'`, the format, the interface version, the blocks ([ooui.md](ooui.md#u3a-ooui-as-a-banked-library)) | no |
| Memory free | functions 53 and 57 (`MC_MAX`, then `MC_FREE` at once) | no |
| Cards | `HW_PROBE` | ID bytes ([answer 1](#the-owners-answers)) |
| Video | function 83's `DM_INFO` (card memory, the program's pages); `VIDEO_MEMSIZE` agrees | ID, MEMSIZE |
| Sound | function 82's `SN_INFO` (revision, codecs, parts free) | no |
| Storage | the drives from the BIOS's DPBs (functions 14 and 31) and their free space (46); the card from `IDENTIFY` | `IDENTIFY` ([answer 1](#the-owners-answers)) |
| Input | `USB_DEVICES`; the event area's controllers and paddles (function 81, layout 2) | DEVICES |
| Clock | function 105 for the time; `NB_RTCCTL`'s LOST; the temperature through RTCREG | RTCCTL, RTCREG ([answer 1](#the-owners-answers)) |
| Panel | `CONSOLE_SWR`, `CONSOLE_PANEL` | reads |

A sample, from `backplane.machine` (the versions, the SD lines and the time are illustrative):

```
A>SYSINFO /ALL
OO-1 SYSTEM INFORMATION                                   2026-11-13  14:02:37

SYSTEM
  CPU        W65C02S at 7.159 MHz: 7,159,090 Hz by the clock chip,
             7,159,1xx Hz by the video frame
  ROM BIOS   0.14            OODOS 0.9, its BIOS 0.9      OOLIB interface 8
  OOUI.BNK   A:, interface 1, 3 blocks
  MEMORY     1,024 KB: 960 KB of RAM and the video card's 64 KB aperture
             for programs: a 47K TPA; banks 1-5, 320 KB (320 KB free);
             banks 9-B, 192 KB
  BOOTED     from the flash, at power-on

CARDS
  ADDR   ID   CARD                            REVISION
  $C300  --   serial: the stand-in 6551 (no ID register)
  $C380  $71  mass storage (SD)               1
  $C400  $81  DMA controller                  1
  $C480  $92  video                           2
  $C500  $A2  sound                           2
  $C580  $B1  USB, game port and speaker      1
  $C600  $C1  Disk II controller              1
  $C680  $D1  north bridge                    1
  $C700  $E1  console (front panel)           1

VIDEO      2,048 KB of card memory (MEMSIZE 32); the program's 1,536 KB
SOUND      two SIDs; the 6510 and its 64 KB; an ES5503 and 128 KB of wave
           RAM; raw PCM and MP3 (CODECS $83). Free: 6510, stream, ES5503
STORAGE    SD card: SDHC, 7,733,248 sectors (3,776 MB), FAT, firmware 1.0
             unit 1 BOOT.BIN     44 sectors   read-only
             unit 2 OODOS.IMG    16,384 sectors
           drives  A: 8M (7,912K free)   B: 8M   M: 128K
INPUT      USB keyboard, USB mouse
           game port: paddles 127 127 255 255, no button down
           joysticks 1 and 2: nothing pressed (a port can't tell an empty
           socket from an idle stick)
CLOCK      2026-11-13 14:02:37, the oscillator kept time; 25.00 C
PANEL      switches $00000, HALT/ENABLE at ENABLE, S INST
```

**Size:** about 4 KB of code and text, plus the library modules it takes (about 1.2 KB).


## DIAG.COM ##

***Replaced by plan 2 (2026-11-13, the owner's redesign).*** DIAG.COM was built in D2 on `feat/diag-d2` and doesn't
merge: its tests become the ROM's ([the card tests, as ROM tests](#plan2-card-tests)), and `DIAG` at `A>` becomes the
launcher ([the DIAG launcher](#plan2-launcher)). Why: under OODOS it couldn't reach OODOS's memory, the console's
card memory or a DMA channel of its own, and half of its 22 KB was OODOS's way round the hardware (the claims, the
event queue, the W1 contract and a layout around W1's window, ^C, CC3's put-backs). In the ROM each test is the
direct one. This section is plan 1's, kept as the record; D2's as built is [below](#d2-as-built).

`DIAG.COM` under OODOS: `DIAG [area ...] [/FULL] [/I] [/WRITE] [/LOOP] [/?]`.

- **Areas:** `NB`, `MEM`, `DMA`, `VIDEO`, `SOUND`, `SD`, `USB`, `SERIAL`, `DISK2`, `PANEL`; none is all of them.
- **`/FULL`:** the march test with all four backgrounds, and the march on card memory too (below); about three
  times as long. It also runs S6 (all of wave RAM rewritten) and, with `/I`, P3 (the CPU halted), which the
  quick run leaves out (review CC6).
- **`/I`:** the interactive tests, which ask the user to do something (press a key, move the mouse, press CONT).
- **`/WRITE`:** the tests that write the SD card. DIAG says what it will write and asks for `Y` first; any other key
  skips them.
- **`/LOOP`:** run again until a key, with a pass count.
- **The return code** (function 108, `P_CODE`) is the count of failures, 0 for none, so a `SUBMIT` file can test it.

Each test prints one line: its name, then `PASS`, `FAIL`, `SKIP` with the reason, or `INFO` for a measurement with
no limit. A failure adds a line saying what was seen and what was expected. The summary ends the run.

**Memory DIAG may test** (the owner's "RAM that OODOS doesn't own"):

- the TPA from DIAG's end to `$BFFC`, its own;
- the blocks functions 53–58 give it, banks 1–5 (blocks 4–23), as many as are free; each is allocated, tested and
  freed (58 at the end, and the warm boot frees them anyway);
- banks 9–B (blocks 36–47), the running program's under interface 8;
- the video card's program pages, `$040000–$1BFFFF` (1.5 MB), through aperture slot 1 (`GX_VMAP`).

It reaches blocks through W1 with OOLIB's `GX_BLOCK` and card memory with `GX_VMAP`, under the W1 contract, so its
code and data stay in `$0300–$3FFF` (DIAG.COM must stay under about 15 KB) and `$8000–$BFFC`, and `GX_W1BACK` puts
block 1 back. It writes no window itself.

**The tests,** by area:

| Test | What it does | Pass | Fail means |
|---|---|---|---|
| **North bridge** | | | |
| NB1 | ID `$Dx`; STATUS bits 4–7 equal the ID's revision | they agree | the bridge (or the data bus) answers wrong |
| NB2 | `DM_FILL` writes a block's number into the first bytes of each block DIAG may map; `GX_BLOCK` maps each in W1 and reads it back | each block shows its own number | a window bit (A14–A19) stuck or two blocks aliased |
| NB3 | reads IRQL and IRQH 1,000 times over 10 ms (plain reads: the poll the dispatcher makes, which acknowledges nothing) | no bit of a module `HW_PROBE` didn't find is ever 0 | a stuck /IRQ line or an empty slot pulling it |
| NB4 | the RTC: BUSY clears within 1 ms; SEC steps by one each second for 3 s; LOST clear | as said | LOST set: the time must be set (a warning, not a failure) |
| NB5 | the CPU clock against the DS3231 (`HW_CLOCK`) | within 0.1% of 7,159,090 Hz | the crystal or the divider |
| NB6 | the CPU clock against the video frame | within 0.1% | the pixel oscillator or the CPU's |
| **Memory** | | | |
| M1 | the TPA, by the CPU: address in address, then the march | no error | a bad cell in blocks 0–2 |
| M2 | the free blocks of banks 1–5: address in address | no error | the block and offset, the bits wrong |
| M3 | the same blocks: the march, background `$00` (`/FULL`: four) | no error | the block and offset, the bits wrong |
| M4 | banks 9–B: M2 and M3 | as M2, M3 | as M2, M3 |
| M5 | the program's card pages through slot 1: address in address (`/FULL`: the march) | no error | the card address, the bits wrong |
| **DMA** (through OODOS's functions: every channel is OODOS's) | | | |
| D1 | ID `$81` | | |
| D2 | channel 3: `DM_FILL` 2 KB of a pattern into a tested block; the CPU reads it | the pattern | the fill path |
| D3 | channel 3: `DM_COPY` 2 KB between two blocks, both directions | the copies agree | the copy path |
| D4 | D2 timed by VIA timer 2 | 2 cycles a byte, give or take the call's own cost | the controller stealing more cycles than it should |
| D5 | channel 0: `DM_LOAD` DIAG.COM's own records into a block; the same records read by `F_READ` (20) into the TPA | they agree | the SD card's DMA path |
| D6 | channel 1: `DM_VWRITE` 4 KB to the program's card pages, `DM_VREAD` it back, and the same bytes read through slot 1 | all three agree | the video port's DMA, or the aperture |
| D7 | channel 2: `SN_LOAD` 4 KB into the card's RAM (6510 claimed); the 6510 sums it and reports the sum | the sums agree | the sound card's DMA path |
| **Video** (revision 2) | | | |
| V1 | ID `$92`; MEMSIZE at least 28 (1.75 MB: the console's and Apple II mode's parts and some of the program's) | | |
| V2 | the registers a program may write ([oolib.md](oolib.md), "The registers a program may write"): BORDER and layer 1's, with layer 1 off, written and read back, then put back, in one vertical blank | each reads back | a register bit |
| V3 | the beam: LINE goes 0–524 and STATUS's vertical blank comes every 119,437 cycles ± 0.1% | | the card's timing |
| V4 | slot 1 against the port: a pattern written through slot 1 and read through the port (`DM_VREAD`), and the reverse | they agree | the aperture's or the port's address decode |
| V5 | past MEMSIZE: the program can't map it, so this is the ROM core's | | |
| V6 (`/I`) | a test card on layer 1 (colour bars, a grid), the user answers Y or N | Y | |
| **Sound** (claims the 6510, the stream and the ES5503; skipped if any is held) | | | |
| S1 | ID `$A1` or `$A2`; `SN_INFO` agrees; CODECS `$83` on revision 2 | | |
| S2 | the 6510 test program (below) runs and answers its first byte within 0.5 s | | the 6510, its reset vector, the mailbox to the host |
| S3 | the mailbox both ways: 256 bytes sent with `SN_SEND`, each echoed | all 256 | a mailbox bit |
| S4 | the card's RAM, tested by the 6510 itself, apart from its code and the vectors | no error | the card address and bits, sent back |
| S5 | the SIDs, by the 6510: voice 3's oscillator and envelope read back from OSC3 and ENV3 on SID A and SID B | they change as set | a SID or its address decode |
| S6 (`/FULL`) | revision 2: wave RAM, 128 KB, through DOCDATA with AUTOINC (address in address): every byte of it written, with the ES5503 claimed and its oscillators halted | no error | the wave RAM address and bits |
| S7 | revision 2: a 4 KB raw PCM stream at 8,000 Hz (silence): STATE goes buffering, playing, ended; POS counts 8,000 frames a second ± 0.5% against the DS3231 | | the card's sample clock or its FIFO |
| S8 (`/I`) | a tone on each SID and on the ES5503; Y or N | Y | |
| **Storage** | | | |
| SD1 | ID `$71`; STATUS PRESENT and READY | | |
| SD2 | `IDENTIFY`: the type is 1–3, the size not 0, the FAT flag set (why it can't collide with OODOS: [answer 1](#the-owners-answers)) | | a card the firmware can't read |
| SD3 | read speed: `DM_LOAD` of 64 KB of DIAG.COM (repeated) timed | `INFO`, KB a second | |
| SD4 (`/WRITE`) | a 64 KB file `DIAG$$$.TMP` on A: written with a pattern by `F_WRITE` (21), read back by `F_READ` (20), erased by `F_DELETE` (19) | the same bytes | the write path |
| **USB** | | | |
| U1 | ID `$B1`; DEVICES | `INFO` | |
| U2 | the game port: the paddles settle (each reads the same value twice, 20 ms apart, with the controllers in the event area) | `INFO` with the values | |
| U3 (`/I`) | keys: "press A, then Esc", seen in the event queue | | |
| U4 (`/I`) | the mouse moved and each button pressed; each paddle button | | |
| U5 (`/I`) | the speaker: `SN_BEEP` at 1 kHz; Y or N | | |
| **Serial and VIA** | | | |
| SR1 | the card's ID; with the stand-in, `SKIP` (no ID) | | |
| SR2 | the VIA's timer 2 counts: two reads 1,000 cycles apart differ by 1,000 ± 20 | | the VIA or PHI2 to it |
| SR3 (`/I`) | each joystick direction and fire, on both ports | | a stick or its line |
| SR4 | the TL16C552A, once it exists: COM2 in its internal loopback (MCR bit 4), 256 bytes, then put back. COM1 is the console and is left alone | | |
| **Disk II** | | | |
| DK1 | ID `$C1`. Nothing else: any access to its switches moves the drive's stepper or motor | | |
| **Front panel** | | | |
| P1 | ID `$E1`; the switches and HALT/ENABLE shown | `INFO` | |
| P2 | the lights: a walking bit on DISPLAY with CTRL's bit 3, 0.2 s a step, then CTRL put back as it was | `INFO` (`/I`: Y or N) | |
| P3 (`/FULL /I`) | the breakpoint: BPADDR a byte in DIAG's TPA (its physical address by function 73), BPCTRL a read that halts; "the CPU will halt: press CONT"; then STATUS's HIT, LADDR and LDATA checked, BPCTRL 0, HIT cleared | they match | the comparator or the latch |

**P2 and P3 can't be left half done (review CC3).** A ^C, or a warm boot, in the middle of either could leave CTRL's
bit 3 set (the lights stuck on DISPLAY) or a breakpoint armed on an address in DIAG's TPA, which would halt the next
program that reads it. So DIAG sets bit 3 of the console mode first (function 109, `C_MODE`: ^C then doesn't end
the program), saves CTRL and BPCTRL, runs the test, puts CTRL and BPCTRL back (BPCTRL 0, STATUS's HIT cleared),
and only then puts the console mode back. Break or a reset in between still can't be caught: Break enters the
monitor, which leaves BPCTRL alone, and a /RESET keeps BPADDR and BPCTRL ([chapter 12](../docs/oo1-technical-reference/12-front-panel.md#reset)),
so the User's Guide's entry for DIAG says to turn the breakpoint off (`C70B: 00` at the monitor) after a P3 that
was broken into.

A sample, quick, with a fault injected at physical `$A1234` bit 3:

```
A>DIAG
OO-1 DIAGNOSTICS 0.1   quick, nothing destroyed   (DIAG /? for the options)

NORTH BRIDGE $D1
  NB1  ID and STATUS agree, revision 1 ......................... PASS
  NB2  W1 shows each of 32 blocks as itself .................... PASS
  NB3  no interrupt line stuck ................................. PASS
  NB4  the clock chip ticks; the time is valid ................. PASS
  NB5  CPU 7,159,090 Hz by the clock chip (+0.000%) ............ PASS
  NB6  CPU 7,159,1xx Hz by the video frame (+0.00x%) ........... PASS
MEMORY
  M1   TPA $5800-$BFFC, 26,620 bytes ........................... PASS
  M2   banks 1-5, 20 blocks, 320 KB: address in address ........ PASS
  M3   banks 1-5: march, background $00 ....................... PASS
  M4   banks 9-B, 12 blocks, 192 KB ............................ FAIL
       block 40 +$1234 ($A1234): read $08, expected $00: bit 3
  M5   card memory $040000-$1BFFFF, 1,536 KB ................... PASS
DMA $81
  ...
SUMMARY  44 tests: 39 passed, 1 failed, 4 skipped (interactive: DIAG /I)
```

**The 6510 test program** (`card6510.s`, about 400 bytes, the card's own code: the 6510's opcodes, assembled by ca65
for the 6502) is shared by DIAG.COM and the ROM core. Loaded at the card's `$0200` with its reset vector, it sends
`$A5` when it starts, then answers commands from the mailbox: echo the next byte; sum a range; test a range of its
RAM (address in address, then the march) and send back the first failing address and bits; set and read back a
SID's voice 3. It never writes `$0000–$01FF` while it runs from them, so its RAM test covers `$0400–$FFF9` and
then its own pages last, from a copy it moves itself to.

**Size:** about 9 KB of code and text, plus the library modules (about 1.6 KB) and OOLIB's `GX_BLOCK` and
`GX_VMAP` with what they pull in.


## The ROM core ##

*Plan 2 keeps this section's entry (the reset's prompt, CONT and `Y`, `C812R`, the signature), its stages 0–6, its
lights and its Break handler, and replaces the rest: where it runs (three blocks, not one), stages 7–12, the switches
(16 is new), the output's sample and how it ends ([plan 2](#plan-2)).*

A dead-test-style core in the flash, which assumes no working RAM until it has tested some.

**Where it runs.** In its own flash block, ROM A block 23, through W2 (`$8000–$BFFF`), so it runs where it is,
copied nowhere ([ROM space](#rom-space)). W3 stays on the flash's block 31, the BIOS, read-only, and VEC is put on block 23 for the core's own vectors. W0 is the
block the core chose for its zero page and stack (stage 2), W1 the block under test.

**How it is entered:**

- **At reset, with switch 19 up, after a confirmation** (decided, [answer 2](#the-owners-answers), the owner's
  option A; it replaces round 1's HALT and LOCK combination: **neither HALT/ENABLE nor the keyswitch is part of the
  entry now**). Switch 19 alone can't be the entry, since it is also A19 of LOAD ADRS and an address of `$80000` or
  more may be left on the switches; so it only asks. The BIOS's RESET checks first thing, before it copies itself
  into RAM: `CONSOLE_ID` is `$E1` (read twice, as [Card discovery](#card-discovery) says) and `CONSOLE_SWR+2`
  bit 3 (switch 19) is set; then the signature (below); then it jumps to the core's **prompt**, in block 23. With
  no console card, switch 19 clear or no signature it boots as now, with no delay.
- **The prompt** shows `DIAGNOSTICS: PRESS CONT OR Y WITHIN 5 S` on the serial line and the screen, and `$A5` on the
  lights. CONT on the panel, or `Y` on the serial line or the USB keyboard, within 5 s enters the core (stage 0 on);
  any other key, or nothing for 5 s, jumps back to the BIOS's `BOOT` and boots as usual. **Nothing is written to
  RAM before the confirmation,** nor to any card but what the prompt shows itself by: the 6551's settings and the
  characters it sends, the video card's font, palette, text and layer 0 (card memory, which the boot's VINIT
  writes again anyway) and the console card's DISPLAY and CTRL's bit 3 (put back to 0 on a normal boot).
- **What the prompt may use: nothing that needs RAM.** RAM isn't trusted yet, and the point of the switch is to
  reach the core when bank 0 or F is bad. So the prompt is written in the no-RAM style of stages 0–2 (macros, no JSR,
  no zero page, no stack), reusing stage 0's screen and serial macros; it doesn't need stages 1–2 first, and
  doing them first would put 0.1 s of flash sums and the zero page search before every such boot, for nothing when
  the answer is no. The 5 s are counted from `NB_SEC`: the last value in Y, the seconds seen in X, A for each
  poll; six changes, so between 5 and 6 s. The keys are polled in the same loop: the 6551's status (a byte
  received), `USB_KEY` (bit 7, the strobe; `Y` is `$D9`, and a lower-case `y` `$F9`, taken too) and `CONSOLE_KEYS`.
- **Why CONT and not START (found in writing it in; CONT decided by the owner, [answer 2](#the-owners-answers)).**
  START can't be read: it isn't a bit in KEYS (those are LOAD ADRS, EXAM, DEP and CONT), it *is* the hard reset,
  through the north bridge ([chapter 12](../docs/oo1-technical-reference/12-front-panel.md#reset)). Pressed at the
  prompt it resets the machine: KEYS, CTRL and the rest are cleared, it leaves Apple II mode if it were on, and the
  boot starts again, which with switch 19 still up is the prompt again, without end. The BIOS can't tell that
  reset from any other (`NB_STATUS` bit 1 only says whether it was a power-on) unless the prompt first wrote a
  marker into a register a reset keeps (the console card's PADDR or BPADDR), which breaks "nothing written before
  the confirmation" and overwrites the user's panel address or breakpoint. **CONT** is in KEYS: a press while the
  CPU runs is latched there (the emulator's `ConsoleCard::apply` sets KEYS for any key, halted or not) and does
  nothing else while running (CONT acts only while halted). KEYS is cleared by every /RESET, so a CONT pressed
  before this reset can't count; the prompt also reads KEYS once at its start, to drop a press during the BIOS's
  first instructions, then watches for bit 3. LOAD ADRS would be latched too, but it copies the switches into
  PADDR as a side effect; EXAM and DEP do nothing while running, but CONT is the panel's "go on". The keyswitch at
  LOCK makes the panel's keys ignored, so then only `Y` enters.
- **`C812R` from the monitor** (decided, [answer 3](#the-owners-answers)): it prints `ALL OF MEMORY WILL BE
  OVERWRITTEN. Y TO GO ON` and waits for `Y`, since a user typing at the monitor may have work in RAM; any other key
  is the monitor again.
- **Either way** the BIOS first checks the core's signature (`OOHWDIAG` at block 23's `$8000`, read through W2), so
  a flash whose block 23 is erased (an older image, a /BOOTSEL boot with the flash erased) prints `NO DIAGNOSTICS IN
  THE FLASH` and goes on booting, or back to the monitor.

**Switches read at its start** (all on the front panel, so a machine with no console card runs the default):

| Switch | Up |
|---|---|
| 19 | at reset: the prompt, then the core if CONT or `Y` confirms it (above) |
| 18 | full: the march with all four backgrounds, and the march on the video card's memory |
| 17 | loop: run again and again, counting passes, until a key |

**Its stages, in order.** Each needs only what the ones before it proved.

| Stage | What | Needs | No RAM? |
|---|---|---|---|
| 0 | SEI, CLD; W3 on flash block 31 (`NB_ROM` \| `NB_RO` \| 31) and VEC on block 23, the core's own vectors (below); the stack pointer `$FF`; lights on (CTRL bit 3), DISPLAY `$01`; the 6551 at 19,200 baud; the screen, if the video card is `$92`: the font from flash block 30 through W1 and the palette, by unrolled loops into PDATA, the text area cleared through the port, layer 0 on | the CPU, the north bridge, the flash | yes |
| 1 | the flash's sums: blocks 23–31, each 16 KB through W1, a 16-bit sum against the table at block 23's end | stage 0 | yes: the sum is in A and Y, X the byte's index, and the block's count in the stack pointer (`TSX`, `INX`, `TXS`: S is a register, and nothing is pushed until stage 3); the loop unrolled a page at a time |
| 2 | zero page and stack: `$4000–$41FF` of block 0 through W1, march with the backgrounds `$00` and `$55` by absolute indexed loads; the first block that passes becomes W0, its `$0000–$01FF` the core's zero page and stack. Block 0 first, then 1, 2 and on, the block's number kept in S as in stage 1 | stage 0 | yes |
| 3 | from here on the core uses JSR and zero page (S set to `$FF` first). The data lines: walking 1s and 0s at the first byte of each RAM module's first block (0, 16, 32 or 36, 48) | stage 2 | |
| 4 | the address lines and blocks: each RAM block's number and its complement written at `+0` and at each power of two to `+$2000`, in every block first, then all read back | stage 3 | |
| 5 | address in address over every RAM block: all written, then all read | stage 4 | |
| 6 | the march, background `$00` (switch 18: four) | stage 5 | |
| 7 | the DMA controller: ID; channel 3's ADDR, COUNT, MODE and DEST written and read back; a HOLD fill of each RAM block read by the CPU; a copy between two blocks up and down (DEC); STATUS's terminal count; ABORT stops a copy part way, COUNT reads what was left | stage 6 | |
| 8 | the video card (`$92`): its memory through the aperture (block 32 in W1, SLOT0 stepped through each 16 KB card bank to MEMSIZE), address in address (switch 18: the march); 64 KB checked through the port against the aperture; a card address past MEMSIZE reads `$FF`; then the screen is drawn again | stage 6 | |
| 9 | the sound card (`$Ax`): the 6510 stopped (CTRL's RUN off); its RAM through RAMADDR and RAMDATA, address in address and the march; on revision 2 the wave RAM through DOCDATA; then the 6510 test program loaded and run: start byte, mailbox echo, the SIDs | stage 6 | |
| 10 | the other cards by their IDs: SD (STATUS; IDENTIFY; LBA 0 of unit 0 read by the CPU and again by DMA channel 0, the two agree), USB (DEVICES), Disk II (ID only), the panel; the north bridge: WAIT0–3 and VEC written and read back, then put back; a write through a read-only window leaves RAM as it was; a write through a flash window changes nothing | stage 6 | |
| 11 | the clock: cycles between two changes of `NB_SEC`, interrupts off; the video frame the same way. How polling SEC meets the bridge's own I²C reads of the DS3231 on real hardware (a read during the second's refresh) isn't known; the emulator latches whole | stage 3 | |
| 12 | the summary; the loop, or the end | | |

**It never writes `NB_MODE`** (APPLE can't be undone but by a hard reset) and never touches the Disk II's switches.

**Its output.** The screen is a fixed form, a line a stage, filled in as each ends; the serial line gets the same
lines as they are written. A sample, with a stuck bit in block 41:

```
OO-1 ROM DIAGNOSTICS 0.1          BIOS 0.14         PASS 1         QUICK
 1  FLASH BLOCKS 23-31 ..................................... PASS
 2  ZERO PAGE AND STACK: BLOCK 0 ........................... PASS
 3  DATA LINES, RAM A B C D ................................ PASS
 4  ADDRESS LINES, 60 BLOCKS ............................... PASS
 5  ADDRESS IN ADDRESS, 960 KB ............................. PASS
 6  MARCH, 960 KB, BACKGROUND 00 ........................... FAIL
      BLOCK 41 +0123 (A4123): READ 08, WROTE 00, BIT 3
 7  DMA ................................................... PASS
 8  VIDEO CARD MEMORY, 2048 KB ............................. PASS
 9  SOUND CARD RAM 64 KB, WAVE RAM 128 KB, 6510, SIDS ...... PASS
10  SD, USB, DISK II, PANEL, NORTH BRIDGE .................. PASS
11  CLOCK 7159090 HZ BY THE CLOCK CHIP ..................... PASS
RESULT: 1 FAILED                                        LIGHTS C1
A KEY OR LOAD ADRS: BOOT AS USUAL
```

**The lights** (DISPLAY, with CTRL's bit 3 set, for a machine with no screen and no terminal):

| Lights | Meaning |
|---|---|
| `$A5` | the prompt, waiting for CONT or `Y` (at most 6 s) |
| `$01`–`$0B` | stage n running |
| `$FF`, steady | the end: everything passed |
| `$C0` + n, steady | the end: n stages failed (1–63) |
| `$80` + n, then the block number, then the bits wrong, each 0.5 s, round again | stage n failed in a way the core can't go on from: stage 1's block 23 (its own code), or stage 2 with no block left for a zero page and stack. It stays so until a reset |

**How it ends.** With switch 17 down, it waits for a key on the serial line or the USB keyboard, or LOAD ADRS on the
panel (KEYS bit 0: the only key that does anything while the CPU runs; ignored while the keyswitch is at LOCK). It
then puts back what it changed to the values /RESET leaves (SLOT0–3 0–3, PSTEP 1, PCTL 0; DMA CONTROL `$70`; the
sound card's CTRL, RUN off; WAIT0–3 0; DISPLAY 0 and CTRL 0; VEC on flash block 31, `$DF`, so an NMI before the
boot's copy takes the BIOS's own vector, and the boot then points VEC at block 63 as it always does) and jumps to
the BIOS's boot just past the switch check, which copies the BIOS into RAM and boots as usual. With switch 19
still up the next reset shows the prompt again; switch 17 loops without a reset or a confirmation. After a fatal failure it never ends.

Break (NMI) while the core runs would take the flash's own vector, the BIOS's BREAK, which needs RAM. So the core
points VEC at block 23, whose last six bytes are its own vectors, so an NMI or IRQ goes to the core's handler in
W2: it prints `BREAK` and ends as a key would. It doesn't RTI: the stack may be the one thing that is bad.

**Stages 0–2's size.** About 1.3 KB, a little over the first estimate's 1.2 KB: the sum's loop is 64 unrolled
`ADC abs,X` with the carry into Y (6 bytes each, about 400 bytes), its block counter in S and the compare against
the table about 40; stage 2's march, two backgrounds over two pages by absolute indexed loads, about 330; the screen
(the font's 16 pages unrolled, the palette, the clear, the registers) about 200; the messages and their delays
(no JSR: each a macro) about 250; the lights about 60. It fits block 23 with room to spare.

**Size:** about 6.9 KB of the 16 KB block, the prompt's 0.3 KB included (below).


## The memory tests ##

*Plan 2 keeps the algorithms; its [ranges and times](#plan2-memory) replace the table here (DIAG.COM's rows go).*

**Algorithms:**

- **Walking 1s and 0s** (data lines): at one address a module, write `$01`, `$02`, ... `$80`, then `$FE` ... `$7F`,
  reading each back. 16 writes and reads: no time.
- **The address lines and blocks** (stage 4): a tag, the block's number, at `+0` and at each power of two to
  `+$2000` (A0–A13) in all 60 blocks, then all read back. It finds a stuck or shorted address line, a window bit,
  and two blocks that answer as one. No time.
- **Address in address:** each byte written with a value from its address (the low byte, XOR the page, XOR the
  block's number), in every block first, then every block read back, so a write to one block that lands in another
  is seen. About 33 cycles a byte for the two passes.
- **The march** (March C−): ⇕(w0); ⇑(r0, w1); ⇑(r1, w0); ⇓(r0, w1); ⇓(r1, w0); ⇕(r0), with 0 the background and 1
  its complement. It finds stuck bits, transition faults and coupling between cells at different addresses within a
  block. One byte is eight bits at one address, so coupling between bits of a byte needs more backgrounds: `$00`,
  `$55`, `$33` and `$0F` (and their complements) separate every pair of bits. About 109 cycles a byte a
  background: (r, w) elements about 21 each, the w0 and r0 about 11 each.
- **DMA-driven:** the fill (HOLD) writes 2 cycles a byte; the CPU's check reads about 12. A copy is 2 cycles a byte
  each way. These test the controller's path as much as the memory.

**Time on the real 7.16 MHz** (7,159,090 cycles a second; the cycles are counted from the instructions, so these are
estimates; the cards' wait states on the real hardware aren't known, except the video card's aperture, which has
none):

| Range | Size | Address in address | March, 1 background | 4 backgrounds |
|---|---|---|---|---|
| a zero page and stack candidate (no RAM) | 512 B | | 8 ms ($00, $55) | |
| all system RAM, banks 0–7, 9–F (with video revision 2) | 960 KB | 4.5 s | 15.0 s | 60 s |
| the same with revision 1 (bank 8 is RAM C's) | 1,024 KB | 4.8 s | 16.0 s | 64 s |
| DIAG.COM: banks 1–5 and 9–B | 512 KB | 2.4 s | 8.0 s | 32 s |
| video card memory, all (the ROM core) | 2,048 KB | 9.7 s | 32 s | |
| video card memory, the program's (DIAG.COM) | 1,536 KB | 7.2 s | 24 s | |
| the sound card's RAM, through RAMDATA (the ROM core) | 64 KB | 0.3 s | 1 s | |
| wave RAM, through DOCDATA | 128 KB | 0.6 s | | |
| the DMA fill and check of every RAM block (stage 7) | 960 KB | 1.9 s | | |

So the ROM core's quick run is about **35 s** and its full run about **2 minutes**; DIAG.COM's quick run about
**20 s**, a little more for OODOS's interrupts (the tick is about 1.4% of the CPU).

**Banks and windows.** The core tests the 60 RAM blocks (64 with video revision 1) through W1, one at a time; W0's
block is tested through W1 too, all but its first 512 bytes, and those last, once the core has moved its zero page
and stack to another block that passed. Bank 8 (blocks 32–35) is the video card's aperture with revision 2: it is
tested as card memory (stage 8), not as RAM. Under OODOS only the blocks above are reached, and only by `GX_BLOCK`.

**Card memory through the aperture.** The ROM core points W1 at block 32 (slot 0's window) and steps SLOT0 over each
16 KB card bank up to MEMSIZE × 4; DIAG.COM uses slot 1 through `GX_VMAP`, its own pages only. The port is checked
against the aperture (both paths must agree) rather than tested separately.

**Faults reported** as the block (or card bank), the offset, the physical address, what was read and what was
expected, and the bits that differ. Stage 4's failures name a line ("A13 stuck", "blocks 17 and 21 answer as one");
a whole module failing alike is named as the module.


## The library: oohw.lib ##

*Plan 2: the OODOS build stays for SYSINFO; the ROM build is included as source by each of the three blocks with its
exports off ([the flash](#plan2-rom-space)). DIAG.COM's column below is the record.*

**Where:** `backplane/oohw/` (decided, [answer 9](#the-owners-answers)), its interface `oohw.inc`, since
the ROM core and OODOS programs share it and it is about the hardware, not OODOS. Built twice from the same source:

- **for OODOS:** `make -C oodos` builds `build/oohw.lib` (ar65, each module taken only if used, as `oolib.lib`), with
  `-D OOHW_OS`; its zero page is its own `ZEROPAGE` segment in `$00–$EF`, and it keeps the 65C816 rules (the rules
  gate checks DIAG.COM's and SYSINFO.COM's `CODE`, which includes it);
- **for the ROM:** the BIOS's Makefile assembles the modules the core uses with `-D OOHW_ROM` into block 23's
  segment; their zero page is the core's, at `$00` of the block it put in W0.

The output is the one difference that matters: every routine that prints calls `HW_PUTC` (A a character), which
each side provides: DIAG and SYSINFO with function 2 (`C_WRITE`), the core with its own (the 6551 and the screen
through the port). The ROM's no-RAM stages 0–2 can't share anything: they are macros in the core, with no JSR and
no zero page.

**The routines** (A, X, Y not kept unless said; carry set for an error):

| Routine | What | Returns | ROM core | Size |
|---|---|---|---|---|
| `HW_PROBE` | reads the ID bytes of modules 6–E, each twice (CC5) | `HW_FOUND` (2 bytes, bit n module n), `HW_REV` (16 bytes, the revision or `$FF`) | shared | 80 B |
| `HW_NAME` | A a module ID: its name for the report | A/X the name (a string ended by 0) | shared | 30 B + 180 B of names |
| `HW_KNOWN` | A a module, X a revision: whether this build has tests for it | carry clear if so | shared | 40 B |
| `HW_CLOCK` | PHI2 cycles in one DS3231 second, by VIA timer 2 (one-shot, as OODOS leaves the ACR) and its wraps, started and stopped at changes of `NB_SEC` (how the polling meets the bridge's own I²C reads on real hardware is unknown); with `HW_FRAMES` set, over 60 frames of the event area's counter instead | `HW_HZ`, 4 bytes | shared (the core runs it with interrupts off) | 150 B |
| `HW_AIA` | address in address: `HW_PTR` a CPU address, `HW_LEN` the bytes, `HW_KEY` the block's number; a write pass and a check pass, called separately so all blocks are written before any is checked | carry set at the first error, `HW_BAD` its address, `HW_GOT`, `HW_WANT` | shared | 140 B |
| `HW_MARCH` | the march over `HW_PTR`, `HW_LEN` with `HW_BG` the background | as `HW_AIA` | shared | 320 B |
| `HW_WALK` | walking 1s and 0s at `HW_PTR` | as `HW_AIA` | shared | 50 B |
| `HW_PORTAIA` | address in address through a card's data port: `HW_PORT` the port's data register's address, which steps itself (the video card's PDATA, the sound card's RAMDATA or DOCDATA) | as `HW_AIA` | shared | 120 B |
| `HW_HEX2`, `HW_HEX4`, `HW_HEX6` | a byte, a word, a 20- or 24-bit address in hex | printed | shared | 50 B |
| `HW_DEC` | a 32-bit number in decimal, with commas or not | printed | shared | 120 B |
| `HW_LINE` | a test's line: A/X the name, then dots to column 62, then the result (`PASS`, `FAIL`, `SKIP`, `INFO`) | printed; the counts kept in `HW_TALLY` | shared | 90 B |
| `HW_FAULT` | the failure's line from `HW_BAD`, `HW_GOT`, `HW_WANT` and the block or bank | printed | shared | 110 B |
| `HW_SUMMARY` | the counts | printed; A the failures | shared | 70 B |
| `HW_RTC` | the RTC's status and temperature (RTCCTL, RTCREG `$11`–`$12`, waiting on BUSY) | `HW_RTCST` 3 bytes | shared | 70 B |
| `HW_SD_ID` | IDENTIFY into a 512-byte buffer by the CPU through DATA (no DMA), waiting on BUSY with a timeout | carry set on an error, A the card's ERROR | shared ([answer 1](#the-owners-answers)) | 110 B |

About 1.9 KB in all; SYSINFO takes about 1.2 KB of it, DIAG about 1.6 KB, the core about 1.3 KB (not `HW_NAME`'s
long names: it has its own short ones, nor `HW_SD_ID`'s OS timeout).

**What the core can't share:** stages 0–2 (no RAM: macros), the screen without RAM, the window and slot mapping
(the core writes `NB_WIN1` and `SLOT0` itself; DIAG.COM goes through `GX_BLOCK` and `GX_VMAP`), and everything that
calls the BDOS or OOLIB. **What DIAG and SYSINFO can't share with the core:** the destructive parts, which are the
core's alone.


## ROM space ##

*Replaced by plan 2's [flash: blocks 21–23](#plan2-rom-space); the measurement of the flash stands.*

Measured from the current `bios.rom` (BIOS 0.13, main 8877ba7, `ld65 -m`):

| Flash block | Physical | Holds | Free |
|---|---|---|---|
| 0–23 | `$00000–$5FFFF` | nothing: erased | 384 KB |
| 24–28 | `$60000–$73FFF` | the boot splash's wave RAM image, 80 KB | none |
| 29 | `$74000–$77FFF` | the video demo, 9,323 bytes (`$4000–$646A`) | 7,061 bytes |
| 30 | `$78000–$7BFFF` | the font, 4 KB | 12,288 bytes |
| 31 | `$7C000–$7FFFF` | the BIOS: code `$C800–$E229` (6,698 bytes), the vectors; `$C000–$C7FF` of it is under the I/O hole | 7,632 bytes (`$E22A–$FFF9`; review CC2) |

**The core's cost:** about 6.9 KB (the reset's prompt about 0.3 KB, stages 0–2 as macros about 1.3 KB, the screen and its form 1 KB, stages 3–11
about 2.5 KB, the library's modules about 1.3 KB, the 6510 program 0.4 KB), and 64 bytes of sums.

**Options:**

1. **Recommended: its own block, 23,** run through W2. The core has 16 KB, all its own; block 31 grows by about 140
   bytes (the switch check, the signature check, `C812R`'s prompt); block 31's free space stays for the BIOS. It also
   lets the core put VEC on its own block, for its NMI. It is still one image: `bios.cfg` splits its `PAD` area
   (blocks 0–23) into `PAD` (0–22) and `DIAG` (23, at `$8000`), so `bios.rom` is the same 512 KB and nothing else
   in it moves.
2. **In block 31's free 7,632 bytes,** run through W3. It fits, with about 1.1 KB left for the BIOS, and needs no
   block of its own; but the BIOS's room to grow is nearly gone, and the core's no-RAM stages would run in the block
   whose RAM copy C812R would be overwriting (so `C812R` must still go back to the flash first).
3. **In block 30's free 12 KB** after the font, through W2 (`$9000–$BFFF`). It fits, but the font and the core share
   a block, which makes both harder to change.

**The sums** are a 16-bit sum of each block, 23 to 31 (block 23's own without its last page, which holds the table),
written into the table by a small build step after ld65 (`backplane/bios/romsum.py`, run by the BIOS's Makefile),
so a build never ships a wrong table.


## The interfaces it touches ##

*Replaced by plan 2: the BIOS's part grows by `$C815` and `MONBOOT` ([D3a](#d3a-the-core-its-entries-the-memory-tests)),
and OODOS's by function 75 with A = 2 and the `DIAG` built-in if the owner chooses it ([the launcher](#plan2-launcher)).
Apple II mode is still unchanged ([the books](#plan2-books)). Kept as plan 1's record.*

### The BIOS ###

BIOS 0.14. Exactly what changes:

- **`bios.s`:**
  - **RESET** gains the switch check first (about 20 bytes: `CONSOLE_ID` = `$E1` read twice, switch 19), then the
    signature check (about 30 bytes: block 23 in W2, `OOHWDIAG` compared with immediates, W2 back on block 2 if it's
    missing), then a jump to the core's prompt in block 23: about 55 bytes in all, against round 1's 46. Its own
    label after the check, `BOOT`, is where the prompt (on no) and the core (at its end) come back to. No RAM is
    used. The prompt itself (about 300 bytes) is in block 23, not block 31.
  - **The jump table:** the first spare, `$C812`, becomes `JMP DIAG` (C812R's own `Y` prompt, about 80 bytes with
    a key routine, then the signature check and the core at stage 0: it doesn't go through the reset's 5 s prompt,
    and switch 19 plays no part in it); `$C815` stays a spare. **The other entries don't move:** the
    table is eight 3-byte jumps at `$C800–$C817`, and only the spare's target changes.
  - **The greeting:** `BACKPLANE BIOS 0.14`. OODOS's boot status and SYSINFO find the version from it.
- **`bios.cfg`:** `PAD` becomes `$0000–$5BFFF`; a new `DIAG` area, start `$8000`, size `$4000`, and its segment.
- **The Makefile:** the core's objects and the library's ROM build; `romsum.py` after ld65.
- **New:** `backplane/bios/diag.s` (the core), `backplane/oohw/` (the library), `backplane/oohw/card6510.s`.
- **Nothing else moves** in block 31 but the code after RESET, by the check's bytes. Nothing outside the BIOS uses
  an address inside it except through the jump table or `bios.rom.lbl`'s names. The Technical Reference's listings
  of `bios.s` (chapter 3's `bios.s:87-116` and `:51-61`) shift and are updated with it; chapter 2's "0–23 erased"
  becomes "0–22 erased, 23 the diagnostics".
- **QUIET, BREAK, ECHO, the SD boot and the splash are unchanged.**

### OODOS and OOLIB ###

**No change** to OODOS or OOLIB is needed for D1–D3, with answer 1 as decided: the
tools use only existing functions (72, 73, 80–83, 53–58, 105, the file functions), OOLIB's `GX_BLOCK`, `GX_VMAP`
and `GX_W1BACK`, and read the hardware where AGENTS.md asks for the owner's word first ("for what OOLIB doesn't
cover yet, ask before a program touches that hardware"), which [answer 1](#the-owners-answers) gives for these
tools, with the full list. The two things that would have needed OODOS to change, a new information function and
leaving OODOS for the ROM core in one step ([answer 7](#the-owners-answers)), were not chosen.

The tools follow AGENTS.md otherwise: `OODOS_HEADER`, `JMP WBOOT`, oodos.inc's names, the 65C816 rules gate, no
window written but through OOLIB, no interrupt, vector or DMA channel taken (every DMA test goes through 82 and
83), VIA timer 2 only (never ACR, IER, IFR or timer 1), the sound card only as claimed.

### Apple II mode ###

**Unchanged,** as the Technical Reference's [chapter 13](../docs/oo1-technical-reference/13-apple-ii-mode.md) says:

- `C806R` and `apple.s` don't change.
- The switch check runs only on the flash's reset path in native mode. In Apple II mode Ctrl-Reset takes the IIe
  ROM's reset vector through the Apple map, never the flash's, so it can't reach the check; a hard reset leaves
  the mode first, as now.
- Neither tool nor the core ever writes `NB_MODE`. The core overwrites the card memory Apple II mode uses
  (`$1C0000–$1FFFFF`, the character ROM at `$1E0000`), which `C806R` writes afresh each time it enters the mode, and
  system RAM, which nothing may count on after a hard reset in any case.
- `test_apple`, `test_iie` and `test_oodos_apple` run unchanged as D3's gate.


## The plan ##

Three sub-phases, each a branch with its own gate (`tools/check.sh changed main`, the quick tier, `make -C docs
check`), merged in order. Each reads its part of the books again at its start and says here what changed.

### D1: the library and SYSINFO ###

- **What:** `backplane/oohw/` (the OS build: `oohw.lib`, `oohw.inc`), `oodos/utils/sysinfo.s`, the User's Guide's
  entry for SYSINFO (`make -C docs check` holds every utility to one), and the card's `SYSINFO.COM` (`make_card.sh`).
- **Size:** about 900 lines of ca65 (the library about 500, SYSINFO about 400); SYSINFO.COM about 5 KB.
- **Tests:** a new suite, `test_oodos_sysinfo`, on the `CcpRig` with a fixed RTC time (`MachineOptions::rtc_time`):
  - golden output of `SYSINFO /ALL` on `backplane.machine` (`emulator/test/host/golden/sysinfo.txt`, as `ooui.txt` is);
  - a missing card, by the machine's text with its line left out (the sound card, the USB card, the console), and
    a different revision (the sound card's `set_revision(1)`, the video card's `rev=1`): each changes only its
    lines, and the golden files say how;
  - the clock reads exactly 7,159,090 Hz by the DS3231 and within 0.01% by the frame;
  - **the must-fail cases:** a card left out must lose its line and a revision changed must change its line, each
    against the golden file, so a probe that reads the wrong byte fails; and, once while D1 is built, a wrong
    constant in `HW_CLOCK` (a frame of 119,000 cycles, say), which the clock's check must catch. The emulator can't
    run its DS3231 off true, so a slow clock chip can't be shown failing; that stays a hardware check.
- **Test map:** `backplane/oohw/  bios oodos docs` (both builds use it); `oodos/utils/sysinfo.s` is under
  `oodos/`; the suite's label `oodos sd video sound usb rtc console` in `EMU_TEST_LABELS`.

#### D1: as built (2026-11-13) ####

**Built** on `feat/diag-d1`, for the owner's review. What differs from the plan above, and why:

- **The ROM BIOS's version comes from a new function, 77 `O_ROMVER` (D1-1 and EE2, decided below).** The plan had
  SYSINFO copy the greeting out of RAM block 63 by function 83's `DM_COPY`, "a physical source may be anything". The
  Programmer's Guide says a source may be anything *a segment may reach*, and OODOS's code (`oodos/sys/video.s`,
  SRCOK) refuses OODOS's own blocks as a source as well as a destination; block 63 is one. So OODOS gives it: the
  cold boot, which already finds the version for its boot status, keeps it as two numbers (`ROMVER`, `cold.s`), and
  function 77 returns them, the major in A and the minor in X, `$FF $FF` if it found none. Function 72 is as it was
  (a first build grew its block by two bytes behind a marker; the review's EE2 replaced that). A new function is an
  interface addition, so OOLIB's interface version (function 80's `FT_INFO`) goes from 8 to 9, the number marking
  OODOS's own additions too (EE3, hw/oolib.md's question 15); OODOS stays 0.9, and 77's Since line names the
  interface, not an OODOS version (EE1). An OODOS before 9 answers 77 as a function it hasn't, A = X = `$FF`, which
  is also "unknown", so SYSINFO asks `FT_INFO` first and says which. `VERTEST` (`oodos/sys/test/vertest.s`) prints
  `FT_INFO`, 77's answer and 72's block, for test_oodos_sysinfo.
- **The library's routines built in D1:** `HW_PROBE`, `HW_NAME`, `HW_KNOWN`, `HW_CLOCK`, `HW_FRAMEHZ`, `HW_RTC`,
  `HW_SD_ID` and the output (`HW_HEX2`, `HW_HEX4`, `HW_HEX6`, `HW_DEC`), with two the table above hasn't: `HW_PUTS`
  (a 0-ended string, its length in Y) and `HW_DIV` (32 bits by 16, which `HW_DEC`, `HW_FRAMEHZ` and SYSINFO share).
  The memory tests (`HW_AIA`, `HW_MARCH`, `HW_WALK`, `HW_PORTAIA`) and the report lines (`HW_LINE`, `HW_FAULT`,
  `HW_SUMMARY`) are DIAG's, so they come with D2. Each module is a file, `backplane/oohw/hw*.s`; their zero page is
  one pointer, `HW_PTR` (`hwzp.s`). `oohw.inc` is the interface. The library's code is 930 bytes and its names 264,
  about 1.2 KB, as planned.
- **The build:** `oodos/Makefile`'s `OOHW_USERS` (SYSINFO now) are assembled with `backplane/bios` and
  `backplane/oohw` on their include path and linked with `build/oohw.lib`, through the rules gate (3,315
  instructions, no problems) and with no ld65 warning. `make_card.sh` copies `backplane/oohw/` into its container.
- **SYSINFO.COM is 9,704 bytes,** not about 5 KB: `cmd.inc` and `util.inc`, which every utility assembles in, are
  most of the difference.
- **The drives** are selected by function 14 in the error mode that returns errors (45, `$FF`), so a drive that
  isn't is passed over in silence, and their free space is function 46's. O: and P: (the Disk II's) aren't selected,
  so the Disk II's drive never turns for SYSINFO.
- **The clock** can't read exactly 7,159,090 Hz, as the plan's test said: the poll is about 80 cycles, so each edge
  is seen up to a pass late. By the DS3231 it read 7,159,031–7,159,122 Hz in the suite's runs (within 9 ppm), so the
  check is 0.002%; by the video frame 7,159,041–7,159,579 Hz (within 68 ppm: the frame counter is the vertical-blank
  interrupt's, which other interrupts can hold off), so its check is the plan's 0.01%. The golden files hold
  `#,###,### Hz` in their place, `hh:mm:ss` for the time, and `#K free` for the drives' free space (EE6), which
  changes with every file on the card; one exact check of A:'s, worked out from the card's directory, stays.
- **On the demo card too (D1-2, decided below),** with a line in its README; and on the test card, which changes
  OOUI's two picker frames (`u2-pick`, `u2-pick-turbo`: A:'s count and the list's scroll thumb), made again after
  looking at them. `feat/ooui-u3b-land` changes the same frames (OOUI.BNK on A:): whichever merges second makes them
  again.
- **The must-fail cases,** each made to fail once on a copy of the sources (`OODOS_UTILS_DIR`): the frame's
  correction taken out (60 frames counted as a second: 7,166,152 Hz, caught by the 0.01% check in three cases);
  `HW_PROBE`'s second read made absolute like the first (the floating bus then reads as a Disk II at revision 6,
  caught); `HW_PROBE` reading `+$7E` instead of `+$7F` (every card wrong: every golden file differs). The floating
  bus is a test-only device in the suite (answer 8): it answers each read with the byte before the CPU's PC, the
  instruction's last byte, as an empty slot might.

<a id="d1-questions"></a>
**D1's questions, decided (2026-11-13, the owner), as recommended:**

- **D1-1, the ROM BIOS's version:** first decided as function 72's block grown by two bytes; **replaced by the
  review's EE2 (2026-11-13, the owner):** a new function, 77 `O_ROMVER`, with function 72 exactly as it was; the
  interface version raised to 9 (EE3), 77's Since line the interface's (EE1). SYSINFO prints it. Built as above.
- **EE4:** 77's own test: a known version, `$FF $FF` when the cold boot finds no greeting, and SYSINFO on an OODOS
  that reports interface 8, each shown failing on a broken build. **EE5:** the VIA's timer 2 in answer 1's list.
  **EE6:** the drives' free space masked in SYSINFO's golden files, with one exact check of A:'s from the card.
- **D1-2, the demo card:** SYSINFO.COM is on it with the other utilities.
- **The OOUI frames:** made again after looking at them (above).

### D2: DIAG.COM ###

*Built on `feat/diag-d2`; doesn't merge (plan 2). What moves from it, and how: [plan 2](#plan2-what-moves).*

- **What:** `oodos/utils/diag.s`, `backplane/oohw/card6510.s` (built for both), the library's memory and port
  routines, the User's Guide's entry for DIAG, `DIAG.COM` on the card.
- **Size:** about 1,800 lines of ca65 and 250 of the 6510 program; DIAG.COM about 11 KB, under the 15 KB the W1
  contract leaves below `$4000`.
- **Faults the emulator needs** (test-only, in `emulator/test/host/`, [answer 8](#the-owners-answers)):
  - **a bad RAM bit:** a `FaultyMemory` device, mapped over a range after the machine is built (`Machine::bus().map`:
    later mappings win), which forwards to the RAM it covers but forces a bit, or aliases an address line. DMA goes
    through the same mappings (`Decoder::dma_read`, `dma_write`), so DMA sees the fault too. No emulator code
    changes;
  - **a bad card memory bit:** the same device over the aperture's bank 8, so the aperture and the port disagree
    (V4, D6). Card memory itself is inside `VideoCard`, so a fault seen by the port too would need a hook there: not
    needed for D2;
  - **a bad sound card RAM bit:** needs a test-only hook in `SoundCard` (a mask over a byte of its RAM). That is an
    emulator change, run under the sanitizers (`test_sound`), made in D2 ([answer 8](#the-owners-answers)); D3's
    CPU path uses the same hook;
  - **a missing card:** the machine's text without it, as D1.
- **Tests:** `test_oodos_diag`: golden output of `DIAG` on a good machine; a stuck bit in block 40 (M4 fails, return
  code 1, the line names `$A1234` bit 3); a stuck bit in block 8 (M2); an aperture fault (V4); no sound card (S1-S8
  skipped); the sound card claimed by another program first (`SKIP`, "in use"); `/WRITE` answered `N` (nothing on
  the card changed: the image's bytes compared) and `Y` (the file written and erased, the card's free space as
  before); the rules gate on DIAG.COM;
  - **the must-fail case for CC3:** ^C typed while P3 waits for CONT (`DIAG PANEL /FULL /I`), then a test program
    that reads the address P3 armed: it must run to its end without halting, and CTRL's bit 3 must be clear.
- **Test map:** `oodos/utils/diag.s` is under `oodos/`; the suite labelled `oodos sd video sound usb dma rtc console`.

<a id="d2-as-built"></a>
#### D2: as built (2026-11-13), not merged ####

*The record of `feat/diag-d2` (tip 4fdc4f2), copied here because the branch doesn't merge (plan 2). The files it
names are on that branch, not on main.*

**Built** on `feat/diag-d2`, for the owner's review. What differs from the plan above, and why:

- **DIAG.COM is bigger than 15 KB, so it is laid out around W1's window** (`oodos/cfg/diag.cfg`, D2-1 below). Its
  code is 15.3 KB (13.4 KB of it below `$4000`, 2 KB, `CODE2`, at `$8000`) and its strings and tables 6.6 KB:
  about 22 KB and 5,000 lines of ca65, against the plan's 11 KB and 1,800. So its code runs from `$0300` to `$3FFF` and on
  at `$8000`; `$4000–$7FFF`, which OOLIB maps, holds nothing of DIAG's (16 KB of zeros in the file, which is
  40,582 bytes); its data and BSS follow `CODE2`; its buffers are from `$AC00` to `$BFFC`. The W1 contract holds
  (`w1.py`: no segment in the window), and the rules gate checks `CODE` and `CODE2` (`rules.py --code
  CODE,CODE2`). DIAG doesn't include `util.inc` (2.8 KB), which it can't spare: it has its own start, end and
  tail (about 120 bytes).
- **M1 tests two runs,** the TPA DIAG doesn't use: `$4000–$7FFF` and from past its BSS to `$BFFC` (23,036 bytes
  now), not "from DIAG's end".
- **The 6510 program is about 800 bytes** (`card6510.s`, `$0200–$05FF`), not 400: the march, a page at a time, is
  most of it. So the RAM test the host asks for is `$0600–$FFF9` (`C65_FIRST`), and `$D000–$DFFF` is reached as
  RAM meanwhile (the port's bits 0–2 low). Its own pages, `$0000–$05FF`, aren't tested under DIAG (S2 and S3
  exercise them); the ROM core tests the card's RAM through RAMDATA (D3). **S4 is address in address quick, and
  the march too with `/FULL`**: the march at the 6510's 1 MHz is about 8 s. The 6510's answers come through the
  event queue (`EK_SOUND`, a 256-byte layout 2 area), which loses none, not the mailbox's stamp.
- **D6 and V4 read the card back into a tested block,** not a second TPA buffer (room: one 4 KB buffer); **D5
  compares 32 records** (4 KB), not all of DIAG.COM.
- **SR2 is timer 2 across a 1,000-cycle loop less across none:** the emulator holds the CPU on the VIA's
  registers (wait states), so two reads "1,000 cycles apart" by the instructions' count were 1,231 apart. The
  difference takes the reads' own cycles out.
- **S7 measures POS against the millisecond counter** over 400 ms (timer 1, PHI2, which NB5 holds to the DS3231),
  not against the DS3231's second directly: 4 KB at 8,000 Hz plays for half a second. Its first 2 KB must leave the
  stream buffering, the next start it.
- **P3 arms the breakpoint, then waits for RETURN** before its read halts the CPU: so a ^C typed at P3, with the
  breakpoint armed, is what console mode bit 3 is for. With the bit, function 10 gives DIAG the ^C, which puts
  CTRL, BPADDR and BPCTRL back (BPCTRL 0, HIT cleared), then the console mode, and ends (`$FFFE`); without it the
  BDOS's warm boot would leave the breakpoint armed (the must-fail case below). P2 keeps the user's breakpoint as it
  was. A ^C seen at a line's end anywhere else ends DIAG too, giving back the blocks, the sound card's parts, the
  event area, W1 and the panel.
- **DIAG reads W1's map from OOLIB's GX_INIT** (an OODOS of interface 8 and a revision 2 video card): without
  them the blocks can't be mapped, and NB2, M2–M5, D2–D6 and V4 are skipped (`no W1 map`).
- **The report:** a test's result is at column 65, its dots to 64 (`HW_LINE`); `HW_TEST` begins the line, and
  DIAG's `HW_PUTC` counts the column (`HW_COL`). A skip says why in brackets. The summary counts `INFO` lines as
  "measured". An interactive test writes its line, its prompts, then the line again with its result.
- **The library's routines built in D2:** `HW_WALK`, `HW_TAGW`/`HW_TAGC` (stage 4's tags, which DIAG runs on its
  blocks too), `HW_AIAW`/`HW_AIAC` (the plan's `HW_AIA`, its two passes), `HW_MARCH`, `HW_PAIAW`/`HW_PAIAC`
  (`HW_PORTAIA`), `HW_TEST`, `HW_LINE`, `HW_FAULT`, `HW_SUMMARY`. SYSINFO.COM is byte for byte as it was.
- **The emulator's one change** is `SoundCard::set_ram_fault()` (a byte read through a mask by the host, DMA and
  the 6510 alike), compiled only with `EMU_FAULT_HOOKS`, which `devices/CMakeLists.txt` defines for the native
  build with its tests and never for Emscripten's (the WebUI); its own case is in `test_sound`.
- **DIAG.COM is on neither card** (D2-2 below): `make_card.sh` leaves it out, and `test_oodos_boot`'s list of
  the utilities on the cards does too.
- **Time on the emulator:** a quick run of every area is 40.7 s of the machine's time (the plan's estimate was
  about 20 s: NB4's three seconds, NB5's and NB6's measurements, S4, S7 and the 6510's start add to the memory),
  `/FULL` 227 s. On `backplane.machine` every test passes or is skipped: SR1 and SR4 (the stand-in serial card),
  V5 (the ROM core's), and those of `/I`, `/FULL` and `/WRITE` not asked for.

**The tests** (`test_oodos_diag`, 12 cases, about 23 s): each was seen to fail on a copy of the sources with one
fault put in (`OODOS_UTILS_DIR`):

| Case | The fault that made it fail |
|---|---|
| the whole machine, golden (`golden/diag.txt`) | `HW_LINE`'s dots to column 63 |
| a stuck bit in block 40: M4, "block 40 +$1234 ($A1234): read $08, expected $00: bit 3", return code 1 | `HW_MARCH` returning at once |
| a stuck bit in block 8: M2 | `HW_AIAC` returning at once |
| an aperture fault (block 33): V4 | V4's port-to-slot-1 compare left out |
| no sound card: S1–S8 skipped | `sndup` not asking whether the card is there |
| the sound card held by another: "in use" | the claim's refusal not looked at |
| a stuck bit in the sound card's RAM: S4 | the 6510's check pass left out |
| `/WRITE` answered N: the card's image unchanged | the answer not looked at |
| `/WRITE` answered Y: written, read back, erased | the erase left out |
| P3: the read halts the CPU, CONT goes on | the breakpoint on a fetch, not a read |
| CC3: ^C at P3, then SWEEP reads the armed byte and runs to its end | console mode bit 3 not set: the warm boot left BPCTRL `$05`, and SWEEP halted |
| the rules gate on both code segments | (in the case: an RMB put at `CODE2`'s first byte) |

And `test_sound`'s hook case, with the hook made to do nothing.

<a id="d2-questions"></a>
**D2's questions** (moot under plan 2, kept as asked):

- **D2-1, DIAG.COM's size:** laid out around W1's window as above (built), or held to 15 KB below it, which would
  mean leaving tests out (the interactive ones are about 2.5 KB), or an overlay.
- **D2-2, the cards:** DIAG.COM on the demo card (and the test card, which changes `u2-pick`'s and
  `u2-pick-turbo`'s A: count and thumb), or on neither, as now.
- **D2-3, S4's march:** under `/FULL` only (built), or in the quick run too (about 8 s more).

### D3: the ROM core and BIOS 0.14 ###

*Replaced by plan 2's D3a, D3b and D3c ([sub-phases](#plan2-phases)); kept as plan 1's record.*

- **What:** `backplane/bios/diag.s`, the ROM build of the library, `romsum.py`, `bios.s`'s changes, `bios.cfg`'s
  block 23; the Technical Reference's chapter 3 (the entry and `C812R`), chapter 12 (switches 17–19 at reset, the
  lights' codes), chapter 2 (block 23); hw/design.md's Boot (BIOS 0.14, proposed).
- **Size:** about 1,700 lines of ca65 for the core and the prompt; about 140 bytes in block 31; 6.9 KB in block 23.
- **Tests:** `test_bios_diag`, on `backplane.machine` with the console card's switches set before reset:
  - switch 19 up and CONT pressed at the prompt (`ConsoleCard::set_switches`, then `press(CONT)` once the prompt
    is on the serial line): the core runs; golden serial output; the screen's text read from card memory; DISPLAY
    `$FF` at the end; a key boots on as usual (the greeting, then the monitor). The same with `Y` on the serial line,
    and on the USB keyboard;
  - **the must-fail cases for the prompt:** switch 19 up and no answer: the prompt, DISPLAY `$A5`, then the boot as
    usual within about 6 s of emulated time, RAM unchanged (the test fills it with a pattern before the reset and
    compares after the prompt has ended, before the boot writes it); switch 19 up and `N` typed: the boot at once;
    CONT pressed before the reset (KEYS then cleared by it): the prompt still waits; switch 19 up and START pressed
    at the prompt: a hard reset and the prompt again, nothing entered; switch 19 down: no prompt, the boot as now;
  - **for CC5:** a machine with no Disk II line, whose `$C67F` the test makes answer as a floating bus would (a
    test-only device returning the last byte fetched): `HW_PROBE` and the core both find no Disk II;
  - `C812R` with `N`, then `Y`;
  - the faults: a stuck bit in block 0's first 512 bytes (the zero page moves to block 1, and stage 6 reports block
    0), a stuck bit in block 41 (stage 6), every RAM block bad (DISPLAY blinks `$82`, the block, the bits, sampled
    over time), RAM B's line left out of the machine ("RAM B: not answering"), a byte of block 30 changed in a copy
    of `bios.rom` (`MachineOptions::rom_override`: stage 1 fails block 30), no video card (serial only, stage 8
    skipped), no console card (no lights, the switches can't be read: the BIOS boots as now);
  - a block 23 that is erased (a copy of `bios.rom` with it blanked): `NO DIAGNOSTICS IN THE FLASH`, then the boot;
  - the time of a quick run, in emulated cycles, reported in seconds at 7.16 MHz against the table above;
  - `test_bios`, `test_apple`, `test_iie`, `test_oodos_apple`, `test_rom_boot`: unchanged and passing.
- **Test map:** `backplane/bios/` already chooses `bios docs web`; the new suite labelled `bios bridge console video
  sound dma sd usb backplane`, as `test_bios` is.

### Not planned ###

- Testing OODOS's own memory from under OODOS. That is the ROM core's. (Plan 2: nothing is tested under OODOS.)
- The serial card's tests beyond SR4: nothing to test until the TL16C552A card exists.
- The ESP32's bus spy as a test host ([design.md](design.md#boot)): later, using the core's serial output.


## The owner's answers ##

**Decided (2026-11-13, the owner):** questions 1–9 as recommended; question 1 amended by review round 1 (CC6), and question 2
replaced by the owner's option A, the prompt. The alternatives are kept for the record.

**Under plan 2 (the owner's redesign, 2026-11-13):** answer 1's exception covers SYSINFO alone (and whatever the
launcher needs: nothing, if P2-1 is chosen as recommended), and D3c narrows AGENTS.md to it; answer 4 becomes "block 23,
or more blocks" (P2-3); answer 5 stands for the ROM, and `/WRITE` goes with DIAG.COM; answer 7 is replaced by `DIAG`
at `A>` (P2-1, P2-2). Answers 2, 3, 6, 8 and 9 stand.

1. **Hardware DIAG and SYSINFO touch directly under OODOS. Decided: allowed for DIAG and SYSINFO** (plan 2:
   SYSINFO alone), named in
   AGENTS.md as an exception for the system tools (written in D1), with no OODOS change. AGENTS.md otherwise asks
   for the owner's word before a program touches hardware OOLIB doesn't cover. The whole list (CC6):
   - **Reads:** each card's ID byte (twice, CC5); `NB_STATUS`; `NB_RTCCTL`; IRQL and IRQH (NB3: the same poll the
     dispatcher makes, which acknowledges nothing and changes nothing); `VIDEO_MEMSIZE`; `USB_DEVICES`; `SD_STATUS`;
     the console card's SWR, PANEL, STATUS, LADDR and LDATA; `NB_SEC` polled for the clock (its effect on the
     bridge's I²C reads on real hardware unknown, as stage 11 says).
   - **The VIA's timer 2** (EE5, added 2026-11-13): `T2CL` and `T2CH` written and read, for `HW_CLOCK`'s count.
     That changes only IFR's timer 2 flag (a read of `T2CL` clears it), which OODOS doesn't use; never ACR, IER or
     timer 1.
   - **Writes:** RTCREG, to read the DS3231's temperature through RTCDATA (an I²C read; BUSY waited for); SD's
     `IDENTIFY`; the console card's DISPLAY, CTRL's bit 3 (P2), and its breakpoint (P3: BPADDR, BPCTRL, STATUS's
     HIT); and, with the parts claimed through function 82, the sound card's registers as the Programmer's Guide
     allows a claimed part's, S6's included.
   - **S6 (`/FULL`)** writes every byte of the ES5503's 128 KB of wave RAM through DOCDATA. With `SC_DOC` claimed
     the chip and its RAM are DIAG's, and nothing kept there belongs to anyone else (the boot splash's samples are
     loaded at each boot); its oscillators are halted first, and `SN_RELEASE` gives the chip back stopped.
   - **P3 (`/FULL /I`)** halts the CPU until CONT. While it is halted nothing runs: no interrupt is taken, so
     OODOS's 1 kHz tick and the millisecond counter stop (timer 1 runs on, so one tick is taken late and the rest
     of the halt's ticks are lost), the frame counter and any vertical-blank copies miss the frames (DIAG sets none),
     the USB card's event FIFO can fill and drop events, and the sound card plays on by itself. The DS3231 keeps the
     time, so function 105 is right after; the millisecond counter is behind by the halt. On the card RDY holds the
     CPU on the cycle; the emulator stops between instructions.
   - **Why IDENTIFY can't collide with OODOS's driver:** OODOS drives the SD card only inside BDOS and BIOS calls,
     each command waited for until BUSY clears (`oodos/sys/bios.s`), and never from an interrupt: its SD interrupt
     stays off (`SD_IEN` 0, `irq.s`'s DEAFEN) and the dispatcher doesn't serve the card. A program runs only between
     those calls, so when DIAG writes CMD no command of OODOS's is under way. IDENTIFY changes nothing on the card
     (LBA and COUNT stay), DIAG reads DATA by the CPU with CTRL's DMA bit clear and puts CTRL back as it found it,
     and OODOS's next command writes UNIT, LBA, COUNT and CTRL afresh.

   *Alternative (not chosen):* a new OODOS function that returns the IDs, the SD card's identity and the RTC's
   status.
2. **The front-panel entry. Decided (2026-11-13, the owner, option A; it replaces round 1's HALT and LOCK
   combination):** switch 19 up at any reset shows a prompt on the screen and the serial line, with `$A5` on the
   lights; a confirmation within 5 s enters the core, anything else or nothing boots as usual; nothing is written
   before the confirmation. HALT/ENABLE and the keyswitch are no longer part of the entry. Switches 18 and 17
   choose full and loop. **Found in writing it in:** the panel's confirmation can't be START.
   START is the hard reset itself, not a KEYS bit, so pressing it at the prompt resets the machine and shows the
   prompt again; it starts nothing else (a hard reset, which also leaves Apple II mode, as any START does). Telling
   that reset apart would need a marker written into a register a reset keeps, before the confirmation.
   **Decided (2026-11-13, the owner): CONT** on the panel, or `Y` on a keyboard: CONT is latched in KEYS while the CPU runs and does
   nothing else then, and every reset clears KEYS ([How it is entered](#the-rom-core)). *Alternative (not chosen):* LOAD ADRS,
   latched the same way, but it copies the switches into PADDR as it is pressed.
3. **The monitor's entry. Decided:** `$C812` (`C812R`), with the prompt and `Y` before it overwrites memory.
   *Alternative (not chosen):* no prompt.
4. **Where the core lives. Decided:** flash block 23 (plan 2: blocks 21–23 proposed, P2-3), through W2 ([ROM space](#rom-space), option 1).
   *Alternative (not chosen):* block 31's free 7,632 bytes (option 2), which would leave the BIOS about 1.1 KB.
5. **Writing the SD card. Decided:** the ROM core never writes the card (stands under plan 2); DIAG.COM's `/WRITE` writes, reads back and
   erases a 64 KB file on A: after `Y`. *Alternative (not chosen):* a raw test of a user's `/DIAGTEST.BIN`.
6. **The serial card's ID. Decided:** the TL16C552A card's ID is `$61`, written into hw/design.md in D1; the
   emulator's stand-in stays as it is, and the tools say "the stand-in 6551 (no ID register)" for any other value.
   *Alternative (not chosen):* the stand-in answers `$60`.
7. **From OODOS to the core.** *Replaced by plan 2: `DIAG` at `A>` ([the launcher](#plan2-launcher)).* **Decided then:** no OODOS change: `WOZMON`, then `C812R`, as the User's Guide's DIAG entry
   will say. *Alternative (not chosen):* function 75 with A = 2.
8. **The emulator's faults. Decided:** test-only, in `emulator/test/host/`: `FaultyMemory` mapped over a range after
   the machine is built (and, for CC5, a device answering as a floating bus); for the sound card's RAM a test-only
   hook in `SoundCard`, the one emulator change, made in D2. *Alternative (not chosen):* a `fault` line in the
   machine-file language.
9. **Where the library lives. Decided:** `backplane/oohw/`. *Alternative (not chosen):* `oodos/lib/oohw*.s`.

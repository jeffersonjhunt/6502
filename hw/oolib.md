# OOLIB: the OO-1's game and device services #

**Status: draft (2026-10-30); the plan and the open questions decided 2026-11-07 ([The plan](#the-plan)).** This document proposes how OO-1 programs reach the
graphics, sound, timing and input hardware, so that every game and GW-BASIC use the same calls and nobody invents
their own. The hardware is in [design.md](design.md); OODOS, the operating system these services join, is in
[oodos.md](oodos.md). The questions at the end are for the owner to settle, as OODOS's phase 0 settled its own. They
are settled in the books' phase G3 ([docs/README.md](../docs/README.md#phases)), which writes the Programmer's
Guide's chapter 8 on what is decided. **A plan for building it is proposed (2026-11-07)** under
[The plan](#the-plan), with a recommendation on each question.


## Goals ##

- **One way to do each thing.** Every program sets up the screen, plays a tune, waits for a frame or reads the
  joystick through the same interface. A program that does it its own way gets no help from the system and may
  collide with it.
- **Fast enough for games.** A frame at 60 Hz is about 119,300 cycles at 7.16 MHz. Anything a game does many times a
  frame (put a tile, move a sprite, plot a point) must cost what a `JSR` costs, not what an operating system call
  costs.
- **The system owns what is shared.** Interrupts, DMA channels, the sound card's processor and the event queues
  belong to OODOS, so two parts of a program (or a program and an RSX) never fight over them.
- **A program leaves nothing behind.** When it ends, the warm boot stops its music, its hooks and its DMA, and puts
  the console back, as it already frees the memory a program allocated (phase 3c).
- **The first client is GW-BASIC** (OODOS phase 7). Its machine layer (SCREEN, PSET, LINE, CIRCLE, PAINT, SOUND,
  PLAY, BEEP, INKEY$, STICK, STRIG, ON TIMER, ON KEY) is written on these services, so they are tested by a real
  program from the start.


## What the hardware gives ##

| Area | Hardware ([design.md](design.md)) | Notes for the services |
|---|---|---|
| Video | VGA card `$C480`, ID 9: two layers (text, tile, bitmap 256, bitmap 512, mono) and 64 sprites; palette of 256 12-bit colours; data in banks 8–B, snooped | Base and scroll registers latch at vertical blank. Interrupts: vertical blank, a chosen scan line, sprite collision |
| Sound | Two SIDs `$C500`, ID A, and a 6510 on the card with its own 64 KB, a one-byte mailbox each way. Revision 2 (ID `$A2`, BIOS 0.12): a stereo DAC fed by a 128 KB FIFO (raw PCM at 8–48 kHz, or MP3 the card decodes), a ten-band EQ, volume and a vis page; an ES5503 with 128 KB of wave RAM; DRQSEL picks what the card's DMA request feeds | SID writes are ignored while the 6510 runs. `.sid` tunes play on the 6510 with no main-CPU time; a stream plays from the FIFO with none either, as long as it's kept fed |
| Speaker | USB card `$C5A6` (SPKR) | One bit, toggled per access |
| Keyboard | USB card `$C580`, ID B: cooked KEY (ASCII, repeat) and a raw event FIFO (HID usage + press/release) | OODOS's console reads only KEY today |
| Mouse | USB card `$C590`–`$C59A`: buttons, relative or absolute X/Y, wheel | Absolute mode suits a pointer sprite |
| Game port | USB card `$C5A0`–`$C5A5`: four paddles 0–255, three buttons | Apple-style joystick |
| Joysticks | Two digital DE-9 ports (Atari/Commodore pinout) on the serial card's W65C22S, `$C341` and `$C340` bits 0–4, 0 = pressed (proposed 2026-10-30) | One load reads a stick; no interrupt needed |
| Timing | Vertical blank (60 Hz), the scan-line interrupt, the DS3231's 1 Hz TICK (ID D), and the W65C22S's two timers at 7.16 MHz (proposed 2026-10-30) | Timer 1 is OODOS's system tick; timer 2 is the program's |
| DMA | Controller `$C400`, ID 8: four channels (0 storage, 1 serial, 2 USB / sound RAM, 3 general: copies and blits); memory to memory at 2 cycles a byte | OODOS's BIOS uses channels 0 and 3 today |
| Interrupts | IRQL/IRQH at `$C688`: one bit per module ID | Since L1 OODOS's dispatcher answers them, and the BDOS runs with interrupts on (question 14; [L1, as built](#the-plan)) |


## Proposed: three layers ##

Costs that decide the split, measured on OODOS:

| Call | Cycles |
|---|---|
| `JSR`/`RTS` | 12 |
| FARCALL (into a block in W3) | 281 |
| a BDOS call, the stub alone (in and out) | 216, plus the dispatch and the function |

**1. OODOS services: what has to be shared.** New OODOS functions of our own, as 72–74 are. They are called rarely
(at setup, or a few times a frame), so the BDOS's cost is fine, and the work they start runs in OODOS's interrupt
handlers, outside the program's time.

**2. Shared data: what a program reads every frame.** The program gives OODOS a small block of its own memory
(an *event area*), and the interrupt handlers write into it through the program's saved windows, as 3c's DMA
segment writes records to physical memory. The program then reads the frame counter, the keys held and the event
queue with ordinary loads: no call at all.

**3. OOLIB: a library linked into the program.** Graphics is register and memory work with nothing to share, so it
is a ca65 library in this repo (`oodos/lib/`, with cc65 C headers), linked into each `.COM` that wants it. Each
program carries the parts it uses; every program uses the same interface. It calls layer 1 for the few things that
need the system (the sprite table at vertical blank, a DMA channel).

Why not everything as OODOS functions: a sprite move at about 250 cycles, a few hundred a frame, would take most of
the frame. Why not everything as a library: the interrupts, the DMA channels and the sound processor would then
have as many owners as there are programs and RSXs.


## Proposed services by area ##

Function numbers, register conventions and error codes are left for the design step; these are the services.

### Interrupts and the frame ###

- **An interrupt dispatcher in OODOS.** Common memory's IRQ entry reads IRQL/IRQH and runs the handler of each card
  interrupting: video, USB, sound mailbox, DMA terminal count, the clock's TICK, and the serial card's VIA (its
  INTSTAT bit 3, then the VIA's IFR: timer 1, timer 2, CA1/CA2, CB1/CB2, the shift register). It is the piece
  everything else in layers 1 and 2 needs, so it is built first.
- **The frame counter:** counted at each vertical blank, in the event area (32 bits: about 2.3 years).
- **Wait for the next frame:** a call that returns at the next vertical blank (or the program polls the counter).
- **A frame hook:** a routine of the program's that OODOS calls at each vertical blank, for music cues, palette
  fades and scrolling. It runs in an interrupt, so it has rules (open question 3): which windows are mapped, how
  long it may take, no BDOS calls, none of the program's zero page unless it says so, and never a write to the
  VIA's ACR, IER or IFR (see [The VIA is shared](#the-via-is-shared)).
- **A scan-line hook:** the same at a chosen line, for split screens (a status bar over a scrolling map).
- **Sprite collision:** the collision groups at the last vertical blank, in the event area, and optionally a hook.

### Timing ###

Two clocks, both in the event area:

- **The frame counter** (vertical blank, 1/60 s) is the clock for anything drawn: animation, scrolling, sprite
  moves.
- **The millisecond counter** (32 bits, about 49 days) is OODOS's system tick: the VIA's timer 1, free-running at
  1 kHz (proposed; open question 11). It is the clock for anything that isn't tied to the picture: game logic
  timers, input sampling, `ON TIMER`, timeouts, and sound effects sequenced on the main CPU.
- **A tick hook:** a program routine OODOS calls every *n* ms, as the frame hook is called every frame, with the
  same rules and a tighter budget (open question 3), since it runs up to 60 times as often.
- **Delays:** "wait *n* frames" and "wait *n* ms", built on the two counters.
- **Seconds and the date:** CP/M 3's function 105 and the SCB, as now.
- **Raster effects:** the scan-line interrupt (a line is about 31.8 µs), as before.
- **Timer 2 is the program's,** through OODOS (below): a one-shot after *n* cycles (up to 65,535, about 9.2 ms),
  or a count of pulses on the user port's PB6. Its interrupt reaches the program as a hook, or the program polls
  its flag.
- **What the tick costs.** Every tick is an interrupt: the dispatcher's entry and exit, the counter and the input
  sampling below, about 100 cycles, is 100,000 cycles a second at 1 kHz, about 1.4% of the CPU; a tick hook adds
  its own. Music is unaffected: the sound card's 6510 keeps time for tunes with its own CIA.

#### The VIA is shared ####

Timer 1 is OODOS's and timer 2, the ports and the shift register are the program's, but they share the VIA's control
registers: the ACR holds both timers' modes, the shift register's mode and the ports' latching; the IFR and IER hold
every interrupt's flag and enable. A program that writes them directly can stop the system tick or take its
interrupt without knowing it. So:

- **OODOS owns ACR, IER and IFR.** A program sets timer 2's mode, the shift register's mode, the ports' latching and
  its interrupts through OODOS calls that change only its own bits.
- **A program may write** timer 2's counter, the shift register, the port registers and the DDRs, and PCR's bits for
  the lines it has claimed (the user port's handshake lines).
- **Every VIA interrupt goes through OODOS's dispatcher,** which clears the flags and calls the program's hook for
  its own (timer 2, CA1/CA2, CB1/CB2, the shift register). A program has no raw IRQ handler of its own.
- **The warm boot** stops timer 2, turns off the program's interrupts, puts the shift register off and every port
  line back to an input (the joystick lines are inputs anyway), and leaves timer 1 running.

### Input ###

- **An event queue** in the event area: key press and release (USB HID usage, with the modifiers), mouse movement
  and buttons, game port buttons, a device plugged in or out. One format for every kind of event, with the frame
  count of each.
- **Keys held now:** a 256-bit map (32 bytes) by HID usage, kept from the raw events, so `IF key held` is one
  load and an AND.
- **Controllers, not devices.** Game input comes from three kinds of hardware: the two digital joysticks on the
  VIA, the Apple-style joystick on the USB card's game port (two paddles and buttons), and USB gamepads later. The
  event area presents them all as **numbered controllers in one format**: four direction bits and the fire
  buttons, plus an analogue X and Y (0–255) where the device has one. A digital stick gives X and Y at 0, 128 or
  255; the Apple joystick gives directions where X or Y is past a threshold (open question 12). A game asks for
  *controller 1* and works with whatever is plugged in; which hardware is which controller is open question 12.
- **Sampled every tick,** so a press shorter than a frame isn't missed, and fire presses and releases go into the
  event queue with the millisecond count, as keys do. A game may still read a digital stick itself from the VIA
  (`LDA $C341`, one load), as the hardware allows.
- **The paddles themselves** (0–255 each, all four) stay in the event area for games that want paddles, not a
  joystick. GW-BASIC's STICK and STRIG read controllers.
- **The mouse:** position (absolute mode with limits) and buttons in the event area; an optional pointer sprite
  OODOS moves at each vertical blank.
- **The console keeps working.** Function 6 and the console functions still get characters from KEY, as CP/M
  programs expect; the event queue is extra, not a replacement. Whether a game can turn the console's keys off (so
  ^C, ^S and ^P don't act while it plays) is open question 4.
- **The keyboard's LEDs, repeat rate and layout:** a call each, so a game can turn repeat off.

### Graphics (OOLIB, linked) ###

- **Modes:** set either layer to any type the card has, with its memory placed for the program; put the console's
  text screen back.
- **The palette:** load, set one entry, fade (by DMA at vertical blank, through layer 1).
- **Tiles and maps:** load tile images, put and get map entries, scroll each layer (the card latches at vertical
  blank, so no tearing).
- **Sprites:** a shadow table of 64 entries in the program's memory; set position, image, flips, depth, palette and
  collision group; OODOS copies the table to the card by a 512-byte DMA at vertical blank (about 72 µs), as
  design.md proposes.
- **Bitmaps:** plot, line, rectangle, fill, circle, a flood fill (PAINT), text in a font on a bitmap; copies and
  fills by DMA channel 3.
- **Double buffering:** draw into one buffer, flip the base register (latched at vertical blank).
- **Video memory:** banks 8–B are shared by the console (its text screen and font) and the program. OOLIB hands
  out pieces of them; the warm boot puts the console's back.

### Sound ###

The card's 6510 can be used three ways; the services cover all three, one at a time:

- **OODOS's own music driver on the 6510** (proposed default): a driver of ours on the card that plays tunes and
  sound effects from commands sent through the mailbox. A program says *play tune n*, *stop*, *fade*, *play effect
  e on voice v*, *set volume*, and gets *tune ended* and sync points back in the event area. GW-BASIC's SOUND, PLAY
  and BEEP use it. The tune and effect format is ours (open question 5).
- **A `.sid` file:** played as the emulator's player does today (`sid_file.h`), moved into OODOS: load, choose the
  song, play, stop. The card does all the work.
- **The program's own driver:** a program loads its own 6510 code onto the card and talks to it through the
  mailbox; OODOS only loads it, starts it, and stops it at the warm boot.
- **The SIDs directly:** with the 6510 stopped, a program may write the SID registers itself (design.md allows it);
  a call stops the driver and hands the SIDs over.
- **The speaker:** a beep (pitch and length), timed by OODOS.

Revision 2 adds three more, each a claim of its own (question 6):

- **A stream:** start a raw PCM or MP3 stream (rate, format, codec), feed it, pause, seek (FLUSH), end it (EOF), and
  read its state, position, free space and bitrate. The program reads the file; OODOS sends each buffer to PCMDATA by
  DMA channel 2 (DRQSEL 1), so the program never drives the channel itself. The card plays on its own clock, so
  a slow frame costs nothing while the FIFO has data: 0.74 s of 44.1 kHz 16-bit stereo, 8 s of 128 kbps MP3.
- **EQ, volume and the vis page:** register work with nothing to share, so OOLIB (linked), not OODOS calls.
- **The ES5503:** load wave RAM by DMA (DRQSEL 2), set oscillators, and take its interrupts through the
  dispatcher. It and a PCM stream exclude each other (the card's STATE `$82`); the SIDs run alongside either.

### The user port ###

- The VIA's ten spare lines and four handshake lines are raw I/O for hobby hardware (an LCD, sensors, a second
  keyboard). OOLIB doesn't wrap them: a program drives them itself, within the rules above.
- **A claim call** gives them to one owner at a time, so a program and an RSX can't both drive them; the warm boot
  frees the claim (open question 6).

### DMA ###

- **Channels have owners.** OODOS keeps channel 0 (storage). Channel 3 (copies and blits) is shared: OOLIB and the
  BIOS both use it, so a call takes it and gives it back, or OODOS queues copies for it. Channel 2 loads the sound
  card's RAM. Which channels a program may own outright is part of open question 6.
- **Copy and fill** between any physical addresses, checked as 3c's segment is (none of OODOS's own memory).

### Files and assets ###

- **Load a file to physical memory** (tiles, pictures, a tune) by 3c's DMA segment: mostly built already, but only
  a record (128 bytes) a call: a program sets the segment (51) and loops read-sequential (20).
- **Load to memory by DMA** (proposed, the owner's 2026-11-07): one BDOS call that loads a file, or a part of one,
  into any physical memory, video included, by DMA, rather than a record at a time through the CPU. Video memory
  is one destination among others: tiles, pictures, sound samples, overlays, data in the upper banks. It refuses
  OODOS's own memory, as 51's segment does. Open question 13.
- **Asset formats:** a small header for tiles, sprites, palettes, maps and tunes, so tools on Linux can make them
  (a converter from PNG, say). Open question 8.

### Memory ###

- Banks 1–5 by 53–58, as phase 3c built, for a program's data and code; FARCALL for code in a block (phase 7's
  common-memory question applies).


## What a program sees, in brief ##

```
setup:  OODOS: open the event area (address, size); set the frame hook (and a tick hook, if it wants one)
        OOLIB: set layer 0 to tiles, layer 1 to text; load tiles, map, palette, sprites
        OODOS: start the music driver; play tune 1
loop:   read the event area: frame and ms counters, keys held, controllers, events
        OOLIB: move sprites in the shadow table, put map entries, scroll
        OODOS: wait for the next frame (the sprite table goes to the card then)
end:    RTS: the warm boot stops the music, the hooks, timer 2 and DMA, frees the user port, and puts the
        console back
```


## GW-BASIC on these services ##

| GW-BASIC | Service |
|---|---|
| SCREEN, COLOR, PALETTE, CLS, WIDTH | OOLIB modes and palette; the console's text screen |
| PSET, PRESET, LINE, CIRCLE, PAINT, DRAW, GET/PUT (graphics), POINT | OOLIB bitmaps |
| SOUND, PLAY, BEEP | the music driver; the speaker |
| INKEY$, INPUT$ | the console (function 6) |
| ON KEY, KEY(n) | the event queue, keys held |
| STICK, STRIG, ON STRIG | controllers 1 and 2 in the event area (fire events for ON STRIG) |
| ON TIMER, TIMER | the millisecond counter and the tick hook |
| ON PLAY | the music driver's sync points |
| DEF SEG, PEEK, POKE, BLOAD, BSAVE | physical memory by DMA (3c), video banks included |


## How it fits OODOS ##

- **A program asks first.** Function 72 (or a new one) reports which services and which OOLIB interface version
  OODOS has, so a program written for later services can say so instead of crashing.
- **RSXs** (OODOS phase 5) see the services like any program; an RSX that hooks the frame shares the dispatcher's
  list.
- **The warm boot** stops everything a program started (hooks, the music driver and any tune, DMA channels it
  took, timer 2, its VIA interrupts and the user port's claim, the pointer sprite), empties the event queue, closes the event area, and puts the video card back to the
  console's text screen and font.
- **Apple II mode** is untouched: these are native-mode services.
- **Phasing (proposed):** a new OODOS phase before GW-BASIC (between 6 and 7, or before 6), in steps:
  1. the interrupt dispatcher, the system tick (VIA timer 1), the frame and millisecond counters and the event area;
  2. input: the event queue, keys held, controllers, the mouse;
  3. sound: the music driver, `.sid` playback, the speaker;
  4. OOLIB graphics and the sprite upload;
  5. a demo game of ours, which tests it all together and goes on the website's card (phase 8).


## The plan ##

**Proposed (2026-11-07), for the owner.** Nothing here is built until the owner has settled the questions below.

**Decided (2026-11-07, the owner):** the plan as written, and every question (1–15) as recommended. L1 first.
Each step reads its part of the books again at its start and says here what changed, as phase 6's steps did.

**What there is now (main, 9887b59)**

- **OODOS takes no interrupts.** Common memory's IRQ entry (`oodos/sys/common.s`) sends a BRK to the monitor and
  returns from anything else at once. The BDOS stub sets the interrupt mask on entry and never clears it, so a
  call runs masked from start to finish, and the Disk II driver masks them on purpose while it reads and writes
  (`disk2.s`). Nothing enables a card's interrupt.
- **The emulator already has what OOLIB needs:** the VIA's two timers and its flags (`via6522.cpp`); the video
  card's vertical-blank, scan-line and collision interrupts; IRQL/IRQH; the USB card's raw event FIFO, mouse and
  game port; the DMA controller's four channels; and sound card revision 2.
- **Function numbers:** OODOS's own are 50–59 and 72–76; 60 is CP/M 3's RSX call; 61–71 and 77–97 are free.
  Function 72 (O_VERSION) already reports OODOS's version.
- **The first client is already being written:** OO-Amp, the owner's music player (a separate repo). Its
  `oa_hal.s` does by hand, with the owner's approval, what OOLIB will do: it remaps W1, polls vertical blank, reads
  the USB event FIFO directly, uses DMA channel 2 for the stream, and saves and restores the video registers. Its
  calls are named after this document's services, so OOLIB can replace the file.

**The steps.** Each is a branch, reviewed and merged on its own. Its tests run on the emulator through
`oodos_rig.h`, and in the quick tier.

- **L1: the dispatcher and the clocks.** The IRQ entry reads IRQL/IRQH and runs each card's handler. VIA timer 1
  runs at 1 kHz and keeps the millisecond counter; vertical blank keeps the frame counter. The event area can be
  opened and closed. Wait for frame, and the frame and tick hooks with question 3's rules. The BDOS runs with
  interrupts enabled (question 14). The warm boot takes everything back: hooks, interrupts, the event area. Tests:
  the counters against emulated time (they must not depend on host speed), a hook's budget, a disk read that
  loses no ticks, and a ^C or Break mid-hook.
**Proposed (L1, as built, 2026-11-07)** (`oodos/sys/irq.s`, new; `common.s`, `disk2.s`, `bdos.s`, `cold.s`,
`crt.s`; `oodos/include/oodos.inc`'s `O_FRAME`, `FT_*`, `FC_*`, `EV_*`, `OA_*`; bit names in `backplane.inc`, the
ROM byte-identical). The differences from the questions below were the owner's decision (2026-11-07), each noted at
its question.

- **The dispatcher,** in common memory (IRQ's RTI at IRQ + 12 still the way out when no card interrupts): BRK to
  the monitor as before; A, X, Y and W0 saved; W0 on the system block (31) with S left as it was. The BDOS runs
  on the system stack, so when it is interrupted the dispatcher stacks under it; when anything else is, the system
  stack is empty (the stub starts it afresh each call), so the same S is free there. It reads IRQL and IRQH once
  each: the VIA (ID 6) alone is the fast path, then video (ID 9); anything else, and a VIA flag other than timer
  1's, is turned off (DEAFEN, in block 62's code window, W3 switched for it): a source OODOS didn't turn on can't
  hold /IRQ low. The serial card's INTSTAT isn't modelled, so the VIA is told by its IFR; cold boot finds the VIA by
  IER bit 7 (the 6551 stand-in's mirror there reads its command, `$0B`) and the video card by its ID. Without a VIA
  (review, 2026-11-08) ID 6 is the 6551's, whose registers are mirrored at `$C340–$C34F`: the dispatcher takes
  neither VIA path (the fast path compares IRQL with `VIAL`, a zero-page byte cold boot sets to `<~NB_IRQ_VIA` only
  if it found the VIA, else 0, which no IRQL is), so it never writes the 6551's command and status as IER and IFR
  nor reads its data as T1CL; DEAFEN turns its receive interrupt off.
- **DMA copies** (review, 2026-11-08, the owner chose 2 KB): the CPU is held through a memory-to-memory copy, 2
  cycles a byte, and the VIA has one flag, so a copy longer than a tick (3,580 bytes) could cover two of timer 1's
  expiries and lose one: the console's scroll is 4,640 bytes, and lost a tick about one scroll in three. The BIOS's
  DMAGO, which every channel-3 copy goes through (MOVE's, the scroll, the erase), copies 2,048 bytes a block at
  most, and a pending interrupt is taken between blocks.
- **The clocks:** timer 1 free-running (ACR `$40`, latch 7,157: 7,159 cycles, 1 kHz) into a 32-bit millisecond
  counter; the vertical blank into a 32-bit frame counter (`MS`, `FRAMES` in the system block).
- **Critical sections** (question 14): masked only while the stub moves the windows and S, in (`CLI` just before
  the BDOS) and out (`SEI` at `STUBRET`), in the warm boot, and in the Disk II driver's fields. The dispatcher
  touches no BDOS state, and puts back every window it changes; nothing else needed masking (the allocation
  vector is the BDOS's alone). The warm boot enters the CCP with interrupts on, so programs start with them on.
- **The Disk II** (decided by the owner): the code never masked reads (the stub masked everything), only writes,
  from the search to the end. Now both mask each field, from its D5 to the end of the data field read or
  written after it, about 12 ms (shorter than a frame: no vertical blank is lost), and unmask while searching.
  The driver counts the disk bytes read meanwhile (RBYTE), 224 cycles each, plus the write loop's 356, adds that
  time to the millisecond counter as it unmasks (MSACC keeps the fraction of a tick), and clears timer 1's flag,
  whose tick is in the count. A PIP to or from O: keeps time to 0.5% (sync bytes' 280 cycles counted as 224); the
  PIP of 20,000 bytes from O: takes 3.09 s against 3.00 s before.
- **The event area,** layout 1, 16 bytes (`EV_LEN`) at least: +0 the layout (1), +1 the clocks (`FC_FRAME`
  `$01`, `FC_MS` `$02`), +2 its size, +4–7 the ms counter, +8–11 the frame counter, +3 and +12–15 0. In one window
  W0–W2, on a block of banks 0–5; written through that block (W1 put on it for each write), the whole of it at
  the open (0 past layout 1), then each tick the ms counter's low byte (all eight bytes after a carry or a
  catch-up) and each frame all eight. L2's input goes after +16; a program says how big its area is.
- **Function 80** (decided by the owner: question 2's "operation in A and a block for more" doesn't fit A/X): X =
  0, A is the operation, no parameters; else A/X is a block whose +0 is the operation, as 50's. `FT_INFO` (0),
  `FT_COUNT`, `FT_OPEN`, `FT_CLOSE`, `FT_WAIT` (by `WAI`), `FT_DELAY`, `FT_FRAMEHOOK`, `FT_TICKHOOK`, `FT_COSTS`.
- **Question 1 is answered through 80, not 72** (the owner's decision): 72 stays 10 bytes, as on main; `FT_INFO`
  returns OOLIB's interface version (1) in A and the service-area bits in X (`OA_FRAME` `$01`; 81–84 `$02`–`$10`
  later), and an OODOS without 80 answers A = X = `$FF`.
- **The hooks** (decided by the owner, question 3): each called with the windows (W0–W3) the program had when it
  set it, A, X, Y and the flags saved. Its stack is page 1 of its own W0, the program's: question 3's "the system's
  stack" can't be had with the program's windows and zero page, since the stack page is in W0 with the zero page.
  The dispatcher puts S under both the program's stack and, when the BDOS was interrupted, the S the program had
  at its call (the stub's `ST_S`), whichever is lower, so neither is written over. The frame hook runs with
  interrupts on (its 8,000-cycle budget is more than a tick: masked, about one frame in six would lose a tick),
  with a guard: a vertical blank while it runs is counted, the hook not called again (`FHSKIP`). The tick hook
  runs masked. A debug build (`make DEBUG=1`) times each by timer 1, from the call to the return (so about 130
  cycles of the dispatcher's besides the hook's, and for the frame hook the ticks under it), for `FT_COSTS`.
- **The warm boot:** both hooks off, the event area closed, every interrupt enable but timer 1's and the vertical
  blank's off (DEAFEN), the CCP entered with interrupts on; the counters run on. DEAFEN also (review, 2026-11-08)
  sets timer 1 up again as cold boot did, should a program have changed it: ACR `$40`, its enable, and its latch,
  written only if it reads otherwise (writing T1LH clears the flag); MS isn't touched. It clears a DMA channel's
  terminal count only if the channel's interrupt was on (a mask built as it turns MODE's bit 7 off), so channel 0's,
  which SDIO reads at its end, isn't lost when an interrupt comes in the middle; and it sets the 6551's receive
  interrupt off (ACIA_CMD bit 1, read-modify-write). `FT_DELAY` gives up, A = X = `$FF`, if the ms counter stands
  still for 4 frames (timer 1 stopped by a program), rather than wait for ever. Function 75's Apple II mode turns
  OODOS's two off first; Break and 75's monitor go through the ROM's QUIET, which turns off the VIA's and the video
  card's. Cold boot starts the clocks afresh.
- **Costs:** a tick, with no event area and no hook, is 145 cycles through the dispatcher (the IRQ's 7 included;
  144 before the review's `CMP VIAL`): 2.0% of the CPU, against the plan's 1.4% estimate. An open event area adds
  43 a tick. The stub costs 2 cycles more each way (`CLI`, `SEI`). Common memory grows by 527 bytes: **268 bytes
  are left** ($FEED to the vectors), **67 in the debug build**, a constraint for L2's handlers (DEAFEN and CLKOFF
  are already in block 62).
- **Tests** (emulator, fast preset): `test_oodos_clock` (13 cases, 6 s: the counters against the cycles at the
  prompt and through commands, 80's info and 72's 10 bytes, the event area's refusals and writes, wait and delay,
  the hooks with their windows in the BDOS and in the program, each pushing 16 bytes, and in the BDOS under 100
  sentinel bytes on the program's stack (they fail with the `CPX ST_S` branch taken out), the warm boot by `JMP
  WBOOT`, ^C and Break, full-budget hooks losing nothing, a 36 KB PIP from A: losing no tick, an interrupt OODOS
  doesn't answer turned off, 120 lines scrolled losing no tick (40 lost with the copy in one block), a stopped
  timer 1 ending `FT_DELAY` and set going again by the warm boot, and a machine without the VIA: the 6551's
  receive interrupt turned off and its key kept); `test_oodos_clock_disk2` (3 cases, 18 s, fixtures: a Disk II
  read and write caught up to 1.5%, and 16 writes of 1 KB to O: under a full-budget frame hook, each at another
  phase of the frame against the disk, all written); `test_oodos_clock_debug` (1 case, 0.6 s: the debug build's
  costs). Test program `oodos/sys/test/l1test.s`. `test_oodos_sys`'s stub and jump-table cycle checks leave out
  time spent in interrupt handling, from the interrupt's entry, 7 cycles before the dispatcher's first (the owner's
  decision): a tick held off by the stub's masked switch is taken at its `CLI`, inside the measured span.

- **L2: input.** The event queue, the keys-held map, controllers (question 12), the mouse, and the console's keys
  during a game (question 4).
- **L3: sound.** Claiming and releasing the card; `.sid` playback moved into OODOS; the stream service; the
  ES5503's claim and wave RAM loads; the speaker's beep; and the warm boot stopping all of it, as BIOS QUIET does.
  **Sound rev 2's last acceptance item lands here:** a test program streams 44.1 kHz 16-bit stereo from a file on
  M: through the stream service, with real reads, for some seconds of emulated time, and the card counts no
  underrun.
- **L4: graphics, the linked library** (`oodos/lib/`, with cc65 headers): modes, the palette, tiles and maps,
  sprites (the shadow table goes to the card by DMA at vertical blank, through L1's dispatcher), bitmaps, double
  buffering, and handing out video memory. With it come the asset format and tools (question 8), and load to memory
  by DMA (question 13), which is the asset loader.
- **L5: OO-Amp on OOLIB.** OO-Amp's agent replaces `oa_hal.s` with OOLIB, in its own repo. That's the
  real-program test of L1–L4, as GW-BASIC was meant to be. Our own gate is still the rig's tests.
- **L6: the music driver** for SOUND, PLAY and BEEP (question 5), just before GW-BASIC needs it. Nothing before
  then needs it.

**The books:** the Programmer's Guide's chapter 8 (phase G3) and each new function's entry, written step by step
as each part is decided; the Technical Reference's interrupts chapter says who owns the IRQ. **The version:**
question 15.

## Open Questions ##

**Recommended** answers are the plan's (2026-11-07). **Decided (2026-11-07, the owner): all fifteen as recommended.**

1. **Portability.** Do these services keep CP/M's rule that a program asks what exists (versioned services, a
   program that checks), or may OO-1 programs simply assume the hardware?
   **Recommended:** ask once. Function 72 also returns OOLIB's interface version and a bit per service area, and
   OOLIB's start-up checks them and says what's missing instead of crashing. The hardware itself (the cards,
   revision 2) is a program's own check, through the cards' IDs, as the BIOS does.
   *As built (L1, the owner's decision 2026-11-07): through function 80's `FT_INFO`, not 72, which stays 10 bytes.*
2. **Calls into OODOS.** New function numbers one per service (75 on, as 72–74), or one function with a service
   number and a parameter block (as 50 does for the BIOS)?
   **Recommended:** one function per area, at 80 on (80 frame and timing, 81 input, 82 sound, 83 DMA and
   loading, 84 video memory). Each takes an operation in A, and a parameter block at a word for more. That keeps
   FIDX small, leaves room in each area, and none of them is called in a hot loop: the event area is.
   *As built (L1, the owner's decision): A/X can't carry both, so X = 0 means A is the operation with no block,
   else A/X is a block with the operation at +0.*
3. **The hooks' rules** (frame, tick, scan line, timer 2). Which of the program's windows are mapped while one
   runs, how long each may take (a budget in cycles: the tick hook's must be far smaller than the frame hook's),
   whether it may call OOLIB, whether it gets a zero-page slice of its own, and the VIA rule (never ACR, IER or
   IFR).
   **Recommended:** a hook runs with the windows the program had when it set the hook (OODOS saves and puts
   back the interrupted ones), on the system's stack, with A, X and Y saved. The program's zero page is its own
   to share with its hooks. Budgets: 8,000 cycles for the frame hook (about 7% of a frame), 300 for the tick
   hook. They aren't enforced, but a debug build counts and reports them. OOLIB's register-only routines may be
   called; no BDOS call and no DMA; never ACR, IER or IFR.
   *As built (L1, the owner's decision): the hook's stack is page 1 of its own W0 (the stack page is in W0 with the
   zero page), under the program's and the BDOS's; the frame hook runs with interrupts on and a re-entry guard, the
   tick hook masked; no far call either.*
4. **Keys during a game.** May a game turn off the console's handling of ^C, ^S and ^P (CP/M 3's console mode,
   function 109, already has bits for this), and should the event queue then get those keys instead?
   **Recommended:** yes, through function 109, as OO-Amp does now. While the event area is open every key goes
   to the queue, ^C included, and Break still stops any program.
5. **The music format.** Our own tune format for the driver (notes, instruments, effects), or an existing tracker
   format (as long as it's free to use)? A converter from MIDI or from a tracker on Linux?
   **Recommended:** none before L6. Music in L1–L5 is `.sid` tunes on the 6510 and streams through the DAC.
   L6's driver takes note-level commands (note, length, voice, envelope); GW-BASIC's PLAY strings are parsed
   by BASIC itself. Trackers come later through the card's codec 2 (MOD), already reserved.
6. **What a program may own outright.** DMA channel 1 or 3 for a streaming effect, timer 2, the user port, the
   SIDs directly: which may a program claim for itself (and the warm boot take back), and which only through
   OODOS's calls or queue?
   **Recommended:** a program may claim timer 2, the user port, the SIDs (the 6510 stopped) and the ES5503
   outright, and the warm boot takes them back. DMA channels 0 and 1 stay OODOS's. Channel 2 is used only
   through the stream and wave-RAM services, so OODOS starts and stops it. Channel 3 is shared through
   OODOS's copy queue.
7. **A hardware timer.** *Answered (proposed 2026-10-30): a W65C22S on the serial card*
   ([design.md](design.md#the-via-and-the-joystick-ports)): timer 1 is OODOS's system tick, timer 2 and the ports are
   the program's, and programs may hook the tick (with question 3's rules). The rate is question 11.
8. **Asset formats and tools.** Which formats, and which Linux tools (a PNG to tiles converter, a palette tool, a
   map editor's export) come with OOLIB?
   **Recommended:** one small header (a magic, a type, a version, the sizes) for tiles, maps, palettes and
   sprites, starting from OO-Amp's `.GFX` file and its `mkgfx.py` once they're reviewed. A Python PNG-to-asset
   converter goes in `oodos/tools/`.
9. **Where OOLIB's code lives.** Linked into each program (as proposed), or also kept in an allocated block and
   reached by FARCALL for programs short of TPA space (GW-BASIC is)?
   **Recommended:** linked, in L4. A FARCALL copy waits for GW-BASIC, with its own measurements.
10. **The console during a game.** May a game use the text layer for its own status line while the console's
    output goes elsewhere, or does any console output put the console's text screen back?
    **Recommended:** a program that takes the screen keeps it. Console output goes on into the console's own
    text memory, unseen, and to the serial line, and the warm boot shows it again. A game may make its other
    layer text for a status line.
11. **The tick rate.** Fixed at 1 kHz (about 1.4% of the CPU before any hook), or settable by a program within
    limits (say 100 Hz to 4 kHz), which every other user of the millisecond counter then has to allow for?
    **Recommended:** fixed at 1 kHz, so a millisecond is always a tick. A program wanting finer timing uses
    timer 2.
12. **The controller model.** How many controllers the event area has; how the hardware is numbered by default (the
    two digital sticks as 1 and 2, the Apple joystick as 3?); whether a game or the user (a setting kept on the card)
    picks which is which; and the Apple joystick's thresholds for directions.
    **Recommended:** four controllers: 1 and 2 the DE-9 sticks, 3 the game port's joystick, 4 kept for a USB
    gamepad. A call lets a game swap them; a user setting on the card can come later. The Apple stick's
    thresholds are 64 and 192.
13. **Load to memory by DMA.** A BDOS function in OODOS's own range (as 50–59) or an OOLIB service (question 2);
    a whole file or an offset and a length; the storage channel (0) straight to the destination, or a buffer and
    channel 3 (question 6); and, for video memory, whether it waits for the vertical blank.
    **Recommended:** a function of OODOS's (83), since it is file I/O. It takes an open FCB, a first record, a
    count of records and a 20-bit destination, and it refuses OODOS's own memory, as 51 does. Whole 512-byte
    sectors on an SD unit go by channel 0 straight to the destination; the ends, and O: and P: (the CPU reads the
    Disk II), go through a buffer and channel 3. It doesn't wait for vertical blank: a program that cares loads
    into the hidden buffer and flips.
14. **Interrupts during a BDOS call.** *New (2026-11-07).* Today a call runs masked from start to finish, so a
    1 kHz tick loses every tick of a call longer than a millisecond, which is any disk read, and the frame
    counter loses frames. **Recommended:** the BDOS runs with interrupts enabled. The dispatcher lives in common
    memory and maps what it needs itself, whatever the call had mapped, and touches no BDOS state. The few
    critical sections mask briefly (the stub's window switch, the allocation vector). The Disk II driver must
    still mask while it reads or writes a sector (the CPU times the bits), and no clock on the machine counts
    frames or milliseconds while interrupts are masked: the video card has no frame count register, and the
    VIA's timer 1 flags only once however many periods pass. Its loops are cycle-counted, though, so the
    driver knows each sector's cost: it adds that to the millisecond counter as it unmasks, and a vertical
    blank missed in that time is lost (the frame counter counts frames seen). That happens only on O: and P:.
    *As built (L1, the owner's decision): masked a field at a time, from its D5, both reads (which the code never
    masked) and writes; the cost counted in disk bytes read. A field is shorter than a frame, so none is lost.*
15. **The version.** *New (2026-11-07).* **Recommended:** OODOS 0.5 when L1–L4 are merged, one bump as for
    phases 5 and 6, and OOLIB's interface version 1 from L1, raised by each step that adds to it.

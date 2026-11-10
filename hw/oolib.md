# OOLIB: the OO-1's game and device services #

**Status: draft (2026-10-30); the plan and the open questions decided 2026-11-07 ([The plan](#the-plan)); L1–L4, L4a and L4b merged 2026-11-08, OODOS 0.6; L5 merged 2026-11-09, OODOS 0.7.** This document proposes how OO-1 programs reach the
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
| Mouse | USB card `$C590`–`$C59A`: buttons, relative or absolute X/Y, wheel | Absolute mode suits a pointer sprite, within the sprites' 320×240 space: limits 319 and 239, not the 639 and 479 of /RESET (corrected, L4's plan) |
| Game port | USB card `$C5A0`–`$C5A5`: four paddles 0–255, three buttons | Apple-style joystick |
| Joysticks | Two digital DE-9 ports (Atari/Commodore pinout) on the serial card's W65C22S, `$C341` and `$C340` bits 0–4, 0 = pressed (proposed 2026-10-30) | One load reads a stick; no interrupt needed |
| Timing | Vertical blank (60 Hz), the scan-line interrupt, the DS3231's 1 Hz TICK (ID D), and the W65C22S's two timers at 7.16 MHz (proposed 2026-10-30) | Timer 1 is OODOS's system tick; timer 2 is the program's |
| DMA | Controller `$C400`, ID 8: four channels (design.md's: 0 storage, 1 serial, 2 USB / sound RAM, 3 general: copies and blits); memory to memory at 2 cycles a byte | Under OODOS (corrected, L4's plan): 0 the SD card (the BIOS's SDIO and function 83's loads), 1 the vertical-blank copies (L4; no serial card's DMA is modelled), 2 the sound card's (L3), 3 the BIOS's DMAGO (MOVE, the console, function 83's copies) |
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
is a ca65 library in this repo (`oodos/lib/`; no cc65 C headers until there is a C target, L4's question 5), linked
into each `.COM` that wants it. Its memory work reaches video memory through W1, which OOLIB maps itself under the
W1 contract (L4's question 1; corrected: a program's CPU sees banks 8–B only through a window). Each
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
- **Sprite collision:** the collision groups at the last vertical blank, and optionally a hook. *Corrected (L4's
  plan): the card keeps them itself, in STATUS bits 3–0 and ISR bit 2, so the library reads them there; no OODOS
  code, no copy in the event area, and no collision hook in L4.*

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
  OODOS moves at each vertical blank. *Corrected (L4's plan): sprites live in the tiles' 320×240 space, so a pointer
  needs absolute mode with limits 319 and 239 (`IN_MOUSE`); the screen's 639 and 479 would run it off the right and
  the bottom.*
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
  collision group; OODOS copies the table to the card by a 512-byte DMA at vertical blank (about 143 µs: memory to
  memory is 2 cycles a byte, 1,024 cycles; corrected from 72 µs, L4's plan), as design.md proposes.
- **Bitmaps:** plot, line, rectangle, fill, circle, a flood fill (PAINT), text in a font on a bitmap; copies and
  fills by DMA channel 3.
- **Double buffering:** draw into one buffer, flip the base register (latched at vertical blank).
- **Video memory:** banks 8–B are shared by the console (its text screen and font) and the program. OOLIB hands
  out pieces of them; the warm boot puts the console's back. *Corrected (L4's plan): the console's units 0–12
  (`$80000–$833FF`) are OODOS's, never a program's to write (function 83 refuses them, block 32 isn't in the W1 map),
  so the warm boot only sets the card's registers again; the program has units 13–255, which the library hands out.
  The CPU reaches them through W1 (the W1 contract), not by itself.*

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
- **EQ and the vis page:** register work with nothing to share, so OOLIB (linked), not OODOS calls. **Volume**
  became function 82's (2026-11-08, step L4b in [the plan](#the-plan)), so the warm boot can put it back.
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
  card's RAM. Which channels a program may own outright is part of open question 6. *Corrected (L4's plan): all
  four are OODOS's. 0 the SD card; 1 the vertical-blank copies (nothing else uses it: the serial card's DMA isn't
  built); 2 the sound card's, through function 82; 3 the BIOS's DMAGO, through which function 83's copies and
  fills run synchronously in the BDOS, 2 KB a block: there is no queue, and a program never drives a channel.*
- **Copy and fill** between any physical addresses, checked as 3c's segment is (none of OODOS's own memory).

### Files and assets ###

- **Load a file to physical memory** (tiles, pictures, a tune) by 3c's DMA segment: mostly built already, but only
  a record (128 bytes) a call: a program sets the segment (51) and loops read-sequential (20). *Corrected (L4's
  plan): up to 128 records a call with function 44's count, but each still through a BDOS buffer and MOVE, about
  0.45 s for 64 KB.*
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
  console's text screen and font. *Corrected (L4's plan): the card's registers as cold boot's CRTINIT sets them;
  the font, the palette and the text are never a program's to change (units 0–12), so they are as it found them,
  and the warm boot copies nothing.*
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
  nor reads its data as T1CL; DEAFEN turns its receive and transmit interrupts off, and the dispatcher goes on to
  the video card in the same pass (second review), so a 6551 left interrupting can't starve the frame counter.
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
  written only if it reads otherwise (writing T1LH clears the flag, so a program that changed the latch may cost
  MS a tick: documented, the owner's decision); MS isn't otherwise touched. It clears a DMA channel's
  terminal count only if the channel's interrupt was on (a mask built as it turns MODE's bit 7 off), so channel 0's,
  which SDIO reads at its end, isn't lost when an interrupt comes in the middle; and it sets the 6551's receive
  and transmit interrupts off (ACIA_CMD bit 1 set and bits 3-2 `%10`, read-modify-write, before its STATUS is
  read). `FT_DELAY` gives up, A = X = `$FF`, if the ms counter stands still for 4 frames (timer 1 stopped by a
  program), rather than wait for ever; without a running frame counter it waits on. DMAGO keeps SRCP and DSTP
  (it puts them back after the blocks). A debug build without the VIA counts no hook (`FT_COSTS` A = 0).
  Function 75's Apple II mode turns OODOS's two off first; Break and 75's monitor go through the ROM's QUIET,
  which turns off the VIA's and the video card's. Cold boot starts the clocks afresh.
- **Costs:** a tick, with no event area and no hook, is 145 cycles through the dispatcher (the IRQ's 7 included;
  144 before the review's `CMP VIAL`): 2.0% of the CPU, against the plan's 1.4% estimate. An open event area adds
  43 a tick. The stub costs 2 cycles more each way (`CLI`, `SEI`). Common memory grows by 532 bytes: **263 bytes
  are left** ($FEF2 to the vectors), **62 in the debug build**, a constraint for L2's handlers (DEAFEN and CLKOFF
  are already in block 62).
- **Tests** (emulator, fast preset): `test_oodos_clock` (14 cases, 6 s: the counters against the cycles at the
  prompt and through commands, 80's info and 72's 10 bytes, the event area's refusals and writes, wait and delay,
  the hooks with their windows in the BDOS and in the program, each pushing 16 bytes, and in the BDOS under 100
  sentinel bytes on the program's stack (they fail with the `CPX ST_S` branch taken out), the warm boot by `JMP
  WBOOT`, ^C and Break, full-budget hooks losing nothing, a 36 KB PIP from A: losing no tick, an interrupt OODOS
  doesn't answer turned off, 120 lines scrolled losing no tick (40 lost with the copy in one block), a stopped
  timer 1 ending `FT_DELAY` and set going again by the warm boot, ACR changed and put back by the warm boot with
  no tick lost (the latch left alone), and a machine without the VIA: the 6551's
  receive interrupt turned off and its key kept); `test_oodos_clock_disk2` (3 cases, 18 s, fixtures: a Disk II
  read and write caught up to 1.5%, and 16 writes of 1 KB to O: under a full-budget frame hook, each at another
  phase of the frame against the disk, every step's result checked and the last file read back as written);
  `test_oodos_clock_debug` (1 case, 0.6 s: the debug build's costs). Test program `oodos/sys/test/l1test.s`.
  `test_oodos_sys`'s stub and jump-table cycle checks leave out time spent in interrupt handling, from the
  interrupt's entry, 7 cycles before the dispatcher's first (the owner's decision): a tick held off by the stub's
  masked switch is taken at its `CLI`, inside the measured span.

- **L2: input.** The event queue, the keys-held map, controllers (question 12), the mouse, and the console's keys
  during a game (question 4).
**L2's plan (decided 2026-11-07, the owner; built, below).** The plan as below, and its five questions as
recommended: 8-byte events with a 32-bit millisecond stamp; the mouse's movement not queued; the handlers in block
62 behind a W3 switch; the sticks sampled every tick; OO-Amp's own reads of the USB FIFO documented as not sharing
it with a layout-2 area (L5 moves OO-Amp onto function 81). It rests on questions 4, 6 and 12 as decided, and on
L1 as built.

- **What there is.** The USB card (`$C580`): the cooked KEY, the console's, which L2 leaves alone, and a raw event
  FIFO of 128 bytes, 64 events of two bytes (the HID usage, then flags, bit 7 a release; the modifiers come as keys
  `$E0–$E7`; a full FIFO drops an event whole). The mouse (`$C590–$C59A`): buttons, relative or absolute X and Y
  with limits, the wheel; reading MX's low byte latches all three. The game port (`$C5A0–$C5A5`): three buttons,
  four paddles of 0–255 after PTRIG. USB_IEN and USB_ISR: KEY, MOUSE, GAME, DEVICE, on IRQH bit 3; USB_DEVICES says
  what is plugged in. The joysticks are on the VIA: port A stick 1, port B stick 2, bits 0–4, 0 pressed, with no
  interrupt. L1 left the tick at 145 cycles and 263 bytes of common memory free (62 in the debug build).
- **Where the code lives.** Only a few bytes join the dispatcher in common memory (a USB branch and a call from
  the tick); the handlers go in block 62's code window, reached by switching W3 as L1 does for DEAFEN and CLKOFF:
  about 20 cycles more an input interrupt, and no common memory spent on the bulk.
- **How input is taken.** USB events by the card's interrupt (IEN KEY, MOUSE, GAME, DEVICE), on only while an
  event area of layout 2 is open: the handler drains the FIFO, keeps the keys-held map and queues each event. The
  DE-9 sticks sampled at each tick (two loads, an event only when a bit changes), so a press shorter than a frame
  isn't missed; nothing is sampled while no layout-2 area is open, so the tick costs every other program what it
  did. The paddles: PTRIG at each vertical blank, read at a tick a few milliseconds later, and the controllers' X
  and Y and the paddle bytes updated then (60 Hz, the paddles' natural rate). The mouse: its position and buttons
  into the area at its interrupt, button and wheel changes queued, movement not (the position in the area is
  always current; a queue of moves would fill it).
- **The event area, layout 2.** Layout 1's 16 bytes unchanged; a program opens a larger area to get L2's part.
  +16 the queue's control, 8 bytes: the head (OODOS's), the tail (the program's), the number of slots, 1 reserved,
  the events lost to a full queue (2 bytes), 2 reserved. +24 the modifiers held (USB_MODS), the devices plugged in
  (USB_DEVICES), 6 reserved. +32 the mouse, 8 bytes: X and Y (2 bytes each), the buttons, the wheel's count (signed,
  kept until the program clears it), 2 reserved. +40 four controllers of 4 bytes: the directions (bits 0–3 up,
  down, left, right) and fire buttons (bits 4–7), X and Y (0–255; a digital stick gives 0, 128 or 255), and which
  hardware it is. +56 the four paddles, then 4 reserved. +64 the keys held, 32 bytes, a bit a HID usage. +96 32
  bytes reserved for L3's sound (L3's question 1). +128 the queue, 8 bytes an event, as many as the size leaves (a
  256-byte area holds 16). **An event:** its kind (key, mouse button, wheel, controller, device), the code (the
  HID usage, the button, the controller's number), the value (pressed or released, the new bits, the wheel's
  step), the modifiers then, and the millisecond counter's low 32 bits. Eight bytes keep the indexing a shift.
- **Function 81, input,** by L1's convention (X = 0: the operation in A; else A/X a block): info (what is plugged
  in, the layout L2 writes), flush the queue, the controller map (read it, swap two), the mouse's mode and limits,
  the keyboard's LEDs, its repeat (or repeat off), and the next event, for a program that would rather call than
  read the ring.
- **The console's keys.** The cooked KEY path is untouched, so CP/M programs see no change. With a layout-2 area
  open every raw key reaches the queue; whether ^C, ^S and ^P also act on the console is function 109's business
  (question 4). A program that reads both gets each key twice (cooked and raw) and uses one or the other.
- **The warm boot** turns USB_IEN off, empties the queue, and puts the mouse's mode and limits, the LEDs and the
  repeat back as OODOS set them at cold boot; closing the area (L1's) stops the sampling.
- **Not in L2:** the pointer sprite (L4, with the sprites); USB gamepads (controller 4 reads as nothing plugged
  in); the USB FIFO by DMA (the CPU drains 64 events in well under a frame); keyboard layouts other than US (the
  card's cooked KEY is the card's business).
- **The version:** OOLIB's interface version 2 (function 80's info); OODOS stays 0.4 (question 15).
- **Tests:** a layout-2 area filled from typed keys (the rig's keyboard); a press and release inside one frame;
  the queue full (the events lost counted, none torn); keys held; the modifiers; the mouse in relative and
  absolute mode; the wheel; the DE-9 sticks (a change inside a tick's gap; both ports); the paddles and the game
  port's buttons; the controller swap; a layout-1 program unchanged (a 16-byte area, no USB interrupt enabled);
  the tick's cost with no layout-2 area, which must be L1's; ^C reaching the queue with function 109's mode set;
  the warm boot (WBOOT, ^C, Break) leaving USB_IEN off and the mouse, the LEDs and the repeat as cold boot set
  them. Each suite's fast time stated, the sanitizer tier's TIMEOUT checked.

**L2, as built (2026-11-08)** (`oodos/sys/input.s`, new: the handlers in block 62 and function 81; `irq.s`'s
dispatcher, tick, `FT_OPEN` and `FT_INFO`; `common.s`'s warm boot; `oodos/include/oodos.inc`'s `O_INPUT`, `IN_*`,
`EV_*`, `EQ_*`, `EK_*`, `EC_*`, `CB_*`, `CH_*`, `ED_*`; bit names in `backplane.inc`, the ROM byte-identical).
OODOS stays 0.4; OOLIB's interface version is 2, `FT_INFO`'s areas `$03`, its +4 layout 2. Where the plan left
something open it was decided as below, each with its reason; the first two are for the owner to confirm.

- **A 256-byte area is size 0.** `FT_OPEN`'s size is a byte, so the plan's 256-byte area can't be given as 256: 0,
  which L1 refused, now means 256 (and `EV_SIZE` reads 0). No other size changes meaning.
- **The ring keeps a slot empty,** so head = tail is empty with no count that both sides write: a 256-byte area has
  16 slots and holds 15 events waiting, not 16. An event that would fill the last slot is lost whole, and counted.
- **The layout by the size:** 144 bytes (`EV_LEN2`: the fixed part to +127 and two slots) or more is layout 2,
  its slots (size − 128) / 8; less is layout 1, exactly as L1 made it (no USB interrupt, nothing past +16).
- **Common memory:** 51 bytes, **212 left (11 in the debug build)**. In it: `CALL62` (L1's `DEAF62` made general:
  W3 on block 62, `R62`'s routine X, W3 back), the dispatcher's other-cards path calling `IRQOTH` there instead of
  `DEAFEN`, and the tick's layout-2 checks inline: the two ports against `STKA` and `STKB`, the frame counter
  against `PADFRM`, the ms counter past `PADDUE` (MS − PADDUE ≥ 0, signed: so an MS that jumps on, the Disk
  II's catch-up, still reads the paddles that frame), and `L2TICK` through `CALL62` only when one differs. The
  plan's PTRIG "at each vertical blank" is at the frame's first tick instead (up to 1 ms later): in the video
  handler it would have cost 13 bytes the debug build hasn't got. The paddles are read 4 ms after it (`PAD_MS`;
  255 counts are 2.8 ms), once for each PTRIG (`PADARM`): **they need the video card's frames**, and without them
  nothing is triggered or read. Block 62 has 1,402 bytes of `input.s`, 1,234 left; function 81 is 294 bytes of
  the BDOS's block, 2,077 left.
- **Costs** (`test_oodos_input`, the dispatcher from the interrupt's first cycle to its RTI's last): a tick with no
  area **145 cycles, L1's to the cycle** (159 at MS's carry, as L1's), every program's; a layout-1 area's 184, 4
  fewer than L1's 188 (W1 kept in X, not on the stack); a layout-2 area's 234 (1.2% of the CPU more than none),
  and per frame one tick of 366 (PTRIG) and one of 673 (the paddles read, controller 3 written). The USB card's
  interrupt for a key is 651 cycles; a burst is taken 16 events an interrupt at most (`UBATCH`: a pass of 16
  measures 3,000–3,400 cycles, about 5,000 when a tick shares it, under a tick's 7,159; the test holds every pass
  under 7,159): KEY's flag is cleared only when the FIFO is seen empty, and looked at again after, so
  while events wait /IRQ stays low and the next interrupt takes the rest, the tick due first.
- **The event's fields:** a key's and a button's value 1 pressed, 0 let go; a controller's its new bits; the
  wheel's the steps that interrupt latched (signed), its code 0; a device's the new DEVICES, code 0. The modifiers
  are kept from the events themselves (`$E0–$E7`), from USB_MODS at the open, so each event carries them as they
  were with it, a modifier's own included. Keys held at the open aren't in the map until let go and pressed again.
- **The mouse in relative mode:** X and Y are the movement summed since the open or `IN_MOUSE`, 16 bits going
  round, which OODOS never clears (a program clearing them would race the interrupt); the wheel is summed and
  the program's to clear, as planned. MX's low byte is read once an interrupt: a second read latches again and
  loses the wheel's steps and relative mode's sums.
- **The controllers:** below 64 is up or left, 192 or more down or right (question 12's thresholds); a DE-9 has
  one fire button (bit 4), the game port three (bits 4–6); controller 4 (`CH_GAMEPAD`) reads 0, X and Y 128.
  Each piece of hardware keeps its own state; the map (`CMAP`) is only which controller shows it, so `IN_SWAP`
  rewrites both controllers at once. The warm boot puts the map back to 1–4.
- **Function 81:** `IN_INFO` (A the devices, X the layout), `IN_FLUSH` (the tail to the head, and the lost count
  0), `IN_MAP`, `IN_SWAP`, `IN_MOUSE`, `IN_LEDS`, `IN_REPEAT`, `IN_NEXT` (A the kind, 0 none; refused with no
  layout-2 area). **Repeat off isn't there:** the card has no off, its slowest is `$3F` (1 s, then two a second);
  raw events never repeat, so a game reading the queue has nothing to turn off. `IN_MOUSE`, `IN_LEDS` and
  `IN_REPEAT` are refused with no USB card.
- **The warm boot** (`WBQUIET`, block 62, where `WARM` called `DEAFEN`): DEAFEN (USB_IEN off), then the USB card's
  MCTRL relative, MAXX and MAXY 639 and 479, LEDS 0, REPEAT `$10` and KBCFG `$80` (Pause on /NMI, should a
  program have changed it: not in the plan, but Break must keep working), and the controller map. The cold boot's
  first warm boot sets the same, so they are what cold boot set. `CLKOFF` (Apple II mode) turns USB_IEN off too.
  `FT_OPEN` turns it off before any new area, `FT_CLOSE` with the area.
- **The VIA's port B:** OODOS reads IORB at every tick with a layout-2 area open, which clears CB1's and CB2's
  flags: documented (Technical Reference, chapter 9), as OODOS owns IFR anyway. Port A is read by its
  no-handshake register. **Without the VIA** there is no tick: no sticks, and the paddles aren't read either
  (their PTRIG rides the tick); the USB card's events go on.
- **Function 81's results are read before the mask is given back** (review, 2026-11-08): QTAKE's result was in
  `UTMP`, which the handlers use as scratch, and was read after the PLP, so an interrupt there could lose an event
  or make an old `SNAP` one. `AREAOUT` now loads it into Y while masked. Only $E0–$E7 are modifiers; $E8–$FF are
  keys. `FT_OPEN` and `FT_CLOSE` turn USB_IEN off before EVON. `UBATCH` is at most 128 (its count runs down to `BMI`); `PAD_MS` 1–127 (the due test is signed).
  `FT_OPEN` and `FT_CLOSE` clear USB_IEN and EVON masked, as a pair (second review). Without frames the paddles are
  read once, at the open.
- **OO-Amp's own FIFO reads** (question 5): documented, chapter 8 and the Technical Reference's chapter 8.
- **Tests** (emulator, fast preset): `test_oodos_input` (15 cases, 2.6 s: a 256-byte area from typed keys, each
  event's fields and times, `IN_NEXT`, a press and release 3 ms apart, the modifiers, `IN_FLUSH`; a 4-slot queue
  full, 7 lost and 3 kept whole; the FIFO full (64 events) taken in 4 passes, none left and no tick lost (with no batch, one pass
  of 13,125 cycles, and a tick lost); $E8 a key, not a modifier; an interrupt at `AREAOUT`'s PLP changing neither
  an empty queue's "none" nor a waiting event; the paddles read in a frame whose MS jumped past their time; the mouse relative, absolute with limits and back, the wheel, a button; both DE-9 ports, a press of
  1.2 ms seen; the paddles at 63/64 and 191/192 and the game port's buttons; `IN_SWAP` and the warm boot's map; a
  16-byte area left as L1 left it, no USB interrupt; the tick's cost, 145 with no area; ^C typed reaching the
  queue with console mode `$000A`, the program going on; the warm boot by `JMP WBOOT`, ^C and Break putting the
  input back; `IN_INFO`, `FT_INFO` and the refusals). Test program `oodos/sys/test/l2test.s`.
  `test_oodos_clock`'s `FT_INFO` expectations follow the version: `02 03`, `02030302`.

- **L3: sound.** Claiming and releasing the card; `.sid` tunes; the stream service; the ES5503's claim and wave
  RAM loads; the speaker's beep; and the warm boot stopping all of it, as BIOS QUIET does. **Sound rev 2's last
  acceptance item lands here.**
**L3's plan (decided 2026-11-07, the owner; built, below).** The plan as below, and its five questions as
recommended: L2's layout amended (+96 to +127 for sound, the queue from +128); `.sid` in OOLIB with PLAYSID.COM; a
busy channel 2 refuses a load with "busy"; the beep blocking and cycle-counted with interrupts on; the 6510's
mailbox bytes into the event queue. It rests on questions 5, 6 and 9 as decided, on L1, and on L2 (the event
queue, block 62 as the handlers' home).

- **What there is.** The sound card (`$C500`, revision 2, ID `$A2`): two SIDs and a 6510 with its own 64 KB,
  loaded through RAMADDR and RAMDATA; a one-byte mailbox each way; the stream (PCMCTL, CODEC, RATE, PCMFMT, FREE,
  PCMDATA, STATE, POS, KBPS), the EQ, the volume and the vis page, and the ES5503 (DOCCTL, DOCDATA, 128 KB of wave
  RAM). DRQSEL picks what the card's DMA request feeds: 0 RAMDATA, 1 PCMDATA, 2 DOCDATA; with 1 the request is
  held only while the FIFO has room, so a demand-mode transfer longer than the FIFO stops by itself and goes on as
  the card plays. Its interrupts (SOUND_IEN): a mailbox byte waiting and (revision 2) the ES5503's, on IRQH bit 2;
  none for the FIFO running low. DMA channel 2 is the card's; a channel with MODE's IRQ set interrupts at terminal
  count (IRQH bit 0), and AUTO reloads it. The BIOS's splash loads its tune through RAMDATA by the CPU; QUIET (on
  Break) stops the 6510, silences the SIDs, flushes the stream and stops the ES5503. `.sid` today is the
  emulator's (`sid_file.cpp`: the PSID and RSID headers, a free page for a stub, CIA 1's timer or the card's
  frame interrupt); nothing of it is 6502 code. Common memory is short (L1), so L3's handlers go in block 62
  behind a W3 switch, as L2's.
- **Function 82, sound,** by L1's convention. **Info:** the card's revision, its codecs, whether the ES5503 is
  there. **Claim and release,** a part at a time: the 6510 and the SIDs, the stream, the ES5503 (question 6); a
  part another program or an RSX holds is refused, and the warm boot releases what the program held. **Load the
  card's RAM** from physical memory (an address and a length, checked as function 51's segment is) by channel 2
  with DRQSEL 0; **start the 6510** at an address; **stop** it; **send a mailbox byte.** **The stream:** start
  (the codec, the rate, the format), give a buffer (a physical address and a length), pause, resume, seek (FLUSH),
  end (EOF), status. OODOS keeps a queue of two buffers: it starts the first by channel 2 (demand mode, DRQSEL 1,
  MODE's IRQ set) and, at its terminal count, starts the next from the interrupt and marks the first free; the
  program refills a free buffer when it likes, by ordinary BDOS reads, and the card plays on meanwhile. **Load the
  ES5503's wave RAM** by channel 2 with DRQSEL 2, and an **ES5503 hook,** a routine of the program's called at the
  chip's interrupt (an oscillator done), by the frame hook's rules. **Beep:** a pitch and a length, on the USB
  card's speaker, blocking and cycle-counted with interrupts left on (the tick adds a little jitter; nothing is
  lost). Channel 2 has one user at a time: a RAM or wave-RAM load while a stream's buffer is moving is refused,
  "busy".
- **The event area's sound part** (+96 to +127, read with a load): the stream's state, FIFO free, its position in
  frames, its bitrate, the two buffers' free flags, and the underruns counted. Mailbox bytes from the 6510 go into
  L2's queue as a sound event, so a program's own 6510 driver reports back without polling.
- **`.sid` in OOLIB, not OODOS.** Parsing the header, choosing the stub's page and building the stub is file work
  with nothing shared, so it goes in the linked library (question 9), ported from `sid_file.cpp`'s rules, calling
  function 82 to claim the 6510 and the SIDs, load the RAM and start. **PLAYSID.COM** plays a `.sid` from `A>` on
  it, and its test exercises the library. PSID only, as the emulator's player; RSID stays in the backlog.
- **EQ, volume and the vis page:** OOLIB routines (register work). The vis page is read with the stream claimed.
- **The warm boot** does what QUIET does for revision 2 (the 6510 stopped, the SIDs silenced, FLUSH, the ES5503's
  RUN cleared), aborts channel 2, puts DRQSEL back to 0 and SOUND_IEN to 0, and releases the claims.
- **The acceptance test** (sound rev 2's last item): a test program streams 44.1 kHz 16-bit stereo raw PCM from a
  file on an SD drive through function 82, refilling its two buffers with ordinary BDOS reads, for several
  seconds of emulated time; the card's underrun count stays 0 and the bytes at the card's tap match the file's. A
  second case does the same for an MP3 (the repo's own MP3 fixtures, `test/host/mp3`).
- **The version:** interface version 3 from L3; OODOS stays 0.4 (question 15).
- **Not in L3:** the music driver for SOUND, PLAY and BEEP (L6), RSID, MOD (codec 2), a `.sid` and a stream
  together (the card allows it; not tested here).
- **Tests:** the claims (refused while held, released by WBOOT, ^C and Break); a card-RAM load by DMA checked
  byte for byte; the 6510 started and stopped; mailbox bytes both ways, the 6510's arriving in the queue; the
  stream started, two buffers chained across terminal counts with no gap, paused, sought, ended, its states and
  the area's fields; the 44.1 kHz and MP3 acceptance cases; wave RAM loaded and the ES5503 hook called; PLAYSID on
  PSID files the tests build themselves, as `test_sid_file.cpp` does, never the owner's collection; the beep's
  length; a layout-1 or layout-2 program unaffected. Each suite's fast time stated, and the sanitizer TIMEOUT
  checked (the acceptance case plays the fewest seconds that prove the chaining).

**L3, as built (2026-11-08)** (`oodos/sys/sound.s`, new: function 82 in block 60, the handlers in block 61;
`irq.s`'s `CALL61`, the frame's call, DEAFEN, cold boot's; `input.s`'s IRQOTH branch and WBQUIET's call;
`oodos/include/oodos.inc`'s `O_SOUND`, `SN_*`, `SC_*`, `EV_S*`, `EK_SOUND`; `oodos/lib/sid.s` and
`oodos/include/oolib.inc`, new: the linked library; `oodos/utils/playsid.s`, new; bit names in `backplane.inc`, the
ROM byte-identical). OODOS stays 0.4; OOLIB's interface version is 3, `FT_INFO`'s areas `$07`. The owner's memory
decision of 2026-11-08 (from L3's sizing study) is followed: (a) the handlers in block 61, (b) first, (d1) kept for
L4. Where the plan left something open it was decided as below, each with its reason.

- **(b), the debug build's counters out of common memory** (the first commit): T1NOW, ADDTK, HOOKCOST and
  CALLHOOK's start-of-hook part (now HKON) moved to block 62 behind `CALL62` (`R62_HKON`, `R62_HKCOST`), WARM's
  STZs of FHMAX and THMAX into WBQUIET. The normal OODOS.SYS is byte-identical (cmp); the debug build's common memory
  went from 11 bytes free to 202. The crossing adds 58 cycles to each measured hook (the tick hook 431 on main, 489
  after; the frame hook 9,936 to 10,147), not the 35 the sizing guessed, which broke `test_oodos_clock_debug`'s
  tick bound (380-460); an inline W3 switch in place of `CALL62` was measured too, 469, still over. The owner
  widened the bound to 380-520 (fix 1, 2026-11-08); the frame bound is as it was.
- **Where it lives** (decision (a)): block 61 has the handlers, reached from the dispatcher by `CALL61` (common
  memory: W2 saved, block 61 mapped, the routine, W2 put back byte for byte, as `CALL62` does W3; W1 is never code,
  being the event area's): channel 2's terminal count (the next piece, the next buffer), the mailbox into the queue,
  the ES5503's acknowledge and hook, the area's sound part, and the warm and cold boots' parts. Function 82 is in
  block 60, the BDOS's, since PGET and PPUT remap W2; it calls block 61's helpers directly (W2 is block 61 there
  between PGETs). Block 62 has only IRQOTH's branch (`SIEN` or `C2ON`: `CALL61` to `SNDIRQ`, which returns the IRQH
  bits it answered, so they aren't strays), WBQUIET's and cold boot's calls, DEAFEN's care below and CLKOFF's IEN.
  `CALL61` sits after the tick's path in common memory: placed before it, it moved `TICK` so a branch crossed a page,
  and the tick cost 146.
- **DEAFEN and L3** (the sizing's first design point): DEAFEN writes the card's IEN as `SIEN`, L3's own, rather than
  0, and skips channel 2 while a stream buffer moves on it (`C2ON`), its MODE and its terminal count left alone, so
  a stray elsewhere can't stop the stream or lose the count that starts its next piece. The plan's "re-enable them
  as L2 does USB" would have left a gap: a piece that finished at once could have its count cleared by DEAFEN before
  the re-enable.
- **Pieces of 4 KB** (not in the plan): with room in the FIFO the card holds DRQ and channel 2 holds the CPU a cycle
  a byte; a whole 64 KB buffer would hold it 9 ms, over a tick (the VIA has one flag: ticks lost). So a buffer goes
  to the card 4,096 bytes a piece, each terminal count starting the next, as DMAGO's copies are 4,096 cycles. The
  loads (`SN_LOAD`, `SN_WAVE`) go the same way, synchronously in the BDOS, each piece waited for with interrupts on
  between: a 64 KB load keeps the millisecond counter to the cycle count (17 counted, 17.7 in the cycles).
- **The buffers are taken in turn** (0, 1, 0, 1 from `SN_STREAM` or `SN_SEEK`), not the first free: with room in the
  FIFO a buffer is sent whole before the program gives the next, and "first free" then gave it the same slot again,
  so a program alternating two buffers of its own couldn't tell which was free. `SN_BUFFER` is refused `SN_BUSY` while
  the buffer whose turn it is is still in use.
- **"Busy"** (the plan's question 3) is A = `$FE` (`SN_BUSY`), X = `$FF`, to tell it from A = X = `$FF`, refused.
- **The event area's sound part** (decided: +96 to +127): +96 the state, +97 the buffers free (bit a buffer), +98-99
  the FIFO's room, +100-103 the frames, +104-105 the kbit/s, +106-107 the underruns, +108-127 reserved. Written only
  while the stream is claimed and a layout-2 area is open, once a frame and at each buffer's end (not at each piece:
  a buffer's free bit changes only then), so a layout-2 program that claims nothing sees the zeros L2 left. The card
  counts no underruns a program can read: OODOS counts STATE going to 4 when it sees it (a frame, a buffer's end,
  `SN_STATUS`), so a dry spell shorter than a frame between two looks isn't counted.
- **The mailbox** (decided: into the queue): IEN bit 2 only while the 6510 is claimed and a layout-2 area is open, set
  and cleared with the claim and with `FT_OPEN` and `FT_CLOSE` (masked with EVON's change), so a program without an
  area polls MBIN as before and loses nothing to OODOS. `SN_CLAIM` of the 6510 empties MBIN first. An event is
  `EK_SOUND` (6), code 0, the value the byte.
- **The ES5503's hook** (the sizing's second design point, DOCADDR): the handler saves DOCCTL, DOCADDR (both bytes)
  and DOCBANK, reads `$E0` with the registers selected, calls the hook with the port so, and puts all three back after
  the hook returns (review F2); during `SN_WAVE` the ES5503's bit is out of `SIEN` itself, so the handler never runs
  under the transfer, a stray's DEAFEN included (F3). Whether the chip is pending is told by `$E0`'s bit 7, not by
  STATUS's IRQ, which a mailbox byte sets too (F4). The hook runs masked, as the tick hook does, in the tick hook's record (masked
  hooks never nest: HT's record set aside, the ES5503 hook's copied in, `CALLHOOK` for HT, HT's put back whole), so
  common memory gained nothing for it. Its A can't carry the interrupt register (the hook's W0 is mapped before its
  registers are free), so `SN_DOCHOOK`'s +3 names a byte of its zero page that OODOS writes first. Not in the plan.
- **The beep:** the pitch in Hz, 20-20,000, the length in ms by the millisecond counter (refused without the VIA, or
  without the USB card); the delay loop in block 61, aligned so its branches never cross a page (a crossing cost a
  fifth of the pitch). 1,000 Hz for 100 ms toggles the speaker 193 times, the ticks lowering it about 2%.
- **The warm boot** (`SNDQUIET`, before DEAFEN): QUIET's work for the card, channel 2 aborted, MODE 0 and its count
  cleared, DRQSEL 0, IEN 0, the hook off, every claim released: at every warm boot, whether anything was claimed or
  not, so a program that used the card behind OODOS's back (OO-Amp's `oa_hal.s`, until L5) leaves nothing playing.
  Break goes through the ROM's QUIET, which aborts every channel; booting again starts afresh.
- **`.sid` in OOLIB** (decided): `lib/sid.s`, assembled into `build/oolib.lib` (`oodos/Makefile`'s `OOLIB_USERS`
  link with it, through the rules gate), ported from `sid_file.cpp`'s rules (the meaning: no code copied): the
  header's checks, the free page chosen in the same order and by the same clashes, the stub the same bytes, the
  KERNAL's three exits and the vectors, CTRL's NTSC, `$D500` and BOTH; then claimed, stopped, loaded by `SN_LOAD`,
  started by `SN_START`. The chips' model (6581 or 8580) is a jumper on the card, not a register, so the library
  can't set it, as the plan's "as the emulator's player" can't. PSID only. PLAYSID reads the file whole into the TPA
  (the last record's byte count, which open gives for CR `$FF`, taken off), plays until a key, and stops.
- **Memory** (bytes free after L3 and its review, normal build, then debug): common memory 176 (166), down 36
  (`CALL61` and the frame's call); the system block's SYSBSS 132 (129); block 60 569 (553): function 82 is about
  1,500 bytes, so the BDOS block is now the tight one (the owner, 2026-11-08: its room waits for L4, when the loads
  and the beep could move to block 61); block 61 6,204; block 62's code window 1,188 (985); zero page 2. PLAYSID is
  7,281 bytes, the library about 2,650 of them (and 359 of BSS).
- **Costs** (`test_oodos_sound`, from the interrupt's first cycle to its RTI's last): a tick with no area **145, L1's
  to the cycle**, a layout-1 area's 184, a layout-2 area's 234, with or without the stream: L3 adds nothing a tick.
  A terminal count's handler is about 530-700 cycles (688 the most common), about 860 at a buffer's end (the area
  written; `CALL61` keeping A, review F1, added 4 to each), the next
  piece's 4,096 cycles of DMA inside it when the FIFO has room: 43 pieces a second at 44.1 kHz 16-bit stereo, under
  0.5% of the CPU. A frame with the stream claimed and a layout-2 area open is 688 cycles against 389 without: the
  sound part about 300 (692 since F1's 4). The USB card's key interrupt is 662 cycles, 11 more than L2's (IRQOTH's check). The mailbox
  and ES5503 interrupts weren't timed.
- **Tests** (emulator, fast preset): `test_oodos_sound` (15 cases since the review, 2.8 s: the claims and refusals; `SN_SEND` busy; loads to the RAMs' ends; 64 KB of card RAM
  by DMA byte for byte with no tick lost; the 6510 started, its mailbox both ways into the queue, stopped; busy while
  a buffer moves; six buffers of 10,000 bytes chained across terminal counts, the tap what was sent; pause, resume,
  seek, an underrun counted once, the end; 6,000 bytes of wave RAM across its first 64 KB and the ES5503 hook with
  DOCCTL and DOCADDR put back; the beep; the warm boot by `JMP WBOOT`, ^C and Break; a layout-1 or layout-2 program
  that claims nothing unchanged; and the acceptance: 1.3 s of 44.1 kHz 16-bit stereo raw PCM from B:, 0 underruns,
  the tap the file's bytes; the repo's `cbr320_stereo_48k.mp3` from B:, the tap minimp3's decoding of it, 0
  underruns); `test_oodos_playsid` (6 cases since the review, 2.2 s, song 256 of 256 among them: tunes the test builds, never the owner's, each played by
  PLAYSID and its card RAM and CTRL checked against `plan_sid` for the same bytes: a frame song, a CIA-timed NTSC one
  on the header's free page, version 1 with its load address in its data and no play, a tune over `$0314` with SID
  B at `$D500`, one over `$F000-$FFEF`; and the refusals). Test programs `oodos/sys/test/l3test.s` and `l3strm.s`.
  The clock and input suites' `FT_INFO` expectations follow the version: `03 07`, `03070302`.

- **The review** (2026-11-08; the owner: fix F1-F6 and F8-F10, note F7):
  - F1: `CALL61` returned the saved W2 in A, not `SNDIRQ`'s answer, so IRQOTH's strays were judged by it: in the BDOS
    (W2 `$3D`) a real stray on IRQH bits 0 and 2-5 was hidden and the dispatcher took it again and again (USB_IEN
    left on with no area, while a stream moved, held a function 10 call); in a program a terminal count still
    pulling (the next piece run at once) was taken for a stray, DEAFEN running for each. `CALL61` keeps A now (TAX,
    TXA: X is free in each caller). Must-fail tests, both failing before: `a_stray_in_a_bdos_call_is_turned_off_
    while_a_stream_moves` (USB_IEN stayed `$0F`) and `chained_terminal_counts_in_a_program_are_not_strays` (DEAFEN
    ran 12 times for 13 pieces after a FIFO flush under the program; 0 now).
  - F2: the hook ran after DOCCTL and DOCADDR were put back, and DOCBANK wasn't kept, so its writes went into what it
    interrupted (the must-fail `a_hook_s_register_write_leaves_the_interrupted_docdata_run_whole`: the hook's `$77`
    landed in wave RAM at `$40`; now the run is whole and register `$40` is `$77`).
  - F3: the wave load held bit 3 off at the card only, and a stray's DEAFEN wrote `SIEN` back mid-load (the test,
    failing with the fix taken out: bit 3 on in 635 of the load's 836 steps; 0 now). F4: above.
  - F5: the beep reads MS whole, masked (`BEEP_LOOP` 12: 194 toggles for 1,000 Hz, 100 ms). F6: `SN_SEND` is busy
    while STATUS says the 6510 hasn't taken the last byte. F8: a load past the end of the card's RAM or wave RAM is
    refused, not wrapped. F9: song 256 of 256 plays, its start song too, and prints as 256 (`SID_PLAN` takes A/X).
    F10: a refused `SN_DOCHOOK` leaves no hook, documented. F7, noted in the Technical Reference: the handler's
    DOCCTL write clears a waiting STATE `$82`.
  - The second review (2026-11-08; the owner: all seven as recommended): with the ES5503's interrupt routed to the
    6510 (DOCCTL's IRQ_CARD) `SNDIRQ` no longer reads `$E0`, which on a mailbox byte's interrupt took the 6510's
    oscillator and called the hook (the test, failing before: the chip's interrupt gone, the hook called once);
    MBOUT can't be emptied from the host (only /RESET does), so a byte left there is documented for a new 6510
    driver to drop; `SN_WAVE` refuses a +6 past 1 (`$20000` up aliased into the RAM); `SN_DOCHOOK` takes the ES5503's
    bit out of `SIEN` and IEN, masked, with the old hook, and `SIENSET` puts it back with the new, so an interrupt
    in between waits in the chip rather than being taken with no hook (the other choice, masking across PGET and
    SETHOOK, would have held interrupts off longer); song 256's test CIA-timed (speed's bit 31); the hook's DOCCTL
    write undone at its return, and `SN_BUSY`'s cases, documented.
  - The owner's answers to L3's questions: block 60's room waits for L4; the ES5503 hook as built (masked, the tick
    hook's record, the register in a zero-page byte the program names); the SID model a jumper, documented.

- **L4: graphics, the linked library** (`oodos/lib/`): modes, the palette, tiles and maps, sprites (the shadow
  table goes to the card by DMA at vertical blank, through L1's dispatcher), bitmaps, double buffering, and handing
  out video memory. With it come the asset format and tools (question 8), and load to memory by DMA (question 13),
  which is the asset loader.
**L4's plan (decided 2026-11-08, the owner; built, below).** The plan as below, and its six questions: 1 against the
recommendation, the owner's (OOLIB writes `NB_WIN1` itself under the W1 contract; no `DM_MAP`, and function 83 has
seven operations), 2-6 as recommended (the console's units 0-12 OODOS's; vertical-blank copies on channel 1, four
slots of 1 KB in all, started by `EV_VBGO`; L3's load engine and the beep's body moved to block 61 first, (d1) kept
in reserve; no cc65 C headers; asset format 3). Graphics, assets, and load to memory by DMA: function 83 in
OODOS for what needs a DMA channel, the vertical blank or physical memory, and the linked library for the drawing.
It rests on questions 8, 9, 13 and 15 as decided, on L1 (the dispatcher, the event area, the hooks, `CALL61`), L2
(the mouse) and L3 (block 61 as the handlers' home, 4 KB pieces). Measured on main 0497570 (`make -C oodos/sys
sizes`, the maps, normal and `DEBUG=1`).

- **What there is.**
  - **The video card** (`$C480`, ID `$91`; Technical Reference chapter 6; `video_card.h`, `video_card.cpp`): 640×480
    at 60 Hz, two layers each of text 80×30 or 40×30, tiles (320×240, 8×8 or 16×16, 4 or 8 bits, maps 32 or 64 a
    side), bitmap 256 (256×240, 8 bits, 60 KB), bitmap 512 (512×480, 4 bits, 120 KB) or mono (640×480, 37.5 KB);
    64 hardware sprites of 8 bytes (512-byte table at SPRBASE, VERA's layout), 8 to 64 pixels a side, 32 a line,
    four collision groups (STATUS bits 3–0, ISR bit 2); a palette of 256 12-bit entries in video memory, read
    afresh for every line (`video_card.cpp:323`); PALBASE, SPRBASE, MAPBASE, TILEBASE and the scrolls latched at
    line 480; interrupts at vertical blank, a chosen line and a collision. Vertical blank is lines 480–524, 1.43 ms,
    about 10,240 cycles. **No hardware blitter, no frame counter, no line-buffer DMA:** the card draws from memory
    and is told where things are. Video memory is banks 8–B, physical `$80000–$BFFFF`, RAM blocks 32–47
    (`video_card.h:131`), in 1 KB units for the base registers; the card snoops writes, so the CPU and the DMA
    controller see ordinary RAM.
  - **The console's part of it** (`crt.s`'s CRTINIT, cold boot only): the palette at unit 0, the font at units
    1–4 (copied from flash block 30), the text screen at units 8–12 (4,800 bytes). **The warm boot doesn't put the
    card back today:** only cold boot and Break (the ROM's VSCREEN) set its registers, so a program that changes the
    screen leaves it changed. The dispatcher's VIDSRV (`irq.s:374`, common memory) answers vertical blank alone and
    turns off any other enabled source; it never clears ISR bit 2 unless it was enabled.
  - **DMA** (Technical Reference chapter 5; `dma_controller.cpp:109–110`): memory to memory 2 cycles a byte on any
    channel, a fill by HOLD; card transfers fly-by, a cycle a byte. Channel 0 the SD card (`bios.s`'s SDIO, one
    sector a command, demand mode); channel 2 the sound card's (L3); channel 3 the BIOS's DMAGO, 2 KB blocks with a
    pending interrupt taken between them (L1's review); **channel 1 is used by nothing** (design.md's DMASEL1 gives
    it to the serial card's UARTs, none modelled; question 6 keeps it OODOS's).
  - **How a file reaches memory today:** function 51's segment and 33 or 20, up to 128 records a call with
    function 44's count. Each SD sector is read into a BDOS buffer by channel 0, then each record copied out by
    channel 3 (MOVE): no CPU copy, but about 4,300–4,600 cycles of BDOS work a record and 12,649 for one whose
    sector it reads (hw/oodos.md, 3b1 and 3b2's measurements on `oodos-8m`): about 0.45 s for 64 KB. OO-Amp loads
    OOAMP.GFX that way, a record a call (`OOAMP.s`'s gfx_load: 51 and 33 into bank 9 by the units its header names);
    PLAYSID reads a whole file into the TPA. The card can do better: one READ of many sectors runs on at a byte a
    cycle after its first sector's 715 cycles (Technical Reference chapter 10, "Timing"). The BDOS's buffers are
    written back late (BCB_DIRTY), so a read around them must write back first. The segment's check (`bdosmem.s`'s
    SEGOK and BLKOK) allows banks 0–5, 8–B and E: block 32, the console's, included.
  - **A program can't map memory.** The windows are OODOS's (AGENTS.md; Programmer's Guide chapter 5). W2 can't be
    lent anyway: `$0203` jumps to `$BFFD` in block 2 (`com.cfg`), so a program with another block in W2 can't call
    the BDOS. W3's code window needs common memory in the block's top 2 KB (FARCALL), which a video block hasn't.
    W1 can be: the stub reads WIN1 itself at a call and puts that value back (`common.s:166`, `ST_W1`), the
    dispatcher borrows it for the event area and puts back what it found (EVALL), a hook gets the windows it was set
    with, and the warm boot puts block 1 back. A single `STA NB_WIN1` can't be split by an interrupt. OO-Amp's
    `oa_hal.s` maps W1 itself, with the owner's leave, and works for these reasons.
  - **Memory** (bytes free, normal build, then debug): common memory 176 (166); block 60 569 (553); block 61 6,204;
    block 62's code window 1,188 (985); SYSBSS 132 (129); zero page 2. Function 82 is 1,488 bytes of block 60, of
    which the loads' engine (DMALOAD, RAMEND: 207), BEEPQ (75) and the beep's loop (about 90) read no parameters
    and could run in block 61.

- **Function 83, DMA and loading** (`O_DMA`, `DM_*`), by L1's convention (X = 0: the operation in A; else A/X a
  block, +0 the operation). Addresses are physical, 3 bytes (function 73 turns a CPU address into one). A
  destination is checked as 51's segment is (SEGOK) and must not touch the console's units 0–12 (`$80000–$833FF`);
  a refusal is A = X = `$FF`, nothing done.
  - `DM_INFO` (0): A = the first video unit a program may use (13), X = the vertical-blank slots (4); a block gets
    +1 and +2 those, +3 the slots' budget in 256-byte pages, +4 the pointer's sprite (`$FF` off), and +5–12 **the W1
    map** (as built, the owner's: `FT_INFO`'s convention):
    a bit for each of the 64 blocks, set for those W1 may hold now (block 1, the blocks the program has allocated,
    video 33–47). The library reads it at its start and again after it allocates or frees a block.
  - `DM_LOAD` (1): +1–2 an open FCB, +3–5 the first record, +6–7 a count of records (1–512: 64 KB a call), +8–10
    the destination. Whole 512-byte sectors on an SD drive of OODOS's formats go by channel 0 straight to the
    destination, at most 8 sectors (4 KB) a READ, so the CPU is held 4,096 cycles at most and interrupts are taken
    between them (L3's piece). On `oodos-8m` a CP/M block is 4 KB, 8 sectors, so it is one READ; on `oodos-32m` (8
    KB) two. A partial sector at either end goes through the BDOS's buffer and MOVE, as 33 does, with the segment
    set for it and put back. M: copies from banks C–D by DMAGO. apple-do drives (O:, P:, and apple-do images on
    the card: their 256-byte sectors are skewed in a track buffer) go through the buffer whole. The drive's dirty
    buffers are written back first, so a load sees every write. A = 0; 1 the file ended or a block was never
    written (+6–7 rewritten as the records loaded); a physical error as 33 gives it. The FCB is left as a random
    read of the last record loaded leaves it. No vertical-blank wait (question 13): a program loads into a hidden
    buffer and flips. Estimated 64 KB from `oodos-8m` in about 15 ms against about 0.45 s; the test measures it.
  - `DM_COPY` (2): +1–3 source, +4–6 destination, +7–8 length (0: 65,536), channel 3 by DMAGO. A copy to a lower
    address is exact over itself; to a higher one it runs forward, as DMAGO's do. `DM_FILL` (3): +1 the byte, +2–4
    destination, +5–6 length, DMAGO with HOLD from a byte in the system block (as built, the owner's: the byte to the first, then the first copied forward over the rest). The source may be anything SEGOK
    allows, the console's font included (the library copies it for text on bitmaps).
  - `DM_VBSET` (4): +1 a slot 0–3, +2–4 source, +5–7 destination, +8–9 length (1–1,024; all four 1,024 at most;
    0 frees the slot). **Copied at the vertical blank** by channel 1, from the frame handler, each slot whose bit
    the program has set in the event area's +12 (`EV_VBGO`, with `TSB`, which an interrupt can't split), its bit
    cleared when done. Refused with no video card or no DMA controller. Nothing goes with no event area open (any
    layout: +12 is in layout 1's 16 bytes). The library binds slot 0 to its sprite shadow table (512 bytes) and slot
    1 to its palette shadow, so sprite moves and fades show whole on the next frame, at no cost to the program but
    a `TSB`.
  - `DM_POINTER` (5): +1 a sprite 0–63 (`$FF` off), +2 and +3 the hotspot. At each vertical blank, after the slots,
    that sprite's X and Y (bytes 2–5) in the table at SPRBASE are written from L2's mouse position (MXS, MYS) less
    the hotspot, modulo 1,024: the pointer L2 deferred. The program draws the image, sets the sprite's other bytes,
    and puts the mouse in absolute mode within the sprite space (`IN_MOUSE`, 319 and 239). Refused without a
    layout-2 area (MXS and MYS are kept only then).
  - `DM_CONSOLE` (6): the console's screen back now, as the warm boot puts it (question 10: console output goes on
    into the text screen unseen, and this shows it, for GW-BASIC's `SCREEN 0`).
  - **No map operation (decided, question 1):** OOLIB writes W1 itself, under the W1 contract below.
  - **Function 84 isn't needed:** nothing in video memory is shared between owners. The running program has units
    13–255 whole, as it has its TPA, and the library hands them out inside it. `OA_VIDEO` stays unassigned.
- **The W1 contract** (decided 2026-11-08, the owner: OOLIB is OODOS's extension, there so programs don't go around
  the rules, and may do what the rules keep from programs where OODOS states it as part of the interface). From
  interface version 4: OOLIB may write `NB_WIN1` directly, with any block `DM_INFO`'s W1 map allows, and OODOS
  keeps that value across a BDOS call, an interrupt and a hook (each puts back what it found), and puts block 1 back
  at the warm boot. OOLIB checks `FT_INFO`'s version at its start and refuses to map below 4; a later change to the
  contract comes with a new version. Only W1: W2 and W3 stay OODOS's (the `$BFFD` jump, FARCALL's common memory).
  While another block is in W1 the program's own `$4000–$7FFF` is out of its reach, and of the BDOS's: its code,
  its event area, the FCBs and parameter blocks it passes, and what it draws from stay in `$0300–$3FFF` or
  `$8000–$BFFC`. A hook that draws maps W1 itself. A switch costs 4 cycles, against about 450 for a BDOS call. A
  program not using OOLIB keeps the old rule: the windows are OODOS's. AGENTS.md and the Programmer's Guide's
  chapter 5 say so.
- **The registers a program may write** (chapter 8, Technical Reference chapter 6): every video register but IEN
  (the dispatcher's) and SPLASH; ISR's bit 2 only (writing `$04` clears the collision flag; OODOS never enables
  it). Collisions are read from STATUS and ISR by the library: no OODOS code.
- **The frame handler** (VIDSRV, common memory): after EVALL, `LDA VBON / BEQ / LDX #R61_VB / JSR CALL61`, about 9
  bytes, so a frame with no area is L3's to the cycle and one with an area but no slot or pointer 6 cycles more.
  VBWORK (block 61): the go byte read through W1 as EVALL reads, each slot's copy on channel 1 in one block (1 KB
  at most: 2,048 cycles with the CPU held, under a tick), the bits cleared, then the pointer's 4 bytes from a
  staging copy in SYSBSS. It runs before the sound part and the frame hook. The interrupt normally comes within
  a few hundred cycles of line 480; a 4 KB DMA piece or a USB batch under way can hold it off 4,000–5,000, and
  1 KB of copies still ends inside the blank's 10,240 (the test measures both cases). Channel 1 has no other user,
  so a BDOS DMAGO or LOAD between its blocks is never disturbed (question 3).
- **The linked library** (`oodos/lib/`, `build/oolib.lib`, `include/oolib.inc`, as `sid.s`; each module taken only
  if used; the 65C816 rules gate as PLAYSID's):
  - `gxmode.s`: a layer's type and options (CONFIG), its map and tiles (units), on and off, the border, the console
    back (`DM_CONSOLE`).
  - `gxmem.s`: video memory in 1 KB units, 13–255, a 243-bit map in the program's BSS: allocate n units aligned to
    n (a bitmap 512 is 120 units; two of them fill 16–255), free, and map a unit's block into W1 (`STA NB_WIN1`
    under the W1 contract, checked against `DM_INFO`'s map, the block cached so a draw maps only when it moves on),
    and put block 1 back. Units 13–15 are DMA-only (block 32 isn't in the map).
  - `gxpal.s`: set entries, load 256, fade over n frames, through a 512-byte shadow on slot 1.
  - `gxspr.s`: the 64-entry shadow table on slot 0: set position, image, size, depth, flips, palette, collision
    groups; show (`TSB`); the collisions seen.
  - `gxtile.s`: put and get map entries, fill a map, scroll a layer.
  - `gxbmp.s`: plot and point at 8, 4 and 1 bits a pixel; line; rectangle; filled rectangle (full-width rows by
    `DM_FILL`: bitmap 256 and 512 rows are 256 bytes, so a full-width band is one run; partial rows by the CPU);
    circle; flood fill (spans, for PAINT); a copy of a rectangle; double buffering (MAPBASE, then `FT_WAIT`).
  - `gxtext.s`: an 8×16 font (the console's, copied from units 1–4 by `DM_COPY`, or the program's) drawn on a
    bitmap, a transparent or opaque background.
  - `gxasset.s`: an asset file opened and its parts loaded by `DM_LOAD` to the units its header names, or to units
    it allocates; the units returned for the program to point the card at.
  - Estimated 3–4 KB for all of it, a program taking what it calls.
- **The asset format** (question 8; question 6 below): OO-Amp's OOAMP.GFX format 2 (`'OGX'`, the format, the parts,
  8 bytes a part: type, unit, first record, records, length) grown to format 3: record 0 a 16-byte header and up
  to 7 parts of 16 bytes (type, the layer CONFIG bits it was made for, unit or `$FF` for any, first record,
  records, length to 17 bits); types map, tiles, palette, sprite images, sprite table, bitmap, font, raw. Each part
  starts on a 1 KB boundary of the file and fills whole units, so its middle goes by channel 0 and its last record
  stays in its own units. **`oodos/tools/mkasset.py`** (Python, no packages: PNG read with zlib, 8-bit indexed,
  RGB and RGBA, quantised to 12 bits): PNG to bitmap, tiles and map, sprite images, palette; and a check of its own
  files. **VIEW.COM**, a utility (User's Guide chapter 5): shows an asset file until a key, as PLAYSID plays a
  tune, and is the library's test program.
- **The warm boot** (`WBQUIET`'s `CALL61` to a new R61 routine, block 61, about 60 bytes): the slots freed, the
  pointer off, `VBON` 0; the console's registers as CRTINIT sets them (CRTINIT's register part made a routine both
  call), ISR's line and collision bits cleared, the cursor as it was. W1 is already put on block 1. The console's
  units were never writable by function 83 or in the W1 map, so its font, palette and text are as the program found
  them. By `JMP WBOOT`, ^C, an error, or Break (the ROM's VSCREEN, as before). Cold boot: `VBON` 0 and the slots
  cleared (SYSBSS).
- **The event area:** no layout 3. +12 becomes `EV_VBGO` in every layout (L1's reserved, written 0 at the open);
  +13–15 stay reserved. Interface version 4 says it's there.
- **Where it lives** (estimates against the measured room):

  | Where | Free now | L4 | After |
  |---|---|---|---|
  | common memory | 176 (166) | about 9 (VIDSRV's call) | about 167 (157) |
  | block 60 | 569 | first moved out: L3's DMALOAD, RAMEND, BEEPQ and the beep's loop, about +370; then 83's entry, its block read (PREAD's), the table's entry, LOAD's BDOS side (positioning by the extents, the ends through the record body, the write-back) and the console-units check: about 350–450 | about 500–600 |
  | block 61 | 6,204 | L3's moved code 370; 83's operations 400; SDIO with a count and a destination, M:'s path 80; VBWORK 180; boots 70 | about 5,100 |
  | block 62 | 1,188 (985) | WBQUIET's and cold boot's calls, about 10 | about 1,178 |
  | SYSBSS | 132 (129) | slots 32, pointer and staging 8, flags 2, LOAD's state about 16 | about 74 |
  | zero page | 2 | none (the BDOS's T0–T5 and SG) | 2 |

  (d1), the BIOSW wrappers out of common memory, isn't needed and stays in reserve. Moving L3's code first (its
  own commit, OODOS.SYS otherwise byte-identical in behaviour, `test_oodos_sound` unchanged) is question 4.
- **The version:** OOLIB's interface version 4 (`OOLIB_IFACE`), `FT_INFO`'s areas `$0F` (`OA_DMA`); the clock, input,
  sound and playsid suites' expectations follow (`04 0F`, `040F0302`).
- **OODOS 0.5** (question 15), after L4's review, as 0.4 was done after 6d's (7971f7f): `system.inc`'s `SYS_MINOR`
  5, the version's one source, and every place that checks it: the cold boot's banner, OOLDR's and PIP's
  (`test_oodos_sys`, `test_oodos_e2e`, `test_oodos_boot`, `test_oodos_utils_pip`), function 72's answer
  (`test_oodos_clock`'s `0005`), and the User's Guide's samples. Then the cards rebuilt (`make_card.sh`), and
  hw/oodos.md and hw/oolib.md's status lines. The site gets 0.5 by its own deploy branch, after the merge.
- **The books:** chapter 8's Graphics section (modes, video memory, the window, the vertical-blank slots, the
  pointer, the library, assets) and Still to come; function 83's entry and docs/index/functions.json; chapter 5
  (the W1 contract, the console's units refused to 51's reads too if question 2 says so); AGENTS.md's windows rule; appendix D; the Technical
  Reference's chapters 5 (channel 1 OODOS's), 6 (under OODOS: what a program may write, the console's units) and 4
  (the frame handler's copies); the User's Guide's VIEW; `make -C docs check`.

- **Tests** (emulator, fast preset; each suite's fast time stated, the sanitizer tier's TIMEOUT checked):
  - `test_oodos_video` (about 3 s; `oodos/sys/test/l4test.s`): `DM_INFO` and `FT_INFO` 4 and `$0F`; `DM_INFO`'s W1 map
    (block 1, video 33–47, an allocated block set; 32, 0, 2, 3, 24–31, 48–63 and an unallocated block clear, and the
    map following an allocation and a free); the W1 contract: a video block and an allocated one written to WIN1,
    CPU writes landing at the physical address, the value kept across a BDOS call (function 9 from W0), a tick,
    a frame and a USB interrupt, and a hook set before keeping its own windows; a function 33 into `$4000–$7FFF`
    landing in the mapped block; the warm boot putting block 1 back; the library refusing to map on an interface
    below 4 and a block not in the map; `DM_COPY` and `DM_FILL` (60 KB,
    byte for byte, across blocks, down over itself) with no tick lost (MS against the cycles, as L1's scroll test),
    and their refusals (units 0–12, OODOS's blocks, past `$FFFFF`); `DM_VBSET`: a slot's bytes changing only while
    the beam is in lines 480–524 (the must-fail: a copy made in the BDOS's time, before the blank, fails it), the
    go bit cleared, four slots, the budget refused at 1,025, nothing with no area; the frame's cost with a
    512-byte slot, and the latest a slot ends with a 4 KB `DM_LOAD` piece and a 16-event USB batch under way;
    `DM_POINTER` following the rig's mouse in absolute mode, less the hotspot, off with `$FF`, refused without a
    layout-2 area; the warm boot by `JMP WBOOT`, ^C and Break: the console's registers, units 0–12 byte for byte
    as before the program, slots and pointer off; the tick with no area 145, the frame with no area L3's; a layout
    1, 2 and a streaming program unchanged.
  - `test_oodos_load` (about 3 s; files put on B: behind OODOS's back, `test_oodos_sound`'s `put_b` moved into
    `oodos_rig.h`): 64 KB into blocks 33–36 byte for byte, its cycles against 51 and 33 with a count of 128 (both
    stated); an unaligned first record and an odd count (both ends through the buffer); the file ending early (A =
    1, +6–7 the count); an unwritten block; a count of 0 and of 513, an FCB not open (as built: 33's answer, A = 1, the owner's), a destination in units 0–12
    or OODOS's memory, all refused with nothing written; records written and not yet closed, then loaded (the
    must-fail: without the write-back the load reads the card's old sectors); M:; an apple-do image on the card
    (the buffer path); no tick lost; a 44.1 kHz stream playing through a 64 KB load with no underrun. O: and P:
    share the apple-do path; a case on the Disk II itself would need the owner's sequencer PROM, so none is added.
  - `test_oodos_gfx` (about 3 s; VIEW and a test program on the library): each layer type set and the card's
    `frame()` compared with what the test draws from the same bytes; plot, line, rectangles, circle and flood fill
    at 8, 4 and 1 bits checked against a reference the test computes; map entries, scrolling, the sprite shadow
    shown on the next frame, a palette fade over n frames, text on a bitmap; double buffering with no torn frame;
    VIEW on assets `mkasset.py` makes from PNGs the build writes (never the owner's); VIEW's refusals.
  - `mkasset.py`'s own check, run by `make check` as `test/check_mksys.py` is: PNGs it writes itself, round trips,
    the header's limits.

- **Not in L4:** the scan-line hook and a collision hook (two layers give a status bar without a split: a later step if a game needs one; the dispatcher reaches the video card only after the VIA's check, which leaves little of the
  180 cycles a line allows); software sprites (64 hardware ones); a vertical-blank wait for `DM_LOAD` (question 13); FARCALL's
  copy of OOLIB for programs short of TPA (question 9: with GW-BASIC); a cc65 C target (question 5); the demo game;
  OO-Amp on OOLIB (L5); function 84.

**L4's questions (decided 2026-11-08, the owner: 1 as below, 2-6 as recommended)**

1. **How a program's CPU reaches video memory.** *Decided (2026-11-08, the owner): OOLIB writes W1 itself, under
   the W1 contract (above), and there is no `DM_MAP`.* OOLIB is OODOS's extension, there so that programs don't go
   around the rules; where it does what a program may not, OODOS states it as part of the interface, checked by
   version. The stub, the dispatcher, the hooks and the warm boot already keep or restore W1, so the contract
   documents what holds; a switch is 4 cycles instead of a 450-cycle call, which matters to a draw crossing
   blocks; `DM_INFO`'s W1 map replaces the check `DM_MAP` would have made. Only W1 (W2: the `$BFFD` jump; W3:
   FARCALL's common memory). The cost is unchanged: while another block is in W1, the program keeps its code, its
   event area, what it passes the BDOS and what it draws from out of `$4000–$7FFF` (a program of 15 KB or less, or
   one laid out for it: a `gfx.cfg` later, if one is wanted).
2. **The console's video memory, units 0–12.** *Decided: as recommended.* (a) OODOS's: refused as a destination by function 83 and by 51's
   reads, and block 32 not in the W1 map, so the warm boot only sets registers; (b) the program's, the warm boot copying
   the font and palette again and clearing the screen (about 11 ms each warm boot, the console's text lost);
   (c) as now, a program's own care. **Recommended:** (a). Question 10 has console output go on into the text
   screen unseen and show again at the warm boot, which (b) can't keep; (a) costs a program 13 KB of 256 and about
   25 bytes of block 60. With it, function 84 isn't needed: the program has 13–255 whole.
3. **Copies at vertical blank.** *Decided: as recommended.* (a) OODOS's, on channel 1, four slots of 1 KB in all, started by bits the program
   sets in the area's +12, no layout 3; (b) the same on channel 3, its registers saved and put back around the
   interrupt's copy; (c) none in OODOS: the library flips between two sprite tables by SPRBASE. **Recommended:**
   (a). Channel 1 has no user, so the interrupt never meets DMAGO half-programmed (its registers are written over
   several instructions with interrupts on); the pointer sprite (L2's deferral) needs OODOS at the blank, which (c)
   can't give; +12 is free in every layout. 1 KB (a sprite table and a full palette) still ends inside the blank
   with the interrupt held off by a 4 KB piece; 2 KB might not. If the serial card's DMA is built later, OODOS
   owns both and moves its copies.
4. **Block 60's room.** *Decided: as recommended.* Function 83's BDOS side is about 350–450 bytes against 569 free. **Recommended:** move L3's
   load engine and the beep's body (about 370 bytes, none of which reads parameters) to block 61 first, as L3's
   step (b) went first, and keep (d1) in reserve: common memory needs only about 9 bytes.
5. **cc65 C headers.** *Decided: as recommended.* The plan's L4 line names them, but there is no C target for OODOS (a crt0 with
   `OODOS_HEADER`, a linker config, the runtime's zero page inside `$00–$EF`, the 65C816 rules over cc65's
   library). **Recommended:** a step of its own after L4, with a C test program, rather than headers nothing can
   use yet; L4's interface is ca65's, as L3's is.
6. **The asset format.** *Decided: as recommended.* OO-Amp's format 2 as it is, or format 3 (16-byte parts, a part's unit chosen at load when
   the file says `$FF`, each part on 1 KB of the file), with `mkasset.py` and VIEW.COM. **Recommended:** format 3,
   magic `'OGX'` kept so OO-Amp's reader needs a version check only: format 2 has no room for a layer's CONFIG
   bits or a part longer than 64 KB (its length is 2 bytes; a bitmap 512 is 120 KB), and fixed units clash when two files load together.
**L4, as built (2026-11-08)** (`oodos/sys/video.s`, new: function 83's entry and operations, the vertical-blank
slots, the pointer and the warm boot's part, block 61; `bdosfile.s`'s `LOADGO`, DM_LOAD's file part, block 60;
`bios.s`'s `SDION` and `RDRUN`; `crt.s`'s `CRTREGS`; `bdosmem.s`'s `UNITS0`; `irq.s`'s frame call; `input.s`'s
WBQUIET call; `oodos/include/oodos.inc`'s `O_DMA`, `DM_*`, `EV_VBGO`; `oodos/lib/gx*.s` and `oodos/include/oolib.inc`'s
graphics, `oodos/tools/mkasset.py`, `oodos/utils/view.s`, new; `DMA_CH1` in `backplane.inc`, the ROM byte-identical).
OODOS stays 0.4 on L4's branch, and became 0.5 straight after its merge (question 15, its own commit); OOLIB's interface version is 4, `FT_INFO`'s areas
`$0F`. L3's load engine and the beep's body went to block 61 first (question 4), its own commit:
`test_oodos_sound`'s and `test_oodos_playsid`'s output identical to main's, but for the wave-load case's watch range,
which followed the code (the owner's leave). Where the plan left something open it was decided as below.

- **Owner-approved changes to the plan** (2026-11-08):
  - `DM_FILL` writes the byte to the destination's first byte by DMAGO, then copies the first forward over the rest
    (a copy over itself, a byte at a time, repeats it), as `crt.s`'s erase does: the plan's HOLD would have needed a
    mode byte in DMAGO, which the BIOS's MOVE and the console's scroll use; the cost is the same, 2 cycles a byte.
  - `DM_LOAD` on an FCB that was never opened answers as function 33 does, A = 1 with 0 records and nothing
    written, rather than A = X = `$FF`: CP/M's FCB has no mark of being open the BDOS could trust (S2's bit 7 is
    clear on an FCB that has been written, too).
  - `DM_INFO`'s block follows `FT_INFO`'s convention: +1 the first unit (13), +2 the slots (4), +3 the budget in
    256-byte pages (4), +4 the pointer's sprite (`$FF` off), +5–12 the W1 map. The plan's offsets ("+1 those, +2 the
    budget") contradicted themselves.
  - `SN_CLAIM` of the stream writes the area's sound part at once (`SFIELDS`, as `SN_STREAM` and `SN_SEEK` do), so
    `EV_SBUFS` reads 3 straight after the claim rather than at the next frame: `test_oodos_sound` asserted it there
    and passed only when a vertical blank fell in between. `test_oodos_clock`'s check of the frames between two
    dumps is bounded by the time between them (`(ms) / 16 + 1`), as 40 ms of printing crosses 2 or 3 blanks.
- **Decided here, each for its reason:**
  - The pointer isn't written while SPRBASE is in the console's units (below 13): the warm boot leaves SPRBASE 0,
    where the table would be the console's palette.
  - The pointer's four bytes go by channel 1 from a staging copy (`PSTAGE`), as planned, so the interrupt needs no
    zero page of its own (two bytes are left). A slot's copy is waited for (`CH1GO`), so a burst limit in CONTROL
    can't let the next slot's programming meet it running.
  - `SDIO` takes a count (`SDION`, 1 by `SDIO`'s own entry): the READ's sector count and channel 0's length; `RDRUN`
    is the direct path (an SD unit's sectors, or M:'s by DMAGO). The BDOS selects the BIOS's drive as `DISKOP` does,
    and puts W2 back on block 61 before calling it (`GETBUF` leaves W2 on the buffers).
  - `LOADGO` gives 3 and 6 as 33 does (an extent that can't be closed, past 32 MB); 4 (an extent never written) is
    the file's end, 1. The write-back is `FLUSHD` for the drive; a failure there is 33's physical error (`DERR`).
  - The library allocates units aligned to 8 for 8 or more (else to the largest power of two at most the count):
    the plan's "aligned to n" can't be met for 120 units, and 8 gives two bitmaps 512 at 16 and 136. Units 13–15 are
    in block 32, which the W1 map hasn't: the CPU can't reach them, DMA can (a palette or sprite table shadow, a
    sprite table GX_ASSET relocates through a copy).
  - The console's font is read a character at a time (16 bytes by `DM_COPY`), not copied whole: 4 KB of the program's
    memory below `$4000` is worth more than 500 cycles a character.
  - The vertical-blank go bits are set by a `TSB` whose operand `GX_INIT` writes (in the DATA segment), so the
    program's read-modify-write can't be split by the interrupt's.
  - A function 51 read into units 0–12 is refused at `DOUT` (`UNITS0`), as function 83's destinations are (question
    2's "and by 51's reads").
- **Memory** (bytes free after L4, normal build, then debug): common memory 166 (156), down 10 (the frame's call);
  block 60 347 (331): 905 after the move, then `LOADGO` (about 520), `UNITS0` and `DOUT`'s check; block 61 4,630;
  block 62's code window 1,183 (980); SYSBSS 59 (56); zero page 2. VIEW.COM is 5,238 bytes, its code and BSS to
  `$1C0C`; GXTEST, every module linked, ends at `$2A70`.
- **Costs** (`test_oodos_video`, from the interrupt's first cycle to its RTI's last): a tick with no area **145, L1's
  to the cycle**; a frame with no area **199, L3's to the cycle** (main 0497570 measured the same way); a layout-2
  frame 396, L3's 389 and `LDA VBON / BEQ`'s 7; with a slot set and no bit 516; a 512-byte slot copied 1,774 (its
  1,024 cycles of DMA inside); the pointer on 728. The latest a copy ended: line 502 with a 16-event USB batch at line
  470, line 495 under 64 KB loads; the blank is lines 480–524. `test_oodos_load`: 64 KB from `oodos-8m` by DM_LOAD
  in 114,442 cycles (16.0 ms), against 620,419 (86.7 ms) by 51 and 33 with 44's count of 128; 116,346 with a 44.1 kHz
  stream playing, no underrun; 24 KB from M: 75,534; 20 KB from an apple-do unit 913,717 (through the track buffer).
- **Tests** (emulator, fast preset): `test_oodos_video` (8 cases, 1.8 s), `test_oodos_load` (6 cases, 1.1 s),
  `test_oodos_gfx` (7 cases, 2.4 s), and mkasset.py's own (`oodos/test/check_mkasset.py`, from `make -C oodos check`,
  0.4 s). Must-fail, each against a build with the fault put in: a slot copied in the BDOS's time (DM_VBSET copying
  at once) fails the slot's case (the bytes changed at line 51, before the bit); DM_LOAD without its write-back
  reads the card's old record 8. Test programs `oodos/sys/test/l4test.s` and `oodos/test/gx/gxtest.s`. The clock and
  input suites' `FT_INFO` expectations follow the version: `04 0F`, `040F0302`.
- **The review** (2026-11-08; the owner: fix F1, F2, F4-F9, document F3, F10 disputed: "Since OODOS 0.5" stays):
  - F1: `DM_VBSET` summed a new length with the others' in 16 bits and lost the carry, so slot 1 at 1 byte and slot
    0 at `$FFFF` passed: a 64 KB copy, masked, each frame. A length over 1,024 is refused alone now, and the sum
    refuses a carry (the test: both `$FFFF` and `$FC00` refused, slot 0's length as it was).
  - F4: a load past the end of a file whose last extent is full left the FCB on the next, empty extent; `LDEND`
    now positions it at the last loaded record's extent, as 33 does. The test, failing before (EX 1, function 20
    at the end of the file): a 128-record file loaded with a count of 200, then function 20 reads record 127.
  - F2: the library never gives function 83 a length of 0 (65,536): `GX_BCOPY` of no width or height and `GX_TFILL`
    of no rows do nothing, and a `GX_BCOPY` rectangle that leaves the bitmap is refused (`GE_BOUNDS`), not clipped:
    draws clip, but clipping a copy's one side would move the other. F3, documented: a copy's rows go forward, as
    memcpy, so a strip moved right on its own rows repeats its first bytes.
  - F5: `GX_ASSET` checks each part (its type 1-8, its record on 1 KB and not 0, 1 to 131,072 bytes, its records
    its bytes' rounded up), takes a unit a part names (`GXTAKE`: 13-255, free) as well as allocating `$FF`'s, and frees
    every unit the file took when a part is refused (the tests: files of our own on M:, each refused and the units
    free after, a sentinel untouched).
  - F6: with no event area (`GX_INIT`'s 0) `GX_PALSHOW`, `GX_SPRSHOW` and `GX_FADE` are refused (`GE_AREA`); the
    `TSB` had nothing but the program's zero page `$0C` to hit.
  - F7: mkasset.py checks each chunk's length and CRC, IHDR's 13 bytes and size, PLTE's entries and every index,
    and decompresses no more than the picture needs, refusing a short one: an error line and exit 1, never a
    traceback (the check builds the bad PNGs itself).
  - F8: the latest a frame's copies end was measured with half the budget; with all four slots (1,024 bytes), the
    pointer and a 44.1 kHz stream it is line 508 under a USB batch (16 lines to spare) and 511 under 64 KB loads (13;
    round 2 has the stream asserted still playing, no underrun, at the end); a frame of all of it 3,645 cycles.
  - F9: the Break case checked after a cold boot, which clears what it checked; it now checks a slot and the
    pointer on before Break, and after it the card's interrupts off and a slot's bit set under the monitor copying
    nothing.
- **The second review** (2026-11-08; the owner: fix R1-R3 as recommended):
  - R1: `HRUN` and `VRUN` ran until the pixel reached `hi`, so a clip that left `lo` past `hi` (a rectangle wholly
    off the bitmap) went round 65,536 pixels a row; both now draw nothing then, and mono's whole-row fill too. The
    test: a filled rectangle wholly off each edge, at 8, 4 and 1 bits, draws nothing, each call under 60 ms.
  - R2: the full-budget case asserts the stream still playing at its end, with no underrun (its runs shortened to
    fit the 0.74 s of buffers).
  - R3: `GX_BCOPY`'s refusals on a mono bitmap and at an odd x or width at 4 bits return `GE_BOUNDS` in A.

**Added (2026-11-08, the owner), from OO-Amp's proposal** (`OOLIB-SOUND-PROPOSAL.md` in OO-Amp's repo, written at
its M4). L3 gives a program the card's parts one at a time; a player also wants the same few verbs whatever is
sounding: pause and resume, volume and balance, where it is, whether it still plays, how loud it is now. OO-Amp
had to leave pause and volume out for SIDs and work out the rest itself. Two steps after L4 (each its own branch),
and more in L5:

- **L4a: open's byte count.** With CR `$FF`, function 15 copies S1 from the extent it opened
  (`bdosfile.s`, open), but the Programmer's Guide says it gives the byte count of the file's last record. cpmtools
  keeps the count in the last extent's entry only, so a file of more than one directory entry (over 32 KB on M:)
  reads as whole records; PLAYSID pads two of the corpus's tunes over their last bytes. The fix is the code's, not
  the book's: with CR `$FF`, open also finds the file's last extent and takes its S1. A test opens a file of three
  extents with a short last record, and PLAYSID loads such a tune to its exact length. OO-Amp's own lookup of the
  last extent can then go.
  - *L4a, as built (2026-11-08):* first, cpmtools itself: `cpmcp -f oodos-ram` of a 70,050-byte file writes three
    entries, EX 1, 3 and 4, S1 0, 0 and 34, so a full entry's S1 is 0 and a full last record's too. The rig's
    `put_m` now writes a file of several entries so (`MFile`'s `count`: S1 in the last entry only); a file of one
    entry is written as before. The tests showed the bug before the fix: `test_oodos_load` opened a three-entry
    file at each extent and got CR 0 but from the last; `test_oodos_playsid`'s 34,956-byte PSID (two entries, a
    last record of 12 bytes) had 116 bytes of padding loaded past its end. The fix (`bdosfile.s`, open's `@count`
    and `LASTS1`): with CR `$FF`, after the access stamp (which needs extent 0's DIDX), every entry of the file is
    looked at, as Compute File Size looks, in the user it was opened from (`EXTUSER`: user 0 for a SYS file found
    there), and CR takes the S1 of the entry with the highest extent (S2, then EX; an entry's EX is its last
    logical extent, so the extent mask needs nothing more). S1 is copied as it is, so 0 keeps its meaning; the FCB
    is otherwise as before, byte for byte. Tests: every extent of three, the last extent's entry first in the
    directory, a SYS file of user 0's opened from user 3, a one-entry file and a full last record as before, the
    access stamp still extent 0's and nothing else in the directory changed; each fails on the old code (and the
    user and highest-extent cases on a fix that scans the current user, or takes the last entry found). Cost:
    block 60 1,493 bytes free to 1,442 (debug 1,477 to 1,426), 51 bytes (67 after review round 1, below); about 2,400 cycles more for an open of a
    one-entry file on M:, 3,900 for three, only with CR `$FF`. The books say "from OODOS 0.6; before it, the
    opened extent's", as "from OODOS 0.5" says for the video-memory refusal.
  - *L4a review round 1 (2026-11-08):* `LASTS1` now runs before the access stamp, keeping DIDX in `DSAVE` (free once
    `OPENPW` is done) and giving it back for the stamp, so a read that fails in the look (Disk I/O, no disk on O:
    or P:) fails the open before anything is written. The access-stamp test still sees only extent 0's slot change,
    and fails on a version that doesn't give DIDX back (the stamp in another slot). A test that the look's failure
    leaves no stamp was not made: M: can't fail a read, and on the card the rig can only eject it whole, which
    fails the stamp's own write as well, so the two orders look the same from outside. New tests: entries written
    by hand across a module (S2 1, EX 0 first in the directory, S2 0, EX 31 after: S2 1's S1), which fails on a
    compare of EX alone; a three-entry file with S1 in its first two entries and 0 in the last (function 30's
    f6', then growth), giving 0, and $44, $44, $33 giving $33, which fail on "the nonzero S1" and "the largest".
    The function 15 entry's Cycles say the look may read the Disk II on O: and P:. Owner: "from OODOS 0.6; before
    it, ..." is the books' marker for a behaviour change; F_SIZE's user (`SCB_USER`, not `EXTUSER`) is on the
    backlog. Block 60 1,426 bytes free (debug 1,410): the look 67 bytes in all.
- **L4b: hold, go and volume, in function 82.**
  - `SN_HOLD`: the 6510's CTRL RUN off without RESET, the SIDs silenced through VOLL/VOLR (the firmware applies
    them at once, [sound-rev2.md](sound-rev2.md)), the volumes before it kept. `SN_GO`: RUN on, those volumes back.
    The 6510's state and the tune's RAM are untouched, so it carries on where it stopped. [SC_6510]
  - `SN_VOLUME`: VOLL and VOLR, which scale the SIDs and the DAC alike, so one call covers every source. It needs no
    claim. This amends [Sound](#sound): volume becomes OODOS's, because the warm boot must put it back to 255
    (unity), as it puts back the rest of the card; the EQ and the vis page stay linked.
  - The warm boot sets VOLL and VOLR to 255, after a hold as after anything else.
  - `SN_DOCHOLD`/`SN_DOCGO` (the ES5503's RUN) wait for MOD playback, which asks for them.
  - **Decided (2026-11-08, the owner, from the sizing):** L4a and L4b as planned would leave block 60 about 160 bytes,
    so L4b's first commit moves function 82 whole into block 61, as function 83 lives: its entry and every operation
    (PGET and PPUT put W2 back on block 61; nothing in it calls the BDOS's positioning or buffers, which move W2).
    Block 60 went from 337 bytes free to 1,493, block 61 from 4,618 to 3,462; `test_oodos_sound`'s and
    `test_oodos_playsid`'s output is identical to main's, line for line. **And hold silences each SID through its own
    `$D418` volume nibble,** kept and given back by go, not through VOLL/VOLR: the master volume scales the stream's
    DAC too, so a hold by VOLL would mute a stream playing alongside and fight `SN_VOLUME`. L4a follows, in block 60.
  - *L4b, as built (2026-11-08):* `SN_HOLD` 17, `SN_GO` 18, `SN_VOLUME` 19 (`SN_OPS` 20), all in block 61. First a
    test showed the premise: each SID's `$D418` reads back as last written, by the 65C02 or by the 6510 running a
    tune, and not as VOLL/VOLR scaled it. The hold turns RUN off first (the card drops the 65C02's SID writes while
    the 6510 runs), reads both SIDs' `$D418` before writing either (with CTRL's BOTH a write to A reaches B), and
    writes each with its low nibble 0. Decided here: a hold is refused unless RUN is on, which covers a 6510 never
    started, stopped, or held already (a second hold so can't overwrite the kept bytes), and a go is refused unless
    held. `SN_STOP`, `SN_START`, a release of the 6510 and the warm boot forget a hold (SIDSTOP clears the flag;
    `SN_START` too). `SN_VOLUME` takes a block only (+1 VOLL, +2 VOLR) and is refused on revision 1, which has no
    master volume; no A/X form, since a read form's 255s would look like a refusal. The warm boot (SNDQUIET, so the
    cold boot's warm boot too) writes both back to `$FF` on revision 2. Memory: three bytes of SYSBSS (`SHELD`,
    `SVOLA`, `SVOLB`): 59 free to 56 (debug build 56 to 53); block 61 3,462 free to 3,322 (140 bytes, both builds);
    block 60 unchanged, 1,493 free (debug 1,477). Nothing is added to the tick, the frame or the dispatcher. OOLIB's
    interface version stays 4 for now: question 15 says a step that adds raises it, but that changes five
    owner-approved `FT_INFO` expectations, so it is the owner's call.
  - **Owner (2026-11-08), after the build:** OOLIB's interface version 5 for the three operations (`OOLIB_IFACE`;
    `FT_INFO` answers `05 0F`, so a program can tell they exist: an OODOS 0.5 refuses them as a refused hold is
    refused), the five `FT_INFO` expectations changed with it and `test_oodos_gfx`'s patch of the answer looking for
    5; the books' "since OODOS 0.6", one bump after L4a and L4b; the sound suite's own `SN_OPS` 20, as `oodos.inc`.
  - *Review round 1 (2026-11-08):* `SN_VOLUME` reads both bytes before writing either, so a block refused at +2
    (in the I/O hole) changes nothing; `SN_START` forgets a hold only once its block is read, so a refused start
    leaves the hold for `SN_GO`; `SN_GO` turns RUN off before writing the kept bytes, as a program may have set RUN
    while held (the card would drop the writes). Tests for each, and for CTRL's NTSC, `$D500` and BOTH bits kept
    through a hold and a go; L3TEST's B calls function 82 with a block just under `$C000`. Block 61 now 3,310 bytes
    free (12 more). Accepted risk (F6): a 6510 that stops itself through `$DE02` in the few cycles between the hold's
    read of CTRL and its write is still recorded as held, so a go would start it again past its own stop.

- **L5: OO-Amp on OOLIB.** OO-Amp's agent replaces `oa_hal.s` with OOLIB, in its own repo. That's the
  real-program test of L1–L4, as GW-BASIC was meant to be. Our own gate is still the rig's tests. With it, in
  OOLIB (linked), from OO-Amp's proposal:
  - **A playback layer** over function 82, the source a number (`PB_SID`, `PB_STREAM`, `PB_DOC`): `PB_PAUSE` and
    `PB_RESUME` (hold and go, the stream's pause and resume, or the ES5503's); `PB_VOLUME`, a level 0–255 and a
    balance (0 left, 128 the centre, 255 right) turned into VOLL and VOLR by one fixed pan law; `PB_STATUS`, a
    block: the state (stopped, starting, playing, paused, stalled, ended), the milliseconds played with paused time
    left out, and a stream's underruns. A SID counts as stalled after 2 s with no mailbox byte, or 10 s before the
    first, as OO-Amp has it, since a tune's init can take seconds. `PB_LEVELS`, a block of a few bands and a VU
    pair in one format for every source: a stream's from the vis page (its 32 bands averaged down), a SID's
    estimated from its registers (each voice's gate, envelope and pitch give a level in the band of its pitch). The
    SID's are an estimate for a meter, and the books say so.
  - **SID reads:** `SID_VOICES` (both SIDs' voices and filters, from the last values the card holds),
    `SID_NOTE` (a frequency and the clock to a note, C-0 to B-7), `SID_ENV` (a per-frame envelope estimate kept
    per voice). OO-Amp's `oa_hal.s`/`oa_voices.s` do these now; they move here once reviewed.
  - **EQ and the vis page** (`EQ_SET`, the preamp and ten bands, `EQ_ON`), which L3 planned as linked register
    work and didn't build.
  - **Planned (2026-11-09, the owner):** four steps, each its own branch. L5a, OODOS: the mailbox stamped in the
    area and the warm boot's flat EQ (OOLIB interface 6). L5b, the EQ and the vis page (`PB_EQ`, `PB_EQON`,
    `PB_EQALL`, `PB_VIS`: `EQ_` is the event queue's). L5c, the SID reads. L5d, the playback layer, `PB_LEVELS` with
    8 bands. Then OODOS 0.7. OO-Amp's half (`oa_hal.s` on OOLIB) was done in its repo on 2026-11-08
    (`PLAN-L5.md` there); it asked for the EQ and the vis page first (`OOLIB-EQ-VIS-REQUEST.md`).
  - *L5a, as built (2026-11-09):* with the 6510 claimed and a layout-2 area open, MBOXIN writes the byte
    (`EV_SMBOX`, +108), the ms counter (`EV_SMMS`, +109-112) and a count held at 65,535 (`EV_SMCNT`, +113-114)
    before it queues `EK_SOUND`, so a stall shows without the program passing its events on. `SN_START` turns RUN
    off first, then under SEI drops a byte the old run left in MBIN, sets `EV_SMMS` to its own time and the count
    to 0 (W1 borrowed), so "no byte yet" has a start; `EV_SMBOX` is left. The count saturates rather than wrapping, so 0 always means none since the
    start. SNDQUIET, on revision 2, turns the EQ off (EQIDX `$80`, EQVAL 0) and writes the eleven gains 0, as
    /RESET leaves them. `OOLIB_IFACE` 6, the seven `FT_INFO` expectations and `test_oodos_gfx`'s patch with it;
    `SOUND_EQ_ENABLE` in backplane.inc. Block 61: 3,310 bytes free to 3,209 (101, more than the plan's 40: the
    saturating count and the start's own stamp); block 60, SYSBSS and common unchanged. Nothing added to the tick
    or the frame; on the mailbox's interrupt about 140 cycles more a byte (counted from the code). Tests (`test_oodos_sound`): the byte, its
    time equal to the queued event's and the count, over two bytes; the count held at 65,535; a restart counted
    afresh; before a first byte, the start's time and 0; nothing stamped with a layout-1 area; the warm boot's
    flat EQ by WBOOT, Ctrl-C and BREAK.
  - *L5a review, round 1 (2026-11-09):* F1, an area opened while the 6510 already runs starts with `EV_SMMS` 0:
    no OODOS change; chapter 8 says to measure from the later of `EV_SMMS` and the program's own start, and L5d's
    `PB_STATUS` does so from `PB_BEGIN`'s time. F2, a byte the old run sent between the stamp and RUN off counted
    as the new run's first: the stamp now follows RUN off, and MBIN is read once under the same SEI to drop it
    (a test puts a byte in MBIN at that read, `SMDROP`, and fails without it). F3, a test with the queue full: the
    stamp follows every byte.
  - *L5b, as built (2026-11-09):* `lib/pbeq.s` (`PB_EQ`, `PB_EQON`, `PB_EQALL`; 95 bytes), `lib/pbvis.s` (`PB_VIS`;
    41) and `lib/pbcard.s` (22: the card's ID, revision 2 or later, read directly, no BDOS call), with `lib/pb.inc`
    naming the registers as `gx.inc` does the video card's; 5 bytes of zero page. Two modules so a meter links no
    EQ. Decided here: a refusal writes nothing and says why (`PE_NOCARD`, `PE_RANGE`); `PB_EQALL` checks all eleven
    gains before writing any; `PB_EQON` takes any non-zero A as on; `PB_VIS`'s length 0 means the whole page, and
    over 98 is refused without a snapshot. No claim, as OO-Amp asked. Tests (`test_oodos_pb`, PBTEST as GXTEST): the
    registers read back through EQIDX/EQVAL after each call; every refusal leaving the EQ as it was; OO-Amp's test,
    a sine at each band's centre +12 dB by the library, 12.00 dB louder at the tap (fails with the bands shifted by
    one); the vis copy equal to the card's held page byte for byte and fresh; a revision-1 card refused; the warm
    boot flat. Nothing in OODOS changed.
  - *L5b review, round 1 (2026-11-09):* F1, a hook calling an EQ routine while the program is between another's
    EQIDX and EQVAL writes: documented (chapter 8, oolib.inc), not guarded, as the library turns interrupts off
    nowhere. F2, the revision-1 case's last check couldn't see a write (that card ignores the registers): taken
    out, the case named for what it shows.
  - *L5c, as built (2026-11-09):* `lib/sidv.s` (`SID_VOICES`, `SID_VOFF`; 36 bytes and 6), `lib/sidnote.s`
    (`SID_NOTE`; 80, and 384 of thresholds) and `lib/sidenv.s` (`SID_ENV0`, `SID_ENV`, `SID_LVL`; 253, 70 of steps, 27
    of variables). The rules are OO-Amp's, so its notes and meters stay as they were: `tools/mksidtab.py` writes
    `lib/sidnote.inc` and `lib/sidenv.inc` (committed), equal to OO-Amp's `build/sidtab.inc` but its padding (the
    search stops at note 95). Decided here: `SID_ENV` steps all six voices from one block, their levels in
    `SID_LVL` with `SID_ENV0` to reset, rather than a call a voice; `SID_NOTE` takes CTRL's bits in Y; each returns
    carry clear. Tests (`test_oodos_pb`): `SID_VOICES` against 50 registers as written; `SID_NOTE` either side of
    every threshold for both clocks, worked out in the test; `SID_ENV`'s six voices, 100 frames, against a model
    of the rules with no difference, and voice 0 within a frame of the data sheet's times (full 15 against 15.0,
    the sustain 6 after against 5.7, quiet 3 against 2.3). Both cases fail with SID B's offsets or the clock bit
    broken.
  - *L5c review, round 1 (2026-11-09):* F1, `SID_ENV` a frame's step a call, so missed frames make the meters lag:
    documented (once a frame, not made up), not given a frame count. F2, a retrigger between two calls unseen and
    a frequency read across a 6510 write: documented as the estimate's limits.
  - *L5d, as built (2026-11-09):* `lib/pb.s` (`PB_INIT`, `PB_BEGIN`, `PB_PAUSE`, `PB_RESUME`, `PB_STATUS`; 846
    bytes, most of it the stream's exact milliseconds), `lib/pbvol.s` (`PB_VOLUME`; 74), `lib/pblev.s`
    (`PB_LEVELS`; 287) and `lib/pbmath.s` (70). Decided here: `PB_INIT` takes the event area, as `GX_INIT` does;
    errors `PE_STATE` and `PE_REFUSED`; states `PS_STOPPED` to `PS_ERROR`; a SID with RUN off is stopped (so a
    hold made with `SN_HOLD` directly reads as stopped); the stall rule counts from the later of `EV_SMMS` and the
    begin or last resume (review F1 of L5a); a stream's milliseconds are its frames x 1,000 / its rate; `PB_LEVELS`
    puts a SID's voice in its octave's band, scaled by its SID's volume. Tests (`test_oodos_pb`, 8 new cases) on
    real sources: a 6510 sending mailbox bytes then stopping, a silent one, a 44.1 kHz stream, the ES5503's RUN;
    the law at 45 points. The resume case fails with the stall's restart taken out, the law's with 128 for 127.
  - *L5d review, round 1 (2026-11-09):* F1, a .sid with no play routine sends no mailbox bytes, so it read as
    stalled: `PB_BEGIN`'s X bit 0 says whether the 6510 reports (`SID_PLAYA` not 0); clear, a running 6510 is
    playing. F2, a resume refused after the part was stopped left it paused: it is no longer paused then. F3,
    `PB_STATUS` and the stream's arithmetic moved to `lib/pbstat.s` (640 bytes), so pausing alone links `pb.s`'s
    222. F4,
    a test of `PB_LEVELS` for the ES5503.
- **L6: the music driver** for SOUND, PLAY and BEEP (question 5), just before GW-BASIC needs it. Nothing before
  then needs it.
  - *L6's plan (decided 2026-11-09, the owner: as recommended):*
    - **Scope:** note-level commands, as question 5 has it (note, length, voice, envelope). GW-BASIC parses PLAY.
    - **Where it runs:** on the card's 6510 (this section's "Sound").
    - **Timing:** 1 ms steps by CIA 1's timer A.
    - **Voices:** six (both SIDs), each with a queue of 32 entries, GW-BASIC's buffer.
    - **Transport:** mailbox bytes by `SN_SEND`, with events back as `EK_SOUND`.
    - **No OODOS change and no bump:** interface 6 has what it needs. BEEP stays `SN_BEEP`.
    - **Two steps:** L6a, the driver and its protocol; L6b, the library (`MU_*`) and a test program.
  - *L6a as built:* `oodos/lib/mudrv/mudrv.s` (its own folder: the Makefile assembles `lib/*.s` as 65C02 library
    modules), ca65 for the 6502, `mudrv.cfg` at `$1000` of the card's RAM, 938 bytes.
    - **The image:** `JMP init`; at `$1003` the timer latch for 1 ms, set by the loader (984 PAL, 1,022 NTSC);
      at `$1005` the version, 1; at `$1006` a status block the host may read: `LOST` (events dropped, held at
      255) and `TICKS` (the steps since the start, 24 bits).
    - **Commands:** `$0v` NOTE (frequency word, length ms, gate ms), `$1v` INSTR (control bits, AD, SR, pulse
      width), `$2v` STOP (`$27` all), `$3v` MARK (0–15), `$40` PAUSE, `$50` RESUME, `$6s` VOLUME (SID A, B or
      both), `$70` HELLO. Anything else is refused (`$37`).
    - **Events:** `$01` HELLO's answer, `$1v` an entry finished, `$2v` the voice's queue empty, `$3v` refused
      (full), `$80` + v × 16 + m a marker.
    - **Its cost, measured:** six voices holding take about 19% of the 6510; a voice changing note about 180
      clocks more; six changing in one step about 1,140 clocks, past the millisecond, with no step lost.
    - **Two races found in testing, between the main loop and the step:** the queue's slot was read from `CNT` and
      the head in two reads, so a step taking an entry between them put the new one a slot too far; and the two
      shared a scratch byte. The slot is now worked out with interrupts off, and the step has no scratch byte of
      the main loop's.
    - **The first step** took 57% of the 6510 with six notes held, so the main loop starved and events were
      dropped. Each entry's two waits are now worked out when it is queued, and the step counts one negated word.
    - *L6a review, round 1 (2026-11-09):* F3, INSTR kept the SID's TEST bit, which stops the oscillator: masked
      to waveform, ring and sync. F4, the test read the driver's zero page at hand-worked addresses: `LOST` and
      `TICKS` are in the image's status block. Accepted: F1, an event dropped is said only in `LOST` (L6b resyncs
      its counts at each `$2v`); F2, no resync for a lost command byte (`SN_SEND` loses none); F5, `TICKS`'s few
      clocks; F6, a legato note's gate drops and rises in one step, so its attack starts afresh.
    - **Tests:** `test_mudrv` (14 cases) runs the driver on the card with the SIDs as the oracle: the gate and the
      length to within 0.5 ms, articulation, rests, six voices, the queue's 32 and the 33rd refused with no event
      lost, markers, STOP, PAUSE, instruments, volumes and refusals, NTSC's latch, the races under load, and the
      step's cost. The must-fail cases: PAL's latch on NTSC (in the NTSC case); events sent before the host took
      the last (the queue case fails); the slot read without interrupts off (the load case fails).

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
    Disk II), go through a buffer and channel 3. *Corrected (L4's plan): only on OODOS's own formats, at most 8
    sectors (4 KB) a READ so the CPU is held under a tick; an apple-do image on the card goes through the buffer
    too (its 256-byte sectors are skewed in the track buffer), and M: by DMAGO from banks C–D.* It doesn't wait for vertical blank: a program that cares loads
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

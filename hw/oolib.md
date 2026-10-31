# OOLIB: the OO-1's game and device services #

**Status: draft (2026-10-30). Nothing here is decided yet.** This document proposes how OO-1 programs reach the
graphics, sound, timing and input hardware, so that every game and GW-BASIC use the same calls and nobody invents
their own. The hardware is in [design.md](design.md); OODOS, the operating system these services join, is in
[oodos.md](oodos.md). The questions at the end are for the owner to settle, as OODOS's phase 0 settled its own.


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
| Sound | Two SIDs `$C500`, ID A, and a 6510 on the card with its own 64 KB, a one-byte mailbox each way | SID writes are ignored while the 6510 runs. `.sid` tunes play on the 6510 with no main-CPU time |
| Speaker | USB card `$C5A6` (SPKR) | One bit, toggled per access |
| Keyboard | USB card `$C580`, ID B: cooked KEY (ASCII, repeat) and a raw event FIFO (HID usage + press/release) | OODOS's console reads only KEY today |
| Mouse | USB card `$C590`–`$C59A`: buttons, relative or absolute X/Y, wheel | Absolute mode suits a pointer sprite |
| Game port | USB card `$C5A0`–`$C5A5`: four paddles 0–255, three buttons | Apple-style joystick |
| Joysticks | Two digital DE-9 ports (Atari/Commodore pinout) on the serial card's W65C22S, `$C341` and `$C340` bits 0–4, 0 = pressed (proposed 2026-10-30) | One load reads a stick; no interrupt needed |
| Timing | Vertical blank (60 Hz), the scan-line interrupt, the DS3231's 1 Hz TICK (ID D), and the W65C22S's two timers at 7.16 MHz (proposed 2026-10-30) | Timer 1 is OODOS's system tick; timer 2 is the program's |
| DMA | Controller `$C400`, ID 8: four channels (0 storage, 1 serial, 2 USB / sound RAM, 3 general: copies and blits); memory to memory at 2 cycles a byte | OODOS's BIOS uses channels 0 and 3 today |
| Interrupts | IRQL/IRQH at `$C688`: one bit per module ID | OODOS enables none yet (phase 2: the console is polled) |


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

- **Load a file to physical memory** (tiles, pictures, a tune) by 3c's DMA segment: mostly built already.
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


## Open Questions ##

1. **Portability.** Do these services keep CP/M's rule that a program asks what exists (versioned services, a
   program that checks), or may OO-1 programs simply assume the hardware?
2. **Calls into OODOS.** New function numbers one per service (75 on, as 72–74), or one function with a service
   number and a parameter block (as 50 does for the BIOS)?
3. **The hooks' rules** (frame, tick, scan line, timer 2). Which of the program's windows are mapped while one
   runs, how long each may take (a budget in cycles: the tick hook's must be far smaller than the frame hook's),
   whether it may call OOLIB, whether it gets a zero-page slice of its own, and the VIA rule (never ACR, IER or
   IFR).
4. **Keys during a game.** May a game turn off the console's handling of ^C, ^S and ^P (CP/M 3's console mode,
   function 109, already has bits for this), and should the event queue then get those keys instead?
5. **The music format.** Our own tune format for the driver (notes, instruments, effects), or an existing tracker
   format (as long as it's free to use)? A converter from MIDI or from a tracker on Linux?
6. **What a program may own outright.** DMA channel 1 or 3 for a streaming effect, timer 2, the user port, the
   SIDs directly: which may a program claim for itself (and the warm boot take back), and which only through
   OODOS's calls or queue?
7. **A hardware timer.** *Answered (proposed 2026-10-30): a W65C22S on the serial card*
   ([design.md](design.md#the-via-and-the-joystick-ports)): timer 1 is OODOS's system tick, timer 2 and the ports are
   the program's, and programs may hook the tick (with question 3's rules). The rate is question 11.
8. **Asset formats and tools.** Which formats, and which Linux tools (a PNG to tiles converter, a palette tool, a
   map editor's export) come with OOLIB?
9. **Where OOLIB's code lives.** Linked into each program (as proposed), or also kept in an allocated block and
   reached by FARCALL for programs short of TPA space (GW-BASIC is)?
10. **The console during a game.** May a game use the text layer for its own status line while the console's
    output goes elsewhere, or does any console output put the console's text screen back?
11. **The tick rate.** Fixed at 1 kHz (about 1.4% of the CPU before any hook), or settable by a program within
    limits (say 100 Hz to 4 kHz), which every other user of the millisecond counter then has to allow for?
12. **The controller model.** How many controllers the event area has; how the hardware is numbered by default (the
    two digital sticks as 1 and 2, the Apple joystick as 3?); whether a game or the user (a setting kept on the card)
    picks which is which; and the Apple joystick's thresholds for directions.

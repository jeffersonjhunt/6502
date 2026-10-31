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
  interrupting: video, USB, sound mailbox, DMA terminal count, the clock's TICK. It is the piece everything else in
  layers 1 and 2 needs, so it is built first.
- **The frame counter:** counted at each vertical blank, in the event area (32 bits: about 2.3 years).
- **Wait for the next frame:** a call that returns at the next vertical blank (or the program polls the counter).
- **A frame hook:** a routine of the program's that OODOS calls at each vertical blank, for music cues, palette
  fades and scrolling. It runs in an interrupt, so it has rules (open question 3): which windows are mapped, how
  long it may take, no BDOS calls, none of the program's zero page unless it says so.
- **A scan-line hook:** the same at a chosen line, for split screens (a status bar over a scrolling map).
- **Sprite collision:** the collision groups at the last vertical blank, in the event area, and optionally a hook.

### Timing ###

- **Ticks:** the frame counter is the clock for games: 1/60 s.
- **Seconds and the date:** CP/M 3's function 105 and the SCB, as now.
- **Finer than a frame:** the W65C22S's timer 1 as OODOS's system tick (proposed: 1 kHz), for `ON TIMER`, music
  tempo off the frame rate and short delays; timer 2 for a program's own one-shot. The scan-line interrupt (a line
  is about 31.8 µs) still serves raster effects.
- **Delays:** "wait n frames", built on the frame counter.

### Input ###

- **An event queue** in the event area: key press and release (USB HID usage, with the modifiers), mouse movement
  and buttons, game port buttons, a device plugged in or out. One format for every kind of event, with the frame
  count of each.
- **Keys held now:** a 256-bit map (32 bytes) by HID usage, kept from the raw events, so `IF key held` is one
  load and an AND.
- **The joystick and paddles:** read at each vertical blank into the event area (paddles 0–255, buttons), and the
  two digital joysticks as one byte each (a game may also read them itself from the VIA, one load). GW-BASIC's STICK
  and STRIG take either.
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

### DMA ###

- **Channels have owners.** OODOS keeps channel 0 (storage). Channel 3 (copies and blits) is shared: OOLIB and the
  BIOS both use it, so a call takes it and gives it back, or OODOS queues copies for it. Channel 2 loads the sound
  card's RAM. Which channels a program may own outright is open question 6.
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
setup:  OODOS: open the event area (address, size); set the frame hook
        OOLIB: set layer 0 to tiles, layer 1 to text; load tiles, map, palette, sprites
        OODOS: start the music driver; play tune 1
loop:   read the event area: frame counter, keys held, joystick, events
        OOLIB: move sprites in the shadow table, put map entries, scroll
        OODOS: wait for the next frame (the sprite table goes to the card then)
end:    RTS: the warm boot stops the music, the hooks and DMA, and puts the console back
```


## GW-BASIC on these services ##

| GW-BASIC | Service |
|---|---|
| SCREEN, COLOR, PALETTE, CLS, WIDTH | OOLIB modes and palette; the console's text screen |
| PSET, PRESET, LINE, CIRCLE, PAINT, DRAW, GET/PUT (graphics), POINT | OOLIB bitmaps |
| SOUND, PLAY, BEEP | the music driver; the speaker |
| INKEY$, INPUT$ | the console (function 6) |
| ON KEY, KEY(n) | the event queue, keys held |
| STICK, STRIG, ON STRIG | the game port in the event area |
| ON TIMER, TIMER | the frame counter and the frame hook |
| ON PLAY | the music driver's sync points |
| DEF SEG, PEEK, POKE, BLOAD, BSAVE | physical memory by DMA (3c), video banks included |


## How it fits OODOS ##

- **A program asks first.** Function 72 (or a new one) reports which services and which OOLIB interface version
  OODOS has, so a program written for later services can say so instead of crashing.
- **RSXs** (OODOS phase 5) see the services like any program; an RSX that hooks the frame shares the dispatcher's
  list.
- **The warm boot** stops everything a program started (hooks, the music driver and any tune, DMA channels it
  took, the pointer sprite), empties the event queue, closes the event area, and puts the video card back to the
  console's text screen and font.
- **Apple II mode** is untouched: these are native-mode services.
- **Phasing (proposed):** a new OODOS phase before GW-BASIC (between 6 and 7, or before 6), in steps:
  1. the interrupt dispatcher, the frame counter and the event area;
  2. input: the event queue, keys held, the joystick and mouse;
  3. sound: the music driver, `.sid` playback, the speaker;
  4. OOLIB graphics and the sprite upload;
  5. a demo game of ours, which tests it all together and goes on the website's card (phase 8).


## Open Questions ##

1. **Portability.** Do these services keep CP/M's rule that a program asks what exists (versioned services, a
   program that checks), or may OO-1 programs simply assume the hardware?
2. **Calls into OODOS.** New function numbers one per service (75 on, as 72–74), or one function with a service
   number and a parameter block (as 50 does for the BIOS)?
3. **The frame hook's rules.** Which of the program's windows are mapped while it runs, how long it may take (a
   budget in cycles), whether it may call OOLIB, and whether it gets a zero-page slice of its own.
4. **Keys during a game.** May a game turn off the console's handling of ^C, ^S and ^P (CP/M 3's console mode,
   function 109, already has bits for this), and should the event queue then get those keys instead?
5. **The music format.** Our own tune format for the driver (notes, instruments, effects), or an existing tracker
   format (as long as it's free to use)? A converter from MIDI or from a tracker on Linux?
6. **DMA channels.** May a program own channel 1 or 3 outright for a streaming effect, or does everything go
   through OODOS's queue?
7. **A hardware timer.** *Proposed answer (2026-10-30): a W65C22S on the serial card* ([design.md](design.md#the-via-and-the-joystick-ports)),
   timer 1 OODOS's system tick, timer 2 and the ports for programs. Left to settle: the tick's rate (1 kHz
   proposed) and whether programs may hook it as they hook the frame.
8. **Asset formats and tools.** Which formats, and which Linux tools (a PNG to tiles converter, a palette tool, a
   map editor's export) come with OOLIB?
9. **Where OOLIB's code lives.** Linked into each program (as proposed), or also kept in an allocated block and
   reached by FARCALL for programs short of TPA space (GW-BASIC is)?
10. **The console during a game.** May a game use the text layer for its own status line while the console's
    output goes elsewhere, or does any console output put the console's text screen back?

# 8 Game services (OOLIB)

The services a game or GW-BASIC calls for the screen, sound, timing and input: OOLIB. They come in steps, L1 to
L6. This chapter has what is built. **L1, the clocks**: a millisecond counter, a frame counter, a block of the
program's memory OODOS keeps them in, waiting for a frame, and routines of the program's that OODOS calls at each
frame or every few milliseconds; that is [function 80](07-function-reference.md#bdos-80). **L2, input**: the
keyboard's every press and release in a queue, the keys held, four game controllers, the paddles and the mouse,
kept in that same block; and [function 81](07-function-reference.md#bdos-81) for the rest. **L3, sound**: the sound
card as a device (revision 3, since OODOS 0.17): its tune player, its stream and its music, each claimed by one
program at a time; tunes, samples, effects and note blocks loaded by DMA; a stream fed from two buffers of the
program's while it plays; the beep, the mix and the EQ; and OOLIB's linked library for `.sid` tunes, music and
playback, which PLAYSID uses; that is [function 82](07-function-reference.md#bdos-82). **L4, graphics**: files loaded
into video memory by DMA, copies and fills, copies at the vertical blank and the mouse's pointer, which is [function
83](07-function-reference.md#bdos-83), and OOLIB's linked graphics library, its asset files and VIEW.

The design record, for the reasons: [hw/oolib.md](../../hw/oolib.md#goals), and its plan, step by step:
[hw/oolib.md, "The plan"](../../hw/oolib.md#the-plan).

## Asking what there is

A program that uses these services asks first, once: function 80's `FT_INFO`, with X = 0.

```
        lda     #FT_INFO
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS            ; A = OOLIB's interface version, X = the service areas
```

A is OOLIB's interface version: 1 from L1, 2 from L2, 3 from L3, 4 from L4, 5 from L4b (function 82's hold, go and volume), 6 from L5a (the mailbox's stamp), 7 from OODOS 0.8 (`EV_SFLAGS`; the hold, the stamp and `EV_SFLAGS` went at 18), 8 from OODOS 0.9 (the video card: function 83's card operations and the W1 contract on the aperture, [below](#video-memory)), 9 from OODOS 0.10 ([function 77](07-function-reference.md#bdos-77), the ROM BIOS's version: from 9 the number marks OODOS's own additions a program needs to detect too), 10 from OODOS 0.10 ([function 75](07-function-reference.md#bdos-75)'s A = 2, the ROM's diagnostics), 11 from OODOS 0.11 (function 75's A = 3, Apple II mode with ROM A's image X), 12 from OODOS 0.11 (function 83's `DM_BANK8` and WIN1H in the W1 contract, [below](#video-memory)), 13 from OODOS 0.12 (function 83's `DM_VFILLW` and the 640x480 colour layers), 14 from OODOS 0.13 (the full-resolution pointer), 15 from OODOS 0.14 (nothing a program uses), 16 from OODOS 0.15 ([function 85](07-function-reference.md#bdos-85), the serial line's receive buffer, and the event area's `EV_RXCNT`, [below](#the-serial-line)), 17 from OODOS 0.16 ([function 78](07-function-reference.md#bdos-78), the random source, `FC_T1` and `FT_CLOCK`), 18 from OODOS 0.17 (sound card revision 3: function 82 numbered afresh, [below](#sound)); each step that adds to the services
raises it. X has a bit for each service area OODOS has, one BDOS function each from 80: `OA_FRAME` (`$01`, 80, frame
and timing), `OA_INPUT` (`$02`, 81, input), `OA_SOUND` (`$04`, 82, sound), `OA_DMA` (`$08`, 83, DMA and loading)
and `OA_SERIAL` (`$20`, 85, the serial line, since interface 16) now; `OA_VIDEO` (84) isn't needed (the program has its video memory whole) and stays unassigned. An OODOS without the services answers 80 as a
function it doesn't have, A = X = `$FF`, so a version of `$FF` means none. A program that needs what isn't there
says so and ends, rather than calling it. [Function 72](07-function-reference.md#bdos-72) says OODOS's own version,
as before.

Whether a card is there is another question, the program's own, by the card's ID byte, as the BIOS asks. `FT_INFO`
with a block says which clocks run: the frame counter needs the video card, the millisecond counter the serial
card's VIA.

## Calling function 80

Function 80 takes an operation and, for most, a few parameters. With **X = 0**, A is the operation and there are
none: `FT_INFO`, `FT_COUNT`, `FT_CLOSE`, a plain `FT_WAIT`, and taking a hook off. Otherwise **A/X is a block**, its
+0 the operation and the parameters after it, as [function 50](07-function-reference.md#bdos-50)'s block has the
BIOS function first. So a block can't be in page 0 of the program's memory, which zero page has anyway. A refused
call returns A = X = `$FF`. [The function's entry](07-function-reference.md#bdos-80) has every operation.

Function 80 is for setting things up and for waiting. What a game reads every frame it reads from the event area,
with ordinary loads: no call at all.

## The clocks

Two counters, 32 bits each, low byte first, from the cold boot on:

- **The millisecond counter** is the north bridge's own (revision 3, which OODOS 0.16 needs): a 1 kHz tick from
  the master oscillator, a millisecond exactly over every 11, 7,159 or 7,160 cycles at 7.16 MHz. It is the clock for
  anything not tied to the picture: timers in a game's logic, timeouts, sound effects played by the CPU. It runs
  for about 49 days before it goes round. (Before OODOS 0.16 it counted timer 1 of the serial card's VIA.)
- **The frame counter** counts the video card's vertical blanks, 59.94 a second. It is the clock for anything
  drawn. It runs for about 2.3 years.

Both run whatever the program does, in a BDOS call or out of one: the BDOS runs with interrupts on. The rate is
fixed, so a millisecond is always one count. **Timer 1 of the VIA is the program's** since OODOS 0.16 (interface 17:
`FT_INFO`'s `FC_T1`), polled: its counter, its latches, ACR's bits 7–6 (keep the rest of ACR as you read it) and its
IFR flag, never its IER bit, which stays OODOS's; a timed call goes through the tick hook instead.

**The time of day to the millisecond.** `FT_CLOCK` (interface 17) fills a block with the date and time as
[function 105](07-function-reference.md#bdos-105) gives them, the seconds, and the milliseconds into that second,
all from one latch of the clock, and two flags: `FK_SYNC`, the milliseconds are in step with the clock chip's second,
and `FK_LOST`, the clock chip's time is invalid ([function 80](07-function-reference.md#bdos-80)).

**On O: and P:** the Disk II driver turns interrupts off for each sector it reads or writes, about 12 ms, because
the CPU times the disk's bits itself. The counter is the bridge's, so it loses nothing: the ticks that came
meanwhile are taken as one at the end, and an open event area's counter is written whole then. A sector takes less
than a frame, so no frame is missed.

**What the clocks cost** (OODOS 0.16, measured). Each tick is an interrupt: 142 cycles with no event area and no
hooks, 2% of the CPU, in every program (162 with OODOS 0.15's VIA tick); a frame is 231. These include the
dispatcher's 17 cycles for WIN1H, where WIN1H is 0 and nothing needs clearing; while a program that called
`GX_BANK8` has slot 1 in W1, WIN1H `$0E`, the dispatcher clears it and puts it back, 10 cycles more on every
interrupt. An open event area of layout 1 makes a tick 188, one of layout 2 (input) 241, and each hook adds its own
time ([the input's cost](#what-input-costs)).

`FT_COUNT` reads both counters at one moment: A/X has the millisecond counter's low word, and a block gets all 8
bytes. To wait, there are two calls, both with interrupts on, so the hooks keep running:

- **`FT_WAIT`** returns at the next vertical blank, or after the count of frames in its block's +1. A/X is the
  frame counter's low word then. A game that draws in a loop calls it once a round, so it draws once a frame.
- **`FT_DELAY`** returns after the count of milliseconds in its block's +1–2, up to 65,535, by the bridge's
  counter, which can't be stopped. (OODOS 0.15 and before gave up with A = X = `$FF` if timer 1 had been stopped.)

## The event area

A program gives OODOS a block of its own memory, the **event area**, and OODOS's interrupt handlers write the
counters into it. Then the program reads them with a load: `LDA area + EV_FRAMES` is the frame counter's low byte,
current to the last frame.

```
        lda     #<open
        ldx     #>open
        ldy     #O_FRAME
        jsr     BDOS            ; A = 0: open
        ...
open:   .byte   FT_OPEN
        .word   area
        .byte   EV_LEN          ; its size: 16 at least
        ...
area:   .res    EV_LEN
```

| Offset | Name | What |
|---|---|---|
| +0 | `EV_LAYOUT` | the layout OODOS writes: 1 |
| +1 | `EV_CLOCKS` | the clocks that run (`FC_FRAME`, `FC_MS`) |
| +2 | `EV_SIZE` | the size the program gave |
| +3 | | reserved, 0 |
| +4–7 | `EV_MS` | the millisecond counter |
| +8–11 | `EV_FRAMES` | the frame counter |
| +12 | `EV_VBGO` | the vertical-blank copies' go bits (L4, [below](#the-vertical-blank-slots)), 0 at the open |
| +13 | `EV_RXCNT` | the serial line's bytes received (COM1's) while the area is open, modulo 256, 0 at the open (interface 16, [below](#the-serial-line)) |
| +14 | `EV_RXCNT2` | kept for a second port, 0 |
| +15 | | reserved, 0 |

- **The area must** be at least 16 bytes (`EV_LEN`; up to 256, given as 0), in one of W0–W2's 16 KB windows
  without crossing into the next, and in the program's own RAM: its blocks 0–2, or a block it allocated in banks 1–5 and mapped. OODOS writes
  it through the block that window had at the call, so a program that maps another block there later doesn't move
  the area.
- **At the open** OODOS writes all of it: the layout, the clocks, the size, the counters, and 0 everywhere else, up
  to the size given. An area already open is closed first.
- **Layouts grow at the end.** L2's layout 2 adds the input's (the event queue, the keys held, the controllers)
  after +16, for an area of 144 bytes (`EV_LEN2`) or more ([below](#input)). A program that gives less gets layout
  1's, and zeros past it. The layout byte says which it got.
- **Reading a counter whole.** A tick can come between two loads, so a 4-byte value read a byte at a time may be
  torn. Read the low byte alone when that is enough, or read the value twice and take it when the two agree, or use
  `FT_COUNT`.
- **`FT_CLOSE`** stops the writes. The warm boot closes it too.

## Hooks

A **hook** is a routine of the program's that OODOS calls from its interrupt handler. There are two:

- **The frame hook**, at each vertical blank: for music cues, palette fades, scrolling. `FT_FRAMEHOOK`, its block's
  +1–2 the routine.
- **The tick hook**, every *n* milliseconds: for input sampling, `ON TIMER`, effects. `FT_TICKHOOK`, its block's
  +1–2 the routine and +3–4 *n*, 1 at least.

An address of 0, or the call with X = 0, takes the hook off. A hook ends with RTS. OODOS has saved A, X, Y and the
flags, and puts back the windows and the stack it found.

**The rules a hook keeps** ([hw/oolib.md](../../hw/oolib.md#open-questions), question 3):

- **Its windows** are the ones the program had when it set the hook, all four: OODOS puts them on before the call
  and puts the interrupted ones back after. The program's zero page is the hook's too, to share as it likes.
- **A hook that maps W1 through OOLIB** (`GX_VMAP`, `GX_BLOCK`) **ends with `GX_W1BACK`** (since OODOS 0.11, B7). The
  library's note of what W1 holds is shared with the program, and after the hook it still names the hook's map,
  although OODOS has put the program's W1 back; without `GX_W1BACK` the program's next map of the same bank or
  block would be skipped and its writes would go to the wrong block. `GX_W1BACK` writes block 1 whatever the note
  says and leaves the note at block 1. **After `GX_BANK8`, a hook must not map W1 through OOLIB at all:** the note
  can then say WIN1H 0 while OODOS has put the program's `$0E` back, the hook can fire between any two of the
  program's instructions, so no order of the program's calls protects it, and a hook's `GX_VMAP` moves slot 1's
  bank (`VID_SLOT1`), which nothing puts back. A `GX_BANK8` program draws from its main loop.
- **Its stack** is page 1 of its own W0 block, under whatever is in use there: under the program's stack, or, when
  the hook interrupts a BDOS call, under the program's stack as it was at the call. A hook pushes what it needs and
  pops it again, as any routine does. That page 1 is the W0 block's the hook was *set* with, whatever W0 has when
  it runs, while "under the program's stack" is reckoned by the S of the moment: so set the hooks with the W0 they
  will run under. A program that changes W0 later and keeps its hooks would have them push into the other block's
  page 1 wherever S points, over what that page holds.
- **Budgets: 8,000 cycles a frame hook** (about 7% of a frame), **300 a tick hook** (it may run 60 times as often).
  They aren't enforced. A debug build of OODOS.SYS (`make -C oodos/sys DEBUG=1`) times each hook by the north
  bridge's counter (MS0 and TBSUB), and
  `FT_COSTS` says the most each took: from the dispatcher's call to its return, so about 130 cycles more than the
  hook's own, and for a frame hook the ticks taken under it.
- **The frame hook runs with interrupts on,** so the ticks that come while it runs are counted (its budget is
  longer than a tick). A frame that comes while the frame hook is still running is counted, but the hook isn't
  called again for it; `FT_COSTS` counts those. **The tick hook runs with interrupts off, and must leave them
  off:** no `CLI`. With them on, the next tick could come while it runs and call it again, on top of itself.
- **Not allowed:** a BDOS call, DMA, a far call (FARCALL's operand is one slot in common memory, shared by every
  block, so a far call from a hook would spoil one under way), a write to the VIA's IER or IFR or to ACR's bits but
  timer 1's, and the north bridge's timebase, which are OODOS's. Since OODOS 0.16 timer 1 is the program's, so a
  hook may poll it as the program does. OOLIB's routines that only touch registers may be called once there are
  some (L4).

## Input

An event area of 144 bytes or more (`EV_LEN2`) is **layout 2**: the clocks as layout 1 has them, then the input.
While it is open OODOS takes the USB card's interrupt, samples the joysticks at every tick and reads the paddles
every frame, and keeps all of it in the area:

```
open:   .byte   FT_OPEN
        .word   area
        .byte   0               ; 256 bytes: the queue has 16 slots
        ...
area:   .res    256
```

| Offset | Name | What |
|---|---|---|
| +0–15 | | layout 1's: +0 is 2 |
| +16 | `EV_QHEAD` | the queue's head: the slot OODOS writes next (OODOS's) |
| +17 | `EV_QTAIL` | the queue's tail: the slot the program reads next (the program's) |
| +18 | `EV_QSLOTS` | the queue's slots: (size − 128) / 8 |
| +19 | | reserved, 0 |
| +20–21 | `EV_QLOST` | events lost to a full queue (the program may clear it) |
| +22–23 | | reserved, 0 |
| +24 | `EV_MODS` | the modifiers held: L-Ctrl, L-Shift, L-Alt, L-GUI in bits 0–3, then the right ones |
| +25 | `EV_DEVICES` | what is plugged in: `ED_KEYBOARD` `$01`, `ED_MOUSE` `$02`, `ED_GAMEPAD` `$04` |
| +26–31 | | reserved, 0 |
| +32–33 | `EV_MX` | the mouse's X |
| +34–35 | `EV_MY` | the mouse's Y |
| +36 | `EV_MBUTTONS` | its buttons, bits 0–4 |
| +37 | `EV_MWHEEL` | its wheel's steps, signed, summed until the program clears it |
| +38–39 | | reserved, 0 |
| +40–55 | `EV_CTL` | four controllers, 4 bytes each, controller 1 first |
| +56–59 | `EV_PADDLES` | the game port's four paddles, 0–255 |
| +60–63 | | reserved, 0 |
| +64–95 | `EV_KEYS` | the keys held, a bit for each HID usage |
| +96–127 | `EV_SOUND` | the sound part ([below](#the-event-areas-sound-part)): the stream's, the tune's and music's, each 0 unless its part is claimed |
| +128 | `EV_QUEUE` | the queue: 8 bytes a slot, as many as the size leaves |

### The queue

Every key pressed and every key let go, a mouse button, a turn of the wheel, a controller's change, a device
plugged in or out and the sound card's news is an **event**, 8 bytes, in a ring of slots from +128:

| Offset | Name | What |
|---|---|---|
| +0 | `EQ_KIND` | `EK_KEY` 1, `EK_BUTTON` 2 (the mouse's), `EK_WHEEL` 3, `EK_CTL` 4 (a controller), `EK_DEVICE` 5, `EK_SOUND` 6 (an event of the sound card's: [below](#the-cards-events)) |
| +1 | `EQ_CODE` | the key's HID usage; the button, 0–4; the controller, 1–4; the sound card's event kind (`SK_*`); else 0 |
| +2 | `EQ_VALUE` | 1 pressed, 0 let go; a controller's new bits; the wheel's steps, signed; the devices now; the sound event's object |
| +3 | `EQ_MODS` | the modifiers held then (a modifier's own event has itself in) |
| +4–7 | `EQ_MS` | the millisecond counter then |

OODOS writes an event in the slot at the head and then moves the head on; the program reads the slot at the tail
and moves the tail on. When they are equal the queue is empty. One slot always stays empty, so a 256-byte area's
16 slots hold 15 events; an event that would fill the last is lost, whole, and `EV_QLOST` counts it. A program
reads its events with loads:

```
next:   ldx     area + EV_QTAIL
        cpx     area + EV_QHEAD
        beq     none            ; nothing waits
        txa
        asl     a
        asl     a
        asl     a
        tay                     ; the slot's offset from EV_QUEUE
        lda     area + EV_QUEUE + EQ_KIND,y
        ...                     ; and its code, value, modifiers, time
        inx                     ; then the tail on, round at EV_QSLOTS
        cpx     area + EV_QSLOTS
        bcc     @t
        ldx     #0
@t:     stx     area + EV_QTAIL
```

Or it calls function 81's `IN_NEXT`, which copies the next event to a block and moves the tail itself. `IN_FLUSH`
empties the queue.

### The keyboard

The **keys held** are a bit each, by HID usage: usage *u* is bit *u* AND 7 of byte +64 + *u* / 8, so "is Space
held?" (usage `$2C`) is `LDA area + EV_KEYS + 5`, `AND #$10`. The modifiers are there too, as usages `$E0–$E7`,
and in +24 as a byte. A key held when the area was opened isn't in the map until it is let go and pressed again; its release is still
queued.

Raw events never repeat. The console's **key repeat** (the card's REPEAT: [the Technical Reference](../oo1-technical-reference/08-usb.md#reg-c586))
is for the cooked keys, and function 81's `IN_REPEAT` sets it; the card has no "off", but a game that reads the
queue has no repeats to turn off. `IN_LEDS` sets the keyboard's three lights.

**The console's keys go on as before.** Functions 1, 6, 10 and the rest still read the card's cooked keys, so a
program that reads both sees each key twice, once cooked and once in the queue, and uses one or the other. Every
key reaches the queue, ^C, ^S and ^P too; whether they also act on the console is function 109's console mode
([question 4](../../hw/oolib.md#open-questions)). A game that doesn't want ^C to end it, or ^S to stop it, sets
bits 3 and 1 (`$000A`) first, and puts the mode back before it ends. Break (Pause) stops any program whatever
the mode.

### The controllers

Four **controllers**, 4 bytes each from +40, so a game asks for controller 1 and works with whatever is plugged in:

| Offset | Name | What |
|---|---|---|
| +0 | `EC_BITS` | `CB_UP` `$01`, `CB_DOWN` `$02`, `CB_LEFT` `$04`, `CB_RIGHT` `$08`; the fire buttons from `CB_FIRE` `$10` up |
| +1 | `EC_X` | 0–255: 0 left, 255 right (a digital stick: 0, 128 or 255) |
| +2 | `EC_Y` | 0–255: 0 up, 255 down |
| +3 | `EC_HW` | the hardware it is: `CH_STICK1` 1, `CH_STICK2` 2, `CH_GAMEPORT` 3, `CH_GAMEPAD` 4 |

- **Controllers 1 and 2** are the DE-9 joysticks on the serial card's VIA, ports A and B
  ([the Technical Reference](../oo1-technical-reference/09-serial-and-via.md#the-joysticks)), with one fire
  button. OODOS samples them at every tick, so a press shorter than a frame is seen, and a change is an event.
- **Controller 3** is the USB card's game port: its X and Y are paddles 0 and 1, its directions where they are past
  64 and 192 (below 64 left or up, 192 or more right or down: `GP_LOW`, `GP_HIGH`), its three buttons fire bits
  4–6. The paddles are read every frame: OODOS starts them at the frame's first tick and reads them 4 ms later.
  So they need the video card's frames (and the VIA's ticks): without them the paddles and controller 3's
  directions stay as the open found them, though its buttons still change.
  The four paddles themselves are at +56 for a game that wants paddles, not a joystick. A button change is an
  event at once (the card's interrupt); a change of direction an event at the frame it is read.
- **Controller 4** is kept for a USB gamepad: nothing yet, its bits 0, X and Y 128.
- **Swapping:** function 81's `IN_SWAP` swaps two controllers' hardware (a two-player game that wants the game
  port as player 1), and `IN_MAP` says which is which. The warm boot puts them back, 1 to 4 in order.

A game may still read a digital stick itself, `LDA $C341` and `AND #$1F`, as the hardware allows.

### The mouse

Its position is in the area, always current: in **relative mode**, the cold boot's, X and Y are the movement
summed since the area opened (16 bits, going round), so a game takes the difference from the last it read; in
**absolute mode** they are the card's position, within limits a program sets. Function 81's `IN_MOUSE` sets the
mode and the limits, and starts X and Y afresh. A move is not an event (a queue of moves would fill); a button
pressed or let go is, one event a button, and so is each turn of the wheel, whose steps are summed in +37 too.

### What input costs

With a layout-2 area open each tick costs 249 cycles, against 162 with no area: 87 more, about 1.2% of the CPU.
Twice a frame a tick costs more, when it starts the paddles (382) and when it reads them (687), and a stick's
change is a little more again. The USB card's interrupt for a key is 679 cycles. Without a layout-2 area
OODOS samples nothing: a program that doesn't open one pays only the 162 a tick with no area. These include the
dispatcher's 17 cycles for WIN1H 0 (since OODOS 0.11, B7; 10 more while WIN1H is `$0E`, [as the clocks
say](#the-clocks)), measured at B7's final build.

**Reading a value whole.** As with the counters, an interrupt can come between two loads: read a 2-byte X twice,
or take only the low byte. A wheel cleared by the program with `STZ` can lose a step that came between its load
and the store.

**Sharing the FIFO.** While a layout-2 area is open the USB card's event FIFO is OODOS's: a program that reads it
itself (OO-Amp does, for now) gets what OODOS hasn't taken, and OODOS what it hasn't. Use one or the other. OODOS
also reads the VIA's port B at every tick, which clears its CB1 and CB2 flags.

## Sound

The sound card ([the Technical Reference, chapter 7](../oo1-technical-reference/07-sound.md)) is revision 3, a
device: two SIDs, a 6510 of its own for `.sid` tunes, an ES5503 for samples, a stream decoder for raw PCM and MP3, a
tone generator, and a store of 2 to 8 MB, all run by the card's firmware. A program never writes the card's
registers. It asks OODOS, through function 82, and OODOS sends the card command records and reads its replies and
events. The card does the playing: once a tune, a block of notes or an effect is loaded and started, it costs the
65C02 nothing.

This is OOLIB's interface 18 (OODOS 0.17), which numbered function 82 afresh. Before OODOS 1.0 a program ships with
the OODOS it was built for and checks no version: `FT_INFO` reports the interface, it isn't a gate.

**The parts.** Three things on the card are taken by one program at a time: **the tune player** (`SC_TUNE`, `$01`:
a `.sid` tune on the 6510 and the SIDs it holds), **the stream** (`SC_STREAM`, `$02`) and **music** (`SC_MUSIC`,
`$04`: the card's sequencer of note blocks). Function 82's `SN_CLAIM` takes the ones in its block's +1, all of them
or none: a part held already, by this program or an RSX, is refused. `SN_RELEASE` gives parts back, each stopped.
`SN_INFO` says the card's revision (3, or 0 for none) and its FEATURES, and with a block which parts are free. The
rest needs no claim: the sound bank and its effects, the beep, the volumes, the mix, the EQ and what the card shows
(the vis page, the levels, the SIDs' registers), and the status of each part. **DMA channel 2 is OODOS's**: a program
reaches it only through function 82's loads and its stream.

```
        lda     #<claim
        ldx     #>claim
        ldy     #O_SOUND
        jsr     BDOS            ; A = 0: the stream is ours
        ...
claim:  .byte   SN_CLAIM, SC_STREAM
```

**Loads.** A tune, samples, effects, note blocks and a firmware image go to the card from the program's memory by
DMA: the block gives a physical address (a program's own memory below `$C000` is its blocks 0–2, so its CPU address
is the physical one there; [function 73](07-function-reference.md#bdos-73) makes one of any) and a length. Each is
checked as [function 51](07-function-reference.md#bdos-51) checks a DMA segment: none of OODOS's own memory. The call
returns when the card has taken it all.

**What a call returns.** A = 0 and X = 0, or the operation's results. A = X = `$FF` for a call OODOS refuses itself
(a part not claimed, an address it won't send, no card). A = `SN_REFUSED` (`$FD`) with X the card's own error for one
the card refused: a full store is 4 (MEMORY), the chips in use 5 (BUSY), a file that doesn't parse 6 (DATA). A =
`SN_BUSY` (`$FE`) when the card didn't answer in 100 ms or channel 2 is busy with the stream: try again later. [The
function's entry](07-function-reference.md#bdos-82) has every operation and every byte.

### The card's events

The card says what happens as it happens: a tune began or ended, an effect ended, music reached a marker, a stream
ran dry or moved on to the next. With a layout-2 event area open, each reaches the queue as an `EK_SOUND` event:
`EQ_CODE` is the card's event kind (oodos.inc's `SK_*` names) and `EQ_VALUE` its object.

| Code | Name | The value |
|---|---|---|
| `$20` | `SK_TUNE_STARTED` | the tune slot: the tune's init has returned and its first play call came |
| `$21` | `SK_TUNE_STALL` | the tune slot: no play call for 2 s when one was due |
| `$22` | `SK_TUNE_JAM` | the tune slot: its 6510 stopped on a jam opcode |
| `$23` | `SK_TUNE_ENDED` | the tune slot: its length and fade ran out |
| `$30` | `SK_VOICE_DONE` | the voice handle: an effect ended |
| `$31` | `SK_VOICE_STOLEN` | the voice handle: its voice taken for a higher priority |
| `$40` | `SK_MUSIC_MARK` | the marker a voice reached |
| `$41` | `SK_MUSIC_COUNT` | 0: the notes waiting fell below the threshold |
| `$42` | `SK_MUSIC_DONE` | the last block's slot: the queued music has all played |
| `$51` | `SK_UNDERRUN` | the stream: its ring ran dry |
| `$52` | `SK_UNDERRUN_END` | the stream: sound again |
| `$53` | `SK_TRACK` | the stream now playing: the one before it ended and closed |
| `$54` | `SK_STREAM_ENDED` | the stream: it played out after `SN_END` |
| `$60` | `SK_BEEP_DONE` | 0: a beep or the bell ended |

The card's replies, its transfers' ends and its requests for stream data are OODOS's, and never reach the queue.
Without a layout-2 area the events are not kept: a program then reads each part's state by its status operation.

### The event area's sound part

While a part is claimed and a layout-2 area is open, OODOS asks the card for that part's status once a frame and
writes the answer into the area's +96–127. The answer comes during the next frame, so the fields are a frame
behind. `EV_SBUFS` alone is written at once, as each stream buffer is taken or sent.

| Offset | Name | What |
|---|---|---|
| +96 | `EV_SSTATE` | the stream's state: 0 idle, 1 buffering, 2 playing, 3 paused, 4 underrun, 5 ended; `$80` up an error |
| +97 | `EV_SBUFS` | the buffers free: bit 0 buffer 0, bit 1 buffer 1 |
| +98–99 | `EV_SFREE` | the ring's room, in 256-byte units (as the card's last request for data said) |
| +100–103 | `EV_SPOS` | the frames played |
| +104–105 | `EV_SKBPS` | an MP3's bitrate, kbit/s |
| +106–107 | `EV_SUNDER` | the underruns since the stream started |
| +108 | `EV_SNEED` | the flags of the card's last request for the stream's data: bit 0, `SNF_URGENT`, under 1 s was buffered |
| +109–112 | `EV_SBUFMS` | the ms the card had buffered at that request |
| +113–115 | | reserved, 0 |
| +116 | `EV_TSTATE` | the tune's state, as `SN_TSTATUS`'s +1 (0 stopped ... 6 ended), while the tune player is claimed |
| +117–119 | `EV_TMS` | its milliseconds played, paused time left out |
| +120 | `EV_MSTATE` | music's state, as `SN_MSTATUS`'s +1 (0 stopped, 1 playing, 2 paused), while music is claimed |
| +121 | `EV_MBLOCKS` | the blocks queued behind the one playing |
| +122–123 | `EV_MWAIT` | the notes waiting, every voice, not yet begun |
| +124–127 | | reserved, 0 |

A part not claimed leaves its fields as they were. One write doesn't wait for a frame: an `SN_PLAY` that succeeds
sets `EV_TSTATE` to 1 (starting, as the card has it then) and `EV_TMS` to 0 before it returns, so the state an
earlier tune ended in isn't read as the new one's; the card's own figures follow from the next frame. A refused
`SN_PLAY` leaves the fields as they were.

### Tunes

The card plays a C64 `.sid` file, PSID or RSID, on its own 6510 and SIDs, as a C64 would: the tune's code runs at a
C64's speed, with the chips where it expects them. It holds four tunes, in slots 0–3. With the tune player claimed:

- `SN_TUNE` loads a file into a slot: +1–3 its physical address, +4–6 its length, +7 the slot. The card checks the
  header as it comes; a file it can't play is refused with its error (6, DATA), and the slot is left empty.
- `SN_PLAY` plays a song of it: +1 the slot, +2 the song (0 the file's start song), +3 the clock (bit 0 PAL, bit 1
  NTSC; neither, the file's), +4–7 a length in ms (0: until stopped), +8–9 a fade over the end in ms. A tune takes
  the SIDs it uses; while music has them, `SN_PLAY` is refused (5, BUSY).
- `SN_TPAUSE` holds the tune where it is, its chips silent; `SN_TRESUME` lets it go on from there. `SN_STOP` stops it
  and silences its chips.

`SN_TSTATUS` (16 bytes) and `SN_TINFO` (a slot's header: its songs, its clock, its chips, its name, author and
released fields) need no claim. A tune that stops being called, or jams its 6510, is the card's to judge: the state
says stalled or jammed, and an event comes.

### .sid tunes

OOLIB's linked library reads a `.sid` file for the program and hands it to the card: `oodos/lib/sid.s`, linked from
`build/oolib.lib` (`include/oolib.inc` has its calls). `SID_PARSE` reads the header and fills the `SID_*` fields;
`SID_PLAN` checks a song against it; `SID_PLAY` claims the tune player, loads the file into slot 0 by `SN_TUNE` and
plays the song by `SN_PLAY`, at the clock `SID_FLAGS`' bits 2–3 name (01 PAL, 10 NTSC; 00 or 11, the file's: a program
may set them after `SID_PARSE` and before `SID_PLAN`, which names notes by the same bits); `SID_STOP` stops it and gives the player back. The file must be in one run of physical
memory (a `.COM`'s blocks 0–2 are). PSID and RSID files both play. The chips' model (a 6581 or an 8580) isn't the
program's to choose: a tune written for the other plays on the chips fitted, and IDENTIFY says which they are.
PLAYSID ([the User's Guide](../oodos-users-guide/05-utilities.md#util-playsid)) is a program on it.

### The stream

`SN_STREAM` starts a stream: +1 the codec (0 raw PCM, 1 MP3), +2–3 the rate and +4 the format (raw PCM's: bit 0
stereo, bit 1 16-bit). The card keeps a ring for it in its store and plays from it on its own clock. Then the
program gives OODOS its data a buffer at a time: `SN_BUFFER`, +1–3 a physical address and +4–5 a length (0 for 64
KB). OODOS keeps two, taken **in turn**, 0, 1, 0, 1 (A says which), and `SN_BUFFER` is refused with A = `SN_BUSY`
while the next one is still in use; so a program alternating two buffers of its own always has its first in 0 and
its second in 1. When the card asks for data and a buffer is waiting, OODOS sends it, by channel 2, from the
interrupt. As each is sent, its bit in the area's `EV_SBUFS` goes to 1: the program refills it then and gives it
again.

```
loop:   lda     #1              ; buffer k's bit: 1, then 2, in turn
        and     area + EV_SBUFS
        beq     loop            ; still going to the card
        ...                     ; read the file into buffer k, then SN_BUFFER it
```

The card starts playing once it has 100 ms of raw PCM, or 750 ms of MP3, buffered. `SN_PAUSE` and `SN_RESUME` hold
the stream and let it go; `SN_SEEK` empties its ring and drops both buffers, for data from a new place (the next
buffer is 0 again); `SN_END` says no more is coming, so what is in plays out, the state goes to ended and
`SK_STREAM_ENDED` comes. `SN_STATUS` reads the stream's state at one moment, and with the stream claimed and a
layout-2 area open the area has it each frame.

**The next track.** `SN_NEXT`, with the same block as `SN_STREAM`'s, opens a second stream queued behind the one
playing, and from then on `SN_BUFFER` feeds the new one. When the first ends, the second starts with no gap, the first
closes, and `SK_TRACK` comes; the status is then the second's. Give the first its `SN_END` before its last buffer
runs out, and the second its first buffers before the first ends. A `SN_SEEK` with the second queued closes the
second: the seek is in the track playing, and its buffers and `SN_END` go to it; call `SN_NEXT` again for the next.

**Keeping it fed.** Nothing in this needs the program's time while the ring and a buffer have data: a slow frame
costs nothing. An underrun is the ring running dry before `SN_END`; the card counts each one (`EV_SUNDER`) and says
when it starts and ends. A raw 48 kHz 16-bit stereo stream is 192 KB a second. Read from a file on the card, drive
B: included, that is within reach when the program reads it with [function 83's](07-function-reference.md#bdos-83)
`DM_LOAD`, which loads records into memory by DMA, straight into the buffer it then gives `SN_BUFFER`; the stream
test program does so, with no underrun.

**Urgent.** Each time the card asks for data it says how much it has buffered, and below 1 s it marks the request
urgent. OODOS keeps the last request's word, for the stream playing: `EV_SNEED`'s bit 0 (`SNF_URGENT`) and
`EV_SBUFMS` in the area, `SN_STATUS`'s +15 and +16–19. A program with other work to do (drawing, say) reads its
file first while the flag is set. The card asks again only once a buffer has answered it, so between requests the
values are what the last one said, not the ring's level now. The flag is cleared by `SN_END` (no more data is
coming), and both are 0 when a stream starts, when it is released or closes (after `SK_TRACK`, the first's), and at
the warm boot.

### The sound bank and effects

The card keeps samples and effects for a game's sounds, and plays them on voices it hands out, beside a tune, music or
a stream. None of it needs a claim.

- `SN_SAMPLE` loads a sample into one of 256 slots: +1 the slot, +2–4 the address, +5–7 the length, +8 the flags
  (bit 0: 8-bit signed, else unsigned; bit 1: keep `$00` bytes as the ES5503's stop markers, else they are made
  `$01`), +9–10 the rate it was recorded at, +11–13 and +14–16 the loop's start and end (0: no loop).
- `SN_EFFECTS` loads a table of effects, numbered from +1's on: each is +0 its length (the bytes after it), +1 its
  kind (0 a SID effect, 1 a sample effect), then its definition. A **SID effect** is a few steps (1–16) on a SID voice,
  each a length in ms, the waveform's bits, a frequency, a pulse width, the ADSR and a sweep of the frequency and the
  pulse width over it, with a step to loop back to or none. A **sample effect** names a sample slot, the rate to play
  it at, whether it loops, and an attack, a hold and a release in ms. The Technical Reference has the bytes
  ([chapter 7](../oo1-technical-reference/07-sound.md#the-commands)).
- `SN_EFFECT` plays one: +1 the effect, +2 the volume, +3 the pan, +4 the priority (0–3), +5–6 a pitch offset in
  cents, +7 the chip (0 any, 1 a SID, 2 the ES5503). A is the voice handle. When every voice is busy, one of a lower
  priority is taken (`SK_VOICE_STOLEN`); one of a higher is never. `SK_VOICE_DONE` says an effect ended.
- `SN_ESTOP` stops a voice by its handle, or every effect voice with 0.

The warm boot empties the store, so a program loads its bank when it starts.

### Music

The card's sequencer plays **note blocks**: a whole tune, or a whole PLAY statement, in one transfer, with lengths in
microseconds, 32 bits each. It holds 32 blocks, in slots 0–31, playing and queued. With music claimed, `SN_MUSIC`
loads a block into a slot and plays it: +1 the slot, +2–4 the block's address, +5–7 its length, +8 bit 0 queued
behind what plays (else at once), bit 1 the slot freed when it is done. `SN_MCTL` stops music (every queued block
dropped), pauses it, resumes it, or sets the threshold for `SK_MUSIC_COUNT` (+1 3, +2 n: the event when the notes
waiting fall from n to n − 1). `SN_MSTATUS` says music's state, its blocks queued, and the notes waiting on each
logical voice and on all of them; a note waiting hasn't begun, and the one sounding isn't counted.

A block is a header, an instrument for each voice it uses, and its entries:

| Part | Bytes | What |
|---|---|---|
| The header | 8 | +0 the version, 1; +1 the logical voices used, bit n voice n (0–7); +2–3 0; +4–7 the entries that follow |
| An instrument | 8 a voice | one for each voice in +1, in voice order: +0 the source (0 a SID voice, 1 a sample, 2 the tone generator); a SID voice's +1 waveform, ring and sync bits, +2 AD, +3 SR, +4–5 the pulse width; a sample's +1 slot, +2 the note it plays at its own rate, +3 1 looped; +6 the volume, +7 the pan; the rest 0 |
| The entries | 2–12 each | +0 the kind in the high nibble and the voice in the low; then by kind: 0 a note (+1 the note, 0–127, 69 the A at 440 Hz; +2–5 the length and +6–9 the gate, µs), 1 a frequency (+1–3 Hz × 16; +4–7 the length, +8–11 the gate), 2 a rest (+1–4 the length), 3 an instrument (+1–8), 4 a volume (+1), 5 a marker (+1, 0–254) |

Each voice keeps its own time, so the voices' entries may be interleaved. The gate is how long the note sounds before
its release, at most its length; equal is legato. The card checks the whole block as it comes and refuses one that
doesn't parse (6, DATA).

**The library.** OOLIB's music routines build the blocks for a program (`oodos/lib/mu.s`, `munote.s` and `mutone.s`;
`include/oolib.inc` has the calls), on six logical voices, for a program's music. (GW-BASIC's SOUND and PLAY build
their own blocks, one a statement, on the tone generator's voice, and use `MU_INIT`, `MU_BLOCK`, `MU_COUNT`,
`MU_THRESH`, `MU_STOP` and `MU_QUIT`.)

- `MU_INIT`: A the clock, 0 PAL or 1 NTSC. It claims music; every voice is a 50% pulse at full volume. `PE_REFUSED`
  without a card, or while music is another program's. `MU_QUIT` stops it and gives it back; the warm boot does too.
- `MU_NOTE`: A/X a block of 7 bytes (`MU_NLEN`): +0 the voice, 0–5; +1 the note, 0 (C-0) to 95 (B-7; A-4, 440 Hz,
  is 57), or `MU_REST`; +3–4 the length in ms (0 is 1); +5–6 the gate in ms (0 silent; the length or more, legato).
  PLAY's MN is 7/8 of the length, MS 3/4. `MU_TONE`: the same with +1–2 a frequency in Hz (0 a rest), exact.
  Each is sent at once, as a block of one entry, queued behind what plays.
- `MU_BLOCK` (interface 18): A/X a block of 4 bytes (`MU_BLEN`): +0–1 the address of a note block the program built
  in its own memory, +2–3 its length. It is sent whole and queued. `PE_REFUSED` if the card refused it.
- `MU_INSTR`: A/X a block of 6 bytes (`MU_ILEN`): +0 the voice, +1 the waveform's bits (noise, pulse, saw, triangle,
  ring, sync), +2 attack and decay, +3 sustain and release, +4–5 the pulse width, from the voice's next entry on.
  `MU_VOLUME`: A a SID (0, 1 or `MU_ALL`), X its volume, 0–15, for the voices that name it.
- `MU_MARK`: A a voice, X a marker, 0–14: `MU_EVENT` says `MV_MARK` when the voice reaches it, for a program's own
  sync points. `MU_THRESH` (interface 18): A n, the threshold, 0 none (GW-BASIC's ON PLAY(n) sets n - 1).
- `MU_STOP`: music stopped, every queued block dropped (the card has one queue). `MU_PAUSE` and `MU_RESUME`.
- `MU_EVENT`: A an `EK_SOUND` event's code, X its value: A = `MV_DONE` (the queued music has all played, X the last
  slot), `MV_MARK` (X the voice, Y the marker), `MV_COUNT` (the threshold met), or `MV_NONE` for an event that isn't
  music's. `MU_COUNT`: A a voice; A = its notes waiting (the card's count, exact), X = music's state.

Each call but `MU_EVENT` and `MU_COUNT` returns carry clear and A = 0, or carry set and A an error: `PE_STATE` before
`MU_INIT`, `PE_RANGE`, `PE_FULL` (all 32 slots playing or queued: a program that sends a note at a time can be
32 ahead), or `PE_REFUSED`.

### The beep

`SN_BEEP` sounds the card's tone generator, a clean square wave beside whatever else plays: +1–2 a pitch in Hz
(20–20,000), +3–4 a length in milliseconds. It needs no claim and returns when the beep has ended. The console's BEL
character rings the card's bell, its own short tone, and doesn't wait.

### The volume and the mix

`SN_VOLUME` sets the card's master volumes, +1 left and +2 right, 255 for unity: everything the card plays, the SIDs
too. `SN_MIX` sets one source's level: +1 the source (0 tunes, 1 music, 2 effects, 3 streams, 4 the beep), +2 its
level, 255 unity. So a game can turn its music down under its effects. Neither needs a claim, and the warm boot sets
them all back to 255.

```
        lda     #<vol
        ldx     #>vol
        ldy     #O_SOUND
        jsr     BDOS            ; A = 0: both at half
        ...
vol:    .byte   SN_VOLUME, 128, 128
```

### The EQ and the vis page

The card's ten-band EQ shapes everything it plays, and its vis page shows it. Function 82 has them as `SN_EQ` and
`SN_VIS`, and the linked library as routines, with nothing to share, so there is no claim: a program may set the EQ
before its stream starts. Two modules, so a program that only draws a meter doesn't link the EQ:
`oodos/lib/pbeq.s` and `oodos/lib/pbvis.s`.

- `PB_EQ`: A the band (0 the preamp, 1–10 the bands at 60, 170, 310 and 600 Hz and 1, 3, 6, 12, 14 and 16 kHz),
  X its gain in signed half-dB, −24 to 24 (−12 to +12 dB). The enable and the other gains are kept.
- `PB_EQON`: A 0 turns the EQ off (the sound passes bit for bit), anything else on. The gains are kept.
- `PB_EQALL`: A/X 11 bytes, the preamp's gain and then the ten bands', for a stream's start. Every gain is checked
  before any is written.
- `PB_VIS`: A/X a buffer, Y a length, 1–98 (0 means 98). The card takes a snapshot, and its first Y bytes are
  copied in: +0–31 the spectrum's bands, +32–33 the VU pair (both 0–255 for −60 to 0 dBFS), +34–97 the last 64
  samples (`$80` is zero). A meter takes 34.

Each returns carry clear and A = 0, or carry set with nothing written and A = `PE_NOCARD` (no card OODOS drives) or
`PE_RANGE` (a band past 10, a gain past ±24, a length past 98). The warm boot turns the EQ off with every gain 0
([When the program ends](#when-the-program-ends)).

### The SIDs' voices

The linked library reads what the SIDs are doing, for a display: the card keeps both chips' registers as last
written, by a tune, music or an effect, and `SN_SIDREGS` copies them out. The library only reads. Three modules,
`oodos/lib/sidv.s`, `sidnote.s` and `sidenv.s`, so a program links the ones it calls.

- `SID_VOICES`: A/X a block of 50 bytes (`SID_BLOCK`), SID A's 25 registers (`$00`–`$18`) and then SID B's. Voice
  v, 0–5 (A's three, then B's), has 7 bytes from `SID_VOFF`'s byte v: +0–1 the frequency, +2–3 the pulse width, +4
  the control (bits 7–4 noise, pulse, saw, triangle; bit 0 the gate), +5 attack and decay, +6 sustain and release.
  A SID's +21–24 are its filter's cutoff, resonance and routing, and its mode and volume.
- `SID_NOTE`: A/X a voice's frequency, Y the clock (`SID_CTRL`'s bit 0, NTSC): A = the nearest note, 0 (C-0) to 95
  (B-7; A-4, 440 Hz, is 57), or `SID_NONE` (`$FF`) for a frequency of 0. Noise has no note: test the control's bit 7
  first.
- `SID_ENV0`, then `SID_ENV` once a frame with the frame's `SID_VOICES` block: `SID_LVL` has the six voices' levels,
  0–255, moved as the chip would move them from the gate and the ADSR. It is an estimate for a meter, a frame's step a
  call: a frame missed isn't made up. `PB_LEVELS` has the card's own measure. Call `SID_ENV0` for a new tune or song.

None of them fails: each returns carry clear.

### Playback

The linked library has the verbs any player uses, whichever part is sounding: pause and resume, volume and balance,
how long it has played, whether it still plays, and how loud it is now. The source is a number: `PB_SID` (a tune),
`PB_STREAM`, `PB_MUSIC` (interface 18); `PB_DOC` is refused (`PE_REFUSED`: the ES5503 is the card's). The program
starts the part itself (`SID_PLAY`, `SN_STREAM`, `MU_*`) and then calls `PB_BEGIN` with its source. Four modules:
`oodos/lib/pb.s` (begin, pause and resume), `pbstat.s` (the status), `pbvol.s` and `pblev.s`.

- `PB_INIT`: A/X the event area (0 for none). It is kept, but the card now judges a tune's stall itself.
- `PB_BEGIN`: A the source, once the program has started it.
- `PB_PAUSE` and `PB_RESUME`: `SN_TPAUSE` and `SN_TRESUME` for a tune, `SN_PAUSE` and `SN_RESUME` for the stream,
  music's pause and resume. `PE_STATE` means nothing is begun, it is paused already, or (for `PB_RESUME`) `PB_PAUSE`
  didn't pause it. A resume refused because the part was stopped meanwhile leaves it no longer paused.
- `PB_VOLUME`: A the level (255 unity), X the balance (0 left, 128 the centre, 255 right). At the centre both sides
  get the level; away from it the far side falls in a straight line to 0 at the end, then `SN_VOLUME`.
- `PB_STATUS`: A/X a block of 7 bytes (`PBS_LEN`): +0 the state, +1–4 the milliseconds played with paused time left
  out, +5–6 a stream's underruns. The states (`PS_*`): 0 stopped, 1 starting, 2 playing, 3 paused, 4 stalled, 5
  ended, 6 an error. A tune's and music's come from the card's own status and count, a tune's stall and jam
  (`PS_STALLED`, `PS_ERROR`) the card's judgement; a stream's from `SN_STATUS`, its milliseconds from the frames
  played and its rate, exactly.
- `PB_LEVELS`: A/X a block of 10 bytes (`PBL_LEN`): eight bands, lowest first, and the VU, left and right, all
  0–255: the card's own measure (`SN_LEVELS`), the same for every source.

Each returns carry clear and A = 0, or carry set and A an error. `PB_STATUS` and `PB_LEVELS` are function 82 calls,
each a round trip to the card: call them a few times a second, or once a frame, not in a hook.

### Firmware

The card's firmware is in its own flash, in two halves: the one running and the other. `SN_FIRMWARE` sends a new
image: a begin (+7 1, with the image's length in +8–11), its pieces (+7 0, each +1–3 an address and +4–6 a length,
into the card by DMA), then a commit (+7 2). The card checks the image (its header, its length and two CRC-32s),
writes it into the half not running, and marks it to boot; it runs it from its next start. A bad image is refused
(11, FIRMWARE) and the old one stays. An update is refused while any part is claimed, by any program. SNDFW ([the User's
Guide](../oodos-users-guide/05-utilities.md#util-sndfw)) does it from a file. `SN_IDENTIFY` says the version running,
its build and which half it is in.

### What sound costs

Nothing a tick, and nothing a frame while no part is claimed. Measured on the emulator at 7.16 MHz, sound card
revision 3 (`test_oodos_sound`'s records, OODOS 0.17):

- **A load or a stream buffer** holds the CPU about 4,096 cycles a 4 KB piece, one DMA cycle a byte, and each piece's
  end is an interrupt whose handler takes 583 cycles at the fewest. So a piece costs about 4,679 cycles, and a
  stream about its byte rate and a seventh more: 44.1 kHz 16-bit stereo, 176,400 bytes a second, about 221,500
  cycles a second with its frames' status, 3.1% of the CPU; 48 kHz, 192,000 bytes, about 239,400, 3.3%. Each buffer
  also costs one record to the card and its reply, when the card asks for it.
- **A frame** with a part claimed and a layout-2 area open sends the card one status request a part, and reads the
  replies when they come, in the next frame: a frame's interrupt takes 447 cycles with nothing claimed, 781 with the
  stream claimed (334 more), and 5,285 with all three parts claimed (about 4,840 more: some 290,000 cycles a second,
  4% of the CPU).
- **A call that asks the card and reads its reply** waits for the reply and copies its results into the program's
  block: `PB_VIS` about 7,400 cycles (its 98 bytes), `SID_VOICES` 4,200 (`SN_SIDREGS`' 50), `PB_LEVELS` 2,100 (10)
  and `SN_STATUS` 3,700 (19), with a stream playing (`test_oodos_pb`'s `pb_vis_costs`). Called each frame, `PB_VIS`
  is about 6% of a frame.
- **The card's events** are an interrupt each, read and queued.
- **A tune, music, effects and the beep**, once started, cost nothing: the card plays them.

## Graphics

L4's graphics are in two parts. What needs a DMA channel, the vertical blank or physical memory is OODOS's,
[function 83](07-function-reference.md#bdos-83): loading a file's records into memory, copies and fills, copies at
the vertical blank, the mouse's pointer sprite, and the console's screen back. What a program draws with is
register and memory work with nothing to share, so it is **OOLIB's linked library**: `oodos/lib/gx*.s`, linked from
`build/oolib.lib`, its calls in `include/oolib.inc`. The video card itself, its layers, sprites and registers, is
the [Technical Reference's chapter 6](../oo1-technical-reference/06-video.md).

### Video memory

Since OODOS 0.9, from interface version 8: video memory is the video card's own, revision 2's 2 MB, not system RAM
([chapter 5](05-memory-and-banking.md#video-memory-on-revision-2); the card itself is the [Technical Reference's
chapter 6](../oo1-technical-reference/06-video.md#revision-2-card-memory)). Its addresses are **card addresses**, 24
bits, and the library names it in **card pages**, a card address ÷ 256, a word, which is what the card's base
registers hold: PALBASE, SPRBASE, a layer's MAPBASE and TILEBASE, and a sprite's image.

`$000000–$03FFFF` is OODOS's: the console's palette (`$000000`), font (`$000400`) and text (`$002000`), and the rest
kept. No card destination of function 83 may touch it, so the console's text, written on while a game has the
screen ([hw/oolib.md, question 10](../../hw/oolib.md#open-questions)), shows again as it was at the warm boot. A
program may read it: the console's font, for text on a bitmap. `$1C0000–$1FFFFF` is Apple II mode's, never a
program's. The running program has `$040000–$1BFFFF` whole, card pages `$0400–$1BFF`, as it has its TPA, and the
library hands it out:

- `GX_VALLOC`: `GX_V` the size in card pages (1–6,144), A the alignment in pages (a power of two, 1–128; 0 is 1):
  A/X the first page of a free run, aligned, and `GX_A` its card address; `GE_MEMORY` if there is none. Memory is
  taken in whole kilobytes, 4 pages, so a size or an alignment under 4 takes 4.
- `GX_VFREE`: A/X the first page, `GX_V` the size: the run free again, whole or not at all.
- `GX_VADDR`: A/X a card page: `GX_A` its card address.
- `GX_VMAP`: A/X a card page: its 16 KB card bank shown in aperture slot 1 and slot 1's window put in W1 (block 33;
  after `GX_BANK8`, WIN1H `$0E` with block 1, slot 1 at `$E04000`); A/X and `GXP` the page's CPU address
  (`$4000–$7F00`). `GE_MAP` for a page outside the program's part. `GX_W1BACK` puts block 1 back, with WIN1H 0.
- `GX_VPUT` and `GX_VGET`: A/X a CPU address in the program's memory, `GX_A` a card address, `GX_V` the length: to
  the card or from it, by `DM_VWRITE` or `DM_VREAD`.
- `GX_VCOPY`: `GX_A` the source's card address (anywhere in the fitted memory), `GX_C` the destination's (the
  program's part), `GX_V` the length: card to card, by `DM_VCOPY`. `GX_VFILL`: A the byte, `GX_A`, `GX_V`: by
  `DM_VFILL`.

A program's CPU reaches its card memory through slot 1 in W1, which the library maps itself ([chapter 5, "The W1
contract"](05-memory-and-banking.md#the-w1-contract)). A program using the library keeps its code, its event area
and its data out of `$4000–$7FFF`. Banks 9–B are ordinary RAM again, the program's, and `GX_BLOCK` maps one of their
blocks (36–47) as it maps an allocated block, with WIN1H 0; A of 64 or more is refused (`GE_MAP`, since interface
version 12), as is any block not in the W1 map.

**Bank 8 as RAM** (`GX_BANK8`, since OODOS 0.11, interface version 12, B7). While AP8 is set, as OODOS leaves it,
bank 8 is the card's aperture and slot 1's window is block 33. A program may ask for bank 8 as its RAM too by calling
`GX_BANK8` after `GX_INIT`; the library then reaches slot 1 at the aperture's own place, `$E04000`, by WIN1H `$0E`
with block 1, and bank 8's blocks 32–39 join the W1 map for `GX_BLOCK`. A program that never calls it runs exactly
as before. A program that loads card memory by DMA through physical `$84000` (slot 1's old window) must not call
it: that address is then RAM, and a DMA address can't name `$E04000`.

**A screen** of the 640×480 colour layers (since OODOS 0.12, interface version 13,
[hw/video640.md](../../hw/video640.md)): a bitmap 640 is 1,920 card pages (480 KB, rows of 1,024 bytes), a direct
640 2,400 (600 KB, rows of 1,280); the program's part holds three bitmap 640 screens or two direct 640 ones, a
double buffer. Take them on 16 KB (an alignment of 64) and a bitmap 640 row never crosses a slot. A direct 640 row
does, at 4 rows in every 64 (rows 12, 25, 38 and 51 of each 64 from a base on 16 KB): the library maps each pixel
by its own address, so its draws are right there, but a program that writes along a row through `GX_VMAP`'s window
itself must map the next page at the slot's end.

### Loading

`DM_LOAD` loads a count of a file's records, up to 512 (64 KB), from a record of an open FCB, to a physical address:
whole sectors straight from the SD card by DMA, at most 8 a READ, and the ends through a BDOS buffer. 64 KB takes
about 16 ms from an `oodos-8m` drive, against 87 ms by functions 51 and 33 a record at a time. The destination is
system memory, so a file goes to the card in two steps: `DM_LOAD` into a buffer, then `DM_VWRITE` from it, as the
library's `GX_ASSET` does 4 KB at a time. Neither waits for the vertical blank: a program loads into card memory it
isn't showing, then flips to it.

```
        lda     #<load
        ldx     #>load
        ldy     #O_DMA
        jsr     BDOS            ; A = 0: loaded; 1: the file ended first
        ...
load:   .byte   DM_LOAD
        .word   fcb             ; opened by function 15
        .byte   0, 0, 0         ; from record 0
        .word   32              ; 4 KB
        .byte   $00, $F0, $0B   ; to $BF000, in bank B
```

Then to the card:

```
        lda     #<put
        ldx     #>put
        ldy     #O_DMA
        jsr     BDOS            ; A = 0: on the card
        ...
put:    .byte   DM_VWRITE
        .byte   $00, $F0, $0B   ; from $BF000
        .byte   $00, $00, $05   ; to card address $050000
        .word   4096
```

### Copies and fills

`DM_COPY` copies and `DM_FILL` fills physical memory by DMA, 2 cycles a byte, in the BDOS's time. For card memory
(since OODOS 0.9, interface version 8) `DM_VWRITE` and `DM_VREAD` copy to it and from it through the card's data port,
`DM_VFILL` fills it, and `DM_VCOPY` copies within it: a scroll of a bitmap, a cleared screen, the console's font read
for text. Through the port a byte is a cycle of DMA, so 60 KB is about 8.6 ms of it, the CPU held. `DM_VFILLW`
(since OODOS 0.12, interface version 13) fills it with a word, a direct 640 colour, in two passes at the port's
step 2. The library fills a bitmap's whole rows in one call (`GX_FRECT`, by `DM_VFILL`, or `DM_VFILLW` for a direct
640 colour whose two bytes differ) and copies its rectangles a row a call (`GX_BCOPY`, by `DM_VCOPY`).

### The vertical-blank slots

Four **slots**, 1 KB of copies in all, run at the vertical blank: a sprite table and a palette from the program's
memory to card memory, say, through the card's port, so that a frame's changes show together and nothing is torn. `DM_VBSET` sets a slot's
source, destination and length; the program then sets the slot's bit in the event area's +12 (`EV_VBGO`) with a
`TSB`, which an interrupt can't split, and OODOS copies the slot at the next vertical blank and clears the bit. A
bit still set means the copy hasn't happened yet. Nothing is copied while no event area is open. The library binds
slot 0 to its sprite shadow table and slot 1 to its palette shadow.

```
        lda     #1              ; slot 0's bit
        tsb     area + EV_VBGO  ; copied at the next vertical blank
```

### The pointer

`DM_POINTER` names a sprite OODOS moves with the mouse: at each vertical blank, after the slots, its X and Y in the
card's sprite table (at SPRBASE, which must be in the program's part of card memory) are the mouse's, from a layout-2 event area, less the hotspot. The program draws the image, sets
the sprite's other bytes, and puts the mouse in absolute mode with limits 319 and 239 ([function
81's](07-function-reference.md#bdos-81) `IN_MOUSE`), since sprites live in a 320 × 240 space.

Since OODOS 0.13 (interface version 14) the pointer can be a full-resolution sprite, one screen pixel a sprite pixel
([the Technical Reference](../oo1-technical-reference/06-video.md#full-resolution-sprites)), so a paint program on a
640 × 480 bitmap points at the pixel: the program turns full-resolution sprites on (`GX_SPRFULL` with A = 1, below),
puts the mouse's limits at 639 and 479, and asks `DM_POINTER` for `DM_PTRFULL` (64) + its sprite. OODOS then sets
the entry's byte 3 bit 7 with X and Y each frame. Without the 64 the pointer stays doubled, in 320 × 240, whatever
SPRCTL says; OOUI's pointer is doubled.

### The library

`GX_INIT` first, with the event area's address (or 0): it checks for DMA (`OA_DMA`) and the video card
(`GE_VERSION` without them), reads the W1 map and the card's room (`DM_INFO`), and marks the program's
card memory all free. Then:

- **`GX_BANK8`** (`gxmem.s`, since OODOS 0.11, interface version 12), after `GX_INIT`: asks for function 83's
  `DM_BANK8` ([chapter 7](07-function-reference.md#bdos-83)) and takes slot 1's window from it.
  - With the video card: carry clear; bank 8, `$80000–$8FFFF`, is the program's RAM until the warm boot, slot 1
    is mapped by WIN1H `$0E` with block 1, and blocks 32–39 join the W1 map.
  - With no video card: carry clear; bank 8 is RAM, and there is no window (`GX_INIT` had said `GE_VERSION`).
  - `GE_VERSION`, with nothing changed and the old mode kept (block 33, WIN1H never written): no DMA, or
    `DM_BANK8` refused.
  - `GX_INIT` always takes the old mode, so a `GX_INIT` after `GX_BANK8` needs `GX_BANK8` again.
- **Layers and memory** (`gxmode.s`, `gxmem.s`): `GX_LAYER` a layer's type (X, its CONFIG), its map's card page
  (`GX_V`) and its tiles' or font's (`GX_R`), its scrolls and PALOFS 0; `GX_PALOFS` (since OODOS 0.9) a layer's palette
  group, A the layer and X the group, 0–15 (its PALOFS: a text, mono or 4-bit pixel's colour is the group × 16 + its
  value, 0 still transparent), so a layer can keep to its own 16 entries; `GX_SHOW` the display, `GX_BORDER`, `GX_PALBASE` and `GX_SPRBASE` (a
  card page in A/X), `GX_SCROLL`, `GX_FLIP` (double buffering: a layer in A, the card page in `GX_V` its MAPBASE
  takes, latched at the vertical blank, then the frame waited for), `GX_CONSOLE`; the card memory calls
  ([above](#video-memory)), `GX_BLOCK`, `GX_W1BACK`.
- **The palette** (`gxpal.s`): `GX_PALINIT` takes two card pages for it and points PALBASE there; a shadow of 256
  entries on slot 1, `GX_PALSET`, `GX_PALLOAD`, `GX_PALSHOW`; and (`gxfade.s`, a module of its own since OODOS 0.9,
  so a program that only sets colours doesn't link its 3 KB of accumulators) `GX_FADE` to another palette over n
  frames, a step a frame.
- **Sprites** (`gxspr.s`): `GX_SPRINIT` takes two card pages for the table and points SPRBASE there; a shadow table
  of 64 entries on slot 0, `GX_SPRPOS`, `GX_SPRIMG` (the image in `GX_V` as revision 2's table holds it: bits 0–14
  its card page, bit 15 set for 8 bits a pixel), `GX_SPRATTR`,
  `GX_SPRSHOW` (refused with no event area, as `GX_PALSHOW` and `GX_FADE` are); `GX_COLL` the groups that collided, from the card's STATUS and ISR.
  Since OODOS 0.13 (interface version 14; `gxsctl.s` and `gxsprf.s`, modules of their own, so a program that doesn't
  call them links what it did): `GX_SPRFULL` (A = 1 full-resolution sprites on, the card's SPRCTL; A = 0 off; carry
  clear) and `GX_SPRFPOS` (A a sprite: X and Y from `GX_X` and `GX_Y` in the screen's
  640 × 480, the entry's byte 3 bit 7 set). `GX_SPRPOS` writes the bit clear, so the position call chooses the size:
  a sprite moved by `GX_SPRFPOS` is full resolution, by `GX_SPRPOS` doubled. The groups and `GX_COLL` work at both
  sizes, in screen pixels. The warm boot sets SPRCTL to 0 again.
- **Tiles** (`gxtile.s`): `GX_TMAP` (a map's card page in A/X, its width in Y), `GX_TPUT`, `GX_TGET`, `GX_TFILL`
  (by `DM_VCOPY`).
- **Bitmaps** (`gxbmp.s`): `GX_BMP` names the bitmap (its type in A, its first card page in `GX_V`), drawn through
  slot 1; bitmap 256 (`GB_256`, 8 bits a pixel), bitmap 512 (`GB_512`, 4) and mono (`GB_MONO`, 1), and since OODOS
  0.12 (interface version 13) bitmap 640 (`GB_640`, layer type 6: 1,024 × 480 at 8 bits, 640 shown) and direct
  640 (`GB_DIRECT`, type 7: 640 × 480, a word a pixel). On `GB_DIRECT` every colour is a word, A its low byte and X
  its high, `$0RGB` as a palette entry is (`$8000` transparent, `$0000` black; bits 12–14 written 0), and
  `GX_POINT` returns A/X; on the other bitmaps X isn't read; a program sets the high bytes it uses (`GX_FGH`,
  `GX_BGH` included: OODOS doesn't clear a program's BSS). `GX_BMP` refuses a type past `GB_DIRECT` (carry set,
  `GE_FORMAT`, the bitmap as it was). `GX_FRECT` says a fill function 83 refused (carry set, `GE_DMA`), carry clear otherwise. `GX_PLOT`, `GX_POINT`,
  `GX_LINE`, `GX_RECT`, `GX_FRECT`, `GX_CIRCLE`, `GX_FLOOD` (a fill of a region, by spans: GW-BASIC's PAINT) and
  `GX_BCOPY`. Draws are clipped to the bitmap (one wholly off it draws nothing); a copy whose rectangle leaves it is
  refused (`GE_BOUNDS`, as is a copy on a mono bitmap or at an odd x or width at 4 bits), and one of no width or
  height does nothing. A copy's rows are each one card copy forward, as memcpy, not memmove: moving a strip right
  on the same rows repeats its first bytes, so copy through another place for that.
- **Text** (`gxtext.s`): an 8 × 16 font on a bitmap, the console's (read from the card a character at a time, by
  `DM_VREAD`) or the program's: `GX_FONT`, `GX_TEXT`. On `GB_DIRECT` its colours are words: `GX_FGH:GX_FG` and
  `GX_BGH:GX_BG` (the high bytes since interface version 13).
- **Assets** (`gxasset.s`): `GX_ASSET` opens an asset file and loads each part into the card at the page it names, or
  at pages it allocates, 4 KB at a time by `DM_LOAD` into a staging buffer, `GX_STAGE` (physical: `$BF000`, bank
  B's last 4 KB, unless the program moves it), and `DM_VWRITE` on. Each part is checked first (its type, its place
  in the file, its records against its bytes, its pages free and the program's), and every page the file took is
  freed again if a part is refused. A part is 1 to 1,572,864 bytes, the program's part of card memory, since
  interface version 13 (131,072 before).

A program links only the modules it calls. Coordinates and values go in the `GX_` variables; a refusal is the carry
set with an error in A (`GE_`).

### Assets

An **asset file** (format 4, since OODOS 0.9, interface version 8) holds a picture's parts, ready for the card: a
palette, a bitmap, tiles and their map, sprite images and a sprite table, a font. Record 0 has a 16-byte header
(`'OGX'`, the format, the parts) and up to 7 parts of 16 bytes (the type, the layer's CONFIG it was made for, at +2–3
a card page, a multiple of 4 in the program's part or `$FFFF` for any, the first record, the records, the bytes, a
width and height); each part starts on a 1 KB boundary of the file, so it loads in whole sectors. Sprite images are
each padded to 256 bytes, a card page, and a sprite table's entries count them in pages from the images' part, which
`GX_ASSET` moves on by where that part went. The library reads format 4 alone: an older format is refused (`GE_FORMAT`). `oodos/tools/mkasset.py` makes them from PNG images on Linux (`bitmap256`, `bitmap512`, `tiles`,
`sprites`, `palette`, and since OODOS 0.12 `bitmap640`, a PNG up to 1,024 × 480 in 256 colours, its palette part
first, and `direct640`, a PNG up to 640 × 480 in its own 12-bit colours, alpha below 128 the transparent `$8000`, no
palette part), puts the parts of several into one file so a program loads them in one call (`combine`: a
palette that repeats one already taken is left out; one sprite images part and one sprite table at most), and checks
one (`check`). Since OODOS 0.13, `sprites --table --full` makes the table's sprites full resolution: their places in
640 × 480 and each entry's byte 3 bit 7 set, shown so once the program calls `GX_SPRFULL` (VIEW does). [VIEW](../oodos-users-guide/05-utilities.md#util-view) shows one.

### What graphics costs

With no slot set and no pointer, nothing: the tick with no event area is 162 cycles and the frame 215, as before,
and a layout-2 area's frame 7 cycles more (412). A slot set but its bit not is 534 a frame; a 512-byte slot copied
through the port, 1,323 (512 of them the DMA's, a cycle a byte); the pointer, 753; the whole budget, four slots of
256 bytes, with the pointer and a 44.1 kHz stream's fields, 2,789. Since OODOS 0.11 (B7) these include the
dispatcher's 17 cycles for WIN1H 0 (10 more while WIN1H is `$0E`, [as the clocks say](#the-clocks)), measured at
B7's final build. In the frames sampled it ended inside the vertical blank even with a USB batch or a 4 KB load
piece holding the interrupt off (`test_oodos_video`: line 505 at the latest under a USB batch and under 64 KB loads,
19 lines to spare in the frames sampled; the blank is lines 480–524). That is a sampled figure, not a worst case:
the worst seen for a single 512-byte slot under loads is line 522, 2 lines from the blank's end, and a sweep for
the worst case is later work ([hw/oolib.md](../../hw/oolib.md#open-questions), question 3). These are
measured on DMA revision 2 (B2, *proposed*), where each copy selects channel 1 and puts the controller's CONTROL
back, and each card transfer begins with a poll of the cards. The figures are
recorded with their source in [hw/oolib.md, "Video card revision 2"](../../hw/oolib.md#the-plan).

The 640×480 colour layers (since OODOS 0.12; measured in test_oodos_gfx, recorded in
[hw/video640.md](../../hw/video640.md)): a whole direct 640 screen filled by `GX_FRECT` takes 0.116 s (`DM_VFILLW`,
ten calls) or 0.113 s (`DM_VFILL`, for black or clear), a whole bitmap 640 0.092 s, a bitmap 640's 640 shown
columns 0.213 s (a fill a row); a 600 KB part loaded by `GX_ASSET` from an `oodos-8m` drive 0.67 s. `GX_PLOT`,
from its first instruction to its return: 406 cycles on a bitmap 256, 443 on a bitmap 640, 498 on a direct 640,
and 514 where it maps the next slot along a straddling row.

## Linking the library into an overlay

An overlay that [function 59](07-function-reference.md#bdos-59) loads has no zero page of its own: the loader
refuses one whose header says it has. It uses its caller's, by importing the caller's names, which never move. So
each library module's zero page is in a module of its own, `lib/zp*.s`, which holds nothing else (since
2026-11-10; nothing in OODOS changed for it):

- **A program** links the library as before, to the byte. ld65 takes a zero page module with the code that names
  it, and `lib/zpNAME.s` is in `oolib.lib` right after `NAME`, so the program's zero page is laid out as it was.
- **Where it goes** (since OODOS 0.9): while the library draws, block 33 is in W1 (or WIN1H `$0E` with block 1,
  after `GX_BANK8`), so an overlay linked with the
  library's graphics modules is loaded outside `$4000–$7FFF`, as the program's code and data are (the W1 contract).
  The build checks a program linked with the library for that (`oodos/tools/w1.py`), but not an overlay, which has
  no address until function 59 relocates it, nor a buffer the program puts at a fixed address.
- **An overlay** is linked with its caller's addresses for those names, so ld65 doesn't take the zero page modules,
  and the overlay's zero page size is 0. `oodos/tools/libzp.py names` writes the addresses as a module, from the
  caller's map (ld65's `-m`). Assemble it, and link it before `oolib.lib` in each of the three links that make a
  `.PRL` ([chapter 6](06-program-formats.md)):

  ```
  python3 -I oodos/tools/libzp.py names build/host.map SID_ZP -o build/plugzp.s
  ca65 build/plugzp.s -o build/plugzp.o
  ld65 -C oodos/cfg/prl.cfg plug.o build/plugzp.o build/oolib.lib -o build/plug.a -m build/plug.a.map
  ```

  oodos's Makefile does it for each overlay in `OVL_LIB`, from the map of `OVL_CALLER`'s `.COM` and the names in
  `OVL_ZP`.
- **The caller** links each zero page module its overlays need, even one for code it doesn't call itself, by naming
  it and using the name once: `.importzp SID_ZP` and `lda #SID_ZP` (an import that is never used isn't linked).
  `libzp.py` refuses a name the caller's map doesn't have. Those bytes are the library's while an overlay runs, as
  they would be for the caller's own calls.
- **One copy for a job.** The zero page is shared, but a module's state is in its own BSS: if the caller links
  `lib-sid` too, its copy doesn't see what the overlay's `SID_PLAY` set (`SID_STUB`, `SID_CTRL`, the fields). So
  make a tune's calls, `SID_PARSE` to `SID_STOP`, through one copy, the overlay's.
- An overlay linked **without** its caller's names takes the zero page modules with the code, and 59 refuses it
  (`$FE02`).

| Modules | Their zero page | Bytes | Defined in |
| --- | --- | --- | --- |
| `gxasset`, `gxbmp`, `gxfade`, `gxmem`, `gxmode`, `gxpal`, `gxspr`, `gxtext`, `gxtile` | `GXP`, `GXQ`, `GXT` | 6 | `zpgxmem` |
| `gxfade` | `GXFADE_ZP` (since OODOS 0.9: was `gxpal`'s `GXPAL_ZP`) | 4 | `zpgxfade` |
| `mu`, `munote`, `mutone` | `MUP` | 2 | `zpmu` |
| `pblev`, `pbmath`, `pbvol` | `PBP` | 2 | `zppbmath` |
| `pbeq` | `PBEQ_ZP` | 2 | `zppbeq` |
| `pblev` | `PBLEV_ZP` | 2 | `zppblev` |
| `pbstat` | `PBSTAT_ZP` | 4 | `zppbstat` |
| `pbvis` | `PBVIS_ZP` | 3 | `zppbvis` |
| `sid` | `SID_ZP` | 2 | `zpsid` |
| `sidenv` | `SIDENV_ZP` | 2 | `zpsidenv` |
| `sidnote` | `SIDNOTE_ZP` | 2 | `zpsidnote` |
| `sidv` | `SIDV_ZP` | 2 | `zpsidv` |

`pb`, `pbcard` and `physof` have none. The build checks, as `oolib.lib` is made (`libzp.py check`), that no other
module has zero page of its own and that each `zpNAME` follows its `NAME`.

## The serial line

Since interface version 16 (OODOS 0.15) the serial line's input is buffered: OODOS's interrupt keeps up to 255
received bytes in a ring, and functions 3 and 7 read it ([chapter 4](04-console-devices-scb.md#the-serial-lines-receive-buffer)).
`FT_INFO`'s X has `OA_SERIAL` (`$20`) then. Two things serve a program that waits on the line:

- **The event area's +13, `EV_RXCNT`:** the bytes the ring has taken while the area is open, modulo 256, 0 at
  `FT_OPEN`, in every layout. It counts arrivals, not bytes waiting, so it stays right whoever reads them. A game or
  a terminal tests it with one load and a compare against its last copy; a change means something came, with no
  BDOS call. +14 (`EV_RXCNT2`) is kept for a second port, 0.
- **[Function 85](07-function-reference.md#bdos-85), `O_SERIAL`:** `SR_INFO` (the ports, the ring's size),
  `SR_STATUS` (the bytes waiting, the room left, the overruns counted, whether the receiver is stopped) and
  `SR_FLUSH` (the ring emptied).

```
        lda     area + EV_RXCNT
        cmp     lastrx
        beq     :+              ; nothing new
        sta     lastrx
        jsr     takeall         ; function 7 and 3 until 7 says 0
:
```

## When the program ends

The warm boot takes back what L1 and L2 gave: both hooks off, the event area closed, the serial line's receive ring
emptied, and every interrupt enable the
program turned on off again (OODOS keeps only its own: the north bridge's tick and the vertical blank; every VIA
interrupt off and ACR 0). For the input it turns the
USB card's interrupt off, puts the mouse back in relative mode with the limits a reset gives (639, 479), the LEDs
off, the repeat as a reset sets it (`$10`), Pause on Break, and the controllers 1 to 4 in order: what the cold
boot sets. For L3's sound it resets the card, whether a program claimed it or not (CTRL's RESET, as the BIOS's Break
does): every sound stopped, an open transfer abandoned, the master volumes and every source's level back at 255, the
EQ off with every gain 0; then it empties the card's store (FREEALL), so nothing a program loaded is left, aborts
channel 2 and releases every part. For L4's graphics it frees the vertical-blank slots,
turns the pointer off, sets the video card's registers as the cold boot set them for the console (its card memory,
`$000000–$03FFFF`, was never the program's, so its text shows again as it was), turns the port's DMA request off, and
puts the aperture's slots on card banks 0, 16, 2 and 3. That is so whether the program ends by `JMP WBOOT`,
by ^C, or by an error. The counters run on, whatever the program did to timer 1. Break goes to the
monitor with the machine's interrupts off, as it always has; booting OODOS again starts the clocks afresh.

## Still to come

MOD; USB gamepads as controller
4; for graphics, a scan-line hook and a collision hook, software sprites, a C target for the library, and a copy of
the library for FARCALL (with GW-BASIC): [hw/oolib.md, "The plan"](../../hw/oolib.md#the-plan). Until a service is
built, a program has none for it: see [AGENTS.md](../../AGENTS.md) for what not to take over in the meantime.

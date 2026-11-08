# 8 Game services (OOLIB)

The services a game or GW-BASIC calls for the screen, sound, timing and input: OOLIB. They come in steps, L1 to
L6. This chapter has what is built. **L1, the clocks**: a millisecond counter, a frame counter, a block of the
program's memory OODOS keeps them in, waiting for a frame, and routines of the program's that OODOS calls at each
frame or every few milliseconds; that is [function 80](07-function-reference.md#bdos-80). **L2, input**: the
keyboard's every press and release in a queue, the keys held, four game controllers, the paddles and the mouse,
kept in that same block; and [function 81](07-function-reference.md#bdos-81) for the rest. **L3, sound**: the sound
card's 6510, its stream and its ES5503, each claimed by one program at a time; the card's RAM and wave RAM loaded by
DMA; a stream fed from two buffers of the program's while it plays; and a C64 `.sid` tune played by OOLIB's linked
library, which PLAYSID uses; that is [function 82](07-function-reference.md#bdos-82).

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

A is OOLIB's interface version: 1 from L1, 2 from L2, 3 from L3; each step that adds to the services raises it. X
has a bit for each service area OODOS has, one BDOS function each from 80: `OA_FRAME` (`$01`, 80, frame and timing),
`OA_INPUT` (`$02`, 81, input) and `OA_SOUND` (`$04`, 82, sound) now, and later `OA_DMA` (83) and `OA_VIDEO` (84). An OODOS without the services answers 80 as a
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

- **The millisecond counter** counts timer 1 of the serial card's VIA, which OODOS runs at 1 kHz: 7,159 cycles at
  7.16 MHz. It is the clock for anything not tied to the picture: timers in a game's logic, timeouts, sound
  effects played by the CPU. It runs for about 49 days before it goes round.
- **The frame counter** counts the video card's vertical blanks, 59.94 a second. It is the clock for anything
  drawn. It runs for about 2.3 years.

Both run whatever the program does, in a BDOS call or out of one: the BDOS runs with interrupts on. The rate is
fixed, so a millisecond is always one count; a program wanting finer timing has timer 2, which a later step gives
programs.

**On O: and P:** the Disk II driver turns interrupts off for each sector it reads or writes, about 12 ms, because
the CPU times the disk's bits itself. It adds that time to the millisecond counter afterwards, from the bytes it
read, so the counter keeps time through it to about half a percent. A sector takes less than a frame, so no frame
is missed.

**What the clocks cost.** Each tick is an interrupt: 145 cycles with no event area and no hooks, 2% of the CPU, in
every program. An open event area of layout 1 adds 39 cycles a tick, one of layout 2 (input) 89, and each hook its
own time ([the input's cost](#what-input-costs)).

`FT_COUNT` reads both counters at one moment: A/X has the millisecond counter's low word, and a block gets all 8
bytes. To wait, there are two calls, both with interrupts on, so the hooks keep running:

- **`FT_WAIT`** returns at the next vertical blank, or after the count of frames in its block's +1. A/X is the
  frame counter's low word then. A game that draws in a loop calls it once a round, so it draws once a frame.
- **`FT_DELAY`** returns after the count of milliseconds in its block's +1–2, up to 65,535. If the millisecond
  counter stands still for 4 frames meanwhile, timer 1 has been stopped, against the rules below, and it gives up
  then with A = X = `$FF` rather than wait for ever. Without a running frame counter (no video card, or its
  vertical blank turned off) it can't tell, and waits on.

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
| +12–15 | | reserved, 0 |

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
- **Its stack** is page 1 of its own W0 block, under whatever is in use there: under the program's stack, or, when
  the hook interrupts a BDOS call, under the program's stack as it was at the call. A hook pushes what it needs and
  pops it again, as any routine does. That page 1 is the W0 block's the hook was *set* with, whatever W0 has when
  it runs, while "under the program's stack" is reckoned by the S of the moment: so set the hooks with the W0 they
  will run under. A program that changes W0 later and keeps its hooks would have them push into the other block's
  page 1 wherever S points, over what that page holds.
- **Budgets: 8,000 cycles a frame hook** (about 7% of a frame), **300 a tick hook** (it may run 60 times as often).
  They aren't enforced. A debug build of OODOS.SYS (`make -C oodos/sys DEBUG=1`) times each hook by timer 1, and
  `FT_COSTS` says the most each took: from the dispatcher's call to its return, so about 130 cycles more than the
  hook's own, and for a frame hook the ticks taken under it.
- **The frame hook runs with interrupts on,** so the ticks that come while it runs are counted (its budget is
  longer than a tick). A frame that comes while the frame hook is still running is counted, but the hook isn't
  called again for it; `FT_COSTS` counts those. **The tick hook runs with interrupts off, and must leave them
  off:** no `CLI`. With them on, the next tick could come while it runs and call it again, on top of itself.
- **Not allowed:** a BDOS call, DMA, a far call, and a write to the VIA's ACR, IER or IFR, which are OODOS's.
  Timer 1 is OODOS's tick, so its registers are too: a write to `T1CL`, `T1CH`, `T1LL` or `T1LH` changes the
  tick, and even a *read* of `T1CL` clears timer 1's interrupt flag, so the tick due then is lost. `T1CH` and the
  latches (`T1LL`, `T1LH`) are safe to read. OOLIB's routines that only touch registers may be called once there
  are some (L4).

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
| +96–127 | `EV_SOUND` | the stream's part ([below](#sound)), 0 unless the stream is claimed |
| +128 | `EV_QUEUE` | the queue: 8 bytes a slot, as many as the size leaves |

### The queue

Every key pressed and every key let go, a mouse button, a turn of the wheel, a controller's change and a device
plugged in or out is an **event**, 8 bytes, in a ring of slots from +128:

| Offset | Name | What |
|---|---|---|
| +0 | `EQ_KIND` | `EK_KEY` 1, `EK_BUTTON` 2 (the mouse's), `EK_WHEEL` 3, `EK_CTL` 4 (a controller), `EK_DEVICE` 5, `EK_SOUND` 6 (a byte from the sound card's 6510: [Sound](#sound)) |
| +1 | `EQ_CODE` | the key's HID usage; the button, 0–4; the controller, 1–4; else 0 |
| +2 | `EQ_VALUE` | 1 pressed, 0 let go; a controller's new bits; the wheel's steps, signed; the devices now; the 6510's byte |
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

With a layout-2 area open each tick costs 234 cycles, against 145 with no area: 89 more, about 1.2% of the CPU.
Twice a frame a tick costs more, when it starts the paddles (366) and when it reads them (673), and a stick's
change is a little more again. The USB card's interrupt for a key is about 650 cycles. Without a layout-2 area
OODOS samples nothing: a program that doesn't open one pays only L1's 145 a tick.

**Reading a value whole.** As with the counters, an interrupt can come between two loads: read a 2-byte X twice,
or take only the low byte. A wheel cleared by the program with `STZ` can lose a step that came between its load
and the store.

**Sharing the FIFO.** While a layout-2 area is open the USB card's event FIFO is OODOS's: a program that reads it
itself (OO-Amp does, for now) gets what OODOS hasn't taken, and OODOS what it hasn't. Use one or the other. OODOS
also reads the VIA's port B at every tick, which clears its CB1 and CB2 flags.

## Sound

The sound card ([the Technical Reference, chapter 7](../oo1-technical-reference/07-sound.md)) has three parts a
program takes, one program at a time: **the 6510 and the SIDs** (`SC_6510`), **the stream** (`SC_STREAM`: the PCM or
MP3 FIFO, revision 2) and **the ES5503** (`SC_DOC`, revision 2). Function 82's `SN_CLAIM` takes the ones in its
block's +1, all of them or none: a part held already, by this program or an RSX, is refused, and so is one the
card hasn't. `SN_RELEASE` gives parts back, each stopped. `SN_INFO` says the card's revision and codecs, and with a
block which parts are free. A claimed part is the program's to drive itself, its registers written as the Technical
Reference has them ([question 6](../../hw/oolib.md#open-questions)): the SIDs while the 6510 is stopped, the
ES5503's oscillators, the EQ, the volumes and the vis page. **DMA channel 2 is OODOS's**: a program reaches it only
through function 82's loads and its stream.

```
        lda     #<claim
        ldx     #>claim
        ldy     #O_SOUND
        jsr     BDOS            ; A = 0: the stream is ours
        ...
claim:  .byte   SN_CLAIM, SC_STREAM
```

### The 6510

`SN_LOAD` copies a run of physical memory into the card's RAM by channel 2 (+1–3 the address, +4–5 the card's
address, +6–7 the length, 0 for 64 KB). The source is checked as [function 51's](07-function-reference.md#bdos-51)
DMA segment is: none of OODOS's own memory. A program's own memory below `$C000` is its blocks 0–2, so its CPU
address is the physical one there. `SN_START` writes the 6510's reset vector and resets it running (+3: CTRL's NTSC,
SID B at `$D500`, BOTH); `SN_STOP` stops it and silences the SIDs; `SN_SEND` puts a byte in its mailbox, refused with `SN_BUSY` while the
6510 hasn't taken the last one (a second would replace it). The mailbox to the card is emptied only by a /RESET, so
a byte a program left there, not taken, is still there for the next: a new 6510 driver reads `$DE00` once when it
starts. A byte the
6510 sends back reaches the event queue as an `EK_SOUND` event, its value the byte, while a layout-2 area is open
(without one, the program reads the card's MBIN itself), so a program's own driver on the card reports without
being polled.

### The stream

`SN_STREAM` starts a stream: +1 the codec (0 raw PCM, 1 MP3), +2–3 the rate and +4 the format (raw PCM's: bit 0
stereo, bit 1 16-bit). Then the program gives OODOS its data a buffer at a time: `SN_BUFFER`, +1–3 a physical
address and +4–5 a length (0 for 64 KB). OODOS keeps two: the first given goes to the card at once, by channel 2,
and when it has all gone the other follows, from the interrupt, with no gap. They are taken **in turn**, 0, 1, 0,
1 (A says which), and `SN_BUFFER` is refused with A = `SN_BUSY` (`$FE`) while the next one is still in use; so a
program alternating two buffers of its own always has its first in 0 and its second in 1. As each is sent, its bit
in the area's `EV_SBUFS` goes to 1: the program refills it then, with ordinary BDOS reads, and gives it again. The
card plays on its own clock meanwhile, from its 128 KB FIFO: 0.74 s of 44.1 kHz 16-bit stereo, 8 s of 128 kbit/s
MP3.

```
loop:   lda     #1              ; buffer k's bit: 1, then 2, in turn
        and     area + EV_SBUFS
        beq     loop            ; still going to the card
        ...                     ; read the file into buffer k, then SN_BUFFER it
```

`SN_PAUSE` and `SN_RESUME` hold it and let it go; `SN_SEEK` empties the FIFO and drops both buffers, for data from
a new place (the next buffer is 0 again); `SN_END` says no more is coming, so what is in plays out and the state goes
to ended. `SN_STATUS` reads the rest at one moment. While the stream is claimed and a layout-2 area is open, OODOS
writes its state into the area once a frame and at each buffer's end:

| Offset | Name | What |
|---|---|---|
| +96 | `EV_SSTATE` | the stream's state: 0 idle, 1 buffering, 2 playing, 3 paused, 4 underrun, 5 ended; `$80` up an error (the card's STATE) |
| +97 | `EV_SBUFS` | the buffers free: bit 0 buffer 0, bit 1 buffer 1 |
| +98–99 | `EV_SFREE` | the FIFO's room, in 256-byte units (512 empty) |
| +100–103 | `EV_SPOS` | the frames played (the card's POS) |
| +104–105 | `EV_SKBPS` | an MP3's bitrate, kbit/s |
| +106–107 | `EV_SUNDER` | the underruns seen since the stream started |
| +108–127 | | reserved, 0 |

**Keeping it fed.** Nothing in this needs the program's time while both buffers and the FIFO have data: a slow
frame costs nothing. An underrun is the FIFO running dry before `SN_END`, and OODOS counts each it sees (at a frame,
a buffer's end, or `SN_STATUS`). A buffer goes to the card in pieces of 4 KB: with room in the FIFO the card takes
a byte a cycle and the CPU waits, 4,096 cycles a piece at most, under a tick, so no tick is lost.

### The ES5503

`SN_WAVE` loads its wave RAM as `SN_LOAD` loads the card's (+4–6 the wave RAM's 17-bit address). Neither wraps: a
load that would run past the end of its RAM (64 KB the card's, 128 KB wave RAM) is refused. `SN_DOCHOOK` sets
a routine of the program's that OODOS calls at the chip's interrupt (an oscillator done), +1–2 its address and +3 a
byte of the program's zero page: OODOS reads the chip's interrupt register (`$E0`, which takes the interrupt) and
writes it there before the call, so the hook finds the oscillator in bits 1–5. It keeps the rules of the hooks
([above](#hooks)), with interrupts off as the tick hook's: no `CLI`, and a small budget, a few hundred cycles. The
hook finds the port on the chip's registers (DOCCTL's RAM and AUTOINC off, DOCADDR `$E0`), and may write them: to
reprogram the oscillator, say. OODOS saved DOCCTL, DOCADDR and DOCBANK before it read `$E0`, and puts them back
after the hook returns, so a register or wave RAM access the interrupt came in the middle of goes on as it was,
whatever the hook wrote. So a DOCCTL write made in the hook is undone when it returns: a hook that wants the chip
stopped halts its oscillators (each one's control register, `$A0` + its number, bit 0) instead. With the chip's
interrupt routed to the card's 6510 (DOCCTL's IRQ_CARD), the interrupt is the 6510's, and OODOS leaves it alone. A
wave load and the hook never meet: OODOS holds the chip's interrupt off while `SN_WAVE`
runs. If `SN_DOCHOOK` is refused, no hook is set, as with the frame and tick hooks.

### The beep

`SN_BEEP` sounds the USB card's speaker: +1–2 a pitch in Hz (20–20,000), +3–4 a length in milliseconds. It returns
when it is done. The tone is timed in cycles with interrupts left on, so the ticks lower it about 2%; the length is
timed by the millisecond counter.

### .sid tunes

A C64 `.sid` file is a tune's own 6502 code. Playing one is file work with nothing to share, so it is in **OOLIB's
linked library** ([question 9](../../hw/oolib.md#open-questions)), not in OODOS: `oodos/lib/sid.s`, linked from
`build/oolib.lib` (`include/oolib.inc` has its calls). `SID_PARSE` reads the header, `SID_PLAN` finds a page the tune
leaves free and builds a small player there that stands in for what a C64 leaves a tune, as the emulator's own player
does, and `SID_PLAY` claims the 6510, loads the tune, the player and the vectors by `SN_LOAD`, and starts it.
`SID_STOP` stops it. PSID files only: an RSID expects a whole C64. The chips' model (a 6581 or an 8580) isn't
chosen: it is a jumper on the card, not a register, so a tune written for the other plays on the chips fitted. PLAYSID ([the User's
Guide](../oodos-users-guide/05-utilities.md#util-playsid)) is a program on it.

### What sound costs

Nothing a tick: the tick with no area is 145 cycles and with a layout-2 area 234, as before. Each terminal count of
a piece is an interrupt of about 530 to 700 cycles, about 850 at a buffer's end (the area written); with room in the
FIFO the next piece's 4,096 cycles of DMA come inside it. 44.1 kHz 16-bit stereo is 43 pieces a second, under 0.5%
of the CPU. A frame with the stream claimed and a layout-2 area open costs about 300 cycles more (688 against 389),
for the area's sound part.

## When the program ends

The warm boot takes back what L1 and L2 gave: both hooks off, the event area closed, and every interrupt enable the
program turned on off again (OODOS keeps only its own: timer 1 and the vertical blank). For the input it turns the
USB card's interrupt off, puts the mouse back in relative mode with the limits a reset gives (639, 479), the LEDs
off, the repeat as a reset sets it (`$10`), Pause on Break, and the controllers 1 to 4 in order: what the cold
boot sets. For L3's sound it does what the BIOS's Break does to the card, whether a program claimed it or not: the
6510 stopped, the SIDs silenced, the stream flushed and stopped, the ES5503 stopped; and channel 2 aborted, the card's
DRQSEL back to 0 and its interrupts off, the ES5503 hook off, and every part released. Timer 1 is set up again
as the cold boot left it, should the program have changed it. That is so whether the program ends by `JMP WBOOT`,
by ^C, or by an error. The counters run on (a program that changed timer 1's latch may cost the millisecond
counter 1 ms: putting the latch back clears timer 1's flag, and a tick due then with it). Break goes to the
monitor with the machine's interrupts off, as it always has; booting OODOS again starts the clocks afresh.

## Still to come

L4 the graphics library, the pointer sprite and the asset loader, L6 the music driver (and with it GW-BASIC's
SOUND, PLAY and BEEP); RSID tunes and MOD; USB gamepads as controller 4: [hw/oolib.md, "The plan"](../../hw/oolib.md#the-plan). Until a service is
built, a program has none for it: see [AGENTS.md](../../AGENTS.md) for what not to take over in the meantime.

# 8 Game services (OOLIB)

The services a game or GW-BASIC calls for the screen, sound, timing and input: OOLIB. They come in steps, L1 to
L6. This chapter has what is built. **L1, the clocks**: a millisecond counter, a frame counter, a block of the
program's memory OODOS keeps them in, waiting for a frame, and routines of the program's that OODOS calls at each
frame or every few milliseconds; that is [function 80](07-function-reference.md#bdos-80). **L2, input**: the
keyboard's every press and release in a queue, the keys held, four game controllers, the paddles and the mouse,
kept in that same block; and [function 81](07-function-reference.md#bdos-81) for the rest. **L3, sound**: the sound
card's 6510, its stream and its ES5503, each claimed by one program at a time; the card's RAM and wave RAM loaded by
DMA; a stream fed from two buffers of the program's while it plays; and a C64 `.sid` tune played by OOLIB's linked
library, which PLAYSID uses; that is [function 82](07-function-reference.md#bdos-82). **L4, graphics**: files loaded
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

A is OOLIB's interface version: 1 from L1, 2 from L2, 3 from L3, 4 from L4, 5 from L4b (function 82's `SN_HOLD`, `SN_GO` and `SN_VOLUME`), 6 from L5a (the mailbox's stamp, [below](#the-6510)), 7 from OODOS 0.8 (`EV_SFLAGS`, the same); each step that adds to the services
raises it. X has a bit for each service area OODOS has, one BDOS function each from 80: `OA_FRAME` (`$01`, 80, frame
and timing), `OA_INPUT` (`$02`, 81, input), `OA_SOUND` (`$04`, 82, sound) and `OA_DMA` (`$08`, 83, DMA and loading)
now; `OA_VIDEO` (84) isn't needed (the program has its video memory whole) and stays unassigned. An OODOS without the services answers 80 as a
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
| +12 | `EV_VBGO` | the vertical-blank copies' go bits (L4, [below](#the-vertical-blank-slots)), 0 at the open |
| +13–15 | | reserved, 0 |

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
ES5503's oscillators, the EQ and the vis page (or the library's `PB_EQ` and `PB_VIS`: [The EQ and the vis
page](#the-eq-and-the-vis-page)). The master volumes are function 82's (`SN_VOLUME`, below), so the
warm boot can put them back. The EQ is the program's to set, but the warm boot turns it off with every gain 0, as a
/RESET leaves it, so the next program starts flat (from OODOS 0.7; before it, the EQ was left as the program set it). **DMA channel 2 is OODOS's**: a program reaches it only
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

**The mailbox's stamp** (since OODOS 0.7). With the 6510 claimed and a layout-2 area open, OODOS also writes the
last byte into the area's sound part as it queues it, with the time it came and a count. A player can then see that
a tune has stopped reporting, even if the program hasn't read its queue, and a full queue loses no stamp:

| Offset | Name | What |
|---|---|---|
| +108 | `EV_SMBOX` | the last byte from the 6510 |
| +109–112 | `EV_SMMS` | the millisecond counter when it came; before the first byte, when `SN_START` started the 6510 |
| +113–114 | `EV_SMCNT` | the bytes since `SN_START`, up to 65,535, where it stays |

`SN_START` stops the old run, drops a byte it left in the mailbox, sets `EV_SMMS` to its own time and `EV_SMCNT`
to 0, then starts the 6510; `EV_SMBOX` is left as it was. So "how long since the 6510 last spoke" is the counter now
less `EV_SMMS`, from the start if it hasn't yet. An area opened after `SN_START`, while the 6510 already runs, starts
as every area does, with `EV_SMMS` and `EV_SMCNT` 0 until the next byte: measure from the later of `EV_SMMS` and
when the program opened the area or started the tune. With a layout-1 area, or no area, nothing is stamped.

**Stamped, not queued** (since OODOS 0.8). A `.sid` player's stub sends a byte at each call of the tune's play
routine, and a tune on a fast CIA timer calls it thousands of times a second: a 256-byte area's queue holds 15
events, so the queue fills within a frame and key and mouse events are lost with the rest (`EV_QLOST`). A program
that needs only the stamps sets `ES_NOQUEUE` in `EV_SFLAGS` (+115): the bytes are still stamped, but not queued.
Set it after `FT_OPEN`, which fills the area from scratch; clear it again to have them queued. OODOS before 0.8
(`FT_INFO`'s interface below 7) ignores the byte. The music library ([Music](#music)) counts each voice's notes from
these events, so a program that sets the flag can't use `MU_EVENT`'s counts.

### Pausing a tune, and the volume

`SN_HOLD` pauses the 6510 where it is: RUN off without a reset, so its registers and the tune's RAM are left as they
were, and each SID silenced through its own `$D418` volume nibble, the two bytes kept. `SN_GO` writes them back and
lets the 6510 run on from the same instruction, so the tune carries on where it stopped. A hold is refused if the
6510 isn't running, and a go if it isn't held. The hold touches only the 6510 and the SIDs' volumes, not the
master volumes, so a stream playing alongside goes on unchanged.

`SN_VOLUME` sets the card's master volumes, +1 left (VOLL) and +2 right (VOLR), 255 for unity, on a revision-2
card. They scale everything the card plays, the stream, the ES5503 and the SIDs alike (SID A by the left, SID B by
the right), so one call turns all of it down. It needs no claim, and the warm boot sets both back to 255.

```
        lda     #<vol
        ldx     #>vol
        ldy     #O_SOUND
        jsr     BDOS            ; A = 0: both at half
        ...
vol:    .byte   SN_VOLUME, 128, 128
```

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
| +108–114 | | the 6510's mailbox stamp ([The 6510](#the-6510)), written while the 6510 is claimed |
| +115 | `EV_SFLAGS` | the program's (since OODOS 0.8): bit 0, `ES_NOQUEUE`, the 6510's bytes stamped but not queued |
| +116–127 | | reserved, 0 |

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

### The EQ and the vis page

Since OODOS 0.7, the linked library sets the EQ and reads the vis page, on a revision-2 card (Technical Reference,
[chapter 7](../oo1-technical-reference/07-sound.md#reg-c559)). Both are register work with nothing to share, so
there is no claim: a program may set the EQ before its stream starts. Two modules, so a program that only draws a
meter doesn't link the EQ: `oodos/lib/pbeq.s` and `oodos/lib/pbvis.s`.

- `PB_EQ`: A the band (0 the preamp, 1–10 the bands at 60, 170, 310 and 600 Hz and 1, 3, 6, 12, 14 and 16 kHz),
  X its gain in signed half-dB, −24 to 24 (−12 to +12 dB). The enable and the other gains are kept.
- `PB_EQON`: A 0 turns the EQ off (the stream passes bit for bit), anything else on. The gains are kept.
- `PB_EQALL`: A/X 11 bytes, the preamp's gain and then the ten bands', for a stream's start. Every gain is checked
  before any is written.
- `PB_VIS`: A/X a buffer, Y a length, 1–98 (0 means 98). The card takes a snapshot, and its first Y bytes are
  copied in: +0–31 the spectrum's bands, +32–33 the VU pair (both 0–255 for −60 to 0 dBFS), +34–97 the last 64
  samples (`$80` is zero). A meter takes 34. The SIDs aren't in it: their sound never passes through the card's
  digital path.

Each returns carry clear and A = 0, or carry set with nothing written and A = `PE_NOCARD` (a revision-1 card, or
none) or `PE_RANGE` (a band past 10, a gain past ±24, a length past 98). The EQ routines write the card's index,
then its value, with interrupts on: don't call one from a frame or tick hook while the program may be in another
itself, or a gain can land on the wrong band. `PB_VIS` has an index of its own. The warm boot turns the EQ off with every
gain 0 ([When the program ends](#when-the-program-ends)). `PB_VIS` costs about 18 cycles a byte, some 1,800 for the whole page (counted from the code).

### The SIDs' voices

Since OODOS 0.7, the linked library reads what a tune is doing, for a display: the card answers a read of a SID
register with the last value written to it, by the 6510 playing the tune or by the 65C02 (Technical Reference,
[chapter 7](../oo1-technical-reference/07-sound.md)). The library only reads; it never writes a SID. Three modules,
`oodos/lib/sidv.s`, `sidnote.s` and `sidenv.s`, so a program links the ones it calls.

- `SID_VOICES`: A/X a block of 50 bytes (`SID_BLOCK`), SID A's 25 registers (`$00`–`$18`) and then SID B's. Voice
  v, 0–5 (A's three, then B's), has 7 bytes from `SID_VOFF`'s byte v: +0–1 the frequency, +2–3 the pulse width, +4
  the control (bits 7–4 noise, pulse, saw, triangle; bit 0 the gate), +5 attack and decay, +6 sustain and release.
  A SID's +21–24 are its filter's cutoff, resonance and routing, and its mode and volume.
- `SID_NOTE`: A/X a voice's frequency, Y the 6510's CTRL bits (`SID_CTRL`'s; bit 0 NTSC): A = the nearest note, 0
  (C-0) to 95 (B-7; A-4, 440 Hz, is 57), or `SID_NONE` (`$FF`) for a frequency of 0. Noise has no note: test the
  control's bit 7 first. A note's threshold is the geometric mean of its register value and the note below's, so a
  frequency goes to the note it is nearer.
- `SID_ENV0`, then `SID_ENV` once a frame with the frame's `SID_VOICES` block: `SID_LVL` has the six voices' levels,
  0–255. The card can't read a voice's envelope, so the library moves each level as the chip would: up at the
  attack's rate to full when the gate goes on, down at the decay's to the sustain level, and at the release's to 0
  when it goes off, at the data sheet's times (2 ms to 8 s, 6 ms to 24 s) for a frame of 1/59.94 s. It is an
  estimate for a meter, not the chip's own curve: the chip's decay and release aren't straight lines. It moves a
  frame's step a call, so call it once a frame: a frame missed isn't made up, and the meters fall behind (at every
  other frame, a 250 ms attack takes 500 ms). A gate turned off and on again between two calls isn't seen, so no
  new attack starts; and a frequency read while the 6510 writes it can show the wrong note for a frame. Call
  `SID_ENV0` for a new tune or song.

None of them fails: each returns carry clear. `SID_ENV` costs at most about 150 cycles a voice, some 900 a frame
(counted from the code).

### Playback

Since OODOS 0.7, the linked library has the verbs any player uses, whichever part is sounding: pause and resume,
volume and balance, how long it has played, whether it still plays, and how loud it is now. The source is a number:
`PB_SID` (a `.sid` on the 6510), `PB_STREAM`, `PB_DOC` (the ES5503). The program still starts and stops the part
itself (`SID_PLAY`, `SN_STREAM`, its own ES5503 code) and then calls `PB_BEGIN` with its source. Four modules:
`oodos/lib/pb.s` (begin, pause and resume), `pbstat.s` (the status), `pbvol.s` and `pblev.s`.

- `PB_INIT`: A/X the event area (0 for none), for a SID's stall. Without one, a running 6510 is taken as playing.
  The area has to be in sight when the library reads it: not in `$4000`–`$7FFF` while the library holds W1.
- `PB_BEGIN`: A the source, once the program has started it. Its time counts from now. For a SID, X bit 0 says the
  6510 reports through the mailbox: set it for a `.sid` with a play routine (`SID_PLAYA` not 0), whose player sends
  a byte at each call. A tune with none installs its own interrupt and sends nothing, so with bit 0 clear a running
  6510 is simply playing.
- `PB_PAUSE` and `PB_RESUME`: `SN_HOLD` and `SN_GO` for the 6510, `SN_PAUSE` and `SN_RESUME` for the stream. The
  ES5503's pause is refused (`PE_REFUSED`) until function 82 has one. `PE_STATE` means nothing is begun, it is
  paused already, or (for `PB_RESUME`) `PB_PAUSE` didn't pause it. A resume refused because the part was stopped
  meanwhile (`SID_STOP`, `SN_RELEASE`) leaves it no longer paused, so the status is the part's own.
- `PB_VOLUME`: A the level (255 unity), X the balance (0 left, 128 the centre, 255 right). At the centre both sides
  get the level; away from it the far side falls in a straight line to 0 at the end (VOLR = level × balance ÷ 128,
  or VOLL = level × (255 − balance) ÷ 127), then `SN_VOLUME`. Every program's balance moves alike.
- `PB_STATUS`: A/X a block of 7 bytes (`PBS_LEN`): +0 the state, +1–4 the milliseconds played with paused time left
  out, +5–6 a stream's underruns. The states (`PS_*`): 0 stopped, 1 starting, 2 playing, 3 paused, 4 stalled, 5
  ended, 6 an error.
  - A SID: paused while `PB_PAUSE` holds it, stopped once its RUN is off, playing while it runs if it doesn't report
    or there is no area, and otherwise judged by the mailbox's stamp ([The 6510](#the-6510)): playing with a byte in the last 2 s, starting before the first byte for up to 10
    s (a tune's init can take seconds), stalled past either. Both count from the later of the stamp and the
    `PB_BEGIN` or last `PB_RESUME`, so neither an area opened late nor a hold reads as a stall. Its milliseconds
    come from the clock.
  - The stream: the card's state through `SN_STATUS` (idle is stopped, buffering starting, an underrun stalled),
    stopped once it isn't claimed; its milliseconds from the frames the card has played and its rate, exactly.
  - The ES5503: playing while its RUN is on.
- `PB_LEVELS`: A/X a block of 10 bytes (`PBL_LEN`): eight bands, lowest first, and the VU, left and right, all
  0–255, in one form for every source. The stream's and the ES5503's come from the vis page, its 32 bands averaged
  four at a time. A SID's are estimated: each voice's `SID_ENV` level, scaled by its SID's volume, goes in the band of
  its note's octave (noise in the top band), and SID A's loudest is the left VU, SID B's the right. So for a SID
  `PB_LEVELS` is called once a frame, as `SID_ENV` is, and a meter for it is an estimate.

Each returns carry clear and A = 0, or carry set and A an error. `PB_STATUS` costs some 5,000 cycles for a stream
(two BDOS calls and two 32-bit divisions, counted from the code), less for a SID: call it a few times a second, not
in a hook.

### Music

The linked library plays notes on the two SIDs' six voices, for a program's own music and for GW-BASIC's SOUND
and PLAY. It needs nothing from OODOS beyond OODOS 0.7's function 82, so it works on 0.7 though it came after it.
The notes are played by OOLIB's driver on the card's 6510 (`oodos/lib/mudrv/`), which steps every voice each
millisecond, so the 65C02 spends nothing between notes. Each voice has a queue of 32 entries: notes, rests and
markers. Three modules: `oodos/lib/mu.s`, `munote.s` (notes by number) and `mutone.s` (tones in Hz).

- `MU_INIT`: A the clock, 0 PAL or 1 NTSC. It claims the 6510 (`SC_6510`), loads the driver and starts it.
  `PE_REFUSED` without a card, or while the 6510 is claimed already: a `.sid` can't play at the same time.
  `MU_QUIT` stops it, silences the SIDs and gives the 6510 back; the warm boot does too.
- `MU_NOTE`: A/X a block of 7 bytes (`MU_NLEN`): +0 the voice, 0–5 (SID A's three, then B's); +1 the note, 0 (C-0)
  to 95 (B-7; A-4, 440 Hz, is 57), or `MU_REST`; +3–4 the length in milliseconds (0 is 1); +5–6 the gate time in
  milliseconds. The gate is on for the gate time, then the note's release sounds for the rest of the length; then
  the next entry starts in the same millisecond. A gate time of 0 is silent; one at the length or past it holds
  the whole note. PLAY's MN is 7/8 of the length, MS 3/4 and ML all of it (even then the gate drops and rises
  between two notes, so each note's attack starts afresh). On PAL, B-7 is above the SID's range and plays at its
  top, 3,848 Hz.
- `MU_TONE`: the same block with +1–2 a frequency in Hz (0 a rest): SOUND's. Past the SID's top (3,848 Hz on PAL,
  3,995 on NTSC) it plays the top.
- `MU_INSTR`: A/X a block of 6 bytes (`MU_ILEN`): +0 the voice, +1 the waveform's bits (`SID_V_CTRL`'s: noise,
  pulse, saw, triangle, ring and sync; the gate and test bits are ignored), +2 attack and decay, +3 sustain and
  release, +4–5 the pulse width. It applies from the voice's next note. At the start every voice is a 50% pulse
  with no attack and full sustain, like a speaker.
- `MU_STOP`: A a voice, or `MU_ALL`. The queue is emptied, the gate dropped and the count set to 0.
- `MU_PAUSE` and `MU_RESUME`: every voice held where it is, both SIDs' volume 0 meanwhile.
- `MU_MARK`: A a voice, X a marker, 0–14 (15 is the library's own). It takes no time: `MU_EVENT` says `MV_MARK` when
  the voice reaches it, for GW-BASIC's ON PLAY or a program's own sync points.
- `MU_VOLUME`: A a SID (0, 1 or `MU_ALL`), X its volume, 0–15 (15 at the start), beside `PB_VOLUME`'s level and
  balance.

The driver answers through the mailbox, one byte at a time, which reaches the program as an `EK_SOUND` event in its
event area ([The 6510](#the-6510)). Open an area of layout 2 first, and hand each `EK_SOUND` event's value byte to
`MU_EVENT`: A = what it was, X the voice, Y a marker's number. `MV_DONE` (an entry finished), `MV_EMPTY` (the voice
had nothing waiting when the driver said so), `MV_MARK`, `MV_REFUSED` (its queue was full), `MV_HELLO`, or
`MV_NONE` for a byte that isn't the driver's or is left over from a `MU_STOP`. `MU_COUNT` with A a voice gives its
entries waiting, GW-BASIC's PLAY(n): those sent, less those `MU_EVENT` has been handed as done, reached or refused.
So the count is only as current as the events handed over (and needs them queued: not with `ES_NOQUEUE`,
[The 6510](#the-6510)), and `MU_NOTE` says `PE_FULL` at 32 even if the driver
has played some meanwhile: hand the events over often (a queue of 256 bytes holds 15).

Each call but `MU_EVENT` and `MU_COUNT` returns carry clear and A = 0, or carry set and A an error: `PE_STATE`
before `MU_INIT`, `PE_RANGE` (a voice, note, marker or volume out of range), `PE_FULL`, or `PE_REFUSED`. Each
command goes to the driver by `SN_SEND`, a byte at a time, so a note costs seven BDOS calls. A driver that takes
nothing for 50 ms (the 6510 stopped by something else) makes the call `PE_REFUSED`.

The driver takes about 19% of the 6510 with six notes held. Notes of a few milliseconds on several voices make
events faster than the mailbox carries them, and the driver drops what it can't keep; an event the program's queue
has no room for is lost too. Either leaves that voice's count high until its next `MU_STOP`. PLAY's shortest note is
about 15 ms, far from that.

### What sound costs

Nothing a tick: the tick with no area is 145 cycles and with a layout-2 area 234, as before. Each terminal count of
a piece is an interrupt of about 530 to 700 cycles, about 850 at a buffer's end (the area written); with room in the
FIFO the next piece's 4,096 cycles of DMA come inside it. 44.1 kHz 16-bit stereo is 43 pieces a second, under 0.5%
of the CPU. A frame with the stream claimed and a layout-2 area open costs about 300 cycles more (688 against 389),
for the area's sound part.

## Graphics

L4's graphics are in two parts. What needs a DMA channel, the vertical blank or physical memory is OODOS's,
[function 83](07-function-reference.md#bdos-83): loading a file's records into memory, copies and fills, copies at
the vertical blank, the mouse's pointer sprite, and the console's screen back. What a program draws with is
register and memory work with nothing to share, so it is **OOLIB's linked library**: `oodos/lib/gx*.s`, linked from
`build/oolib.lib`, its calls in `include/oolib.inc`. The video card itself, its layers, sprites and registers, is
the [Technical Reference's chapter 6](../oo1-technical-reference/06-video.md).

### Video memory

The video card draws from banks 8–B, 256 KB, in **units** of 1 KB that its base registers name. Units 0–12
(`$80000–$833FF`) are the console's: its palette (unit 0), its font (1–4) and its text screen (8–12). They are
OODOS's: no destination of function 83 may touch them, nor a record read through a DMA segment, and the console's
block, 32, is never in W1. So the console's text, written on while a game has the screen ([hw/oolib.md, question
10](../../hw/oolib.md#open-questions)), shows again as it was at the warm boot. The running program has units
13–255 whole, as it has its TPA; the library hands them out (`GX_ALLOC`, `GX_FREE`), aligned to 8 units for 8 or
more. Units 13–15 are in block 32, so DMA reaches them and the CPU doesn't: the library keeps them for palettes and
sprite tables, which go by DMA.

A program's CPU reaches video memory through W1, which the library maps itself ([chapter 5, "The W1
contract"](05-memory-and-banking.md#the-w1-contract)). A program using the library keeps its code, its event area
and its data out of `$4000–$7FFF`.

### Video memory on revision 2

**Proposed.** On revision 2 of the video card (ID `$92`) video memory is the card's own 2 MB, not banks 8–B
([chapter 5](05-memory-and-banking.md#video-memory-on-revision-2)). What a program does stays the same: it asks the
library for video memory and frees it, and never chooses an address OODOS owns. What it gets changes:

- **Where.** The library allocates from card memory `$040000–$1BFFFF`, 1.5 MB on a 2 MB card; OODOS keeps
  `$000000–$03FFFF` (the console, as units 0–12 are today, its sprites and the pointer) and Apple II mode's
  `$1C0000–$1FFFFF`. How much is there comes from the card's MEMSIZE, so a larger card gives more.
- **How it is named.** A card address is 24 bits, and the card's base registers count in 256-byte units, not 1 KB
  units. How `GX_ALLOC` and `GX_FREE` take and return an allocation (its unit, its alignment, the `GX_` variables
  it uses), and how an asset part names where it loads, are *TBD: stream C*.
- **Drawing.** The library plots, fills and copies through an aperture slot it maps into W1, or through the card's
  data port, under the W1 contract as revised ([chapter 5](05-memory-and-banking.md#video-memory-on-revision-2)).
- **Loading and copies.** `DM_LOAD`, `DM_COPY` and `DM_FILL` reach card memory through the data port by DMA, and
  the vertical-blank slots and the pointer go through the port on channel 1, from the frame handler as now. How a
  parameter block names a card address rather than a physical one, and what each costs, are *TBD: stream C*.
- **Room to spare.** Double-buffered screens and large tile and sprite sets fit, and a full-width 640×480 bitmap
  (300 KB at 8 bits) would too; the card has no such mode, and adding one is a separate decision
  ([hw/ooui.md, "Bitmap backend"](../../hw/ooui.md#bitmap-backend)).

### Loading

`DM_LOAD` loads a count of a file's records, up to 512 (64 KB), from a record of an open FCB, to a physical address:
whole sectors straight from the SD card by DMA, at most 8 a READ, and the ends through a BDOS buffer. 64 KB takes
about 16 ms from an `oodos-8m` drive, against 87 ms by functions 51 and 33 a record at a time. It doesn't wait for the
vertical blank: a program loads into a buffer it isn't showing, then flips to it.

```
        lda     #<load
        ldx     #>load
        ldy     #O_DMA
        jsr     BDOS            ; A = 0: loaded; 1: the file ended first
        ...
load:   .byte   DM_LOAD
        .word   fcb             ; opened by function 15
        .byte   0, 0, 0         ; from record 0
        .word   480             ; 60 KB
        .byte   $00, $40, $08   ; to $84000: unit 16
```

### Copies and fills

`DM_COPY` copies and `DM_FILL` fills physical memory by DMA, 2 cycles a byte, in the BDOS's time: a scroll of a
bitmap, a cleared screen, the console's font copied out for text. The library fills a bitmap's whole rows in one
call.

### The vertical-blank slots

Four **slots**, 1 KB of copies in all, run at the vertical blank: a sprite table and a palette from the program's
memory to the card's, say, so that a frame's changes show together and nothing is torn. `DM_VBSET` sets a slot's
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
card's sprite table are the mouse's, from a layout-2 event area, less the hotspot. The program draws the image, sets
the sprite's other bytes, and puts the mouse in absolute mode with limits 319 and 239 ([function
81's](07-function-reference.md#bdos-81) `IN_MOUSE`), since sprites live in a 320 × 240 space.

### The library

`GX_INIT` first, with the event area's address (or 0): it checks OODOS's interface (4 at least, with `OA_DMA`) and
reads the W1 map. Then:

- **Layers and memory** (`gxmode.s`, `gxmem.s`): `GX_LAYER` a layer's type and units, `GX_SHOW` the display,
  `GX_BORDER`, `GX_PALBASE`, `GX_SPRBASE`, `GX_SCROLL`, `GX_FLIP` (double buffering: MAPBASE, latched at the
  vertical blank, then the frame waited for), `GX_CONSOLE`; `GX_ALLOC`, `GX_FREE`, `GX_BLOCK`, `GX_W1BACK`.
- **The palette** (`gxpal.s`): a shadow of 256 entries on slot 1, `GX_PALSET`, `GX_PALLOAD`, `GX_PALSHOW`, and
  `GX_FADE` to another palette over n frames, a step a frame.
- **Sprites** (`gxspr.s`): a shadow table of 64 entries on slot 0, `GX_SPRPOS`, `GX_SPRIMG`, `GX_SPRATTR`,
  `GX_SPRSHOW` (refused with no event area, as `GX_PALSHOW` and `GX_FADE` are); `GX_COLL` the groups that collided, from the card's STATUS and ISR.
- **Tiles** (`gxtile.s`): `GX_TMAP`, `GX_TPUT`, `GX_TGET`, `GX_TFILL`.
- **Bitmaps** (`gxbmp.s`): bitmap 256 (8 bits a pixel), bitmap 512 (4) and mono (1): `GX_PLOT`, `GX_POINT`,
  `GX_LINE`, `GX_RECT`, `GX_FRECT`, `GX_CIRCLE`, `GX_FLOOD` (a fill of a region, by spans: GW-BASIC's PAINT) and
  `GX_BCOPY`. Draws are clipped to the bitmap (one wholly off it draws nothing); a copy whose rectangle leaves it is
  refused (`GE_BOUNDS`, as is a copy on a mono bitmap or at an odd x or width at 4 bits), and one of no width or
  height does nothing. A copy's rows are each one DMA copy forward, as memcpy, not memmove: moving a strip right
  on the same rows repeats its first bytes, so copy through another place for that.
- **Text** (`gxtext.s`): an 8 × 16 font on a bitmap, the console's or the program's: `GX_FONT`, `GX_TEXT`.
- **Assets** (`gxasset.s`): `GX_ASSET` opens an asset file and loads its parts by `DM_LOAD`, each part checked
  first (its type, its place in the file, its records against its bytes, its units free), and every unit freed again
  if a part is refused.

A program links only the modules it calls. Coordinates and values go in the `GX_` variables; a refusal is the carry
set with an error in A (`GE_`).

### Assets

An **asset file** (format 3) holds a picture's parts, ready for the card: a palette, a bitmap, tiles and their map,
sprite images and a sprite table, a font. Record 0 has a 16-byte header (`'OGX'`, the format, the parts) and up to
7 parts of 16 bytes (the type, the layer's CONFIG it was made for, a unit or `$FF` for any, the first record, the
records, the bytes, a width and height); each part starts on a 1 KB boundary of the file, so it loads straight from
the card. `oodos/tools/mkasset.py` makes them from PNG images on Linux (`bitmap256`, `bitmap512`, `tiles`,
`sprites`, `palette`), and checks one (`check`). [VIEW](../oodos-users-guide/05-utilities.md#util-view) shows one.

### What graphics costs

With no slot set and no pointer, nothing: the tick with no event area is 145 cycles and the frame 199, as before,
and a layout-2 area's frame 7 cycles more (396). A slot set but its bit not is 516 a frame; a 512-byte slot copied,
1,774 (1,024 of them the DMA's); the pointer, 728; the whole budget, four slots of 256 bytes, with the pointer and a
44.1 kHz stream's fields, 3,645. That ends inside the vertical blank even with a USB batch or a 4 KB load piece
holding the interrupt off (`test_oodos_video`: line 508 at the latest under a USB batch, 16 lines to spare, and line 511 under 64 KB loads,
13 to spare; the blank is lines 480–524).

## Linking the library into an overlay

An overlay that [function 59](07-function-reference.md#bdos-59) loads has no zero page of its own: the loader
refuses one whose header says it has. It uses its caller's, by importing the caller's names, which never move. So
each library module's zero page is in a module of its own, `lib/zp*.s`, which holds nothing else (since
2026-11-10; nothing in OODOS changed for it):

- **A program** links the library as before, to the byte. ld65 takes a zero page module with the code that names
  it, and `lib/zpNAME.s` is in `oolib.lib` right after `NAME`, so the program's zero page is laid out as it was.
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
| `gxasset`, `gxbmp`, `gxmem`, `gxmode`, `gxpal`, `gxspr`, `gxtext`, `gxtile` | `GXP`, `GXQ`, `GXT` | 6 | `zpgxmem` |
| `gxpal` | `GXPAL_ZP` | 4 | `zpgxpal` |
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

## When the program ends

The warm boot takes back what L1 and L2 gave: both hooks off, the event area closed, and every interrupt enable the
program turned on off again (OODOS keeps only its own: timer 1 and the vertical blank). For the input it turns the
USB card's interrupt off, puts the mouse back in relative mode with the limits a reset gives (639, 479), the LEDs
off, the repeat as a reset sets it (`$10`), Pause on Break, and the controllers 1 to 4 in order: what the cold
boot sets. For L3's sound it does what the BIOS's Break does to the card, whether a program claimed it or not: the
6510 stopped, the SIDs silenced, the stream flushed and stopped, the ES5503 stopped; and channel 2 aborted, the card's
DRQSEL back to 0 and its interrupts off, the ES5503 hook off, every part released, a hold forgotten, and the master
volumes back at 255, and (from OODOS 0.7; before it, the EQ was left as the program set it) the EQ off with every
gain 0. Timer 1 is set up again
as the cold boot left it, should the program have changed it. For L4's graphics it frees the vertical-blank slots,
turns the pointer off, and sets the video card's registers as the cold boot set them for the console (its memory,
units 0–12, was never the program's, so its text shows again as it was). That is so whether the program ends by `JMP WBOOT`,
by ^C, or by an error. The counters run on (a program that changed timer 1's latch may cost the millisecond
counter 1 ms: putting the latch back clears timer 1's flag, and a tick due then with it). Break goes to the
monitor with the machine's interrupts off, as it always has; booting OODOS again starts the clocks afresh.

## Still to come

GW-BASIC's SOUND, PLAY and BEEP (on [Music](#music) and the beep, with GW-BASIC itself); RSID tunes and MOD; USB gamepads as controller
4; for graphics, a scan-line hook and a collision hook, software sprites, a C target for the library, and a copy of
the library for FARCALL (with GW-BASIC): [hw/oolib.md, "The plan"](../../hw/oolib.md#the-plan). Until a service is
built, a program has none for it: see [AGENTS.md](../../AGENTS.md) for what not to take over in the meantime.

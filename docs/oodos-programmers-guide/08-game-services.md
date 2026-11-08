# 8 Game services (OOLIB)

The services a game or GW-BASIC calls for the screen, sound, timing and input: OOLIB. They come in steps, L1 to
L6. This chapter has what is built: **L1, the clocks**: a millisecond counter, a frame counter, a block of the
program's memory OODOS keeps them in, waiting for a frame, and routines of the program's that OODOS calls at each
frame or every few milliseconds. All of it is [function 80](07-function-reference.md#bdos-80).

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

A is OOLIB's interface version, 1 since L1; each step that adds to the services raises it. X has a bit for each
service area OODOS has, one BDOS function each from 80: `OA_FRAME` (`$01`, 80, frame and timing) now, and later
`OA_INPUT` (81), `OA_SOUND` (82), `OA_DMA` (83) and `OA_VIDEO` (84). An OODOS without the services answers 80 as a
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
every program. An open event area adds 43 cycles a tick, and each hook its own time.

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

- **The area must** be at least 16 bytes (`EV_LEN`), in one of W0–W2's 16 KB windows without crossing into the
  next, and in the program's own RAM: its blocks 0–2, or a block it allocated in banks 1–5 and mapped. OODOS writes
  it through the block that window had at the call, so a program that maps another block there later doesn't move
  the area.
- **At the open** OODOS writes all of it: the layout, the clocks, the size, the counters, and 0 everywhere else, up
  to the size given. An area already open is closed first.
- **Layouts grow at the end.** L2 adds the input's (the event queue, the keys held, the controllers) after +16. A
  program that gives 16 bytes gets layout 1's; one that gives more gets what its OODOS fills and zeros past that. The
  layout byte says which it got.
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

## When the program ends

The warm boot takes back what L1 gave: both hooks off, the event area closed, and every interrupt enable the
program turned on off again (OODOS keeps only its own: timer 1 and the vertical blank). Timer 1 is set up again
as the cold boot left it, should the program have changed it. That is so whether the program ends by `JMP WBOOT`,
by ^C, or by an error. The counters run on (a program that changed timer 1's latch may cost the millisecond
counter 1 ms: putting the latch back clears timer 1's flag, and a tick due then with it). Break goes to the
monitor with the machine's interrupts off, as it always has; booting OODOS again starts the clocks afresh.

## Still to come

L2 input (the event queue, the keys held, controllers, the mouse), L3 sound, L4 the graphics library and the
asset loader, L6 the music driver: [hw/oolib.md, "The plan"](../../hw/oolib.md#the-plan). Until a service is
built, a program has none for it: see [AGENTS.md](../../AGENTS.md) for what not to take over in the meantime.

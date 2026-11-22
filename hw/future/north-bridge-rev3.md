# North bridge revision 3: the timebase, and what the revision adds #

The design record for north bridge revision 3 (ID `$D3`), as a whole. Revision 3 adds two things to revision 2's
bridge: **the random number source**, an ATECC608B driven by the bridge's I²C engine, whose spec is
[random-source.md](random-source.md) (on `docs/random-source` until it merges); and **the timebase**, specified in
full here: a 1 kHz tick of the bridge's own, a free-running 32-bit millisecond counter, and a "milliseconds into
this second" count that gives the DS3231's time to the millisecond. The two documents together are the revision's
spec.

**Status: draft, 2026-11-21, for the owner to read.** Nothing is built. **Decided** means the owner's decisions of
2026-11-21 (the timebase moves to the bridge; OODOS 0.16 requires revision 3; the 1 Hz interrupt dropped; timer 1
polled only: [the questions](#open-questions), all answered); everything else is **Proposed**. Hardware is treated as the
video card's spec treats it: this says what the bridge does, and assumes it can be built to do it. When approved,
the hardware sections go into hw/design.md beside ["Real-time clock"](../design.md#real-time-clock), in our own words
as here.

## Summary ##

| Area | What | Status |
|---|---|---|
| The revision | north bridge revision 3, `$D3`: the random source ([random-source.md](random-source.md)) and the timebase | **Decided** |
| The tick | 1 kHz, from the master oscillator (OSC, 14.31818 MHz), exact over every 11 ms; its own flag, enable and acknowledge; IRQH bit 5 | **Decided** (the tick on the bridge); the details Proposed |
| The millisecond counter | 32 bits, free-running, cleared by a hard reset, read through a latch | **Decided** (the counter); the details Proposed |
| Time to the millisecond | MSS, 0-999, restarted by each of the DS3231's seconds, latched with SEC-YEAR | **Decided** (the count); the details Proposed |
| The 1 Hz TICK interrupt | dropped: TIE reads 0 on revision 3; TICK and ALARM stay as flags to poll | **Decided** (the owner, 2026-11-21: [Q2](#open-questions)) |
| Registers | MSS at `$1C-$1D`; TBCTL, MS0-MS3 and TBSUB at `$28-$2E`; no collision with `$19-$1B` | Proposed |
| OODOS 0.16 | requires revision 3: the tick is the bridge's alone, the VIA timer 1 tick retired; on `$D1` or `$D2` it refuses to boot, with a message | **Decided** (the owner, 2026-11-21: [Q1](#open-questions)); the refusal Proposed |
| What goes | the VIA's tick, OODOS's own `MS`, the Disk II's catch-up, `T1NOW`, `FT_DELAY`'s stopped-timer check, OODOS's revision 1 paths | Proposed |
| The VIA's timer 1 | the program's under OODOS 0.16, polled only: IER stays OODOS's, timed calls go through the tick hook; AGENTS.md's rule changes | **Decided** (the owner, 2026-11-21: [Q3](#open-questions)); the exact terms Proposed |
| OOLIB | interface 17 (with the random source's function 78): `FC_T1`, `FT_CLOCK` | Proposed |
| The cost | a tick about 139 cycles from 162, estimated; measured in T3 | Proposed |
| Versions | as the random source's: north bridge `$D3`, ROM BIOS 0.20, OODOS 0.16, OOLIB interface 17 | Proposed |

## Why ##

The owner's direction (2026-11-21): move the tick to the north bridge and count its ticks there, with the time of
day staying the DS3231's ("Could we move the tick to the northbridge AND count those ticks ... then we eliminate an
entire interrupt from the RTC?"; then, on the adjusted proposal, "yes, add it to the north bridge rev 3 plan").
Today:

- **OODOS's timebase needs the serial card.** The tick is timer 1 of the serial card's VIA, free-running at a latch
  of 7,157 (a period of 7,159 cycles). A machine without the VIA has no millisecond counter (`FC_MS` clear), so no
  `FT_DELAY`, no tick hook and no beep length.
- **It runs 12.7 ppm fast.** A millisecond is 7,159.09 cycles; the VIA can only count whole ones, so the counter
  gains about 1.1 s a day before the crystal's own error.
- **It loses time.** The VIA has one flag, so anything that holds the tick off past a second expiry loses a
  millisecond. OODOS works round it: DMA copies go 2 KB at a time, the warm boot touches timer 1's latch only if a
  program changed it, and the Disk II driver, which masks interrupts for each sector field, adds the time back from
  the bytes it read, to about half a percent.
- **The tick costs a VIA flag check** in the dispatcher's fast path, and reading `T1CL` to clear it.
- **Timer 1 is off limits to programs**, even to read its low byte; its hooks may read only `T1CH` and the latches.
- **The 1 Hz TICK interrupt is unused.** OODOS turns TIE off; nothing in the ROM turns it on.

Revision 3 gives the bridge its own tick and counter. The counter can't lose a millisecond, whatever masks the
interrupt; the tick needs no card; the VIA's timer 1 goes back to programs; and the DS3231's second, read by the
bridge each second as now, gives the time of day to the millisecond with no drift handling in software.

## What revision 3 adds ##

| Part | Spec | Registers | Interrupt |
|---|---|---|---|
| The random number source: an ATECC608B on the bridge's I²C bus, a 32-byte pool | [random-source.md](random-source.md) | RNGST, RNGCNT, RNGDATA, `$19-$1B` | none |
| The tick and the millisecond counter | [below](#the-tick) | TBCTL, MS0-MS3, TBSUB, `$28-$2E` | ID D, IRQH bit 5 |
| The time to the millisecond | [below](#the-time-to-the-millisecond) | MSSL, MSSH, `$1C-$1D` | none |
| The 1 Hz TICK | [below](#the-1-hz-tick) | RTCCTL's TIE reads 0 | dropped |
| The ID | `$D3`; STATUS's bits 7-4 read 3 | `$7F` | |

Software uses any revision 3 register only after reading NB_ID as `$D3`: on `$D1` and `$D2` the new offsets read
`$FF` and drop writes. A write is harmless anywhere, so the BIOS's Break may write TBCTL without the check. The ROM
BIOS still runs on all three revisions; OODOS 0.16 reads NB_ID once, at cold boot, and runs only on `$D3`
([below](#revision-3-required)).

## The timebase ##

### The tick ###

**The divider.** The bridge counts OSC, the backplane's 14.31818 MHz master clock (exactly 315/22 MHz), which runs
in every mode: native ÷2, Apple II mode's ÷14 and ÷16, and a turbo if one ever comes. A millisecond is 14,318 2/11
OSC clocks, so in every 11 ms nine periods are 14,318 clocks and two are 14,319: 157,500 clocks, exactly 11 ms of
the crystal. Put without the pattern: **the k-th tick comes ⌊157,500 k / 11⌋ OSC clocks after the counter started.**
A period is never more than one OSC clock (70 ns) from the ideal, and no error builds up within the bridge: the
counter's rate is the crystal's, and its only drift is the crystal's own tolerance, which the DS3231's second
corrects for the time of day ([below](#the-time-to-the-millisecond)).

In native cycles (two OSC clocks), a period is 7,159 cycles, or 7,160 once in every 11 ms (78,750 cycles per 11 ms).

**At each tick** the bridge adds 1 to the millisecond counter and sets **MSF**. If MSF was still set, it sets
**OVR** too: one interrupt now stands for two or more ticks, but the counter has all of them.

**The interrupt.** MSF with **MSIE** set pulls the bridge's /IRQ, ID D, which reads as IRQH bit 5. On revision 3
that is the bridge's only interrupt: the 1 Hz TICK's is dropped ([below](#the-1-hz-tick)) and the random source has
none. So a poll of IRQL `$FF` and IRQH `$DF` means the tick and nothing else, with no flag to check.

**The acknowledge.** Writing 1 to MSF clears it, and 1 to OVR clears OVR; writing 0 leaves either as it is. A read
clears nothing, so a debugger's look or a poll can't lose a tick. MSIE takes the value written. Writing back what
was read clears exactly the flags that were set and keeps MSIE, so a read-modify-write is safe here (unlike
RTCCTL).

### The millisecond counter ###

**MS0-MS3** hold a 32-bit count of ticks, low byte first. It is cleared while /RESET is held at a hard reset (the
power-on one included) and starts at /RESET's release, so the reset's first instruction finds 0 and the count of
milliseconds since. A /RESET pulse (entering Apple II mode, Ctrl-Reset there) leaves it running. It is read-only,
and goes round after 2³² ms: 49 days, 17 hours, 2 minutes and 47.296 seconds.

**The latch.** Reading MS0 copies MS0-MS3 and TBSUB into a latch, and MS1-MS3, TBSUBL and TBSUBH read the latch,
so the four bytes belong to one moment: read MS0 first. A later read of MS0 takes a new latch, so **a multi-byte
read must not be interrupted by another reader**: OODOS reads all four with interrupts off outside its handlers
([below](#the-counter-in-oodos)). A debugger's `peek()` takes no latch.

**TBSUB** is how far into the current millisecond the latch was taken: the divider's count of OSC clocks since the
last tick, halved, so 0-7,159 native cycles. It is for OODOS's debug build, which times hooks by it in place of
timer 1 ([below](#the-debug-builds-hook-costs)).

### The time to the millisecond ###

**MSS** (MSSL, MSSH) is the milliseconds into the DS3231's current second, 0-999, binary, low byte first.

- **Where a second starts.** The bridge already reads the time at each falling edge of SQW, the DS3231's 1 Hz
  square wave ([design.md, "Real-time clock"](../design.md#real-time-clock)). The edge is where the chip's second
  begins. MSS counts milliseconds from the edge, by its own divider restarted at the edge (the tick's 2-in-11
  pattern, from the edge), not by the free-running tick, so it is in step with the chip and not with the counter.
- **SEC-YEAR and MSS change together.** The time in SEC-YEAR changes when the second's read lands, 330 µs after the
  edge (longer if a random-source transaction is on the bus: up to 815 µs more). Until then MSS goes on counting the
  old second; when the read lands, SEC-YEAR take the new time and MSS the milliseconds since the new edge (0, or 1
  after a long wait). So the time is monotonic, and never the old second with the new second's milliseconds.
  Precisely: MSS = min(999, the whole milliseconds since R), where R is the edge whose read last landed.
- **Never 1,000.** MSS holds at 999 if a second runs long against the bridge's crystal (the two can differ by the
  crystal's tolerance, tens of ppm, a few hundredths of a millisecond a second). Nothing accumulates: every edge
  starts it again. This is the "no drift handling": software never corrects anything.
- **Latched with SEC.** Reading SEC copies SEC-YEAR and MSS into the latch, so SEC, MIN to YEAR, then MSSL and MSSH
  read one moment, to the millisecond. MSSL and MSSH read the latch; read SEC first.
- **SET** restarts the chip's second (design.md). R becomes the end of SET's transaction, so MSS starts from 0 with
  the time that was sent. An edge that follows (the emulated chip gives one) restarts it from 0 again, with the same
  second: still monotonic.
- **Before the first second.** At power-on R is the end of the power-on read of the time: the bridge doesn't know
  where in its second the chip is, so MSS is up to a second behind until the first edge. **SYNC** in TBCTL says
  whether it is in step: clear from power-on until the first edge's read lands.
- **If the chip stops.** SYNC clears when no edge has come for 1.1 s (a chip missing, dead, or its SQW stuck); MSS
  then holds at 999 and SEC-YEAR at the last time read. LOST (the chip's OSF) is separate: it says the time is
  invalid, not that the seconds have stopped. **The tick and the millisecond counter never depend on the DS3231.**

### The 1 Hz TICK ###

**Decided (the owner, 2026-11-21: [Q2](#open-questions)): the interrupt is dropped.** On revision 3, RTCCTL's TIE reads 0 and writes to it
are ignored; TICK can't interrupt. TICK itself stays, set at each second's read and cleared by writing 1, and ALARM
stays: both are flags to poll. The ROM's launcher counts its 5-second prompt by polling TICK (`diag23.s`), which
needs the flag, not the interrupt.

**Not taken: an alarm-only interrupt** (TIE meaning "ALARM interrupts"). Nothing uses the DS3231's alarms today; a
program can poll ALARM; and an interrupt for it would be one more thing for OODOS's dispatcher to tell apart from the
tick in its fast path. If a use comes, a later revision can add it as a new bit, without taking TIE's meaning from
revision 2.

### The registers ###

Offsets from `$C680`. The table is revision 3's whole map: revision 2's registers are unchanged, and `$1E-$1F`,
`$26-$27` and `$2F-$7E` stay free.

| Offset | Name | Access | Purpose | Reset |
|---|---|---|---|---|
| `$00-$06`, `$08-$0D`, `$10-$18`, `$20-$25` | as revision 2 | | WIN0-WIN3, VEC, MODE, STATUS, IRQL, IRQH, WAIT0-3, SEC-YEAR, RTCREG, RTCDATA, WIN0H-WIN3H, VECH, MAP | as revision 2 |
| `$07` | RTCCTL | read/write | as revision 2, but **bit 6 TIE reads 0 and ignores writes** ([above](#the-1-hz-tick)) | as revision 2 |
| `$10` | SEC | read | as revision 2; reading it latches MSSL and MSSH too | |
| `$19-$1B` | RNGST, RNGCNT, RNGDATA | | the random source ([random-source.md](random-source.md#the-registers)) | |
| `$1C` | MSSL | read | MSS's low byte, from SEC's latch | |
| `$1D` | MSSH | read | MSS's high byte (0-3), from SEC's latch | |
| `$28` | TBCTL | read/write | **Bit 7 MSF:** a tick since the last acknowledge; write 1 to clear. **Bit 6 OVR:** a tick came while MSF was set; write 1 to clear. **Bit 5 MSIE:** MSF interrupts, on IRQH bit 5 (ID D); takes the value written. **Bit 4 SYNC:** MSS is in step with the DS3231's second (read-only). Bits 3-0 read 0 | MSF, OVR, MSIE 0; SYNC 0 after power-on, kept by a later reset |
| `$29` | MS0 | read | the millisecond counter's low byte, live; **reading it latches MS1-MS3 and TBSUB** | 0 at /RESET's release (hard reset) |
| `$2A-$2C` | MS1-MS3 | read | the counter's other bytes, from MS0's latch | 0 |
| `$2D` | TBSUBL | read | native cycles into the millisecond, 0-7,159, from MS0's latch | |
| `$2E` | TBSUBH | read | its high byte | |
| `$7F` | ID | read | **`$D3`** | |

- **IRQH bit 5** (ID D) becomes "the bridge's: TBCTL's MSF, with MSIE" on revision 3.
- **backplane.inc:** `NB_MSSL`, `NB_MSSH`, `NB_TBCTL`, `NB_MS0`-`NB_MS3`, `NB_TBSUBL`, `NB_TBSUBH`; `NB_TB_MSF`
  `$80`, `NB_TB_OVR` `$40`, `NB_TB_MSIE` `$20`, `NB_TB_SYNC` `$10`; `NB_IRQ_NB` `$20` (IRQH bit 5); `NB_ID_V3`
  `$D3` (the random source's spec names it too).
- **Read side effects:** MS0 and SEC latch. Read them with a load (LDA, LDX or LDY), never a read-modify-write or a
  16-bit read, each of which can read twice. RNGDATA's rule is the random source's.

### Power-on, resets, Apple II mode and Break ###

- **Power-on.** The bridge holds /RESET until its set-up is done: the DS3231's, then the random source's (about
  31 ms). The divider and the counter are held at 0 meanwhile and start at the release. (The wording follows the
  random source's correction, [its Q2](random-source.md#open-questions): the bridge orders its own power-on; the
  DS3231's RST doesn't gate I²C.)
- **Any reset** clears MSF, OVR and MSIE, so no tick interrupts until software turns MSIE on. A hard reset clears
  the counter; a /RESET pulse doesn't.
- **Apple II mode.** The bridge's registers are hidden, the /RESET pulse that enters the mode has cleared MSIE, and
  the counter runs on at the same rate, since it counts OSC, not PHI2. Nothing in Apple II mode sees it.
- **Break** (the ROM BIOS 0.20): beside RTCCTL's `$80` it writes `$C0` to TBCTL, both flags cleared and MSIE off,
  with no ID check (on an older bridge the write is dropped). Break leaves the counter running.

### What the bridge's logic grows by ###

A 14-bit divider with its 2-in-11 step; a 32-bit counter and a 46-bit latch (the counter and TBSUB); MSS's two
10-bit counts (the old second's and the new one's, swapped when a read lands) with their divider, and a 10-bit latch
beside SEC-YEAR's; SYNC's 1.1 s timeout; TBCTL. No I²C traffic is added: MSS uses the edges the bridge already
reads. It is small beside the random source's engine.

### Considered and not taken ###

- **Counting in OODOS, from the bridge's tick alone.** Ticks held off would still be lost, and the Disk II's
  catch-up would stay. The owner's decision has the counter in the bridge.
- **A software wall clock seeded from the DS3231** and counted by the tick: it drifts by the crystal's error and
  needs correcting each second. MSS gives the same answer with nothing to correct.
- **The tick from the DS3231's 32 kHz output.** It would lock the counter to the chip (±2 ppm), but 32.768 kHz
  doesn't divide to 1 kHz (a 30 µs jitter), it needs a pin wired to the bridge, and the tick would stop with the
  chip. MSS already gives the chip's time to the millisecond.

## OODOS 0.16 ##

### Revision 3 required ###

**Decided (the owner, 2026-11-21: "require revision 3").** OODOS 0.16 has one tick, the bridge's. The VIA timer 1
tick is retired entirely: no fallback for `$D1` or `$D2`.

**On an older bridge OODOS refuses to boot (Proposed).** The cold boot's first act, before it turns anything on,
is to read NB_ID. Anything but `$D3`: it prints, on the BIOS's console and the serial line,

    OODOS 0.16 NEEDS NORTH BRIDGE REVISION 3 ($D3). THIS ONE IS $D2.

and goes back to the monitor through the BIOS's Break, with no interrupt of OODOS's on and nothing of the card
written. A refusal is plain: the machine says why, and nothing half-works. *Not taken:* booting without a
millisecond counter (`FC_MS` clear), as OODOS does today without a VIA. It would boot, but `FT_DELAY`, the tick
hook, the beep, the sticks' sampling and the random source (revision 3's too) would all be missing, and a program
that didn't check `FT_INFO` would fail in ways harder to read than one line at boot. The owner's "require" reads as
the refusal, so this isn't a question.

**What that simplifies.** Every machine OODOS 0.16 runs on has the bridge's tick and counter, and revision 2's
registers (WINnH, VECH, MAP), since revision 3 has them:

- one tick path; OODOS's own `MS`, `MSACC`, the Disk II's `CATCHUP`, `T1NOW` and `ADDTK`'s timer use, and the
  warm boot's timer 1 latch check, all go;
- `CLK_MS` is always set, so its tests in `disk2.s`, `sound.s` and `input.s` go (the sticks test VIAL, below);
- `FT_DELAY`'s check for a stopped counter goes: the bridge's counter can't stop, so the `$FF` return it guarded
  can't come (no caller loses a result it could get);
- OODOS's revision 1 paths go: the `$D2` tests in `crt.s` (NB2), `common.s` (WINH0) and the dispatcher's
  "WIN1H reads `$FF`: no register" case become unconditional or go, and so do refusals whose only reason was a
  `$D1` bridge (`GX_BANK8`'s, GW-BASIC's DSB8 test of it). T3 sweeps them, each named in its review;
- the random source's "a bridge before revision 3" answers (`RN_INFO` 0, its O5 case) can't come under OODOS
  0.16; `RNGOK` keeps its CHIP and LOCKED test.

The ROM BIOS keeps supporting revisions 1 and 2: its diagnostics and boot run on all three.

### Cold boot, the warm boot, leaving ###

- **CLKINIT**, after the ID check: TBCTL `$E0` (both flags cleared, MSIE on); `CLK_MS` in CLOCKS, always. The VIA,
  if there is one, gets IER `$7F`, its IFR cleared and ACR `$00`; timer 1 isn't loaded. VIAL (the dispatcher's "a
  VIA is there") is set by the VIA's presence, as now.
- **DEAFEN and the warm boot**: TBCTL `$E0` again (MSIE on, in case a program wrote it), every VIA interrupt off, and
  ACR `$00`, as /RESET leaves it, so a program's timer modes and PB7 output end with it (timer 2 and the shift
  register too, as hw/oolib.md's question 6 has it). Timer 1's latch is no longer compared or written: it is the
  program's, and nothing of OODOS's depends on it.
- **CLKOFF** (function 75: Apple II mode, the monitor) writes TBCTL `$C0`, MSIE off.
- **`CLK_MS` stops meaning "a VIA".** Two places use it for the VIA's ports: L2OPEN's reading of the sticks
  (`input.s`) and the layout-2 tick's sampling, which take VIAL instead. Elsewhere it is always set now, and its
  tests go.

### The dispatcher ###

The fast path tests IRQL for `$FF` (`INC A`, as today's no-card test) and IRQH for `$DF`, and calls TICK. No VIA
register is read. VIASRV treats every VIA flag as a stray: turned off, its flag cleared. In particular a program's
timer 1 can't reach the tick: its IER bit is off, and the dispatcher never reads `T1CL`.

### The tick ###

- **The acknowledge** is the handler's first write: `LDA #$E0` `STA NB_TBCTL` (MSF and OVR cleared, MSIE kept). A
  tick that comes during the handler sets MSF again and interrupts after the RTI.
- **No event area:** nothing else for the counter: it is the bridge's. Then the tick hook's count, as now.
- **An event area open:** TBCTL is read first (`BIT`: OVR in V), then acknowledged, then EV_MS's low byte written
  from MS0. All four bytes are written (EVALL, from the latch) when EVDIRTY is set (the open), when MS0 reads 0 (the
  low byte went round), or when OVR was set (ticks ran together, perhaps across a carry). So the area's counter is
  whole at the next tick whatever held the interrupt off.
- **The tick hook** still counts interrupts, every *n*, as now: ticks that ran together are one call. **The sticks
  and the paddles** are still sampled at the tick, on the VIA's ports (OODOS's in layout 2, as now).

### The counter in OODOS ###

OODOS's own `MS` goes. Every reader takes the bridge's counter:

- **In a handler** (interrupts off): as it likes, a byte or all four. The USB events' and the mailbox's stamps,
  PADDUE's compare and EVALL read the bridge.
- **Elsewhere** (the BDOS's `FT_COUNT`, `FT_DELAY`'s wait, SNAPIT): all four with interrupts off, about 20 cycles,
  so that a handler's latch can't come between MS0 and MS3. A single byte may be read with them on.
- **`FT_COUNT`** gives the bridge's count, and `FT_DELAY` waits on it. Their meaning doesn't change: a millisecond
  is a count, now exactly one of the crystal's. `FT_DELAY`'s stopped-counter check goes ([above](#revision-3-required)).
- **EV_MS** is the bridge's count, as above.

### The Disk II ###

The driver still masks interrupts for each sector field. **CATCHUP goes** (with `MSACC` and the driver's byte
count's use for time); the unmask sets EVDIRTY, so the event area's counter is written whole at the next tick. The
bridge counted every millisecond meanwhile, so the counter is exact through a read or a write (today it is "about
half a percent"). The tick that comes at the unmask carries OVR. Ticks for the hook are missed as now.

### DMA's 2 KB pieces ###

**Kept, with a new reason.** DMAGO's 2 KB pieces and the sound stream's pieces under a tick were for a counter that
lost a millisecond to two expiries under one flag. Now a longer piece loses no time (OVR, and the area's
counter written whole), but it would still merge tick interrupts, so the hook and the sticks' sampling miss a tick,
and it would hold off the serial line's receive interrupt and the stream's next piece for longer. So the pieces
stay; the comment and the `.assert`'s reason in `bios.s` change, and the Technical Reference's chapter 5 says so.

### The debug build's hook costs ###

`FT_COSTS` (the `OOLIB_DEBUG` build) times hooks by timer 1 today (`T1NOW`). It reads MS0 then TBSUB (one latch)
instead, and adds whole ticks as now; `T1NOW` and its late-flag reasoning go. So a debug build never touches the
program's timer 1. `FT_COSTS`'s A (1: counted) is always 1.

### Interface 17: `FC_T1` and `FT_CLOCK` ###

OOLIB's interface goes to 17 with the random source's function 78 (one release; interface 17 covers both). No
existing call changes (the owner's rule: interfaces only grow).

- **`FC_T1` (`$04`)**, a new bit in `FT_INFO`'s clocks (+3) and EV_CLOCKS: **the VIA's timer 1 is the program's**.
  OODOS 0.16 sets it whenever a VIA is fitted; an OODOS before 0.16 never does, so a program that may run on one
  tests it before touching timer 1. `FC_MS` keeps its meaning, "the millisecond counter runs", now always set; its
  comment in oodos.inc stops naming the VIA.
- **`FT_CLOCK` (function 80, operation 9; `FT_OPS` 10):** the time of day to the millisecond, from one latch. Block
  form only: +0 `FT_CLOCK`; the result at +1: +1-4 a DAT as `T_GET`'s (the day, the hour and the minute, BCD),
  +5 the seconds, BCD, +6-7 the milliseconds into the second (0-999), +8 `FK_SYNC` `$01` (SYNC) and `FK_LOST`
  `$02` (LOST). A = X = 0; A = X = `$FF` for a clock past 2155 (as `T_GET`). Its side effect is `T_GET`'s: the
  SCB's date and time. It is for `TIMER` in GW-BASIC (not built yet) and for anything that stamps events in wall
  time. (Proposed.)

### The VIA's timer 1, given to programs ###

Where `FT_INFO` says `FC_T1`, timer 1 is the program's, **polled only** (**Decided**, the owner, 2026-11-21: [Q3](#open-questions)):

- its counter and latches, read and written: `T1CL`, `T1CH`, `T1LL`, `T1LH`;
- ACR's bits 7-6, timer 1's mode (one-shot or free-running; PB7 as its output), written with the rest of ACR kept as
  read;
- its flag, IFR bit 6, polled, and cleared as the chip clears it (reading `T1CL`, writing `T1CH`);
- **not its interrupt.** IER stays OODOS's: OODOS owns the vectors, so a timer 1 interrupt would have no handler of
  the program's; the dispatcher turns it off. A program that needs a timed call has the tick hook.

The warm boot takes it back (ACR `$00`). Hooks follow the program's rule: they may use timer 1 too. (Under an
OODOS before 0.16, which never sets `FC_T1`, the old rule stands.)

### The cycle saving ###

Today's tick with no event area and no hook is 162 cycles (`test_oodos_input`'s pin): the interrupt's 7, IRQ's 19,
the dispatcher's 30 to its poll, the fast path's 37, TICK's 33 and the way out, 36. On OODOS 0.16, estimated:

| Part | Today | OODOS 0.16 | Saved |
|---|---|---|---|
| the fast path: two polls tested, then TICK | 37 (with the VIA's IFR and IER checked) | 22 (IRQL by `INC A`, IRQH by `CMP #$DF`, no VIA) | 15 |
| TICK, no area, no hook | 33 (`T1CL` read, `MS` counted) | 25 (EVON, the acknowledge, THON) | 8 |
| **a tick** | **162** (2.26% of the CPU) | **about 139** (1.94%) | **about 23** |
| a tick with a layout-1 area | 201 | about 188 (OVR and the wrap tested; no `MS` to count) | about 13 |

About 23,000 cycles a second, 0.3% of the CPU: a modest saving. What revision 3 buys is mostly elsewhere: a counter
that is exact and loses nothing, a tick on every machine, and timer 1 for programs. T3 measures the figures with
`test_oodos_input`'s Costs and pins them; the books take the measured ones.

### SYSINFO ###

No new hardware reads: AGENTS.md's SYSINFO list doesn't grow. Its north bridge line names `$D3` (the random source's
`hwname.s` change); under OODOS 0.16 it never meets `$D1` or `$D2`. `HW_CLOCK`'s notes on OODOS's millisecond
handler sliding 90 cycles a second against the DS3231 change: the tick now slides by the crystal's error alone. Its
goldens change only by the random source's line and the revision's name.

## AGENTS.md ##

The rule "Don't take over what OODOS owns" changes (the owner's decision gives timer 1 back; the wording is
proposed):

> ... the DMA channels, the north bridge's windows **and its timebase (TBCTL, MS0-MS3, TBSUB, MSS)**, the sound
> card's 6510, the VIA's IER and IFR, **and its ACR but for timer 1's mode bits (7-6). Timer 1 is the program's,
> polled: its counter, its latches, ACR's bits 7-6 (the rest of ACR kept as read), its IFR flag; never its IER
> bit. (OODOS 0.16 needs north bridge revision 3; a program that may run on an older OODOS checks `FT_INFO`'s
> `FC_T1` first.)**

SYSINFO's exception is unchanged: it writes timer 2 only, never ACR, IER or timer 1.

## The ROM BIOS 0.20 ##

Nothing in the ROM uses a tick before OODOS: the BIOS enables no interrupts. It uses the VIA's timers only in the
diagnostics (SR2 counts both timers across a loop; NB5's `HW_CLOCK` uses timer 2, and the core's own timer 1
free-running), and RTCCTL's TICK only as a polled flag (the launcher's prompt, NB4). All keep working on revision 3.
The changes, beside the random source's (NB9, `$D3` in stage 7, `hwname.s`, the "revision 2 or more" sweep):

- **Break** writes TBCTL `$C0` ([above](#power-on-resets-apple-ii-mode-and-break)).
- **NB10, the timebase** (after NB9), on `$D3` only, else SKIP: MSF sets within 1.1 ms of being cleared, and a read
  doesn't clear it; with MSIE, IRQH bit 5 reads 0 while MSF is set and 1 once it is cleared (polled with I set),
  then MSIE off; MS0-MS3 step by 1,000 ± 1 across one of the DS3231's seconds (two SEC changes) and never go
  backwards across a low-byte carry read through the latch; at each of three SEC changes MSS reads 0 or 1, and never
  more than 999 between them; SYNC set. A FAIL names the step.

## The emulator ##

- **The bridge, revision 3** (with the random source's: `revision=1|2|3`, default 3, ID `$D3`;
  [backplane_bridge.h](../../emulator/components/devices/include/backplane_bridge.h)): the divider in OSC clocks,
  native cycles × 2, Apple II mode's by its 912 clocks in 65 cycles; the k-th tick at ⌊157,500 k / 11⌋; `tick()`
  steps to the next tick's exact cycle, as it steps to the DS3231's edges, so MSF lands on its cycle and DMA cycles
  count; MS, OVR, MSIE, the latches; MSS with R moved when a Refresh lands, its 1.1 s SYNC timeout; TIE reading 0;
  `irq()` is `MSF && MSIE` on revision 3, `TICK && TIE` before; `peek()` takes no latch. Revisions 1 and 2 read
  `$FF` at `$1C-$1D` and `$28-$2E`.
- **The older revisions are kept, for the ROM.** `revision=1|2` stays a machine-file option (Proposed): the ROM
  BIOS still supports both, and its tests (`test_bios`'s B7 cases, `test_bios_diag`'s, `test_video_rev2`'s
  revision 1 alias, `test_session`'s `nb2.machine`, `test_machine`'s parsing) run on them unchanged. On those
  machines OODOS 0.16 refuses to boot, so they lose OODOS and its tick, not the option. No machine file in
  `emulator/machines/` names an old revision: `backplane.machine` takes the new default, 3, and its comment says
  OODOS needs it. The WebUI offers no revision choice, so the site always runs revision 3.
- **The two clocks' rates.** The emulator's NATIVE_HZ is 7,159,090, a whole number, while the divider is 7,159,090.9
  cycles a second: the emulated DS3231's second is 0.13 ppm short of 1,000 bridge milliseconds, which MSS's hold at
  999 absorbs. A test-only skew (`Ds3231::set_skew_ppm()`) runs the chip fast or slow against the CPU clock, to
  test MSS under a real crystal's error.
- **Test hooks:** `set_ms(value)`, to put the counter just before a carry; and the existing `set_rtc_time()`.
- **The debugger:** `describe_timebase()` beside `describe_clock()` (the count, TBCTL, MSS, SYNC); the WebUI's
  `debugger/devices.ts` lists the new registers. The WebUI's whole-second steps of the DS3231 to the host's clock
  keep the second's phase, so MSS is unaffected; the counter is never stepped (it is emulated time since reset).

## Tests ##

Each must-fail case is seen failing under its mutant, on its own case; each mutant is a defect plausible in real
bridge logic, wiring or hand-written 6502 code ([hw/testing.md](../testing.md): the new suite gets its labels,
`bridge` and `rtc`, and a map rule).

**The bridge** (`test_nb_timebase.cpp`, new):

| # | Case | The real failure it guards | Must fail under |
|---|---|---|---|
| B1 | `ticks_come_at_the_crystal_s_millisecond`: ticks 1-1,100 each on cycle ⌊78,750 k / 11⌋ from the release; MS 1,100 | drift: a divider that rounds (12.7 ppm fast, 1.1 s a day) | the 2-in-11 step removed (7,159 always); the long periods on the wrong count (3 in 11) |
| B2 | `the_latch_holds_four_bytes_across_a_carry`: `set_ms($00FFFFFE)`, MS0 read, the carry, then MS1-MS3: one moment; TBSUB from the same latch | a torn 32-bit read | MS1-MS3 read live; TBSUB not latched |
| B3 | `msf_interrupts_until_acknowledged`: MSF with MSIE pulls IRQH bit 5; a read leaves it; a 0 written leaves it; a 1 written lets it go; MSIE off lets it go | the interrupt not acknowledged, or acknowledged by a read | the acknowledge done by a read (as the VIA's `T1CL`); `irq()` not gated by MSIE |
| B4 | `a_tick_under_dma_is_counted`: a 64 KB copy holding the CPU 18 ms: MS +18 exactly, MSF and OVR set, one interrupt | a missed tick under DMA | the counter stepped by the acknowledge, not the tick; OVR never set |
| B5 | `mss_restarts_with_each_second`: the chip skewed +1,000 and -1,000 ppm, 30 s: at each SEC change MSS 0 or 1; never past 999; SEC and MSS from one latch never the old second with 0 | the ms-into-second not reset; a torn time | MSS from the free-running tick modulo 1,000 (not reset); MSSL/MSSH read live, not from SEC's latch |
| B6 | `the_tick_needs_no_clock_chip`: SQW stopped: SYNC clear 1.1 s after the last edge, MSS 999, the tick and MS unaffected; SYNC clear from power-on to the first edge | a bridge whose tick or MSS hangs on the DS3231 | the tick taken from SQW's divider; SYNC never cleared |
| B7 | `tie_is_gone_on_revision_3`: TICK set each second and cleared by 1; TIE written 1 reads 0, no interrupt; revision 2 as before | an unexpected 1 Hz interrupt in the tick's fast path | TIE kept on revision 3 |
| B8 | `resets_and_apple_ii_mode`: a hard reset: MS 0 at the first instruction, MSIE off; a /RESET pulse: MS runs on, MSIE off; in Apple II mode MS gains 1,000 a second | a counter that slows sevenfold in Apple II mode, or restarts on Ctrl-Reset | the divider counting PHI2; the pulse clearing MS |
| B9 | `a_peek_latches_nothing`: the debugger reads MS0 and SEC; a CPU read of MS1 still gives the CPU's latch | the WebUI's memory view tearing OODOS's reads | `peek()` calling `read()` |

**OODOS** (`test_oodos_clock`, `test_oodos_clock_disk2`, `test_oodos_input`, `test_oodos_clock_debug`, on CcpRig,
revision 3):

| # | Case | The real failure it guards | Must fail under |
|---|---|---|---|
| O1 | `the_tick_is_the_bridge_s`: at the prompt MSIE set, VIA IER 0, ACR 0, timer 1's latch as the test left it; `FC_MS` and `FC_T1`; `FT_COUNT` follows MS | OODOS still driving the VIA | CLKINIT still loading timer 1 |
| O2 | `ft_count_is_never_torn`: an event area open, `FT_COUNT` in a loop across MS's 24-bit carry, its start swept a cycle at a time against the tick: every result whole and monotonic | a torn read when the handler's latch lands between MS0 and MS3 | `FT_COUNT`'s interrupts-off removed; MS1 read before MS0 |
| O3 | `the_area_s_counter_is_whole_after_ticks_run_together`: just before a carry, interrupts held off 3 ms (a test program's SEI loop); EV_MS's four bytes right at the next tick | the area's high bytes a carry behind | OVR's test removed from the area path |
| O4 | `a_disk_ii_read_keeps_exact_time`: a sector read and a write on O:, MS as the bridge's, exact | time lost or counted twice under the masked fields | CATCHUP still adding its ticks |
| O5 | `timer_1_is_the_program_s`: a program runs timer 1 one-shot and free-running with PB7, polling IFR bit 6, through 10 s; it sees every flag, OODOS's tick loses none; the warm boot puts ACR to 0, IER off | the program's flag cleared under it by OODOS | the dispatcher's VIASRV still taking timer 1's flag as the tick (reading `T1CL`) |
| O6 | the tick's cost (`test_oodos_input`): the measured figures, no area, layout 1, layout 2, pinned | a regression in the fast path | (the pin) |
| O7 | `oodos_refuses_an_older_bridge` (`test_oodos_sys`): `revision=1` and `revision=2`: the line naming `$D1` or `$D2`, then the monitor's prompt; no OODOS interrupt on (VIA IER, TBCTL, VIDEO_IEN, ACIA_CMD as the BIOS left them), the card not written | OODOS half-running on a bridge without the tick | the ID check accepting `$D2` and up; the check after CLKINIT (an interrupt turned on first) |
| O8 | `ft_clock_reads_the_time_to_the_ms`: the DAT as `T_GET`'s; ms 0-999; called across a second, never the old second with the new ms; `FK_SYNC`; `revision=2` refused | a torn or wrong time to the ms | MSSL read before SEC (no latch) |
| O9 | `hook_costs_by_the_bridge` (`test_oodos_clock_debug`): the tick hook measured within 4 cycles of OODOS 0.15's 479 less the tick's saving, timer 1 untouched | a debug build that disturbs the program's timer 1 | TBSUB read without MS0's latch |

The existing hook, stick, paddle, delay and "loses no tick" cases run on revision 3 unchanged.

**The diagnostics** (`test_bios_diag`): NB10 PASS on revision 3, SKIP on revision 2, FAIL with a bridge fault option
(MSS not restarted; MSF not cleared by a 1). Mutant: NB10's MSS compare removed, so the fault passes, and the case
must fail.

**Expectation changes (need approval):**

- the tick's cost pins (`test_oodos_input`) and the books' figures (measured in T3); the Disk II cases'
  tolerance (`test_oodos_clock_disk2`), made exact; `FT_INFO`'s interface 17 and its clocks byte; the tests'
  `ms_count()` and `sym("MS")` reads taking the bridge's counter;
- `test_rtc`'s `tick_with_tie_interrupts_as_irqh_bit_5` and `a_program_reads_the_time_and_takes_the_tick_interrupt`
  pinned to `revision=2` (the bridge's behaviour there is unchanged);
- **retired, with the VIA tick:** `test_oodos_clock`'s `a_stopped_timer_ends_a_delay_and_the_warm_boot_starts_it_again`
  and `a_changed_acr_is_put_back_by_the_warm_boot_losing_no_tick` (the second replaced by O5's ACR check), and
  `without_the_via_the_6551_s_interrupt_is_served_and_its_key_kept`'s expectation of no ms counter (it keeps its
  6551 checks; `FC_MS` is now set);
- **OODOS's revision 1 cases, retired** (OODOS 0.16 refuses `$D1`; O7 replaces them): `test_oodos_sysinfo`'s
  `on_a_revision_1_north_bridge_its_row_says_d1_and_revision_1`, `test_gwbasic_screen`'s
  `a_revision_1_bridge_has_peek_and_poke_but_not_bload`, and the `revision=1` cases of `test_oodos_video`,
  `test_oodos_gfx` and `test_oodos_sys` (each helper that inserts ` revision=1`);
- **unchanged:** the ROM's and the emulator's own revision 1 and 2 cases (`test_bios`, `test_bios_diag`,
  `test_video_rev2`, `test_session`, `test_machine`), since the BIOS keeps all three revisions;
- the random source's O5 (`an_old_bridge_has_no_source`, `revision=2`) becomes O7's refusal.

Anything else that changes output is a finding to bring back.

## Docs and records ##

- **hw/design.md:** "The north bridge, revision 3" beside "Real-time clock" (the random source's sections and this
  one's), the register map ([North bridge registers](../design.md#north-bridge-registers)), RTCCTL's TIE,
  [the VIA's timers](../design.md#the-via-and-the-joystick-ports) (timer 1 the program's on revision 3),
  ["Interrupts"](../design.md#interrupts).
- **The Technical Reference:** [chapter 11](../../docs/oo1-technical-reference/11-north-bridge-and-rtc.md) ("The
  timebase", the registers `$C69C-$C69D` and `$C6A8-$C6AE`, RTCCTL's and IRQH's entries, NB_ID `$D3`);
  [chapter 4](../../docs/oo1-technical-reference/04-interrupts.md#under-oodos) (the enables' table, "Under OODOS"
  for revision 3, Break); chapter 5 (the 2 KB pieces' reason); chapter 9 (timer 1 under OODOS); chapter 15 (NB10);
  `docs/index/registers.json`.
- **The Programmer's Guide:** [chapter 8, "The clocks"](../../docs/oodos-programmers-guide/08-game-services.md#the-clocks)
  (the counter on the bridge, the costs, the Disk II note, `FC_T1`, `FT_CLOCK`) and [its hooks' rule](../../docs/oodos-programmers-guide/08-game-services.md#hooks)
  on timer 1; function 80's entry in chapter 7 (`FT_CLOCK`, `FC_T1`); `docs/index/functions.json` if it lists the
  operations.
- **[hw/oodos.md](../oodos.md), [hw/oolib.md](../oolib.md)** (question 7's answer: the tick is the bridge's on
  revision 3, timer 1 the program's), [AGENTS.md](../../AGENTS.md) ([above](#agentsmd)),
  [`backplane.inc`](../../backplane/bios/backplane.inc), `oodos.inc` (`FC_T1`, `FT_CLOCK`, `FK_*`, `FT_OPS`), the
  comments in [irq.s](../../oodos/sys/irq.s), [disk2.s](../../oodos/sys/disk2.s) and [bios.s](../../oodos/sys/bios.s),
  the cards, the site's release note.

## Versions ##

As the random source's spec has them, unchanged: **north bridge revision 3, `$D3`; ROM BIOS 0.20; OODOS 0.16; OOLIB
interface 17.** The two parts ship as one revision and one release: interface 17 is function 78 and this document's
`FC_T1` and `FT_CLOCK` together. If the owner splits them, the second to land takes interface 18.

## The units ##

T1 is this document; it and the random source's R1 are the revision's spec. The rest go beside the random source's
units, the emulator's work in one change with its bridge:

1. **T1, the spec:** this document, approved; its hardware sections into hw/design.md.
2. **T2, the emulator:** the bridge's timebase (with R3's revision 3 bridge), the skew and `set_ms()` hooks, the
   debugger and WebUI lists; `test_nb_timebase` B1-B9; `test_rtc`'s TIE case pinned to revision 2.
3. **T3, OODOS's tick on the bridge:** the cold boot's refusal of `$D1` and `$D2`; CLKINIT, DEAFEN, CLKOFF, the
   dispatcher, TICK, the counter's readers; the VIA tick, `MS`, CATCHUP, `T1NOW` and `FT_DELAY`'s stopped-counter
   check removed; the sweep of OODOS's revision 1 paths; the debug build's costs; O1-O7 and O9; the retired cases
   above; the cost measured and pinned.
4. **T4, interface 17's timing:** `FC_T1`, `FT_CLOCK`; O8; AGENTS.md's rule; function 80's entry.
5. **T5, the ROM BIOS 0.20:** Break's TBCTL, NB10 and its `test_bios_diag` cases (with R2's `$D3` sweep and NB9).
6. **T6, the books and records:** the list above, with the measured figures; `make -C docs check`.
7. **T7, the release:** OODOS 0.16 with the random source; the cards rebuilt; the site.

## Risks ##

- **The saving is small** (about 23 cycles a tick, estimated); the case for revision 3 rests on exact, lossless time
  and on freeing the serial card and timer 1. If the measured saving is less, nothing else changes.
- **The latch's discipline.** One MS0 latch shared by every reader means a multi-byte read outside a handler must
  mask interrupts; a reader that forgets tears its read only when a handler's read lands between its bytes. O2
  sweeps for it; the books say it.
- **OODOS 0.16 won't boot on a revision 1 or 2 machine.** No such board is built, and the site's emulator and
  `backplane.machine` run revision 3; a machine file someone wrote with `revision=2` gets the refusal's line, not a
  half-working system. Cards built for 0.16 boot only on revision 3.
- **The bridge's logic grows again,** on top of the random source's engine: the FPGA looks likelier still.

## Open questions ##

All three decided by the owner, 2026-11-21. None is left open.

1. **Keep OODOS's VIA tick for bridges before revision 3?** **Decided: "require revision 3".** OODOS 0.16 has the
   bridge's tick alone; the VIA timer 1 tick is retired, with no fallback for `$D1` or `$D2`. On an older bridge
   OODOS refuses to boot, with a line naming the bridge ([Revision 3 required](#revision-3-required)).
   *Not taken:* keeping the VIA tick as a fallback; booting with no millisecond counter.
2. **The 1 Hz TICK interrupt: drop it, or keep it for the alarm?** **Decided: dropped** ("recommendations as
   listed"). On revision 3 TIE reads 0 and ignores writes; TICK and ALARM stay as flags to poll (the ROM's launcher
   polls TICK). *Not taken:* TIE as "ALARM interrupts", which would put a second source on ID D for the tick's fast
   path to tell apart.
3. **Timer 1 for programs: polled only, or with an interrupt?** **Decided: polled only** ("recommendations as
   listed"). IER stays OODOS's; a program polls timer 1's flag, and timed calls go through the tick hook.
   *Not taken:* an OOLIB timer-1 hook, which would put a VIA check back in the dispatcher's path.

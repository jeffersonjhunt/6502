# The random number source: an ATECC608B on the north bridge #

The design record for the OO-1's hardware random number source: a Microchip ATECC608B on the CPU card, on the
north bridge's I²C bus beside the DS3231, driven by the bridge's own engine, as the bridge drives the clock
([hw/design.md, "Real-time clock"](../design.md#real-time-clock)). It is unit R1 of the random source plan: the
spec, with the chip's facts checked against Microchip's documents ([the appendix](#appendix-the-chips-facts-checked)).

**Status: draft, 2026-11-21, for the owner to read before R2.** Nothing is built. **Decided** means the owner's
answers of 2026-11-21; everything else is **Proposed**. Hardware is treated as the video card's spec treats it: this
says what the bridge and the chip do, and assumes the board can be built to do it. When it is approved, the
hardware sections go into hw/design.md beside "Real-time clock", in our own words as here.

## Summary ##

| Area | What | Status |
|---|---|---|
| The source | one: an ATECC608B. No LFSR, no software generator in OODOS or OOLIB | **Decided** |
| Where | on every CPU card, beside the DS3231, on the north bridge's I²C bus | **Decided** |
| The part | a factory-locked Trust Platform variant. **ATECC608B-TNGTLSS** (Trust&GO TLS), 7-bit address `$35` | **Decided** (the owner, 2026-11-21: [Q1](#open-questions)) |
| Not the 608A | the 608A can disturb a shared bus at low clock rates, and its timing differs; the bill of materials says 608B | Proposed |
| Who drives it | the north bridge, revision 3 (`$D3`): wake, the fixed Random command, polling, the read, the CRC check, sleep | **Decided** (the bridge); the sequence Proposed |
| Software's view | a 32-byte pool behind three registers, RNGST, RNGCNT and RNGDATA at `$19-$1B` | Proposed |
| The lock | the bridge reads the configuration zone's lock byte at power-on and never draws from an unlocked chip | Proposed |
| Writes to the chip | none, ever: the bridge sends only Random, one Read and the wake/sleep sequences | **Decided** |
| OODOS | function 78 `O_RANDOM`: `RN_INFO` and `RN_FILL`; OOLIB interface 17 | **Decided** (the two operations) |
| SYSINFO | reports the chip through `RN_INFO`; its hardware exception doesn't grow | **Decided** |
| AGENTS.md | the random number registers join what OODOS owns | **Decided** |
| Throughput | about 26 ms a draw of 32 bytes at the planning figure, so about 1,200 bytes a second | Proposed |
| Versions | north bridge `$D3`; ROM BIOS 0.20; OODOS 0.16, OOLIB interface 17 | Proposed |

## Why ##

Games need a seed that differs every run and a few numbers a frame; programs now and then need a few unpredictable
bytes (a shuffle, a temporary file's name). The owner's direction (2026-11-21): the random number comes from
hardware through the north bridge, as the time does, and the ATECC608 is the one source ("Lets just plan on using
the ATECC608 and skip the LFSR idea"). Nothing cryptographic uses it today, but its bytes would serve one: they come
from a NIST SP 800-90 A/B/C generator, which the summary data sheet says the part is built to meet.

**What it can't do:** at about 1,200 bytes a second it serves seeds and a few numbers a frame. A game that wants
hundreds of numbers a frame, or a sequence it can replay from a seed (a level code, a demo's recording), seeds its
own generator from function 78 in its own code. OOLIB offers none (**Decided**, 2026-11-21).

GW-BASIC is unaffected: RND stays the manual's generator and `RANDOMIZE TIMER` its reseed.

## The chip and its fitting ##

**The part.** An ATECC608B in the 8-lead SOIC, a factory-provisioned variant, so it arrives with its configuration
zone locked (**Decided**). Its pins: 4 ground, 5 SDA, 6 SCL, 8 VCC; 1-3 and 7 not connected. Its supply range is
2.0-5.5 V, so it runs from the CPU card's logic supply. A 0.1 µF ceramic capacitor at pin 8 is the board's usual
practice (the documents read don't give a value).

**Which variant (Proposed, [Q1](#open-questions)).** Microchip sells three provisioned flows; each fixes the address:

| Variant | Ordering (SOIC) | 7-bit address | What it takes to buy |
|---|---|---|---|
| **Trust&GO TLS** (recommended) | ATECC608B-TNGTLSS-C (100 a reel), -B (10, bulk), -G (2,000 a reel) | `$35` | nothing: a stock part |
| TrustFLEX TLS | ATECC608B-TFLXTLSS-PROTO (10, bulk); production by a Secure Exchange Package | `$36` | the prototype parts are stock; production goes through Microchip's provisioning flow |
| TrustCUSTOM | ATECC608B-TCSMS | as configured (blank parts default to `$60`) | a full configuration through Microchip's tools |

All three come locked, all are I²C parts, and none collides with the DS3231 at `$68`. Trust&GO is recommended
because it is the only one ordered like any other part. Its I/O levels are fixed to a reference, so the bus side may run at a lower voltage
than the chip's supply. Power-on self test is off in both the Trust&GO and
TrustFLEX configurations, which keeps the chip's power-up time short. The bridge's chip address is a constant of its
build, set to the fitted variant's.

**The address can't move under us.** The provisioned parts allow their I²C address to be changed once, by a command
(UpdateExtra) that the bridge never sends. Nothing in the OO-1 can send the chip any command but the bridge's fixed
ones, so the address is the factory's for the board's life.

**The bus.** One I²C bus, the bridge's, at 400 kHz as now; shared pull-ups; software never drives it. Two rules
follow from the part:

- **The bus runs at 400 kHz except for the wake byte.** The 608B's sheets warn that traffic below roughly 300 kHz
  can wake the chip by accident. At 400 kHz the DS3231's traffic can't; the one slow byte is the bridge's own wake.
- **A 608B, not a 608A.** Microchip lists a fix in the 608B for bus contention when the 608A shares a slow bus with
  other devices; the board's bill of materials names the 608B.

## The north bridge, revision 3 ##

### What the bridge does by itself ###

The bridge keeps a **pool of 32 random bytes** filled from the chip, so the CPU only ever reads registers. All the
chip's traffic is the bridge's; RTCCTL's BUSY stays the clock's own.

**A draw** (one try):

1. **Wake.** The bridge sends one address byte of `$00` (the general-call address, a write) at 100 kHz, then STOP.
   That holds SDA low for 80 µs, past the chip's 60 µs minimum. The chip doesn't acknowledge it, and the bridge
   doesn't expect it to. The bridge then leaves the bus idle for the wake delay, 1.5 ms.
2. **Wake check.** A 4-byte read: the chip answers a fresh wake with a status group of `$11`. Any other answer, or
   a NAK of its address, fails the try (a status of `$07` means the chip's self test failed).
3. **The command.** A write to the chip's address with word address `$03`, then the group: count `$07`, opcode
   `$1B` (Random), mode `$00`, param2 `$0000`, CRC `$24 $CD`. The command never changes, so its CRC is a constant
   of the bridge; the bridge needs a CRC checker, not a generator for commands.
4. **Poll.** While the chip works it NAKs its address on a read. The bridge tries a read 1 ms after the command's
   STOP, then every 1 ms, and gives up after 100 ms. Each NAKed poll is 11 bit times and leaves the bus free
   between polls.
5. **The response.** The first acknowledged read is the answer. The bridge reads the count byte, then the rest of
   the group:
   - count 35: 32 bytes and a CRC. The CRC must match; the 32 bytes go into the pool.
   - count 4: a status group (an error: `$FF` CRC or I/O error, `$08` a health test error, `$0F` can't execute,
     `$07` self test, `$EE` the watchdog). The try fails.
   - any other count, a bad CRC or a NAK in the middle: the try fails.
6. **Sleep.** A write with word address `$01`. The chip drops into its low-power sleep and resets its I/O state, so
   every draw starts from the same state.

**Retries and FAULT.** A failed try is repeated, up to three tries in all. Before a retry the bridge sends the I²C
software-reset sequence that the part's sheet gives for lost synchronisation (START, nine SCL pulses with SDA
released, START, STOP), then starts again from the wake. After the third failure FAULT is set and the pool stays
empty. A health test error clears itself on a later command, the sheets say, so the retries cover a passing one.

**Refill.** When software takes the pool's last byte, the bridge starts a draw on its own. After a FAULT it doesn't
draw again on its own, so a broken chip can't keep the bus busy. Software asks for a fresh draw with DRAW.

**Sharing the bus with the clock.** Transactions stay one at a time. A draw is a run of short transactions (the
wake byte, the wake check, the command, each poll, the response, sleep) with the bus free between them, and the
longest is the response, 815 µs. The second's read of the DS3231 goes in the next gap, so it's delayed by at most
one transaction and no square-wave edge is missed. RNGST's BUSY covers a draw from start to end; RTCCTL's BUSY
shows only the clock's transactions, so neither driver's wait grows by the other's work.

**No interrupt.** The bridge doesn't interrupt for the chip; OODOS polls, as it does for SET.

### Power-on ###

The bridge holds /RESET until its own set-up is done, as revision 2 does for the clock: first the DS3231's (as
now), then the chip's:

1. Wake and wake check (steps 1-2 above). No answer: CHIP clear, and the bridge never addresses the chip again
   until the next power-on.
2. **The lock read:** a Read (opcode `$02`, mode `$00`, 4 bytes from the configuration zone) of word address `$0015`
   (block 2, word 5: bytes 84-87). The group: `$07 $02 $00 $15 $00`, CRC `$17 $5D`. The answer is a 7-byte group:
   count, the four bytes, CRC.
3. **Byte 87 is LockConfig.** `$55` means the configuration zone is unlocked; anything else, locked (Microchip's
   library tests it that way; locked parts read `$00`). Locked: LOCKED set. Unlocked: LOCKED clear, sleep, and no
   draw is ever made from that chip.
4. Locked: the first draw, on the same wake, then sleep.

The whole set-up takes about 31 ms at the planning figures, well inside the chip's watchdog (at least 0.7 s from
the wake). The chip itself is ready 100 µs after its supply comes up (power-on self test is off on the recommended
parts). The first instruction finds RNGST valid and, on a good board, 32 bytes waiting with BUSY clear.

The bridge, not the DS3231's RST, sets the order. The DS3231's data sheet says its RST doesn't affect its I²C
interface, and that RST's 250 ms hold is skipped when its oscillator isn't running. So the bus isn't free because
the clock is in reset; it is ordered because the bridge does one thing at a time
([Q2](#open-questions): the wording in "Real-time clock" needs the same correction).

A hard reset that isn't a power-on doesn't set the chip up again and keeps the pool, as it keeps the clock's state.

### The registers ###

Offsets from `$C680`, after RTCDATA (`$18`); `$19-$1F` are free today.

| Offset | Name | Access | Purpose | Reset |
|---|---|---|---|---|
| `$19` | RNGST | read/write | **Read:** bit 7 READY: the pool holds a byte. Bit 6 CHIP: a chip answered at power-on. Bit 5 LOCKED: its configuration zone is locked, so the bridge draws from it. Bit 4 FAULT: the last draw failed three tries. Bit 0 BUSY: a draw is under way. Bits 3-1 read 0. **Write:** bit 0 DRAW, a strobe. Writing 1 clears FAULT and, unless a draw is under way, empties the pool and draws 32 fresh bytes; writing 0 does nothing. Only DRAW is writable, so a read-modify-write is harmless | READY, CHIP and LOCKED as the power-on set-up found them; FAULT and BUSY 0 |
| `$1A` | RNGCNT | read | the bytes in the pool, 0-32 | 32 on a good board, else 0 |
| `$1B` | RNGDATA | read | the pool's next byte; **reading it removes the byte.** With the pool empty it reads `$00` and removes nothing (check READY first). Taking the last byte starts a refill | |
| `$7F` | ID | read | **`$D3`**: revision 2's registers plus these. STATUS's bits 4-7 read 3 | |

- **Telling the bridges apart:** software uses `$19-$1B` only after reading NB_ID as `$D3`. On `$D1` and `$D2` those
  offsets read `$FF`, which would look like READY and CHIP set and `$FF` bytes, so the ID check isn't optional.
- **RNGDATA has a read side effect.** A debugger's look (`peek()`) and the WebUI's memory view must not take bytes;
  only CPU reads do. Read it with a load (LDA, LDX or LDY), never a read-modify-write instruction or a 16-bit read,
  each of which can read the address twice. The register's entry says so, as RTCCTL's does.

### The lock check, and why there are three ###

An unlocked ATECC608 answers Random with a fixed pattern that never changes (Microchip's library says so in its
tests), and locking is a one-time, irreversible write. A board fitted with a blank part would hand every program the
same "random" bytes. Three things stop that:

1. **The bill of materials says a factory-locked variant** (**Decided**). Locking is never the OO-1's job: the
   bridge has no path to write the chip's configuration, so nothing on the machine can make an irreversible change
   to the board.
2. **The bridge checks LockConfig at power-on** and won't draw from an unlocked chip. A wrongly built board fails
   visibly (SYSINFO, the diagnostics, `RN_INFO`) instead of quietly.
3. **The diagnostics look at the bytes** (NB9): a draw whose 32 bytes are one value, or a 4-byte group repeated,
   fails. The pattern a third-party driver reports for an unlocked part (`$FF $FF $00 $00` repeated) is such a
   group.

Random needs only the configuration zone locked; the data zone's lock doesn't matter to it. The recommended parts
have both locked.

### Why an engine, not a raw I²C port ###

The owner's direction, and it suits the OO-1: software never drives I²C or holds the bus; a crashed program can't
stop the clock; OODOS's driver is a few polls, not a packet layer; and nothing can write the chip's configuration.
The cost is in the bridge's logic: a sequencer with a 100 kHz byte and 400 kHz bytes, a 32-byte pool, a CRC-16
checker and a few constant command bytes. That is easy in an FPGA and tight in two CPLDs; the bridge's part is still
open (hw/design.md), and this is one more reason to lean towards the FPGA.

### Timing ###

At 400 kHz a bit is 2.5 µs (17.9 cycles at 7.16 MHz); at 100 kHz, 10 µs. Bits count 9 a byte and one for each
START and STOP, as the clock's transactions are counted now.

| Step | Bits | Time |
|---|---|---|
| wake: the `$00` byte at 100 kHz, then the wake delay | 11 | 110 µs + 1.5 ms |
| wake check: START, address, 4 bytes, STOP | 47 | 118 µs |
| Random: START, address, word address, 7 bytes, STOP | 83 | 208 µs |
| execution, polled every 1 ms (28 µs a poll) | — | **23 ms planning figure**; 100 ms give-up |
| response: START, address, 35 bytes, STOP | 326 | 815 µs |
| sleep: START, address, word address, STOP | 20 | 50 µs |
| **a draw** | | **≈ 25.8 ms for 32 bytes** at the planning figure |
| power-on lock read: the 7-byte Read on the first draw's wake | 83 + 74 | ≈ 5.5 ms (Read polled; 5 ms planning figure) |

**Microchip publishes no execution time for the 608B's Random; the part is meant to be polled.** Its library warns
that the 608B supports only polling, and the 608B's summary says fixed waits must be re-checked against it. The
23 ms (Random) and 5 ms (Read) figures are the library's fixed-wait figures for the 608 family, used here only to
plan throughput and to choose the give-up. The bridge waits for the chip's answer, however long it takes up to
100 ms; a draw takes as long as the chip does.

The figures are the bridge's build constants; the emulator keeps them in one table.

## OODOS ##

### The BIOS's driver ###

In `oodos/sys/bios.s` beside `TIME`, driving the registers as TIME drives SET: a register write, then a bounded
poll. It is **not** a BIOS jump-table entry; function 78 is the interface.

- `RNGOK`: NB_ID is `$D3`, and RNGST has CHIP and LOCKED both set.
- `RNGFILL` (n bytes to a buffer): for each byte, wait for READY or FAULT, then load RNGDATA. The wait gives up
  after 400 ms, longer than the bridge's three tries at their give-up (about 310 ms), so the driver sees FAULT
  rather than racing the retries. If FAULT is seen, write DRAW once and wait again; if that fails too, the call
  fails.
- Interrupts stay as they are: the tick runs through the wait.

The ROM BIOS has no driver: nothing in the ROM needs random numbers. It changes only for the diagnostics.

### Function 78 `O_RANDOM` ###

**Decided:** two operations. 78 is free (72-77 are OODOS's own; 80-83 and 85 are OOLIB's areas; 84 is
OA_VIDEO's unassigned slot). It is OODOS's own (no OA bit), found by the interface version.

| Op | Form | Result |
|---|---|---|
| `RN_INFO` (0) | Y = 78, A = 0, X = 0 | A = `RS_CHIP` `$01` (a locked chip on a revision 3 bridge: a source), `RS_UNLOCKED` `$02` (a chip that isn't locked: no source), `RS_FAULT` `$04` (its last draw failed); 0 = no chip, or a bridge before revision 3. X = 0 |
| `RN_FILL` (1) | A/X = a block: +0 1, +1 n (1-255); the bytes land at +2 | A = 0: n bytes from the chip. A = X = `$FF`: refused (n 0, no source, or the chip failed twice; the bytes at +2 are then undefined) |

- **X = 0 means A is the operation**, as in OODOS's other functions. A block is never in the zero page or the
  stack, so its address's high byte is never 0.
- **A 16-bit number is an `RN_FILL` of 2**, so no result can be mistaken for a refusal.
- **Cost:** the BDOS's dispatch (about 600 cycles) plus about 20 cycles a byte while the pool lasts. An `RN_FILL` of
  255 waits for 7 refills, about 180 ms at the planning figure.
- **OOLIB's interface goes to 17** for it (OODOS's additions are marked by the interface number, as from
  interface 9). No existing call changes (the owner's rule: interfaces only grow). An older OODOS answers 78 as a
  function it doesn't have, so a program checks FT_INFO's interface version first.

### SYSINFO ###

A new line from `RN_INFO`, not from the hardware (**Decided**):

    RANDOM   ATECC608B on the north bridge          (or: none / not locked / failed)

AGENTS.md's SYSINFO list doesn't grow; the random number registers join what OODOS owns (**Decided**). `hwname.s`
learns `$D3`.

## The ROM BIOS 0.20 ##

- **The revision.** The diagnostics' stage 7 check (`diag22.s`) takes `$D3`, and `hwname.s` names it. Every test of
  NB_ID for exactly `$D2` (WINnH, MAP, AP8, in the BIOS and in OODOS's `crt.s` and `common.s`) becomes "revision 2
  or more": a sweep with its tests, in R2.
- **NB9, the random number chip** (a new check after NB4's clock chip): no `$D3`, SKIP; no CHIP, SKIP ("NO RANDOM
  NUMBER CHIP"); CHIP without LOCKED, FAIL ("NOT LOCKED"); otherwise read 32 bytes and FAIL if they are all one
  value or one 4-byte group repeated; then DRAW, and BUSY must clear and READY return within 150 ms; FAIL on FAULT.

## The emulator ##

- **The bridge, revision 3** (a machine file's `revision=1|2|3`, default 3; `REVISION` 3, ID `$D3`): RNGST, RNGCNT
  and RNGDATA, with `peek()` taking nothing; the pool, the refill, DRAW, three tries and FAULT; a draw's steps
  queued as transactions beside the clock's and timed by their bits, the wake byte at 100 kHz; BUSY kept per owner.
  At power-on the set-up and the first draw happen at once, as the DS3231's do, so the first instruction sees 32
  bytes and BUSY clear. The debugger gets a description beside `describe_clock()`, and the WebUI's
  `debugger/devices.ts` lists the three registers.
- **The chip** (`components/devices/atecc608.{h,cpp}`, written from the part's documented behaviour in our own
  words): asleep or awake; woken by SDA low for 60 µs or more, answering `$04 $11 $33 $43` to the first read after;
  NAKing its address while asleep or busy; the watchdog (asleep again 1.3 s after the wake); the word addresses
  (`$00` reset, `$01` sleep, `$02` idle, `$03` command); the group parsed byte by byte and its CRC checked (a bad
  one answers status `$FF`); Random and the 4-byte configuration Read; the lock state (unlocked: Random answers
  `$FF $FF $00 $00` repeated, the third-party report, since Microchip's documents say only "fixed"). The execution
  time of each command is a constant of the model (Random 23 ms, Read 5 ms), so tests are repeatable; the bridge
  polls it as it would the part.
- **Its source of bytes is injected:** `std::random_device` in the native app; the browser's
  `crypto.getRandomValues` in the WebUI (through the WASM binding, or Emscripten's `random_device` if that is
  backed by it, to be confirmed in R3); **a fixed stream in tests and CcpRig** (a seeded splitmix64), so every byte
  a test sees is known.
- **Machine options:** `rng_chip=locked|unlocked|none` (default locked), and a test-only fault injector (corrupt
  the next k responses, NAK the next k addresses, or answer status `$08` k times).
- **A CRC oracle:** `tools/atecc_crc.py`, our own, about 20 lines, independent of the C++ routine the bridge's
  checker and the chip model share. Its check values, computed by our routine and matched against the routine in
  Microchip's library: the wake answer `$04 $11` gives `$33 $43` (the value Microchip's library expects of the part);
  the Random command gives `$24 $CD`; the lock Read gives `$17 $5D`; the ASCII string `123456789` gives `$BCDD`.
  (Microchip's library also holds a self-test-failure answer, `$04 $07 $C4 $40`, whose CRC doesn't match its own
  routine, which gives `$40 $C2`; it isn't used as a check value.)
- **Revisions 1 and 2** keep `$19-$1B` reading `$FF`; existing tests that pin `revision=1` are unchanged.

## Tests ##

Each must-fail case is seen failing under its mutant, on its own case; each mutant is a defect plausible in real
wiring, bridge logic or hand-written 6502 code.

**The bridge and the chip** (`test_nb_random.cpp`, new; `test_rtc` for C5):

| # | Case | The real failure it guards | Must fail under |
|---|---|---|---|
| C1 | `pool_full_at_power_on`: RNGCNT 32, READY, CHIP, LOCKED, BUSY clear at the first instruction; the bytes are the stream's first 32 | a board whose first program finds no bytes, or the wrong ones | the power-on draw skipped; the pool read from the wrong end |
| C2 | `an_unlocked_chip_is_never_drawn`: `rng_chip=unlocked`: CHIP set, LOCKED clear, RNGCNT 0, no Random on the bus | a mis-built board handing out the fixed pattern | the lock check removed; byte 86 tested instead of 87 |
| C3 | `crc_and_count_are_checked`: one corrupted response retried, the pool right; three, FAULT and an empty pool; a status group not taken as data. The bridge's CRC equals `atecc_crc.py`'s check values | corrupted bytes, or an error group read as 35 bytes | the CRC check removed; the count check removed; the bit order reversed |
| C4 | `no_chip_no_hang`: `rng_chip=none`: CHIP clear, BUSY never stuck, the clock's second unaffected | a board without the part, or a dead one, hanging the bus | a poll without its give-up |
| C5 | `the_clock_keeps_its_second_through_draws`: draws for 5 s straight: TICK each second, SEC right, RTCCTL's BUSY never set by a draw | a draw holding the bus across SQW's edge | one shared BUSY; a draw sent as one long transaction |
| C6 | `refill_after_the_last_byte`: 32 read, BUSY, 32 more that continue the stream; DRAW after FAULT clears it and refills | a pool that empties once and stays empty | the refill trigger removed; DRAW not clearing FAULT |
| C7 | `the_chip_is_woken_for_every_draw`: a second draw 2 s after the first | the second and later draws failing because the chip slept | the wake skipped after the first draw |
| C8 | `a_peek_takes_nothing`: the debugger reads RNGDATA ten times, RNGCNT unchanged | the WebUI's memory view draining the pool | `peek()` calling `read()` |
| C9 | `the_chip_is_polled`: the model's Random time set to 40 ms: the draw still succeeds, and the bridge's first read comes 1 ms after the command | a fixed wait that reads too early on a slower part | a fixed 23 ms wait in place of the poll |

**OODOS** (`test_oodos_random.cpp`, new, on CcpRig with the fixed stream):

| # | Case | The real failure it guards | Must fail under |
|---|---|---|---|
| O1 | `rn_fill_gives_the_chip_s_bytes`: n 1, 2, 32, 33, 255, exactly the stream's next n, nothing past +2+n | a write past the caller's block; bytes skipped across a refill | the count compare off by one; a refill byte read twice |
| O2 | `rn_fill_waits_for_ready`: n 64 straight after n 32, never `$00`s | an empty pool's `$00`s handed out | the READY wait removed |
| O3 | `rn_fill_recovers_from_a_fault`: one draw's three tries failed, DRAW written, bytes right; two draws failed, refused, no hang | a game hanging on a flaky chip, or given garbage | DRAW not written; the give-up removed |
| O4 | `rn_info_says_what_there_is`: locked; unlocked `RS_UNLOCKED`, RN_FILL refused; none 0, refused | a program unable to tell why it has no numbers | RS_UNLOCKED's test inverted |
| O5 | `an_old_bridge_has_no_source`: `revision=2`, RN_INFO 0, RN_FILL refused at once | `$FF` from unused offsets taken as READY, CHIP and data | the NB_ID check removed |
| O6 | `the_interface_is_17`: FT_INFO; every existing function's cases unchanged | a program unable to tell whether 78 exists | (the pin) |
| O7 | SYSINFO's goldens: locked, unlocked, none, revision 2 | a wrong RANDOM line | the line's RS bit tested wrongly |

**The diagnostics** (`test_bios_diag`): NB9 PASS on a good machine; FAIL with `rng_chip=unlocked` and with the
injector making the chip answer a constant; SKIP with no chip. Mutant: NB9's byte compare removed, so the constant
chip passes, and the case must fail.

**Expectation changes (need approval):** NB_ID `$D2` to `$D3` in goldens; the version pins; SYSINFO's new line.
Anything else that changes output is a finding to bring back.

## Versions and records ##

- **North bridge revision 3, `$D3`.** ROM BIOS 0.20 (after the SID noise seed's removal, 0.19). OODOS 0.16, OOLIB
  interface 17 (after `feat/serial-rx`'s 0.15 and interface 16).
- **hw/design.md:** "The north bridge, revision 3" beside "Real-time clock" (the chip, the engine, the registers in
  the map, the board's factory-locked part, the timing, the emulated model), and the CPU card's note in
  [B6](../design.md#b6-as-built): the part, its address, decoupling.
- **The Technical Reference,** [chapter 11](../../docs/oo1-technical-reference/11-north-bridge-and-rtc.md): "The
  random number chip"; register entries `$C699-$C69B`; NB_ID's `$D3`; `docs/index/registers.json`; chapter 15 for
  NB9.
- **The Programmer's Guide:** function 78's entry, `docs/index/functions.json`, FT_INFO's list, an example in
  `docs/examples/` (a die rolled from `RN_FILL`, its output under the fixed stream), and the advice to seed one's own
  generator for many numbers or a replay. **The User's Guide:** SYSINFO's sample.
- **[hw/oodos.md](../oodos.md), [hw/oolib.md](../oolib.md),** the cards, the site's release note;
  [AGENTS.md](../../AGENTS.md)'s list of what OODOS owns; [`backplane.inc`](../../backplane/bios/backplane.inc)
  (`NB_RNGST`, `NB_RNGCNT`, `NB_RNGDATA`, the bits, `NB_ID_V3`); `oodos.inc` (`O_RANDOM`, `RN_*`, `RS_*`).

## Risks ##

- **Execution time is the chip's, not ours.** Polling makes the bridge right whatever it is; only throughput moves.
  The 100 ms give-up is four times the planning figure and a seventh of the watchdog's minimum.
- **EEPROM wear is unverified.** No 608 document read says whether Random writes the chip's EEPROM (its endurance
  is 400,000 writes a byte); they list only mode `$00`. The refill is on demand, so wear could only come from a
  program draining the pool without end. R3 can't settle it; a question to Microchip, or a look at the full data
  sheet under NDA, would.
- **The bridge's logic grows:** a sequencer, the pool and a CRC checker. It may not fit two CPLDs.
- **RNGDATA's read side effect:** a stray reader drains the pool. Only OODOS reads it, and the debugger's `peek()`
  is guarded (C8).
- **No source on a machine without the chip** (an old bridge in the emulator, `rng_chip=none`, a board missing the
  part): a program gets a refusal, not numbers. The books say to check `RN_INFO`.

## Open questions ##

Both decided by the owner, 2026-11-21: "recommendations as listed".

1. **Which factory-locked variant?** **Decided:** ATECC608B-TNGTLSS (Trust&GO TLS, SOIC), 7-bit address `$35`:
   a stock part, locked, I²C, power-on self test off.* *Alternatives:* TrustFLEX TLS (`$36`), whose production parts
   go through Microchip's provisioning flow; TrustCUSTOM, configured through Microchip's tools.
2. **hw/design.md's "Real-time clock" says the DS3231 ignores I²C while its RST is low.** Its data sheet says RST
   doesn't affect the I²C interface, and that the 250 ms hold is skipped when the oscillator isn't running. Nothing
   breaks (the bridge orders its own power-on transactions), but the decided text is wrong. **Decided:** when
   R1's text goes into hw/design.md, say the bridge holds /RESET until its own set-up is done, and drop "ignores
   I²C meanwhile".

## Appendix: the chip's facts, checked ##

Each fact the plan held from memory, against Microchip's documents (read for meaning; nothing copied). "Change"
says whether this spec differs from the plan.

| Fact | The plan's value | Verified value | Source | Change |
|---|---|---|---|---|
| Address, blank / TrustCUSTOM part | `$60` | `$60` (7-bit; the library's `$C0` is the 8-bit form) | [S5](#sources) `atca_cfgs.c`; [S1](#sources) (TrustCUSTOM samples are the blank parts) | no |
| Address, provisioned parts | "other addresses" | Trust&GO TLS `$35`; TrustFLEX TLS `$36`; also, from the library only, Trust&GO LoRa `$59`, TrustFLEX WPC `$38` | [S2](#sources) 2.1; [S3](#sources) 2.1; [S5](#sources) `device_instance.py` | **yes:** `$35`, Trust&GO recommended |
| Ordering codes | — | TNGTLSS-G/-C/-B (SOIC); TFLXTLSS, TFLXTLSS-PROTO; TCSMS | [S1](#sources) table 3-1; [S2](#sources), [S3](#sources) ordering pages | added |
| Address changeable? | — | once, by UpdateExtra (byte 85); the bridge never sends it | [S2](#sources), [S3](#sources) 2.1 | added |
| Wake | SDA low, "tens of µs" | SDA low at least 60 µs (tWLO); the sheets' suggested way is a `$00` byte at 100 kHz; a wake while awake is ignored | [S1](#sources) table 2-2; [S2](#sources) 7.1 | **yes:** the `$00` byte at 100 kHz |
| Wake delay | ≈ 1.5 ms | tWHI at least 1,500 µs (20 ms if self test runs at wake; off on the recommended parts) | [S1](#sources) table 2-2; [S2](#sources) 5.1.7 | no |
| Wake answer | — | status `$11`: group `$04 $11 $33 $43` | [S2](#sources) table 4-3; [S5](#sources) `atca_hal.c` | added (the wake check) |
| Watchdog | ≈ 1.3 s | 0.7 / 1.3 / 1.7 s (min / typ / max) from the wake; 1.3 s on the provisioned parts | [S1](#sources) table 2-2; [S2](#sources), [S3](#sources) 2.1 | no (0.7 s is the bound used) |
| Random opcode, params | — | opcode `$1B`, mode `$00`, param2 `$0000`, no data | [S2](#sources) 5.1.5; [S3](#sources) 5.1.5; [S5](#sources) `calib_command.h` | no |
| Random response | count, 32 bytes, CRC | count 35, 32 bytes, CRC | [S2](#sources) 4.1, table 5-20; [S5](#sources) | no |
| Random execution time | ≈ 23 ms, waited out | **not published for the 608B; poll.** The library's fixed-wait figure: 23 ms | [S5](#sources) `calib_execution.c` (608B "polling only"); [S1](#sources) 4.2 (fixed waits to be re-checked) | **yes:** the bridge polls, 100 ms give-up |
| Busy behaviour | — | NAKs its address on a read while busy, idle or asleep | [S2](#sources) 7.5 | added (polling) |
| Packet layout | word address, count, opcode, params, CRC | word address `$03`, then count (counting itself and the CRC), opcode, param1, param2 (low byte first), data, CRC; group 4-155 bytes | [S2](#sources) 4.1, 4.2, 7.2; [S5](#sources) | no |
| CRC-16 | polynomial, bit order unknown | polynomial `$8005`, register starts at 0, over the count and packet; each byte fed least significant bit first; no final reflection; sent low byte first | [S2](#sources) 4.1; [S5](#sources) `atCRC` | settled; check values in "The emulator" |
| Word addresses | idle | `$00` reset, `$01` sleep, `$02` idle, `$03` command | [S2](#sources) table 7-2; [S5](#sources) `calib_basic.c` | **yes:** sleep after a draw, not idle |
| Status codes | — | `$00` ok, `$03` parse, `$07` self test, `$08` health test, `$0F` execution, `$11` woken, `$EE` watchdog, `$FF` CRC or I/O | [S2](#sources) table 4-3 | added |
| Lost sync | — | the I²C software-reset sequence, then a wake | [S2](#sources) 7.2.2 | **yes:** before each retry |
| Lock byte | "the lock byte" | LockConfig, configuration byte 87, in word `$0015` (block 2, word 5, bytes 84-87); `$55` unlocked; read by a 4-byte Read (opcode `$02`, mode `$00`) | [S2](#sources) 4.4.1, 5.1.6; [S5](#sources) `calib_helpers.c`, `calib_command.h` | settled |
| Unlocked Random | a fixed test pattern | a fixed pattern that never changes (Microchip); its bytes, `$FF $FF $00 $00` repeated, only from a third-party driver | [S5](#sources) `test_atcacert_host_hw.c`; [S7](#sources) | no; the bytes **unverified** by Microchip |
| Which lock Random needs | config only, or data too? | the configuration zone only | [S5](#sources) `test_atcacert_host_hw.c` (gates on the config lock alone) | settled |
| Power-up | ≈ 1 ms, inside 250 ms | tPU at least 100 µs; longer with power-on self test, which is off on Trust&GO and TrustFLEX | [S1](#sources) table 2-2; [S2](#sources), [S3](#sources) 5.1.7 | no |
| DS3231 during RST | ignores I²C | RST doesn't affect its I²C; tREC 250-300 ms, skipped if the oscillator isn't running | [S6](#sources) electrical table, note 13 | **yes:** the bridge orders power-on ([Q2](#open-questions)) |
| Wake disturbing the DS3231 | believed harmless | the wake is a well-formed transaction to address 0 (general call), which the DS3231's sheet doesn't list; it acknowledges only its own address, so it ignores it | [S6](#sources) I²C section; [S2](#sources) 7.1 | settled by inference, not by a statement |
| Slow traffic waking the chip | — | traffic below ≈ 300 kHz can wake the 608B; the sheets advise sleep or idle after | [S2](#sources) 7.1 | **yes:** 400 kHz except the wake byte |
| 608A on a shared bus | — | the 608B fixes bus contention at low clock rates with several devices | [S1](#sources) 4.1 | **yes:** 608B only |
| Supply | ≈ 2.0-5.5 V | 2.0-5.5 V | [S1](#sources) table 2-5 | no |
| I²C clock | 400 kHz | up to 1 MHz (the DS3231 caps the bus at 400 kHz) | [S1](#sources) table 2-4; [S6](#sources) | no |
| Pinout | — | SOIC: 4 GND, 5 SDA, 6 SCL, 8 VCC; others not connected | [S2](#sources) table 1-1 | added |
| EEPROM wear by Random | — | not stated in any document read | — | **unverified** (a risk) |

### Sources ###

- **S1.** Microchip, *ATECC608B CryptoAuthentication Device Summary Data Sheet*, DS40002239B. Read from a copy at
  <https://cdn.sparkfun.com/assets/a/3/7/1/9/ATECC608B_datasheet.pdf>. The full ATECC608B data sheet is under NDA
  and wasn't read.
- **S2.** Microchip, *ATECC608B-TNGTLS CryptoAuthentication Data Sheet*, DS40002250A, a complete data sheet:
  <https://ww1.microchip.com/downloads/en/DeviceDoc/ATECC608B-TNGTLS-CryptoAuthentication-Data-Sheet-DS40002250A.pdf>;
  its configuration page online:
  <https://onlinedocs.microchip.com/oxy/GUID-59F7F830-64CA-4EE1-A21D-C5745DEC9509-en-US-2/GUID-FED15817-0399-4A58-AE2A-37A7BE3FDADC.html>.
- **S3.** Microchip, *ATECC608B-TFLXTLS CryptoAuthentication Data Sheet*, DS40002249A:
  <https://ww1.microchip.com/downloads/en/DeviceDoc/ATECC608B-TFLXTLS-CryptoAuthentication-Data-Sheet-DS40002249A.pdf>.
- **S4.** Microchip, *ATECC608A-TFLXTLS CryptoAuthentication Data Sheet*, DS40002138A (for comparison):
  <https://ww1.microchip.com/downloads/en/DeviceDoc/ATECC608A-TFLXTLS-CryptoAuthentication-Data-Sheet-DS40002138A.pdf>.
- **S5.** Microchip, *CryptoAuthLib*, <https://github.com/MicrochipTech/cryptoauthlib>, branch main as of
  2026-11-21: `lib/calib/calib_command.h` and `.c`, `calib_basic.c`, `calib_execution.c`, `calib_helpers.c`,
  `lib/atca_cfgs.c`, `lib/hal/atca_hal.c`, `harmony/config/device_instance.py`,
  `test/atcacert/test_atcacert_host_hw.c`.
- **S6.** Maxim Integrated, *DS3231 Extremely Accurate I²C-Integrated RTC/TCXO/Crystal*, 19-5170 rev. 10. Read
  from a copy at <https://courses.physics.illinois.edu/PHYS371/sp2023/code/DS3231.pdf>; Analog Devices' own copy
  (<https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf>) timed out.
- **S7.** Tock OS, the `atecc508a` capsule's source (third-party, for the unlocked pattern's bytes only):
  <https://docs.tockos.org/src/capsules_extra/atecc508a.rs.html>.
- **Not reached:** Microchip's application note AN8843 on generating random secrets (refused, 403); the ATECC608A
  and ATECC508A complete data sheets (under NDA; only summaries are public).

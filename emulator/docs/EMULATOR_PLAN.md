# Emulator Plan — refactor toward the backplane system

Turns the Apple-1 emulator into one that can stand in for the real 6502
backplane computer (`hw/modules.md`, `hw/console.md`) during bring-up, and
brings it in line with the house C++ style used by `pinled`
(`ooe-site/projects/001/firmware`).

The code as it stood before any of this is tagged **`beta`**.

## 1. Where it is today

A jump-table NMOS 6502 core (derived from gianlucag/mos6502), a flat 64 KB
`Memory` that special-cases the Apple-1 PIA keyboard flag, a raw-tty keyboard,
a stdout display, and a main loop that loads `ibasic.rom` at `$8000` and runs
16 steps per tick. It boots Woz Monitor and Integer BASIC "for the most part".

Known problems at `beta`, and what Phase 1 found behind each (every fix has
a test that was seen failing first):

- **The Applesoft ROM "does not work"**: two emulator bugs.
  - `Memory` let the CPU write over ROM. Applesoft's cold start sizes RAM by
    complementing a byte per page until one doesn't change, so it never found
    the top of RAM and looped at `$FCE8`. ROM is now read-only to the CPU
    (`Memory::SetReadOnly`, set up by `LoadApple1`).
  - A store to `DSP` only printed if the stored byte had bit 7 set. On the
    Apple-1 that bit is the display's busy line, and every store prints.
    Integer BASIC lost the last letter of every error message (`EN ERR`), and
    commit 3612a35 had commented out Applesoft's `and #$7F` in `RDKEY` to keep
    its echo visible. That broke CR detection, because keys then arrived as
    `$8D`. Fixed in `Memory`, with the 6821 rule that `$D012` is the DDR while
    CRB bit 2 is 0, and `io.s` restored to the original.
- **Phantom key at reset.** Woz Monitor stores `$A7` to `KBDCR`, and that set
  key-ready. Bits 6-7 of both PIA control registers are now read-only to the
  CPU; peripherals set them through `BusInterface::Poke`.
- **ROM loading.** It copied a full 128-byte buffer after a short read, logged
  the wrong address, ran 64 KB of zeros when the file was missing, and would
  have wrapped an oversized image into zero page. Replaced by
  `ooe::LoadImage`, which refuses instead.
- **ROM path** was hard-coded relative to the working directory. It is now
  `emulator [rom]`, and failing to load is fatal.
- **Ctrl-C** was SIGINT (raw mode keeps ISIG), which killed the process
  before the terminal was restored. SIGINT/SIGTERM now end `Run()`, and
  everything is owned by `main()`, so destructors run with no leaks. `LoadROM`
  and `shouldPause` are gone.
- Still open, for later phases: no cycle counting, NMOS opcodes only, and
  invalid-BCD decimal results (Phase 4).

## 2. House style (from `pinled`)

| Area | Convention |
|---|---|
| Namespace | `namespace ooe::emu { … }`, body indented 4 spaces |
| Naming | `PascalCase` types, `snake_case()` methods, `member_` with `{}` initialisers, `static constexpr` constants |
| Braces | Allman, spaces not tabs |
| Headers | `#pragma once`, Doxygen `@file/@author/@brief/@version/@date/@copyright (MIT)` and a paragraph on *why* |
| Layout | `components/<name>/{include/<name>.h, <name>.cpp, CMakeLists.txt}` |
| Errors | return codes, `new (std::nothrow)`; no exceptions on the hot path |
| Logging | `TAG`-scoped printf-style macros; nothing on the hot path unless compiled in |
| Build | explicit source lists, C++17, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`, ASan/UBSan on by default, `CMakePresets.json`, `.clangd` |
| Tests | `test/host/harness.h` (`TEST`/`CHECK_EQ`), CTest, hardware-free core |
| Docs | `docs/REQUIREMENTS.md` + plan; requirement IDs cited in code |

## 3. Target layout

```
emulator/
  CMakeLists.txt  CMakePresets.json  .clangd  README.md
  docs/           REQUIREMENTS.md  EMULATOR_PLAN.md  MACHINES.md
  components/
    cpu6502/      6502/65C02 core: registers, decode table, cycle counts
    bus/          Device interface + address decoder (ranges / module IDs)
    devices/      ram, rom, pia6821, via6522, acia6551, console
    machine/      machine descriptions: apple1, eater, ooe-backplane
    debug/        debugger, disassembler, ld65 symbols, bus trace (Phase 5)
    session/      a machine run a budget of cycles at a time: the WebUI's core
    emu_log/      TAG-scoped logger (replaces easylogging++)
  apps/emu/       terminal frontend: raw tty, throttle, debugger
  test/host/      harness.h, test_*.cpp, fixtures/
```

Dependency direction, no cycles:

```
apps ─▶ machine ─▶ devices ─▶ bus ─▶ cpu6502
```

`cpu6502` and `bus` include nothing from the terminal or the logger — the same
property that keeps `pinled_schema` testable off-target.

## 4. Phases

### Phase 0 — tests before refactoring

Measure the existing core before changing it.

1. `test/host/harness.h` and a CTest project with the full warning set and
   sanitizers on test code. The core is built without `-Werror` until Phase 2
   brings it under the house flags.
2. Hand-written smoke cases that need no fixtures, so every machine runs
   *something*.
3. Klaus Dormann's `6502_functional_test` against the unchanged core.
4. Tom Harte's SingleStepTests (`65x02/6502/v1`), a pinned subset per legal
   opcode, comparing registers and RAM (cycles recorded, not yet compared).
5. Record what fails (`docs/PHASE0_RESULTS.md`). That list is Phase 1.

### Phase 1 — fix known bugs

Each bug from §1 and each Phase 0 failure gets a failing test first.

### Phase 2 — restyle and restructure

No behaviour change; Phase 0 suites must stay identical. Rename to
`ooe::emu::Cpu6502` (`step/reset/irq/nmi`), move to the component layout, add
Doxygen headers, explicit CMake sources, presets and `.clangd`. Replace
easylogging++ and `fmt` with `emu_log`; bus tracing becomes compile-time so it
costs nothing when off. Bring the core under `-Werror`.

**Done 2026-10-18.** Layout as in §3, except `devices/`, which waits for
Phase 3; the Apple-1 map lives in `components/machine` until then, and the
terminal keyboard/display in `apps/emu`. The opcode table was generated from
the old one. Results are unchanged: Harte's `HARTE_VERBOSE` output at 1,000
cases/opcode is byte-identical to `beta`'s, and Dormann traps at $3469 after
the same 30,646,176 instructions. The core also gained `regs()`/`set_regs()`,
which tests use instead of going through the reset vector, and Phase 5's
debugger will need. Mutations of the restyled core (an unsigned SBC borrow,
`JMP ($xxFF)` without the page wrap) are caught. The app now logs only with
`--log FILE`, and `logging.cfg` is gone.

### Phase 3 — a bus that matches the hardware

- `Device`: `read(addr)`, `write(addr, data)`, `tick(cycles)`, an IRQ line.
- `Bus` owns a decode table; devices stop hard-coding addresses. The PIA's
  clear-on-read moves out of `Memory` into a `Pia6821` device.
- North bridge model: 20-bit address bus and 4-bit module ID (PS0..3), so the
  emulator decodes into module IDs 0x0..0xF exactly as the backplane will.
- Machine descriptions (text/JSON) choose the map, ROMs and devices: one
  binary runs the Apple-1, the Eater board, and the backplane system.

**3a done 2026-10-18.** `Device` and `Decoder` in `components/bus`, with
mappings in 20-bit physical space, later-wins overlap, a module ID per
mapping and an IRQ wire per mapping. A16-A19 come from a pluggable
`NorthBridge`, which for now is the identity (A16-A19 low) until that is
decided (§5). `components/devices` has `Ram`, `Rom` and `Pia6821`, with DDR
select, read-only flags, clear-on-read and a CA1/CB1 strobe. Machines are
plain text (`machines/apple1.machine`, compiled in as the default;
`emulator --machine FILE`). The Apple-1 is now built from that description,
and `apple1::Io` is the one keyboard/screen adapter shared by the app and
the boot tests. `tick()` is deliberately left out until Phase 4 has cycles
to count. The decoder is a reverse linear scan: the release build runs
Applesoft at ~88M instructions/s, about 300x a 1 MHz 6502, so nothing
faster is warranted yet. Harte's known gap (decimal ADC/SBC on invalid BCD)
is now set aside and reported rather than failed (`HARTE_STRICT=1`
restores it), so the suite is green and any new failure stands out.

**3b done 2026-10-19**, except `backplane.machine`, which waited on the
north bridge's A16-A19 rule and the module address map (§5).

**`backplane.machine` step 1 (2026-10-22): the north bridge.** hw/design.md
settled the rule, and its emulator plan's first step is in:

- **Three address spaces.** A `NorthBridge` now returns a `Route`: memory
  (the 1 MB), flash (ROM A, then ROM B) or the 2 KB I/O hole, plus a
  writable flag. Mappings name their space (`space=flash|io`; I/O mappings
  take the module ID of their 128-byte block). VPB reaches the bus
  (`Bus::vector_pull`), and the bus trace records the lines of each cycle as
  they were (`Decoder::last_access`).
- **`BackplaneBridge`** (`components/devices`), from the machine line
  `northbridge backplane [bootsel]`: four 16 KB windows with ROM and RO
  bits, VEC on vector pulls, the I/O hole ahead of the windows, and its
  registers at $C680 with the decided reset values (W3 and VEC on the top
  block of ROM A, or RAM block 63 under /BOOTSEL), STATUS, the /IACK poll
  (IRQL/IRQH), WAIT0-3 and the ID byte ($D1: module D, revision 1). The
  bridge resets with the CPU (`Decoder::reset`, from the app and the
  debugger's `reset`).
- **`machines/backplane.machine`:** RAM A-D, the 512 KB flash, and a 6551
  in the serial card's block ($C300) as a stand-in terminal until the
  TL16C552A is modelled. The flash image is the whole ROM A, BIOS in its
  top 16 KB.
- **test_backplane** checks each register and route, and runs the design's
  boot as 6502 code: the BIOS copies itself from flash into RAM block 63
  through W2, points W3 at the copy and keeps running with no jump.

Not yet: MODE's APPLE bit (logged and ignored), and the DMA controller and
the cards (hw/design.md "Emulator", steps 2-7).

**A minimal BIOS (2026-10-22)** in `backplane/bios` (ca65): it shadows itself
into RAM block 63, switches W3 and VEC to the read-only copy, restores W2
and enters the Woz Monitor (Eater's 6551 version, ported) on the stand-in
terminal; NMI re-enters the monitor. The same image boots under /BOOTSEL
from RAM. The test build assembles it and test_bios boots it.

**RDY wait states (2026-10-22), hw/design.md's emulator step 2.** An access
can be held for N extra cycles: `Bus::wait_states()` reports them after each
read or write and the CPU counts them, so `--clock` slows down and the bus
trace logs the access once, on its last cycle, leaving a gap as the spy
would. The decoder's clock includes them, so timers run through the held
cycles, and the device is accessed once, at the end. The waits come from
the north bridge's WAIT0-3 (2 bits per module ID; WAITn holds IDs 4n to
4n+3, lowest in bits 1-0, now in hw/design.md) plus a mapping's `wait=N`
(0-15) in the machine file. Holding the CPU for an open-ended time (the
console's halt and bus-cycle step, DMA) still needs a core that stops
between cycles; the DMA step decides how to steal cycles. Wait states that
vary per access (an RP2350 card holding RDY until ready) get a device hook
when the first such card is modelled.

**The DMA controller (2026-10-22), step 3.** `DmaController` (module 8,
$C400; machine line `dma 400-47F space=io irq`) has the decided registers
and modes: single, demand, block and memory to memory, DEC, HOLD, AUTO,
BURST, fixed priority, ABORT, terminal count with TC to the card, STATUS
and the interrupt; CONTROL resets to $70 (DMA off). Cycles are stolen
exactly where the board steals them: the decoder offers the bus to its
`BusMaster` before every CPU cycle and every WAI/STP idle cycle, the
controller runs real bus cycles (one to take the bus, one per fly-by byte,
two per memory-to-memory byte), and the CPU's pending access is held for
them through the wait-state path, so the CPU clock, `--clock`, timers and
the trace gap all agree. DMA reaches memory space by physical address.
Cards raise `Device::drq()` and answer `dack_read`/`dack_write` with the TC
flag; a mapping's `drq=0-3` wires one to a channel. No modelled card uses
DMA yet (the SD card will); test_dma uses a test card. Not modelled: an
NMOS 6502 ignoring RDY on writes (the backplane is W65C02S only).

**The Console card (2026-10-23), WebUI step 1.** `ConsoleCard` (module E,
$C700; machine line `console 700-77F space=io irq`) has the decided
registers: SWR, PANEL, KEYS, DISPLAY, CTRL, the BPADDR/BPCTRL breakpoint,
STATUS, the latched cycle (LADDR, LDATA, LSTAT), PADDR, IEN and ISR. It is
a `CycleObserver` (now in `bus/include/bus_cycle.h`, with `CycleTee` so the
trace and the card both watch), so the comparator and the latch see every
cycle's physical address, and the lights accumulate per-line duty when
asked. The panel's switches and keys are methods, not registers. The run
loop asks `may_run()` before each instruction and takes the card's /NMI and
START after it; the app turns a card halt into the debugger's "panel"
stop, and `r` releases the card. Two deviations, both from stepping whole
instructions: a read or write breakpoint latches its exact cycle but stops
after the instruction, and S BUS CYCLE steps an instruction. EXAM and DEP
use `Decoder::peek_physical`/`write_physical`, which count no cycle, since
emulated time stands still while halted. Not modelled: Apple mode (AMODE,
hidden registers) and BE in LSTAT and the lights.

**Session (2026-10-23), WebUI step 2.** `components/session` is the run
loop without a terminal, clock or debugger: `Session::run(budget)` runs the
machine for a budget of cycles (the browser's is one frame, ~119,318 at
7.16 MHz) and stops early at a Console card halt or STP. Serial goes
through a key queue and `take_output()`; the panel is the card's inputs
and its SPI registers; lamps, registers, peeks and a trace ring are read
without bus cycles. `power_cycle()` rebuilds the machine from its files
and keeps the panel's switches. The caller paces it, so it is the same code
on the host and, next, in WebAssembly.

**WebAssembly (2026-10-23), WebUI step 3.** `apps/web` is `Session` behind
embind as an ES module (`oo1.mjs` + `oo1.wasm`, ~770 KB) with
backplane.machine and the BIOS embedded, built by the `web` preset in a
pinned image (`emscripten/emsdk:6.0.12` plus cc65 and Ninja,
`tools/web_build.sh`). The BIOS rules moved to `cmake/roms.cmake` so the
tests and the module share them. Thirteen host suites also build to WASM and
run under node, so both sides of the fork run the same checks; they need a
1 MB stack (`-sSTACK_SIZE`), since tests keep 64 KB buses on the stack and
Emscripten's default stack is 64 KB. Clang's `-Wconversion` includes
sign conversions, which found three spots GCC let through. Speed under node:
~21 MHz, so 7.16 MHz real time is about a third of a core on the main
thread, as the plan hoped.

**WebUI shell (2026-10-23), WebUI step 4.** `apps/web/ui`: TypeScript and
Vite, no framework. The page talks to the machine through `MachinePort`
(docs/MACHINE_PORT.md) only; `WasmPort` runs the module on the main thread,
a budget of clock × frame time per animation frame, capped at 100 ms.
Mockup C's toolbar (Run/Halt/Step through the panel's HALT switch and CONT,
clock, phosphor, CAPS) and monitor: an 80x30 terminal drawn from a glyph
atlas on a 640x480 canvas held at 2x, with Tiles and Apple II showing NO
SIGNAL until the video card exists. Tested by vitest (WasmPort against the
real module in node, with a hand-stepped clock) and Playwright (boot to the
monitor in Chromium, typing, halt and run, phosphor, NO SIGNAL, a missing
core).

**Front panel (2026-10-23), WebUI step 5.** Mockup C's rack unit, wired:
address, data and status lamps from the card's duty (incandescent curve,
flicker while partly lit, frozen and read out when halted), the switch
register, LOAD ADRS/EXAM/DEP/CONT/START, the HALT and S INST toggles and the
keyswitch, all through one PanelState so the toolbar and panel move the same
switches. `Session::irq()` feeds the IRQ lamp. Playwright runs in Chromium
and in WebKit at 2x (a Retina Mac); repeated parallel runs found the canvas
left blank after a resize when the next frame was slow, so `Crt.resize`
redraws at once.

**Debugger (2026-10-23), WebUI step 6.** Mockup C's collapsible pane:
registers and flags, the bus trace (the trace ring, only while its tab is
open), and breakpoints on the Console card's comparator: a saved list, one
armed, labels resolved through the windows to physical addresses, fetch,
read or write, halt or /NMI, and the latched cycle on a hit. Re-armed after
a power cycle, since the card forgets. Memory and Devices tabs are placeholders.

**The video card (2026-10-24), step 4, part 1.** `VideoCard` (module 9,
$C480; machine line `video 480-4FF space=io irq`) has the decided registers
and VGA 640x480 timing: 800 pixel clocks a line at 25.175 MHz, 525 lines,
turned from CPU cycles at the clock it is told, so it is 60 frames a second
at any CPU speed. It draws from banks 8-B, which the board's card snoops;
a copy that sees every write is the RAM, so the machine hands it a pointer
to the RAM there (and refuses a description where banks 8-B are not one
`ram` line). Each line is drawn as the beam finishes it, so mid-frame
changes to memory and the palette show from the next line; base and scroll
registers latch at vertical blank, and `frame()` is the last finished
frame (double buffered). Vertical blank and line interrupts, LINE, STATUS.
Layers so far: text 80x30 and 40x30 (attributes, PALOFS, blink) and mono.
`emulator --screen FILE` writes the last frame as a PPM. Five behaviours
the design left open are now in hw/design.md (what shows through, the
border, display off, transparency, blink rate). Next: the BIOS on the
screen, then the WebUI's VGA scene, bitmaps, tiles and sprites.

**The BIOS on the screen (2026-10-24), video part 2.** BIOS 0.2
(`backplane/bios/video.s`): at boot, if the video card and the DMA
controller answer with their IDs, it copies an 8x16 font from flash block
30 into video memory (IBM VGA 8x16 from VileR's Ultimate Oldschool PC Font
Pack, CC BY-SA 4.0, converted by `font/fon2bin.py`), writes the CGA
palette and turns on an 80x30 text layer; ECHO then writes every character
to the screen as well as the serial line: CR, backspace, wrap at column 80,
and a cursor that the card blinks (attribute bit 7). Scrolling is one
4,640-byte memory-to-memory DMA (1.3 ms) and clearing copies a blank cell
onto the next with an overlapping DMA, the DMA controller's first real
work. The video code sits after the Woz Monitor, which moved only 3 bytes
(WOZMON $C865, ECHO $C954). Without a card the BIOS runs as before.

**VGA in the WebUI (2026-10-24), video part 3.** `Session::video()` and
`set_cpu_hz()` (kept across power cycles), and in the module `hasVideo`,
`videoFrames`, `videoFrame` (a view onto the RGBA frame) and `setClock`.
`MachinePort.watchVideo` sends the card's frame in `FrameState.video` only
while the VGA scene shows it and only when it is new. The monitor's Tiles
button is now VGA: the frame drawn pixel for pixel at the canvas's scale,
true colour or a mono phosphor's brightness. The debugger's Devices tab
decodes the card's registers; the page credits the font. The card's tick is
now 32.32 fixed point with the beam as line and column (no divisions);
with the text layer on, the core runs about 10% slower flat out (19-20 MHz
against 21-22 under node), some 4% of a core at real time.

**Bitmaps (2026-10-24), video part 4.** Bitmap 256 (256x240 at 8 bits,
doubled, no PALOFS) and bitmap 512 (512x480 at 4 bits, left pixel high),
both centred with the border either side, scroll wrapping in the image.

**Tiles (2026-10-24), video part 5.** Tile layers in a 320x240 space shown
doubled: 8x8 or 16x16 tiles at 4 or 8 bits, maps 32 or 64 entries each
way of VERA's 2-byte entries (tile 0-1023, flips, palette offset), fine
scroll wrapping round the map, either layer, over or under text. The scale,
the map-size bits and how the two palette offsets add are recorded in
hw/design.md.

**Sprites (2026-10-24), video part 6.** 64 sprites in VERA's 8-byte
entries, in the tiles' 320x240 space: 8-64 pixels each way at 4 or 8 bits,
flips, X and Y wrapping at 1024, depth against the layers, the lowest 32 a
line with a lower number in front, and collision groups reported in STATUS
and ISR at the next vertical blank. The Devices tab shows the table and
the last collision.

**The video demo (2026-10-24), video part 7; BIOS 0.3.** `C803R` at the
monitor (the BIOS now starts with a jump table: $C800 reset, $C803 the
demo) runs `backplane/bios/demo.s` from flash block 29 through W1: a 64x32
tile landscape scrolled a pixel a frame, twelve 16x16 balls between the
layers whose shadow table in RAM goes to the card by DMA in each vertical
blank, a 40-column caption in the BIOS's font, and a 16-step palette fade
in. It keeps clear of the BIOS's video memory, so a key goes back to the
monitor with its screen as it was, by register writes alone. Graphics are
made by `demo/make_assets.py` and committed. The Woz Monitor moved again
(WOZMON $C86B, ECHO $C95A). That finishes step 4 of hw/design.md's
emulator list; Apple II mode is next.

**The USB card (2026-10-24), part 1.** `UsbCard` (module B, $C580;
`usb 580-5FF space=io irq`): the keyboard as cooked ASCII in KEY (US
layout, the Apple's conventions, Caps Lock, key repeat) and raw HID events
in a 128-byte FIFO with DRQ, the mouse (relative sums or an absolute
position, latched together), the game port's 558 paddles timed in real
time and buttons, the speaker's toggle, the interrupts, and Pause/Break's
/NMI, which the Session takes. The host side takes HID usages and mouse
movement; the WebUI is to feed it. The few rules the tables left open are
in hw/design.md.

**The BIOS on the USB keyboard (2026-10-24), part 2; BIOS 0.4.**
`backplane/bios/input.s`: the Woz Monitor's key comes from the serial line
or the USB card's KEY, whichever has one (USB letters made capitals for
the monitor), the demo stops on either, and at boot KBCFG bit 7 is set so
Pause/Break breaks into the monitor. The wait loop (GETKEY) reads only
code and I/O, the card's ID each time round, so the lamps and the trace
show it plainly; the tests that watch it moved to $CB6x. ECHO is now $C956.

**The USB keyboard and mouse in the WebUI (2026-10-24), part 3.** On the
VGA scene the keyboard goes to the USB card as HID usages from
KeyboardEvent.code (`crt/usb-keyboard.ts`), Shift, Ctrl and Alt kept in
step with each event's flags, held keys released when focus goes, Cmd
shortcuts left to the browser; the mouse gives an absolute position in
640x480, buttons and the wheel. `MachinePort.sendKey/sendMouse`, the
module's `usbKey` and friends. Host keys reach the card at USB's pace,
one change per 8 ms polling interval (`UsbCard::queue_key`): a page that
sends a burst within one frame would otherwise overwrite KEY before the
BIOS read it, which the first browser test showed. The Devices tab shows
the card.

**Apple II mode, step 1 (2026-10-24): the cartridge and the way in; BIOS
0.5.** ROM B, the cartridge (module 5), is in backplane.machine as erased
flash (`rom ... -`); `MachineOptions::apple_rom` (the app's `--apple-rom`,
Session's `Config::apple_rom`) puts the owner's 16 KB IIe ROM, or its CD and
EF halves, at its start, and `apple_chars` the character ROM at $4000.
`backplane/bios/apple.s`, `C806R`: check the cartridge holds a IIe ROM
($FBB3 = $06), copy it to bank E ($EC000), clear banks 0-3 with four DMA
fills (inline, as the clear takes the stack), write MODE. The BIOS's jump
table now has eight entries, five spare, so new ones stop moving the code
after it (WOZMON $C880, ECHO $C968). The tests use a stand-in ROM; real
ones go in `test/host/fixtures/apple2e/`.

**Apple II mode, steps 2 and 3 (2026-10-24): the north bridge and the
clock.** MODE's APPLE bit now switches the bridge to the IIe's map: A0-A15
pass through and A16-A19 come from RAMRD/RAMWRT (so `NorthBridge::route`
takes a `write` flag), ALTZP, 80STORE with PAGE2 and HIRES, and the
language card (bank 2/3, the other $D000 bank at 4/5, the ROM image in
bank E). `$C000-$CFFF` goes to `AppleIo` (apple_io.h) at `APPLE_IO` in Io
space, whose reads and writes are bus cycles with the IIe's side effects
and whose peeks change nothing: the soft switches, their status at
$C011-$C01F (bits 0-6 the keyboard latch), the keyboard, speaker and game
port through the USB card, VBL from Apple timing in CPU cycles, the
language card's two-read rule, and the ROM space's INTCXROM, SLOTC3ROM and
INTC8ROM. Entering asks the run loop for a /RESET pulse
(`take_reset`/`pulse_reset`), which keeps APPLE; only a hard reset
leaves. `NorthBridge::clock_hz()` is the Apple clock in the mode
(1,020,484 Hz): Session clocks the cards by it, the app paces by it, and
the WebUI locks its clock buttons to 1.02. Found on the way: Session first
stored the fixed clock as the caller's, so after a power cycle the cards
kept Apple time; the fixed one is now kept apart. Not yet: the USB card's
Apple keys (Open/Closed Apple, Ctrl-Reset), the video card's Apple modes,
slot cards (the Disk II), the floating bus (reads $FF).

**Apple II mode, step 4 (2026-10-24): the USB card's Apple keys.** Open
Apple (left GUI or Alt) and Closed Apple (right GUI or Alt) are buttons 0
and 1 at $C061/$C062, ORed with the game port's, as a IIe wires them;
Ctrl-Delete is Ctrl-Reset: the card pulses /RESET, which Session takes and
which keeps Apple mode. `test_iie`, with the owner's ROMs in
`test/host/fixtures/apple2e/` (gitignored; skipped without them): the
enhanced IIe boots to "Apple //e" and Applesoft's prompt, answers PRINT
2+2 typed on the USB keyboard, keeps its program over Ctrl-Reset, cold
starts on Ctrl-Open Apple-Reset, and passes its own self-test ("System
OK") on Ctrl-Closed Apple-Reset: the north bridge's RAM, MMU and IOU as
the IIe's diagnostics see them.

**Apple II mode, step 5a (2026-10-24): the IIe's display; BIOS 0.6.** In
Apple mode the video card draws banks 0 and 1 as the north bridge's
switches say: 40- and 80-column text (80: aux then main), lo-res, hi-res
with NTSC artifact colour (a lone dot violet/green, or blue/orange shifted
half a dot by bit 7; pairs white), MIXED, PAGE2 unless 80STORE, flashing and
the alternate set; each Apple dot 2x2, 560x384 centred on black, the IIe's
colours in the card's 12 bits. C806R puts the character ROM's 2 KB at
$BE000 in video memory, from the cartridge or, without one, built from the
VGA font. test_iie, with the owner's ROMs, checks the boot banner dot for
dot against the real character ROM, and GR/PLOT and HGR/HPLOT typed into
Applesoft.

**Apple II mode, step 5b (2026-10-24): the double modes.** With 80COL and
AN3 off: double lo-res (80 blocks of 7 dots, aux then main, aux nibbles
shown rotated a bit as their dots fall out of phase) and double hi-res
(560 dots of aux and main bytes in turn, 4 dots to one of 16 colours).
Every Apple colour now follows one rule: dot x of a 560-dot line counts as
bit x mod 4 of a lo-res colour, which is where hi-res's violet, green,
blue and orange already came from. A hi-res gap between two dots of one
colour takes it. These follow the rule, not yet a real program: the double
modes have no ROM command, so they wait for a disk to show them. Next: the
WebUI (step 6).

**Apple II mode, step 6 (2026-10-24): the WebUI.** Insert IIe ROM… under
the monitor takes the owner's files, sorts them by size and contents
(16 KB, or CD and EF halves, and the character ROM), and plugs them into
the cartridge through the port (`insertCartridge`, Session::set_cartridge:
a power cycle, as with a cartridge plugged in with the power off). The
page keeps them in localStorage, never uploads them, and Eject forgets
them. Start Apple IIe is START then C806R; Ctrl-Reset and Reboot type the
IIe's resets on the USB keyboard. The Apple II scene is the card's 560x384
picture filling the screen, with the USB keyboard; entering the mode
switches to it, and AMODE lights. The Devices tab reads the cards in I/O
space (`readMemory(..., "io")`, Decoder::peek_io), which Apple II mode hides
from the CPU, and shows the IIe's switches (`readApple`). e2e/iie.spec.ts
boots the real enhanced IIe in Chromium and WebKit where the owner's ROMs
are (Applesoft answers PRINT 2+2; Ctrl-Reset gives a fresh prompt and keeps
the screen), and skips elsewhere. Next: the Disk II card.

- **Devices keep time in lockstep.** Every CPU cycle is one bus access, so
  the decoder counts them (plus `Bus::idle()` for WAI/STP) and catches a
  timed device up just before it is accessed and before each IRQ check,
  which the run loop now makes after every instruction. A 6522 counter
  read mid-instruction is exact and a timer interrupt is taken after the
  instruction it fired in, which a trace diffed against the board needs.
  The per-slice `tick()` is gone.
- **`Via6522`** (W65C22): ports with DDRs, CA1/CA2/CB1/CB2 edges per the
  PCR, IFR/IER, T1 one-shot and free-running with PB7 output, T2 one-shot
  and PB6 pulse counting. T1 times out N+1.5 cycles after a load and
  free-runs every N+2, as the datasheet gives. Not modelled: the shift
  register, input latching, CA2/CB2 handshake and pulse outputs.
- **`Acia6551`** as the W65C51N, stuck TDRE included; receive interrupt,
  DTR gating, programmed reset. No baud timing.
- **Peripherals** on a 6522's pins, wired in the machine file: an HD44780
  LCD (`hd44780`, 4- or 8-bit, any pins) and a PS/2 keyboard (`ps2kbd`,
  scan codes on a port with CA1).
- **Consoles.** The app talks to whatever the machine names `terminal` (a
  6821 or a 6551), `lcd` and `keyboard`, so it runs the Apple-1 and the
  Eater board alike; host keys are queued rather than dropped, and the
  LCD is drawn as a box.
- **`machines/eater.machine`** (Eater's final board: 6551, 6522, 4-bit LCD,
  PS/2) and **`eater-lcd8.machine`** (his first wiring). All six programs in
  `eater/src` run in test_eater, assembled by vasm as `eater/Makefile` does.

Found on the way: `eater/src/wozmon.s` did not assemble with its own
Makefile (a ca65 `.setcpu` line vasm rejects; removed), and
`eater/src/keyboard.s`'s shifted keymap has `#` and `$` swapped (left as
it is; the test pins it). Two HD44780 behaviours are inferred rather than
from the datasheet: DDRAM addresses $28-$3F in 2-line mode reach line 2
(Eater's Enter relies on it), and the busy flag always reads clear. The
VIA's T2 pulse-count interrupt point (after N pulses) varies between
datasheets and is unchecked on silicon.

Cost: unthrottled, the Apple-1 runs at ~138 MHz (was ~157) and the Eater
machine at ~107 MHz; real time is unaffected.

### Phase 4 — timing and CPU variant

- Per-instruction cycle counts with page-cross and branch-taken penalties;
  throttle to a real clock (e.g. 1 MHz). Harte's cycle traces become checked.
- **W65C02S as the primary core** — the real board's CPU (decided
  2026-10-18). Adds BRA, PHX/PLX/PHY/PLY, STZ, TRB/TSB, WAI/STP,
  RMB/SMB/BBR/BBS, `(zp)` addressing, BIT #imm/zp,X/abs,X, INC A/DEC A,
  fixed `JMP ($xxFF)`, D cleared on interrupts, valid N/Z in decimal mode and
  its extra decimal cycle; undefined opcodes as WDC's sized NOPs. Verified
  against Harte's `65x02/wdc65c02` set and Dormann's
  `65C02_extended_opcodes_test`. NMOS stays as a per-machine option for the
  Apple-1 machine.
- RDY, SYNC and VPB as bus signals visible to the console and bus spy.

**Done 2026-10-18,** further than planned: the core is bus-cycle accurate,
not just cycle-counted. Every cycle is the one bus access the silicon makes,
dummy reads and NMOS write-backs included, so cycles are simply accesses and
Phase 5 can diff traffic against the bus spy. Checked per cycle (address,
data, direction) against Harte for both models: 151,000 NMOS and 254,000
WDC cases at 1,000/opcode, all passing. Dormann's functional test passes on
both models and the 65C02 extended test on the W65C02S. NMOS decimal mode
now matches silicon on invalid BCD too (Clark's sequences), which closes the
gap Phase 0 found rather than setting it aside. Also: `cpu w65c02s` in
machine files (`machines/apple1-65c02.machine`, a Replica-1), WAI/STP,
7-cycle reset/IRQ/NMI sequences, `Device::tick(cycles)`, and a cycle-sliced
run loop with `--clock HZ` (default 1 MHz, measured at 1.000 MHz;
`--clock 0` runs ~166 MHz).

Moved to Phase 5: RDY, SYNC and VPB. They only matter to whatever watches
the bus, which is the bus trace and console that Phase 5 builds. (Phase 5 did
SYNC and VPB; RDY is still open, see below.)

To confirm on hardware: two WDC details come only from Harte. The decimal
extra cycle in immediate mode reads $007F (ADC) or $0000 (SBC), and the
$5C/$DC/$FC NOPs take 4 cycles (WDC's own documentation gives $5C 8).

### Phase 5 — bring-up tooling

- Debugger mirroring the console panel: run/halt/step, store/fetch, break,
  register and memory dump.
- Bus trace in the ESP32 bus spy's format, so emulator and board can be
  diffed cycle by cycle.
- Intel HEX / raw loaders and ca65 `.lbl` symbols for labelled traces.

**Done 2026-10-19.** Ctrl-] stops the running machine at a `debug>`
prompt with the panel's operations (run, step, fetch, store) plus
breakpoints, register edits and a disassembler; `--halt` starts there, as
the system key at Step/Halt would, and STP lands there too. The debugger
(`components/debug`) is terminal-free, taking a command line and returning
text, so all of it is unit-tested; the app only prompts. Fetch and the
disassembler read through a new side-effect-free `Bus::peek()`, so dumping
the 6821 does not eat a keystroke. `--load` takes Intel HEX or a raw image
at an address, and `--symbols` takes ld65 `-Ln` labels; the Apple-1 ROM
builds now assemble with `ca65 -g` and write `<rom>.lbl` (the ROMs are
byte-identical).

SYNC and VPB come from the core through a `CycleObserver`, and `--trace
FILE` writes one line per bus cycle with the 20-bit address, data, R/W,
SYNC, VPB and module ID. Nothing in `hw/` defined the spy's format, so
`docs/BUS_TRACE.md` defines it (version 1) for the ESP32 firmware to
follow, and `tools/trace_diff.py` reports the first cycle where a board
capture and an emulator run disagree, with `?` for anything the spy did
not capture and `--from` to line up a capture that began mid-program.

Checked: the disassembler against two independent oracles (lengths
against how far the core moves PC for every opcode of both models, and
ca65 reassembling every opcode to the same bytes, which caught an invalid
`jmp (a:$0080)`); HEX against Wikipedia's worked example; the trace line
for line from reset; the app end to end in a pty. Ten deliberate
mutations, one per mechanism, were each caught. Cost: the breakpoint
check before every instruction takes the unthrottled release build from
~166 to ~157 MHz; tracing runs at ~14 MHz.

Not done, and why:

- **RDY.** Holding the CPU mid-instruction needs a core that can stop
  between cycles; this one runs an instruction at a time. (Fixed-length
  wait states were added later, 2026-10-22; see the backplane section.) The debugger
  halts between instructions, and BUS_TRACE.md asks the spy to log only
  cycles where RDY is high, so a single-stepped board capture still diffs.
- **Cycle-level step** on the console needs the same, so it waits on RDY.
- **WAI/STP idle cycles** are not traced; they show as a gap in the cycle
  numbers, which the diff checks for equal length.

## 5. Open decisions

1. ~~CPU on the real board~~ — **W65C02S** (decided 2026-10-18). Its
   address bus is 16 bits, so A16..A19 come from the north bridge: four
   16 KB windows with 6-bit block registers (decided in hw/design.md,
   2026-10-21; modelled 2026-10-22).
2. ~~Machine description format~~ — **plain text**, one line per chip
   (decided 2026-10-18; `components/machine/include/machine.h`).
3. ~~`fmt`~~ — dropped in Phase 2, with easylogging++; `emu_log` is
   printf-style, like pinled's `ESP_LOGx`.

**The USB card's type-ahead (2026-10-24).** Keys queued from the host (a
burst, a paste) arrived every 8 ms whatever the software was doing, and
the Apple's KEY holds one character: Applesoft, a line's worth of work
behind, lost the start of the next line. A queued press that makes a
character now waits until the last one is taken (KEYCLR, or $C010), at
most 250 ms; a lone key is never held back, nor is Ctrl-Reset. Paste on
the VGA and Apple II scenes types on the USB keyboard; the serial line has
nobody listening in Apple II mode.

**The Disk II (2026-10-24): roadmap step 6.** The card in slot 6 (`disk2`
line, module C, ID $C1 at $C67F): the owner's two PROMs, the real sequencer
at 2 MHz, two drives with quarter-track heads, the motor's run-on, write
protect, and the MC3470's noise. Its PROM's address wiring was found by
running the owner's 341-0028 against a known bit stream: one layout of
the 2,880 possible decodes it, and the same layout writes. Disk images:
`.woz` (1 and 2) bit for bit, `.dsk`/`.do`/`.po` written out as DOS 3.3
formats them, the order decided from the contents; saved back as `.woz`,
or as sectors while every track still reads as 16 plain ones. The disks
stay in their drives through power cycles (Session), and in the WebUI the
last one is kept in IndexedDB, with what was written to it once the drive
stops. With the owner's files: the DOS 3.3 System Master boots and runs
its HELLO, DOS saves a program and lists it, a write-protected disk gets
DOS's own error, and The Oregon Trail's WOZ boots to its menu, natively
and in Chromium and WebKit.

**The sound card, step 7a (2026-10-25): the NMOS undocumented opcodes.**
The card's 6510 has to run C64 tunes, and their players use the NMOS
6502's undocumented opcodes, which the core used to run as one-byte NOPs.
The Nmos6502 model now runs all 244 non-jamming opcodes as the die does:
the RMW + ALU pairs (SLO, RLA, SRE, RRA, DCP, ISC) on the RMW's bus
sequence, SAX, LAX, LAS, the immediate ANC, ALR, ARR (with its decimal-mode
fix-ups), SBX, and the NOPs that address memory. The unstable ones follow
Harte's measurements: ANE and LXA OR in $EE; SHA, SHX, SHY and TAS store
the register ANDed with the base's high byte + 1, which becomes the
address's high byte on a page cross. The twelve jams stop the CPU until
reset (Harte has no cases for them: a jam never ends). Checked per cycle
against Harte's 6502 set, now 244 opcodes: 1,000 cases each for the 93 new
ones, all passing. The disassembler shows them as `.byte` lists of the
right length, which ca65 still reassembles, with the instruction as a
comment (`.byte $A7,$12 ; lax $12`).

**The sound card, step 7b (2026-10-25): its registers and mailbox.**
`SoundCard` (module A, $C500, `sound` line; DMA channel 2 in
backplane.machine): the two SIDs' 29 registers each, the card's 64 KB
through RAMADDR and RAMDATA (DRQ always asserted, so a DMA channel loads a
tune at bus speed), CTRL, STATUS, IEN, and the mailbox, with its interrupt
on bit 2 of IRQH. Host writes to the SIDs are dropped while CTRL RUN gives
them to the card's 6510; BOTH copies SID A's writes to SID B. The card's
side is card_receive(), card_send() and sid_write(), for step 7c's 6510
to call from $DE00, $DE01 and $D400. /RESET now reaches devices that ask
for it (Device::bus_reset()); the sound card stops and goes quiet, keeping
its RAM. The WebUI's Devices tab shows CTRL, the mailbox, RAMADDR and both
SIDs' voices and filter in words.

**The sound card, step 7c (2026-10-25): its own 6510.** The card runs an
NMOS 6510 (step 7a's undocumented opcodes) while CTRL RUN is set, one
cycle per SID clock, PAL 985,248 Hz or NTSC 1,022,727 Hz, against the
host's clock (set_cpu_hz, as the USB and video cards keep time). Its map
follows the C64's: 64 KB of RAM, the 6510's port at $00/$01 banking
$D000-$DFFF between I/O and RAM, SID A at $D400 repeating to $D7FF with SID
B at $D420 or $D500, CIA 1's timer A and its interrupt at $DC00, and the
mailbox at $DE00/$DE01; the rest of I/O reads $FF. CTRL RESET resets the
6510 and the CIA. Checked with hand-assembled programs (the clock to
within an instruction a second, the map, the timer's latch + 1 period
and one-shot) and with a real driver, test/host/sound/driver.s,
assembled by ca65: it counts 50 Hz frames on the CIA's interrupt with ISC,
LAX, SAX and DCP, banks I/O out and back, and plays notes on mailbox
commands, answering each. The Devices tab shows the 6510's registers,
its port and the timer (soundCpu in the bindings, readSound on the port).

**The sound card, step 7d (2026-10-25): the SIDs' oscillators and
envelopes.** `Sid` (sid.h), written from the chip's documented and
measured behaviour (reSID is GPL, so none of it is used): 24-bit phase
accumulators, the sawtooth, triangle (with ring), pulse and noise
waveforms (the 23-bit register, taps 22 and 17, clocked by bit 19), hard
sync and TEST; the ADSR envelopes with the measured rate periods, the
exponential decay and release, sustain, and the rate counter's wrap (the
ADSR delay). That is what software reads back: OSC3 and ENV3, live, so
tunes that use them for vibrato, noise or timing work. The chips run from
the SID clock whether or not the 6510 does, and are clocked up to the
moment they are touched (mid-instruction for the 6510's own accesses), in
bulk where nothing depends on the order. Combined waveforms are ANDed,
which the real chip's are not. Checked against the datasheet's timings:
a full attack at A=0 in 255 x 9 clocks, a decay through all six of the
exponential marks, the delay after a period drops below the count; and
OSC3 read by the 6510 on its LDA's own cycle. The Devices tab shows each
voice's envelope, and OSC3 and ENV3.

## Backlog

Decided to come later, not forgotten:

- **The sound card's sound** (2026-10-25). Steps 7a-7d model everything software can read back;
  8a the output stage (sid.h: waveform DACs, envelopes, the filter with 6581 and 8580 curves, the
  mixer and its DC, and the card's resampling to stereo). Against sidplayfp (reSIDfp) on the
  owner's 174 PSIDs, the spectrograms' median correlation is 0.97 (157 at 0.9 or more); timbre
  is within 1 dB a band on the 6581 and 8580 tunes. The low ones use combined waveforms, which are
  still ANDed; two others weren't pinned down. Still to come: the output to Web Audio (and the
  terminal app), the combined waveforms' real values, the DACs' nonlinearity and the 6581
  filter's distortion.
- **`.sid` playback** (2026-10-25). Loading and running a tune is done (`sid_file.h`, `sidcheck`):
  of 192 owner-supplied tunes, every PSID with a play routine plays, and 164 of them match
  siddump's registers call for call over 500 calls (siddump can't run 7 of the rest, 2 need a third
  SID and 1 relies on `$D440` mirroring `$D400`, which siddump doesn't do). Still to come: a way to
  play one in the WebUI (with the sound), RSIDs, which need the VIC-II's raster interrupt (all 17
  of the owner's wait on `$D012`), CIA 2 and timer B, and a third SID.
- **More Disk II image formats** (2026-10-24). The Disk II starts with `.woz` (WOZ 1 and 2: the
  track's bits, for copy-protected originals), `.dsk`/`.do` and `.po` (140 KB sector images, DOS
  or ProDOS order, told apart by their contents). Later:
  - `.nib` (232,960 bytes: each track's raw bytes; keeps most nibble-count protection);
  - `.d13` (116,480 bytes: 13-sector DOS 3.2 disks, with the 13-sector boot ROM and sequencer
    PROM, both in the owner's ROM folder);
  - `.2mg` (a header around a `.po` or `.dsk` image, with a write-protect flag).


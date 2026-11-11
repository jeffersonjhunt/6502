# 14 Emulator notes

Where the emulator differs from the design, what it leaves out, and how to run a program on it.

The design record, for the reasons: [hw/design.md, "Emulator"](../../hw/design.md#emulator).

## Machines

Status: **emulated**

The emulator ([emulator/](../../emulator/)) builds a machine from a plain-text file, one line per chip, so a memory
map can be read and diffed like a schematic. The format is in
[`machine.h`](../../emulator/components/machine/include/machine.h). Five files are in
[emulator/machines/](../../emulator/machines/):

| File | The machine | CPU |
|---|---|---|
| [`backplane.machine`](../../emulator/machines/backplane.machine) | the OO-1: this book's machine | `w65c02s` |
| [`apple1.machine`](../../emulator/machines/apple1.machine) | an Apple-1 with a 32 KB ROM; the terminal app's built-in default | `nmos6502` |
| [`apple1-65c02.machine`](../../emulator/machines/apple1-65c02.machine) | the same with a W65C02S, as on a Replica-1 | `w65c02s` |
| [`eater.machine`](../../emulator/machines/eater.machine) | Ben Eater's breadboard computer as his series ends: 6522, 6551, LCD, PS/2 keyboard | `w65c02s` |
| [`eater-lcd8.machine`](../../emulator/machines/eater-lcd8.machine) | his first board: the LCD on 8 data lines, no serial port | `w65c02s` |

The `cpu` line picks the core: `nmos6502`, the NMOS 6502, or `w65c02s`, WDC's W65C02S. There is no other.

### backplane.machine

Status: **emulated**

Each line of `backplane.machine` is a part of the OO-1:

| Line | What it fits |
|---|---|
| `cpu w65c02s` | the CPU |
| `northbridge backplane` | the north bridge: the windows, VEC, ROM space, the I/O hole, MODE and the DS3231, with its registers at `$C680` and the clock's tick wired to /IRQ as module D ([chapter 11](11-north-bridge-and-rtc.md)). `bootsel` after it holds /BOOTSEL ([chapter 3](03-boot.md#booting-from-ram-bootsel)) |
| `ram … module=0` to `module=3` | RAM A–D, 256 KB each, physical `$00000–$FFFFF` |
| `rom … space=flash module=4 … bios.rom` | ROM A, the 512 KB flash, from the BIOS's image |
| `rom … space=flash module=5 … -` | ROM B, the cartridge: `-` is erased flash, all `$FF`, until `--apple-rom` fills it |
| `acia6551 300-37F space=io … irq` | the 6551 stand-in, mirrored through the serial card's block ([chapter 9](09-serial-and-via.md#the-stand-in-6551)) |
| `via6522 340-34F space=io … irq` | the serial card's VIA, over the 6551's mirror: where two lines overlap, the later one answers |
| `joyports … via=via` | the two joysticks, on the VIA's PA0–PA4 and PB0–PB4 ([chapter 9](09-serial-and-via.md#the-joysticks)) |
| `sd 380-3FF … irq drq=0` | the mass storage card, its DRQ on DMA channel 0 ([chapter 10](10-storage.md)) |
| `dma 400-47F … irq` | the DMA controller ([chapter 5](05-dma.md)) |
| `video 480-4FF … irq` | the video card, drawing from banks 8–B ([chapter 6](06-video.md)) |
| `sound 500-57F … irq drq=2` | the sound card, RAMDATA on DMA channel 2 ([chapter 7](07-sound.md)) |
| `usb 580-5FF … irq` | the USB card ([chapter 8](08-usb.md)) |
| `disk2 600-67F` | the Disk II card, with no IRQ ([chapter 10](10-storage.md#the-disk-ii)) |
| `console 700-77F … irq` | the console card, the front panel ([chapter 12](12-front-panel.md)) |

- **`space=io`** puts a mapping in the I/O hole, `$000–$7FF` of it. It takes the module ID of its 128-byte block,
  so `300-37F` is module 6. A mapping can't cross into the next block.
- **`irq`** wires the device's interrupt output to /IRQ. A device on a line without it can't interrupt.
- **`drq=N`** wires the device's DMA request to channel N, 0–3. Only the SD card and the sound card have one
  ([chapter 5](05-dma.md#transfers-for-a-card)).
- **`wait=N`**, which `backplane.machine` doesn't use, holds RDY for N extra cycles on every access to the
  mapping, as a slow card would. The north bridge's WAIT0–3 add theirs on top
  ([`$C68A`](11-north-bridge-and-rtc.md#reg-c68a)).

The comments at the head of `backplane.machine` are older than some of the cards. Where they and the code disagree,
the code is right.

### The terminal app

Status: **emulated**

The terminal app, `emulator` ([`apps/emu/main.cpp`](../../emulator/apps/emu/main.cpp)), runs a machine in a
terminal. Whatever the machine names `terminal` is the host's terminal: on the OO-1 that is the 6551 stand-in, so
the terminal is the serial line. The app has no USB keyboard, no joystick and no front panel, and it shows the video
card's picture only at the end, with `--screen`.

| Option | What it does |
|---|---|
| `--machine FILE` | the machine file. Without it, the built-in Apple-1 |
| the last argument, a file | replaces the image of the machine's first `rom` line: for the OO-1, ROM A, the whole flash |
| `--clock HZ` | the emulated clock, 1,000,000 Hz unless set. `--clock 0` runs as fast as the host can |
| `--sd FILE` | a card image, whole 512-byte sectors, in the SD card's slot. Written through: the machine's writes change the file. A file that can't be written is a write-protected card |
| `--apple-rom FILE[,FILE]` | the owner's Apple IIe ROM into the cartridge: one 16 KB file, or the CD and EF halves |
| `--apple-chars FILE` | the IIe's character ROM, 4 or 8 KB |
| `--disk2-rom BOOT,SEQUENCER` | the Disk II card's two PROMs, 256 bytes each |
| `--disk FILE` | a `.woz`, `.dsk`, `.do` or `.po` in drive 1. What is written stays in memory; the file isn't changed |
| `--load FILE[@ADDR]` | an image put on the bus before the reset: Intel HEX by its extension, else raw at the CPU address ADDR |
| `--screen FILE` | the video card's last frame, 640×480, written as a PPM on the way out |
| `--symbols FILE` | an ld65 `-Ln` label file, for the debugger |
| `--trace FILE` | every bus cycle, in the bus spy's format, from the reset on |
| `--break ADDR`, `--halt` | a breakpoint in the debugger; start at its prompt |
| `--log FILE`, `-v` | a log, which is off unless asked for |

The ROM is the last argument, not an option's value: there is no `--rom`.

**The debugger.** Ctrl-] stops the machine at a `debug>` prompt, and `h` lists its commands: `r` run, `s` step
instructions, `f` show bytes with no side effects, `st` store, `x` the registers, `l` disassemble, `b` and `d`
breakpoints, `t` the trace on or off, `reset` a hard reset, the cards' and the CPU's, `q` quit. Addresses take
labels. STP stops into it too. On the OO-1 it has two more:

- **`rtc`** shows the real-time clock: the time, RTCCTL and the DS3231's registers.
- **`joy`** shows the joysticks. `joy 1 uf` holds joystick 1 up with fire pressed, and `joy 1 -` lets it go
  ([chapter 9](09-serial-and-via.md#the-joysticks)).

The console card's breakpoint and CTRL's HALT stop the app at its prompt, and going on from there lets the card go
too ([chapter 12](12-front-panel.md#switches-and-lights)).

### The WebUI

Status: **emulated**

The WebUI ([apps/web/](../../emulator/apps/web/), and [its README](../../emulator/apps/web/ui/README.md)) runs the
same core in the page, compiled to WebAssembly: a `Session`
([`session.cpp`](../../emulator/components/session/session.cpp)) behind
[`bindings.cpp`](../../emulator/apps/web/bindings.cpp), with `backplane.machine` and the BIOS built in.

- **The clock.** 1.02, 1.79, 3.58 and 7.16 MHz (1,020,484, 1,789,773, 3,579,545 and 7,159,090 Hz), and Max, as
  fast as the browser can. It starts at 7.16 MHz and remembers a pick. In Apple II mode the clock is the mode's,
  and the choice is locked.
- **The keyswitch** is the power. The page starts with the machine off and the key at OFF. Turning it to RUN is a
  cold start; LOCK makes the card ignore the panel's keys, START and HALT. A power cycle builds the machine afresh,
  but the DS3231 keeps its time on its battery, and the disks, the SD card, the joysticks and the panel's switches
  stay as they were.
- **The screen** is Terminal, the serial line, or Display, the video card's picture (the IIe's in Apple II mode).
  On Terminal the keyboard types on the 6551, in the visitor's own layout. On Display it is the USB card's
  keyboard, a US layout, Pause/Break and Ctrl-Delete included, and the mouse is the USB card's over the picture
  ([chapter 8](08-usb.md)).
- **Gamepads** drive the VIA's joysticks, each taking the lowest free stick. Keys can drive them too, once Settings
  turns that on: the arrows and Space for joystick 1, W A S D and the left Shift for joystick 2.
- **The front panel** is the console card's: the switch register, the keys, HALT, S INST / S BUS CYCLE, START, the
  keyswitch and the lamps ([chapter 12](12-front-panel.md#switches-and-lights)). The toolbar's Run, Halt and Step
  inst work through the HALT switch and CONT. Step cycle is greyed out: the emulator steps whole instructions.
- **The SD slot.** Each visit puts in a demo card with OODOS on it, so power-on boots OODOS. Settings takes a card
  image of one's own, saves the card with its writes, write-protects it, or ejects it. With the slot empty,
  power-on reaches the Woz Monitor.
- **Play .sid** plays a C64 tune on the sound card, through the card's host registers
  ([chapter 7](07-sound.md#playing-a-sid-file)). The SIDs and the USB card's speaker play through the browser.
- **Settings** also takes a BIOS of one's own, which becomes the flash and restarts the page on it; the owner's IIe
  ROM and the Disk II's PROMs, each added or ejected on its own, which power-cycles the machine; and disks for the
  two drives. The IIe ROM's row has **Start Apple IIe**: START, then `C806R` typed at the monitor. In Apple II mode
  the status strip has the IIe's **Reset** (Ctrl-Delete) and **Reboot** (Ctrl-Open Apple-Delete)
  ([chapter 13](13-apple-ii-mode.md#entering-apple-ii-mode)).
- **The debugger** shows the CPU's registers, a bus trace and the breakpoints. A breakpoint is the console card's
  comparator, written as the ESP32 would write it.

## What the emulator models

Status: **emulated**

### Timing

Status: **emulated**

- **The CPU is bus-cycle accurate.** Every cycle is one bus access, the dummy reads and writes included, at the
  address the silicon uses, so a trace can be compared with a logic analyser's cycle by cycle
  ([`cpu6502.h`](../../emulator/components/cpu6502/include/cpu6502.h)). It still executes an instruction at a time,
  and takes interrupts between instructions.
- **Wait states.** An access that RDY holds counts its extra cycles, from the north bridge's WAIT0–3 or a mapping's
  `wait=` ([`$C68A`](11-north-bridge-and-rtc.md#reg-c68a)).
- **DMA steals cycles.** The controller takes the bus before a CPU cycle, which costs one cycle, and the CPU waits
  out the bytes it moves, as for a wait state ([chapter 5](05-dma.md#the-controller)).
- **The cards are told the CPU's clock,** so their time stays right at any speed: the video card's frame, the USB
  card's key repeat and paddle timers, the sound card's SID clock, the SD card's start-up and sector times, the
  Disk II's sequencer, the DS3231's second ([chapter 1](01-overview.md#clocks)).
- **Flat out** (`--clock 0`, or Max), the cards keep 7.16 MHz time: a video frame is still about 119,400 cycles, so
  in real time the frames come faster than 60 a second.
- **What counts cycles keeps counting cycles.** The VIA's timers and the BIOS's wait after each character are in
  CPU cycles, so at a slower clock they take longer in time and the same in cycles
  ([chapter 9](09-serial-and-via.md#the-timers)).
- **Apple II mode fixes the clock** at 1,020,484 Hz in both apps, whatever was picked
  ([chapter 13](13-apple-ii-mode.md#the-clock)).
- **Halted, time stands still.** While the console card or the debugger holds the CPU, nothing runs
  ([chapter 12](12-front-panel.md#halting-and-stepping)).

### Logic, not pins

Status: **emulated**

The backplane's signals are modelled for what they do, not as pins: module decode on PS0–PS3, RDY and BE for wait
states and DMA, the /IACK poll, DRQ and /DACK, terminal count, AMODE and /BOOTSEL
([chapter 1](01-overview.md#the-backplane)). The north bridge and the cards are behaviour too: what a register does
when it is read or written, at the cycle it happens. The VIA's ports are the exception: their pins are modelled,
since the joysticks hang off them, as the LCD and the PS/2 keyboard do on Eater's machines. The pin-out, the
connectors and the power are the design's only ([chapter 1](01-overview.md#the-physical-bus)).

### What isn't there

Status: **proposed**

The design has these, and the emulator doesn't:

- **The TL16C552A:** COM1, COM2 and LPT, and the serial card's DMASEL, INTSTAT, LPTCTL and ID
  ([chapter 9](09-serial-and-via.md#com1-com2-and-lpt)). A 6551 stands in.
- **The VIA's user port,** and its shift register ([chapter 9](09-serial-and-via.md#the-user-port)).
- **Reprogramming the flash** ([chapter 2](02-memory-and-banking.md#reprogramming-the-flash)).
- **The bus spy** ([chapter 3](03-boot.md#the-bus-spy)). `bootsel` and `--load` stand in for it.
- **Turbo,** the CPU at ÷1 ([chapter 11](11-north-bridge-and-rtc.md#turbo)).
- **The sound card's bus timing:** BUSY, and RDY on a SID access ([chapter 7](07-sound.md#bus-timing)).
- **The sound card's revision 2 sound:** its registers, the raw PCM stream and its mix with the SIDs are in, but
  there is no EQ, volume, vis page or ES5503 sound yet ([chapter 7](07-sound.md#in-the-emulator-so-far)).
- **The USB card's other layouts, a gamepad on the paddles, device events, the FIFO on DMA and a real 558**
  ([chapter 8](08-usb.md#proposed)).
- **The console card's bus-cycle step, its breakpoint on the cycle, EXAM and DEP on the bus, AMODE, the design's
  ISR, its BE lamp and the ESP32's SPI header** ([chapter 12](12-front-panel.md#what-the-emulator-leaves-out)).
- **Apple II mode's long cycle,** every 65th cycle stretched
  ([chapter 13](13-apple-ii-mode.md#the-long-cycle)).

The emulator's own backlog, beside the design, has `.nib`, `.d13` and `.2mg` disk images
([chapter 10](10-storage.md#disk-images)).

## What differs

Status: **emulated**

Every place the chapters note that the emulator differs from hw/design.md, by chapter. The books describe the
emulator; each link goes to the note.

- **The backplane and the clocks** ([chapter 1](01-overview.md))
  - The backplane's signals are behaviour, not pins ([The backplane](01-overview.md#the-backplane)).
  - The CPU runs at the clock the app sets, flat out included: the WebUI's 1.02–7.16 MHz, the terminal app's
    1 MHz unless set. The machine has ÷2 only ([Clocks](01-overview.md#clocks)).
- **Memory** ([chapter 2](02-memory-and-banking.md))
  - The flash ignores every write, so it can't be reprogrammed
    ([Reprogramming the flash](02-memory-and-banking.md#reprogramming-the-flash)).
- **The boot** ([chapter 3](03-boot.md))
  - /BOOTSEL is the machine file's `northbridge backplane bootsel`, and an image loaded into RAM stands in for the
    bus spy's ([Booting from RAM](03-boot.md#booting-from-ram-bootsel)).
- **Interrupts** ([chapter 4](04-interrupts.md))
  - There is no INTSTAT: `$C322` is the 6551's mirror, so a handler tells the VIA by its IFR
    ([The serial card's INTSTAT](04-interrupts.md#the-serial-cards-intstat)).
  - Pause/Break comes only from the WebUI. The terminal app has no USB keyboard, so there Break comes only from the
    console card ([Pause/Break](04-interrupts.md#pausebreak)).
- **DMA** ([chapter 5](05-dma.md))
  - Channel 0's DRQ is the SD card's, and channel 2's the sound card's, always (RAMDATA's, unless its DRQSEL
    picks another: [`$C563`](07-sound.md#reg-c563)). Nothing drives channels 1
    and 3, which the design gives to the serial card and to blits, and channel 2 isn't shared with the USB card
    ([Transfers for a card](05-dma.md#transfers-for-a-card), [`$C410`](05-dma.md#reg-c410)).
- **Video** ([chapter 6](06-video.md))
  - A whole sprite table copied by DMA takes about 143 µs, at two cycles a byte, where hw/design.md says about
    72 µs ([Sprites](06-video.md#sprites)).
  - The card reads the palette from video memory for every line, where the design copies it into the card when
    PALBASE changes ([The palette](06-video.md#the-palette)).
  - The card interrupts on ISR AND IEN, where the design's register table says any pending ISR bit
    ([Interrupts](06-video.md#interrupts)).
  - hw/design.md still labels the boot splash proposed; the emulator has it
    ([The boot splash](06-video.md#the-boot-splash)).
- **Sound** ([chapter 7](07-sound.md))
  - Two waveforms at once are ANDed, and OSC3 then reads approximately. The DACs' nonlinearity, the 6581 filter's
    distortion and the external input aren't modelled ([A voice](07-sound.md#a-voice)).
  - SID writes go to the chip at once: STATUS's BUSY is always 0, and the card never holds the CPU
    ([Bus timing](07-sound.md#bus-timing), [`$C541`](07-sound.md#reg-c541)).
  - The card's RAM is all `$00` at power-on, where the design leaves it undefined
    ([`$C545`](07-sound.md#reg-c545)).
- **USB** ([chapter 8](08-usb.md))
  - The event FIFO holds 128 bytes, so EVCOUNT never reaches the design's 255 ([`$C583`](08-usb.md#reg-c583)).
  - LEDS reads back, where the design has it write-only, and lights nothing ([`$C585`](08-usb.md#reg-c585)).
  - KBCFG's layout bits are kept, but the card cooks in US only ([`$C587`](08-usb.md#reg-c587)).
  - A read of PTRIG starts the paddles' timers too, where the design says a write ([`$C5A1`](08-usb.md#reg-c5a1)).
  - The apps have no control for the paddles or the buttons, so they read 0
    ([The game port](08-usb.md#the-game-port)).
  - The 558 is timed in software. A keyboard and a mouse are always there, so DEVICES reads `$03` and ISR's device
    bit is never set. The FIFO's DRQ reaches no channel ([Proposed](08-usb.md#proposed)).
- **Serial and the VIA** ([chapter 9](09-serial-and-via.md))
  - A 6551 stands in for the TL16C552A, mirrored through the block, so `$C37F` reads its CTRL, not an ID
    ([COM1, COM2 and LPT](09-serial-and-via.md#com1-com2-and-lpt), [`$C303`](09-serial-and-via.md#reg-c303)).
  - The 6551 sends a byte at once. Its baud rate, character time, parity, framing and overrun errors, echo mode and
    modem lines aren't modelled ([The stand-in 6551](09-serial-and-via.md#the-stand-in-6551),
    [`$C302`](09-serial-and-via.md#reg-c302)).
  - The 6551 doesn't hear /RESET: only a power-on sets its CMD and CTRL
    ([The stand-in 6551](09-serial-and-via.md#the-stand-in-6551)).
  - The VIA's shift register is a plain byte, and its flag is never set ([`$C34A`](09-serial-and-via.md#reg-c34a)).
  - CA1, CA2, CB1 and CB2 aren't connected, and latching the ports' inputs is stored and does nothing
    ([The shift register and the control lines](09-serial-and-via.md#the-shift-register-and-the-control-lines)).
  - The joysticks take both ports, so there is no user port: its lines read 1
    ([The user port](09-serial-and-via.md#the-user-port)).
- **Storage** ([chapter 10](10-storage.md))
  - STATUS's DATA is set for a whole sector or a whole free buffer, where the design's register table says a byte
    ([`$C381`](10-storage.md#reg-c381)).
  - A hard reset keeps the units mounted; only a power cycle starts the RP2350 afresh
    ([Starting up and reset](10-storage.md#starting-up-and-reset)).
  - The card's times are estimates. A CRC error, 3, never comes, and error 4 is a host error with the image
    ([Errors](10-storage.md#errors), [Timing](10-storage.md#timing)).
  - The Disk II's bit cell follows a WOZ image's own timing, where the design gives every cell 4 µs, and the north
    bridge resets the card with the IIe's switches, not through the card's own reset
    ([The sequencer and the drives](10-storage.md#the-sequencer-and-the-drives)).
  - The Disk II's head moves the moment a phase changes, and its motor is at speed the moment it is switched on.
    A real drive needs time to step, to settle and to spin up
    ([Timing in native mode](10-storage.md#timing-in-native-mode)).
  - The Disk II's motor runs on for 0.998 s in native mode, where hw/design.md says a second: the run-on is
    counted in the sequencer's steps, Apple II mode's second without its long cycle
    ([Timing in native mode](10-storage.md#timing-in-native-mode)).
  - When the emulator runs native mode's CPU slower than 7.16 MHz (the WebUI's speed settings), the Disk II's
    sequencer keeps its own speed, so a disk byte is fewer than 224 of those cycles and a write loop counted for
    7.16 MHz is out of step; reading, which is polled, still works
    ([Timing in native mode](10-storage.md#timing-in-native-mode)).
- **The north bridge and the clock** ([chapter 11](11-north-bridge-and-rtc.md))
  - MODE reads back, where the design has it write-only ([`$C685`](11-north-bridge-and-rtc.md#reg-c685)).
  - The wait states apply at ÷2, to every module, the DMA controller's accesses too; the design keeps them for a
    turbo mode ([`$C68A`](11-north-bridge-and-rtc.md#reg-c68a)).
  - MODE's TURBO bit is kept as written and does nothing ([Turbo](11-north-bridge-and-rtc.md#turbo)).
  - The DS3231 takes the host's time once, never runs its battery flat, reads 25.00 °C, ignores its aging
    register and answers at once, with no 250 ms start-up
    ([The emulated chip](11-north-bridge-and-rtc.md#the-emulated-chip)).
- **The front panel** ([chapter 12](12-front-panel.md))
  - S BUS CYCLE steps an instruction, not a bus cycle
    ([Halting and stepping](12-front-panel.md#halting-and-stepping), [`$C703`](12-front-panel.md#reg-c703)).
  - A read or write breakpoint halts after the instruction, not on its own cycle, though LADDR has the exact cycle
    ([The breakpoint](12-front-panel.md#the-breakpoint), [`$C70B`](12-front-panel.md#reg-c70b)).
  - EXAM and DEP take no bus cycle ([EXAM and DEP](12-front-panel.md#exam-and-dep)).
  - LSTAT's AMODE bit reads 0, and the BE lamp stays dark ([`$C714`](12-front-panel.md#reg-c714),
    [Switches and lights](12-front-panel.md#switches-and-lights)).
  - The card's registers are out of reach in Apple II mode by the north bridge's map, not by AMODE
    ([What the emulator leaves out](12-front-panel.md#what-the-emulator-leaves-out)).
  - ISR is set only when IEN was set at the key press ([`$C719`](12-front-panel.md#reg-c719)).
  - The ESP32's SPI header isn't there; the WebUI reads the registers as that path would
    ([What the emulator leaves out](12-front-panel.md#what-the-emulator-leaves-out)).
- **Apple II mode** ([chapter 13](13-apple-ii-mode.md))
  - The clock is an even 1,020,484 Hz, the IIe's average, with no long cycle every 65th
    ([The long cycle](13-apple-ii-mode.md#the-long-cycle), [Clocks](01-overview.md#clocks)). The Disk II's
    sequencer takes two steps a cycle with no long cycle either: 0.2% off, inside a drive's tolerance
    ([The sequencer and the drives](10-storage.md#the-sequencer-and-the-drives)).
  - The native cards are quiet in the mode because the /RESET pulse that enters it resets them, where hw/design.md
    has them kept quiet while AMODE is high. The DMA controller is reset with them, and nothing can start it again
    ([In Apple II mode](04-interrupts.md#in-apple-ii-mode), [DMA in Apple II mode](05-dma.md#apple-ii-mode),
    [The /RESET pulse](13-apple-ii-mode.md#the-reset-pulse)).
  - The 6551 keeps its CMD through that pulse, so with its receive interrupt on, a byte arriving pulls /IRQ in the
    mode. The monitor's `$0B` has it off ([In Apple II mode](04-interrupts.md#in-apple-ii-mode),
    [The /RESET pulse](13-apple-ii-mode.md#the-reset-pulse)).
  - A write to an odd language-card address only starts the count of reads again. hw/design.md has it clear the
    write enable too ([The language card](13-apple-ii-mode.md#the-language-card)).
  - `$C019`'s vertical blank is counted from the bus's cycles in the north bridge, where hw/design.md's switch list
    has the video card drive it; the same section's decisions agree with the emulator
    ([The status reads](13-apple-ii-mode.md#the-status-reads)).
  - `$C060`, the cassette input, reads `$00`, and so does its mirror `$C068`, where hw/design.md says the cassette
    reads `$FF`. Without a USB card the whole game port reads `$00`
    ([What isn't there](13-apple-ii-mode.md#what-isnt-there),
    [`apple_io.cpp`](../../emulator/components/devices/apple_io.cpp)).

## Known bugs

Status: **emulated**

None that the chapters know of. Where the emulator does less than the design, it is in
[What isn't there](#what-isnt-there) and [What differs](#what-differs).

## What the BIOS leaves

Status: **emulated**

Not bugs, but what BIOS 0.13 does, which a program run from the monitor has to know:

- **Break turns off what the BIOS's IRQ handler can't answer:** the cards' IENs, the clock's TIE, the VIA's IER
  (`$7F`) and the console card's ISR (`$01`), the last two since BIOS 0.11
  ([IRQ and NMI](04-interrupts.md#irq-and-nmi), [Pause/Break](04-interrupts.md#pausebreak)). Under BIOS 0.10 a VIA
  interrupt or a pending panel key kept /IRQ low after Break, so a program written for it clears both itself.
- **Break stops a revision 2 sound card's PCM stream and ES5503,** since BIOS 0.12: it writes PCMCTL `$04` (FLUSH
  alone, so the FIFO is emptied) and clears DOCCTL's RUN, keeping its other bits. The ES5503's registers and wave
  RAM, the volumes and the EQ stay as they were, and an ES5503 interrupt pending at Break stays pending on its route
  ([Silencing them](07-sound.md#silencing-them)). BIOS 0.11 left both playing behind the monitor.
- **Nothing in the BIOS clears the video card's ISR,** so after a reset it soon reads `$03`, and after Break LINE
  and the collision bits are as a program left them ([Interrupts](06-video.md#interrupts)).
- **The SD card's ERR is set under the monitor** after a power-on with a card that has no `/BOOT.BIN`, from the
  BIOS's MOUNT, and ISR's bit 0 is set after any of the BIOS's commands
  ([What the BIOS leaves](10-storage.md#what-the-bios-leaves)).
- **The 6551's receive interrupt is off** under the monitor (CMD `$0B`), and stays off into Apple II mode
  ([The stand-in 6551](09-serial-and-via.md#the-stand-in-6551)).

## Running a program

Status: **emulated**

The terminal app's commands below are run from `emulator/`, after `cmake --preset release` and
`cmake --build --preset release` ([emulator/README.md](../../emulator/README.md)).

### A ROM on the bare machine

Status: **emulated**

`make -C backplane/bios` builds `bios.rom`, the whole 512 KB of ROM A with the BIOS in its top 16 KB, and
`bios.rom.lbl`, its labels. Then
`./build/release/apps/emu/emulator --machine machines/backplane.machine ../backplane/bios/bios.rom` boots it to the
Woz Monitor on the terminal ([chapter 3](03-boot.md)). `--symbols ../backplane/bios/bios.rom.lbl` gives the debugger
its labels.

A ROM of one's own goes in the same place, as the last argument. It is the flash's image from its first byte, so it
must be the whole 512 KB: the reset vector is in block 31, the last 16 KB, and a shorter file doesn't reach it
([chapter 2](02-memory-and-banking.md#physical-memory)). In the WebUI, Settings takes one as the BIOS.

**From the monitor**, a program in RAM runs with its address and `R`. It gets there typed in with `:`, or loaded
with `--load prog.bin@1000` before the reset, at an address the BIOS leaves alone
([chapter 2](02-memory-and-banking.md#zero-page-and-low-ram)); then `1000R`. It goes back to the monitor with BRK,
or with a jump to `$C800`, which starts the machine again ([chapter 3](03-boot.md#the-monitor)). It prints with
`JSR $C80F`, ECHO, on the serial line and the screen ([chapter 3](03-boot.md#the-bioss-entry-points)).

### A program from the SD card

Status: **emulated**

The BIOS boots `/BOOT.BIN` from the SD card's FAT file system at power-on, once the splash is over, or at `C80CR`.
It loads at `$1000`, 44 KB at most, and is called with JSR, so an RTS goes back to the monitor
([chapter 3](03-boot.md#booting-from-the-sd-card)). `mkfs.fat -C card.img 32768` makes a card image, and `mcopy`
puts the file on it. Then `--sd card.img` puts it in the terminal app's slot, or Settings in the WebUI's. `C809R`
lists the card's root directory, as a check.

### A program under OODOS

Status: **emulated**

A `.COM` or `.PRL` under OODOS is built and calls OODOS as the
[OODOS Programmer's Guide](../oodos-programmers-guide/README.md) says, and [AGENTS.md](../../AGENTS.md) has the
rules: the BDOS call, the header, the zero page, the hardware OODOS owns. Building it, and putting it on a card with
cpmtools, are in [Appendix F](../oodos-programmers-guide/appendix-f-building.md). A card with OODOS boots it at
power-on, as the WebUI's demo card does.

### Checking a program

Status: **emulated**

Programs are checked in the emulator's native tests, run by `ctest`
([emulator/test/host/README.md](../../emulator/test/host/README.md)). For a program under OODOS, the rig in
[`oodos_rig.h`](../../emulator/test/host/oodos_rig.h) does it: `CcpRig` boots OODOS.SYS, `put_m()` puts a `.COM`
on `M:`, and `cmd("m:name")` types it at the prompt and returns what it printed.
[`test_docs_examples.cpp`](../../emulator/test/host/test_docs_examples.cpp) is the smallest suite that does this,
for the books' examples in [docs/examples/](../examples/)
([Appendix F, "Running it"](../oodos-programmers-guide/appendix-f-building.md#running-it)).

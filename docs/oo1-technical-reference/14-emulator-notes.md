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
| `northbridge backplane` | the north bridge: the windows, VEC, ROM space, the I/O hole, MODE and the DS3231, with its registers at `$C680` and the clock's tick wired to /IRQ as module D ([chapter 11](11-north-bridge-and-rtc.md)). `bootsel` after it holds /BOOTSEL ([chapter 3](03-boot.md#booting-from-ram-bootsel)). The bridge is revision 3 (ID `$D3`, *proposed*), with WIN0H–WIN3H, VECH and MAP ([chapter 11](11-north-bridge-and-rtc.md#the-high-window-registers-and-map)), [the timebase](11-north-bridge-and-rtc.md#the-timebase), [the random number chip](11-north-bridge-and-rtc.md#the-random-number-chip) and [the common hole](11-north-bridge-and-rtc.md#the-common-hole). `rng_chip=locked` (the default), `unlocked` or `none` sets the random number chip the bridge finds at power-on. The chip's bytes are a fixed stream by default, the same every run, for tests; the WebUI and the terminal app take the host's random device. Any other value is refused, the token named |
| `ram 00000-3FFFF name=ram_a` to `ram_d` | the RAM card, 1 MB at physical `$000000–$0FFFFF`, in four lines, RAM A–D, 256 KB each. Memory answers by address, so the lines carry no `module=` since B2 (*proposed*, [the 24-bit bus](#the-24-bit-bus)) |
| `flash 00000-7FFFF space=flash name=rom_a bios.rom` | ROM A, the 512 KB flash, two chips that can be written ([below](#the-flash)), from the BIOS's image: memory `$F00000–$F7FFFF`. Its blocks 0–5, erased in `bios.rom`, take the IIe images and sets from `--apple-rom` and `--apple-chars` (since B5, *proposed*) |
| `flash 80000-FFFFF space=flash name=rom_b -` | ROM B, the game cartridge, memory `$F80000–$FFFFFF`, two chips as ROM A: `-` is erased flash, all `$FF`. Before B5 both were `rom` lines, and `--apple-rom` filled ROM B |
| `acia6551 300-37F space=io … irq` | the 6551 stand-in, mirrored through the serial card's block ([chapter 9](09-serial-and-via.md#the-stand-in-6551)) |
| `via6522 340-34F space=io … irq` | the serial card's VIA, over the 6551's mirror: where two lines overlap, the later one answers |
| `joyports … via=via` | the two joysticks, on the VIA's PA0–PA4 and PB0–PB4 ([chapter 9](09-serial-and-via.md#the-joysticks)) |
| `sd 380-3FF … irq drq` | the mass storage card, its DMA request on /DREQ, served by channel 0 (CARD `$07`) ([chapter 10](10-storage.md)) |
| `dma 400-47F … irq` | the DMA controller ([chapter 5](05-dma.md)) |
| `video 480-4FF … irq drq` | the video card ([chapter 6](06-video.md)), revision 2, ID `$92` (*proposed*), the only one: it draws from its own 2 MB, with its data port's DMA request on /DREQ, served by channel 1 (CARD `$09`) ([below](#revision-2-of-the-video-card)). The line takes no `rev=` |
| `aperture E00000-E0FFFF` | the video card's aperture (*proposed*): 64 KB at `$E00000` and nowhere else, four slots on card memory ([chapter 6](06-video.md#the-aperture)), at `$E00000`. While the north bridge's AP8 (MAP, bit 0) is set, as from every reset whatever is fitted, it sends bank 8 (`$080000–$08FFFF`) there, so the CPU's windows and DMA reach the aperture at bank 8 as before, and RAM C's bank 8 can't then be reached by them; with AP8 clear bank 8 is RAM (*proposed*, B7). With no video card the line is inert: bank 8 reads `$FF` while AP8 is set, until the BIOS clears it at boot |
| `sound 500-57F … irq drq` | the sound card (revision 3, the only one; `memsize=` optional, the line takes no `rev=`), its DMA request (DATA's, while a transfer is open) on /DREQ, served by channel 2 (CARD `$0A`) ([chapter 7](07-sound.md)) |
| `usb 580-5FF … irq` | the USB card ([chapter 8](08-usb.md)), with its DMA request left off /DREQ |
| `disk2 600-67F` | the Disk II card, with no IRQ ([chapter 10](10-storage.md#the-disk-ii)) |
| `console 700-77F … irq` | the console card, the front panel ([chapter 12](12-front-panel.md)) |

- **Addresses** are up to six hex digits: memory is 24 bits, `$000000–$FFFFFF`. **`space=flash`** takes an
  offset into the flash, `00000–FFFFF`, the same as memory `$F00000–$FFFFFF`.
- **`space=io`** puts a mapping in the I/O hole, `$000–$7FF` of it. It takes the module ID of its 128-byte block,
  so `300-37F` is module 6. A mapping can't cross into the next block.
- **`irq`** wires the device's interrupt output to /IRQ. A device on a line without it can't interrupt.
- **`drq`**, a flag, wires the device's DMA request to /DREQ, the shared line (*proposed*, since B2). Its module
  is its I/O block's, and the channel whose CARD names that module serves it; CARD's reset values give channel 0 the
  SD card, 1 the video card and 2 the sound card ([chapter 5](05-dma.md#transfers-for-a-card)). A device on a line
  without it never pulls /DREQ. A line with no module ID (a memory line without `module=`) can't take it: no
  CARD could name the card, so `drq` there is refused with an error. The machine file names no channel: `drq=N`
  is an unknown option.
- **`wait=N`**, 0–15, which `backplane.machine` doesn't use, is the card's /WAIT: N extra cycles on every CPU
  access to the mapping, as a slow card would ask, and N on each DMA grant cycle it answers. On a fly-by both the
  memory and the card answer, and the grant takes the longer of their two. The north bridge's WAIT0–3 add nothing
  since B2 ([`$C68A`](11-north-bridge-and-rtc.md#reg-c68a)).
- **`module=N`**, the ID a mapping reports on PS0–PS3, is for a bus with no north bridge (the Apple-1 and Eater
  machines, and tests). `backplane.machine` doesn't use it: the bridge gives memory's PS, and an I/O line's module is
  its block's.

### The 24-bit bus

Status: **proposed**

Emulated as step B1 of [the new bus](../../hw/design.md#the-new-bus-memory-by-address-dma-by-module-id-one-backplane)
([as built](../../hw/design.md#b1-as-built)). Nothing a program sees changes; the bus underneath it is the new one:

- **Physical addresses are 24 bits,** A0–A23, and memory answers by its address, not by a module ID: RAM at
  `$000000–$0FFFFF`, the aperture at `$E00000`, ROM A at `$F00000` and ROM B at `$F80000`. A window's ROM block
  *b* is `$F00000 + b × $4000`.
- **No memory answers an I/O cycle,** `$C000–$C7FF`, whatever A0–A23 hold. Module IDs 0–5 and F, once RAM A–D and
  the flash, are empty: their blocks read `$FF` and have no ID byte.
- **PS0–PS3 on a memory cycle** carry the master's ID: D for the CPU, 8 for a DMA memory-to-memory cycle, the card's
  on a fly-by. The debugger's trace and the WebUI's show it so ([BUS_TRACE.md](../../emulator/docs/BUS_TRACE.md)),
  with the address in at least five hex digits (six when A20–A23 aren't 0).
- **What a tool reads at a physical address.** The decoder's `peek_physical()` (and the WebUI's physical view, the
  tests' `phys()`) is the CPU card's view: while MAP's AP8 is set (*proposed*, B7) bank 8 is the aperture, as
  `$E0xxxx` is, and reads `$FF` without the video card; with AP8 clear bank 8 is RAM; in Apple II mode both
  read `$FF`; without the video card `$E0xxxx` reads `$FF`. It follows MAP as it is written. The console card's
  EXAM and DEP go by the bus itself (`peek_bus()`), where bank 8 is always RAM: the redirection is the CPU card's.
- **The console card stays 20 bits:** its breakpoint matches a cycle only when A20–A23 are 0, so it never fires on
  a cycle to the flash or the aperture, and LADDR and the lights show A0–A19.

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
| the last argument, a file | replaces the image of the machine's first `rom` or `flash` line: for the OO-1, ROM A, the whole flash |
| `--clock HZ` | the emulated clock, 1,000,000 Hz unless set. `--clock 0` runs as fast as the host can |
| `--sd FILE` | a card image, whole 512-byte sectors, in the SD card's slot. Written through: the machine's writes change the file. A file that can't be written is a write-protected card |
| `--apple-rom [N=]FILE[,FILE]` | the owner's Apple IIe ROM into ROM A's image N, 0–3 (0 without the number): one 16 KB file, or the CD and EF halves, in one option or two. Only a leading digit and `=` count, so `dir/x=y.rom` is a file. Since B5 (*proposed*); before, the cartridge |
| `--apple-chars [N=]FILE` | the IIe's character ROM, 4 or 8 KB, into set N (0 without the number), at ROM A's `$10000 + $2000 × N`; once for each set |
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
labels. STP stops into it too. On the OO-1 it has three more:

- **`rtc`** shows the real-time clock: the time, RTCCTL and the DS3231's registers.
- **`joy`** shows the joysticks. `joy 1 uf` holds joystick 1 up with fire pressed, and `joy 1 -` lets it go
  ([chapter 9](09-serial-and-via.md#the-joysticks)).

**`vram`** (Proposed, with the video card's [revision 2](06-video.md#revision-2-card-memory)) shows and writes the
card's own memory, which the CPU reaches only through the aperture: `vram 2000 32` shows 32 bytes from card address
`$002000`, and `vram 1E1000 = 12 34` writes two there. It takes no bus cycles and leaves the port's registers as they
are; past the fitted memory it reads `$FF` and drops writes, as the card does. The WebUI has the same, as Card memory
under the video card in the debugger's Devices tab.

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
  Since BIOS 0.14: switch 19 up, then START, asks for the ROM's diagnostics, and CONT on the panel enters
  them ([chapter 15](15-diagnostics.md#the-three-entries)); switches 16–18 are their options there.
- **The SD slot.** Each visit puts in a demo card with OODOS on it, so power-on boots OODOS. Settings takes a card
  image of one's own, saves the card with its writes, write-protects it, or ejects it. With the slot empty,
  power-on reaches the Woz Monitor.
- **Play .sid** plays a C64 tune on the sound card, through the card's host registers
  ([chapter 7](07-sound.md#playing-a-sid-file)). The SIDs and the USB card's speaker play through the browser.
- **Settings** also takes a BIOS of one's own, which becomes the flash and restarts the page on it; the owner's IIe
  ROM, written into ROM A's image 0 and set 0 (since B5, *proposed*; Eject erases them), and the Disk II's PROMs,
  each added or ejected on its own, which power-cycles the machine; and disks for the two drives. ROM A's blocks
  0–5 keep what they hold through power cycles ([below](#the-flash)). The IIe ROM's row has **Start Apple IIe**:
  START, then `C806R` typed at the monitor. In Apple II mode the status strip has the IIe's **Reset** (Ctrl-Delete)
  and **Reboot** (Ctrl-Open Apple-Delete) ([chapter 13](13-apple-ii-mode.md#entering-apple-ii-mode)).
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
- **Wait states.** An access that RDY holds counts its extra cycles, from a mapping's `wait=`, the card's /WAIT
  ([above](#backplanemachine)). Since B2 the north bridge's WAIT0–3 add none
  ([`$C68A`](11-north-bridge-and-rtc.md#reg-c68a)).
- **DMA steals cycles.** The controller takes the bus before a CPU cycle, which costs one cycle, and the CPU waits
  out the bytes it moves, as for a wait state ([chapter 5](05-dma.md#the-controller)). A card's transfer adds the
  poll, one more cycle each take (*proposed*, B2). Like the takeover, the poll is counted as a stolen cycle but
  isn't a bus cycle anyone sees: the trace, the cycle observers and a breakpoint never meet it, and the front
  panel's lights show it as they show the takeover, the CPU's held address with RDY low. The controller is caught
  up with the timed cards before it looks at /DREQ, so it sees each card's request as of that cycle. With CARD's
  WIDE (*proposed*, B4) a transfer is a word where the rules allow, at a byte's cost
  ([chapter 5](05-dma.md#16-bit-transfers)).
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

The backplane's signals are modelled for what they do, not as pins: module decode on PS0–PS3, RDY and BE for DMA,
/WAIT, the /IACK poll, /DREQ and the DMA controller's poll and grant (/DMA), terminal count, AMODE and /BOOTSEL
([chapter 1](01-overview.md#the-backplane)). The north bridge and the cards are behaviour too: what a register does
when it is read or written, at the cycle it happens. The VIA's ports are the exception: their pins are modelled,
since the joysticks hang off them, as the LCD and the PS/2 keyboard do on Eater's machines. The pin-out, the
connectors and the power are the design's only ([chapter 1](01-overview.md#the-physical-bus)). The line that tells
a byte cycle from a word cycle, /WORD (*proposed*, B6), is modelled as the call: the decoder's word cycles, which
the DMA controller makes with WIDE (*proposed*, B4), and a word flag on the cycle the observers and the console card
see, once a word at its even address ([chapter 5](05-dma.md#16-bit-transfers)).

### The flash

Status: **proposed**

Emulated as step B5 ([hw/design.md, as built](../../hw/design.md#b5-as-built)).

The machine file's `flash` lines ([`flash.h`](../../emulator/components/devices/include/flash.h)) are two chips side
by side, as ROM A and ROM B are: flash offset *o* is byte *o* / 2 of the even chip (*o* even) or the odd one. A
`flash` line's size must be even. Each chip, on its own:

- **reads as memory** until it is sent a command, and takes the commands of
  [chapter 2](02-memory-and-banking.md#reprogramming-the-flash) at its chip addresses `$5555` and `$2AAA`, comparing
  only A0–A14: program a byte, erase a 4 KB sector or the chip, and software ID (the maker's code `$BF` at chip
  address 0 and the part's `$B6` at 1, until `$F0`);
- **keeps its place in a sequence** until its next write: reads don't disturb it, nothing times it out and the part
  has no reset pin, so /RESET leaves it too. A chip left after `$A0` programs the next write, wherever it lands;
- **is busy** for the parts' typical times, counted in bus cycles at the CPU clock: 14 µs a byte (101 cycles at
  7,159,090 Hz), 18 ms a sector (128,864) and 70 ms the chip (501,137). While busy it ignores writes and reads give
  its status (bit 7 the inverse of the byte's, 0 in an erase; bit 6 changing on every read; bits 0–5 the array's).
  The debugger's peek shows the status without changing bit 6.

A change lands in the array when its operation starts, and there is no wear. A write through a window with RO set
never reaches a chip.

`--apple-rom` and `--apple-chars`, and Settings' IIe ROM row, write ROM A's array directly, as a programmer would:
each slot erased, then the file. The row erases and writes image 0 and set 0 only when its IIe ROM's or
characters' bytes change; a Disk II PROM alone, or the same bytes again, keeps them (since B5, proposed). **ROM A's blocks 0–5 are kept through power cycles** (the WebUI's keyswitch, or
Settings' IIe ROM row), whoever wrote them: the row, or the BIOS's [flash update](03-boot.md#the-flash-update) run in
the machine. The kept bytes are copied into each new build; a ROM the row refuses leaves them as they were. They are
in memory only: a page reload, or a new run of the terminal app, starts again from the files, and nothing is
written back to `bios.rom` or any other file. Blocks 6–31 and ROM B are the files' again at every power cycle.

### What isn't there

Status: **proposed**

The design has these, and the emulator doesn't:

- **The TL16C552A:** COM1, COM2 and LPT, and the serial card's DMASEL, INTSTAT, LPTCTL and ID
  ([chapter 9](09-serial-and-via.md#com1-com2-and-lpt)). A 6551 stands in.
- **The VIA's user port,** and its shift register ([chapter 9](09-serial-and-via.md#the-user-port)).
- **The bus spy** ([chapter 3](03-boot.md#the-bus-spy)). `bootsel` and `--load` stand in for it.
- **Turbo,** the CPU at ÷1 ([chapter 11](11-north-bridge-and-rtc.md#turbo)).
- **The USB card's other layouts, a gamepad on the paddles, device events, the FIFO on DMA and a real 558**
  ([chapter 8](08-usb.md#proposed)).
- **The console card's bus-cycle step, its breakpoint on the cycle, EXAM and DEP on the bus, AMODE, the design's
  ISR, its BE lamp and the ESP32's SPI header** ([chapter 12](12-front-panel.md#what-the-emulator-leaves-out)).
- **Apple II mode's long cycle,** every 65th cycle stretched
  ([chapter 13](13-apple-ii-mode.md#the-long-cycle)).
- **The CPU card's word buffer,** which may read or write a whole word of memory and shift its bytes to the 8-bit CPU
  ([hw/design.md](../../hw/design.md#backplane-signals)). The CPU's cycles are bytes; the buffer wouldn't change a
  cycle count.

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
  - The flash's two chips take their commands, with each chip's status and the parts' typical busy times, but a
    change lands in the array when its operation starts, so a power-off part way leaves it done, not half done;
    there is no wear, and no word buffer. ROM A's blocks 0–5 keep what they hold through power cycles but not
    across a page reload or a new emulator run, and are never written back to a file
    ([The flash](#the-flash), [Reprogramming the flash](02-memory-and-banking.md#reprogramming-the-flash)).
- **The boot** ([chapter 3](03-boot.md))
  - /BOOTSEL is the machine file's `northbridge backplane bootsel`, and an image loaded into RAM stands in for the
    bus spy's ([Booting from RAM](03-boot.md#booting-from-ram-bootsel)).
- **Interrupts** ([chapter 4](04-interrupts.md))
  - There is no INTSTAT: `$C322` is the 6551's mirror, so a handler tells the VIA by its IFR
    ([The serial card's INTSTAT](04-interrupts.md#the-serial-cards-intstat)).
  - Pause/Break comes only from the WebUI. The terminal app has no USB keyboard, so there Break comes only from the
    console card ([Pause/Break](04-interrupts.md#pausebreak)).
- **DMA** ([chapter 5](05-dma.md))
  - The SD, video and sound cards' requests reach /DREQ (`drq` on their lines), and channels 0, 1 and 2 serve
    them by their reset CARD; the sound card asks while a transfer into its DATA is open and has room
    ([`$C507`](07-sound.md#reg-c507)). The USB card's request isn't wired, and the serial card, a 6551 standing in,
    has no DMA ([Transfers for a card](05-dma.md#transfers-for-a-card), [`$C40C`](05-dma.md#reg-c40c)).
- **Video** ([chapter 6](06-video.md))
  - A whole sprite table copied by DMA takes about 143 µs, at two cycles a byte, where hw/design.md says about
    72 µs ([Sprites](06-video.md#sprites)). With WIDE (*proposed*, B4), two cycles a word, it takes about 72 µs, but
    nothing in the BIOS or OODOS sets WIDE yet.
  - The card reads the palette from video memory for every line, where the design copies it into the card when
    PALBASE changes ([The palette](06-video.md#the-palette)).
  - The card interrupts on ISR AND IEN, where the design's register table says any pending ISR bit
    ([Interrupts](06-video.md#interrupts)).
  - hw/design.md still labels the boot splash proposed; the emulator has it
    ([The boot splash](06-video.md#the-boot-splash)).
- **Sound** ([chapter 7](07-sound.md))
  - The card's SIDs: two waveforms at once are ANDed, and OSC3 then reads approximately. The DACs' nonlinearity,
    the 6581 filter's distortion and the external input aren't modelled
    ([Playing a .sid file](07-sound.md#playing-a-sid-file)).
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
  - The 6551 sends a byte at once. Its character time from the terminal, parity, framing errors, echo mode and
    modem lines aren't modelled; the overrun and the character time are, on the tests' paced line only ([The stand-in 6551](09-serial-and-via.md#the-stand-in-6551),
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

### Revision 2 of the video card

Status: **proposed**

The emulator has the one card, revision 2 ([chapter 6](06-video.md#revision-2-card-memory)), which
`backplane.machine` fits and the BIOS and OODOS drive. Where it differs from the design:

- **The machine's RAM.** The RAM C line covers bank 8, as the design's RAM card does. While the north bridge's
  AP8 is set the aperture is sent over it ([chapter 2](02-memory-and-banking.md#revision-2-bank-8-the-aperture)),
  and the RAM's bank 8 can't be reached by the CPU or DMA; once AP8 is clear it is ordinary RAM (*proposed*, B7).
- **Apple II mode** reads banks 0 and 1 directly, where the card keeps a snooped copy in its own
  memory ([chapter 13](13-apple-ii-mode.md#revision-2-of-the-video-card)); only the character ROM comes from card
  memory. Nothing outside the card can tell the two apart.

## Known bugs

Status: **emulated**

None that the chapters know of. Where the emulator does less than the design, it is in
[What isn't there](#what-isnt-there) and [What differs](#what-differs).

## What the BIOS leaves

Status: **emulated**

Not bugs, but what BIOS 0.13 does, and 0.14 as well, which a program run from the monitor has to know:

- **Break turns off what the BIOS's IRQ handler can't answer:** the cards' IENs, the clock's TIE, the VIA's IER
  (`$7F`) and the console card's ISR (`$01`), the last two since BIOS 0.11
  ([IRQ and NMI](04-interrupts.md#irq-and-nmi), [Pause/Break](04-interrupts.md#pausebreak)). Under BIOS 0.10 a VIA
  interrupt or a pending panel key kept /IRQ low after Break, so a program written for it clears both itself.
- **Break resets the sound card,** since BIOS 0.21: one write of CTRL's RESET stops every sound on it, abandons an
  open transfer and clears IEN; the store keeps what it holds ([Silencing it](07-sound.md#silencing-it)).
- **Nothing in the BIOS clears the video card's ISR,** so after a reset it soon reads `$03`, and after Break LINE
  and the collision bits are as a program left them ([Interrupts](06-video.md#interrupts)).
- **The SD card's ERR is set under the monitor** after a power-on with a card that has no `/BOOT.BIN`, from the
  BIOS's MOUNT, and ISR's bit 0 is set after any of the BIOS's commands
  ([What the BIOS leaves](10-storage.md#what-the-bios-leaves)).
- **The 6551's receive interrupt is off** under the monitor (CMD `$0B`), and stays off into Apple II mode
  ([The stand-in 6551](09-serial-and-via.md#the-stand-in-6551)).
- **A breakpoint on `$BD1A5` is turned off by every reset,** since BIOS 0.14: it is the ROM
  diagnostics' own P3 address, and the BIOS clears BPCTRL only for it ([Reset](12-front-panel.md#reset)).

## Running a program

Status: **emulated**

The terminal app's commands below are run from `emulator/`, after `cmake --preset release` and
`cmake --build --preset release` ([emulator/README.md](../../emulator/README.md)).

### A ROM on the bare machine

Status: **emulated**

`make -C backplane/bios` builds `bios.rom`, the whole 512 KB of ROM A with the BIOS in its top 16 KB,
`bios.rom.lbl`, its labels, and (since B5) `bios-even.rom` and `bios-odd.rom`, its even and odd bytes, a file for
each of ROM A's two chips. Then
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

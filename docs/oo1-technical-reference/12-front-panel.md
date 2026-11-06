# 12 The front panel

The console card (module E, `$C700`): the front panel's switches and lights, as a program sees them.

The design record, for the reasons: [hw/design.md, "Front Panel"](../../hw/design.md#front-panel),
[hw/console.md](../../hw/console.md).

## Switches and lights

Status: **emulated**

The front panel is in the style of a PDP-11's, on its own card, module E. A CPLD on the card watches every bus
cycle, the CPU's and the DMA controller's, so its lamps, its breakpoint and its latched cycle are exact to the
cycle. hw/design.md's "Front Panel" supersedes the switch list in hw/console.md.

**The switches:**

- **The switch register:** 20 switches. LOAD ADRS takes all 20 as a physical address; DEP takes the low 8 as a
  byte. A program can read them in SWR, for boot options, as on a PDP-11.
- **LOAD ADRS, EXAM, DEP and CONT:** momentary keys ([below](#exam-and-dep)).
- **HALT/ENABLE:** at HALT, the CPU stops.
- **S INST / S BUS CYCLE:** what CONT does while halted.
- **START:** the hard reset, through the north bridge as the reset button is, so it leaves Apple II mode too.
- **The keyswitch, OFF / RUN / LOCK:** OFF is the power. LOCK makes the card ignore the panel's keys, START and its
  HALT switch, as CTRL's LOCK does.

**The lamps:**

- **A0–A19:** the physical address, after the north bridge, so not always the CPU's address.
- **D0–D7:** the data bus, or DISPLAY while CTRL's bit 3 is set.
- **SYNC, RWB, VPB and RDY:** the CPU's lines.

While the machine runs, each lamp glows with the share of bus cycles its line was high, so the lamps show what the
bus is doing as a running PDP-11's did. A held cycle glows longer, and RDY glows with the cycles the CPU was held,
for wait states or DMA. Halted, the lamps freeze on the cycle where the CPU stopped, or on what EXAM or DEP last
touched.

**In the emulator**, the WebUI draws the panel: the switches, the keys, the keyswitch, and POWER, RUN, HALT, SYNC,
RWB, VPB, RDY, BE, IRQ and AMODE lamps. POWER, RUN, HALT, IRQ and AMODE come from the machine, not the card. BE stays
dark: the card doesn't model it. The terminal app has no panel. A program there can still use the card's breakpoint
and CTRL's HALT, which stop the app at its debugger, and going on from the debugger lets the card go too.

### Halting and stepping

Status: **emulated**

Three things halt the CPU: the HALT switch at HALT (unless LOCK is on), CTRL's HALT bit, and a breakpoint that
halts. The emulator runs whole instructions, so it halts between them, and while it is halted nothing runs: the
emulated time stands still.

**CONT** while halted clears a breakpoint's halt and CTRL's HALT. With the HALT switch at ENABLE the CPU runs on.
With it at HALT, CONT runs one instruction and the CPU halts again, so each CONT is a step. CONT that leaves a fetch
breakpoint runs the instruction there, rather than stopping on it again. STATUS's HIT stays set until it is cleared.

S BUS CYCLE is read in PANEL, but in the emulator CONT steps a whole instruction with it too
([below](#what-the-emulator-leaves-out)).

A program can halt itself for the panel by setting CTRL's HALT. It stops before its next instruction, and goes on
when CONT is pressed.

### EXAM and DEP

Status: **emulated**

- **LOAD ADRS** copies the switch register into PADDR, the panel's address. It works halted or not.
- **EXAM**, while halted, shows the byte at PADDR on the lamps.
- **DEP**, while halted, writes the switch register's low 8 bits to PADDR, and shows them.
- **Pressed again,** EXAM or DEP first moves PADDR on by one, so repeated presses step through memory. LOAD ADRS,
  the other of EXAM and DEP, or a write to PADDR starts again without moving on. CONT doesn't, so EXAM, CONT, EXAM
  moves on by one.

PADDR is a physical address in RAM, so EXAM and DEP don't depend on the windows. They reach RAM only: not the flash,
and not the cards' registers. In the emulator they take no bus cycle, since time stands still while halted; on the
card they take the bus with BE for one cycle.

### The breakpoint

Status: **emulated**

BPADDR holds a 20-bit physical address, and BPCTRL says which cycles at it match and what then happens. The
comparator checks the A0–A19 of every bus cycle, the DMA controller's included.

- **A fetch** (BPCTRL bit 1) matches an opcode fetch. A fetch that halts stops the CPU before that instruction, with
  PC on it: LADDR is BPADDR, LDATA the opcode and LSTAT `$03`, SYNC and a read.
- **A read** (bit 2) matches any other read, and **a write** (bit 3) a write. A read or write that halts latches its
  exact cycle in LADDR, LDATA and LSTAT, but the CPU stops after the instruction that made it.
- **With NMI** (bit 4), nothing halts. Each match sets STATUS's HIT, latches its cycle, and pulls /NMI after the
  instruction. A fetch match lets the instruction run first.

The latch takes every stop. However a halt ends (CONT, a step, the HALT switch turned off as the WebUI's Run does,
CTRL's HALT cleared), the CPU running again lets the next stop latch. While the CPU runs, LADDR, LDATA and LSTAT keep
the last stop's cycle until the next one.

HIT stays set until a 1 is written to it. Under the BIOS, the NMI is Break: it enters the monitor
([chapter 4](04-interrupts.md#pausebreak)), which leaves BPADDR and BPCTRL alone, so the breakpoint stays armed. Turn
it off with BPCTRL's bit 0.

A register access puts the CPU's own address on A0–A19: `$0C000–$0C7FF`. Those are also RAM block 3's first 2 KB,
so a breakpoint there matches both.

### The panel's interrupt

Status: **emulated**

With IEN's bit 0 set, a key pressed, on the panel or by a write to KEYS, sets ISR's bit 0. The card pulls /IRQ while
ISR's bit 0 is set, on IRQH bit 6 ([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)). Writing a 1 to
ISR's bit 0 clears it.

Clearing IEN stops new keys from setting ISR, but doesn't clear one already pending: the card keeps pulling /IRQ
until ISR is cleared. The BIOS's Break writes `$00` to IEN, and leaves ISR as it was.

### Reset

Status: **emulated**

Any /RESET, and START, put the card back: CTRL, IEN, ISR and KEYS are cleared, and every halt but the HALT switch's
is let go. BPADDR, BPCTRL, STATUS's HIT, PADDR, DISPLAY and the latched cycle are kept. So a breakpoint set before
START catches an address in the boot, and START with the HALT switch at HALT stops at the reset vector's first
instruction, to step from there. LOCK ignores START.

The BIOS leaves the card as reset leaves it, and Break turns IEN off.

In Apple II mode the card's registers are out of reach, with the rest of the I/O hole
([chapter 2](02-memory-and-banking.md#the-io-hole)), but the panel still works: halting, stepping, EXAM, DEP, the
breakpoint and the lamps don't need software.

### What the emulator leaves out

Status: **proposed**

hw/design.md has these, and the emulator doesn't yet:

- **S BUS CYCLE** steps one bus cycle on the card, by holding RDY. The emulator steps an instruction.
- **A read or write breakpoint** halts on its own cycle on the card, since RDY holds a W65C02S on any cycle. The
  emulator halts after the instruction.
- **EXAM and DEP** take one bus cycle with BE.
- **LSTAT's AMODE bit** (bit 4) and **the card's BE lamp.** The emulator's LSTAT bit 4 always reads 0.
- **The card hides its registers in Apple II mode** by AMODE. In the emulator the north bridge's Apple map keeps them
  out of reach instead, which a program can't tell apart.
- **ISR** in the design is the key's pending bit, with IEN choosing whether it interrupts, as the USB card's ISR is
  set whatever its IEN says. The emulator sets it only when IEN was already set.
- **The ESP32's SPI header.** The ESP32 plugs into a small header on the card that gives it the same registers. It
  can halt, step and read the latched cycle even with the bus hung, and mirror the panel over Wi-Fi. The WebUI reads
  the registers as that path would, without side effects, so its reads don't clear KEYS. It never writes KEYS: its
  keys are the panel's own, so LOCK stops them. It writes only BPCTRL, BPADDR and STATUS, for its breakpoints.

## Registers

Status: **emulated**

Offsets with no register (`$C707`, `$C70D–$C70F`, `$C71A–$C77E`) read `$FF`, and writes to them are lost.

<a id="reg-c700"></a>
### $C700 CONSOLE_SWR: the switch register

#### Equates

`CONSOLE`, `CONSOLE_SWR`

#### Module

E, the console card

#### Access

R, 3 bytes, low byte first. Writes are ignored.

#### Bits

All: the 20 switches, a physical address: A0–A7 in `$C700`, A8–A15 in `$C701`, and A16–A19 in `$C702`'s low four
bits. `$C702`'s high four read 0.

#### Reset

Not changed: these are the switches.

#### Status

**emulated**

#### Notes

They read the switches with LOCK on too: LOCK stops LOAD ADRS and DEP from using them, not a program from reading
them.

<a id="reg-c703"></a>
### $C703 CONSOLE_PANEL: the HALT and S BUS CYCLE switches

#### Equates

`CONSOLE_PANEL`

#### Module

E, the console card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–2 | | 0 |
| 1 | BUSCYCLE | 1: S BUS CYCLE; 0: S INST |
| 0 | HALT | 1: the HALT/ENABLE switch is at HALT |

#### Reset

Not changed: these are the switches.

#### Status

**emulated**

#### Notes

Bit 0 shows the switch with LOCK on too, though it halts nothing then. hw/design.md has S BUS CYCLE step a bus
cycle; the emulator steps an instruction ([above](#halting-and-stepping)).

<a id="reg-c704"></a>
### $C704 CONSOLE_KEYS: the keys pressed

#### Equates

`CONSOLE_KEYS`

#### Module

E, the console card

#### Access

R/W. A read gives the keys pressed since the last read, and clears them. A write presses the keys whose bits are 1,
with LOCK on or not.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | 0 |
| 3 | CONT | CONT |
| 2 | DEP | DEP |
| 1 | EXAM | EXAM |
| 0 | LOADADRS | LOAD ADRS |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

A write is the remote panel's way in, the ESP32's in the design. From a running program only LOAD ADRS does
anything: EXAM, DEP and CONT work while halted, and a running program isn't. Every key pressed, by either way, sets
ISR if IEN is set.

<a id="reg-c705"></a>
### $C705 CONSOLE_DISPLAY: the data lamps' byte

#### Equates

`CONSOLE_DISPLAY`

#### Module

E, the console card

#### Access

W. A read gives `$FF`.

#### Bits

All: the byte the data lamps show while CTRL's bit 3 is set: a progress or error code with no screen, as a
PDP-11's display register.

#### Reset

Kept through /RESET. `$00` at power-on.

#### Status

**emulated**

<a id="reg-c706"></a>
### $C706 CONSOLE_CTRL: the card's control

#### Equates

`CONSOLE_CTRL`

#### Module

E, the console card

#### Access

R/W. Setting HALT halts the CPU before its next instruction.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | 0 |
| 3 | SHOW | 1: the data lamps show DISPLAY instead of the bus |
| 2 | LOCK | 1: the panel's keys, START and HALT switch are ignored, as with the keyswitch at LOCK |
| 1 | | 0 |
| 0 | HALT | 1: halt, as the HALT switch does. CONT clears it |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

LOCK doesn't stop writes to KEYS. The keyswitch at LOCK does what bit 2 does, and doesn't show in this register.

<a id="reg-c708"></a>
### $C708 CONSOLE_BPADDR: the breakpoint's address

#### Equates

`CONSOLE_BPADDR`

#### Module

E, the console card

#### Access

R/W, 3 bytes, low byte first.

#### Bits

All: a 20-bit physical address. `$C70A` holds A16–A19 in its low four bits; its high four are dropped and read 0.

#### Reset

Kept through /RESET and START. `$00 $00 $00` at power-on.

#### Status

**emulated**

#### Notes

Write it with BPCTRL's enable off, or a half-written address can match.

<a id="reg-c70b"></a>
### $C70B CONSOLE_BPCTRL: the breakpoint's control

#### Equates

`CONSOLE_BPCTRL`

#### Module

E, the console card

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | | 0: dropped |
| 4 | NMI | 1: a match pulls /NMI and nothing halts; 0: a match halts |
| 3 | WRITE | 1: a write at BPADDR matches |
| 2 | READ | 1: a read at BPADDR that isn't an opcode fetch matches |
| 1 | FETCH | 1: an opcode fetch at BPADDR matches |
| 0 | ENABLE | 1: the breakpoint is armed |

#### Reset

Kept through /RESET and START, so a breakpoint can catch the boot. `$00` at power-on.

#### Status

**emulated**

#### Notes

A fetch that halts stops before the instruction; a read or write that halts, after it, though LADDR has the exact
cycle ([above](#the-breakpoint)). hw/design.md has the card halt on the read's or write's own cycle. Break leaves
BPCTRL alone.

<a id="reg-c70c"></a>
### $C70C CONSOLE_STATUS: the card's status

#### Equates

`CONSOLE_STATUS`

#### Module

E, the console card

#### Access

R/W. A write with bit 1 set clears HIT; the other bits are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–3 | | 0 |
| 2 | KEY | 1: a key has been pressed since KEYS was last read |
| 1 | HIT | 1: the breakpoint matched. Stays until a 1 is written here |
| 0 | HALTED | 1: halted, by the switch, CTRL or the breakpoint |

#### Reset

HIT is kept through /RESET and START; KEY is cleared, and HALTED follows the HALT switch. `$00` at power-on.

#### Status

**emulated**

#### Notes

A running program reads HALTED as 0, except in the instruction that CONT lets through with the HALT switch at
HALT. HALTED is for the remote panel.

<a id="reg-c710"></a>
### $C710 CONSOLE_LADDR: the latched cycle's address

#### Equates

`CONSOLE_LADDR`

#### Module

E, the console card

#### Access

R, 3 bytes, low byte first. Writes are ignored.

#### Bits

All: the latched cycle's A0–A19. `$C712`'s high four bits read 0.

#### Reset

Kept through /RESET. `$00 $00 $00` at power-on.

#### Status

**emulated**

#### Notes

The latch takes a cycle when the CPU halts (the breakpoint's cycle, or the last cycle before the stop) and holds it
until the next stop: the CPU running again, however the halt ended, lets the next stop latch
([the breakpoint](#the-breakpoint)).
A breakpoint with NMI latches each matching cycle, for a debugger on the machine to read.

<a id="reg-c713"></a>
### $C713 CONSOLE_LDATA: the latched cycle's data

#### Equates

`CONSOLE_LDATA`

#### Module

E, the console card

#### Access

R. Writes are ignored.

#### Bits

All: the latched cycle's D0–D7. For a fetch breakpoint that halts, the opcode.

#### Reset

Kept through /RESET. `$00` at power-on.

#### Status

**emulated**

<a id="reg-c714"></a>
### $C714 CONSOLE_LSTAT: the latched cycle's signals

#### Equates

`CONSOLE_LSTAT`

#### Module

E, the console card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | | 0 |
| 4 | AMODE | 0, always: not modelled |
| 3 | BE | 1: the DMA controller's cycle, with the CPU off the bus |
| 2 | VPB | 1: a vector read |
| 1 | RWB | 1: a read; 0: a write |
| 0 | SYNC | 1: an opcode fetch |

#### Reset

Kept through /RESET. `$00` at power-on.

#### Status

**emulated**

#### Notes

hw/design.md has AMODE here, 1 for a cycle in Apple II mode; the emulator's bit 4 reads 0.

<a id="reg-c715"></a>
### $C715 CONSOLE_PADDR: the panel's address

#### Equates

`CONSOLE_PADDR`

#### Module

E, the console card

#### Access

R/W, 3 bytes, low byte first. A write sets it, and the next EXAM or DEP uses it without moving on first.

#### Bits

All: a 20-bit physical address in RAM, which LOAD ADRS sets from the switches and repeated EXAM and DEP presses
step. `$C717`'s high four bits are dropped and read 0.

#### Reset

Kept through /RESET. `$00 $00 $00` at power-on.

#### Status

**emulated**

<a id="reg-c718"></a>
### $C718 CONSOLE_IEN: the panel's interrupt enable

#### Equates

`CONSOLE_IEN`

#### Module

E, the console card

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–1 | | 0 |
| 0 | KEY | 1: a key pressed sets ISR's bit 0 |

#### Reset

`$00`. The BIOS's Break writes `$00` too.

#### Status

**emulated**

#### Notes

Clearing it doesn't clear a pending ISR ([above](#the-panels-interrupt)).

<a id="reg-c719"></a>
### $C719 CONSOLE_ISR: the panel's pending interrupt

#### Equates

`CONSOLE_ISR`

#### Module

E, the console card

#### Access

R/W. A write clears bit 0 if bit 0 of the byte is 1.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–1 | | 0 |
| 0 | KEY | 1: a key was pressed with IEN set. The card pulls IRQH bit 6 while it is set |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

The emulator sets it only if IEN was set when the key was pressed, and keeps it set, pulling /IRQ, after IEN is
cleared. hw/design.md gives ISR as the pending bit and IEN as the choice to interrupt, which reads as the USB card's
rule: pending on every key press.

<a id="reg-c77f"></a>
### $C77F CONSOLE_ID: the card's ID

#### Equates

`CONSOLE_ID`

#### Module

E, the console card

#### Access

R. Writes are ignored.

#### Bits

All: `$E1`, `CONSOLE_ID_V1`: module E, revision 1.

#### Reset

`$E1`.

#### Status

**emulated**

#### Notes

The BIOS's Break reads it before it writes IEN, and OODOS lists the card at boot by it
([chapter 1](01-overview.md#slots-and-module-ids)).

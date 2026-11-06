# 9 Serial and the VIA

The serial card (module 6, `$C300`): COM1, COM2 and LPT on a TL16C552A, the stand-in 6551 until that card exists,
and the W65C22S VIA at `$C340` with the two joystick ports.

The design record, for the reasons: [hw/design.md, "The VIA and the joystick
ports"](../../hw/design.md#the-via-and-the-joystick-ports).

## COM1, COM2 and LPT

Status: **proposed**

The design's serial card has a TI TL16C552A: two 16550-style UARTs with 16-byte FIFOs, and a bidirectional
parallel port. They are COM1, COM2 and LPT, as on a PC. A 1.8432 MHz crystal gives the PC's divisors, so 1 is
115,200 baud and 12 is 9,600. A CPLD turns the 65C02's bus into the chip's /IOR and /IOW, and adds the card's own
registers ([hw/design.md, "Input and Ports"](../../hw/design.md#input-and-ports)).

None of it is emulated. Today `$C300–$C37F` is the 6551 stand-in ([below](#the-stand-in-6551)) and the VIA, and
none of the addresses in this section answer as described here.

The design's block:

| Range | What |
|---|---|
| `$C300–$C307` | COM1's eight registers, in the PC's order |
| `$C308–$C30F` | COM2's, the same |
| `$C310–$C312` | LPT: data, status and control, as at a PC's LPT base |
| `$C320–$C323` | the card's registers |
| `$C340–$C34F` | the VIA, as emulated now ([below](#the-via-and-the-joystick-ports)) |
| `$C37F` | the card's ID |

The chip's registers keep the PC's layout, so a 16550 or LPT driver needs only its base address changed. The card's
four registers:

- **DMASEL1** (`$C320`) chooses what drives DMA channel 1's DRQ: nothing, COM1 or COM2 receiving or sending, or LPT
  sending. **DMASEL3** (`$C321`) does the same for channel 3, for a second stream.
- **INTSTAT** (`$C322`) says which part is interrupting: bit 0 COM1, bit 1 COM2, bit 2 LPT, bit 3 the VIA. Any of
  them pulls IRQL bit 6. The enables stay in the chip's own registers and the VIA's
  ([chapter 4](04-interrupts.md#the-serial-cards-intstat)).
- **LPTCTL** (`$C323`) bit 0, AUTOSTROBE: with DMA, the card strobes the printer for each byte itself, since the
  chip's parallel port has no DMA request of its own.
- **ID** (`$C37F`) is the card's type and revision. Today `$C37F` is the 6551's CTRL, through its mirror, so a
  program can't find the serial card by its ID ([chapter 1](01-overview.md#slots-and-module-ids)).

**Serial DMA.** The UARTs' ready outputs drive the DRQ that DMASEL picks, and the CPLD selects the chosen data
register on /DACK, so the transfer is fly-by ([chapter 5](05-dma.md#transfers-for-a-card)). A receive ring buffer is
channel 1 in AUTO mode, with the program reading the channel's current address to see how far it has got.

**Timing.** The chip needs its strobes longer than PHI2's high time, so every access to it takes one wait state, and
accesses back to back take another. The VIA takes none. In Apple II mode the whole block is hidden.

## The stand-in 6551

Status: **emulated**

Until the TL16C552A card exists, a W65C51N ACIA at `$C300–$C303` is the serial line: the terminal app's terminal,
or the WebUI's Terminal. It decodes only two address lines, so it answers through the whole block, every four bytes,
except at `$C340–$C34F`, where the VIA answers over it. `$C322`, the design's INTSTAT, is its CMD; `$C37F`, the
card's ID, is its CTRL. Use `$C300–$C303` only.

What the emulator models:

- **Sending.** A byte written to DATA goes to the host at once, if CMD has DTR on and the transmitter on. Otherwise
  it is dropped.
- **The stuck transmit flag.** STATUS's TDRE (bit 4) always reads 1, as on the W65C51N, so a program can't tell when
  the transmitter is free. It waits out a character time after each byte instead. The emulator never needs the wait,
  since bytes leave at once, but the real part would, so the BIOS and OODOS keep it.
- **Receiving.** A byte from the host sets RDRF (STATUS bit 3), and, with the receive interrupt on, STATUS's IRQ
  (bit 7). Reading DATA clears RDRF; reading STATUS clears IRQ. While RDRF is set the host holds its next byte back,
  so nothing is ever overrun: a paste into the terminal arrives whole. With DTR off, keys typed at the terminal are
  lost. Enter arrives as CR, `$0D`.
- **Interrupts.** Only receiving interrupts, on IRQL bit 6, which the VIA shares. The line stays low until STATUS is
  read ([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)).

Not modelled: the baud rate and the character's time, parity, framing and overrun errors, echo mode, and the modem
lines, which read as asserted. CTRL and CMD's upper bits are stored and read back, and change nothing.

**/RESET doesn't reset it.** The 6551 is the one device in the emulator that doesn't hear /RESET: the reset button,
START, Ctrl-Reset and entering Apple II mode leave its CMD and CTRL as they were. Only a power-on sets them afresh,
to `$02` and `$00`. [Chapter 4](04-interrupts.md#in-apple-ii-mode) has what that means for its interrupt in Apple II
mode.

**The BIOS** writes `$1F` to CTRL (8 data bits, 1 stop bit, 19,200 baud on the chip) and `$0B` to CMD (DTR on, the
receive interrupt off, the transmitter on) at boot, and again each time the monitor starts, after BRK and Break too.
So under the monitor CMD is `$0B` and CTRL `$1F`. It polls STATUS's RDRF for keys, and after each byte it sends it
waits 3,840 cycles, a little over a character's 3,730 at 19,200 baud and 7.16 MHz. ECHO sends a byte so:

<!-- listing: ../../backplane/bios/wozmon.s:185-201 -->
```
; The W65C51N's transmit-empty flag is stuck, so wait out one character
; instead: 10 bits at 19200 baud is 521 us, 3,730 cycles at 7.16 MHz. Three
; passes of 256 x 5 cycles is 3,840. X is kept for the callers.
ECHO:
                JSR     VPUTC          ; The VGA screen (keeps A, X and Y).
                PHA                    ; Save A.
                PHX                    ; Save X.
                STA     ACIA_DATA      ; Output character.
                LDX     #$03           ; Outer passes.
TXDELAY:        LDA     #$00           ; 256 inner loops.
TXINNER:        DEC                    ; Decrement A.
                BNE     TXINNER        ; Until A gets to 0.
                DEX
                BNE     TXDELAY
                PLX                    ; Restore X.
                PLA                    ; Restore A.
                RTS                    ; Return.
```

OODOS's BIOS does the same: its TTY device writes the same two values when it is initialized, and waits out each
character the same way ([hw/oodos.md, "BIOS"](../../hw/oodos.md#bios)).

The wait is in cycles, so at a slower clock it is longer in time and still enough.

## The VIA and the joystick ports

Status: **emulated**

A WDC W65C22S VIA sits on the serial card at `$C340–$C34F`: two 8-bit ports, two 16-bit timers, a shift register
and four control lines. On the OO-1 its ports are the two joystick ports, and its timers are the machine's only
timers that interrupt at any rate. It runs at the CPU's clock with no wait states. Its interrupt pulls IRQL bit 6,
which it shares with the 6551 ([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)).

The registers are in WDC's order: the ports and their directions at `$0–$3`, timer 1 at `$4–$7`, timer 2 at
`$8–$9`, then SR, ACR, PCR, IFR and IER, and port A again at `$F` ([Registers](#registers)).

**Under OODOS,** the VIA's ACR, IER and IFR are OODOS's ([AGENTS.md](../../AGENTS.md)). OODOS 0.3 doesn't use the
VIA yet. The plan, with timer 1 as OODOS's tick and timer 2, the ports and the shift register the program's, is in
[hw/oolib.md, "The VIA is shared"](../../hw/oolib.md#the-via-is-shared).

### The joysticks

Status: **emulated**

Joystick 1 is port A's bits 0–4, joystick 2 port B's: up, down, left, right and fire, from bit 0. Each switch
closes to ground, so **a bit reads 0 while it is pressed**. The other lines, bits 5–7 of both ports, read 1. With
nothing pressed, or no stick plugged in, a port reads `$FF`.

A game reads a stick with one load: ORA (`$C341`) for joystick 1 or ORB (`$C340`) for joystick 2, then `AND #$1F`,
which gives `$1F` with nothing pressed. Leave both DDRs at `$00`, every line an input, as reset leaves them. A
joystick line set as an output fights the switch: port A then reads 0 while it is pressed, whatever ORA holds, and
port B reads what ORB holds.

In the WebUI, gamepads drive the sticks, and the keyboard can too once Settings turns it on. The terminal app has no
joystick; its debugger's `joy 1 uf` holds joystick 1 up with fire pressed, `joy 1 -` lets go, and `joy` shows both.

### The timers

Status: **emulated**

Both timers count down once a cycle of the CPU's clock: 7.16 MHz in native mode, a count every 139.7 ns. They count
cycles, so at the emulator's slower clocks a count takes longer in time and the same in cycles.

**Timer 1** has a 16-bit counter and a 16-bit latch.

- Write the latch's low byte to T1L-L (`$C346`) or T1C-L (`$C344`), which are the same on a write. Writing T1C-H
  (`$C345`) puts the high byte in the latch, copies the latch into the counter, clears the timer's flag and starts
  it. Writing T1L-H (`$C347`) sets the latch's high byte and clears the flag, and starts nothing.
- With N in the counter, the flag (IFR bit 6) is set N + 1.5 cycles after the write to T1C-H: a read N + 2 cycles
  after it is the first to see it.
- **One-shot** (ACR bit 6 clear) sets the flag once. The counter rolls over to `$FFFF` and counts on, and sets the
  flag again only after the next write to T1C-H.
- **Free-running** (ACR bit 6 set) reloads the counter from the latch at each time-out and sets the flag each time:
  a period of N + 2 cycles. A new latch takes effect at the next reload. A 1 kHz tick is a latch of 7,157, 7,159
  cycles.
- **PB7** (ACR bit 7): port B's bit 7 reads timer 1's output instead of ORB, whatever DDRB says. In one-shot mode it
  is low from the write to T1C-H until the time-out; in free-running mode it changes at each time-out, a square wave.
  On this card PB7 goes nowhere, so a program sees it only in ORB.
- Reading T1C-L clears the flag. Reading T1C-H, T1L-L or T1L-H clears nothing.

**Timer 2** is one-shot only, with an 8-bit latch for its low byte.

- Write T2C-L (`$C348`) with the low byte, then T2C-H (`$C349`) with the high: that loads the counter, clears the
  flag (IFR bit 5) and starts it. The flag comes once, with timer 1's one-shot timing.
- Reading T2C-L clears the flag; reading T2C-H doesn't.
- **Counting pulses** (ACR bit 5): the counter stops counting cycles and counts falling edges on PB6 instead, and
  sets the flag after N of them. On this card PB6 is held high and nothing pulses it, so in this mode timer 2
  stands still.

The longest one-shot is 65,535 + 2 cycles, about 9.2 ms.

### The shift register and the control lines

Status: **emulated**

**The shift register isn't modelled.** SR is a byte that reads back what was written, and its flag (IFR bit 2) is
never set. ACR's bits 2–4, its mode, are stored and do nothing.

**CA1, CA2, CB1 and CB2 aren't connected.** PCR is stored, and its edge and input settings are modelled, but
nothing on the card drives the lines, so their flags (IFR bits 0, 1, 3 and 4) are never set. Reading or writing ORA
or ORB clears the CA or CB flags, as on the chip, and `$C34F` reads and writes port A without clearing them.

ACR's bits 0–1, latching the ports' inputs, are stored and do nothing: a port read always reads the lines as they
are.

So on this card only the two timers ever set a flag.

### The VIA's interrupts

Status: **emulated**

The VIA pulls IRQL bit 6 while a flag in IFR is set and its bit in IER is set too. IFR's bit 7 reads 1 then, so a
handler that finds IRQL bit 6 low tests ACIA_STATUS's bit 7 and VIA_IFR's bit 7 to tell the 6551 from the VIA. It
clears a flag by the access that clears it, or by writing a 1 to its IFR bit.

IER is written with bit 7 saying what to do: 1 sets the IER bits that are 1 in the byte, 0 clears them. `$7F` turns
every VIA interrupt off; `$C0` turns on timer 1's.

**The BIOS doesn't touch the VIA,** and Break leaves it, its IER too. A VIA interrupt a program left on survives
Break, and interrupts the next program that clears I ([chapter 4](04-interrupts.md#irq-and-nmi)). Turn the VIA's
interrupts off (`$7F` to VIA_IER) before leaving.

### Reset

Status: **emulated**

/RESET clears ORA, ORB, both DDRs, ACR, PCR, IFR and IER: every line an input, timer 1 one-shot with PB7 a port
line, no flags and no enables. The timers' counters and latches, and SR, keep their values, and the counters run on,
but neither timer sets its flag again until a program loads it. The reset button, START, Ctrl-Reset and entering
Apple II mode all pulse /RESET. At power-on the counters and latches are `$FFFF` and SR is `$00`.

Since the BIOS never writes the VIA, that is how it is under the monitor after a reset, and Break changes nothing.

In Apple II mode the VIA is out of reach, with the rest of the I/O hole
([chapter 2](02-memory-and-banking.md#the-io-hole)). Entering the mode cleared its IER, so it can't interrupt there.

### The user port

Status: **proposed**

The design brings the VIA's spare lines out to a 2×10 header: PA5–PA7 and PB5–PB7, with PB6 as timer 2's pulse
input and PB7 as timer 1's output, and CA1, CA2, CB1 and CB2 for handshakes and the shift register. It is for
hobby hardware: an LCD, a sensor, a second keyboard. hw/oolib.md has a call to claim it, so that only one program
drives it at a time ([hw/oolib.md, "The user port"](../../hw/oolib.md#the-user-port)).

In the emulator the joysticks take both ports, and nothing else can be put on the VIA. The user port's lines read
1 and go nowhere.

## Registers

Status: **emulated**

<a id="reg-c300"></a>
### $C300 ACIA_DATA: the 6551's received and sent byte

#### Equates

`ACIA`, `ACIA_DATA`

#### Module

6, the serial card

#### Access

R/W. A read gives the last byte received and clears STATUS's RDRF. A write sends the byte, if CMD has DTR and the
transmitter on; otherwise the byte is dropped.

#### Bits

All: the byte.

#### Reset

`$00` at power-on. /RESET doesn't change it.

#### Status

**emulated**

#### Notes

Wait a character time after each byte written: STATUS's TDRE can't say when the transmitter is free
([above](#the-stand-in-6551)). Mirrored every four bytes through `$C304–$C33F` and `$C350–$C37F`.

<a id="reg-c301"></a>
### $C301 ACIA_STATUS: the 6551's status, and its programmed reset

#### Equates

`ACIA_STATUS`

#### Module

6, the serial card

#### Access

R/W. A read clears IRQ (bit 7). A write, of any value, is a programmed reset: CMD's bits 0–4 are cleared, which
turns off DTR and the transmitter, and IRQ is cleared. CTRL, RDRF and the received byte are kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | IRQ | 1: a byte was received with the receive interrupt on. Reading STATUS clears it |
| 6 | DSR | 0, always: asserted |
| 5 | DCD | 0, always: asserted |
| 4 | TDRE | 1, always: the W65C51N's stuck flag |
| 3 | RDRF | 1: a received byte is waiting in DATA. Reading DATA clears it |
| 2 | OVRN | 0, always: nothing is overrun |
| 1 | FE | 0, always: no framing errors |
| 0 | PE | 0, always: no parity errors |

#### Reset

`$10` at power-on. /RESET doesn't change it. Under the monitor it reads `$10`, or `$18` with a key waiting.

#### Status

**emulated**

#### Notes

The 6551 pulls IRQL bit 6 while IRQ is set, so a handler reads STATUS once, keeps the value, and then reads DATA if
RDRF is set. The BIOS's key routine tests bit 3 and nothing else:

<!-- listing: ../../backplane/bios/input.s:27-33 -->
```
KEYHIT:
                LDA     ACIA_STATUS
                AND     #$08           ; a serial key?
                BEQ     @usb
                LDA     ACIA_DATA
                SEC
                RTS
```

<a id="reg-c302"></a>
### $C302 ACIA_CMD: the 6551's command register

#### Equates

`ACIA_CMD`

#### Module

6, the serial card

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | PARITY | parity, on the chip. Stored, not modelled |
| 4 | ECHO | echo mode, on the chip. Stored, not modelled |
| 3–2 | TX | the transmitter: `%00` off, so bytes written to DATA are dropped; any other value on. On the chip it also sets RTS, and `%01` asks for a transmit interrupt, which the W65C51N doesn't give |
| 1 | IRQOFF | 1: the receive interrupt is off. 0: a received byte sets STATUS's IRQ |
| 0 | DTR | 1: the receiver and transmitter work. 0: nothing is sent or received |

#### Reset

`$02` at power-on: DTR off, the receive interrupt off. /RESET doesn't change it. Under the monitor it is `$0B`.

#### Status

**emulated**

#### Notes

The BIOS and OODOS write `$0B`: DTR on, the receive interrupt off, the transmitter on, no parity, no echo. A program
that turns the receive interrupt on brings its own IRQ handler ([chapter 4](04-interrupts.md#irq-and-nmi)), and turns
it off again before it leaves. The 6551 doesn't hear /RESET: the BIOS writes `$0B` again when it boots and when the
monitor starts, but the reset that enters Apple II mode runs the IIe's ROM, which leaves CMD as it was.

<a id="reg-c303"></a>
### $C303 ACIA_CTRL: the 6551's control register

#### Equates

`ACIA_CTRL`

#### Module

6, the serial card

#### Access

R/W. Stored and read back; the emulator has no baud rate, so it changes nothing.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | SBN | the stop bits, on the chip: 0 one, 1 two |
| 6–5 | WL | the data bits, on the chip: `%00` 8, `%01` 7, `%10` 6, `%11` 5 |
| 4 | RCS | the receiver's clock, on the chip: 1 the baud rate generator |
| 3–0 | SBR | the baud rate, on the chip: `%1111` 19,200 |

#### Reset

`$00` at power-on. /RESET doesn't change it. Under the monitor it is `$1F`.

#### Status

**emulated**

#### Notes

The BIOS and OODOS write `$1F`: 8 data bits, 1 stop bit, 19,200 baud. Its mirror at `$C37F` is where the design's
serial card has its ID ([above](#com1-com2-and-lpt)), so `$C37F` reads `$1F` under the monitor, not an ID.

<a id="reg-c340"></a>
### $C340 VIA_ORB: port B, joystick 2

#### Equates

`VIA`, `VIA_ORB`

#### Module

6, the serial card

#### Access

R/W. A read gives ORB's bits where DDRB has outputs and the lines where it has inputs; with ACR bit 7 set, bit 7 is
timer 1's output. A write sets ORB. Either clears IFR's CB1 flag, and its CB2 flag unless PCR has CB2 as an
independent input.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | PB7–PB5 | 1: the user port's lines, not connected. PB7 is timer 1's output with ACR bit 7 |
| 4 | FIRE | joystick 2's fire: 0 pressed |
| 3 | RIGHT | 0: pressed |
| 2 | LEFT | 0: pressed |
| 1 | DOWN | 0: pressed |
| 0 | UP | 0: pressed |

#### Reset

ORB `$00`. With DDRB `$00` a read gives the lines: `$FF` with nothing pressed.

#### Status

**emulated**

#### Notes

`AND #$1F` leaves the stick ([above](#the-joysticks)).

<a id="reg-c341"></a>
### $C341 VIA_ORA: port A, joystick 1

#### Equates

`VIA_ORA`

#### Module

6, the serial card

#### Access

R/W. A read gives the lines, outputs included: an output bit reads what ORA drives, or 0 while its switch is
pressed. A write sets ORA. Either clears IFR's CA1 flag, and its CA2 flag unless PCR has CA2 as an independent
input.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | PA7–PA5 | 1: the user port's lines, not connected |
| 4 | FIRE | joystick 1's fire: 0 pressed |
| 3 | RIGHT | 0: pressed |
| 2 | LEFT | 0: pressed |
| 1 | DOWN | 0: pressed |
| 0 | UP | 0: pressed |

#### Reset

ORA `$00`. With DDRA `$00` a read gives the lines: `$FF` with nothing pressed.

#### Status

**emulated**

#### Notes

`LDA VIA_ORA`, `AND #$1F` reads joystick 1. VIA_ORA_NH (`$C34F`) reads the same without touching the flags.

<a id="reg-c342"></a>
### $C342 VIA_DDRB: port B's directions

#### Equates

`VIA_DDRB`

#### Module

6, the serial card

#### Access

R/W.

#### Bits

All: one bit for each of port B's lines. 1: an output, 0: an input.

#### Reset

`$00`: every line an input.

#### Status

**emulated**

#### Notes

Leave bits 0–4 at 0: they are joystick 2's switches ([above](#the-joysticks)). With ACR bit 7 set, PB7 is timer 1's
whatever bit 7 says.

<a id="reg-c343"></a>
### $C343 VIA_DDRA: port A's directions

#### Equates

`VIA_DDRA`

#### Module

6, the serial card

#### Access

R/W.

#### Bits

All: one bit for each of port A's lines. 1: an output, 0: an input.

#### Reset

`$00`: every line an input.

#### Status

**emulated**

#### Notes

Leave bits 0–4 at 0: they are joystick 1's switches.

<a id="reg-c344"></a>
### $C344 VIA_T1CL: timer 1's counter, low byte

#### Equates

`VIA_T1CL`

#### Module

6, the serial card

#### Access

R/W. A read gives the counter's low byte and clears timer 1's flag (IFR bit 6). A write sets the latch's low byte,
as T1L-L does, and starts nothing.

#### Bits

All: the low byte.

#### Reset

Kept through /RESET: the counter runs on. At power-on it is `$FFFF`, counting down.

#### Status

**emulated**

#### Notes

See [the timers](#the-timers).

<a id="reg-c345"></a>
### $C345 VIA_T1CH: timer 1's counter, high byte

#### Equates

`VIA_T1CH`

#### Module

6, the serial card

#### Access

R/W. A read gives the counter's high byte, and clears nothing. A write sets the latch's high byte, copies the latch
into the counter, clears timer 1's flag and starts the timer; with ACR bit 7, PB7 goes low.

#### Bits

All: the high byte.

#### Reset

Kept through /RESET: the counter runs on.

#### Status

**emulated**

#### Notes

Write the low byte first, then this one. The flag comes N + 1.5 cycles after this write, N being the 16-bit value
loaded.

<a id="reg-c346"></a>
### $C346 VIA_T1LL: timer 1's latch, low byte

#### Equates

`VIA_T1LL`

#### Module

6, the serial card

#### Access

R/W. A read gives the latch's low byte; a write sets it. Neither touches the flag or the counter.

#### Bits

All: the low byte of the value the counter is loaded with.

#### Reset

Kept through /RESET. `$FF` at power-on.

#### Status

**emulated**

<a id="reg-c347"></a>
### $C347 VIA_T1LH: timer 1's latch, high byte

#### Equates

`VIA_T1LH`

#### Module

6, the serial card

#### Access

R/W. A read gives the latch's high byte. A write sets it and clears timer 1's flag, and doesn't load the counter.

#### Bits

All: the high byte of the value the counter is loaded with.

#### Reset

Kept through /RESET. `$FF` at power-on.

#### Status

**emulated**

#### Notes

In free-running mode, a latch written here and at T1L-L takes effect at the next time-out, so a running tick changes
its rate without a gap.

<a id="reg-c348"></a>
### $C348 VIA_T2CL: timer 2's counter, low byte

#### Equates

`VIA_T2CL`

#### Module

6, the serial card

#### Access

R/W. A read gives the counter's low byte and clears timer 2's flag (IFR bit 5). A write sets the low-byte latch,
which the next write to T2C-H loads.

#### Bits

All: the low byte.

#### Reset

Kept through /RESET: the counter runs on. At power-on the counter is `$FFFF` and the latch `$FF`.

#### Status

**emulated**

<a id="reg-c349"></a>
### $C349 VIA_T2CH: timer 2's counter, high byte

#### Equates

`VIA_T2CH`

#### Module

6, the serial card

#### Access

R/W. A read gives the counter's high byte, and clears nothing. A write loads the counter with this byte and the
low-byte latch, clears timer 2's flag and starts the timer.

#### Bits

All: the high byte.

#### Reset

Kept through /RESET: the counter runs on.

#### Status

**emulated**

#### Notes

One-shot: the flag comes once, with timer 1's timing. With ACR bit 5 the counter counts PB6's falling edges instead,
and on this card none come ([above](#the-timers)).

<a id="reg-c34a"></a>
### $C34A VIA_SR: the shift register

#### Equates

`VIA_SR`

#### Module

6, the serial card

#### Access

R/W. A read or a write clears IFR's SR flag (bit 2). Nothing is shifted.

#### Bits

All: the byte last written.

#### Reset

Kept through /RESET. `$00` at power-on.

#### Status

**emulated**

#### Notes

The shift register isn't modelled: SR is a plain byte, and its flag is never set
([above](#the-shift-register-and-the-control-lines)). hw/design.md has it shift on CB1 and CB2, which the design's
card brings out to the user port.

<a id="reg-c34b"></a>
### $C34B VIA_ACR: the auxiliary control register

#### Equates

`VIA_ACR`

#### Module

6, the serial card

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | T1PB7 | 1: PB7 is timer 1's output |
| 6 | T1FREE | 1: timer 1 free-running, reloading from its latch; 0: one-shot |
| 5 | T2PULSE | 1: timer 2 counts PB6's falling edges; 0: it counts cycles |
| 4–2 | SRMODE | the shift register's mode. Stored, not modelled |
| 1 | PBLATCH | latch port B's inputs. Stored, not modelled |
| 0 | PALATCH | latch port A's inputs. Stored, not modelled |

#### Reset

`$00`: timer 1 one-shot with PB7 a port line, timer 2 counting cycles.

#### Status

**emulated**

#### Notes

Under OODOS, ACR is OODOS's ([AGENTS.md](../../AGENTS.md)): it holds both timers' modes, so a program writing it
could stop the system tick that OODOS is to run on timer 1.

<a id="reg-c34c"></a>
### $C34C VIA_PCR: the peripheral control register

#### Equates

`VIA_PCR`

#### Module

6, the serial card

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | CB2 | CB2's mode. Bit 7 clear, an input: bit 6 1 for a rising edge, 0 a falling one; bit 5 1 for an independent input, whose flag port B's accesses leave alone. `%110` held low, `%111` held high. `%100` and `%101`, the handshake and the pulse, aren't modelled |
| 4 | CB1 | 1: CB1's flag on a rising edge; 0: on a falling one |
| 3–1 | CA2 | CA2's mode, as CB2's, for port A |
| 0 | CA1 | 1: CA1's flag on a rising edge; 0: on a falling one |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

CA1, CA2, CB1 and CB2 aren't connected on this card, so no edge ever comes and the outputs go nowhere. What PCR
still changes is which flags a port access clears.

<a id="reg-c34d"></a>
### $C34D VIA_IFR: the interrupt flags

#### Equates

`VIA_IFR`

#### Module

6, the serial card

#### Access

R/W. A write clears each flag whose bit is 1; bit 7 is ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | IRQ | 1: a flag that IER enables is set, and the VIA is pulling IRQL bit 6 |
| 6 | T1 | timer 1 timed out |
| 5 | T2 | timer 2 timed out |
| 4 | CB1 | an edge on CB1: never, on this card |
| 3 | CB2 | an edge on CB2: never |
| 2 | SR | the shift register: never |
| 1 | CA1 | an edge on CA1: never |
| 0 | CA2 | an edge on CA2: never |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

A flag is set whether or not IER enables it, so a program can poll a timer here with its interrupt off. Each flag
also clears by the access that serves it: T1 by reading T1C-L or writing T1C-H or T1L-H, T2 by reading T2C-L or
writing T2C-H. Under OODOS, IFR is OODOS's.

<a id="reg-c34e"></a>
### $C34E VIA_IER: the interrupt enables

#### Equates

`VIA_IER`

#### Module

6, the serial card

#### Access

R/W. A write with bit 7 set sets the enables whose bits are 1; with bit 7 clear, it clears them. The other enables
are left alone. A read gives the enables, with bit 7 as 1.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | SET | write: 1 set, 0 clear. Reads 1 |
| 6 | T1 | timer 1 |
| 5 | T2 | timer 2 |
| 4 | CB1 | CB1 |
| 3 | CB2 | CB2 |
| 2 | SR | the shift register |
| 1 | CA1 | CA1 |
| 0 | CA2 | CA2 |

#### Reset

Reads `$80`: every interrupt off.

#### Status

**emulated**

#### Notes

The BIOS's Break leaves IER as it was ([chapter 4](04-interrupts.md#irq-and-nmi)): write `$7F` here before a program
leaves. Under OODOS, IER is OODOS's.

<a id="reg-c34f"></a>
### $C34F VIA_ORA_NH: port A, without handshake

#### Equates

`VIA_ORA_NH`

#### Module

6, the serial card

#### Access

R/W. The same as VIA_ORA (`$C341`), except that neither a read nor a write touches the CA1 and CA2 flags.

#### Bits

The same as [VIA_ORA](#reg-c341)'s: joystick 1 on bits 0–4, 0 pressed.

#### Reset

ORA `$00`; a read gives `$FF` with nothing pressed.

#### Status

**emulated**

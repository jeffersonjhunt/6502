# 5 DMA

The four-channel DMA controller (module 8, `$C400`): memory to memory copies and fills, and transfers paced by a
card.

The design record, for the reasons: [hw/design.md, "DMA"](../../hw/design.md#dma).

## The controller

Status: **emulated**

The controller is a card of its own, module 8, in the style of the PC's 8237. It has four channels, each with a
20-bit address, a 16-bit count, a mode, a control byte and a 20-bit destination. Channel n's registers are 16 bytes
at `$C400 + n × $10`. STATUS, CONTROL and ABORT at `$C440` are shared, and the ID is at `$C47F`.

- **Physical addresses.** The controller drives A0–A19 itself, so it sees physical memory, never the CPU's windows.
  It reaches RAM only: not the flash and not the I/O hole. Wait states ([WAIT0–3](11-north-bridge-and-rtc.md#reg-c68a))
  apply to its accesses as to the CPU's.
- **Taking the bus.** Before each CPU cycle the controller looks at its channels, channel 0 first, and takes the bus
  for the first one that has work. Taking it costs one cycle: RDY holds the CPU, on a write cycle too, and BE floats
  it off the bus. The controller then moves bytes and hands the bus back.
- **How long it keeps the bus.** CONTROL's BURST sets that: 2^BURST bytes, after which the CPU gets one cycle back,
  or no limit at 7. It resets to `$70`: no limit, with DMA off until software sets the master enable (bit 0). The
  BIOS writes `DMA_ON` (`$71`: on, no limit) when it sets up the text screen at boot, before the SD boot's read and
  before Apple II mode's fills, and leaves it there, so under the monitor CONTROL reads `$71`.
- **Terminal count.** When a channel's count runs out, its STATUS bit is set and its REQ clears. ENABLE clears too,
  unless the mode's AUTO reloads the address, count and destination and keeps the channel armed. With the mode's
  IRQ bit set, the controller interrupts on IRQH bit 0 until the STATUS bit is cleared.
- **Programming.** Write a channel's registers with it disabled, a byte at a time, then set ENABLE: a half-written
  address never matters. Writing ADDR, COUNT or DEST sets both the start value (for AUTO) and the current one;
  reading gives the current one. COUNT 0 means 65,536.

### Apple II mode

Status: **emulated**

The controller is idle in Apple II mode, as hw/design.md wants, since stolen cycles would break code that counts
them. Entering the mode pulses /RESET, which resets the controller: every channel is cleared, and CONTROL goes back
to `$70`, with the master enable off. The IIe's software can't reach the controller's registers, so nothing starts
it again. Ctrl-Reset in the mode pulses /RESET too, with the same effect.

## Memory to memory

Status: **emulated**

Any channel can copy memory to memory (MODE's DIR = `%10`), with DEST as the destination. A copy runs as a block,
whatever XFER says (software writes `%10`, block): REQ in CTRL starts it, and the CPU is held until the count runs
out, or gets a cycle back every 2^BURST bytes. Each byte takes two cycles, a read into the controller's latch and
then the write, so 64 KB takes about 18 ms at 7.16 MHz.

- **A fill:** HOLD keeps the source address still, so one byte fills the destination. The BIOS clears memory that
  way, and the text screen by an overlapping copy of its first cell.
- **Down:** DEC steps both addresses down.
- **Waiting for it:** a program that needs the copy done before it goes on waits for ENABLE to clear.
- **The master enable** is the whole controller's. Turning it on for a copy runs any other channel a program has
  left armed. To keep a channel from running, abort it.

The BIOS's copies, the text screen's scroll among them, are done this way on channel 3 (`video.s`). The routine
turns the master enable on, starts the channel, and waits for ENABLE to clear, then puts CONTROL back as it found
it, which under the monitor is `$71`. A copy still
running after 65,536 polls, about 0.1 s, is aborted: no controller there, or a busier channel starving it.

<!-- listing: ../../backplane/bios/video.s:280-307 -->
```
; Start channel 3, programmed, and wait for ENABLE to clear at terminal
; count, with the master enable on (a program may have turned it off) and
; CONTROL put back after. The enable is the whole controller's, so a
; program's channels still armed run meanwhile: to keep one from running, a
; program aborts it, not just turns the master enable off. 65,536 polls, about 0.1 s, then it gives up and
; stops the channel: no controller there (ENABLE reads 1 off a floating
; bus), or one starved by a busier channel. For the demo too. Uses A, X, Y.
DMA3RUN:
                LDA     DMA_CONTROL
                PHA
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
                LDA     #DMA_GO
                STA     DMA3_CTRL
                LDX     #0
                LDY     #0
@wait:          LDA     DMA3_CTRL
                LSR     A
                BCC     @done
                DEX
                BNE     @wait
                DEY
                BNE     @wait
                LDA     #DMA_CH3
                STA     DMA_ABORT
@done:          PLA
                STA     DMA_CONTROL
                RTS
```

Before it, the caller aborts channel 3, writes ADDR, DEST and COUNT, and writes MODE as `DMA_M2M` (`$0A`: block,
memory to memory).

## Transfers for a card

Status: **emulated**

A card asks for a byte with its channel's DRQ, and the controller answers with that channel's /DACK. The transfer is
fly-by, one cycle a byte. The card puts its byte on the bus, or takes the one memory puts there, and needs no
address of its own. On the last byte the card also sees terminal count.

| Channel | DRQ in the emulator | In the design |
|---|---|---|
| 0 | the SD card, while its CTRL has DMA set and STATUS's DATA is set | mass storage |
| 1 | none | serial and parallel |
| 2 | the sound card, always: RAMDATA, or what its DRQSEL picks ([`$C563`](07-sound.md#reg-c563)) | USB, or the sound card's RAM |
| 3 | none | memory to memory and blits |

MODE's DIR says which way: `%00` card to memory, `%01` memory to card. XFER says how:

- **single** (`%00`): one byte for each request, then the CPU gets a cycle;
- **demand** (`%01`): bytes while the card holds DRQ, up to the BURST limit;
- **block** (`%10`): the whole count once DRQ or REQ starts it, a BURST at a time.

A reserved XFER or DIR (`%11`) never runs. A block channel with no card behind it still runs when REQ starts it:
card to memory writes `$FF`, the floating bus. STATUS's bits 4–7 show each channel's DRQ as it is now.

OODOS's BIOS reads the SD card on channel 0 and copies M:, the RAM disk, on channel 3
([hw/oodos.md, "BIOS"](../../hw/oodos.md#bios)). Its copies on channel 3 go 2 KB at a time, 4,096 cycles each with
the CPU held: the VIA's timer 1 interrupts each 7,159 cycles for OODOS's millisecond counter, and has one flag, so a
copy held across two of its periods would lose a tick. Between the blocks the CPU takes the interrupt.

Since OOLIB's L3, channel 2 is OODOS's too: its function 82 loads the sound card's RAM and the ES5503's wave RAM on it,
and feeds the card's PCM stream from two buffers of a program's, always 4 KB a piece (4,096 cycles at most with the
CPU held, for the same reason), the stream's with MODE's IRQ set so that the next piece starts at each terminal count
([chapter 7](07-sound.md#under-oodos)). OODOS sets the master enable for it and leaves it on. The warm boot aborts the
channel.

## Registers

Status: **emulated**

<a id="reg-c400"></a>
### $C400 DMA0_ADDR: channel 0's address

#### Equates

`DMA`, `DMA0_ADDR`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, low byte first. A write sets the start address and the current one; a read gives the current one.

#### Bits

All: a 20-bit physical address. `$C402` holds bits 16–19 in its low four bits; its high four are dropped. For a
memory-to-memory copy, the source.

#### Reset

`$00 $00 $00`.

#### Status

**emulated**

#### Notes

The address steps up after each byte, down with MODE's DEC, or not at all with HOLD, and wraps within 20 bits. AUTO
puts the start address back at terminal count.

<a id="reg-c404"></a>
### $C404 DMA0_COUNT: channel 0's count

#### Equates

`DMA0_COUNT`

#### Module

8, the DMA controller

#### Access

R/W, 2 bytes, low byte first. A write sets the start count and the current one; a read gives the bytes left.

#### Bits

All: the bytes to move, 1–65,535, or 0 for 65,536.

#### Reset

`$00 $00`: 65,536 if the channel runs.

#### Status

**emulated**

#### Notes

A channel that has run out reads 0, and runs 65,536 bytes if it is started again unwritten.

<a id="reg-c406"></a>
### $C406 DMA0_MODE: channel 0's mode

#### Equates

`DMA0_MODE`

#### Module

8, the DMA controller

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | IRQ | 1: interrupt at terminal count (IRQH bit 0) |
| 6 | AUTO | 1: at terminal count, reload ADDR, COUNT and DEST and stay armed |
| 5 | HOLD | 1: the address stays still. For memory to memory, the source only: a fill |
| 4 | DEC | 1: the address steps down. For memory to memory, both |
| 3–2 | DIR | `%00` card to memory, `%01` memory to card, `%10` memory to memory, `%11` reserved |
| 1–0 | XFER | `%00` single, `%01` demand, `%10` block, `%11` reserved |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

backplane.inc names `DMA_M2M` (`$0A`: block, memory to memory), `DMA_HOLD` (`$20`) and `DMA_DEMAND_IN` (`$01`:
demand, card to memory, as the SD card is read).

<a id="reg-c407"></a>
### $C407 DMA0_CTRL: channel 0's control

#### Equates

`DMA0_CTRL`

#### Module

8, the DMA controller

#### Access

R/W. The controller clears REQ at terminal count, and ENABLE too unless AUTO is set.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–2 | | 0 |
| 1 | REQ | write 1: a software request. It starts a memory-to-memory copy, or a block transfer with no DRQ |
| 0 | ENABLE | 1: the channel is armed. Writing 0 stops a transfer under way |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

`DMA_ENABLE` (`$01`) and `DMA_GO` (`$03`, ENABLE and REQ) are in backplane.inc. A program waits for a copy by
polling ENABLE until it is 0.

<a id="reg-c408"></a>
### $C408 DMA0_DEST: channel 0's destination

#### Equates

`DMA0_DEST`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, low byte first, as [ADDR](#reg-c400).

#### Bits

All: a 20-bit physical address, the destination of a memory-to-memory copy. Bits 16–19 are in `$C40A`'s low four
bits. A card transfer doesn't use it.

#### Reset

`$00 $00 $00`.

#### Status

**emulated**

<a id="reg-c410"></a>
### $C410 DMA1_ADDR: channel 1's address

#### Equates

`DMA1_ADDR`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, as [channel 0's](#reg-c400).

#### Bits

As [DMA0_ADDR](#reg-c400), for channel 1.

#### Reset

`$00 $00 $00`.

#### Status

**emulated**

#### Notes

No card in the emulator drives DRQ1, so channel 1 runs only as a block started by REQ, or a memory-to-memory copy.

<a id="reg-c414"></a>
### $C414 DMA1_COUNT: channel 1's count

#### Equates

`DMA1_COUNT`

#### Module

8, the DMA controller

#### Access

R/W, 2 bytes, as [channel 0's](#reg-c404).

#### Bits

As [DMA0_COUNT](#reg-c404), for channel 1.

#### Reset

`$00 $00`.

#### Status

**emulated**

<a id="reg-c416"></a>
### $C416 DMA1_MODE: channel 1's mode

#### Equates

`DMA1_MODE`

#### Module

8, the DMA controller

#### Access

R/W.

#### Bits

As [DMA0_MODE](#reg-c406), for channel 1.

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c417"></a>
### $C417 DMA1_CTRL: channel 1's control

#### Equates

`DMA1_CTRL`

#### Module

8, the DMA controller

#### Access

R/W, as [channel 0's](#reg-c407).

#### Bits

As [DMA0_CTRL](#reg-c407), for channel 1.

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c418"></a>
### $C418 DMA1_DEST: channel 1's destination

#### Equates

`DMA1_DEST`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, as [channel 0's](#reg-c408).

#### Bits

As [DMA0_DEST](#reg-c408), for channel 1.

#### Reset

`$00 $00 $00`.

#### Status

**emulated**

<a id="reg-c420"></a>
### $C420 DMA2_ADDR: channel 2's address

#### Equates

`DMA2_ADDR`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, as [channel 0's](#reg-c400).

#### Bits

As [DMA0_ADDR](#reg-c400), for channel 2.

#### Reset

`$00 $00 $00`.

#### Status

**emulated**

#### Notes

The sound card's RAMDATA drives DRQ2, and holds it all the time: a transfer on channel 2 loads or reads the card's
own 64 KB from RAMADDR on ([chapter 7](07-sound.md)). Revision 2's DRQSEL can give DRQ2 to PCMDATA or DOCDATA
instead ([`$C563`](07-sound.md#reg-c563)).

<a id="reg-c424"></a>
### $C424 DMA2_COUNT: channel 2's count

#### Equates

`DMA2_COUNT`

#### Module

8, the DMA controller

#### Access

R/W, 2 bytes, as [channel 0's](#reg-c404).

#### Bits

As [DMA0_COUNT](#reg-c404), for channel 2.

#### Reset

`$00 $00`.

#### Status

**emulated**

<a id="reg-c426"></a>
### $C426 DMA2_MODE: channel 2's mode

#### Equates

`DMA2_MODE`

#### Module

8, the DMA controller

#### Access

R/W.

#### Bits

As [DMA0_MODE](#reg-c406), for channel 2.

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c427"></a>
### $C427 DMA2_CTRL: channel 2's control

#### Equates

`DMA2_CTRL`

#### Module

8, the DMA controller

#### Access

R/W, as [channel 0's](#reg-c407).

#### Bits

As [DMA0_CTRL](#reg-c407), for channel 2.

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c428"></a>
### $C428 DMA2_DEST: channel 2's destination

#### Equates

`DMA2_DEST`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, as [channel 0's](#reg-c408).

#### Bits

As [DMA0_DEST](#reg-c408), for channel 2.

#### Reset

`$00 $00 $00`.

#### Status

**emulated**

<a id="reg-c430"></a>
### $C430 DMA3_ADDR: channel 3's address

#### Equates

`DMA3_ADDR`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, as [channel 0's](#reg-c400).

#### Bits

As [DMA0_ADDR](#reg-c400), for channel 3: the source of the BIOS's copies.

#### Reset

`$00 $00 $00`.

#### Status

**emulated**

#### Notes

No card drives DRQ3. Channel 3 is for memory to memory: the BIOS's copies and fills, and OODOS's M:.

<a id="reg-c434"></a>
### $C434 DMA3_COUNT: channel 3's count

#### Equates

`DMA3_COUNT`

#### Module

8, the DMA controller

#### Access

R/W, 2 bytes, as [channel 0's](#reg-c404).

#### Bits

As [DMA0_COUNT](#reg-c404), for channel 3.

#### Reset

`$00 $00`.

#### Status

**emulated**

<a id="reg-c436"></a>
### $C436 DMA3_MODE: channel 3's mode

#### Equates

`DMA3_MODE`

#### Module

8, the DMA controller

#### Access

R/W.

#### Bits

As [DMA0_MODE](#reg-c406), for channel 3.

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c437"></a>
### $C437 DMA3_CTRL: channel 3's control

#### Equates

`DMA3_CTRL`

#### Module

8, the DMA controller

#### Access

R/W, as [channel 0's](#reg-c407).

#### Bits

As [DMA0_CTRL](#reg-c407), for channel 3.

#### Reset

`$00`.

#### Status

**emulated**

<a id="reg-c438"></a>
### $C438 DMA3_DEST: channel 3's destination

#### Equates

`DMA3_DEST`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, as [channel 0's](#reg-c408).

#### Bits

As [DMA0_DEST](#reg-c408), for channel 3.

#### Reset

`$00 $00 $00`.

#### Status

**emulated**

<a id="reg-c440"></a>
### $C440 DMA_STATUS: terminal counts and requests

#### Equates

`DMA_STATUS`

#### Module

8, the DMA controller

#### Access

R. Writing 1s clears those terminal-count bits.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | DRQ3–DRQ0 | 1: that channel's card is asking now (read-only) |
| 3–0 | TC3–TC0 | 1: that channel has reached terminal count. Write 1 to clear |

#### Reset

`$00`, but for a DRQ a card holds: the sound card holds DRQ2 all the time (with DRQSEL at its reset value), so with
a sound card it reads `$40`.

#### Status

**emulated**

#### Notes

A TC bit with its channel's MODE IRQ set is what interrupts. `DMA_CH3` (`$08`) and `DMA_ALL` (`$0F`) name the bits.

<a id="reg-c441"></a>
### $C441 DMA_CONTROL: the whole controller's control

#### Equates

`DMA_CONTROL`

#### Module

8, the DMA controller

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | | kept, unused |
| 6–4 | BURST | the CPU gets a cycle back every 2^BURST bytes; 7: no limit |
| 3–1 | | kept, unused |
| 0 | ENABLE | 1: the master enable. 0: no channel runs |

#### Reset

`$70`: no limit, and off.

#### Status

**emulated**

#### Notes

`DMA_ON` (`$71`) and `DMA_MASTER` (`$01`) are in backplane.inc. The BIOS writes `DMA_ON` in VINIT at boot
(`video.s`), before the SD boot's read (`sd.s`) and before Apple II mode's fills (`apple.s`), and leaves it there,
so under the monitor CONTROL reads `$71`. BURST limits demand and block transfers and
memory-to-memory copies; a single transfer gives a cycle back after every byte whatever it says.

<a id="reg-c442"></a>
### $C442 DMA_ABORT: stop channels

#### Equates

`DMA_ABORT`

#### Module

8, the DMA controller

#### Access

W. Reads `$FF`.

#### Bits

Bits 3–0: a 1 stops channel 3–0 at once, clearing its ENABLE and REQ. Bits 7–4 are ignored.

#### Reset

Not a register to read.

#### Status

**emulated**

#### Notes

ADDR and COUNT keep the values they stopped at. The BIOS's Break aborts all four (`DMA_ALL`) and clears their TC
bits and their modes.

<a id="reg-c47f"></a>
### $C47F DMA_ID: the controller's ID

#### Equates

`DMA_ID`

#### Module

8, the DMA controller

#### Access

R.

#### Bits

All: the type, module 8, in bits 7–4, and the revision, 1, in bits 3–0. `DMA_ID_V1`.

#### Reset

`$81`.

#### Status

**emulated**

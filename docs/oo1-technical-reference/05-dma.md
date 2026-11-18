# 5 DMA

The DMA controller, revision 2 (module 8, `$C400`, ID `$82`): eight channels behind one register window, memory to
memory copies and fills, and transfers for the cards, which ask by module ID.

The design record, for the reasons: [hw/design.md, "The DMA controller, revision 2"](../../hw/design.md#the-dma-controller-revision-2)
and [the B2 migration](../../hw/design.md#the-b2-migration-revision-1-dropped).

## The controller

Status: **proposed**

Emulated as steps B2 and B3 (all eight channels since B3) and B4 (16-bit transfers)
([hw/design.md](../../hw/design.md#the-dma-controller-revision-2)).

The controller is on the CPU card, module 8, in the style of the PC's 8237. It has eight channels, each with a
24-bit address, a 16-bit count, a mode, a control byte, a 24-bit destination and a CARD byte naming the module it
serves. One window, `$C400–$C40F`, shows the channel that CONTROL's SELECT names; STATUS, ABORT and REQ at
`$C410–$C412`, CONTROL at `$C47E` and the ID at `$C47F` are shared. All eight are live (channels 4–7 since B3,
*proposed*), each with its bit in STATUS, ABORT and REQ.

- **The SELECT rule.** SELECT is shared state. Code that sets it saves CONTROL first and puts it back after,
  interrupt code included, so an interrupt taken between a SELECT and the channel's registers leaves the window
  where it found it. Code that writes CONTROL whole (`DMA_ON`) writes the SELECT it means. STATUS, ABORT, REQ and
  the ID need no SELECT. The controller can't check the rule: a register written before its SELECT goes to the
  wrong channel.
- **Physical addresses.** The controller drives A0–A23 itself, so it sees physical memory, never the CPU's
  windows. A DMA cycle is a memory cycle, so it never reaches the I/O hole. It reaches the RAM card at
  `$000000–$0FFFFF` (while the north bridge's AP8 is set, bank 8 there is the aperture, as for the CPU:
  [chapter 2](02-memory-and-banking.md#revision-2-bank-8-the-aperture)), the aperture's own place at `$E00000`, and
  the flash from `$F00000`. Nothing answers `$100000–$DFFFFF`: a read gives `$FF`. Don't write the flash by DMA:
  a write there could be one step of a flash command.
- **Wait states** come by /WAIT, which a slow card pulls. The north bridge turns it into RDY for the CPU, and the
  controller stretches its grant by the same cycles
  ([hw/design.md](../../hw/design.md#wait-states-and-the-read-budget)). The north bridge's
  [WAIT0–3](11-north-bridge-and-rtc.md#reg-c68a) slow nothing since B2.
- **Taking the bus.** Before each CPU cycle the controller looks at its channels and takes the bus when one has
  work. Taking it costs one cycle: RDY holds the CPU, on a write cycle too, and BE floats it off the bus. A card's
  transfer starts with a poll of the cards ([below](#transfers-for-a-card)). The controller then moves bytes (or
  words, with CARD's WIDE: [below](#16-bit-transfers)) and hands the bus back. Priority is fixed: channel 0 first,
  then 1–7.
- **How long it keeps the bus.** CONTROL's BURST sets that: 2^BURST transfers (a byte, or a word with WIDE), after
  which the CPU gets one cycle back, or no limit at 7. CONTROL resets to `$70`: no limit, SELECT 0, and DMA off until software sets the master enable
  (bit 0). With a revision 2 video card the BIOS writes `DMA_ON` (`$71`: on, no limit, SELECT 0) when it sets up the
  text screen at boot, before the SD boot's read, and leaves it there, so under the monitor CONTROL reads `$71`.
- **Terminal count.** When a channel's count runs out, its STATUS bit is set and its REQ clears. ENABLE clears too,
  unless the mode's AUTO reloads the address, count and destination and keeps the channel armed. With the mode's
  IRQ bit set, the controller interrupts on IRQH bit 0 until the STATUS bit is cleared.
- **Programming.** Select the channel, then write its registers with it disabled, a byte at a time, then set
  ENABLE: a half-written address never matters. Writing ADDR, COUNT or DEST sets both the start value (for AUTO)
  and the current one; reading gives the current one. COUNT 0 means 65,536.
- **Break.** The BIOS's QUIET aborts all eight channels and clears their STATUS bits, then selects each in turn,
  clears its MODE and puts its CARD back to the reset value, and puts CONTROL back as it found it.

#### Revision 1: replaced by revision 2 (B2)

Revision 1, ID `$81`, was the emulator's controller until B2, and BIOS 0.14 and OODOS's code before B2 drive it. It
had four channels, each with its 16 bytes at `$C400 + n × $10` (ADDR, COUNT, MODE, CTRL and DEST, the addresses 20
bits), STATUS at `$C440` (the terminal counts in bits 3–0, each channel's request as it was in bits 7–4), CONTROL at
`$C441` and ABORT at `$C442`. Each card had a request line and an acknowledge of its own, one channel's. The emulator
has no revision 1 now. BIOS 0.15, and OODOS from B2, take an ID of `$81` as no controller, since its registers are
elsewhere.

### Under OODOS

Status: **proposed**, on revision 2, which OODOS's code drives from B2 (OODOS.SYS on the cards in the repo). An
OODOS.SYS built before B2 drives revision 1, and on revision 2 it reaches `A>` but can't use a file. Its SD card's
sectors still reach channel 0, because revision 1's channel 0 sat where the window is (with SELECT 0 and the master
on, as BIOS 0.15 leaves them with a revision 2 video card). But its channel-3 work times out and moves nothing, and
its BDOS moves every FCB and every record between banks on channel 3: DIR says "No File", TYPE never comes back,
and a write saves nothing it should. Its paths that check the ID see no controller. Replace such a card with the
built-in one, and don't write with it. Without a revision 2 video card the master is off, and its sector reads fail
too.

OODOS owns all eight channels; a program reaches them only through its functions. Each of its routines keeps the
SELECT rule: CONTROL saved, the channel selected, programmed and waited for, CONTROL put back. **Channel 0** is the
SD card's: the BIOS's SDIO, a sector a READ or WRITE, and function 83's `DM_LOAD`, up to 8 sectors (4 KB) a READ,
the CPU held a cycle a byte. **Channel 1** is the video card's data port (revision 2, *proposed*;
[below](#channel-1-the-video-cards-data-port)): the vertical-blank copies (OOLIB L4), function 83's `DM_VBSET`
slots and the pointer sprite's four bytes, from the frame handler, each waited for; and function 83's `DM_VWRITE`,
`DM_VREAD` and `DM_VFILL`, in pieces with interrupts masked for each, so the frame handler never meets the port or
the channel half set. Nothing else uses it. **Channel 2** is the sound card's, through function 82's loads and
stream (L3); the stream's next piece is programmed in the interrupt, which saves and puts back CONTROL as the rule
says. **Channel 3** is the BIOS's DMAGO, 2 KB a block with a pending interrupt taken between blocks: MOVE, the
console's scroll and erase, and function 83's `DM_COPY` and `DM_FILL`, and `DM_VCOPY`'s pieces, card to card through
aperture slots 2 and 3, which it borrows for each piece. Since the interrupt has a channel of its own, it never meets
a copy on channel 3 half programmed. **Channels 4–7** are free for later drivers; none uses them yet. Since B5
(*proposed*), the warm boot first stops channels 4–7 (ABORT) and sets their counts to 0, so a program's own channel
never outlives it, and puts every channel's CARD back to its reset module, WIDE 0 (DMAWARM); then its DEAFEN selects
each of the eight in turn to clear its MODE's IRQ bit. The Programmer's Guide's
[function 83](../oodos-programmers-guide/07-function-reference.md#bdos-83) has the calls.

### Apple II mode

Status: **proposed**

Emulated as step B2.

The controller is idle in Apple II mode, as hw/design.md wants, since stolen cycles would break code that counts
them. Entering the mode pulses /RESET, which resets the controller: every channel is cleared and its CARD put back
to its reset value, REQ cleared and any hold-off ended, and CONTROL goes back to `$70`, with the master enable off
and SELECT 0. With the master enable off the controller neither polls nor takes the bus, so a card's standing
request (the sound card's) costs no cycle. The IIe's software can't reach the controller's registers, so nothing
starts it again. Ctrl-Reset in the mode pulses /RESET too, with the same effect.

## Memory to memory

Status: **proposed**

Emulated as step B2.

Any channel can copy memory to memory (MODE's DIR = `%10`), with DEST as the destination. A copy runs as a block,
whatever XFER says (software writes `%10`, block): REQ in CTRL starts it, and the CPU is held until the count runs
out, or gets a cycle back every 2^BURST transfers. Each byte takes two cycles, a read into the controller's latch
and then the write, so 64 KB takes about 18 ms at 7.16 MHz; or, with WIDE and the two addresses of one parity, two
cycles a word, so 64 KB takes about 9 ms ([below](#16-bit-transfers)). A copy needs no poll of the cards, so it
costs 1 + 2n cycles for n bytes a take, as on revision 1; a take polls only when a card's channel is armed too
([below](#transfers-for-a-card)).

- **A fill:** HOLD keeps the source address still, so one byte fills the destination. The BIOS clears memory that
  way, and the text screen by an overlapping copy of its first cell.
- **Down:** DEC steps both addresses down.
- **Waiting for it:** a program that needs the copy done before it goes on waits for ENABLE to clear.
- **The master enable** is the whole controller's. Turning it on for a copy runs any other channel a program has
  left armed. To keep a channel from running, abort it.

The BIOS's copies and fills (Apple II mode's clear, and the diagnostics' stage 8) and OODOS's DMAGO run on channel 3.
Each selects channel 3 (`DMA_SEL3`) with the master enable on, aborts it, writes ADDR, DEST and COUNT, writes MODE
as `DMA_M2M` (`$0A`: block, memory to memory) and CTRL as `DMA_GO`, and waits for ENABLE to clear. DMA3RUN, BIOS
0.12's routine for that, went in BIOS 0.15: nothing called it after BIOS 0.13 moved the screen's scroll to revision
2's port on channel 1 ([chapter 6](06-video.md#the-bioss-text-screen)).

## Transfers for a card

Status: **proposed**

Emulated as step B2 ([hw/design.md](../../hw/design.md#the-dma-controller-revision-2)).

A card wanting a transfer pulls /DREQ, one open-drain line that every card shares. The controller finds out who
asks by a poll, and grants a channel whose CARD names that card's module. The transfer is fly-by, one cycle a
transfer: the card puts its byte (or, with WIDE, its word) on the bus, or takes the one memory puts there, and needs
no address of its own. On the last transfer the card also sees terminal count.

| Channel | CARD at reset | Module | In the emulator |
|---|---|---|---|
| 0 | `$07` | 7, mass storage | the SD card, while its CTRL has DMA set and STATUS's DATA is set |
| 1 | `$09` | 9, video | revision 2's data port, while PCTL's DRQ bit is set ([below](#channel-1-the-video-cards-data-port)) |
| 2 | `$0A` | A, sound | the sound card, always at DRQSEL 0 and 2: RAMDATA, or what its DRQSEL picks ([`$C563`](07-sound.md#reg-c563)) |
| 3 | `$08` | none | memory to memory |
| 4–7 | `$08` | none | free for later drivers |

A channel serves a card only by its CARD: software writes a channel's CARD to serve another module. In the emulator
a card's request reaches /DREQ when its machine-file line has `drq` (the SD, video and sound cards on
`backplane.machine`; the USB card is left off), and its module is its I/O block's. The serial card asks by module 6,
through DMASTREAM (*proposed*, [hw/design.md](../../hw/design.md#the-serial-cards-stream-select)), which isn't
modelled.

**The poll.** The controller polls when /DREQ is low, the master enable is on, a channel serving a card is armed
(its MODULE isn't none, and its DIR is card to memory or memory to card), and no hold-off is running. After the
takeover it runs one cycle with /DMA and /IACK low; each card with a request pulls its own data line, D(ID), and REQ
records the result by channel. In the emulator the poll cycle, like the takeover, isn't reported to the bus trace
or to a breakpoint.

**The grant.** It then grants the highest-priority channel among those whose module asked at the poll and those
started by software (REQ, or a block under way). Each grant cycle puts the memory address on A0–A23 and the channel's
MODULE on PS0–PS3, and the card named answers with its data register. A grant is stretched while /WAIT is low: in
the emulator, by the longer of the memory's and the card's `wait`.

**When the card lets go.** In a grant only the card named may pull /DREQ, and it does if it wants another transfer
after this one. A demand transfer goes on while it does, with no new poll, and ends when it lets go, whatever
other cards ask.

**The hold-off.** A poll after which there is nothing to grant (a card asks, but no channel is armed for it, and no
channel started by software waits) gives the bus back after its two cycles, and the controller doesn't poll again
for 64 cycles, counted from that take's first, unless /DREQ goes high and low again or software writes a channel's
CTRL. So a stray request costs at most 2 cycles in 64. A channel started by software meanwhile runs with no poll. A
reset ends the hold-off.

| Transfer | Cycles |
|---|---|
| single (a byte a request) | 3 a byte: the takeover, the poll, the byte |
| demand, block | demand: 2 + n for n bytes, every take polls (only a card's request starts one). Block: 2 + n for the take a card's request starts; 1 + n for a block REQ starts or one under way, which skips the poll unless another armed card channel waits with /DREQ low and no hold-off running |
| memory to memory | 1 + 2n a take, as on revision 1 |

With WIDE, n counts transfers, a byte or a word, at the same cost each: single is 3 cycles a word, and a card's
demand transfer of b bytes from an even address is 2 + b/2 ([16-bit transfers](#16-bit-transfers)).

**The sound card's standing request.** At DRQSEL 0, its reset value and OODOS's idle value, and at 2, the sound
card asks all the time, so with a sound card fitted /DREQ is almost never high. Memory-to-memory copies cost what
they did. A card's transfer pays the poll at each take, a card that becomes ready can wait up to 64 cycles for its
grant, behind the hold-off, and while a card's channel is armed the stray polls cost 2 cycles in 64.

MODE's DIR says which way: `%00` card to memory, `%01` memory to card. XFER says how:

- **single** (`%00`): one transfer for each request, then the CPU gets a cycle;
- **demand** (`%01`): transfers while the card asks, up to the BURST limit;
- **block** (`%10`): the whole count once a request or REQ starts it, a BURST at a time.

A reserved XFER or DIR (`%11`) never runs. A block channel with no card behind it (MODULE none, or no card on its
module) still runs when REQ starts it: card to memory writes `$FF`, the floating bus.

OODOS's BIOS reads the SD card on channel 0 and copies M:, the RAM disk, on channel 3
([hw/oodos.md, "BIOS"](../../hw/oodos.md#bios)). Its copies on channel 3 go 2 KB at a time, 4,096 cycles each with
the CPU held: the VIA's timer 1 interrupts each 7,159 cycles for OODOS's millisecond counter, and has one flag, so a
copy held across two of its periods would lose a tick. Between the blocks the CPU takes the interrupt.

Since OOLIB's L3, channel 2 is OODOS's too: its function 82 loads the sound card's RAM and the ES5503's wave RAM on it,
and feeds the card's PCM stream from two buffers of a program's, always 4 KB a piece (4,096 cycles at most with the
CPU held, for the same reason), the stream's with MODE's IRQ set so that the next piece starts at each terminal count
([chapter 7](07-sound.md#under-oodos)). OODOS sets the master enable for it and leaves it on. The warm boot aborts the
channel.

## Channel 1: the video card's data port

Status: **proposed**

Revision 2 of the video card ([chapter 6](06-video.md#revision-2-card-memory)) has a data port, PDATA
(`$C4BB`), whose request channel 1 serves, by its reset CARD `$09`, as channel 2 serves the sound card's RAMDATA. It
is a card transfer like any other ([above](#transfers-for-a-card)), fly-by, with the card's own PADDR as the
card-memory side and the channel's ADDR as the system-RAM side:

| To | Channel 1's MODE DIR | The card's PCTL |
|---|---|---|
| card memory, from RAM | `%01`, memory to card | bit 1 clear; bit 0 set |
| RAM, from card memory | `%00`, card to memory | bit 1 set; bit 0 set |

- **The two sides step on their own.** The channel's address steps by one, or by two on a word (or down, with DEC,
  by one); PADDR by the card's PSTEP, up or down, once for each byte. So a 16-byte run of RAM can become a column
  of a text screen, PSTEP 160.
- **What it costs.** [hw/video-rev2.md](../../hw/video-rev2.md#data-port-and-dma) reckons two cycles a byte, a 4 KB
  font in about 8,200 cycles and a 300 KB bitmap in about 0.09 s. The emulator moves a port byte as any fly-by card
  transfer, in one cycle, and the card asks for every byte at once, so a 4 KB font takes about 4,100 cycles and
  OODOS's 1,024 bytes of vertical-blank copies about 1,024, the CPU held. With WIDE (*proposed*, B4) the port takes a
  word a grant, as two port accesses, the even byte first, each stepping PADDR by PSTEP: a 4 KB load from RAM in
  demand mode takes 2,050 cycles (0.286 ms), against 4,098 (0.572 ms) without, the takeover and the poll then a
  cycle a word ([16-bit transfers](#16-bit-transfers)). Neither the BIOS nor OODOS sets WIDE yet.
- **Copies within card memory** have no port path of their own: the card has no copy engine yet. Out to RAM and
  back in is two port transfers; OODOS's `DM_VCOPY` instead maps the two places through aperture slots and copies
  memory to memory on channel 3.
- **The serial card** asks by its own module, 6, so it needs a channel whose CARD names it, not channel 1
  ([hw/design.md](../../hw/design.md#the-serial-cards-stream-select)).
- **In Apple II mode** the controller is idle, as [above](#apple-ii-mode), so the port is never used there.

Under OODOS the port and channel 1 are OODOS's ([above](#under-oodos)); a program reaches card memory in bulk
through function 83's card operations
([function 83](../oodos-programmers-guide/07-function-reference.md#bdos-83)), which need the DMA controller.

## 16-bit transfers

Status: **proposed**

Emulated as step B4 ([hw/design.md](../../hw/design.md#16-bit-transfers), [as built](../../hw/design.md#b4-as-built)).

CARD's WIDE (bit 4, [`$C40C`](#reg-c40c)), 0 at reset, lets a channel move a word, D0–D15, in one bus cycle where a
byte took one. A byte cycle carries its byte on D0–D7 whatever A0 is: memory steers an odd address's byte onto it.
A word cycle is at an even address, the even byte on D0–D7 and the odd on D8–D15.

- **When a transfer is a word.** With WIDE set, a transfer at an even ADDR with two bytes or more left is a word;
  an odd first byte and an odd last byte go as bytes, so any address and count work. COUNT stays in bytes and goes
  down by 2 a word, ADDR (and DEST, memory to memory) step up by 2, wrapping within 24 bits.
- **When it isn't.** With MODE's DEC or HOLD set, the channel moves bytes, WIDE or not. Memory to memory moves words
  only when ADDR and DEST have the same parity, since the controller's 16-bit latch keeps each byte on its lane;
  otherwise bytes. The controller decides at each take of the bus, so software may rewrite ADDR or DEST between
  takes.
- **Terminal count** comes with the transfer that empties COUNT, a byte or a word.
- **BURST** counts transfers, a byte or a word ([`$C47E`](#reg-c47e)). A word costs what a byte does, so the CPU's
  longest wait is the same number of cycles either way.
- **/WAIT** stretches a word once, by the longer of the memory's and the card's.
- **Unmapped**, a word reads `$FFFF`, and a write is lost. A block channel with no card behind it, card to memory,
  writes `$FFFF` a word.

| Transfer, with WIDE | Cycles |
|---|---|
| single | 3 a word: the takeover, the poll, the word |
| demand, block | as without WIDE, n being transfers: b bytes from an even address are b/2 |
| memory to memory, 16 bytes | even to even 1 + 16; odd to odd 1 + 18 (a byte, 7 words, a byte); even to odd and odd to even 1 + 32 (bytes) |
| memory to memory, 2 KB even to even | 1 + 2,048 |
| memory to memory, 64 KB | 1 + 2 × 32,768 = 65,537 (9.15 ms at 7.16 MHz), against 131,073 (18.3 ms) without |

**Which registers take words.** Only memory and the video card's port and aperture do. WIDE must stay 0 on a channel
that serves any other card:

| Card, register | Width |
|---|---|
| Memory: the RAM card, ROM A and ROM B | words |
| The video card's aperture ([chapter 6](06-video.md#the-aperture)), `$E00000`, and bank 8 while AP8 is set | words, both bytes of each |
| The video card's port, PDATA ([chapter 6](06-video.md#the-data-port)) | words: two port accesses, the even byte at PADDR, a PSTEP step, then the odd byte, another step |
| The SD card's DATA ([chapter 10](10-storage.md#reading-and-writing-by-dma)) | bytes |
| The sound card's RAMDATA, PCMDATA and DOCDATA ([chapter 7](07-sound.md#reg-c563)) | bytes |
| The USB card's event FIFO ([chapter 8](08-usb.md#proposed)) | bytes |
| The serial card's stream registers ([chapter 9](09-serial-and-via.md#com1-com2-and-lpt)) | bytes |

**A byte card given WIDE** gets what the bus would give it: its data register moves once a grant, on D0–D7; D8–D15
float, so memory gets `$FF` in each odd byte, and each odd byte from memory is lost. A WIDE left on channel 0 under
the monitor (`C40C: 17`) so breaks the SD card's reads and writes; Break puts every CARD back, WIDE 0, and since B5
(*proposed*) so does OODOS's warm boot (DMAWARM, [above](#under-oodos)), so a WIDE a program left on
channel 0 is gone by the next `A>`.

**The front panel** sees a word cycle once, at its even address: its lamps and LDATA show D0–D7, the even byte, and
its breakpoint matches only the even address ([chapter 12](12-front-panel.md#the-breakpoint)).

Neither the BIOS nor OODOS sets WIDE yet: their copies, the SD card's loads and the screen's port jobs move bytes,
and their cycle counts are as above without it. In Apple II mode the controller is idle, so WIDE has no effect there.

## Registers

Status: **proposed**

Emulated as step B2. The window's registers, `$C400–$C40C`, are the channel's that CONTROL's SELECT names
([`$C47E`](#reg-c47e)). In the window `$C403`, `$C40B` and `$C40D–$C40F` are reserved, and so are `$C413–$C47D`:
they read `$FF`, and writes are ignored.

<a id="reg-c400"></a>
### $C400 DMA_ADDR: the selected channel's address

#### Equates

`DMA`, `DMA_ADDR`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, low byte first, the channel SELECT names. A write sets the start address and the current one; a read
gives the current one.

#### Bits

All: a 24-bit physical address, A0–A23. For a memory-to-memory copy, the source.

#### Reset

`$00 $00 $00`, on every channel.

#### Status

**proposed**

Emulated as step B2.

#### Notes

The address steps up after each byte, or by 2 after a word ([16-bit transfers](#16-bit-transfers)), down with
MODE's DEC, or not at all with HOLD, and wraps within 24 bits. AUTO
puts the start address back at terminal count. The third byte is the whole of A16–A23: revision 1 dropped its top
four bits, so a bank byte of `$10` or more, which reached RAM there, reaches nothing, the aperture (`$E0`) or the
flash (`$F0` on) here.

<a id="reg-c404"></a>
### $C404 DMA_COUNT: the selected channel's count

#### Equates

`DMA_COUNT`

#### Module

8, the DMA controller

#### Access

R/W, 2 bytes, low byte first, the channel SELECT names. A write sets the start count and the current one; a read
gives the bytes left.

#### Bits

All: the bytes to move, 1–65,535, or 0 for 65,536. In bytes with WIDE too: a word takes 2.

#### Reset

`$00 $00`: 65,536 if the channel runs.

#### Status

**proposed**

Emulated as step B2.

#### Notes

A channel that has run out reads 0, and runs 65,536 bytes if it is started again unwritten.

<a id="reg-c406"></a>
### $C406 DMA_MODE: the selected channel's mode

#### Equates

`DMA_MODE`

#### Module

8, the DMA controller

#### Access

R/W, the channel SELECT names.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | IRQ | 1: interrupt at terminal count (IRQH bit 0) |
| 6 | AUTO | 1: at terminal count, reload ADDR, COUNT and DEST and stay armed |
| 5 | HOLD | 1: the address stays still. For memory to memory, the source only: a fill. Transfers bytes, with or without WIDE |
| 4 | DEC | 1: the address steps down. For memory to memory, both. Transfers bytes, with or without WIDE |
| 3–2 | DIR | `%00` card to memory, `%01` memory to card, `%10` memory to memory, `%11` reserved |
| 1–0 | XFER | `%00` single, `%01` demand, `%10` block, `%11` reserved |

#### Reset

`$00`.

#### Status

**proposed**

Emulated as step B2.

#### Notes

As revision 1's. backplane.inc names `DMA_M2M` (`$0A`: block, memory to memory), `DMA_HOLD` (`$20`), `DMA_DEC`
(`$10`), `DMA_MODE_IRQ` (`$80`) and `DMA_DEMAND_IN` (`$01`: demand, card to memory, as the SD card is read).

<a id="reg-c407"></a>
### $C407 DMA_CTRL: the selected channel's control

#### Equates

`DMA_CTRL`

#### Module

8, the DMA controller

#### Access

R/W, the channel SELECT names. The controller clears REQ at terminal count, and ENABLE too unless AUTO is set.
Any write ends a hold-off ([Transfers for a card](#transfers-for-a-card)).

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–2 | | 0 |
| 1 | REQ | write 1: a software request. It starts a memory-to-memory copy, or a block transfer with no card's request |
| 0 | ENABLE | 1: the channel is armed. Writing 0 stops a transfer under way |

#### Reset

`$00`.

#### Status

**proposed**

Emulated as step B2.

#### Notes

As revision 1's. `DMA_ENABLE` (`$01`) and `DMA_GO` (`$03`, ENABLE and REQ) are in backplane.inc. A program waits
for a copy by polling ENABLE until it is 0, with the channel still selected.

<a id="reg-c408"></a>
### $C408 DMA_DEST: the selected channel's destination

#### Equates

`DMA_DEST`

#### Module

8, the DMA controller

#### Access

R/W, 3 bytes, low byte first, as [ADDR](#reg-c400).

#### Bits

All: a 24-bit physical address, the destination of a memory-to-memory copy. A card transfer doesn't use it.

#### Reset

`$00 $00 $00`.

#### Status

**proposed**

Emulated as step B2.

<a id="reg-c40c"></a>
### $C40C DMA_CARD: the module the selected channel serves

#### Equates

`DMA_CARD`

#### Module

8, the DMA controller

#### Access

R/W, the channel SELECT names.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | | 0 |
| 4 | WIDE | 1: 16-bit transfers, where the card takes words ([below](#16-bit-transfers)) |
| 3–0 | MODULE | the module ID whose requests the channel serves, and that a grant puts on PS0–PS3. `$8`, the controller's own, which never asks, is none |

#### Reset

Channel 0 `$07` (mass storage), 1 `$09` (video), 2 `$0A` (sound), 3–7 `$08` (none).

#### Status

**proposed**

Emulated as step B2; WIDE since B4.

#### Notes

backplane.inc names `DMA_NONE` (`$08`), `DMA_WIDE` (`$10`) and the reset modules `DMA_MOD_SD`, `DMA_MOD_VIDEO` and
`DMA_MOD_SOUND`. A channel with MODULE none serves no card: it runs memory to memory, or a block REQ starts. Two
channels may name one module; both then show in REQ, and the one of higher priority is granted first. The BIOS's
Break, and the diagnostics' way out, put every channel's CARD back to its reset value, so a CARD changed under the
monitor can't keep the SD boot from its grant. The BIOS and OODOS write no other CARD. Only memory and the video
card's port and aperture take words, and the BIOS and OODOS set WIDE nowhere
([16-bit transfers](#16-bit-transfers)).

<a id="reg-c410"></a>
### $C410 DMA_STATUS: terminal counts

#### Equates

`DMA_STATUS`

#### Module

8, the DMA controller

#### Access

R. Writing 1s clears those bits.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–0 | TC7–TC0 | 1: that channel has reached terminal count. Write 1 to clear |

#### Reset

`$00`.

#### Status

**proposed**

Emulated as steps B2 and B3.

#### Notes

A TC bit with its channel's MODE IRQ set is what interrupts. `DMA_CH0`–`DMA_CH7` and `DMA_ALL` (`$FF`) name the
bits. Revision 1's requests, its STATUS bits 7–4, are [REQ](#reg-c412) now.

<a id="reg-c411"></a>
### $C411 DMA_ABORT: stop channels

#### Equates

`DMA_ABORT`

#### Module

8, the DMA controller

#### Access

W. Reads `$FF`.

#### Bits

Bit n: a 1 stops channel n at once, clearing its ENABLE and REQ.

#### Reset

Not a register to read.

#### Status

**proposed**

Emulated as steps B2 and B3.

#### Notes

ADDR and COUNT keep the values they stopped at. It needs no SELECT. The BIOS's Break aborts all eight (`DMA_ALL`)
and clears their TC bits and their modes.

<a id="reg-c412"></a>
### $C412 DMA_REQ: the requests at the last poll

#### Equates

`DMA_REQ`

#### Module

8, the DMA controller

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–0 | REQ7–REQ0 | 1: that channel's MODULE was asking at the last poll |

#### Reset

`$00`.

#### Status

**proposed**

Emulated as steps B2 and B3.

#### Notes

The controller sets it at each poll ([Transfers for a card](#transfers-for-a-card)) and keeps it until the next, so
it shows what the last poll found, not the line as it is now. A channel with MODULE none reads 0. With a sound card fitted, its standing request shows in bit 2 after any poll.

<a id="reg-c47e"></a>
### $C47E DMA_CONTROL: the whole controller's control

#### Equates

`DMA_CONTROL`

#### Module

8, the DMA controller

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | | 0: writes ignored |
| 6–4 | BURST | the CPU gets a cycle back every 2^BURST transfers (a byte, or a word with WIDE); 7: no limit |
| 3–1 | SELECT | the channel the window at `$C400–$C40F` shows, 0–7 |
| 0 | ENABLE | 1: the master enable. 0: no channel runs, and no poll |

#### Reset

`$70`: no limit, SELECT 0, and off.

#### Status

**proposed**

Emulated as step B2.

#### Notes

backplane.inc names `DMA_ON` (`$71`: on, no limit, SELECT 0), `DMA_MASTER` (`$01`), `DMA_SEL` (`$0E`, SELECT's
mask) and `DMA_SEL0`–`DMA_SEL7` (n × 2). Code that sets SELECT saves CONTROL first and puts it back after
([the SELECT rule](#the-controller)). The BIOS writes `DMA_ON` in VINIT at boot, with a revision 2 video card, and
leaves it there, so under the monitor CONTROL reads `$71`; the SD boot's read saves it and puts it back, and Break
leaves it as it was. Apple II mode's fills write `DMA_ON` with SELECT 3 (`$77`), and the /RESET that enters the mode
puts back `$70`. BURST limits demand and block transfers and memory-to-memory copies; a single transfer gives a cycle
back after every transfer whatever it says.

<a id="reg-c47f"></a>
### $C47F DMA_ID: the controller's ID

#### Equates

`DMA_ID`

#### Module

8, the DMA controller

#### Access

R.

#### Bits

All: the type, module 8, in bits 7–4, and the revision, 2, in bits 3–0. `DMA_ID_V2`.

#### Reset

`$82`.

#### Status

**proposed**

Emulated as step B2.

#### Notes

The BIOS, OODOS and SYSINFO take `$82` exactly: a revision 1 controller (`$81`) has its registers elsewhere, so it
reads as no controller ([above](#revision-1-replaced-by-revision-2-b2)).

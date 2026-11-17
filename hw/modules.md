# Modules for 6502 computer #

## Chain ##

CPU card (CPU, north bridge, clock, DS3231, DMA controller, ROM A) -> RAM card -> the cards (I/O, storage, video,
sound, USB, Disk II, Console, the cartridge) -> Bus Spy (ESP32)

*(2026-11-17, B6: the "LSD (Level Shift Driver)" element is dropped. The bus is 5 V only and every card meets it at
5 V, so no level shifting is needed between cards; a card with 3.3 V parts, such as the bus spy's ESP32 or an
RP2350 card, shifts its own levels on the card. The ROM is ROM A, on the CPU card; the RAM is one card.)*

## Notes: ##

  - One clock: it is on the CPU card, and there is one backplane and one CPU card, so no clock needs a disable
    switch. *(2026-11-17, B6: was "only 1 active clock per stack", from the stacked-module days.)*
  - The bus carries 5 V only (decided, the owner, 2026-11-14). A card that needs 3.3 V or another voltage regulates
    it from the 5 V on the card. *(Replaces the 62-pin bus's +24 V, which every card regulated down.)*
  - /RESET may be pulled low by _any_ module, and that resets the system. Soft starts should go high after 100ms.
    - use a 100k resistor and 1uf cap to the 5 V rail, clamp /RESET high with a zener diode. *(2026-11-17, B6: the
      note is kept; "V~CC1~" is the bus's 5 V now.)*
  - Each module with registers has an ID (2^4), which the north bridge puts on PS0-PS3. More than one ID can be
    served by a physical card. The ID in most cases sets Chip Enable (CE) or Chip Select (CS) of the target using a
    decoder. Memory (the RAM card, ROM A and ROM B) has no ID: it answers by its address while /MEM is low.


## Bus Pin Out ##

### 98-pin PCIe x8 card edge, keyed, 5 V only (proposed: a draft) ###

**Signals decided; every position is open, the owner's.** The connector is the 98-contact card edge that PCIe x8
slots use (decided, the owner, 2026-11-14), but the bus is the OO-1's own: the slot is shrouded and keyed so that no
other card fits. The shroud, its key and the socket's part number are the owner's. The table below is B6's draft
(2026-11-17): the signal list in it is decided; every pin number, the split between the sides and the placement of
the grounds are open, for the owner's board design.

Dir is seen from the CPU card. OUT: the CPU card drives it. IN: a card drives it. IN/OUT: both. OUT (master): the
master holding the bus drives it, which is the CPU card (the north bridge, or the DMA controller beside it), or the
Console card or the bus spy once it has taken the bus with BE. While the Console card or the bus spy holds BE, the
CPU card lets go of /WORD, /OE, /MEM and PS0-PS3 as well as the address, the data and RWB; a master that makes only
byte cycles, such as the bus spy, may leave /WORD to its pull-up. /IACK and /DMA stay the CPU card's. In the cycle
it pulls BE low, the Console card or the bus spy enables its own drivers, with /MEM and /OE high, RWB a read and
PS0-PS3 `0xE`, its ID. Before it releases BE, it drives /MEM and /OE inactive (high), RWB to read, PS0-PS3 to `0xF`
and /WORD high if it drives /WORD, for at least one cycle, so that the pull-ups only hold lines that are already
high. The DMA controller, on the CPU card, needs no such cycle when it releases BE: the CPU card drives the lines
again at once (B6, review rounds 2 and 3).

| Pin | Name | Dir | Description |
|---|---|---|---|
| A1 | GND | | ground |
| A2 | +5V | | power |
| A3 | +5V | | power |
| A4 | +5V | | power |
| A5 | GND | | ground |
| A6 | /RESET | IN/OUT | reset; open drain, pulled up; any card (the bus spy too) may hold it |
| A7 | /NMI | IN | non-maskable interrupt; open drain, pulled up; the USB card (Pause/Break) and the Console card (its breakpoint) pull it |
| A8 | /IRQ | IN | interrupt request; open drain, pulled up |
| A9 | AMODE | OUT | Apple II mode is active (the north bridge) |
| A10 | GND | | ground |
| A11 | /BOOTSEL | IN | boot from RAM, not ROM (the bus spy); open drain, pulled up |
| key | | | the key, between 11 and 12 |
| A12 | GND | | ground |
| A13-A16 | D0-D3 | IN/OUT | data, the byte lane; weak pull-ups on D0-D15 on the backplane; the interrupt poll answers on D0-D7 (line ID mod 8), the DMA request poll on D(ID) |
| A17 | GND | | ground |
| A18-A21 | D4-D7 | IN/OUT | data, the byte lane |
| A22 | GND | | ground |
| A23-A26 | D8-D11 | IN/OUT | data, the odd byte of a word cycle; the DMA request poll's IDs 8-B (of the two polls, only the DMA request poll uses D8-D15) |
| A27 | GND | | ground |
| A28-A31 | D12-D15 | IN/OUT | data, the odd byte of a word cycle; the DMA request poll's IDs C-F |
| A32 | GND | | ground |
| A33 | RWB | OUT (master) | read/write; pulled up, so a cycle no master drives is a read, with /OE high |
| A34 | /OE | OUT (master) | read strobe; driven by whichever master holds the bus, as it drives RWB; pulled up |
| A35 | /WORD | OUT (master) | low on a word cycle (both lanes, at an even address), high on a byte cycle; pulled up, so a master that never drives it makes byte cycles (the bus spy, which makes only byte cycles, leaves it to the pull-up) |
| A36 | /MEM | OUT (master) | a memory cycle: memory decodes A0-A23 only while it is low; high on an I/O cycle, a poll and the DMA takeover; pulled up |
| A37 | GND | | ground, beside PHI2 |
| A38 | PHI2 | OUT | the CPU clock, 50% duty |
| A39 | GND | | ground, beside PHI2 |
| A40 | RDY | IN/OUT | ready; open drain, pulled up; the DMA controller's and the Console card's only (a slow card uses /WAIT) |
| A41 | BE | IN/OUT | bus enable; open drain, pulled up; low floats the CPU (its address, data and RWB) and the north bridge's address drivers, and the DMA controller, the Console card or the bus spy then drives the bus; the DMA controller (on the CPU card), the Console card and the bus spy pull it |
| A42 | /WAIT | IN | a slow card's wait states; open drain, pulled up; the north bridge turns it into RDY for the CPU, and the DMA controller stretches its grant by it |
| A43 | SYNC | OUT | opcode fetch (single step, the bus spy) |
| A44 | GND | | ground, beside OSC |
| A45 | OSC | OUT | the 14.31818 MHz master clock |
| A46 | GND | | ground, beside OSC |
| A47 | VPB | OUT | vector pull (the north bridge's vector rule) |
| A48 | TC | OUT | terminal count, with a transfer's last byte or word (the DMA controller) |
| A49 | RSV0 | | spare: routed to every slot, no card connects it |
| B1 | GND | | ground |
| B2 | +5V | | power |
| B3 | +5V | | power |
| B4 | +5V | | power |
| B5 | GND | | ground |
| B6 | BR0 | | reserved for a future bridged bus: routed to every slot, no card connects it |
| B7 | BR1 | | reserved, as BR0 |
| B8 | BR2 | | reserved, as BR0 |
| B9 | /DREQ | IN | a card's DMA request; one open-drain line all cards share, pulled up |
| B10 | GND | | ground |
| B11 | RSV1 | | spare, as RSV0 |
| key | | | |
| B12 | GND | | ground |
| B13-B16 | A0-A3 | OUT (master) | address; A0-A6 are also a card's register select |
| B17 | GND | | ground |
| B18-B21 | A4-A7 | OUT (master) | address |
| B22 | GND | | ground |
| B23-B26 | A8-A11 | OUT (master) | address |
| B27 | GND | | ground |
| B28-B31 | A12-A15 | OUT (master) | address |
| B32 | GND | | ground |
| B33-B36 | A16-A19 | OUT (master) | address |
| B37 | GND | | ground |
| B38-B41 | A20-A23 | OUT (master) | address; A20-A23 are new on this bus (the 24-bit map) |
| B42 | GND | | ground |
| B43-B46 | PS0-PS3 | OUT (master) | module ID: the card of the I/O block; `0xD` on the CPU's memory cycle, `0x8` on DMA's own cycles, the card's in a fly-by grant, `0xE` while the Console card or the bus spy holds the bus; pulled up, to `0xF`, the idle bus's ID, never a card's, so a cycle no master drives selects no card's registers |
| B47 | GND | | ground |
| B48 | /IACK | OUT | the north bridge's interrupt poll (with /DMA low too: the DMA controller's request poll); pulled up |
| B49 | /DMA | OUT | the DMA controller's poll and grant cycles; in a grant the card named on PS0-PS3 moves its data register; pulled up |

**The counts.** 67 lines (64 signals and BR0-BR2), 6 +5V, 23 GND and 2 spares: 98 contacts, 49 a side.

| | Side A | Side B | Total |
|---|---|---|---|
| lines (with BR0-BR2) | 33 | 34 | 67 |
| +5V | 3 | 3 | 6 |
| GND | 12 | 11 | 23 |
| spare (RSV0, RSV1) | 1 | 1 | 2 |
| contacts | 49 | 49 | 98 |

**The draft's rules.**
- Power, then the slow lines, in the short segment (1-11): GND, three +5V and GND on each side, then /RESET, /NMI,
  /IRQ, AMODE and /BOOTSEL on side A, and BR0-BR2 and /DREQ on side B. RDY, BE and /WAIT, open drain but acting
  within a cycle, sit beside PHI2.
- Data on side A and address on side B, D0-D15 and A0-A23 in order, so a card routes straight.
- At most four lines between two grounds, on both sides. Every routed line counts, the spares and BR0-BR2 too, so a
  later use of them keeps the rule; a +5V contact doesn't count as a return, and neither does the key (each side
  has a ground at 10 and at 12).
- A ground on either side of PHI2 and of OSC, as the 62-pin bus had beside PHI2.
- What is left is spare: RSV0 and RSV1, routed to every slot, unconnected.
- **The six +5V contacts** carry a slot's whole current. Whether six are enough stands on the chosen connector's
  current rating per contact and the owner's budget for each of the 15 or so slots.

**Pull-ups on the backplane,** so that an empty slot leaves its lines in a known state, and a cycle that no master
drives (a BE handover, between one master letting go and the next driving) selects nothing: RDY, BE, /BOOTSEL,
/RESET, /NMI and /IRQ; /WAIT and /DREQ (open drain); weak ones on D0-D15, for both polls and for an unmapped read
(`$FF`, or `$FFFF` as a word); /WORD, /MEM, /DMA, /IACK and /OE, so that such a cycle selects no memory and makes no
poll; PS0-PS3, to `0xF`, the idle bus's ID, which no card is given, so that no card's registers answer it; and RWB,
so that it is a read, with /OE high. A0-A23 have no pull-ups and float then, which is harmless with nothing selected.
A pull-up alone raises a released line slowly on a loaded passive bus, many cycles at 7.16 MHz, so the Console card
and the bus spy leave none of the lines they drive to the pull-ups. In the cycle it pulls BE low, such a master
enables its own drivers, with /MEM and /OE high, RWB a read and PS0-PS3 `0xE`, its ID. Before it releases BE, it
drives /MEM and /OE inactive (high), RWB to read, PS0-PS3 to `0xF` and /WORD high if it drives /WORD, for at least
one cycle, and the pull-ups then only hold lines that are already high. /IACK and /DMA stay the CPU card's. The DMA
controller's release needs no idle cycle, since the CPU card drives the lines again at once (B6, review rounds 2
and 3).

**From the 62-pin bus.** Every 62-pin signal carries over but DRQ0-DRQ3 and /DACK0-/DACK3 (8 lines, replaced by
/DREQ and /DMA) and +24 V (5 V only). New: D8-D15, A20-A23, /MEM, /DMA, /DREQ, /WAIT, /WORD and BR0-BR2. The
62-pin table is no longer here: it is in design.md,
[the 62-pin bus, the first design's](design.md#the-62-pin-bus-the-first-designs). Signal details are in
[design.md](design.md#backplane-signals) and
[the new bus](design.md#the-new-bus-memory-by-address-dma-by-module-id-one-backplane).


## Modules Id List ##

| ID | Module | Where |
| --- | --- | --- |
| 0x0-0x5 | free (once RAM A-D, ROM A and ROM B; memory is selected by its address on the new bus) | |
| 0x6 | serial: COM1/COM2, LPT, the VIA | a slot |
| 0x7 | mass storage (SD) | a slot |
| 0x8 | the DMA controller (revision 2, ID byte `$82`) | the CPU card |
| 0x9 | video | a slot |
| 0xA | sound (2x SID) | a slot |
| 0xB | USB, game port and speaker | a slot |
| 0xC | Disk II controller | a slot |
| 0xD | the north bridge (registers) | the CPU card |
| 0xE | Console (the front panel) | a slot |
| 0xF | reserved: the idle bus's ID, the value PS0-PS3's pull-ups give a cycle no master drives; never a card's (B6). Once the clock's, which moved onto the CPU card, 2026-10-30 | |
| none | the RAM card, 1 MB at `$000000-$0FFFFF`, by address; RAM A-D name its four 256 KB quarters | a slot |
| none | ROM A, 512 KB at `$F00000`: two 256 KB chips side by side, even and odd bytes, that can be written; the BIOS, and the IIe images in blocks 0-5 (since B5, proposed) | the CPU card |
| none | ROM B, the game cartridge, 512 KB at `$F80000`, two chips as ROM A, erased | a slot |
| none | the bus spy (a bus master, no registers) | a slot |

- **The new bus (98 pins, 2026-11-14):** memory is selected by its address, so IDs 0x0-0x5 (once RAM A-D, ROM A
  and B) are free, six IDs for new cards; 0xF is the idle bus's, never a card's (B6):
  [design.md](design.md#the-new-bus-memory-by-address-dma-by-module-id-one-backplane).
- **Decided (B0, 2026-11-14):** the video card's aperture moves to `$E00000`, so all 1 MB is RAM. The DMA
  controller (revision 2, ID `$82`) grants by the module ID in each channel's CARD register, through one channel
  window chosen by CONTROL's SELECT, and the serial card (0x6) picks its DMA stream with DMASTREAM. The north bridge
  (0xD) is revision 2, ID `$D2`, with the wider windows: [design.md](design.md#the-physical-map-on-the-new-bus).
- **Decided (2026-11-14): one backplane, no expansion chassis.** The backplane has about 14-15 slots, one for each
  card that can exist (at most 13 with IDs, plus the RAM card and the cartridge) and the CPU card, which carries the
  CPU, the north bridge, the clock, the DS3231, the DMA controller and ROM A. It is a passive bus at 7.16 MHz.
  BR0-BR2 are reserved, unassigned, for a future bridged bus with its own ID space. Slow cards ask for wait states
  by /WAIT: [design.md](design.md#one-backplane-the-slots-and-the-reserved-pins).

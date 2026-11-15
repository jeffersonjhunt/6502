# 2 Memory and banking

Physical memory, the four 16 KB windows that map it into the CPU's 64 KB, and the I/O hole.

The design record, for the reasons: [hw/design.md, "Memory"](../../hw/design.md#memory).

## Physical memory

Status: **emulated**

The north bridge turns every CPU access into a 20-bit physical address, A0–A19. The RAM is 1 MB on the backplane,
in four 256 KB modules, RAM A–D (module IDs 0–3). It is 16 banks of 64 KB, or 64 blocks of 16 KB; block n is
physical `n × $4000`, and bank b is blocks 4b to 4b+3.

| Bank | Physical | Blocks | What uses it in native mode |
|---|---|---|---|
| 0 | `$00000–$0FFFF` | 0–3 | the CPU's first 48 KB at reset (blocks 0–2) |
| 1–7 | `$10000–$7FFFF` | 4–31 | free to the system |
| 8 | `$80000–$8FFFF` | 32–35 | the video card's aperture (*proposed*, revision 2): the card answers here, not RAM ([below](#revision-2-bank-8-the-aperture)) |
| 9–D | `$90000–$DFFFF` | 36–55 | free to the system |
| E | `$E0000–$EFFFF` | 56–59 | free; Apple II mode copies the IIe ROM image into it each time it starts, with the zero byte its fills copy from at `$EBFFF` |
| F | `$F0000–$FFFFF` | 60–63 | free, but for block 63: the BIOS's copy of itself, read-only through W3, and under the I/O hole its first 2 KB, `$FC000–$FC7FF`, BIOS 0.13's scroll buffer, reached by DMA alone ([chapter 6](06-video.md#the-bioss-text-screen)) |

Apple II mode uses banks 0–5 and E in its own way, and puts the IIe's character ROM in video card revision 2's own
memory, not in these banks (*proposed*: [chapter 13](13-apple-ii-mode.md)). OODOS's use of the banks is in the
[Programmer's Guide](../oodos-programmers-guide/05-memory-and-banking.md#blocks).

The flash is a second physical space, beside the RAM. ROM A (module 4) is 512 KB, 32 blocks, and holds the BIOS.
ROM B (module 5) is the cartridge, 32 blocks more. A window reaches them with its ROM bit
([below](#flash-and-read-only-windows)).

| ROM A block | What |
|---|---|
| 31, `$7C000` | the BIOS and the Woz Monitor, `$C800–$FFFF` of it (the first 2 KB is under the I/O hole) |
| 30, `$78000` | the 8×16 font, 4 KB |
| 29, `$74000` | the video demo |
| 24–28, `$60000` | the boot splash's samples for the ES5503: wave RAM's image, 80 KB ([chapter 7](07-sound.md)) |
| 21–23, `$54000` | since BIOS 0.14: the ROM's diagnostics, block 23 their core and memory tests, 22 and 21 the card tests ([chapter 15](15-diagnostics.md)) |
| 0–20 | erased (`$FF`) |

The DMA controller reaches the RAM only, by physical address ([chapter 5](05-dma.md)).

### The 24-bit bus

Status: **proposed**

Emulated as step B1 ([hw/design.md, as built](../../hw/design.md#b1-as-built)).

On the new bus ([hw/design.md](../../hw/design.md#the-physical-map-on-the-new-bus)) the physical address is 24 bits,
A0–A23, and memory answers by its address, not by a module ID. Nothing above changes for software: the blocks, the
windows' values and the ROM bit mean what they did. The map underneath them:

| Physical | What answers |
|---|---|
| `$000000–$0FFFFF` | the RAM card, 1 MB: RAM A–D are its quarters. Bank 8, with a revision 2 video card, is sent to the aperture ([below](#revision-2-bank-8-the-aperture)) |
| `$100000–$DFFFFF` | nothing: a read gives `$FF` |
| `$E00000–$E0FFFF` | the video card's aperture, revision 2 |
| `$E10000–$EFFFFF` | nothing, kept for cards' memory |
| `$F00000–$F7FFFF` | ROM A: ROM block *b* (0–31) is `$F00000 + b × $4000` |
| `$F80000–$FFFFFF` | ROM B, the cartridge: blocks 32–63 |

- **A window's ROM bit** makes the north bridge put out the flash's address, `$F00000 + b × $4000`, in place of
  the RAM's.
- **The I/O hole** is an I/O cycle, not a memory one: the bridge holds /MEM high there, so no memory answers it,
  whatever the address lines hold. Module IDs 0–5 and F, which were RAM A–D, ROM A and ROM B, have no card: their
  blocks read `$FF`, as they always have.
- **The console card's** registers stay 20 bits ([chapter 12](12-front-panel.md)): its EXAM and DEP reach
  `$000000–$0FFFFF`, and its breakpoint matches only a cycle with A20–A23 0.

### Revision 2: bank 8, the aperture

Status: **proposed**

Revision 2 of the video card ([chapter 6](06-video.md#revision-2-card-memory)), the card BIOS 0.13 and OODOS drive,
draws from 2 MB of its own memory. Bank 8, blocks 32–35, is its aperture: the north bridge decodes the bank to the
card, and block 32 + *n* is the aperture's slot *n* ([chapter 6](06-video.md#the-aperture)). A window reaches a
slot as it reaches a RAM block, its ROM bit clear. Banks 9–B, blocks 36–47, are ordinary RAM. The machine then has
960 KB of RAM that software can reach, and the aperture's 64 KB.

On the 24-bit bus (*proposed*, [above](#the-24-bit-bus)) the aperture's own place is `$E00000–$E0FFFF`, and while a
revision 2 card is fitted the CPU card sends every memory cycle for bank 8 there: the windows' blocks 32–35 and the
DMA controller's addresses `$80000–$8FFFF` reach the aperture as before. RAM C's bank 8 is then out of the CPU's and
DMA's reach, though the console card, which drives the bus itself, sees it. With revision 1, or no video card, bank
8 is RAM and nothing answers at `$E00000` ([chapter 14](14-emulator-notes.md#the-24-bit-bus)). Apple II mode doesn't use banks 8–B: the card ignores the
aperture in the mode, and the IIe's character ROM is in card memory, not at `$BE000`
([chapter 13](13-apple-ii-mode.md#revision-2-of-the-video-card)).

Revision 1, the old card, had no aperture: banks 8–B, blocks 32–47, were its video memory, ordinary RAM that it
read by watching every write to them ([chapter 6](06-video.md#video-memory)).

## The four windows

Status: **emulated**

The CPU's 64 KB is four windows of 16 KB, W0–W3. Each has a register in the north bridge,
WIN0–WIN3 ([`$C680`–`$C683`](11-north-bridge-and-rtc.md#reg-c680)), that names the block it shows:

| Window | CPU range | Register | At reset |
|---|---|---|---|
| W0 | `$0000–$3FFF` | [WIN0](11-north-bridge-and-rtc.md#reg-c680) | `$00`: RAM block 0 |
| W1 | `$4000–$7FFF` | [WIN1](11-north-bridge-and-rtc.md#reg-c681) | `$01`: RAM block 1 |
| W2 | `$8000–$BFFF` | [WIN2](11-north-bridge-and-rtc.md#reg-c682) | `$02`: RAM block 2 |
| W3 | `$C000–$FFFF` | [WIN3](11-north-bridge-and-rtc.md#reg-c683) | `$DF`: ROM A block 31, read-only; `$3F`, RAM block 63, with /BOOTSEL |

A register's bits 5–0 are the block: its top four bits become A16–A19 and its bottom two replace the CPU's A14–A15.
The CPU's A0–A13 pass straight through. So any 16 KB of the megabyte can be in any window: a program can see the
first 16 KB of the aperture without giving up its zero page and stack.

```
CPU address    A15 A14 | A13 ............. A0
                  |                  |
       WINn, n = A15-A14             |
                  v                  v
physical       BLOCK (6 bits)  | A13 ............. A0
               = A19 ... A14
```

Two windows may show the same block. A write through either is seen through both at once.

The machine wakes up as an ordinary 64 KB computer: RAM at `$0000–$BFFF` and the BIOS at the top. The BIOS copies
itself into RAM block 63 and leaves W3 there ([chapter 3](03-boot.md#reset-and-the-flash)). After that, the map is
RAM blocks 0, 1, 2 and 63.

VEC, a fifth register with the same layout, stands in for W3 while the CPU reads a vector
([chapter 4](04-interrupts.md#vec-and-the-vectors)).

## The I/O hole

Status: **emulated**

`$C000–$C7FF` is the cards' registers, decoded by the north bridge ahead of the windows: it is there whatever the
windows hold. Each module ID owns 128 bytes, at `$C000 + ID × $80`, and the 16 IDs fill the 2 KB exactly. A card
matches its ID on PS0–PS3 and decodes only A0–A6.

| Block | ID | What answers |
|---|---|---|
| `$C000–$C2FF` | 0–5 | nothing: RAM and ROM have no registers |
| `$C300–$C37F` | 6 | the serial card: the 6551 stand-in, and the VIA at `$C340` ([chapter 9](09-serial-and-via.md)) |
| `$C380–$C3FF` | 7 | the mass storage card ([chapter 10](10-storage.md)) |
| `$C400–$C47F` | 8 | the DMA controller ([chapter 5](05-dma.md)) |
| `$C480–$C4FF` | 9 | the video card ([chapter 6](06-video.md)) |
| `$C500–$C57F` | A | the sound card ([chapter 7](07-sound.md)) |
| `$C580–$C5FF` | B | the USB card ([chapter 8](08-usb.md)) |
| `$C600–$C67F` | C | the Disk II card ([chapter 10](10-storage.md#the-disk-ii)) |
| `$C680–$C6FF` | D | the north bridge ([chapter 11](11-north-bridge-and-rtc.md)) |
| `$C700–$C77F` | E | the console card ([chapter 12](12-front-panel.md)) |
| `$C780–$C7FF` | F | nothing |

An address no card answers reads `$FF`, and a write to it is lost. The hole only ever hides the first 2 KB of the
block in W3. A program reaches those 2 KB through another window, or by DMA.

In Apple II mode the hole is the IIe's `$C000–$CFFF` instead, and none of these registers can be reached
([chapter 13](13-apple-ii-mode.md)).

## Flash and read-only windows

Status: **emulated**

- **ROM.** With bit 7 set, a window shows the flash, not RAM: blocks 0–31 are ROM A, and 32–63 are ROM B, the
  cartridge. Code in the flash runs where it is, with nothing copied. A write to the flash through a window is
  ignored.
- **RO.** With bit 6 set, writes through the window are ignored. It guards against stray writes, not deliberate
  ones: a program can clear the bit, or reach the block through another window. The BIOS keeps its copy in block 63
  behind it.

### Reprogramming the flash

Status: **proposed**

The flash is meant to be an SST39SF040, which software can reprogram, so the BIOS could be updated in place without
a programmer ([hw/design.md, "Boot"](../../hw/design.md#boot)). The emulator's flash ignores every write, so it can't
be reprogrammed yet.

## Zero page and low RAM

Status: **emulated**

What the BIOS and the monitor keep in bank 0, from [`backplane.inc`](../../backplane/bios/backplane.inc):

| Address | Who | Notes |
|---|---|---|
| `$00–$09` | the boot's copy pointers and `$C800`'s stub; Apple II mode's way in | free once the monitor is up |
| `$24–$2B` | the Woz Monitor | |
| `$30–$7B` | the video demo | |
| `$E0–$E7` | the text screen (`video.s`) | ECHO (`$C80F`) uses them: a program that prints leaves them alone |
| `$F0–$F6` | the SD card's code (`sd.s`) | |
| `$0100–$01FF` | the stack | Break and BRK start it afresh |
| `$0200–$027F` | the monitor's input line | |
| `$0400–$05FF` | the SD card's sector buffer; the video demo's sprite table | |
| `$1000–$BFFF` | where the SD boot loads `/BOOT.BIN` | [chapter 3](03-boot.md#booting-from-the-sd-card) |

A program under OODOS has a different map: the [Programmer's Guide](../oodos-programmers-guide/README.md) has it.

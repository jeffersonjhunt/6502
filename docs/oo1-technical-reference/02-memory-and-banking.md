# 2 Memory and banking

Physical memory, the four 16 KB windows that map it into the CPU's 64 KB, and the I/O hole.

The design record, for the reasons: [hw/design.md, "Memory"](../../hw/design.md#memory).

## Physical memory

Status: **emulated**

The north bridge turns every CPU access into a 20-bit physical address, A0–A19. The RAM is 1 MB on one card in a
slot, selected by its address, with no module ID; its four 256 KB quarters keep the names RAM A–D. It is 16 banks of 64 KB, or 64 blocks of 16 KB; block n is
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

The flash is a second physical space, beside the RAM. ROM A, on the CPU card, is 512 KB, 32 blocks, and holds the
BIOS and, since B5 (*proposed*), the Apple IIe ROM images and their character sets. ROM B is the game cartridge
only, 32 blocks more, and the BIOS takes nothing from it. Neither has a module ID: both answer by their address
([below](#the-24-bit-bus)). A window reaches them with its ROM bit ([below](#flash-and-read-only-windows)).

| ROM A block | What |
|---|---|
| 31, `$7C000` | the BIOS and the Woz Monitor, `$C800–$FFFF` of it (the first 2 KB is under the I/O hole) |
| 30, `$78000` | the 8×16 font, 4 KB |
| 29, `$74000` | the video demo |
| 24–28, `$60000` | the boot splash's samples for the ES5503: wave RAM's image, 80 KB ([chapter 7](07-sound.md)) |
| 21–23, `$54000` | since BIOS 0.14: the ROM's diagnostics, block 23 their core and memory tests, 22 and 21 the card tests ([chapter 15](15-diagnostics.md)) |
| 6–20 | erased (`$FF`) |
| 4–5, `$10000` | since B5 (*proposed*): the IIe character sets, 8 KB each, set *n* at `$10000 + $2000 × n` for image *n*; an erased set (its first 8 bytes `$FF`) is made from the font ([chapter 13](13-apple-ii-mode.md)) |
| 0–3, `$00000` | since B5 (*proposed*): the Apple IIe ROM images 0–3, 16 KB each, image *n* in block *n* as the ROM sits at `$C000–$FFFF`; written only by [the flash update](03-boot.md#the-flash-update) (or, in the emulator, `--apple-rom` and Settings) |

The DMA controller reaches memory by physical address, not through the windows ([chapter 5](05-dma.md)). Since B2
(*proposed*) its ADDR and DEST are 24 bits, so it reaches every memory address in the map below: the RAM, the
aperture and the flash.

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
| `$F00000–$F7FFFF` | ROM A: ROM block *b* (0–31) is `$F00000 + b × $4000`; two chips, the even bytes in one and the odd in the other, and a write (with RO clear) reaches the chip of its address's parity ([below](#reprogramming-the-flash)) |
| `$F80000–$FFFFFF` | ROM B, the game cartridge: blocks 32–63, two chips as ROM A |

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
| `$C000–$C2FF` | 0–5 | nothing: free IDs, no card (once RAM and ROM, which answer by address since B1) |
| `$C300–$C37F` | 6 | the serial card: the 6551 stand-in, and the VIA at `$C340` ([chapter 9](09-serial-and-via.md)) |
| `$C380–$C3FF` | 7 | the mass storage card ([chapter 10](10-storage.md)) |
| `$C400–$C47F` | 8 | the DMA controller ([chapter 5](05-dma.md)) |
| `$C480–$C4FF` | 9 | the video card ([chapter 6](06-video.md)) |
| `$C500–$C57F` | A | the sound card ([chapter 7](07-sound.md)) |
| `$C580–$C5FF` | B | the USB card ([chapter 8](08-usb.md)) |
| `$C600–$C67F` | C | the Disk II card ([chapter 10](10-storage.md#the-disk-ii)) |
| `$C680–$C6FF` | D | the north bridge ([chapter 11](11-north-bridge-and-rtc.md)) |
| `$C700–$C77F` | E | the console card ([chapter 12](12-front-panel.md)) |
| `$C780–$C7FF` | F | nothing: the idle bus's ID, never a card's ([chapter 1](01-overview.md#slots-and-module-ids)) |

An address no card answers reads `$FF`, and a write to it is lost. The hole only ever hides the first 2 KB of the
block in W3. A program reaches those 2 KB through another window, or by DMA.

In Apple II mode the hole is the IIe's `$C000–$CFFF` instead, and none of these registers can be reached
([chapter 13](13-apple-ii-mode.md)).

## Flash and read-only windows

Status: **emulated**

- **ROM.** With bit 7 set, a window shows the flash, not RAM: blocks 0–31 are ROM A, and 32–63 are ROM B, the
  game cartridge. Code in the flash runs where it is, with nothing copied. A write to the flash through a window
  with RO set is ignored. With RO clear it reaches the chip of its address's parity, which takes it as part of a
  command ([below](#reprogramming-the-flash)); a lone write outside a command changes nothing (since B5, emulated;
  before B5 the emulator ignored every write to the flash).
- **RO.** With bit 6 set, writes through the window are ignored. It guards against stray writes, not deliberate
  ones: a program can clear the bit, or reach the block through another window. The BIOS keeps its copy in block 63
  behind it.

### Reprogramming the flash

Status: **proposed**

Emulated as step B5 ([hw/design.md, as built](../../hw/design.md#b5-as-built)).

ROM A is two 256 KB flash chips side by side (SST39SF020A parts), one holding the even bytes and one the odd, so a
word cycle reads both at once ([hw/design.md, "The flash"](../../hw/design.md#the-flash-rom-a-and-rom-b)). ROM B is
built the same way. Software reprograms them through a window with the ROM bit set and RO clear, one chip at a time
or both at once:

- **Each chip has its own commands.** A chip takes a command as a short sequence of writes to two of its own
  addresses, `$5555` and `$2AAA` in the chip. Chip address *c* is flash offset 2*c* in the even chip and 2*c* + 1
  in the odd one, so the even chip's two are offsets `$AAAA` and `$5554` from ROM A's start, and the odd chip's
  `$AAAB` and `$5555`. A write that doesn't fit the sequence puts that chip back to reading its array; a read
  doesn't. The other chip doesn't see the write at all.
- **A byte** is programmed by the unlock pair (`$AA` at the chip's `$5555`, `$55` at its `$2AAA`), `$A0` at its
  `$5555`, then the byte at its address. Programming can only clear bits: the new byte is the old one ANDed with
  the data, so a byte is erased before it is written.
- **The smallest erase is 8 KB.** A chip's sector is 4 KB of its own, which holds the even or the odd bytes of
  8 KB of the flash, so an 8 KB unit is one sector erase in each chip at the same chip address, and a 16 KB block
  is four. A sector erase is the unlock pair, `$80`, the unlock pair again, then `$30` at an address in the sector.
  A chip erase (`$10` in place of `$30`, at `$5555`) clears the whole chip, the BIOS's half with it: the BIOS never
  sends one.
- **Each chip's own status.** While a chip programs or erases, a read of it at an address of its parity gives its
  status, not its array: bit 7 is the inverse of the byte's bit 7 (0 during an erase), and bit 6 changes on every
  read. Two reads that agree mean it has finished. A busy chip ignores writes; the other chip works as usual, so the
  two can work at once.
- **The word buffer.** The CPU card may serve a read from a word it fetched before, but only through a window with
  RO set: a busy chip's status changes from read to read, so the poll goes through a window with RO clear, where
  every read is a bus cycle.

The BIOS writes only the IIe images and their sets, ROM A's blocks 0–5, by its
[flash update](03-boot.md#the-flash-update); the rest of ROM A, the BIOS with it, is the build's, written by a
programmer from the build's two files, `bios-even.rom` and `bios-odd.rom`, one for each chip.

**In the emulator** the two chips follow these sequences, with each chip's own pending state, busy time and status
([chapter 14](14-emulator-notes.md#the-flash)).

## Zero page and low RAM

Status: **emulated**

What the BIOS and the monitor keep in bank 0, from [`backplane.inc`](../../backplane/bios/backplane.inc):

| Address | Who | Notes |
|---|---|---|
| `$00–$09` | the boot's copy pointers and `$C800`'s stub; Apple II mode's way in | free once the monitor is up |
| `$10–$21` | [the flash update](03-boot.md#the-flash-update), while it runs (since BIOS 0.16) | |
| `$24–$2B` | the Woz Monitor | |
| `$30–$7B` | the video demo | |
| `$E0–$E7` | the text screen (`video.s`) | ECHO (`$C80F`) uses them: a program that prints leaves them alone |
| `$F0–$FB` | the SD card's code (`sd.s`) | `$F0–$F6` before BIOS 0.16 |
| `$0100–$01FF` | the stack | Break and BRK start it afresh |
| `$0200–$027F` | the monitor's input line | |
| `$0280–$029A` | the flash update's file name and commit bytes, while it runs (since BIOS 0.16) | |
| `$0400–$05FF` | the SD card's sector buffer; the video demo's sprite table | |
| `$1000–$BFFF` | where the SD boot loads `/BOOT.BIN` | [chapter 3](03-boot.md#booting-from-the-sd-card) |

A program under OODOS has a different map: the [Programmer's Guide](../oodos-programmers-guide/README.md) has it.

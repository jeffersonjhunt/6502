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
| 8–B | `$80000–$BFFFF` | 32–47 | video memory, which the video card reads ([chapter 6](06-video.md)) |
| C–D | `$C0000–$DFFFF` | 48–55 | free to the system |
| E | `$E0000–$EFFFF` | 56–59 | free; Apple II mode copies the IIe ROM image into it each time it starts |
| F | `$F0000–$FFFFF` | 60–63 | free, but for block 63: the BIOS's copy of itself, read-only through W3 |

Apple II mode uses banks 0–5 and E in its own way ([chapter 13](13-apple-ii-mode.md)). OODOS's use of the banks is
in the [Programmer's Guide](../oodos-programmers-guide/05-memory-and-banking.md#blocks).

The flash is a second physical space, beside the RAM. ROM A (module 4) is 512 KB, 32 blocks, and holds the BIOS.
ROM B (module 5) is the cartridge, 32 blocks more. A window reaches them with its ROM bit
([below](#flash-and-read-only-windows)).

| ROM A block | What |
|---|---|
| 31, `$7C000` | the BIOS and the Woz Monitor, `$C800–$FFFF` of it (the first 2 KB is under the I/O hole) |
| 30, `$78000` | the 8×16 font, 4 KB |
| 29, `$74000` | the video demo |
| 0–28 | erased (`$FF`) |

The DMA controller reaches the RAM only, by physical address ([chapter 5](05-dma.md)).

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
first 16 KB of video memory without giving up its zero page and stack.

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

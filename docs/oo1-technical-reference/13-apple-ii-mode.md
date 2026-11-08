# 13 Apple II mode

The mode that runs an enhanced Apple IIe's software unchanged: how to enter it, what it maps, and what it leaves
out.

The design record, for the reasons: [hw/design.md, "Apple II Mode"](../../hw/design.md#apple-ii-mode-1).

In Apple II mode the OO-1 is an enhanced IIe with the extended 80-column card: 128 KB, main and auxiliary memory,
the language card, the IIe's soft switches and display modes, and a Disk II in slot 6. The north bridge does the
IIe's memory management, the USB card is its keyboard, game port and speaker, and the video card draws its display.
The IIe's ROM is the owner's, from the cartridge. Apple's addresses aren't
[`backplane.inc`](../../backplane/bios/backplane.inc)'s equates, so this chapter has no register entries: the
switches are in tables, by the identifiers Apple software knows them by.

## Entering Apple II mode

Status: **emulated**

Native code enters the mode by writing 1 to MODE's APPLE bit ([`$C685`](11-north-bridge-and-rtc.md#reg-c685)). The
north bridge switches to the IIe's map and clock at once and pulses /RESET; the CPU then starts at the IIe ROM's
reset vector, as a IIe does at power-on. APPLE only sets, and once it is set MODE can't be reached.

Nothing should write MODE but the BIOS's `$C806` ([chapter 3](03-boot.md#the-bioss-entry-points)), which sets up the
IIe's memory first. There are two ways to it:

- **From the monitor:** `C806R`.
- **From OODOS:** function 75 with A = 1 ([`O_ROM`](../oodos-programmers-guide/07-function-reference.md#bdos-75)),
  which the CCP's [APPLE2](../oodos-users-guide/04-built-in-commands.md#util-apple2) calls. OODOS checks for a IIe ROM
  itself and writes its changed buffers back. Then, with interrupts off, it puts windows 0–2 on RAM blocks 0–2 and
  W3 and VEC on the BIOS's RAM copy, and jumps to `$C806`.

In the WebUI, Settings' IIe ROM row has **Start Apple IIe**, which does what someone at the machine would: START,
the hard reset, back to the BIOS whatever was running, then `C806R` typed at the monitor. Adding or ejecting any of
Apple II mode's ROM parts in Settings plugs in a new cartridge, which power-cycles the machine; in Apple II mode the
WebUI asks first, since the IIe's program would be lost.

### What the BIOS's entry does

Status: **emulated**

`APPLE` (`backplane/bios/apple.s`) checks for what it needs, and goes back to the monitor if either is missing:

<!-- listing: ../../backplane/bios/apple.s:50-61 -->
```
APPLE:
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BEQ     @dma
                JMP     NODMA_SAY
@dma:           LDA     #CART
                STA     NB_WIN1
                LDA     CART_CPU + IIE_ID_AT ; $FBB3 in the image: a IIe's?
                CMP     #IIE_ID
                BEQ     @rom
                JMP     NOROM_SAY
@rom:
```

- **The DMA controller** must answer with its ID (`DMA_ID_V1`). Without it the BIOS prints `APPLE II MODE NEEDS THE
  DMA CONTROLLER`.
- **A IIe ROM** must be in the cartridge: 16 KB at its start (flash block 32, `CART_BLOCK`), as the ROM sits at
  `$C000–$FFFF`. The byte at its `$FBB3` must be `$06` (`IIE_ID`). Otherwise the BIOS prints `NO APPLE IIE ROM IN
  THE CARTRIDGE`.

Then, in order:

1. **The ROM image.** It copies the cartridge's 16 KB, by the CPU, to RAM block 59: `$EC000–$EFFFF`, bank E's
   `$C000–$FFFF`.
2. **The character ROM.** It puts 2 KB at `$BE000` in video memory, for the video card's Apple text. If the
   cartridge's `$84000` (flash block 33) holds anything (its first 8 bytes aren't all `$FF`), it copies the 2 KB
   from there. If not, it makes a set from the BIOS's VGA font, so Apple text still shows: each glyph's 16 rows
   ORed in pairs to 8, its 7 left dots reversed so that bit 0 is the leftmost, and the codes from `$80` up stored
   inverted, lit where a bit is 0. In the emulator the cartridge's character ROM comes from `--apple-chars` (a 4 or
   8 KB file) or the WebUI's Settings.
3. **Interrupts off** (SEI), and a zero byte at `$EBFFF`, the top of block 58.
4. **The clear,** by DMA, from that zero byte held as the source: banks 0–3 (four fills of 64 KB), then
   `$D000–$DFFF` of banks 4 and 5 (two of 4 KB). Before the first, it aborts every channel, clears their STATUS
   bits and writes `DMA_ON` (`$71`) to CONTROL, so the fills have the bus with no burst limit. Each fill is a block
   transfer on channel 3, done before the next instruction. The first fill wipes bank 0, the BIOS's own stack and
   zero page with it, so this part runs inline and never returns. apple.s reckons the clear at about 75 ms.
5. **MODE:**

<!-- listing: ../../backplane/bios/apple.s:132-134 -->
```
                LDA     #NB_MODE_APPLE
                STA     NB_MODE        ; Apple II mode: the north bridge pulses /RESET
@reset:         BRA     @reset         ; ...which takes the CPU from here
```

The loop waits for the pulse. In the emulator the pulse comes as soon as the `STA` ends, so the loop never runs.

The rest of memory is left as it was: banks 6–D and F, but for the character ROM at `$BE000–$BE7FF`; bank E below
`$EBFFF`, the zero byte and the ROM image above it; and banks 4 and 5 outside `$D000–$DFFF`.

### The /RESET pulse

Status: **emulated**

The pulse that enters the mode is the same /RESET as Ctrl-Reset's ([below](#ctrl-reset-and-the-hard-reset)). It
isn't a hard reset: APPLE survives it. What it resets:

- **The north bridge:** the windows and VEC to their reset values (the mode doesn't use them), the wait states to 0,
  TICK and TIE cleared ([chapter 11](11-north-bridge-and-rtc.md)), and the IIe's switches to the state below. The
  real-time clock keeps its time.
- **The DMA controller:** every channel cleared and CONTROL back to `$70`, master enable off, so it stays idle
  ([chapter 5](05-dma.md#apple-ii-mode)).
- **Every card but the 6551 stand-in:** the SD, video, sound, USB and console cards and the VIA go back to their
  /RESET state, their interrupt enables off. The 6551 keeps its CMD, so it can still interrupt
  ([chapter 4](04-interrupts.md#in-apple-ii-mode)). The USB card's KBCFG is cleared, so Pause/Break does nothing in
  the mode ([chapter 4](04-interrupts.md#pausebreak)).
- **The Disk II card:** its latch, through the IIe's switches
  ([chapter 10](10-storage.md#the-sequencer-and-the-drives)).
- **The CPU:** it takes its reset vector through the IIe's map. The language card reads ROM after /RESET, so the
  vector is the ROM image's, at `$EFFFC`.

The pulse doesn't touch memory.

The IIe's switches after /RESET:

| Switch | State |
|---|---|
| 80STORE, RAMRD, RAMWRT, ALTZP | off: main memory everywhere |
| INTCXROM, SLOTC3ROM, INTC8ROM | off: the slots' ROMs, but the internal ROM at `$C300` |
| 80COL, ALTCHARSET | off |
| TEXT | on; MIXED, PAGE2 and HIRES off |
| AN0–AN2 | off |
| AN3 | on, so the double modes are off |
| IOUDIS | on |
| The language card | `$D000` bank 2, reading ROM and writing RAM |

## The Apple's memory map

Status: **emulated**

In the mode A0–A15 pass through as they are, and the north bridge supplies A16–A19 from the IIe's switches: each
Apple address is the same address in one bank or another. The windows and VEC aren't used, and the vectors come
through the same map as everything else.

| CPU | Goes to |
|---|---|
| `$0000–$01FF` | bank 0, or bank 1 with ALTZP on |
| `$0200–$BFFF` | bank 0 or bank 1: RAMRD picks for reads and RAMWRT for writes, but for 80STORE's pages (below) |
| `$C000–$C0FF` | the soft switches and the slots' I/O ([below](#the-soft-switches)) |
| `$C100–$CFFF` | the ROM space: the internal ROM from bank E, or the slots' ROMs ([below](#the-rom-space)) |
| `$D000–$FFFF` | the language card: RAM in banks 2–5, or the ROM image in bank E ([below](#the-language-card)) |

The physical banks, as the mode uses them:

| Bank | Physical | In Apple II mode |
|---|---|---|
| 0 | `$00000–$0FFFF` | main memory, `$0000–$BFFF` |
| 1 | `$10000–$1FFFF` | auxiliary memory, `$0000–$BFFF` |
| 2 | `$20000–$2FFFF` | the main language card: `$D000–$DFFF` bank 2 and `$E000–$FFFF`, at their own addresses |
| 3 | `$30000–$3FFFF` | the auxiliary language card, the same |
| 4 | `$40000–$4FFFF` | the main language card's `$D000–$DFFF` bank 1, at `$4D000` |
| 5 | `$50000–$5FFFF` | the auxiliary language card's `$D000–$DFFF` bank 1, at `$5D000` |
| B | `$B0000–$BFFFF` | the character ROM, 2 KB at `$BE000` in video memory |
| E | `$E0000–$EFFFF` | the IIe ROM image, `$EC000–$EFFFF` |

Of banks 0 and 1 only `$0000–$BFFF` is addressed, and of banks 2 and 3 only `$D000–$FFFF`, though the BIOS clears
them whole. The other banks aren't reached.

### Main and auxiliary memory

Status: **emulated**

These switches are written: a write to the even address turns one off, to the odd one on. A read of
`$C000–$C00F` is the keyboard instead ([below](#the-keyboard)).

| Write | Switch | On |
|---|---|---|
| `$C000`, `$C001` | 80STORE | PAGE2 picks the bank for the display pages, not RAMRD and RAMWRT |
| `$C002`, `$C003` | RAMRD | reads of `$0200–$BFFF` from bank 1 |
| `$C004`, `$C005` | RAMWRT | writes to `$0200–$BFFF` to bank 1 |
| `$C008`, `$C009` | ALTZP | `$0000–$01FF`, and the language card, in the auxiliary banks |

With 80STORE on, PAGE2 ([`$C054`, `$C055`](#the-display)) picks the bank for `$0400–$07FF`, reads and writes alike:
off bank 0, on bank 1. With HIRES on as well, it picks for `$2000–$3FFF` too. PAGE2 then no longer picks the
display's page: the display shows page 1.

### The language card

Status: **emulated**

The sixteen addresses `$C080–$C08F` set the language card, on any access, read or write:

- **Bit 3** picks which RAM answers at `$D000–$DFFF`: 0 bank 2, 1 bank 1.
- **Bits 0–1** pick what reads see: RAM for `%00` and `%11`, the ROM for `%01` and `%10`.
- **Writing.** An even address turns writing off. Two reads of odd addresses, with no even address and no write to
  `$C080–$C08F` between them, turn it on. A write to an odd address only starts the count again; it leaves writing
  as it was.

The RAM is in physical banks 2–5:

| CPU | Main (ALTZP off) | Auxiliary (ALTZP on) |
|---|---|---|
| `$D000–$DFFF`, bank 2 | `$2D000–$2DFFF` | `$3D000–$3DFFF` |
| `$D000–$DFFF`, bank 1 | `$4D000–$4DFFF` | `$5D000–$5DFFF` |
| `$E000–$FFFF` | `$2E000–$2FFFF` | `$3E000–$3FFFF` |

Reading the ROM, `$D000–$FFFF` reads the image at `$ED000–$EFFFF`. Reads and writes are picked apart: with the ROM
read and writing on, a write goes to the RAM under it. With writing off a write is lost. Nothing in the mode can
write the image.

hw/design.md says an even address, "or a write to an odd one", clears the write enable
([`hw/design.md:223`](../../hw/design.md#apple-ii-mode)). In the emulator a write to an odd address clears only the
count of reads (`emulator/components/devices/apple_io.cpp:77`); writing, once on, stays on until an even address.

### The ROM space

Status: **emulated**

`$C100–$CFFF` is read from the internal ROM, the image at `$EC100–$ECFFF`, or from a slot's ROM, as three switches
say. Writes here change nothing in memory.

| Write | Switch | On |
|---|---|---|
| `$C006`, `$C007` | INTCXROM | the internal ROM for all of `$C100–$CFFF` |
| `$C00A`, `$C00B` | SLOTC3ROM | slot 3's ROM at `$C300–$C3FF`, not the internal ROM |

| CPU | INTCXROM off | INTCXROM on |
|---|---|---|
| `$C100–$C2FF`, `$C400–$C5FF`, `$C700–$C7FF` | the slot's ROM: none, so `$FF` | internal |
| `$C300–$C3FF` | internal; with SLOTC3ROM on, slot 3's: `$FF` | internal |
| `$C600–$C6FF` | the Disk II's boot ROM ([chapter 10](10-storage.md#in-apple-ii-mode-slot-6)) | internal |
| `$C800–$CFFF` | internal with INTC8ROM on, else `$FF` | internal |

INTC8ROM has no address of its own. Any access to `$C300–$C3FF` with SLOTC3ROM off turns it on, and any access to
`$CFFF` turns it off. It can't be read back.

## The soft switches

Status: **emulated**

`$C000–$C0FF`, in sixteen-byte groups:

| Addresses | What |
|---|---|
| `$C000–$C00F` | read: the keyboard. Write: the memory and ROM switches above, and 80COL and ALTCHARSET |
| `$C010–$C01F` | read `$C010`, or write any of them: clear the keyboard strobe. Read `$C011–$C01F`: the status reads |
| `$C020–$C02F` | nothing (the cassette output): reads `$FF` |
| `$C030–$C03F` | the speaker |
| `$C040–$C04F` | nothing (the utility strobe): reads `$FF` |
| `$C050–$C05F` | the display switches and the annunciators |
| `$C060–$C06F` | the cassette input, the buttons and the paddles; `$C068–$C06F` are `$C060–$C067` again |
| `$C070–$C07F` | start the paddle timers; `$C07E` and `$C07F` are IOUDIS and DHIRES |
| `$C080–$C08F` | the language card |
| `$C090–$C0DF` | slots 1–5's I/O: nothing, reads `$FF` |
| `$C0E0–$C0EF` | slot 6, the Disk II's switches ([chapter 10](10-storage.md#in-apple-ii-mode-slot-6)) |
| `$C0F0–$C0FF` | slot 7's I/O: nothing, reads `$FF` |

The display switches, the language card's, the speaker and the paddle timers work on a read as well as a write, and
a read of them returns `$FF`.

### The keyboard

Status: **emulated**

The keyboard is the USB card's KEY ([chapter 8](08-usb.md#reg-c580)), which holds one character with its strobe,
as the IIe's latch does. The card cooks keys the Apple's way, with Caps Lock on at power-up
([chapter 8](08-usb.md#the-keyboard)).

- **`$C000–$C00F`, read:** KEY: the character in bits 0–6, the strobe in bit 7, 1 while a new character waits.
- **`$C010`, read:** clears the strobe. It reads the character in bits 0–6, and in bit 7 whether any key is held
  down. A write to any of `$C010–$C01F` clears the strobe too.
- **Open Apple and Closed Apple** are the left and right GUI or Alt keys. They read as buttons 0 and 1
  ([below](#the-game-port-and-the-speaker)).

### The game port and the speaker

Status: **emulated**

These are the USB card's game port and speaker ([chapter 8](08-usb.md#the-game-port)), at the IIe's addresses. Each
read gives its answer in bit 7, the other bits 0.

| Address | Bit 7 |
|---|---|
| `$C060` | the cassette input: always 0 |
| `$C061` | button 0, or Open Apple: 1 pressed |
| `$C062` | button 1, or Closed Apple |
| `$C063` | button 2 |
| `$C064–$C067` | paddle 0–3's timer: 1 while it runs |

- **The paddle timers** start on any access to `$C070–$C07D`, the card's PTRIG. Each runs for about 11 µs per count
  of its paddle's setting, in real time, so the IIe's counting loop reads it as on a IIe at this clock
  ([below](#the-clock)).
- **The speaker** flips on any access to `$C030–$C03F`, the card's SPKR. In the WebUI it plays mixed with the SIDs.

The emulator's apps have no control for the paddles or the buttons yet: the paddles read as 0, so their timers end
at once, and buttons 0 and 1 are pressed only by the Apple keys.

### The status reads

Status: **emulated**

A read of `$C011–$C01F` gives a switch in bit 7, 1 for on, and the keyboard's character, KEY's bits 0–6, in bits
0–6.

| Read | Bit 7 |
|---|---|
| `$C011` | the language card's `$D000` bank 2 is picked |
| `$C012` | the language card reads RAM |
| `$C013` | RAMRD |
| `$C014` | RAMWRT |
| `$C015` | INTCXROM |
| `$C016` | ALTZP |
| `$C017` | SLOTC3ROM |
| `$C018` | 80STORE |
| `$C019` | 0 in vertical blank, 1 while the picture is drawn |
| `$C01A` | TEXT |
| `$C01B` | MIXED |
| `$C01C` | PAGE2 |
| `$C01D` | HIRES |
| `$C01E` | ALTCHARSET |
| `$C01F` | 80COL |

**The vertical blank** at `$C019` is counted in CPU cycles, in the IIe's frame: 65 cycles a line, 262 lines,
17,030 cycles a frame, with lines 192–261 blank. The count runs from power-on, not from entering the mode. It isn't
the video card's own frame, which is VGA's 60 Hz, and the two drift against each other.

hw/design.md has `$C019` driven by the video card, in step with the Apple timing
([`hw/design.md:217`](../../hw/design.md#apple-ii-mode)). The emulator computes it from the bus's cycle count in
the north bridge's Apple I/O (`emulator/components/devices/apple_io.cpp:25`), as the same section's later list
says ([`hw/design.md:227`](../../hw/design.md#apple-ii-mode)).

## The display

Status: **emulated**

The video card draws the IIe's display from banks 0 and 1, which it reads directly, as the switches say
([chapter 6](06-video.md#apple-ii-mode)). Each of the Apple's lines is drawn twice: a dot of a 280-dot mode is 2×2
pixels, and one of the 80-column or double modes, 560 dots across, is 1×2. The 560×384 picture sits in the middle
of the 640×480 screen, on black. The card's own layers and registers aren't used.

| Access | Switch | On |
|---|---|---|
| `$C050`, `$C051` | TEXT | text |
| `$C052`, `$C053` | MIXED | with graphics, the last four text rows at the bottom |
| `$C054`, `$C055` | PAGE2 | page 2: `$0800` for text and lo-res, `$4000` for hi-res; with 80STORE, the bank instead |
| `$C056`, `$C057` | HIRES | hi-res, not lo-res |
| `$C058–$C05D` | AN0–AN2 | kept; nothing uses them |
| `$C05E`, `$C05F` | AN3 | off: the double modes, with 80COL |

And, written:

| Write | Switch | On |
|---|---|---|
| `$C00C`, `$C00D` | 80COL | 80-column text; with AN3 off, double lo-res and double hi-res |
| `$C00E`, `$C00F` | ALTCHARSET | codes `$40–$7F` shown as they are; off, they flash |

`$C07F` reads DHIRES in bit 7, 1 when AN3 is off. `$C07E` reads IOUDIS; a write to `$C07E` turns it on and one to
`$C07F` off. The emulator keeps IOUDIS, and nothing else uses it.

What the card draws:

- **Text,** 40 columns from bank 0, or 80 from banks 1 and 0 in turn, column 0 from bank 1. White on black, from
  the character ROM at `$BE000`. With ALTCHARSET off, codes `$40–$7F` flash between inverse and normal, about twice
  a second.
- **Lo-res,** 40×48 blocks of the 16 colours, two to a byte.
- **Hi-res,** 280 dots a line, coloured by the simple NTSC artifact rule: a lone dot violet or green by its column,
  blue or orange when its byte's bit 7 is set, two together white.
- **Double lo-res,** 80 blocks across, from banks 1 and 0 in turn, and **double hi-res,** 560 dots from bytes of
  banks 1 and 0 in turn, each 4 dots one of the 16 colours.

Page 2 is shown only with 80STORE off. The colours and their rules are in
[hw/design.md, "Apple II mode"](../../hw/design.md#apple-ii-mode).

## The clock

Status: **emulated**

The mode's clock is the 14.31818 MHz crystal divided by 14, with every 65th cycle stretched to 16 crystal periods:
65 cycles in 912 periods, a scan line, 1,020,484 Hz on average ([chapter 1](01-overview.md#clocks)). The north
bridge picks it with APPLE.

**In the emulator** the mode fixes the clock at 1,020,484 Hz (`APPLE_HZ`), whatever the app was set to, and the
cards are told it, so the paddle timers, the speaker, the Disk II's sequencer and the real-time clock keep real
time at it. The WebUI's clock buttons show 1.02 MHz and can't be changed in the mode. The terminal app's `--clock`
is overridden, `--clock 0` too: the mode always runs at the Apple's pace.

### The long cycle

Status: **proposed**

hw/design.md's divider makes every 65th cycle long, so that each line is 65 cycles in 912 crystal periods
([`hw/design.md:55`](../../hw/design.md#cpu-and-clocks)). The emulator spaces its cycles evenly at the average
rate instead (`emulator/components/devices/include/backplane_bridge.h:163`). Code that counts cycles sees the same
65 a line and 17,030 a frame; only timing finer than a cycle could tell. The Disk II's sequencer is 0.2% off for
it, well inside a drive's tolerance ([chapter 10](10-storage.md#the-sequencer-and-the-drives)).

## The Disk II

Status: **emulated**

Slot 6 holds the Disk II card, module C, as a IIe's disk controller: its switches at `$C0E0–$C0EF` and its 256-byte
boot ROM at `$C600–$C6FF` while INTCXROM is off. Its two PROMs, the boot ROM and the sequencer, are the owner's.
Without a boot ROM the page reads `$FF`, and the IIe finds no disk controller and starts BASIC. The sequencer runs
its PROM as the chip is wired, two steps a CPU cycle, and two drives take `.woz`, `.dsk`, `.do` and `.po` images.

[Chapter 10](10-storage.md#the-disk-ii) has the card: [its switches](10-storage.md#in-apple-ii-mode-slot-6),
[the sequencer and the drives](10-storage.md#the-sequencer-and-the-drives), and
[the disk images](10-storage.md#disk-images). In the terminal app, `--disk2-rom BOOT,SEQUENCER` fits the PROMs and
`--disk` puts an image in drive 1; the WebUI takes the PROMs in its Settings.

The card is native mode's as well, with the same sixteen switches at `$C600–$C60F` and no boot ROM
([In native mode](10-storage.md#in-native-mode)). The disks stay in their drives across the change of mode.

## Ctrl-Reset and the hard reset

Status: **emulated**

**Ctrl-Reset** is Ctrl-Delete on the USB keyboard ([chapter 8](08-usb.md#pausebreak-and-ctrl-reset)). The USB card
pulses /RESET, and everything [above](#the-reset-pulse) is reset as it was on entering: the switches back to their
reset state, and the CPU at the IIe ROM's reset vector. APPLE survives, and memory is kept, so the IIe ROM's own
reset code decides what comes next. The key gives no character.

**Only a hard reset leaves the mode:** the reset button, the console card's START
([chapter 12](12-front-panel.md)), or a power cycle. The north bridge clears APPLE and the machine starts in native
mode, from the flash ([chapter 3](03-boot.md#reset-and-the-flash)). Nothing in the mode can leave it:

- MODE, with the rest of the north bridge's registers, is out of reach, and writing 0 to APPLE wouldn't clear it.
- Pause/Break does nothing: the pulse that entered the mode cleared KBCFG.
- An NMI or an IRQ takes its vector through the IIe's map, from the ROM image or the language card, not the BIOS.

In the emulator the hard reset is the console card's START, on the WebUI's front panel, or the terminal app
debugger's `reset`; a power cycle is the WebUI's power switched off and on. The WebUI delivers Ctrl-Delete, and in
Apple II mode its status strip has the IIe's two resets as buttons: **Reset**, Ctrl-Delete, and **Reboot**,
Ctrl-Open Apple-Delete, typed on the USB keyboard as the keys would be. The terminal app has no USB keyboard, so it
has no Ctrl-Reset, and nothing types to the Apple there.

## What isn't there

Status: **emulated**

- **The cassette.** `$C020–$C02F` do nothing. `$C060` and `$C068` read 0.
- **The utility strobe.** `$C040–$C04F` do nothing.
- **Slots 1–5 and 7.** Their I/O reads `$FF` and ignores writes; their ROM pages and `$C800–$CFFF` read `$FF`. No
  card answers there: no serial, mouse or clock card, and the native cards don't stand in for one.
- **The floating bus.** An address nothing answers reads `$FF`, not the byte the video was last fetching. Software
  that reads it to time itself to the display finds nothing there.
- **The annunciators' outputs.** AN0–AN2 are kept, but the game port has no annunciator lines.
- **The native cards' registers.** `$C000–$CFFF` is the IIe's, so no native register can be reached: not the north
  bridge's, the DMA controller's, the SD card's, the video card's, the SIDs', the mouse, the VIA and its joystick
  ports, the 6551 or the console card's. But for the USB card's keyboard, game port and speaker, the cards sit idle
  in their /RESET state, the 6551 with its CMD kept ([chapter 4](04-interrupts.md#in-apple-ii-mode)).
- **DMA.** The controller is idle ([chapter 5](05-dma.md#apple-ii-mode)).
- **Other speeds.** The clock is the Apple's; TURBO and the native speeds don't apply.
- **The IIe's own 65C02.** The CPU is still the W65C02S, so its bit instructions (BBR, BBS, RMB, SMB), WAI and STP,
  which the IIe's 65C02 doesn't have, work in the mode too.

`$C060` reads 0 in the emulator (`emulator/components/devices/apple_io.cpp:168`). hw/design.md has the cassette
read `$FF` ([`hw/design.md:229`](../../hw/design.md#apple-ii-mode)), and so does the comment in
`emulator/components/devices/include/apple_io.h:41`.

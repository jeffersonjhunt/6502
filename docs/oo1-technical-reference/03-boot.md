# 3 Boot

From reset to a prompt: the flash, the BIOS's copy of itself into RAM, its entry points, the Woz Monitor, and
booting from the SD card.

The design record, for the reasons: [hw/design.md, "Boot"](../../hw/design.md#boot).

## Reset and the flash

Status: **emulated**

At reset the north bridge maps W3 and VEC to the top block of the flash, ROM A block 31, read-only (`$DF`). W0–W2
map RAM blocks 0–2. The CPU reads its reset vector from the flash and starts the BIOS at `$C800`. The block's first
2 KB is under the I/O hole, so the BIOS's code starts at `$C800`. The BIOS, version 0.11:

1. Copies itself, `$C800–$FFFF`, into RAM block 63 through W2.
2. Points W3 and VEC at the copy, read-only (`$7F`). The copy sits at the same CPU addresses, so the CPU carries on
   in RAM with no jump.
3. Puts W2 back on RAM block 2. From here the map is RAM blocks 0, 1, 2 and 63.
4. Sets the 6551 to 19,200 baud, 8 bits, no parity.
5. Starts the boot splash and its sound, if those cards answer ([chapter 6](06-video.md)). They play for 5 s on
   their own while the boot goes on.
6. Sets up the video card's 80×30 text screen, and lets the USB keyboard's Pause/Break pull /NMI, if those cards
   answer.
7. Prints `BACKPLANE BIOS 0.11` on the serial line and the screen.
8. Boots the SD card's `/BOOT.BIN` once the splash is over, if there is one; otherwise it enters the monitor
   ([below](#booting-from-the-sd-card)).

<!-- listing: ../../backplane/bios/bios.s:87-116 -->
```
RESET:
                LDX     #$FF
                TXS
                STZ     NB_WIN0        ; as at a hard reset, for C800R
                LDA     #1
                STA     NB_WIN1

; Copy $C800-$FFFF (W3, the flash) to $8800-$BFFF (W2, RAM block 63).
                LDA     #BLOCK63
                STA     NB_WIN2
                STZ     SRC
                STZ     DST
                LDA     #$C8
                STA     SRC+1
                LDA     #$88
                STA     DST+1
                LDY     #$00
COPY:           LDA     (SRC),Y
                STA     (DST),Y
                INY
                BNE     COPY
                INC     DST+1
                INC     SRC+1          ; $FF -> $00 after the last page
                BNE     COPY

                LDA     #NB_RO | BLOCK63
                STA     NB_WIN3        ; from here on, running the RAM copy
                STA     NB_VEC
                LDA     #BLOCK2
                STA     NB_WIN2
```

The BIOS runs from RAM after that, so the flash is never in use once it has booted. The font (ROM A block 30) and
the video demo (block 29) stay in the flash, and are read through W1 when they are needed.

The BIOS checks for each card by its ID byte ([chapter 1](01-overview.md#slots-and-module-ids)), and leaves alone a
card that doesn't answer. A machine with only the serial card still boots to the monitor.

## Booting from RAM: /BOOTSEL

Status: **emulated**

When /BOOTSEL is held at reset, the north bridge starts W3 and VEC on RAM block 63 (`$3F`) instead of the flash,
and STATUS's bit 0 says so ([`$C686`](11-north-bridge-and-rtc.md#reg-c686)). Whatever is in block 63 runs. The
BIOS's image serves both boots: from RAM its first step copies block 63 onto itself, which changes nothing.

In the emulator, the machine file's `northbridge backplane bootsel` line holds /BOOTSEL, and a program loaded into
RAM stands in for what the bus spy would load.

### The bus spy

Status: **proposed**

On the real machine, an ESP32 card, the bus spy, holds /RESET, writes an image into RAM over the backplane, holds
/BOOTSEL and lets /RESET go. It is for firmware work without programming the flash, recovery from a bad flash, and
loading test images. It needs drivers for the address and data lines, level shifting, and control of /RESET. The
design record has the details ([hw/design.md, "Boot"](../../hw/design.md#boot)).

## The BIOS's entry points

Status: **emulated**

The BIOS starts with a table of jumps at fixed addresses, so a new entry never moves another. From the monitor,
`C803R` and so on run one. A program calls ECHO with `JSR $C80F`.

<!-- listing: ../../backplane/bios/bios.s:51-61 -->
```
; Entry points at fixed addresses, for the monitor (nnnnR) and programs:
; eight of them, so a new one does not move the code after them.
ENTRY:          JMP     START          ; $C800: reset, as the vector does
                JMP     DEMO           ; $C803: the video demo (video.s, demo.s)
                JMP     APPLE          ; $C806: Apple II mode (apple.s)
                JMP     SDCAT          ; $C809: the SD card and its root directory (sd.s)
                JMP     SDBOOT         ; $C80C: boot /BOOT.BIN from the SD card (sd.s)
                JMP     ECHO           ; $C80F: print A, for programs (keeps A and X)
.repeat 2
                JMP     WOZMON         ; $C812-$C815: spare
.endrepeat
```

| Address | Entry | What it does |
|---|---|---|
| `$C800` | reset | Starts again as a hard reset does: W3 and VEC go back to the flash and the CPU takes the flash's reset vector, so the BIOS is copied afresh. After a /BOOTSEL boot there is no flash to go back to, and the RAM copy starts again |
| `$C803` | the video demo | Tiles, sprites, scrolling and a palette fade, run from ROM A block 29. Any key goes back to the monitor |
| `$C806` | Apple II mode | Copies the IIe ROM from the cartridge and enters the mode ([chapter 13](13-apple-ii-mode.md)). Only a hard reset comes back |
| `$C809` | the SD card's directory | The card's name and size, then its root directory, one entry a line |
| `$C80C` | boot the SD card | `/BOOT.BIN`, as [below](#booting-from-the-sd-card) |
| `$C80F` | ECHO | Prints the character in A on the serial line and the text screen. Keeps A and X. Uses `$E0–$E7` |
| `$C812`, `$C815` | spare | The monitor, for now |

## The monitor

Status: **emulated**

The monitor is Steve Wozniak's Woz Monitor from the Apple-1, by way of Ben Eater's serial version. It takes keys
from the serial line or the USB keyboard, whichever has one, and prints on both the serial line and the text screen.
It understands only capital letters: the BIOS turns the USB keyboard's letters into capitals, but serial keys come
as they are typed.

It starts by printing `\` and a new line, then takes a line of input. Backspace takes back the last character. ESC
abandons the line, printing `\` again, and so does a line longer than 127 characters. On a line:

- a hex address shows that address and its byte;
- two addresses with `.` between them show the range, eight bytes a line;
- `.` and an address shows on from the last address shown;
- an address, `:` and bytes stores the bytes from that address on;
- an address and `R` runs the code there, by a jump. A program goes back to the monitor with BRK, or with a jump to
  `$C800`, which starts the machine again.

Several of these can go on one line, with spaces between them.

**BRK and Break** both enter the monitor with a fresh stack and the machine put in order. Break is the NMI, from
Pause/Break or the console card's breakpoint. [Chapter 4](04-interrupts.md#pausebreak) lists what they put back and
what they leave.

## Booting from the SD card

Status: **emulated**

`C80CR`, or the BIOS by itself at power-on, boots `/BOOT.BIN` from the SD card's FAT file system:

1. W0–W2 go back on RAM blocks 0–2, so CPU `$1000` is physical `$01000`, where the DMA writes.
2. The card mounts `/BOOT.BIN` as unit 1 ([chapter 10](10-storage.md)).
3. Its sectors are read to `$1000` by DMA channel 0: 88 at most, 44 KB, `$1000–$BFFF`.
4. The BIOS calls `$1000` with JSR. A program that returns comes back to the monitor.

Each failure is said, then the monitor: `NO SD CARD`, `NO CARD IN THE SLOT`, `NO DMA`, `NOT ON THE CARD: /BOOT.BIN`,
`BOOT.BIN IS EMPTY`, `BOOT.BIN IS OVER 44 KB`, or `SD ERROR` and the card's error code in hex.

**At power-on** (since BIOS 0.10), after the greeting, the BIOS looks for `/BOOT.BIN` itself. With no card, an empty
slot, or no `/BOOT.BIN`, it says nothing more and enters the monitor. With one, it prints
`BOOTING THE SD CARD: A KEY FOR THE MONITOR` and waits for the splash to end. With no video card it waits about 2 s
instead. A key pressed meanwhile is taken, and the monitor comes up instead. Otherwise it boots, as `C80CR` does.
A `/BOOT.BIN` that can't be booted is said, as above, then the monitor. So is any other card error: a card with no
FAT volume prints `SD ERROR 09`, since only error 8, not found, is passed over in silence.

`C809R` lists the card's root directory, as a check: the card's name and size in sectors, then each entry's name
with `/` for a directory or its size in hex.

An OODOS card's `/BOOT.BIN` is OOLDR, which loads OODOS.SYS ([hw/oodos.md, "Loading"](../../hw/oodos.md#loading)).
So a machine with an OODOS card boots OODOS at power-on, and one without boots to the monitor.

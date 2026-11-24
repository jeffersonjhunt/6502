# 3 Boot

From reset to a prompt: the flash, the BIOS's copy of itself into RAM, its entry points, the Woz Monitor, and
booting from the SD card.

The design record, for the reasons: [hw/design.md, "Boot"](../../hw/design.md#boot).

## Reset and the flash

Status: **emulated**

At reset the north bridge maps W3 and VEC to the top block of the flash, ROM A block 31, read-only (`$DF`). W0–W2
map RAM blocks 0–2. The CPU reads its reset vector from the flash and starts the BIOS at `$C800`. The block's first
2 KB is under the I/O hole, so the BIOS's code starts at `$C800`. Since BIOS 0.14
([chapter 15](15-diagnostics.md)): first, with the front panel fitted (its ID read twice), the BIOS turns the
breakpoint off if it is left on the diagnostics' own P3 address, `$BD1A5`, and on no other
([chapter 12](12-front-panel.md#reset)); then, with switch 19 up and flash block 23 holding the diagnostics linked
with this BIOS, the diagnostics ask "PRESS CONT OR Y WITHIN 5 S", having written nothing yet; anything else, or
nothing within 5 s, boots as below. With switch 19 up and no diagnostics in the flash (or another build's), the
BIOS says so after its greeting. No RAM is used before the copy. The BIOS, version 0.19:

1. Copies itself, `$C800–$FFFF`, into RAM block 63 through W2. Before the copy, at `BOOT2`, on a revision 2 north
   bridge (ID `$D2`, *proposed*, B7) it sets WIN0H–WIN3H and VECH to 0 and writes MAP whole: AP8 1 with a revision 2
   video card (`VIDEO_ID` `$92`), else 0, so bank 8 is RAM on a machine with no aperture
   ([chapter 11](11-north-bridge-and-rtc.md#the-high-window-registers-and-map)). Break does the same.
2. Points W3 and VEC at the copy, read-only (`$7F`). The copy sits at the same CPU addresses, so the CPU carries on
   in RAM with no jump.
3. Puts W2 back on RAM block 2. From here the map is RAM blocks 0, 1, 2 and 63.
4. Sets the 6551 to 19,200 baud, 8 bits, no parity.
5. Starts the boot splash and its sound, if those cards answer ([chapter 6](06-video.md)). They play for 5 s on
   their own while the boot goes on.
6. Sets up the video card's 80×30 text screen, and lets the USB keyboard's Pause/Break pull /NMI, if those cards
   answer. The screen needs video card revision 2 (ID `$92`, *proposed*) and the DMA controller; with any other card
   the monitor is on the serial line alone ([chapter 6](06-video.md#the-bioss-text-screen)).
7. Prints `BACKPLANE BIOS 0.19` on the serial line and the screen.
8. Boots the SD card's `/BOOT.BIN` once the splash is over, if there is one; otherwise it enters the monitor
   ([below](#booting-from-the-sd-card)).

<!-- listing: ../../backplane/bios/bios.s:119-198 -->
```
RESET:
                LDX     #$FF
                TXS
                LDA     CONSOLE_ID     ; the front panel? Read twice (hw/diag.md, "Card discovery")
                CMP     #CONSOLE_ID_V1
                BNE     BOOT
                LDA     CONSOLE_ID - $FF,X
                CMP     #CONSOLE_ID_V1
                BNE     BOOT
                LDA     CONSOLE_BPADDR ; P3's breakpoint, and only P3's, off
                CMP     #<DG_P3ADDR
                BNE     @switch
                LDA     CONSOLE_BPADDR + 1
                CMP     #>DG_P3ADDR
                BNE     @switch
                LDA     CONSOLE_BPADDR + 2
                CMP     #^DG_P3ADDR
                BNE     @switch
                STZ     CONSOLE_BPCTRL
@switch:        LDA     CONSOLE_SWR + 2
                AND     #$08           ; switch 19
                BEQ     BOOT
                LDA     #DG_B23        ; the diagnostics' signature, through W2
                STA     NB_WIN2
                LDX     #7
@sig:           LDA     $8000,X
                CMP     DIAGSIG,X
                BNE     @none
                DEX
                BPL     @sig
                LDA     $8009          ; and this build's: the diagnostics' version, the BIOS they were linked with
                CMP     #DG_VERSION
                BNE     @none
                LDA     DG_LINKED
                CMP     #BIOS_VMAJ
                BNE     @none
                LDA     DG_LINKED + 1
                CMP     #BIOS_VMIN
                BNE     @none
                LDX     #DE_PROMPT
                JMP     (DG_ENTRIES,X) ; the prompt: back to BOOT unless CONT or Y
@none:          LDA     #BLOCK2        ; (said after the greeting: DIAGNOTE)
                STA     NB_WIN2

; The boot (BOOT), or the copy, the greeting and the monitor with no splash
; and no SD boot (MONBOOT): the diagnostics' two ways back.
BOOT:           LDA     #0
                BRA     BOOT2
MONBOOT:        LDA     #1
BOOT2:          LDX     #$FF
                TXS
                NBSET                  ; revision 2 or more: WINnH and VECH 0, AP8 by the video card (A kept)
                STZ     NB_WIN0        ; as at a hard reset, for C800R
                STA     BOOTMODE
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
the video demo (block 29) stay in the flash, and are read through W1 when they are needed. So do the Apple IIe
ROM images and their character sets (blocks 0–5, since BIOS 0.16, *proposed*): Apple II mode reads them, and only
[the flash update](#the-flash-update) writes them.

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

On the real machine, an ESP32 card, the bus spy, holds /RESET and BE (BE floats the CPU and the north bridge off the
bus), writes an image into RAM over the backplane, holds /BOOTSEL, and lets BE go, then /RESET. It is for firmware
work without programming the flash, recovery from a bad flash, and loading test images. It needs drivers for the
address and data lines and the lines a master drives, level shifting, and control of /RESET and BE. The
design record has the details ([hw/design.md, "Boot"](../../hw/design.md#boot)).

## The BIOS's entry points

Status: **emulated**

The BIOS starts with a table of jumps at fixed addresses, so a new entry never moves another. From the monitor,
`C803R` and so on run one. A program calls ECHO with `JSR $C80F`.

<!-- listing: ../../backplane/bios/bios.s:78-89 -->
```
; Entry points at fixed addresses, for the monitor (nnnnR) and programs:
; nine of them. The ninth, $C818, dispatches the functions after these, so
; none is added after it and the code after them stays where it is.
ENTRY:          JMP     START          ; $C800: reset, as the vector does
                JMP     DEMO           ; $C803: the video demo (video.s, demo.s)
                JMP     APPLE          ; $C806: Apple II mode (apple.s)
                JMP     SDCAT          ; $C809: the SD card and its root directory (sd.s)
                JMP     SDBOOT         ; $C80C: boot /BOOT.BIN from the SD card (sd.s)
                JMP     ECHO           ; $C80F: print A, for programs (keeps A and X)
                JMP     DIAG           ; $C812: the ROM's diagnostics, after a Y (diag23.s)
                JMP     DIAGPGM        ; $C815: the same for a program: A = $A5, Y = $5A, X the options
                JMP     ROMCALL        ; $C818: the dispatching entry: Y = $5A, X = $A0 + the function, A its argument
```

| Address | Entry | What it does |
|---|---|---|
| `$C800` | reset | Starts again as a hard reset does: W3 and VEC go back to the flash and the CPU takes the flash's reset vector, so the BIOS is copied afresh. After a /BOOTSEL boot there is no flash to go back to, and the RAM copy starts again. The way back runs a short stub in zero page through W0, so on a revision 2 north bridge (*proposed*, B7) `START` first sets WIN0H to 0, whatever a program left there |
| `$C803` | the video demo | Tiles, sprites, scrolling and a palette fade, run from ROM A block 29, on video card revision 2 (*proposed*; with any other card it goes straight back). Any key goes back to the monitor |
| `$C806` | Apple II mode | Copies the first good IIe ROM image in ROM A, of images 0–3, with its own character set, and enters the mode ([chapter 13](13-apple-ii-mode.md)); refuses without video card revision 2 (*proposed*). Since BIOS 0.16 (*proposed*); BIOS 0.15 and before took the cartridge's IIe ROM. Only a hard reset comes back |
| `$C809` | the SD card's directory | The card's name and size, then its root directory, one entry a line |
| `$C80C` | boot the SD card | `/BOOT.BIN`, as [below](#booting-from-the-sd-card) |
| `$C80F` | ECHO | Prints the character in A on the serial line and the text screen. Keeps A and X. Uses `$E0–$E7` |
| `$C812` | the diagnostics | Since BIOS 0.14 ([chapter 15](15-diagnostics.md)): asks for `Y`, since they overwrite all of memory, then runs the ROM's diagnostics from flash block 23, with the panel's switches 16–18 as their options. With no diagnostics in the flash it says `NO DIAGNOSTICS IN THE FLASH` (or that they are another build's) and goes back to the monitor. It never returns |
| `$C815` | the diagnostics, for a program | Since BIOS 0.14: A = `$A5` and Y = `$5A`, X the options (bit 0 full, bit 1 interactive; the switches don't count), with no `Y` asked; anything else is `$C812`. With no diagnostics in the flash it returns, carry set, A, X, Y and W2 as they were. OODOS's `DIAG` reaches it through function 75 with A = 2. The last of the eight; the functions after it go through `$C818` |
| `$C818` | the dispatching entry | Since BIOS 0.16 (*proposed*; on BIOS 0.15 and before `$C818` was the first byte after the table, so a call there restarted the machine). A program asks with a two-byte token, Y = `$5A` and X = `$A0` + the function, and A the function's argument. Function `$A1` is Apple II mode with image A, 0–3: the card and DMA checks as `$C806`'s, then `NO APPLE IIE ROM IN FLASH IMAGE n` and the monitor if that image isn't good. Since BIOS 0.21, functions `$A2` and `$A3` drive a revision 3 sound card ([chapter 7](07-sound.md)): `$A2` beeps at 800 Hz for A × 10 ms (A = 0: 250 ms) and `$A3` stops every sound on it (STOPALL); each waits for the card's READY (250 ms) and for its reply (100 ms), and returns carry clear, or carry set at once with no revision 3 card or when a wait runs out, with A, X, Y and the windows as they were. With the token and A over 3, function `$A0` (the flash update, reached only from the menu in BIOS 0.16) or a function `$A4–$AF` (reserved), it returns, carry set, with A, X, Y and the windows as they were. Without the token, as `C818R` from the monitor (which leaves X 0), it prints a menu, `ROM: F FLASH UPDATE, 0-3 APPLE II MODE, ANY OTHER KEY MONITOR`, and takes a key: `F` is [the flash update](#the-flash-update), a digit 0–3 Apple II mode with that image, any other key the monitor. OODOS's `APPLE2 n` reaches function `$A1` through function 75 with A = 3. No entry is added after it |

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
3. Its sectors are read to `$1000` by DMA channel 0, whose CARD names the SD card (module 7): 88 at most, 44 KB,
   `$1000–$BFFF`. The BIOS selects channel 0 for the read and puts CONTROL back as it found it afterwards, so
   `/BOOT.BIN` starts with the BIOS's own CONTROL: `$71` with a revision 2 video card, `$70` without one, since BIOS
   0.15 (*proposed*).
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

## The flash update

Status: **proposed**

Since BIOS 0.16; emulated as step B5 ([hw/design.md, as built](../../hw/design.md#b5-as-built)).

The Apple IIe ROMs are Apple's, so the OO-1 can't ship them: the owner of a IIe ROM writes it into ROM A himself,
from the SD card, with the BIOS's flash update. It writes one image, 0–3, and its character set, and nothing else
([chapter 2](02-memory-and-banking.md#reprogramming-the-flash)). From the monitor, `C818R` and then `F` at the menu
([above](#the-bioss-entry-points)). A program can't start it: `$C818`'s function `$A0` with the token returns,
carry set, in BIOS 0.16.

1. **The image.** It asks `FLASH UPDATE: WHICH IIE IMAGE, 0-3?`. Any other key goes back to the monitor.
2. **The files**, from the card's root, for image *n*: `/APPLE2En.ROM`, the IIe ROM as it sits at `$C000–$FFFF`,
   exactly 16,384 bytes and needed; and `/APPLE2En.CHR`, the IIe's character ROM, 4 or 8 KB, if there is one. Each
   is mounted as unit 1, as `/BOOT.BIN` is, and read by the CPU, with no DMA, into RAM: the image into block 59, the
   set into block 58, which Apple II mode overwrites anyway. A file's size is the one the root's listing gives
   (`LIST`, as `C809R` lists it), not its sectors', which round it up.
3. **The checks, before anything is erased.** The image must be 16,384 bytes with `$06` at its `$FBB3`, and a
   `.CHR` 4 or 8 KB. A file that isn't on the card (`NOT ON THE CARD: /APPLE2E2.ROM`), the wrong size
   (`/APPLE2E2.ROM ISN'T A IIE ROM`, `/APPLE2E2.CHR ISN'T 4 OR 8 KB`), not a IIe's, or a card error leaves the
   flash as it was, and goes back to the monitor.
4. **What it found**, a line each: `/APPLE2E2.ROM 16 KB`, then `/APPLE2E2.CHR 8 KB` (or `4 KB`), or with no `.CHR`
   `NO /APPLE2E2.CHR: SET 2 ERASED, ITS CHARACTERS THE VGA FONT'S`. Then `WRITE FLASH IMAGE 2 (Y/N)?`; anything but
   `Y` goes back to the monitor with nothing written.
5. **Each chip reading.** A byte of `$FF` to each chip in the image's slot (it programs nothing, but ends a command
   sequence someone left half-sent), then `$F0` to each.
6. **The image not good, then the erase.** First the image's `$FBB3` (the odd chip's byte) is programmed to `$00`,
   polled: programming only clears bits, so a good image's `$06` goes with no erase, and from then on the image is
   not good, however the rest is stopped. Then the image's 16 KB (four sector erases, two in each chip) and the
   set's 8 KB (two). With no `.CHR` the set stays erased, so Apple II mode makes its characters from the VGA font,
   and an old set never stays under a new image.
7. **The program,** a pair of bytes at a time in ascending order: the even byte's sequence and byte to the even
   chip, the odd byte's to the odd one, then each chip polled at an address of its own parity until two reads
   agree. Every byte goes but the commit bytes: the image's `$FBB3`, which makes it good, and the set's first 8.
8. **The verify:** everything, read back through a window with RO set, against the copy in RAM. Then the commit
   bytes, the image's `$FBB3` last of all, and the verify again. So an image is good only once it has been written
   whole and verified, and an update stopped once step 6 has begun (Break, the power, or a chip that stops
   answering) leaves it not good, for `$C806` to pass over: its `$FBB3` is `$00` until its erase, `$FF` after it.
9. **The end:** `FLASH IMAGE 2 WRITTEN`, then the monitor. A byte that doesn't verify is
   `FLASH IMAGE 2 NOT WRITTEN AT $F0xxxx`, with its physical address (`$F1xxxx` in a set), and a chip that doesn't
   answer its poll within 65,536 pairs of reads (about 180 ms) `THE FLASH DOESN'T ANSWER`; either way the image and
   its set are erased again, its `$FBB3` programmed to `$00` first as in step 6 (unless it is that erase again that
   didn't answer), so the image isn't good, and the monitor follows. A chip that stops answering before the
   image's `$FBB3` has been made `$00` can leave it so: if it is the odd chip, which holds that byte, the erase
   again can't program it either, nothing is erased, and the image is left as it was (whole, and good if it was),
   for `$C806` to take.

However it is entered, it runs QUIET first (the DMA stopped and the cards quiet, as Break does), keeps interrupts off,
and sets every window itself: W0 RAM block 0, W2 the copy in RAM, W3 the BIOS's copy, and W1 the flash, writable to
program and read-only to verify; on the way out W1 and W2 go back to blocks 1 and 2. Every write it makes to the
flash goes through one routine, which refuses any window value but ROM A's blocks 0–5, with RO clear, and says
`THE FLASH UPDATE REFUSED BLOCK $bb`: so whatever computed a block, the update can't reach the BIOS, the
diagnostics or ROM B. It never sends a chip erase. It uses zero page `$10–$21` and `$0280–$029A` while it runs
([chapter 2](02-memory-and-banking.md#zero-page-and-low-ram)).

In the emulator the update of one image and an 8 KB set takes about 1.5 s, at the chips' typical times
([chapter 14](14-emulator-notes.md#the-flash)).

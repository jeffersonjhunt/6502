# 4 Interrupts

IRQ and NMI, how a handler finds the card that interrupted, and the vectors.

The design record, for the reasons: [hw/design.md, "Interrupts"](../../hw/design.md#interrupts).

## IRQ and NMI

Status: **emulated**

**/IRQ** is one line that any card can pull low. Each card has its own interrupt enable, and pulls /IRQ while one
of its enabled sources is pending. The line stays low until the handler clears the source in the card, so a handler
that returns without doing that is entered again at once.

| Card | Its enable | What interrupts |
|---|---|---|
| serial, the 6551 | its CMD, bit 1 = 0 | a byte received |
| serial, the VIA | its IER | its timers, its shift register, CA1, CA2, CB1, CB2 |
| storage (SD) | SD_IEN | a command done, a card changed |
| DMA | a channel's MODE, bit 7 | that channel's terminal count |
| video | VIDEO_IEN | vertical blank, a chosen line, a sprite collision |
| sound | SOUND_IEN | a byte in the mailbox from the card's 6510 |
| USB | USB_IEN | a key, the mouse, the game port, a device |
| north bridge | RTCCTL's TIE | TICK, once a second |
| console | CONSOLE_IEN | a key on the panel |

**/NMI** has two sources: the USB keyboard's Pause/Break, with KBCFG's bit 7 set ([below](#pausebreak)), and the
console card's breakpoint, with BPCTRL's NMI bit set. An NMI can't be masked, and the CPU takes it on the falling
edge.

**The BIOS's handlers.** The ROM's vectors point NMI at Break and IRQ at a handler that tells BRK from an IRQ. BRK
enters the monitor as Break does. A real IRQ returns at once, because the BIOS enables no interrupts. The BIOS
starts with the CPU's I flag set. Break turns off the interrupt enables of the SD, video, sound, USB and console
cards and the clock's TIE, and the monitor sets the 6551 up with its interrupts off. **The VIA's IER is left as it
was**: Break doesn't touch it. A VIA interrupt a program left enabled survives Break, and since the handler only
returns, the next program that clears I is interrupted again and again and never gets on. A program run from the
monitor that clears I turns the VIA's interrupts off first (write `$7F` to VIA_IER), or brings its own handler.

<!-- listing: ../../backplane/bios/bios.s:149-158 -->
```
; BRK is the monitor; a real IRQ just returns.
IRQ:            PHA
                PHX
                TSX
                LDA     $0103,X        ; the P it pushed
                AND     #$10           ; B: a BRK
                BNE     BREAK
                PLX
                PLA
                RTI
```

A program that enables an interrupt brings its own handler, through VEC ([below](#vec-and-the-vectors)). Under
OODOS, the interrupts and the vectors are OODOS's ([AGENTS.md](../../AGENTS.md)).

## Finding the source: IRQL and IRQH

Status: **emulated**

The north bridge can't see which card pulled /IRQ, because cards are chosen by module ID, not by slot. Instead it
asks them. Reading IRQL ([`$C688`](11-north-bridge-and-rtc.md#reg-c688)) or IRQH
([`$C689`](11-north-bridge-and-rtc.md#reg-c689)) asserts /IACK for that cycle, and each card that is interrupting
pulls its own data line low: line ID mod 8, in IRQL for IDs 0–7 and IRQH for IDs 8–F. The pull-ups leave every
other bit 1. So **a bit that reads 0 is a card interrupting**.

| Register | Bit | ID | Card |
|---|---|---|---|
| IRQL | 6 | 6 | serial: the 6551 and the VIA share it |
| IRQL | 7 | 7 | storage: the SD card |
| IRQH | 0 | 8 | the DMA controller |
| IRQH | 1 | 9 | video |
| IRQH | 2 | A | sound |
| IRQH | 3 | B | USB |
| IRQH | 5 | D | the north bridge: the clock's TICK |
| IRQH | 6 | E | the console card |

IRQL's bits 0–5 (RAM and ROM), IRQH's bit 4 (the Disk II, which has no interrupt) and bit 7 (no module) always
read 1. One read of each, then each card with a 0 asked for its own status: its ISR, or DMA_STATUS, or RTCCTL's
TICK. Reading the poll brings every card up to date first, so it shows the interrupts pending at that cycle.

The serial card's bit is shared, so a handler asks both: the 6551's STATUS (bit 7) and the VIA's IFR (bit 7).

### The serial card's INTSTAT

Status: **proposed**

The design gives the serial card an INTSTAT register at `$C322`, saying which of its parts is interrupting
([hw/design.md, "Input and Ports"](../../hw/design.md#input-and-ports)). The emulator doesn't have it: `$C322` is
still the 6551's mirror, so a handler tells the VIA by its IFR.

### In Apple II mode

Status: **emulated**

Apple software polls its own cards and never uses /IACK. The north bridge's registers are hidden, so IRQL and IRQH
can't be read. hw/design.md has the native cards keep quiet while AMODE is high. The emulator gets there by the
/RESET pulse that enters the mode: it resets every card, which turns off the interrupt enables of the SD, video,
sound, USB and console cards and the VIA's IER. The bridge clears TIE, and the DMA controller's reset clears every
channel's mode and terminal count. **The 6551 stand-in is the exception**: a /RESET doesn't reset it, so its CMD is
kept, and with its receive interrupt on, a byte arriving pulls /IRQ in Apple II mode. Enter the mode with the 6551's
receive interrupt off (CMD bit 1 set). The monitor's is: it writes `$0B` to ACIA_CMD.

## VEC and the vectors

Status: **emulated**

The W65C02S reads its vectors at `$FFFA–$FFFF` (NMI, reset, IRQ and BRK) with VPB low. While VPB is low, the north
bridge maps `$C000–$FFFF` through VEC ([`$C684`](11-north-bridge-and-rtc.md#reg-c684)) instead of W3. VEC has the
windows' layout, and resets to the same value as W3, so a reset reads its vector from the flash.

- Only the vector's two bytes come through VEC. The instruction fetches after them go through the windows as usual,
  so **the handler's address must be mapped** wherever an interrupt can come.
- The BIOS sets VEC with W3 to `$7F`, its RAM copy in block 63, read-only.
- To take the vectors, a program writes a block with its own vectors at `$3FFA–$3FFF` of the block (CPU
  `$FFFA–$FFFF` through VEC) and points VEC at it. W3 stays as it is. The vectors may point into W0–W2, or anywhere
  the windows map.
- In Apple II mode VEC isn't used: the IIe's map has its own rules for `$D000–$FFFF`
  ([chapter 13](13-apple-ii-mode.md)).

## Pause/Break

Status: **emulated**

Pause/Break on the USB keyboard pulls /NMI when the USB card's KBCFG has bit 7 (`USB_NMI_PAUSE`) set. The BIOS sets
it at boot, if a USB card answers, so Break always reaches the monitor. Break enters it with:

- a fresh stack, and windows 0–2 on RAM blocks 0–2;
- every DMA channel aborted, with its terminal count and mode cleared;
- an SD command abandoned, the units staying mounted;
- the boot splash stopped, the sound card's 6510 stopped and its SIDs silenced;
- the interrupt enables of the SD, video, sound, USB and console cards off, and the clock's TICK and TIE;
- the text screen set up again, found from the cards' ID bytes;
- the 6551 set up afresh by the monitor, with its interrupts off.

It leaves W3, VEC, DMA CONTROL, memory, the VIA (its IER too: [above](#irq-and-nmi)), and the rest of each card's
state as they were. Break works while W3 and VEC
are the BIOS's; a program that moves VEC takes the NMI itself. The design record has the list and its reasons
([hw/design.md, "Boot"](../../hw/design.md#boot)).

The console card's breakpoint can pull /NMI as well, and Break handles it the same way.

**In the emulator,** the WebUI delivers Pause/Break. The terminal app has no USB keyboard to press it on, so there
Break comes only from the console card.

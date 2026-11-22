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
| north bridge | TBCTL's MSIE (revision 3); RTCCTL's TIE before it | the 1 kHz tick (revision 3); TICK, once a second, before it |
| console | CONSOLE_IEN | a key on the panel |

**/NMI** has two sources: the USB keyboard's Pause/Break, with KBCFG's bit 7 set ([below](#pausebreak)), and the
console card's breakpoint, with BPCTRL's NMI bit set. An NMI can't be masked, and the CPU takes it on the falling
edge.

**The BIOS's handlers.** The ROM's vectors point NMI at Break and IRQ at a handler that tells BRK from an IRQ. BRK
enters the monitor as Break does. A real IRQ returns at once, because the BIOS enables no interrupts. The BIOS
starts with the CPU's I flag set. Break turns off the interrupt enables of the SD, video, sound, USB and console
cards and the clock's TIE, and the monitor sets the 6551 up with its interrupts off. Since BIOS 0.11 Break also
writes `$7F` to VIA_IER, turning every VIA interrupt off, and `$01` to CONSOLE_ISR: the console card's interrupt
follows its ISR whatever IEN says, so a panel key that set ISR while a program had IEN on would otherwise keep
/IRQ low. So a program run from the monitor may clear I, as long as it leaves nothing on that the handler can't
answer. Under BIOS 0.10 Break left both, and a program that clears I wrote them first
([chapter 12](12-front-panel.md)).

<!-- listing: ../../backplane/bios/bios.s:396-405 -->
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
OODOS, the interrupts and the vectors are OODOS's ([below](#under-oodos), [AGENTS.md](../../AGENTS.md)).

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

IRQL's bits 0–5 (free IDs, once RAM and ROM: [chapter 2](02-memory-and-banking.md#the-24-bit-bus)), IRQH's bit 4
(the Disk II, which has no interrupt) and bit 7 (no module) always read 1. One read of each, then each card with a
0 asked for its own status: its ISR, or DMA_STATUS, or RTCCTL's TICK. Reading the poll brings every card up to date first, so it shows the interrupts pending at that cycle.

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
sound, USB and console cards and the VIA's IER. The bridge clears TIE, MSIE and HOLE, and the DMA controller's reset clears every
channel's mode and terminal count, all eight, and puts each CARD and CONTROL back to their reset values
([chapter 5](05-dma.md#apple-ii-mode)). **The 6551 stand-in is the exception**: a /RESET doesn't reset it, so its CMD is
kept, and with its receive interrupt on, a byte arriving pulls /IRQ in Apple II mode. Enter the mode with the 6551's
receive interrupt off (CMD bit 1 set). The monitor's is: it writes `$0B` to ACIA_CMD. OODOS, which keeps it on,
turns it off when it leaves for the ROM or Apple II mode (function 75), as its other interrupts.

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

## Under OODOS

Status: **built**

Since OOLIB's L1 ([hw/oolib.md, "The plan"](../../hw/oolib.md#the-plan)) OODOS owns /IRQ, and runs with the CPU's
I flag clear: programs, the CCP, and the BDOS during its calls. Its IRQ vector points into common memory
(`$F800–$FFFF`), which since OODOS 0.16 is the north bridge's common hole: the same bytes whatever W3 holds
([chapter 11](11-north-bridge-and-rtc.md#the-common-hole)), and a vector pull reads the hole whatever VEC holds, so
the handler is there whatever block a program or a far call has in W3. OODOS 0.16 runs only on north bridge
revision 3.

- **The dispatcher** (`oodos/sys/irq.s`) tells BRK from an IRQ as the BIOS does (BRK still enters the monitor),
  saves A, X, Y and W0, puts W0 on OODOS's system block (31) without moving S, and reads IRQL and IRQH once each.
  It touches nothing of the BDOS's, and puts back every window it changes.
- **What OODOS turns on:** the north bridge's 1 kHz tick (TBCTL's MSIE, IRQH bit 5;
  [chapter 11](11-north-bridge-and-rtc.md#the-timebase)), whose counter is OODOS's millisecond counter; the video
  card's vertical blank (VIDEO_IEN bit 0), for its frame counter; and, since OODOS 0.15, the 6551 stand-in's receive
  interrupt (ACIA_CMD `$09`), for the serial line's receive buffer
  ([hw/oodos.md, "The serial receive buffer"](../../hw/oodos.md#the-serial-receive-buffer)). An interrupt that is the
  tick alone (IRQL `$FF`, IRQH `$DF`) takes the fast path. ID 6 goes to the 6551's service: STATUS read (its IRQ
  cleared), and with RDRF set the byte into a 255-byte ring in OODOS's memory, 247 cycles a byte from the interrupt
  to its RTI; with the ring full the byte is left in the chip and the receive interrupt turned off, until the BIOS,
  reading, takes it and turns it on again. The handlers acknowledge by writing `$E0` to TBCTL (MSF and OVR cleared,
  MSIE kept) and 1 to VIDEO_ISR's bit 0. Cold boot finds the video card by its ID and the VIA by its IER, which
  reads bit 7 set (on a machine without one, `$C34E` is the 6551 stand-in's command register, `$0B`); OODOS 0.16
  turns every VIA interrupt off and leaves timer 1 to programs, polled
  ([chapter 9](09-serial-and-via.md)). Without the VIA the dispatcher leaves `$C340–$C34F` alone: they are the 6551's
  registers again.
- **What it doesn't answer** it turns off, so it can't hold /IRQ low: every VIA interrupt (its IER bit off, its
  IFR flag cleared), the video card's line and collision interrupts, and any other card's
  interrupt: the SD, sound, USB and console cards' IEN, every DMA channel's MODE bit 7 (all eight, each selected in
  turn with CONTROL put back after) and the terminal count of each channel that had it on (another's is its driver's to read), the 6551's transmit interrupt
  (ACIA_CMD bits 3-2 `%10`, its receive interrupt bit 1 put back to OODOS's, the rest kept; then its STATUS read,
  and a byte waiting taken into the ring), the clock's TIE. The warm boot does
  the same at each program's end, keeping only OODOS's own, turns the VIA's interrupts off and puts ACR to 0, and
  turns the tick on again.
- **Masked:** only while the BDOS's entry moves the windows and S (in and out), during the warm boot, and while the
  Disk II driver reads or writes a sector's fields, whose bits the CPU times itself. The counter is the bridge's, so
  nothing is lost: the ticks that came meanwhile are one interrupt, with TBCTL's OVR set, and the event area's
  counter is written whole at the next.
- **The USB card** (IRQH bit 3), since OOLIB's L2: only while a program has an event area of layout 2 open
  ([chapter 8](08-usb.md#under-oodos)). Its handler, and the joysticks' and paddles' sampling at the tick, are in
  block 62, behind a switch of W3 and back; only the switch and the tick's checks are in common memory. An
  interrupt from the card with no such area open is turned off as any other's.
- **The sound card and its DMA channel, 2** (IRQH bits 2 and 0; channel 2's CARD names the sound card, module A), since OOLIB's L3: the card's IEN bits 2 (the
  mailbox) and 3 (the ES5503) while a program holds what they serve, and channel 2's terminal count while a stream
  buffer is going to the card ([chapter 7](07-sound.md#under-oodos)). The handler that programs the stream's next
  piece selects channel 2 and puts CONTROL back as it found it ([chapter 5's SELECT rule](05-dma.md#the-controller)). Their handler is in block 61, the BIOS's,
  reached through a borrow of W2 and put back byte for byte, from the same path as the USB card's; W1 is kept for
  the event area. OODOS's turning off of strays leaves those bits and channel 2 alone while they are its own.
- **The vertical-blank copies and the pointer** (OOLIB L4): after the event area's counters, while a slot is set
  or the pointer is on, the frame handler copies each slot whose bit the program set by DMA channel 1 (CARD 9, the
  video card), through video card revision 2's data port into card memory (*proposed*; the CPU held a cycle a byte, 1 KB at most: inside
  the blank's 10,240 cycles, with the pointer and a stream, line 505 at the latest under a USB batch and under
  64 KB loads alike in the frames sampled at B7's final build, as [hw/oolib.md](../../hw/oolib.md#the-plan)
  records; a sampled figure, not a worst case: a single 512-byte slot under loads was seen ending at line 522, and
  a worst-case sweep is later work), then
  writes the pointer sprite's X and Y the same way, before
  the sound part and the frame hook. It is in block 61, reached as the sound card's handler is; with nothing set it
  costs the frame 7 cycles, and with no event area nothing ([chapter 6](06-video.md#under-oodos)).
- **What it costs** (OODOS 0.16, measured): a tick is 142 cycles through the dispatcher, 2% of the CPU (162 with
  OODOS 0.15's VIA tick); with a layout-1 area open 188, with layout 2 241 (the sticks compared), and twice a frame
  more (the paddles started, then read: 378 and 684). A frame is 231 cycles, 447 with a layout-2 area. These include
  the dispatcher's 17 cycles for WIN1H, read, saved and found 0. While a program that called OOLIB's `GX_BANK8` has
  slot 1 in W1 (WIN1H `$0E`), the dispatcher clears WIN1H and puts it back, 10 cycles more on every interrupt.
- **DMA:** the CPU is held through a memory-to-memory copy, so OODOS's copies on channel 3 go 2 KB at a time
  (4,096 cycles, under a tick): the tick due is taken between them, on time. (Before OODOS 0.16 timer 1's one flag
  made that a must; the bridge's counter loses nothing either way, but the hooks and the sticks keep their
  timing.)
- **Leaving:** function 75's Apple II mode turns OODOS's interrupts off first; Break and function 75's monitor
  go through the BIOS's Break ([below](#pausebreak)), which turns off the tick (TBCTL `$C0`) and the video card's.
  Each way turns the common hole off before the ROM runs.

The Programmer's Guide has what programs get from it: the counters, the event area and the hooks
([chapter 8](../oodos-programmers-guide/08-game-services.md)).

## Pause/Break

Status: **emulated**

Pause/Break on the USB keyboard pulls /NMI when the USB card's KBCFG has bit 7 (`USB_NMI_PAUSE`) set. The BIOS sets
it at boot, if a USB card answers, so in native mode Break reaches the monitor. In Apple II mode it doesn't: the
/RESET pulse that enters the mode clears KBCFG, so Pause/Break pulls nothing there. Break enters the monitor with:

- a fresh stack, and windows 0–2 on RAM blocks 0–2;
- every DMA channel aborted, all eight, with its terminal count and mode cleared;
- each DMA channel's CARD put back to its reset value (`$07`, `$09`, `$0A`, then `$08`, none), so a CARD a program
  or the monitor changed can't keep the SD boot from its grant, since BIOS 0.15 (*proposed*,
  [chapter 5](05-dma.md#the-controller));
- an SD command abandoned, the units staying mounted;
- the boot splash stopped, the sound card's 6510 stopped and its SIDs silenced, and on revision 2 its PCM stream
  and ES5503 stopped too (PCMCTL FLUSH alone, DOCCTL's RUN cleared), since BIOS 0.12
  ([chapter 7](07-sound.md#silencing-them));
- the interrupt enables of the SD, video, sound, USB and console cards off, and the clock's TICK and TIE;
- the north bridge's tick off (`$C0` to TBCTL: MSF and OVR cleared, MSIE off), with no ID check, since BIOS 0.20;
- the VIA's interrupts off (`$7F` to VIA_IER), and a panel key's pending interrupt cleared (`$01` to
  CONSOLE_ISR), since BIOS 0.11;
- the text screen set up again, found from the cards' ID bytes;
- the 6551 set up afresh by the monitor, with its interrupts off.

It leaves W3, VEC, DMA CONTROL (its SELECT too: QUIET selects each channel in turn and then puts CONTROL back
whole), memory, the rest of the VIA (its timers run on, and IFR keeps its flags), and the
rest of each card's state as they were ([above](#irq-and-nmi)). Break works while W3 and VEC are the BIOS's; a program that moves VEC
takes the NMI itself. The design record has the list and its reasons
([hw/design.md, "Boot"](../../hw/design.md#boot)).

The console card's breakpoint can pull /NMI as well, and Break handles it the same way.

**In the emulator,** the WebUI delivers Pause/Break. The terminal app has no USB keyboard to press it on, so there
Break comes only from the console card.

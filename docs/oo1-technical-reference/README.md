# OO-1 Technical Reference

The machine without OODOS: the backplane, memory and banking, the boot, interrupts, DMA and each card's
registers. For whoever writes a BIOS, a driver, OOLIB, or a program that runs on the bare machine or from ROM.
Every section carries a status tag: **built** (on a real card), **emulated** (in the emulator, not yet built) or
**proposed** (designed only). Programs under OODOS should go through the
[Programmer's Guide](../oodos-programmers-guide/README.md) instead, and leave the hardware that OODOS owns alone
([AGENTS.md](../../AGENTS.md)). How the books are kept is in [docs/README.md](../README.md).

No card is built yet, so the book describes the emulator ([emulator/](../../emulator/)). Where the emulator and the
design record ([hw/design.md](../../hw/design.md)) differ, the book says what the emulator does and notes the
difference; [chapter 14](14-emulator-notes.md#what-differs) has them all in one list.

| Chapter | Covers | Proposed, designed only |
|---|---|---|
| 1 [Overview](01-overview.md) | the backplane's signals, slots and module IDs, the clocks | the physical bus; ÷1 |
| 2 [Memory and banking](02-memory-and-banking.md) | physical memory, the four windows, the I/O hole, flash and read-only windows, zero page | reprogramming the flash |
| 3 [Boot](03-boot.md) | reset and the BIOS's copy of itself, /BOOTSEL, the entry points, the Woz Monitor, booting the SD card | the bus spy |
| 4 [Interrupts](04-interrupts.md) | IRQ and NMI, IRQL and IRQH, VEC, Pause/Break | the serial card's INTSTAT |
| 5 [DMA](05-dma.md) | the controller, `$C400`: memory to memory, transfers for a card | |
| 6 [Video](06-video.md) | the VGA card, `$C480`: layers, sprites, the palette, video memory, interrupts, the splash | |
| 7 [Sound](07-sound.md) | two SIDs and a 6510, `$C500`: the mailbox, the card's RAM, `.sid` tunes | the card's bus timing |
| 8 [USB](08-usb.md) | the keyboard, mouse, game port and speaker, `$C580` | other layouts, a gamepad, device events, the event FIFO on DMA, a real 558 |
| 9 [Serial and the VIA](09-serial-and-via.md) | the 6551 stand-in at `$C300`, the VIA at `$C340` and the joysticks | COM1, COM2 and LPT; the user port |
| 10 [Storage](10-storage.md) | the SD card, `$C380`: commands, FAT mounts, DMA; the Disk II in both modes, and how OODOS drives it | |
| 11 [The north bridge and the clock](11-north-bridge-and-rtc.md) | the windows, VEC, MODE, STATUS, the wait states, the interrupt poll, the DS3231, `$C680` | turbo |
| 12 [The front panel](12-front-panel.md) | the console card, `$C700`: switches, keys, lights, the breakpoint | the design's bus-cycle step, breakpoint cycle, EXAM and DEP on the bus, AMODE handling and ISR; the BE lamp; the ESP32's SPI header |
| 13 [Apple II mode](13-apple-ii-mode.md) | entering it, the IIe's memory map and switches, the display, the clock, the Disk II, Ctrl-Reset, what isn't there | the long cycle |
| 14 [Emulator notes](14-emulator-notes.md) | the machines and the apps, what the emulator models, every difference from the design, running a program | what isn't there, gathered from the chapters |

Everything else is **emulated**: each chapter's sections and all 191 register entries, one for every I/O address
an equate in [`backplane/bios/backplane.inc`](../../backplane/bios/backplane.inc) names
([docs/templates/register.md](../templates/register.md); [docs/index/registers.json](../index/registers.json)
lists them). Written in G2; the Disk II's native switches after OODOS's phase 6.

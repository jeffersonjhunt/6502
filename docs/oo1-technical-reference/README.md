# OO-1 Technical Reference

The machine without OODOS: the backplane, memory and banking, the boot, interrupts, DMA and each card's
registers. For whoever writes a BIOS, a driver, OOLIB, or a program that runs on the bare machine or from ROM.
Every section carries a status tag: **built** (on a real card), **emulated** (in the emulator, not yet built) or
**proposed** (designed only). Programs under OODOS should go through the
[Programmer's Guide](../oodos-programmers-guide/README.md) instead, and leave the hardware that OODOS owns alone
([AGENTS.md](../../AGENTS.md)). How the books are kept is in [docs/README.md](../README.md).

1. [Overview](01-overview.md): the backplane, slots, module IDs, clocks
2. [Memory and banking](02-memory-and-banking.md): physical memory, the windows, the I/O hole
3. [Boot](03-boot.md): the flash, the BIOS's entry points, the monitor, booting from the card
4. [Interrupts](04-interrupts.md): IRQ, NMI, finding the source, the vectors
5. [DMA](05-dma.md): the controller, `$C400`
6. [Video](06-video.md): the VGA card, `$C480`
7. [Sound](07-sound.md): two SIDs and a 6510, `$C500`
8. [USB](08-usb.md): keyboard, mouse and game port, `$C580`
9. [Serial and the VIA](09-serial-and-via.md): `$C300`, the VIA at `$C340`
10. [Storage](10-storage.md): the SD card, `$C380`, and the Disk II
11. [The north bridge and the clock](11-north-bridge-and-rtc.md): `$C680`
12. [The front panel](12-front-panel.md): the console card, `$C700`
13. [Apple II mode](13-apple-ii-mode.md)
14. [Emulator notes](14-emulator-notes.md)

**Written so far:** the skeleton (G0). The chapters are G2's, for what the emulator implements; every register an
equate in [`backplane/bios/backplane.inc`](../../backplane/bios/backplane.inc) names gets an entry
([docs/templates/register.md](../templates/register.md)).

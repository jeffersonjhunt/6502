# OODOS Programmer's Guide

For anyone writing a program for OODOS: a `.COM` or `.PRL` file on the card, in ca65 or cc65, that calls the BDOS.
It follows the outline of CP/M 3's Programmer's Guide and keeps the meaning of every function number, in our own
words, with what the 6502 and the OO-1 change. The machine underneath, without OODOS, is the
[OO-1 Technical Reference](../oo1-technical-reference/README.md); the commands at the prompt are the
[User's Guide](../oodos-users-guide/README.md). How the books are kept is in [docs/README.md](../README.md).

1. [Introduction](01-introduction.md): what OODOS is, its CP/M 3 heritage, what's ours, the memory a program sees
2. [The system interface](02-system-interface.md): calling the BDOS, the program header, the base page, the
   command tail, FCBs, return and error codes, error modes
3. [The file system](03-file-system.md): drives, users, attributes, passwords, XFCBs, labels, date stamps, random
   records, multi-sector I/O
4. [Console, devices and the SCB](04-console-devices-scb.md): console modes, DEVTBL, BIOS function 50, the SCB,
   the time
5. [Memory and banking](05-memory-and-banking.md): blocks, allocation, functions 72-74 and FARCALL, the DMA
   segment
6. [Program formats](06-program-formats.md): `.COM`, `.PRL`, overlays, the 65C816 rules; RSXs and chaining
   (phase 5)
7. [Function reference](07-function-reference.md): one entry per function
8. [Game services (OOLIB)](08-game-services.md): written in G3

Appendices:

- A. [Error codes](appendix-a-errors.md)
- B. [The PRL format](appendix-b-prl-format.md)
- C. [The SCB](appendix-c-scb.md)
- D. [Function summary](appendix-d-function-summary.md)
- E. [Control characters](appendix-e-control-characters.md)
- F. [Building with ca65 and cc65](appendix-f-building.md)
- G. [How OODOS differs from CP/M 3](appendix-g-differences.md)

**Written so far:** the skeleton (G0) and [function 9](07-function-reference.md#bdos-9). The rest is G1's, for
what OODOS phases 0-4 have built.

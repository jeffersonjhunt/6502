Backplane computer software
===========================

Software for the backplane computer designed in `../hw/design.md`. Until the
hardware exists it runs on `../emulator/machines/backplane.machine`.

## bios/ ##

A minimal BIOS, built with cc65 (`make -C bios`). `bios.rom` is the whole
512 KB of ROM A, ready for a programmer, with the BIOS in its top 16 KB.

At reset the north bridge maps that block read-only at `$C000-$FFFF`
(`$C000-$C7FF` is the I/O hole). The BIOS copies itself into RAM block 63,
the top of bank F, points W3 and VEC at the read-only copy, puts W2 back on
RAM block 2 and starts the Woz Monitor on the terminal. The same image boots
when the bus spy has loaded it into RAM and held /BOOTSEL.

The terminal is a 6551 at `$C300`, a stand-in for the TL16C552A serial card;
the addresses are in `bios/backplane.inc`. NMI re-enters the monitor.

```
make -C bios
../emulator/build/release/apps/emu/emulator --machine ../emulator/machines/backplane.machine bios/bios.rom
```

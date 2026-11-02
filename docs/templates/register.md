# Template: a hardware register

One entry for each I/O address that an equate in [`backplane/bios/backplane.inc`](../../backplane/bios/backplane.inc)
names, in the Technical Reference's chapter for that card, in address order. A register wider than a byte has one
entry, at its lowest address. Copy the block below. The headings are fixed, in this order: `check_docs.py` fails an
entry that leaves one out, renames one or moves one. Only **Notes** may be left out.

- **The anchor** is `reg-` and the address in four lower-case hex digits (`reg-c680`), on its own line just before
  the heading.
- **The heading** is `### $ADDR NAME: what it is`: the address, the equate a program uses, and a few words.
- **Equates:** every equate in backplane.inc with this address, each in backticks (a module's base and its first
  register share one). The check fails if one is missing or has another address.
- **Module:** the module's ID in hex first (the address's: `($ADDR - $C000) / $80`), then the card's name, in
  the words `MODULES` in `check_docs.py` has for it (`console`, `north bridge`).
- **Access:** R, W or R/W, and what a read or write does besides (a read that clears a flag, a write that starts
  something).
- **Bits:** a table of bit, name and meaning, bit 7 first; or "All: " and what the byte holds.
- **Reset:** the value after a reset, in hex, or "Undefined."
- **Status:** `**built**` (on a real card), `**emulated**` (in the emulator, not yet built) or `**proposed**`
  (designed only).
- **Notes** (optional): timing, the order to write things in, the design record in hw/ for the why.

```markdown
<a id="reg-c000"></a>
### $C000 NAME: what it is

#### Equates

#### Module

#### Access

#### Bits

#### Reset

#### Status

#### Notes <!-- optional -->
```

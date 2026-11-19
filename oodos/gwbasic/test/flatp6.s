; The flat test image's stand-in for block P6 (the project's own;
; hw/oodos.md, "The plan, phase 7": 7d1). The flat image (test/flat.cfg)
; runs on the bare W65C02S core, never in screen mode (SCRMOD stays 0: P5's
; stand-in starts nothing), so P6, the screen, is left out of it (the
; Makefile's FLAT_SRC) and its entries come here, never reached.

.include "gwbasic.inc"

.export SCNIPL, TXRUN, SCINLN, KEYEV, EDTBRK
.export CLSP, LOCATP, GWWIDP, SWIDP, COLORP, SCRNP, SCRNFP, CSRLNP

.code

SCNIPL:
TXRUN:
SCINLN:
KEYEV:
EDTBRK:
CLSP:
LOCATP:
GWWIDP:
COLORP:
SCRNP:
SCRNFP:
SWIDP:
CSRLNP:         RTS

; The flat test image's stand-in for block P5 (the project's own;
; hw/oodos.md, "The plan, phase 7": 7c's tail). The flat image (test/flat.cfg)
; runs on the bare W65C02S core, with no OODOS, so there is no OOLIB to start
; nor any memory but its own: P5 is left out of it (the Makefile's FLAT_SRC)
; and its entries come here. Start-up's GXSTRT does nothing; DSBYTE (PEEK
; and POKE outside segment 0's $0000-$BFFF) is Advanced Feature. DSINFO (7e2:
; DSRUN's slot, for the home block's BLOAD and BSAVE run checks, DSRUNH,
; which the flat image leaves out with P8's far4.s and P4) gives nothing;
; DSPUT's slot is P5's retired one-byte stub, and is one here. The graphics
; entries (7e1, 7e2), which the resident core, P3 and the home block call in
; screen mode only (SCRMOD, never set here), are Advanced Feature too,
; GRPRST and CIRCC nothing.

.include "gwbasic.inc"

.export GXSTRT, DSBYTE, DSINFO, DSPUT, GRPRST, POINTP, PSETP, PRSETP, GLINEP, CIRCP, CIRCC, PAINTP, DRWOP
.import ADVERR

.code

GXSTRT:
GRPRST:
DSINFO:
DSPUT:
CIRCC:          RTS
DSBYTE:
POINTP:
PSETP:
PRSETP:
GLINEP:
CIRCP:
PAINTP:
DRWOP:          JMP     ADVERR

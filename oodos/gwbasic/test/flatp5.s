; The flat test image's stand-in for block P5 (the project's own;
; hw/oodos.md, "The plan, phase 7": 7c's tail). The flat image (test/flat.cfg)
; runs on the bare W65C02S core, with no OODOS, so there is no OOLIB to start
; nor any memory but its own: P5 is left out of it (the Makefile's FLAT_SRC)
; and its entries come here. Start-up's GXSTRT does nothing; DSBYTE (PEEK
; and POKE outside segment 0's $0000-$BFFF) and DSRUN (BLOAD, BSAVE) are
; Advanced Feature.

.include "gwbasic.inc"

.export GXSTRT, DSBYTE, DSRUN
.import ADVERR

.code

GXSTRT:         RTS
DSBYTE:
DSRUN:          JMP     ADVERR

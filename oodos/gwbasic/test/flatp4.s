; The flat test image's stand-in for block P4, the files (the project's own;
; hw/oodos.md, "The plan, phase 7": 7c). The flat image (test/flat.cfg) runs
; the math and programs on the bare W65C02S core, with no OODOS, so no file
; can open there: P4 is left out of it (the Makefile's FLAT_SRC), which keeps
; the image under check.py's $9000 (G11), and its entries come here. A file
; statement is Advanced Feature; the output hooks find nothing to flush.

.include "gwbasic.inc"

.export FILGET, FILINP, FILIND, DLINE, CLSALP, FILFNP, FINPRP, FLUSHP
.export OPENP, CLOSEP, RESETP, FIXINPP
.export LOADP, LOADP2, MERGEP, SAVEP, DSKCHP
.import ADVERR

.code

FILGET:
FILINP:
FILIND:
DLINE:
FILFNP:
OPENP:
CLOSEP:
RESETP:
FIXINPP:
LOADP:
LOADP2:
MERGEP:
SAVEP:
DSKCHP:         JMP     ADVERR
CLSALP:
FINPRP:
FLUSHP:         STZ     RUNLEN
                RTS

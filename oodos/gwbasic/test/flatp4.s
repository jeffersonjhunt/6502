; The flat test image's stand-in for block P4, the files (the project's own;
; hw/oodos.md, "The plan, phase 7": 7c). The flat image (test/flat.cfg) runs
; the math and programs on the bare W65C02S core, with no OODOS, so no file
; can open there (nor a device's: LPOS, DEVSOT, GIOINP here too, the 7c
; devices step's): P4 is left out of it (the Makefile's FLAT_SRC), and so are
; its statements' home block stubs (p8/far4.s) and CHAIN (p8/chain.s, since
; F3's M4), which keeps the image under
; check.py's limit (G11: $9000 until 7c, $9400 until 7d2, then $C000); its entries and those stubs come here. A file
; statement (and CHDIR, MKDIR, RMDIR, CHAIN, LIST's file), VARPTR(#n), and
; CVI to MKD$, are Advanced Feature; the output hooks find nothing to flush,
; and WRITE no random file.

.include "gwbasic.inc"

.export FILGET, FILINP, FILIND, DLINE, CLSALP, FILFNP, FINPRP, FLUSHP
.export OPENP, CLOSEP, RESETP, FIXINPP
.export LOADP, LOADP2, MERGEP, SAVEP, DSKCHP
.export OPEN, CLOSE, RESET, LOADS, MERGES, SAVES, HLRUN ; p8/far4.s's
.export FILES, KILL, NAME, CHDIR, MKDIR, RMDIR, FIELD, LSET, RSET, PUT, GET
.export CMPFBP, CHAIN, CHNRET, NAMSCN, NULOPM, GETPTR, DEVSWD
.export DEVSOT, GIOINP, LPOSP
.export ERDVGT, FILESQ                         ; OO-1: P8's ERDEVP's (the IBM spec's fix-up)
.export BSAVE, BLOAD                   ; p8/far4.s's (7c's tail)
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
DSKCHP:
OPEN:
CLOSE:
RESET:
LOADS:
MERGES:
SAVES:
HLRUN:
FILES:
KILL:
NAME:
CHDIR:
MKDIR:
RMDIR:
FIELD:
LSET:
RSET:
PUT:
GET:
CHAIN:
CHNRET:
NAMSCN:
DEVSWD:
NULOPM:
BSAVE:
BLOAD:
GETPTR:
LPOSP:
                JMP     ADVERR         ; (FUNDXP is P3's: in the image itself)
DEVSOT:                                ; no device's file opens here
GIOINP:         RTS                    ; no device to start
CMPFBP:         CLC                    ; WRITE's: not a random file
                RTS
CLSALP:
FINPRP:
FLUSHP:         STZ     RUNLEN
                RTS
FILESQ:
ERDVGT:         STZ     RBL            ; no physical error here
                RTS

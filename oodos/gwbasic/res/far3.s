; P3's resident stubs (the project's own): each statement of block P3 that a
; resident dispatch table names (res/tables.s's STMDSP) is reached through a
; stub here, a far call to its banked routine and back to NEWSTT, as
; res/gwmain.s's PRINT stub is (hw/oodos.md, "The plan, phase 7": far
; calls). FARCALL keeps A and the flags both ways, so the banked routine
; starts with CHRGTR's character and flags, as the statement would.
;
;   ONGOTO    ON ... GOTO, ON ... GOSUB, ON ERROR GOTO (p3/gwmain.s ONGOTOP)
;   ERRORS    ERROR n (p3/gwmain.s ERRORSP)
;   RESUME    RESUME, RESUME NEXT, RESUME n (p3/gwmain.s RESUMEP)
;
; Step a2's:
;
;   SWAP      SWAP a, b (p3/bimisc.s SWAPP)
;   ERASE     ERASE a[, b ...] (p3/bimisc.s ERASEP)
;   CLEAR     CLEAR [x][, n][, m] (p3/bimisc.s CLEARP). The banked part
;             checks and sets the memory map and returns; the stub then
;             jumps to CLEARC, as the 8086's CLEAR ends (GCLERC). CLEARC's
;             STKINI resets the 6502's stack, which a jump from the block
;             would leave its far call frame below; from here only NEWSTT's
;             return is on it, which STKINI keeps.
;   OPTION    OPTION BASE n (p3/gwmain.s OPTIONP)
;   RANDOM    RANDOMIZE [n] (p3/gwmain.s RANDOMP): C set back is a ^C at
;             the seed's prompt, STPEND's, as INPUT's (res/gwmain.s INPRET)
;   WRITE     WRITE (p3/fiveo.s WRITEP)
;   LHSMID    MID$(a$, n[, m]) = b$, from ISMID (p3/bistrs.s LHSMIDP)
;
; and two of EVAL's functions (res/gweval.s), back to EVAL's caller:
;
;   STRNGS    STRING$(n, c) (p3/bistrs.s STRNGP)
;   VARPT2    VARPTR$(v), from VARPTR (p3/gwsts.s VARPT2P)

.include "gwbasic.inc"

.export ONGOTO, ERRORS, RESUME
.export SWAP, ERASE, CLEAR, OPTION, RANDOM, WRITE, LHSMID, STRNGS, VARPT2
.import CLEARC, INPRET

.code

ONGOTO:         FJSR    ONGOTOP
                RTS
ERRORS:         FJSR    ERRORSP
                RTS
RESUME:         FJSR    RESUMEP
                RTS

SWAP:           FJSR    SWAPP
                RTS
ERASE:          FJSR    ERASEP
                RTS
CLEAR:          FJSR    CLEARP
                JMP     CLEARC         ; GCLERC (BIMISC.ASM 811)
OPTION:         FJSR    OPTIONP
                RTS
RANDOM:         FJSR    RANDOMP
                JMP     INPRET         ; C set: STPEND
WRITE:          FJSR    WRITEP
                RTS
LHSMID:         FJSR    LHSMIDP
                RTS
STRNGS:         FJSR    STRNGP
                RTS
VARPT2:         FJSR    VARPT2P
                RTS

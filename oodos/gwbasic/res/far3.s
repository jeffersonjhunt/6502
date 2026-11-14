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
;
; Step b's: DEF, and two more of EVAL's functions, back to EVAL's caller.
; FN and USR return through their stubs; neither ever jumps into the
; statement loop:
;
;   DEF       DEF FN, DEF USR (DEF SEG: 7c's) (p3/gweval.s DEFP)
;   FNDOER    FNname[(arguments)] (p3/gweval.s FNDOERP)
;   USRFN     USR[n](argument) (p3/gweval.s USRFNP)
;
; Step c's: two commands that end in the main loop, as LIST does. The 8086's
; AUTO pops NEWSTT's return and jumps to MAIN, RENUM's SCCALL returns to
; STPRDY: here the banked routine returns, and the stub drops NEWSTT's
; return and goes on, so no far call frame is left behind (EDIT, P2's, does
; the same from res/gwmain.s, whose EDTMAI and LSTRDY these share).
;
;   AUTO      AUTO [n][, [i]] (p3/gwmain.s AUTOP), then MAIN
;   RESEQ     RENUM [n][, [m][, i]] (p3/gwmain.s RESEQP), then READY

.include "gwbasic.inc"

.export ONGOTO, ERRORS, RESUME
.export SWAP, ERASE, CLEAR, OPTION, RANDOM, WRITE, LHSMID, STRNGS, VARPT2
.export DEF, FNDOER, USRFN, AUTO, RESEQ
.import CLEARC, INPRET, EDTMAI, LSTRDY

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

DEF:            FJSR    DEFP
                RTS
FNDOER:         FJSR    FNDOERP
                RTS
USRFN:          FJSR    USRFNP
                RTS

AUTO:           FJSR    AUTOP
                JMP     EDTMAI         ; POP CX, JMP MAIN
RESEQ:          FJSR    RESEQP
                JMP     LSTRDY         ; STPRDY

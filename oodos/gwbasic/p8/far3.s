; P3's statements' stubs, in the home block (the project's own; 7c's home
; block, hw/oodos.md, "The plan, phase 7"). Each statement of block P3 that
; STMDSP names (p8/home.s) is reached through a stub here, a far call to
; its banked routine and back to NEWSTT, as p8/gwmain.s's PRINT stub is.
; FARCALL keeps A and the flags both ways, so the banked routine starts with
; CHRGTR's character and flags, as the statement would.
;
;   ONGOTO    ON ... GOTO, ON ... GOSUB, ON ERROR GOTO (p3/gwmain.s ONGOTOP).
;             The chosen GOTO or GOSUB is started here, from the home block:
;             ONGOTOP leaves its token in ONTOK (res/home.s) and returns,
;             and the stub goes into GONE2 with it, as the 8086's tail call
;             does. (GONE2 reads STMDSP, which is in this block.)
;   ERRORS    ERROR n (p3/gwmain.s ERRORSP)
;   RESUME    RESUME, RESUME NEXT, RESUME n (p3/gwmain.s RESUMEP)
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
;             the seed's prompt, STPEND's, as INPUT's (p8/gwmain.s INPRET)
;   WRITE     WRITE (p3/fiveo.s WRITEP)
;   DEF       DEF FN, DEF USR (DEF SEG: 7c's) (p3/gweval.s DEFP)
;   AUTO      AUTO [n][, [i]] (p3/gwmain.s AUTOP), then MAIN
;   RESEQ     RENUM [n][, [m][, i]] (p3/gwmain.s RESEQP), then READY
;
; AUTO and RENUM end in the main loop, as LIST does. The 8086's AUTO pops
; NEWSTT's return and jumps to MAIN, RENUM's SCCALL returns to STPRDY: here
; the banked routine returns, and the stub drops NEWSTT's return and goes
; on, so no far call frame is left behind.

.include "gwbasic.inc"

.export ONGOTO, ERRORS, RESUME
.export SWAP, ERASE, CLEAR, OPTION, RANDOM, WRITE, DEF, AUTO, RESEQ, COMS, LCOPY
.import CLEARC, INPRET, EDTMAI, LSTRDY, ONTOK, GONE2

.code

ONGOTO:         FJSR    ONGOTOP
                LDA     ONTOK          ; GOTO or GOSUB chosen?
                BNE     :+
                RTS
:               STZ     ONTOK
                JMP     GONE2          ; the tail call, its token in A
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

DEF:            FJSR    DEFP
                RTS

AUTO:           FJSR    AUTOP
                JMP     EDTMAI         ; POP CX, JMP MAIN
RESEQ:          FJSR    RESEQP
                JMP     LSTRDY         ; STPRDY

; 7c's devices: COM(n) ON/OFF/STOP and LCOPY, STMDSX's (P3's: p3/gwsts.s).
COMS:           FJSR    COMSP
                RTS
LCOPY:          FJSR    LCOPYP
                RTS

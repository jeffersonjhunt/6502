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

.include "gwbasic.inc"

.export ONGOTO, ERRORS, RESUME

.code

ONGOTO:         FJSR    ONGOTOP
                RTS
ERRORS:         FJSR    ERRORSP
                RTS
RESUME:         FJSR    RESUMEP
                RTS

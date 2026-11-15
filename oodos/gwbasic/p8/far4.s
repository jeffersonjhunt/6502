; P4's statements' stubs, in the home block (the project's own; 7c's file
; layer, hw/oodos.md, "The plan, phase 7"). Each statement of block P4 that
; STMDSP or STMDSX names (p8/home.s) is reached through a stub here, a far
; call to its banked routine and back to NEWSTT, as p8/far3.s's are. FARCALL
; keeps A and the flags both ways, so the banked routine starts with
; CHRGTR's character and flags, as the statement would.
;
;   OPEN      OPEN mode, [#]n, name[, len] and OPEN name [FOR mode] AS [#]n
;             [LEN = len] (p4/gio86.s OPENP)
;   CLOSE     CLOSE [[#]n[, [#]n ...]] (p4/gio86.s CLOSEP)
;   RESET     RESET (p4/giodsk.s RESETP)
;
; None ends in the main loop: each returns, or ends in an error.

.include "gwbasic.inc"

.export OPEN, CLOSE, RESET

.code

OPEN:           FJSR    OPENP
                RTS
CLOSE:          FJSR    CLOSEP
                RTS
RESET:          FJSR    RESETP
                RTS

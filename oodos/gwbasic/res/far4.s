; P4's resident stubs (the project's own): what resident code calls of the
; file layer, block P4 (hw/oodos.md, "The plan, phase 7": 7c), each a far
; call to its banked routine and back to its caller. FARCALL keeps A, X, Y
; and the flags both ways. P4's statements' stubs (OPEN, CLOSE, RESET) are
; in the home block, p8/far4.s, since only its STMDSP and STMDSX name them.
;
;   CLSALL    every file closed (p4/gio86.s CLSALP), for END, CLEARC (RUN,
;             NEW, CLEAR) and SYSTEM; A kept. OO-1: with no file open
;             (FILOPN, res/oem.s) there is nothing to close, and no far call
;   FILFUN    EOF(n), LOC(n), LOF(n) from FUNDSP (res/tables.s), the
;             function in X as FINGO has it (p4/gio86.s FILFNP)
;   FIXINP    INPUT$(n[, [#]f]) from EVAL (res/gweval.s): FILFUN with X 0
;             (p4/dskcom.s FIXINPP)
;
; None ever jumps into the statement loop: each returns through its stub,
; or ends in an error.

.include "gwbasic.inc"

.export CLSALL, FILFUN, FIXINP
.import FILOPN

.code

CLSALL:         PHA                    ; A kept, as the 8086's PUSH AX keeps it
                LDA     FILOPN
                BEQ     :+
                FJSR    CLSALP
:               PLA
                RTS
FIXINP:         LDX     #0
FILFUN:         FJSR    FILFNP
                RTS

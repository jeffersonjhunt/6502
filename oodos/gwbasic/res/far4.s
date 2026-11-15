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
;   LRUN      RUN "file" (DSKCOM.ASM's LRUN), from the resident RUN, and a
;             program named on the command line (INITSA): S set back to
;             HOMSTK, as res/home.s's GOREADY does, then the home block's
;             HLRUN (p8/far4.s), LOAD's way with its flag 1, which never
;             returns: it ends in the main loop or an error (its SCRTCH
;             resets the stack). TXTPTR is at the file's name.
;
; None but LRUN ever jumps into the statement loop: each returns through its
; stub, or ends in an error.
;
; And one variable P4 reads (OO-1):
;
;   MAXREC    /S:, the most a random file's LEN may be (W4: at most 1,024;
;             GW-BASIC's own default, 128, while the command line's switches
;             are passed over). Resident, so start-up (P2) can set it from
;             the command line, as GWINIT.ASM's WASS reads /S:.

.include "gwbasic.inc"

.export CLSALL, FILFUN, FIXINP, LRUN, MAXREC
.import FILOPN, HOMSTK

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
LRUN:           LDX     HOMSTK
                TXS
                FJSR    HLRUN          ; never returns

.segment "RAM"
MAXREC:         .word   DATPSC         ; /S: (OO-1: W4)

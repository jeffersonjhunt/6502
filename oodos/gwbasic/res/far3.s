; P3's resident stubs (the project's own): each of P3's routines that
; resident code calls (EVAL's functions, ISMID's MID$ =) is reached
; through a stub here, a far call to its banked routine and back to its
; caller. FARCALL keeps A and the flags both ways. P3's statements' stubs
; are in the home block, p8/far3.s, since only the home block's STMDSP
; names them (7c's home block).
;
;   LHSMID    MID$(a$, n[, m]) = b$, from ISMID (p3/bistrs.s LHSMIDP)
;   STRNGS    STRING$(n, c) (p3/bistrs.s STRNGP)
;   VARPT2    VARPTR$(v), from VARPTR (p3/gwsts.s VARPT2P)
;   FNDOER    FNname[(arguments)] (p3/gweval.s FNDOERP)
;   USRFN     USR[n](argument) (p3/gweval.s USRFNP)
;
; FN and USR return through their stubs; neither ever jumps into the
; statement loop.

.include "gwbasic.inc"

.export LHSMID, STRNGS, VARPT2, FNDOER, USRFN

.code

LHSMID:         FJSR    LHSMIDP
                RTS
STRNGS:         FJSR    STRNGP
                RTS
VARPT2:         FJSR    VARPT2P
                RTS
FNDOER:         FJSR    FNDOERP
                RTS
USRFN:          FJSR    USRFNP
                RTS

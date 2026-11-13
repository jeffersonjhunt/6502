; Stand-ins, the project's own: what the translated code calls but 7a hasn't
; translated yet, so it links and runs (the spike's stubs.s, at first). Each
; goes as its routine is translated; what is left at 7a's end is listed in the
; ledger (ledger.txt) as later sub-phases' work. (The errors, END, OUTDO,
; EVALX, OCTCNS, NEWSTX, ISMID and CHKINT went at step I1; PRINT and
; STROUI at step I2.)
;
;   NOTSPK        Illegal function call, for a function not translated yet
;   the rest      USR, FN and the event traps' GOSUB: Illegal function call,
;                 or nothing

.include "gwbasic.inc"

.export NOTSPK, USRFN, FNDOER, GOTRP, FBUFF
.import FCERR

.code

; The untranslated routines.
USRFN:
FNDOER:
NOTSPK:         JMP     FCERR
GOTRP:          RTS

.segment "RAM"

FBUFF:          .res    32             ; $FOUT's output

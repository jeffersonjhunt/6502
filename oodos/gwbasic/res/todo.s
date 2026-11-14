; Stand-ins, the project's own: what the translated code calls but 7a hasn't
; translated yet, so it links and runs (the spike's stubs.s, at first). Each
; goes as its routine is translated; what is left at 7a's end is listed in the
; ledger (ledger.txt) as later sub-phases' work. (The errors, END, OUTDO,
; EVALX, OCTCNS, NEWSTX, ISMID and CHKINT went at step I1; PRINT and
; STROUI at step I2.)
;
;   NOTSPK        Illegal function call, for a function not translated yet
;   GOTRP         the event traps' GOSUB: nothing
;
; (USR and FN went at 7b's step b: res/far3.s.)

.include "gwbasic.inc"

.export NOTSPK, GOTRP, FBUFF
.import FCERR

.code

; The untranslated routines.
NOTSPK:         JMP     FCERR
GOTRP:          RTS

.segment "RAM"

; $FOUT's output and PRINT USING's (7b step d): FBUFFR's 43 (GWDATA.ASM), and
; 4 more (OO-1:): the widest field PRINUS allows, two places before the point
; with a comma and 22 after it and a trailing sign, of a double just under
; 10^16, has $PUF write to FBUFF+46 before $PUFXE gives it its "%". The 8086
; writes past its FBUFFR there too, into TSTACK's bytes, which hold nothing
; then; here the workspace follows (the program's first line).
FBUFF:          .res    47

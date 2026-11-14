; Stand-ins of step S (7a's variables and strings), the project's own: what
; BIPTRG.ASM's and BISTRS.ASM's routines reach that is a later sub-phase's.
; Each gives Advanced Feature (error 73) until its sub-phase translates it
; (ledger.txt):
;
;   NOTYET        VARPTR(#file) (GIO86.ASM's GETPTR, 7c)
;
; (ERSFIN, ERASE's entry, went at 7b's step a2, and VARPTR$'s use of NOTYET.)

.include "gwbasic.inc"

.export NOTYET
.import ERROR

ERRADV          = 73                   ; Advanced Feature

.code

NOTYET:         LDA     #ERRADV
                JMP     ERROR

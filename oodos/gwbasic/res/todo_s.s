; Stand-ins of step S (7a's variables and strings), the project's own: what
; BIPTRG.ASM's and BISTRS.ASM's routines reach that is a later sub-phase's.
; Each gives Advanced Feature (error 73) until its sub-phase translates it
; (ledger.txt):
;
;   ERSFIN        PTRGET's entry for ERASE (SUBFLG 1): 7b's ERASE
;   NOTYET        VARPTR$ (GWSTS.ASM's VARPT2, 7b) and VARPTR(#file)
;                 (GIO86.ASM's GETPTR, 7c)

.include "gwbasic.inc"

.export ERSFIN, NOTYET
.import ERROR

ERRADV          = 73                   ; Advanced Feature

.code

ERSFIN:
NOTYET:         LDA     #ERRADV
                JMP     ERROR

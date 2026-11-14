; Stand-ins of step I1 (7a's interpreter frame), the project's own: what the
; frame reaches that is a later sub-phase's. Each gives Advanced Feature
; (error 73) until its sub-phase translates it (ledger.txt):
;
;   ADVERR        the statements and functions of 7b to 7g in STMDSP, STMDSX
;                 and EVALX (AUTO, RENUM, EDIT, the files, the screen, the
;                 sound and the rest)
;   LRUN          RUN "file" (DSKCOM.ASM, 7c)
;
; (LHSMID, MID$ as a statement, went at 7b's step a2: res/far3.s.)

.include "gwbasic.inc"

.export ADVERR, LRUN
.import ERROR

ERRADV          = 73                   ; Advanced Feature

.code

ADVERR:
LRUN:           LDA     #ERRADV
                JMP     ERROR

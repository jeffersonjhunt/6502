; The graphics statements' stubs, in the home block (the project's own;
; 7e1, hw/oodos.md, "The plan, phase 7"). Each far calls its routine in
; block P5 (p5/gengrp.s) in screen mode, and is Advanced Feature in console
; mode, as it was (7d's answer 7: a serial terminal has no card memory
; GW-BASIC set up). LINE's is p8/gwmain.s's (LINE INPUT's too).
;
;   PSET PRESET   GENGRP.ASM's

.include "gwbasic.inc"

.export PSET, PRESET
.import ADVERR, SCRMOD

.code

PSET:           BIT     SCRMOD
                BPL     ADV
                FJSR    PSETP
                RTS
PRESET:         BIT     SCRMOD
                BPL     ADV
                FJSR    PRSETP
                RTS
ADV:            JMP     ADVERR

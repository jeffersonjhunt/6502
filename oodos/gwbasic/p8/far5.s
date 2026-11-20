; The graphics statements' stubs, in the home block (the project's own;
; 7e1, hw/oodos.md, "The plan, phase 7"). Each far calls its routine in
; block P5 (p5/gengrp.s) in screen mode, and is Advanced Feature in console
; mode, as it was (7d's answer 7: a serial terminal has no card memory
; GW-BASIC set up). LINE's is p8/gwmain.s's (LINE INPUT's too).
;
;   PSET PRESET   GENGRP.ASM's
;   CIRCLE        ADVGRP.ASM's (7e2): P5's CIRCP, which parses it in P3
;   PAINT         ADVGRP.ASM's (7e2): P5's PAINTP
;   DRAW          ADVGRP.ASM's (7e2): P3's DRAWP (its drawing P5's DRWOP)
;   VIEW WINDOW   the OO-1's (7e4; the source's are Advanced Feature):
;                 P5's GVIEW, P1's WINSET

.include "gwbasic.inc"

.export PSET, PRESET, CIRCLE, PAINT, DRAW, VIEW, WINDOW
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
CIRCLE:         BIT     SCRMOD
                BPL     ADV
                FJSR    CIRCP
                RTS
PAINT:          BIT     SCRMOD
                BPL     ADV
                FJSR    PAINTP
                RTS
DRAW:           BIT     SCRMOD
                BPL     ADV
                FJSR    DRAWP
                RTS
VIEW:           BIT     SCRMOD
                BPL     ADV
                FJSR    GVIEW
                RTS
WINDOW:         BIT     SCRMOD
                BPL     ADV
                FJSR    WINSET
                RTS
ADV:            JMP     ADVERR

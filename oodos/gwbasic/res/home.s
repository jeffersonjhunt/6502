; The way back into the home block (the project's own; hw/oodos.md, "The
; plan, phase 7": 7c's home block, p8/home.s). Start-up (INITSA) keeps the
; 6502's S in HOMSTK just before its far call into the home block, which
; never returns. Resident code that ends in the main loop, wherever W3 is
; (an error raised in a block, a ^C, P2's line entry), comes here: S set
; back to HOMSTK, which drops every far call's frame, then the far call
; again. So the stack under the home block is the same far call frame at
; every entry, never one more (SAVSTK would leave one at each).
;
;   GOREADY   READY (ERROR's message, ENDCON's end, ^C, start-up)
;   GOGON4    GONE4: ON ERROR's routine, TXTPTR at its line
;   FINI      P2's way to FINI, its name kept: P2's lines entered and
;             deleted end there
;
; ONTOK: ON's GOTO or GOSUB token, from P3's ONGOTOP to its home stub
; (p8/far3.s), zero otherwise.

.include "gwbasic.inc"

.export GOREADY, GOGON4, FINI, HOMSTK, ONTOK

.code

GOREADY:        LDX     HOMSTK
                TXS
                FJSR    HREADY         ; never returns
GOGON4:         LDX     HOMSTK
                TXS
                FJSR    HGONE4
FINI:           LDX     HOMSTK
                TXS
                FJSR    HFINI

.segment "RAM"
HOMSTK:         .res    1              ; S just before start-up's far call
ONTOK:          .res    1

; The skeleton's start-up (the project's own): the banner. GWINIT's INIT
; replaces it.

.include "gwbasic.inc"

;@entry GWINIT
.export GWINIT
.import STROUT

.code
GWINIT:         LDA     #<MBAN
                STA     PTR
                LDA     #>MBAN
                STA     PTR + 1
                JMP     STROUT

.rodata
MBAN:           .byte   "GW-BASIC for the OO-1", 13, 10, 0

; GW-BASIC's OEM layer for the console, 7a's (hw/oodos.md, "The plan, phase
; 7": 7a, the console through the BDOS). The project's own, not Microsoft's:
; the release has no OEM code (hw/oodos.md, "GW-BASIC").
;
;   CONOUT    A to the console by function 2; A, X, Y and the flags kept
;   CONST     a key waiting? Z clear if so (function 11)
;   CONIN     the next key, waited for (function 6 with $FD)
;   FARMISS   a far call made before the loader patched it: can't happen
;             once loaded, so it says so and ends

.include "gwbasic.inc"

.export CONOUT, CONST, CONIN, FARMISS

        OODOS_HEADER

.import LOAD
.segment "STARTUP"
                JMP     LOAD           ; $0300: the loader, once
.import STROUT

.code

CONOUT:         PHP
                PHA
                PHX
                PHY
                LDY     #C_WRITE
                JSR     BDOS
                PLY
                PLX
                PLA
                PLP
                RTS

CONST:          PHX
                PHY
                LDY     #C_STAT
                JSR     BDOS
                PLY
                PLX
                CMP     #0
                RTS

CONIN:          PHX
                PHY
@w:             LDA     #$FD
                LDY     #C_RAWIO
                JSR     BDOS
                PLY
                PLX
                RTS

FARMISS:        LDA     #<MMISS
                STA     PTR
                LDA     #>MMISS
                STA     PTR + 1
                JSR     STROUT
                JMP     WBOOT

.rodata
MMISS:          .byte   13, 10, "?far call before GW-BASIC was loaded", 13, 10, 0

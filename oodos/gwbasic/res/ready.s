; The skeleton's READY loop (7a's first step, the project's own): "Ok", a line
; by function 10, SYSTEM back to OODOS. GWMAIN's MAIN replaces it.

.include "gwbasic.inc"

.export READY, OUTDO, FTSITE
.import CONOUT, STROUT

.code

OUTDO           = CONOUT

READY:
FTSITE:         FJSR    FARTST         ; a crossing, for the far call's cost
                LDA     #<MOK
                STA     PTR
                LDA     #>MOK
                STA     PTR + 1
                JSR     STROUT
                LDA     #<RBUF
                LDX     #>RBUF
                LDY     #C_READSTR
                JSR     BDOS
                LDA     #13
                JSR     CONOUT
                LDA     #10
                JSR     CONOUT
                LDX     #0
@cmp:           CPX     RBUF + 1
                BEQ     @end
                LDA     RBUF + 2,X
                AND     #$DF
                CMP     MSYS,X
                BNE     READY
                INX
                BRA     @cmp
@end:           CPX     #6
                BNE     READY
                JMP     WBOOT

.rodata
MOK:            .byte   "Ok", 13, 10, 0
MSYS:           .byte   "SYSTEM"

.segment "RAM"
RBUF:           .byte   255
                .res    256

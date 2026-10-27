; OODOS's cold boot (hw/oodos.md, "The system, phase 2"), in block 62: OODOS.SYS's
; entry. OOLDR jumps here with W0 on block 0, W1-W3 and VEC on 60, 61, 62
; and 62, interrupts off and D clear.
;
; It takes the system block (31) as W0 and its stack, clears the system's
; zero page and data, sets the SCB, starts the BIOS (the devices, the drives
; from the card's units table), reads the clock into the SCB, makes M:,
; says who it is with the time, and goes to the CCP by the warm boot.

.setcpu "65C02"
.include "sys.inc"
.import WARM, BINIT, MFORMAT, DAYS2DATE
.importzp TYEAR, TMONTH, TDATE
.export COLD

.segment "RES"

COLD:           SEI
                CLD
                LDA     #SYS_BLOCK
                STA     NB_WIN0
                LDX     #$FF
                TXS
                LDA     #0             ; $0000-$0AFF; the stack is empty
                TAX
@clear:
.repeat >SYS_CLEAR_END, page
                STA     page * $100,X
.endrepeat
                INX
                BNE     @clear
                LDX     #0
@scb:           LDY     SCBINIT,X      ; the SCB's settings: offset, value
                LDA     SCBINIT + 1,X
                STA     SCB,Y
                INX
                INX
                CPX     #SCBINIT_LEN
                BNE     @scb
                JSR     BINIT
                LDA     #0
                JSR     JT(BIOS::TIME)
                JSR     MFORMAT
                LDX     #0
@name:          LDA     BANNER,X
                BEQ     @date
                JSR     PUT
                INX
                BRA     @name
@date:          JSR     DAYS2DATE
                BCS     @end           ; before 2000: no date to show
                LDA     #'2'           ; 20yy or 21yy
                JSR     PUT
                LDA     TYEAR
                LDX     #'0'
                CMP     #100
                BCC     @century
                SBC     #100
                LDX     #'1'
@century:       PHA
                TXA
                JSR     PUT
                PLA
                JSR     DEC2
                LDA     #'-'
                JSR     PUT
                LDA     TMONTH
                JSR     DEC2
                LDA     #'-'
                JSR     PUT
                LDA     TDATE
                JSR     DEC2
                LDA     #' '
                JSR     PUT
                LDA     SCB_HOUR
                JSR     HEX2
                LDA     #':'
                JSR     PUT
                LDA     SCB_MIN
                JSR     HEX2
                LDA     #':'
                JSR     PUT
                LDA     SCB_SEC
                JSR     HEX2
@end:           LDA     #$0D
                JSR     PUT
                LDA     #$0A
                JSR     PUT
                JMP     WARM

PUT:            PHX
                JSR     JT(BIOS::CONOUT)
                PLX
                RTS

; A (0-99) as two decimal digits.
DEC2:           LDX     #'0'
@tens:          CMP     #10
                BCC     @ones
                SBC     #10
                INX
                BRA     @tens
@ones:          PHA
                TXA
                JSR     PUT
                PLA
                ORA     #'0'
                JMP     PUT

; A (BCD) as two digits.
HEX2:           PHA
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     #'0'
                JSR     PUT
                PLA
                AND     #$0F
                ORA     #'0'
                JMP     PUT

BANNER:         .byte   $0D, $0A, "OODOS ", '0' + OODOS_MAJOR, '.', '0' + OODOS_MINOR, "  ", 0

SCBINIT:        .byte   <(SCB_VERSION - SCB), $31
                .byte   <(SCB_WIDTH - SCB), 79
                .byte   <(SCB_PAGELEN - SCB), 24
                .byte   <(SCB_CIVEC + 1 - SCB), VEC_CRT | VEC_TTY ; CON: is CRT and TTY
                .byte   <(SCB_COVEC + 1 - SCB), VEC_CRT | VEC_TTY
                .byte   <(SCB_AIVEC + 1 - SCB), VEC_TTY           ; AUX: is TTY
                .byte   <(SCB_AOVEC + 1 - SCB), VEC_TTY
                .byte   <(SCB_LOVEC + 1 - SCB), VEC_LPT           ; LST: is LPT
                .byte   <(SCB_DELIM - SCB), '$'
                .byte   <(SCB_DMA - SCB), <BP_DMA
                .byte   <(SCB_DMA + 1 - SCB), >BP_DMA
SCBINIT_LEN     = * - SCBINIT

; The base page, from oodos.inc
BP_DMA          = $0280

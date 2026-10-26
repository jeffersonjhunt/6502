; /BOOT.BIN on an OODOS card, until OOLDR (phase 2) takes its place: it
; says what the card is and returns to the monitor. The BIOS's C80CR loads
; it at $1000 and calls it (backplane/bios/sd.s); $C80F is ECHO, which keeps
; A and X.

.setcpu "65C02"

ECHO = $C80F

.segment "CODE"

BOOT:           LDX     #0
@next:          LDA     MSG,X
                BEQ     @done
                JSR     ECHO
                INX
                BNE     @next
@done:          RTS

MSG:            .byte   $0D, "THIS IS AN OODOS CARD. NO LOADER YET: BACK TO THE MONITOR", $0D, 0

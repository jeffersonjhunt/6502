; A sketch of OODOS's FARCALL (hw/oodos.md, "Far calls"), to time and check
; it: the spike's harness calls it (check.py), and README.md gives the cycles. Not GW-BASIC's;
; in OODOS it lives in common memory ($F800-$FFFF), the same copy in every
; block W3 maps.
;
;   JSR FARCALL / .byte block / .word target
;
; maps the block into W3, calls the target, and maps the caller's block
; back. A, X, Y and the flags reach the target as the caller left them, and
; come back as the target left them, so a banked statement still gets
; CHRGTR's character and flags. The old block goes on the 6502's stack, so
; far calls nest.
;
; This is OODOS's code, not a program's: it reads its frame in page 1 by
; address (a 65C816 would use stack-relative addressing), and modifies its
; own operand, in the block it is leaving.

.setcpu "65C02"
.export FARCALL

NB_WIN3         = $C683

.segment "FARCALL"

FARCALL:        PHP
                PHA
                PHX
                PHY
                TSX                    ; $0101,X Y, X, A, P, then the return address
                LDA     $0105,X        ; the inline bytes follow it
                STA     FCRD+1
                LDA     $0106,X
                STA     FCRD+2
                CLC                    ; return past them
                LDA     $0105,X
                ADC     #3
                STA     $0105,X
                BCC     :+
                INC     $0106,X
:               TXA                    ; 5 bytes more, under the registers
                SEC
                SBC     #5
                TAX
                TXS
                LDA     $0106,X        ; the registers down to the top
                STA     $0101,X
                LDA     $0107,X
                STA     $0102,X
                LDA     $0108,X
                STA     $0103,X
                LDA     $0109,X
                STA     $0104,X
                LDA     NB_WIN3        ; the caller's block, for FCRET
                STA     $0109,X
                LDA     #<(FCRET-1)
                STA     $0107,X
                LDA     #>(FCRET-1)
                STA     $0108,X
                LDY     #2             ; the target, less one, for RTS
                JSR     FCRD
                SEC
                SBC     #1
                STA     $0105,X
                INY
                JSR     FCRD
                SBC     #0
                STA     $0106,X
                LDY     #1             ; the block
                JSR     FCRD
                STA     NB_WIN3
                PLY
                PLX
                PLA
                PLP
                RTS                    ; to the target
FCRD:           LDA     $FFFF,Y        ; the operand is the call's return address
                RTS
FCRET:          PHP                    ; the target's return: the old block back
                PHA
                PHX                    ; TSX needs X
                TSX                    ; $0101,X X, A, P, the block, the return
                LDA     $0104,X
                STA     NB_WIN3
                LDA     $0103,X        ; drop the block from under X, A and P
                STA     $0104,X
                LDA     $0102,X
                STA     $0103,X
                LDA     $0101,X
                STA     $0102,X
                PLA                    ; the stale copy of X
                PLX
                PLA
                PLP
                RTS

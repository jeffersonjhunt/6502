; DICE: rolls five dice by BDOS function 78, O_RANDOM, then ends by the warm
; boot. The Programmer's Guide's example for function 78. RN_FILL fills the
; block's bytes from the north bridge's random number chip; a byte of 252 or
; more is drawn again, so that each face of 1-6 is as likely (252 = 42 x 6).

        .include "oodos.inc"

        OODOS_HEADER

DICE    = 5

        .zeropage
n:      .res    1                       ; the dice still to roll

        .code
start:  lda     #RN_INFO                ; a source? (X = 0: no block)
        ldx     #0
        ldy     #O_RANDOM
        jsr     BDOS
        and     #RS_CHIP
        beq     none
        lda     #<head
        ldx     #>head
        ldy     #C_WRITESTR
        jsr     BDOS
        lda     #DICE
        sta     n
roll:   lda     #<blk                   ; one byte into blk + 2
        ldx     #>blk
        ldy     #O_RANDOM
        jsr     BDOS
        cmp     #0
        bne     none                    ; $FF $FF: refused
        lda     blk + 2
        cmp     #252
        bcs     roll                    ; drawn again
@mod:   cmp     #6                      ; A mod 6
        bcc     @face
        sbc     #6
        bcs     @mod                    ; (carry still set: always)
@face:  clc
        adc     #'1'
        ldy     #C_WRITE
        jsr     BDOS
        lda     #' '
        ldy     #C_WRITE
        jsr     BDOS
        dec     n
        bne     roll
        lda     #<crlf
        ldx     #>crlf
        ldy     #C_WRITESTR
        jsr     BDOS
        jmp     WBOOT
none:   lda     #<nosrc
        ldx     #>nosrc
        ldy     #C_WRITESTR
        jsr     BDOS
        jmp     WBOOT

        .data
blk:    .byte   RN_FILL, 1, 0           ; the operation, n, the byte

        .rodata
head:   .byte   "Five dice: $"
crlf:   .byte   13, 10, '$'
nosrc:  .byte   "No random source", 13, 10, '$'

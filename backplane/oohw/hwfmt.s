; HW_PUTS, HW_HEX2, HW_HEX4, HW_HEX6 and HW_DEC (oohw.inc): strings and
; numbers through the program's HW_PUTC.

        .setcpu "65C02"
        .include "oohw.inc"

        .code
; The 0-ended string at A/X; Y its length.
HW_PUTS:
        sta     HW_PTR
        stx     HW_PTR + 1
        ldy     #0
@c:     lda     (HW_PTR),y
        beq     @end
        jsr     HW_PUTC                 ; keeps Y
        iny
        bne     @c
@end:   rts

HW_HEX6:
        pha
        phx
        tya
        jsr     HW_HEX2
        plx
        pla
HW_HEX4:
        pha
        txa
        jsr     HW_HEX2
        pla
HW_HEX2:
        pha
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jsr     @dig
        pla
        pha
        and     #$0F
        jsr     @dig
        pla
        rts
@dig:   cmp     #10
        bcc     @num
        adc     #'A' - 10 - 1 - '0'     ; carry set
@num:   adc     #'0'
        jmp     HW_PUTC

; HW_NUM in decimal; carry set: a comma every three digits.
HW_DEC: lda     #0
        rol     a
        sta     commas
        stz     ndig
@div:   lda     #10                     ; the digits, lowest first
        ldx     #0
        jsr     HW_DIV
        ldx     ndig
        sta     digits,x
        inc     ndig
        lda     HW_NUM
        ora     HW_NUM + 1
        ora     HW_NUM + 2
        ora     HW_NUM + 3
        bne     @div
@out:   dec     ndig
        ldx     ndig
        lda     digits,x
        ora     #'0'
        jsr     HW_PUTC
        lda     ndig
        beq     @done
        lda     commas
        beq     @out
        ldx     ndig                    ; a comma before 3, 6 and 9 digits left
        cpx     #3
        beq     @comma
        cpx     #6
        beq     @comma
        cpx     #9
        bne     @out
@comma: lda     #','
        jsr     HW_PUTC
        bra     @out
@done:  rts

        .bss
digits: .res    10
ndig:   .res    1
commas: .res    1

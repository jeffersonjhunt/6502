; Printing for OODOS.SYS's test programs (test_oodos_sys.cpp reads what they
; print): the BDOS only, so they keep the rules for programs. Included once,
; at the end, after the program's code. Each keeps nothing.
;
;   PUTC     A
;   PUTS     the '$'-ended string at A (low) and X (high)
;   CRLF
;   SPACE
;   HEX2     A as two hex digits
;   HEX4     A (low) and X (high) as four, high first
;   HEXN     the X bytes at PTR, as hex, no spaces
; Its zero page is printzp.inc's, included before the code, so operands
; are zero page ones (rule 2).

        .code
PUTC:   ldy     #C_WRITE
        jmp     BDOS
PUTS:   ldy     #C_WRITESTR
        jmp     BDOS
CRLF:   lda     #13
        jsr     PUTC
        lda     #10
        jmp     PUTC
SPACE:  lda     #' '
        jmp     PUTC
HEX4:   pha
        txa
        jsr     HEX2
        pla
HEX2:   sta     HEXB
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jsr     @digit
        lda     HEXB
        and     #$0F
@digit: cmp     #10
        bcc     @num
        adc     #'A' - '0' - 10 - 1     ; carry set
@num:   adc     #'0'
        jmp     PUTC
HEXN:   stx     HEXC
        ldy     #0
@byte:  cpy     HEXC
        beq     @done
        lda     (PTR),y
        phy
        jsr     HEX2
        ply
        iny
        bra     @byte
@done:  rts

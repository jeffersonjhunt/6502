; RNDTEST: function 78, the random source (OODOS 0.16, interface 17;
; hw/design.md, "The random source"), for test_oodos_random.cpp. The command tail:
;
;   I        INFO aa xx (RN_INFO, no block)
;   F n ...  for each n (two hex digits, space between): RN_FILL of n into
;            a block whose bytes past +2 + n are $EE first: FILL aa xx, then
;            the n bytes (hex, no spaces) and OVER o (1 if a byte past +2 + n
;            changed)
;
; then the warm boot.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .zeropage
tp:     .res    1                       ; the tail's place
n:      .res    1
sx:     .res    1

        .code
start:  ldx     #1                      ; past the tail's leading space
        stx     tp
        lda     BP_DMA + 1
        cmp     #' '
        bne     @l
        inc     tp
@l:     ldx     tp
        lda     BP_DMA,x
        cmp     #'I'
        beq     case_i
        cmp     #'F'
        beq     case_f
        jmp     WBOOT

case_i: lda     #RN_INFO
        ldx     #0
        ldy     #O_RANDOM
        jsr     BDOS
        pha
        stx     sx
        lda     #<sinfo
        ldx     #>sinfo
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     sx
        jsr     HEX2
        jsr     CRLF
        jmp     WBOOT

case_f: inc     tp                      ; past the F
@next:  ldx     tp                      ; skip spaces, to the tail's end (BP_DMA its length)
        cpx     BP_DMA
        beq     :+
        bcs     @end
:       lda     BP_DMA,x
        cmp     #' '
        bne     @num
        inc     tp
        bra     @next
@num:   jsr     hexdig
        asl     a
        asl     a
        asl     a
        asl     a
        sta     n
        inc     tp
        ldx     tp
        lda     BP_DMA,x
        jsr     hexdig
        ora     n
        sta     n
        inc     tp
        jsr     fill
        bra     @next
@end:   jmp     WBOOT

; A hex digit's value (A, uppercase).
hexdig: cmp     #'A'
        bcc     @d
        sbc     #'A' - 10
        rts
@d:     sec
        sbc     #'0'
        rts

; RN_FILL of n: the block filled with $EE first, then the call; FILL aa xx,
; the n bytes, OVER o.
fill:   ldx     #0
@ee:    lda     #$EE
        sta     blk,x
        inx
        bne     @ee
        ldx     #15
@ee2:   sta     blk + 256,x
        dex
        bpl     @ee2
        lda     #RN_FILL
        sta     blk
        lda     n
        sta     blk + 1
        lda     #<blk
        ldx     #>blk
        ldy     #O_RANDOM
        jsr     BDOS
        pha
        stx     sx
        lda     #<sfill
        ldx     #>sfill
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     sx
        jsr     HEX2
        jsr     CRLF
        ldx     #0
@b:     cpx     n
        beq     @over
        lda     blk + 2,x
        phx
        jsr     HEX2
        plx
        inx
        bne     @b
@over:  jsr     CRLF
        lda     #<sover
        ldx     #>sover
        jsr     PUTS
        ldy     #0                      ; anything past +2 + n changed?
        lda     n
        clc
        adc     #2
        tax
        bcs     @hi                     ; (n 254, 255: from blk + 256)
@lo:    lda     blk,x
        cmp     #$EE
        beq     :+
        ldy     #1
:       inx
        bne     @lo
        ldx     #0
@hi:    lda     blk + 256,x
        cmp     #$EE
        beq     :+
        ldy     #1
:       inx
        cpx     #16
        bne     @hi
        tya
        jsr     HEX2
        jmp     CRLF

        .rodata
sinfo:  .byte   "INFO $"
sfill:  .byte   "FILL $"
sover:  .byte   "OVER $"

        .bss
blk:    .res    256 + 16

        .include "print.inc"

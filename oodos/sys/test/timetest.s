; TIMETEST: the clock through functions 104 and 105, for test_oodos_sys.cpp.
; With a tail " dddd hh mm" (hex days, the hour and minute in BCD) it sets the
; date and time first, by function 104. Then function 105:
;
;   TIME dddd hh mm ss

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  ldx     BP_DMA
        beq     @get
        ldx     #1
        jsr     hexbyte
        sta     dat+1
        jsr     hexbyte
        sta     dat
        jsr     hexbyte
        sta     dat+2
        jsr     hexbyte
        sta     dat+3
        lda     #<dat
        ldx     #>dat
        ldy     #T_SET
        jsr     BDOS
@get:   lda     #<dat
        ldx     #>dat
        ldy     #T_GET
        jsr     BDOS
        pha
        lda     #<timemsg
        ldx     #>timemsg
        jsr     PUTS
        lda     dat
        ldx     dat+1
        jsr     HEX4
        jsr     SPACE
        lda     dat+2
        jsr     HEX2
        jsr     SPACE
        lda     dat+3
        jsr     HEX2
        jsr     SPACE
        pla
        jsr     HEX2
        jsr     CRLF
        jmp     WBOOT

; Two hex digits from the tail at X, spaces before them skipped, into A.
hexbyte:
@space: lda     BP_DMA,x
        inx
        cmp     #' '
        beq     @space
        jsr     nibble
        asl     a
        asl     a
        asl     a
        asl     a
        sta     hi
        lda     BP_DMA,x
        inx
        jsr     nibble
        ora     hi
        rts
nibble: cmp     #'A'
        bcc     @digit
        sbc     #'A' - 10
        rts
@digit: sec
        sbc     #'0'
        rts

        .rodata
timemsg: .byte  "TIME $"

        .bss
dat:    .res    4
hi:     .res    1

        .include "print.inc"

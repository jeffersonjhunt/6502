; RSXQ: test_oodos_rsx's program (hw/oodos.md, "The RSXs, phase 5"), built
; plain and with test RSXs attached (the Makefile's gencom.py). It does what
; its tail says, a character at a time, each answer followed by a blank:
;
;   #       '#' by function 2 (each test RSX writes its letter first)
;   ?c      60 with function byte c: "?c=xxxx", X then A as hex
;   -c      60 with function byte c + $20 (an RSX's own remove flag set)
;   T       "T=xxxx": $0204's word
;   0       59 with A/X = 0: "L=aa"
;   Sxx     "Sxx=vv": SCB byte xx (hex) by 49
;   W       $0203 made JMP $BFFD, past the RSXs (the TPA rule broken)
;   X       the lowest RSX's offset 6 made 0 (the TPA rule broken), then the
;           end by JMP $0200 with no BDOS call
;   J       the end by JMP $0200
;   Z       the end by function 0
;   Q       the end by an error: 14 of drive Q in the default error mode
;   K       a line read by 10 (a ^C typed there ends it)
;   C...    the rest of the tail chained to by 47
;
; then CR LF and the end by RTS.

        .setcpu "65C02"
        .include "oodos.inc"
        .macpack longbranch

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  ldx     #0                      ; past the tail's blank
        stx     at
next:   ldx     at
        cpx     BP_DMA
        bne     @more
        jsr     CRLF
        rts
@more:  lda     BP_DMA + 1,x
        inc     at
        cmp     #'#'
        jeq     hash
        cmp     #'?'
        jeq     query
        cmp     #'-'
        jeq     remove
        cmp     #'T'
        jeq     top
        cmp     #'0'
        jeq     drop
        cmp     #'S'
        jeq     scb
        cmp     #'W'
        jeq     bypass
        cmp     #'X'
        jeq     wreck
        cmp     #'J'
        jeq     jend
        cmp     #'Z'
        jeq     zend
        cmp     #'Q'
        jeq     qend
        cmp     #'K'
        jeq     kline
        cmp     #'C'
        jeq     chain
        jmp     next

hash:   lda     #'#'
        jsr     PUTC
        jmp     blank

query:  jsr     nextc
        sta     rsxpb
        pha
        lda     #'?'
        jsr     PUTC
        pla
        jsr     PUTC
call60: lda     #'='
        jsr     PUTC
        lda     #<rsxpb
        ldx     #>rsxpb
        ldy     #P_RSX
        jsr     BDOS
        jsr     HEX4
        jmp     blank

remove: jsr     nextc
        pha
        ora     #$20
        sta     rsxpb
        lda     #'-'
        jsr     PUTC
        pla
        jsr     PUTC
        jmp     call60

top:    lda     #<stop
        ldx     #>stop
        jsr     PUTS
        lda     BP_TOP
        ldx     BP_TOP + 1
        jsr     HEX4
        jmp     blank

drop:   lda     #<sdrop
        ldx     #>sdrop
        jsr     PUTS
        lda     #0
        tax
        ldy     #P_LOAD
        jsr     BDOS
        jsr     HEX2
        jmp     blank

scb:    jsr     nextc
        jsr     hexdig
        asl     a
        asl     a
        asl     a
        asl     a
        sta     scbpb
        jsr     nextc
        jsr     hexdig
        ora     scbpb
        sta     scbpb
        lda     #'S'
        jsr     PUTC
        lda     scbpb
        jsr     HEX2
        lda     #'='
        jsr     PUTC
        lda     #<scbpb
        ldx     #>scbpb
        ldy     #S_SCB
        jsr     BDOS
        jsr     HEX2
        jmp     blank

bypass: lda     #<BDOS_TOP
        sta     BP_TOP
        lda     #>BDOS_TOP
        sta     BP_TOP + 1
        jmp     next

wreck:  lda     BP_TOP                  ; $0204: the lowest RSX's offset 6
        sta     PTR
        lda     BP_TOP + 1
        sta     PTR + 1
        lda     #0
        sta     (PTR)
        jmp     WBOOT

jend:   jmp     WBOOT

zend:   ldy     #P_TERMCPM
        jmp     BDOS

qend:   lda     #16                     ; Q: no such drive, said, the program ended
        ldy     #DRV_SET
        jsr     BDOS
        jmp     next

kline:  lda     #<line
        ldx     #>line
        ldy     #C_READSTR
        jsr     BDOS
        jmp     next

chain:  ldx     at                      ; the rest of the tail to $0280, a 0 after
        ldy     #0
@copy:  cpx     BP_DMA
        beq     @end
        lda     BP_DMA + 1,x
        sta     cbuf,y
        inx
        iny
        bra     @copy
@end:   lda     #0
        sta     cbuf,y
        ldx     #0
@put:   lda     cbuf,x
        sta     BP_DMA,x
        beq     @go
        inx
        bra     @put
@go:    lda     #0
        ldy     #P_CHAIN
        jmp     BDOS

blank:  lda     #' '
        jsr     PUTC
        jmp     next

; The tail's next character (0 at its end).
nextc:  ldx     at
        cpx     BP_DMA
        beq     @none
        lda     BP_DMA + 1,x
        inc     at
        rts
@none:  lda     #0
        rts

hexdig: cmp     #'A'
        bcc     @num
        sbc     #'A' - 10
        rts
@num:   sec
        sbc     #'0'
        rts

        .include "print.inc"

BDOS_TOP = $BFFD

        .rodata
stop:   .byte   "T=$"
sdrop:  .byte   "L=$"

        .data
rsxpb:  .byte   0, 0                    ; the function byte, no words
scbpb:  .byte   0, 0, 0, 0              ; 49: get
line:   .byte   20, 0
        .res    20

        .bss
at:     .res    1
cbuf:   .res    128

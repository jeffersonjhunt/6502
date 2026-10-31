; ARGS: what the CCP gave a program (hw/oodos.md, "The CCP, phase 4"), for
; test_oodos_ccp.cpp's cases. test_oodos_ccp.cpp copies it onto drives
; under other names, each copy's tag (the 4 bytes after "TAG:") its own,
; so a case can tell which copy ran. It prints:
;
;   ARGS tttt           its tag
;   TAIL nn [text]      $0280's length and characters
;   F1 hex              $025C's 16 bytes
;   F2 hex              $026C's 16 bytes, then $027C-$027F's 4
;   BP hex              $0250-$0256: the load drive, the passwords' places
;   PW1 [text]          the first password, by its place and length
;   PW2 [text]          the second
;   DU dd uu            the current drive (25) and user (32)
;   RC xxxx             the return code (108) as it started
;
; A tail whose first characters are "@F" ends it with the return code
; $FF00, "@C" with $FFFE (as a ^C would).

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  lda     #<said
        ldx     #>said
        jsr     PUTS
        lda     #<(tag + 4)
        ldx     #>(tag + 4)
        jsr     PUTS
        jsr     CRLF
        lda     #<stail
        ldx     #>stail
        jsr     PUTS
        lda     BP_DMA
        jsr     HEX2
        lda     #' '
        jsr     PUTC
        lda     #'['
        jsr     PUTC
        ldx     #0
@tail:  cpx     BP_DMA
        beq     @tailed
        lda     BP_DMA + 1,x
        phx
        jsr     PUTC
        plx
        inx
        bra     @tail
@tailed: lda    #']'
        jsr     PUTC
        jsr     CRLF
        lda     #<sf1
        ldx     #>sf1
        jsr     PUTS
        lda     #<BP_FCB
        sta     PTR
        lda     #>BP_FCB
        sta     PTR + 1
        ldx     #16
        jsr     HEXN
        jsr     CRLF
        lda     #<sf2
        ldx     #>sf2
        jsr     PUTS
        lda     #<BP_FCB2
        sta     PTR
        lda     #>BP_FCB2
        sta     PTR + 1
        ldx     #20
        jsr     HEXN
        jsr     CRLF
        lda     #<sbp
        ldx     #>sbp
        jsr     PUTS
        lda     #<$0250
        sta     PTR
        lda     #>$0250
        sta     PTR + 1
        ldx     #7
        jsr     HEXN
        jsr     CRLF
        lda     #<spw1
        ldx     #>spw1
        jsr     PUTS
        ldx     #0
        jsr     pw
        lda     #<spw2
        ldx     #>spw2
        jsr     PUTS
        ldx     #3
        jsr     pw
        lda     #<sdu
        ldx     #>sdu
        jsr     PUTS
        ldy     #DRV_GET
        jsr     BDOS
        jsr     HEX2
        jsr     SPACE
        lda     #$FF
        ldy     #F_USERNUM
        jsr     BDOS
        jsr     HEX2
        jsr     CRLF
        lda     #<src
        ldx     #>src
        jsr     PUTS
        lda     #$FF
        tax
        ldy     #P_CODE
        jsr     BDOS
        jsr     HEX4
        jsr     CRLF
        lda     BP_DMA                  ; "@F" or "@C" first: the code to end with
        beq     @end
        ldx     #0
@skip:  lda     BP_DMA + 1,x
        cmp     #' '
        bne     @mark
        inx
        cpx     BP_DMA
        bne     @skip
        bra     @end
@mark:  cmp     #'@'
        bne     @end
        lda     BP_DMA + 2,x
        cmp     #'F'
        beq     @fail
        cmp     #'C'
        bne     @end
        lda     #$FE
        bra     @code
@fail:  lda     #0
@code:  ldx     #$FF
        ldy     #P_CODE
        jsr     BDOS
@end:   rts

; The password whose place is at $0251 + X: "[text]".
pw:     lda     $0251,x
        sta     PTR
        lda     $0252,x
        sta     PTR + 1
        lda     $0253,x
        sta     HEXC
        lda     #'['
        jsr     PUTC
        ldy     #0
@ch:    cpy     HEXC
        beq     @done
        lda     (PTR),y
        phy
        jsr     PUTC
        ply
        iny
        bra     @ch
@done:  lda     #']'
        jsr     PUTC
        jmp     CRLF

        .include "print.inc"

        .rodata
said:   .byte   "ARGS $"
tag:    .byte   "TAG:----"
        .byte   "$"
stail:  .byte   "TAIL $"
sf1:    .byte   "F1 $"
sf2:    .byte   "F2 $"
sbp:    .byte   "BP $"
spw1:   .byte   "PW1 $"
spw2:   .byte   "PW2 $"
sdu:    .byte   "DU $"
src:    .byte   "RC $"

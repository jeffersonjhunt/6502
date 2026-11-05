; The test RSXs (test_oodos_rsx.cpp, hw/oodos.md "The RSXs, phase 5"): each
; trsxN.s sets LETTER, REMOVE, NONBANK and ZPN, then includes this. Each:
;
;   - function 2 with '#': writes its LETTER first (by its offset 9), then
;     passes the '#' on, so a program's '#' shows the chain's order;
;   - function 0: counted (FN0), then passed on;
;   - function 60, the RSX parameter block's function byte:
;       $10            answered by the first RSX in the chain: A = its LETTER
;       LETTER         A = FN0, X = its zero page's first byte (or 0)
;       LETTER + $20   its own remove flag set to $FF; A = 0
;     anything else passed on;
;   - with ZPN bytes of zero page, each call that passes through counts in
;     its first, so the loader's slice and relocation show.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        RSX_PREFIX NAME, entry, REMOVE, NONBANK

.if ZPN > 0
        .zeropage
zcount: .res    ZPN
.endif

        .code
entry:  sta     sa
        stx     sx
.if ZPN > 0
        inc     zcount
.endif
        cpy     #C_WRITE
        beq     @write
        cpy     #P_TERMCPM
        beq     @fn0
        cpy     #P_RSX
        beq     @rsx
@pass:  lda     sa
        ldx     sx
        jmp     prefix + RSX_NEXT
@write: cmp     #'#'
        bne     @pass
        lda     #LETTER
        ldy     #C_WRITE
        jsr     prefix + RSX_NEXT
        ldy     #C_WRITE
        bra     @pass
@fn0:   inc     fn0
        ldy     #P_TERMCPM
        bra     @pass
@rsx:   lda     sa                      ; the block's function byte
        sta     @pb + 1
        lda     sx
        sta     @pb + 2
@pb:    lda     $FFFF
        cmp     #$10
        beq     @first
        cmp     #LETTER
        beq     @mine
        cmp     #LETTER + $20
        beq     @remove
        ldy     #P_RSX
        bra     @pass
@first: lda     #LETTER
        ldx     #0
        rts
@mine:  lda     fn0
.if ZPN > 0
        ldx     zcount
.else
        ldx     #0
.endif
        rts
@remove: lda    #$FF
        sta     prefix + RSX_REMOVE
        lda     #0
        tax
        rts

        .segment "RSXHDR"
prefix  = * - RSX_PREFIX_LEN            ; (the prefix is the segment's first 27 bytes)

        .data                           ; in the file: 0 as loaded (BSS is not cleared)
sa:     .res    1
sx:     .res    1
fn0:    .res    1

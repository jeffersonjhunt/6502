; OOUI's bars, UT_VBAR and UT_HBAR (hw/ooui.md, "U2"; proposed): a level,
; UW_VALUE from 0 to UW_A, drawn with the sub-cell glyphs: a vertical bar
; n cells long shows 16n levels from the bottom ($01-$0F, then $DB), a
; horizontal one 8n from the left (UG_HBAR1 ..., then $DB). Filled cells in
; TC_BAR, the rest in TC_BAREMPTY. A bar takes no input. OO-Amp: its VU
; and level meters.

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .import SCALE, sv, su, sa, sr
        .export UIW_VBAR, UIW_HBAR

        .segment "BRODATA"
UIW_VBAR:
        .byte   UT_VBAR, WF_RANGE
        .word   DRAW, 0, 0, 0, 0, 0
UIW_HBAR:
        .byte   UT_HBAR, WF_RANGE
        .word   DRAW, 0, 0, 0, 0, 0
; The horizontal partial cells, 1-7 eighths.
HPART:  .byte   $12, $13, $14, $15, $16, $17, $1C

        .bss
vert:   .res    1                      ; 1: vertical
len:    .res    1                      ; its length in cells
full:   .res    1                      ; the cells filled whole
part:   .res    1                      ;   and the next one's part (0 none)
idx:    .res    1                      ; the cell being drawn, from the bar's start
ch:     .res    1

        .segment "BCODE"

; The level in units (16 or 8 a cell): sr = value x len x units / top.
DRAW:   ldy     #UW_TYPE
        lda     (UIR),y
        ldx     #0
        cmp     #UT_VBAR
        bne     @h
        inx
@h:     stx     vert
        lda     UI_RECT + 2             ; the length: the height or the width
        cpx     #0
        beq     @w
        lda     UI_RECT + 3
@w:     sta     len
        stz     su + 1                  ; su = len x 16 (vertical) or x 8
        asl     a
        rol     su + 1
        asl     a
        rol     su + 1
        asl     a
        rol     su + 1
        cpx     #0
        beq     @u
        asl     a
        rol     su + 1
@u:     sta     su
        ldy     #UW_VALUE
        lda     (UIR),y
        sta     sv
        iny
        lda     (UIR),y
        sta     sv + 1
        ldy     #UW_A
        lda     (UIR),y
        sta     sa
        iny
        lda     (UIR),y
        sta     sa + 1
        jsr     SCALE                   ; sr: the units filled (at most su: the value is held to the top)
        ldy     #3                      ; full = sr / units, part = sr mod units
        lda     vert
        beq     @k
        iny
@k:     lda     sr + 1
        sta     t0
        lda     sr
@sh:    lsr     t0
        ror     a
        dey
        bne     @sh
        sta     full
        lda     sr
        ldx     vert
        beq     @h8
        and     #$0F
        bra     @p
@h8:    and     #$07
@p:     sta     part
        stz     idx
@cell:  lda     idx                     ; each cell from the start: full, part, or empty
        cmp     len
        bcs     @r
        cmp     full
        bcc     @fill
        bne     @empty
        lda     part
        beq     @empty
        ldx     vert
        bne     @vp
        tax
        lda     HPART - 1,x
        bra     @put
@vp:    bra     @put                    ; the vertical partial cells are $01-$0F: the part itself
@fill:  lda     #$DB
@put:   sta     ch
        ldy     #TC_BAR
        bra     @draw
@empty: lda     #' '
        sta     ch
        ldy     #TC_BAREMPTY
@draw:  jsr     TATTR
        jsr     LINE
        inc     idx
        bra     @cell
@r:     rts

; Cell idx of the bar drawn across it with ch: a row of the width from the
; bottom (vertical), or a column of the height from the left.
LINE:   lda     vert
        beq     @col
        lda     UI_RECT + 1             ; the row: the bottom less idx
        clc
        adc     UI_RECT + 3
        sec
        sbc     #1
        sec
        sbc     idx
        tax
        lda     UI_RECT
        jsr     GOTO
        lda     ch
        ldx     UI_RECT + 2
        jmp     REPT
@col:   lda     UI_RECT + 1
        sta     t1
        lda     UI_RECT + 3
        sta     t2
@row:   lda     UI_RECT
        clc
        adc     idx
        ldx     t1
        jsr     GOTO
        lda     ch
        jsr     EMIT
        inc     t1
        dec     t2
        bne     @row
        rts

; OOUI's sliders, UT_VSLIDER and UT_HSLIDER (hw/ooui.md, "U2"; proposed):
; UW_VALUE from 0 to UW_A (the top: a vertical slider's top end, a
; horizontal one's right), UW_B's low byte a key's step (0 is 1), its high
; byte the thumb's sprite with UF_SPRITE. The track in TC_SCROLL, the thumb
; a cell in TC_THUMB, or with UF_SPRITE an 8 x 8 sprite placed to the
; pointer unit (OOUI's thumb image, in the overlay's group), which is put
; out of sight when the slider is hidden. It takes the focus: Up and Right
; step it up, Down and Left down, PgUp and PgDn an eighth, Home the top,
; End 0; the wheel steps it; a click or a drag puts it where the pointer
; is (UE_CHANGE with UV_LIVE as it moves, then without at the release).
; OO-Amp: seek, volume, balance, the EQ's 11 bands.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .import SCALE, sv, su, sa, sr
        .export UIW_VSLIDER, UIW_HSLIDER

        .rodata
UIW_VSLIDER:
        .byte   UT_VSLIDER, WF_FOCUS | WF_RANGE | WF_REPEAT
        .word   DRAW, KEY, DOWN, MOVE, UP, WHEEL
UIW_HSLIDER:
        .byte   UT_HSLIDER, WF_FOCUS | WF_RANGE | WF_REPEAT
        .word   DRAW, KEY, DOWN, MOVE, UP, WHEEL

        .bss
vert:   .res    1
len:    .res    1                      ; in cells
val:    .res    2
top:    .res    2
pos:    .res    2                      ; the thumb's place in units from the track's start (0 the low end)
idx:    .res    1
drag:   .res    1
tmp:    .res    1

        .code

; vert, len, val, top; pos = val x (len x units - 1) / top.
MEASURE:
        ldy     #UW_TYPE
        lda     (UIR),y
        ldx     #0
        cmp     #UT_VSLIDER
        bne     @h
        inx
@h:     stx     vert
        lda     UI_RECT + 2
        cpx     #0
        beq     @w
        lda     UI_RECT + 3
@w:     sta     len
        ldy     #UW_VALUE
        lda     (UIR),y
        sta     val
        sta     sv
        iny
        lda     (UIR),y
        sta     val + 1
        sta     sv + 1
        ldy     #UW_A
        lda     (UIR),y
        sta     top
        sta     sa
        iny
        lda     (UIR),y
        sta     top + 1
        sta     sa + 1
        jsr     SPAN
        jsr     SCALE
        lda     sr
        sta     pos
        lda     sr + 1
        sta     pos + 1
        rts

; su = the track's units less one: len x 8 (vertical) or x 4, - 1.
SPAN:   lda     len
        stz     su + 1
        asl     a
        rol     su + 1
        asl     a
        rol     su + 1
        ldx     vert
        beq     @h
        asl     a
        rol     su + 1
@h:     sec
        sbc     #1
        sta     su
        lda     su + 1
        sbc     #0
        sta     su + 1
        rts

DRAW:   jsr     MEASURE
        lda     pos                     ; the thumb's cell, from the low end: pos / units
        ldx     pos + 1
        stx     t0
        ldy     #2
        lda     vert
        beq     @k
        iny
@k:     lda     pos
@sh:    lsr     t0
        ror     a
        dey
        bne     @sh
        sta     tmp
        stz     idx
        ldy     #UW_FLAGS               ; a sprite's thumb: no thumb cell
        lda     (UIR),y
        and     #UF_SPRITE
        beq     @cell
        lda     #$FF
        sta     tmp
@cell:  lda     idx
        cmp     len
        bcs     @spr
        lda     #$C4                    ; the track: a line
        ldx     vert
        beq     @t
        lda     #$B3
@t:     ldy     #TC_SCROLL
        ldx     idx
        cpx     tmp
        bne     @plain
        lda     #$DB                    ; the thumb's cell
        ldy     #TC_THUMB
        ldx     dh
        cpx     ufocus
        bne     @plain
        ldy     #TC_BUTTONFOCUS
@plain: pha
        jsr     TATTR
        jsr     CELLAT
        pla
        jsr     EMIT
        inc     idx
        bra     @cell
@spr:   ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_SPRITE
        beq     @r
        jmp     SPRITE
@r:     rts

; GOTO cell idx from the low end: the bottom up (vertical), the left on.
CELLAT: lda     vert
        beq     @h
        lda     UI_RECT + 1
        clc
        adc     UI_RECT + 3
        sec
        sbc     #1
        sec
        sbc     idx
        tax
        lda     UI_RECT
        jmp     GOTO
@h:     lda     UI_RECT
        clc
        adc     idx
        ldx     UI_RECT + 1
        jmp     GOTO

; The thumb's sprite (UW_B's high byte): OOUI's thumb image (8 x 8, the
; card page after the pointer's), placed on pos, in front, shown.
SPRITE: jsr     THUMBIMG
        ldy     #UW_B + 1
        lda     (UIR),y
        sta     tmp
        lda     upage
        clc
        adc     #UI_OFS_SPR + 1
        sta     GX_V
        lda     upage + 1
        adc     #0
        sta     GX_V + 1
        lda     tmp
        jsr     GX_SPRIMG
        ldy     #TH_OVERGROUP
        lda     (UIT),y
        and     #$0F
        tay                             ; 8 x 8, the overlay's group
        lda     tmp
        ldx     #3 << 2
        jsr     GX_SPRATTR
        lda     vert                    ; X and Y: the cell's, in sprite units, and pos along
        beq     @h
        lda     UI_RECT                 ; x = column x 4 - 2
        jsr     X4
        sec
        sbc     #2
        sta     GX_X
        bcs     @x
        dec     GX_X + 1
@x:     lda     UI_RECT + 1             ; y = (row + len) x 8 - 1 - pos - 4
        clc
        adc     len
        jsr     X8
        sec
        sbc     #5
        sta     GX_Y
        bcs     @y
        dec     GX_Y + 1
@y:     lda     GX_Y
        sec
        sbc     pos
        sta     GX_Y
        lda     GX_Y + 1
        sbc     pos + 1
        sta     GX_Y + 1
        bra     @put
@h:     lda     UI_RECT                 ; x = column x 4 + pos - 4
        jsr     X4
        clc
        adc     pos
        sta     GX_X
        lda     GX_X + 1
        adc     pos + 1
        sta     GX_X + 1
        lda     GX_X
        sec
        sbc     #4
        sta     GX_X
        bcs     @hx
        dec     GX_X + 1
@hx:    lda     UI_RECT + 1             ; y = row x 8
        jsr     X8
        sta     GX_Y
@put:   lda     tmp
        jsr     GX_SPRPOS
        jsr     GX_SPRSHOW
        lda     #<POST                  ; and put out of sight when hidden (PAINT's upost)
        sta     upost
        lda     #>POST
        sta     upost + 1
        rts

; GX_X (or GX_Y) = A x 4 (x 8), its high byte set: A low.
X4:     stz     GX_X + 1
        asl     a
        rol     GX_X + 1
        asl     a
        rol     GX_X + 1
        rts
X8:     stz     GX_Y + 1
        asl     a
        rol     GX_Y + 1
        asl     a
        rol     GX_Y + 1
        asl     a
        rol     GX_Y + 1
        rts

; The thumb's image, 8 x 8 at 4 bits (32 bytes) after the pointer's, in
; the pointer's colours: a ring, filled.
THUMBIMG:
        lda     mapB
        sta     UIQ
        lda     mapB + 1
        clc
        adc     #UI_OFS_SPR + 1
        sta     UIQ + 1
        ldy     #TH_POINTER
        lda     (UIT),y
        sta     tmp
        ldy     #31
@b:     lda     THUMB,y                 ; 2 = the outline's colour, 1 the fill's (both nibbles)
        ldx     #0
        jsr     NIB
        sta     t0
        lda     THUMB,y
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jsr     NIB
        asl     a
        asl     a
        asl     a
        asl     a
        ora     t0
        sta     (UIQ),y
        dey
        bpl     @b
        rts
; A's low nibble (0, 1 the fill, 2 the outline): its colour.
NIB:    and     #$0F
        beq     @r
        cmp     #2
        lda     tmp
        bcs     @o
        and     #$0F
        rts
@o:     lsr     a
        lsr     a
        lsr     a
        lsr     a
@r:     rts

        .rodata
THUMB:  .byte   $00, $22, $22, $00
        .byte   $02, $11, $11, $20
        .byte   $21, $11, $11, $12
        .byte   $21, $11, $11, $12
        .byte   $21, $11, $11, $12
        .byte   $21, $11, $11, $12
        .byte   $02, $11, $11, $20
        .byte   $00, $22, $22, $00
        .code

; After a paint: every UF_SPRITE slider that doesn't show has its sprite out
; of sight (depth 0).
POST:   stz     tmp
        lda     #$FF
        sta     idx
@l:     inc     idx
        lda     idx
        cmp     utot
        bcs     @done
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_VSLIDER
        beq     @s
        cmp     #UT_HSLIDER
        bne     @l
@s:     ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_SPRITE
        beq     @l
        lda     idx
        jsr     RECT
        bcc     @l
        lda     idx
        jsr     REC
        ldy     #UW_B + 1
        lda     (UIR),y
        ldx     #0
        ldy     #0
        jsr     GX_SPRATTR
        inc     tmp
        bra     @l
@done:  lda     tmp
        beq     @r
        jmp     GX_SPRSHOW
@r:     rts

; A key while it has the focus.
KEY:    jsr     MEASURE
        lda     ku
        cmp     #K_UP
        beq     @up
        cmp     #K_RIGHT
        beq     @up
        cmp     #K_DOWN
        beq     @dn
        cmp     #K_LEFT
        beq     @dn
        cmp     #K_PGUP
        beq     @pu
        cmp     #K_PGDN
        beq     @pd
        cmp     #K_HOME
        beq     @top
        cmp     #K_END
        beq     @zero
        sec
        rts
@up:    jsr     STEP
        jmp     ADD
@dn:    jsr     STEP
        jmp     SUB
@pu:    jsr     EIGHTH
        jmp     ADD
@pd:    jsr     EIGHTH
        jmp     SUB
@top:   lda     top
        sta     t0
        lda     top + 1
        sta     t1
        jmp     SETV
@zero:  stz     t0
        stz     t1
        jmp     SETV

; t2/t3 = the step (UW_B's low byte, 0 is 1); EIGHTH: top / 8, at least 1.
STEP:   ldy     #UW_B
        lda     (UIR),y
        bne     @s
        lda     #1
@s:     sta     t2
        stz     t3
        rts
EIGHTH: lda     top + 1
        sta     t3
        lda     top
        ldx     #3
@d:     lsr     t3
        ror     a
        dex
        bne     @d
        sta     t2
        ora     t3
        bne     @r
        inc     t2
@r:     rts

; val + t2/t3, held at the top; val - t2/t3, held at 0.
ADD:    clc
        lda     val
        adc     t2
        sta     t0
        lda     val + 1
        adc     t3
        sta     t1
        bcs     @top
        lda     top
        cmp     t0
        lda     top + 1
        sbc     t1
        bcs     SETV
@top:   lda     top
        sta     t0
        lda     top + 1
        sta     t1
        bra     SETV
SUB:    sec
        lda     val
        sbc     t2
        sta     t0
        lda     val + 1
        sbc     t3
        sta     t1
        bcs     SETV
        stz     t0
        stz     t1
; The value t0/t1: UE_CHANGE if it changed.
SETV:   lda     t0
        cmp     val
        bne     @set
        lda     t1
        cmp     val + 1
        beq     @same
@set:   ldy     #UW_VALUE
        lda     t0
        sta     (UIR),y
        sta     evv
        iny
        lda     t1
        sta     (UIR),y
        sta     evv + 1
        lda     dh
        sta     evh
        jsr     DIRTY
        lda     #UE_CHANGE
        clc
        rts
@same:  lda     #UE_NONE
        clc
        rts

; The pointer: the value where it is (top x units / span), live.
DOWN:   lda     #1
        sta     drag
MOVE:   lda     drag
        bne     @go
        lda     #UE_NONE
        clc
        rts
@go:    jsr     MEASURE
        lda     vert
        beq     @h
        jsr     SPAN                    ; vertical: span - mpy, held to 0-span
        lda     su
        sec
        sbc     mpy
        sta     sv
        lda     su + 1
        sbc     mpy + 1
        sta     sv + 1
        bcs     @v
        stz     sv
        stz     sv + 1
        bra     @v
@h:     lda     mpx                     ; horizontal: mpx
        sta     sv
        lda     mpx + 1
        sta     sv + 1
        bpl     @v
        stz     sv
        stz     sv + 1
@v:     jsr     SPAN                    ; value = sv x top / span
        lda     su
        sta     sa
        lda     su + 1
        sta     sa + 1
        lda     top
        sta     su
        lda     top + 1
        sta     su + 1
        jsr     SCALE
        lda     sr
        sta     t0
        lda     sr + 1
        sta     t1
        lda     top                     ; held at the top
        cmp     t0
        lda     top + 1
        sbc     t1
        bcs     @set
        lda     top
        sta     t0
        lda     top + 1
        sta     t1
@set:   jsr     SETV
        cmp     #UE_NONE
        beq     @r
        lda     #UV_LIVE
        tsb     evf
        lda     #UE_CHANGE
@r:     clc
        rts
@none:  lda     #UE_NONE
        clc
        rts

UP:     lda     drag
        beq     @none
        stz     drag
        jsr     MEASURE
        lda     val
        sta     evv
        lda     val + 1
        sta     evv + 1
        lda     dh
        sta     evh
        lda     #UE_CHANGE
        clc
        rts
@none:  lda     #UE_NONE
        clc
        rts

; The wheel: steps x the step, up for positive.
WHEEL:  jsr     MEASURE
        jsr     STEP
        lda     wst
        bmi     @dn
        beq     @none
        sta     tmp
@u:     jsr     ADD
        lda     t0
        sta     val
        lda     t1
        sta     val + 1
        dec     tmp
        bne     @u
        bra     @ev
@dn:    eor     #$FF
        inc     a
        sta     tmp
@d:     jsr     SUB
        lda     t0
        sta     val
        lda     t1
        sta     val + 1
        dec     tmp
        bne     @d
@ev:    lda     dh
        sta     evh
        lda     val
        sta     evv
        lda     val + 1
        sta     evv + 1
        lda     #UE_CHANGE
        clc
        rts
@none:  sec
        rts

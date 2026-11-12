; OOUI's scroll bars, UT_VSCROLL and UT_HSCROLL (hw/ooui.md, "U2";
; proposed): UW_VALUE the position, 0 to UW_A (the last), UW_B the page
; (the thumb's share of the track). An arrow at each end, the track
; (TC_SCROLL) and the thumb (TC_THUMB) between. A click on an arrow moves
; one, on the track either side of the thumb a page; the thumb is dragged
; (UE_CHANGE with UV_LIVE while it moves, then without at the release); the
; wheel over it moves it, its steps upward taking the position down; the
; accelerator table's UA_UP and UA_DOWN move one. It takes no focus. OO-Amp:
; the playlist's scroll bar.

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .import SCALE, sv, su, sa, sr
        .export UIW_VSCROLL, UIW_HSCROLL

        .segment "BRODATA"
UIW_VSCROLL:
        .byte   UT_VSCROLL, WF_RANGE
        .word   DRAW, KEY, DOWN, MOVE, UP, WHEEL
UIW_HSCROLL:
        .byte   UT_HSCROLL, WF_RANGE
        .word   DRAW, KEY, DOWN, MOVE, UP, WHEEL

        .bss
vert:   .res    1
len:    .res    1                      ; the track's cells (the length less the arrows)
tsz:    .res    1                      ; the thumb's cells
tpos:   .res    1                      ;   and its first, in the track
val:    .res    2
last:   .res    2
page:   .res    2
idx:    .res    1
grab:   .res    2                      ; a drag: the pointer's units into the thumb
drag:   .res    1                      ; 1: the thumb is held

        .segment "BCODE"

; The bar's measures: vert, len, val, last, page, tsz, tpos.
MEASURE:
        ldy     #UW_TYPE
        lda     (UIR),y
        ldx     #0
        cmp     #UT_VSCROLL
        bne     @h
        inx
@h:     stx     vert
        lda     UI_RECT + 2
        cpx     #0
        beq     @w
        lda     UI_RECT + 3
@w:     sec
        sbc     #2
        bcs     @l
        lda     #0
@l:     sta     len
        ldy     #UW_VALUE
        ldx     #0
@c:     lda     (UIR),y
        sta     val,x
        iny
        inx
        cpx     #6
        bne     @c
.assert last = val + 2 && page = val + 4 && UW_A = UW_VALUE + 2 && UW_B = UW_VALUE + 4, error, "val, last, page"
        lda     page                    ; the thumb: len x page / (last + page), at least 1
        sta     sv
        lda     page + 1
        sta     sv + 1
        lda     len
        sta     su
        stz     su + 1
        clc
        lda     last
        adc     page
        sta     sa
        lda     last + 1
        adc     page + 1
        sta     sa + 1
        jsr     SCALE
        lda     sr + 1
        bne     @all
        lda     sr
        bne     @one
        lda     #1
@one:   cmp     len
        bcc     @t
@all:   lda     len
@t:     sta     tsz
        lda     val                     ; its place: (len - tsz) x val / last
        sta     sv
        lda     val + 1
        sta     sv + 1
        lda     len
        sec
        sbc     tsz
        sta     su
        stz     su + 1
        lda     last
        sta     sa
        lda     last + 1
        sta     sa + 1
        jsr     SCALE
        lda     sr
        sta     tpos
        rts

DRAW:   jsr     MEASURE
        stz     idx
@cell:  lda     idx                     ; cell idx of the whole: an arrow, the track or the thumb
        beq     @first
        dec     a
        cmp     len
        beq     @lastc
        bcs     @r
        sec
        sbc     tpos
        bcc     @track
        cmp     tsz
        bcs     @track
        ldy     #TC_THUMB
        lda     #$DB
        bra     @put
@track: ldy     #TC_SCROLL
        lda     #$B0
        bra     @put
@first: ldx     #0
        bra     @arrow
@lastc: ldx     #1
@arrow: lda     vert
        asl     a
        sta     t0
        txa
        ora     t0
        tax
        lda     ARROWS,x
        ldy     #TC_SCROLL
@put:   pha
        jsr     TATTR
        lda     vert
        beq     @hz
        lda     UI_RECT + 1
        clc
        adc     idx
        tax
        lda     UI_RECT
        bra     @go
@hz:    lda     UI_RECT
        clc
        adc     idx
        ldx     UI_RECT + 1
@go:    jsr     GOTO
        pla
        jsr     EMIT
        inc     idx
        bra     @cell
@r:     rts

        .segment "BRODATA"
ARROWS: .byte   $11, $10, $1E, $1F      ; horizontal's left and right, vertical's up and down
        .segment "BCODE"

KEY:    lda     ku
        ldx     #$FF
        cmp     #K_UP
        beq     STEP1
        cmp     #K_LEFT
        beq     STEP1
        ldx     #1
        cmp     #K_DOWN
        beq     STEP1
        cmp     #K_RIGHT
        beq     STEP1
        sec
        rts
STEP1:  phx
        jsr     MEASURE
        pla
        ldx     #0
        cmp     #$80
        bcc     @p
        dex
@p:     sta     t2
        stx     t3
        jmp     ADDV

; The pointer's units along the bar: A/X (mpy vertical, mpx horizontal),
; less one cell for the first arrow; and the cell (along) in idx.
ALONG:  lda     vert
        beq     @h
        lda     mpy + 1
        tax
        lda     mpy
        sec
        sbc     #8
        bcs     @v
        dex
@v:     pha
        lda     mpy
        lsr     a
        lsr     a
        lsr     a
        sta     idx
        pla
        rts
@h:     lda     mpx + 1
        tax
        lda     mpx
        sec
        sbc     #4
        bcs     @x
        dex
@x:     pha
        lda     mpx + 1                 ; mpx / 4: the cell (mpx under 512)
        lsr     a
        lda     mpx
        ror     a
        lsr     a
        sta     idx
        pla
        rts

; A cell's units: 8 down, 4 across.
UNIT:   lda     vert
        beq     @h
        lda     #8
        rts
@h:     lda     #4
        rts

DOWN:   jsr     MEASURE
        stz     drag
        jsr     ALONG
        lda     idx
        bne     @nf
        ldx     #$FF                    ; the first arrow: one back
        bra     STEP1
@nf:    dec     a
        cmp     len
        bcc     @track
        ldx     #1                      ; the last arrow: one on
        bra     STEP1
@track: sec
        sbc     tpos
        bcc     @pgup
        cmp     tsz
        bcs     @pgdn
        lda     #1                      ; the thumb: held, the pointer's units into it kept
        sta     drag
        jsr     ALONG
        sta     grab
        stx     grab + 1
        lda     tpos
        jsr     UNITS
        lda     grab
        sec
        sbc     t0
        sta     grab
        lda     grab + 1
        sbc     t1
        sta     grab + 1
        lda     #UE_NONE
        clc
        rts
@pgup:  lda     #0
        sec
        sbc     page
        sta     t2
        lda     #0
        sbc     page + 1
        sta     t3
        bra     ADDV
@pgdn:  lda     page
        sta     t2
        lda     page + 1
        sta     t3
; val + t2/t3 (signed), held to 0-last: UE_CHANGE if it moved.
ADDV:   clc
        lda     val
        adc     t2
        sta     t0
        lda     val + 1
        adc     t3
        sta     t1
        lda     t3                      ; below 0 (a negative step, and a borrow): 0
        bpl     @up
        bcs     HOLD
        stz     t0
        stz     t1
        bra     HOLD
@up:    bcc     HOLD                    ; past 65,535: the last
        lda     #$FF
        sta     t0
        sta     t1
HOLD:   lda     last                    ; at most the last
        cmp     t0
        lda     last + 1
        sbc     t1
        bcs     SETV
        lda     last
        sta     t0
        lda     last + 1
        sta     t1
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

; t0/t1 = A cells in units.
UNITS:  sta     t0
        stz     t1
        jsr     UNIT
        tax
@m:     cpx     #1
        beq     @r
        asl     t0
        rol     t1
        txa
        lsr     a
        tax
        bra     @m
@r:     rts

; The thumb dragged: its first cell where the pointer, less the grab, is,
; the value last x that / (len - tsz) in units.
MOVE:   lda     drag
        beq     @none
        jsr     MEASURE
        jsr     ALONG
        sec
        sbc     grab
        sta     sv
        txa
        sbc     grab + 1
        sta     sv + 1
        bpl     @pos
        stz     sv
        stz     sv + 1
@pos:   lda     last
        sta     su
        lda     last + 1
        sta     su + 1
        lda     len
        sec
        sbc     tsz
        jsr     UNITS
        lda     t0
        sta     sa
        lda     t1
        sta     sa + 1
        lda     sa                      ; sv held to the track (X2)
        cmp     sv
        lda     sa + 1
        sbc     sv + 1
        bcs     @in
        lda     sa
        sta     sv
        lda     sa + 1
        sta     sv + 1
@in:    jsr     SCALE
        lda     sr
        sta     t0
        lda     sr + 1
        sta     t1
        jsr     HOLD
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

; Let go: after a drag, its last UE_CHANGE, without UV_LIVE.
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

; The wheel: its steps upward (positive) take the position down.
WHEEL:  jsr     MEASURE
        lda     #0
        sec
        sbc     wst
        sta     t2
        ldx     #0
        cmp     #$80
        bcc     @s
        dex
@s:     stx     t3
        jmp     ADDV

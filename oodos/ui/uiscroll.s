; OOUI's scroll bars, UT_VSCROLL and UT_HSCROLL (hw/ooui.md, "U2";
; proposed): UW_VALUE the position, 0 to UW_A (the last), UW_B the page
; (the thumb's share of the track). An arrow at each end, the track
; (TC_SCROLL) and the thumb (TC_THUMB) between. A click on an arrow moves
; one, on the track either side of the thumb a page; the thumb is dragged
; (UE_CHANGE with UV_LIVE while it moves, then without at the release); the
; wheel over it moves it, its steps upward taking the position down; the
; accelerator table's UA_UP and UA_DOWN move one. It takes no focus. OO-Amp:
; the playlist's scroll bar.
;
; U3b: with UF_SPRITE its thumb is a sprite (UW_TEXT's high byte), placed
; to the sprite unit after each paint as a slider's is (uislider.s's POST),
; and the bar works in units, not cells: the thumb is the sprite's length
; (OOUI's 8 x 8, or with UF_OWNTHUMB the program's, its size byte UW_TEXT's
; low byte), at (track - thumb) x value / last along the track; a press
; on it drags it, before or after it pages. With UF_MARGIN, UW_B's high
; byte is the margin (uipoll.s's MARGIN) and the page its low byte.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .import SCALE, sv, su, sa, sr
        .import TSIZE, SPRTAIL, POST, hw, hh, tc, tr, tc1, tr1
        .export UIW_VSCROLL, UIW_HSCROLL, SCMEAS, SCPLACE

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

        .segment "BBSS"                 ; block B's own (U3b: a sprite's thumb)
smode:  .res    1                      ; 1: UF_SPRITE, in units
sth:    .res    1                      ;   the thumb's length, in units
room:   .res    2                      ;   the track's units less the thumb's
tu:     .res    2                      ;   the thumb's first unit along the track

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
        ldy     #UW_FLAGS               ; (U3b) with UF_MARGIN, UW_B's high byte is the margin:
        lda     (UIR),y                 ;   the page its low byte
        and     #UF_MARGIN
        beq     @pg
        stz     page + 1
@pg:
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
        stz     smode                   ; a sprite's thumb (U3b): in units
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_SPRITE
        beq     @r
        inc     smode
        jsr     TSIZE                   ; hw, hh
        lda     hh
        ldx     vert
        bne     @s
        lda     hw
@s:     asl     a
        sta     sth
        lda     len                     ; room = len x units - the thumb, at least 0
        jsr     UNITS
        sec
        lda     t0
        sbc     sth
        sta     room
        lda     t1
        sbc     #0
        sta     room + 1
        bcs     @rm
        stz     room
        stz     room + 1
@rm:    stz     tu                      ; tu = room x val / last (0 with no last)
        stz     tu + 1
        lda     last
        ora     last + 1
        beq     @r
        lda     val
        sta     sv
        lda     val + 1
        sta     sv + 1
        lda     room
        sta     su
        lda     room + 1
        sta     su + 1
        lda     last
        sta     sa
        lda     last + 1
        sta     sa + 1
        jsr     SCALE
        lda     sr
        sta     tu
        lda     sr + 1
        sta     tu + 1
@r:     rts

; For uislider.s's POST (U3b): the bar's measures (UIR the bar, UI_RECT its
; place).
SCMEAS = MEASURE

; The bar's sprite thumb (UIR, MEASURE'd, UI_RECT its place) to its place:
; GX_X and GX_Y, and its first and last cells along the track (tc, tr and
; tc1, tr1) for SHOWS, then uislider.s's SPRTAIL. Vertical: x = column x 4
; + 2 - hw, y = (row + 1) x 8 + tu; horizontal: x = (column + 1) x 4 + tu,
; y = row x 8 + 4 - hh.
SCPLACE:
        lda     vert
        beq     @h
        lda     UI_RECT
        sta     tc
        sta     tc1
        jsr     X4V
        clc
        adc     #2
        sec
        sbc     hw
        sta     GX_X
        bcs     @x
        dec     GX_X + 1
@x:     lda     UI_RECT + 1
        inc     a
        jsr     X8V
        clc
        adc     tu
        sta     GX_Y
        lda     GX_Y + 1
        adc     tu + 1
        sta     GX_Y + 1
        jsr     SENDS                   ; its rows: row + 1 + its first and last cells
        lda     UI_RECT + 1
        sec
        adc     t2
        sta     tr
        lda     UI_RECT + 1
        sec
        adc     t3
        sta     tr1
        jmp     SPRTAIL
@h:     lda     UI_RECT
        inc     a
        jsr     X4V
        clc
        adc     tu
        sta     GX_X
        lda     GX_X + 1
        adc     tu + 1
        sta     GX_X + 1
        lda     UI_RECT + 1
        sta     tr
        sta     tr1
        jsr     X8V
        clc
        adc     #4
        sec
        sbc     hh
        sta     GX_Y
        bcs     @y
        dec     GX_Y + 1
@y:     jsr     SENDS                   ; its columns: column + 1 + its first and last cells
        lda     UI_RECT
        sec
        adc     t2
        sta     tc
        lda     UI_RECT
        sec
        adc     t3
        sta     tc1
        jmp     SPRTAIL
; The thumb's first and last cells in the track (U3b review, G2): t2 = tu /
; u, t3 = (tu + sth - 1) / u, held to the track's len - 1 (and to t2 at
; least: a thumb longer than the track); u is 8 units a cell (vertical) or
; 4.
SENDS:  lda     tu
        sta     t0
        lda     tu + 1
        sta     t1
        jsr     @cell
        sta     t2
        lda     tu
        clc
        adc     sth
        sta     t0
        lda     tu + 1
        adc     #0
        sta     t1
        lda     t0
        bne     @d
        dec     t1
@d:     dec     t0
        jsr     @cell
        ldx     t1                      ; (past 255 cells: past the track)
        bne     @end
        cmp     len
        bcc     @in
@end:   lda     len
        beq     @lo
        dec     a
@in:    cmp     t2
        bcs     @put
@lo:    lda     t2
@put:   sta     t3
        rts
@cell:  lsr     t1                      ; A = t0/t1 / 8 or / 4
        ror     t0
        lsr     t1
        ror     t0
        ldx     vert
        beq     @r
        lsr     t1
        ror     t0
@r:     lda     t0
        rts
; GX_X (GX_Y) = A x 4 (x 8): A its low byte, its high byte set.
X4V:    stz     GX_X + 1
        asl     a
        rol     GX_X + 1
        asl     a
        rol     GX_X + 1
        rts
X8V:    stz     GX_Y + 1
        asl     a
        rol     GX_Y + 1
        asl     a
        rol     GX_Y + 1
        asl     a
        rol     GX_Y + 1
        rts

DRAW:   jsr     MEASURE
        stz     idx
@cell:  lda     idx                     ; cell idx of the whole: an arrow, the track or the thumb
        beq     @first
        dec     a
        cmp     len
        beq     @lastc
        bcs     @r
        ldx     smode                   ; a sprite's thumb: every cell the track's
        bne     @track
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
@r:     lda     smode                   ; a sprite's thumb: placed after the paint (POST)
        beq     @rr
        lda     #<POST
        sta     upost
        lda     #>POST
        sta     upost + 1
@rr:    rts

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
@track: ldx     smode
        bne     @strk
        sec
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
@strk:  jsr     ALONG                   ; a sprite's thumb (U3b): in units
        sta     t0
        stx     t1
        cmp     tu
        lda     t1
        sbc     tu + 1
        bcc     @pgup                   ; before it
        lda     t0                      ; into it
        sec
        sbc     tu
        sta     grab
        lda     t1
        sbc     tu + 1
        sta     grab + 1
        bne     @pgdn                   ; past it
        lda     grab
        cmp     sth
        bcs     @pgdn
        lda     #1
        sta     drag
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
        bne     @held
        jmp     @none
@held:  jsr     MEASURE
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
        lda     smode                   ; a sprite's thumb: its room in units (U3b)
        beq     @cells
        lda     room
        sta     sa
        lda     room + 1
        sta     sa + 1
        ora     sa
        bne     @sa
        stz     t0                      ; no room: 0
        stz     t1
        jsr     HOLD
        bra     @live
@cells: lda     len
        sec
        sbc     tsz
        jsr     UNITS
        lda     t0
        sta     sa
        lda     t1
        sta     sa + 1
@sa:
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
@live:  cmp     #UE_NONE
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

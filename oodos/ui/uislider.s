; OOUI's sliders, UT_VSLIDER and UT_HSLIDER (hw/ooui.md, "U2"; proposed):
; UW_VALUE from 0 to UW_A (the top: a vertical slider's top end, a
; horizontal one's right), UW_B's low byte a key's step (0 is 1), its high
; byte the thumb's sprite with UF_SPRITE. The track in TC_SCROLL, the thumb
; a cell in TC_THUMB, or with UF_SPRITE a sprite placed to the pointer unit
; and centred by its size: OOUI's 8 x 8 thumb image in the overlay's group,
; or (U3b) with UF_OWNTHUMB the program's own image, its size and group
; UW_TEXT's low byte, which OOUI never writes. It is put out of sight when
; the slider is hidden. It takes the focus: Up and Right
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
        .import SCMEAS, SCPLACE
        .export UIW_VSLIDER, UIW_HSLIDER, PNONEW
        .export TSIZE, SPRTAIL, POST, hw, hh, tc, tr, tc1, tr1

        .segment "BRODATA"
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
tc:     .res    1                      ; the thumb's cell (SHOWS's)
plast:  .res    2 * 16                 ; each of OOUI's sprites' thumb: its last cell (column, row), $FF none (Z3)
pk:     .res    1                      ;   the one POST is at, x 2
tr:     .res    1
old:    .res    2                      ; WHEEL's: the value before

        .segment "BBSS"                 ; block B's own, not UISTATE (U3b)
tsize:  .res    1                      ; the thumb's size byte (TSIZE)
hw:     .res    1                      ;   half its width, in sprite units
hh:     .res    1                      ;   half its height
tc1:    .res    1                      ; the thumb's last cell along the track (SHOWS's, U3b review, G2)
tr1:    .res    1
pend:   .res    2 * 16                 ; each of OOUI's sprites' thumb: its last cell's other end (Z3, G2)
clo:    .res    1                      ; ENDS's: the thumb's first and last cells from the track's low end
chi:    .res    1
wp:     .res    1                      ; SHOWS's walk: its pass's layer, its record, its first cell's
wi:     .res    1                      ;   answer
wk:     .res    1

        .segment "BCODE"

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
@spr:   ldy     #UW_FLAGS               ; a sprite's thumb: placed after the paint (POST),
        lda     (UIR),y                 ;   when what is in front of it is known (X4)
        and     #UF_SPRITE
        beq     @r
        lda     #<POST
        sta     upost
        lda     #>POST
        sta     upost + 1
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

; The thumb's sprite (UW_B's high byte) for slider idx (MEASURE'd, UI_RECT
; its place): carry clear and placed on pos, its image OOUI's (8 x 8, the
; card page after the pointer's) or with UF_OWNTHUMB the program's, in front
; and shown, if its cells show (its first and last along the track, held to
; the track: in its parents' client areas, in front of what else is there,
; under no menu that is down: X4, and the U3b review's G2); carry set if
; they don't.
SPRITE: jsr     TSIZE                   ; its size: hw, hh half its width and height
        lda     vert                    ; X and Y: the cell's, in sprite units, and pos along
        beq     @h
        lda     UI_RECT                 ; x = column x 4 + 2 - hw (centred on its column)
        sta     tc
        jsr     X4
        clc
        adc     #2
        jsr     LESSW
@x:     lda     UI_RECT + 1             ; y = (row + len) x 8 - 1 - pos - hh
        clc
        adc     len
        jsr     X8
        sec
        sbc     #1
        sta     GX_Y
        bcs     @y0
        dec     GX_Y + 1
@y0:    jsr     LESSH
@y:     lda     GX_Y
        sec
        sbc     pos
        sta     GX_Y
        lda     GX_Y + 1
        sbc     pos + 1
        sta     GX_Y + 1
        lda     tc                      ; its cells: the column; the bottom row less chi, to the
        sta     tc1                     ;   bottom row less clo
        jsr     ENDS
        lda     UI_RECT + 1
        clc
        adc     UI_RECT + 3
        sec
        sbc     #1
        pha
        sec
        sbc     chi
        sta     tr
        pla
        sec
        sbc     clo
        sta     tr1
        bra     SPRTAIL
@h:     lda     UI_RECT                 ; x = column x 4 + pos - hw
        jsr     X4
        clc
        adc     pos
        sta     GX_X
        lda     GX_X + 1
        adc     pos + 1
        sta     GX_X + 1
        lda     GX_X
        jsr     LESSW
@hx:    lda     UI_RECT + 1             ; y = row x 8 + 4 - hh (centred on its row)
        sta     tr
        jsr     X8
        clc
        adc     #4
        sta     GX_Y
        bcc     @hy
        inc     GX_Y + 1
@hy:    jsr     LESSH
        lda     tr                      ; its cells: the row; the column plus clo, to the column
        sta     tr1                     ;   plus chi
        jsr     ENDS
        lda     UI_RECT
        clc
        adc     clo
        sta     tc
        lda     UI_RECT
        clc
        adc     chi
        sta     tc1
; A thumb's common part (U3b: a scroll bar's too, from uiscroll.s's SCPLACE):
; GX_X, GX_Y its place, tc, tr and tc1, tr1 its first and last cells (the
; top left and the bottom right of its cells on the track), idx its record,
; pk its POST place.
SPRTAIL:
        ldx     pk                      ; the thumb's cells, shown or not, for POST's next filter:
        lda     tc                      ;   when what covers only the thumb goes, its damage is
        sta     plast,x                 ;   there (the U3a review's C1); all of them, not its
        lda     tr                      ;   middle's only (the U3b review's G2)
        sta     plast + 1,x
        lda     tc1
        sta     pend,x
        lda     tr1
        sta     pend + 1,x
        jsr     SHOWS                   ; (UI_RECT and UIR changed)
        bcs     @r
        lda     idx
        jsr     REC
        jsr     SNUM
        sta     tmp
        lda     tsize                   ; the program's own thumb (UF_OWNTHUMB, U3b): its
        beq     @oo                     ;   image the program's, its size and group from UW_TEXT
        ldx     #3 << 2
        tay
        bra     @attr
@oo:    jsr     THUMBIMG                ; (tmp its)
        jsr     SNUM
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
        ldx     #3 << 2
@attr:  lda     tmp
        jsr     GX_SPRATTR
        lda     tmp
        jsr     GX_SPRPOS
        clc
@r:     rts

; The thumb's size (U3b): tsize the program's size byte with UF_OWNTHUMB
; (UW_TEXT's low byte: height << 6, width << 4, the palette group; never 0,
; since a group of 0 with 8 x 8 is OOUI's own thumb's), else 0, OOUI's 8 x 8;
; hw and hh half its width and height in sprite units: 4 << the size's code.
TSIZE:  stz     tsize
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_OWNTHUMB
        beq     @s
        ldy     #UW_TEXT
        lda     (UIR),y
        sta     tsize
@s:     lda     tsize
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        pha
        and     #3
        jsr     HALF
        sta     hw
        pla
        lsr     a
        lsr     a
        jsr     HALF
        sta     hh
        rts
HALF:   tax
        lda     #4
@m:     dex
        bmi     @r
        asl     a
        bra     @m
@r:     rts
; GX_X = A (its high byte GX_X + 1's) less hw; GX_Y less hh.
LESSW:  sec
        sbc     hw
        sta     GX_X
        bcs     @r
        dec     GX_X + 1
@r:     rts
LESSH:  lda     GX_Y
        sec
        sbc     hh
        sta     GX_Y
        bcs     @r
        dec     GX_Y + 1
@r:     rts

; The thumb's first and last cells along the track, from its low end (U3b
; review, G2): clo and chi, the cells its sprite's units cover, held to the
; track's 0 to len - 1. Vertical, pos the unit its middle is on: units pos -
; hh + 1 to pos + hh, 8 a cell; horizontal, pos - hw to pos + hw - 1, 4 a
; cell. OOUI's 8 x 8 thumb covers one or two cells; the program's 8 x 32 on
; a vertical track up to five.
ENDS:   lda     hw
        ldx     vert
        beq     @h
        lda     hh
@h:     sta     t2                      ; half its length along
        lda     pos                     ; t0/t1 = pos + vert, the units from it
        clc
        adc     vert
        sta     t0
        lda     pos + 1
        adc     #0
        sta     t1
        pha
        lda     t0
        pha
        sec                             ; its first: pos + vert - half, at least 0
        sbc     t2
        sta     t0
        lda     t1
        sbc     #0
        sta     t1
        bpl     @lo
        stz     t0
        stz     t1
@lo:    jsr     UCELL
        sta     clo
        pla                             ; its last: pos + vert + half - 1, at most len - 1
        clc
        adc     t2
        sta     t0
        pla
        adc     #0
        sta     t1
        lda     t0
        bne     @d
        dec     t1
@d:     dec     t0
        jsr     UCELL
        ldx     t1                      ; (past 255 cells: past the track)
        bne     @end
        cmp     len
        bcc     @in
@end:   lda     len
        beq     @in
        dec     a
@in:    sta     chi
        rts
; A = t0/t1's cell: / 8 (vertical) or / 4; t1 what is left above it.
UCELL:  lsr     t1
        ror     t0
        lsr     t1
        ror     t0
        ldx     vert
        beq     @r
        lsr     t1
        ror     t0
@r:     lda     t0
        rts

; Carry clear if the thumb's cells show: its first and last along the
; track (tc, tr and tc1, tr1; the U3b review's G2), each where the record
; frontmost is slider or scroll bar idx, and no menu that is down covers
; either. One walk in FRONT's order for both (the overlay's pass first if
; something can be on it, each from the table's end down, inside the
; parents' client areas: X5): the first record either cell is on must be
; idx with both on it. (Two FRONTs cost a band's move on OO-Amp's screen
; 50,000 cycles more.)
SHOWS:  lda     #1                      ; RECT's sibling cache on for the walk (Z3)
        sta     rcv
        lda     #$FE
        sta     rcp
        jsr     OVERS
        sta     wp
@pass:  lda     utot
        sta     wi
@l:     dec     wi
        lda     wi
        cmp     #UI_NONE
        beq     @np
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        beq     @l
        cmp     #UT_MENU
        bcc     @p
        cmp     #UT_SEP + 1
        bcc     @l
@p:     lda     wi
        jsr     RECT
        bcs     @l
        lda     rover
        cmp     wp
        bne     @l
        lda     tc
        ldx     tr
        jsr     INCLIP
        rol     wk                      ; (bit 0: the first cell not on it)
        lda     tc1
        ldx     tr1
        jsr     INCLIP
        lda     wk
        and     #1
        bcc     @on2
        bne     @l                      ; on neither: on down
        bra     @no                     ; on the first only: covered, or idx not whole
@on2:   bne     @no                     ; on the last only
        lda     wi                      ; on both: shown if it is idx
        cmp     idx
        bne     @no
        stz     rcv
        lda     umenu                   ; and under no menu that is down (X4)
        cmp     #UI_NONE
        beq     @yes
        jsr     RECT
        bcs     @yes
        lda     tc
        ldx     tr
        jsr     INRECT
        bcc     @no
        lda     tc1
        ldx     tr1
        jsr     INRECT
        bcc     @no
@yes:   clc
        rts
@np:    dec     wp
        bmi     @no
        jmp     @pass
@no:    stz     rcv
        sec
        rts

; Carry clear if cell A, X is in UI_RECT (INRECT), and (INCLIP) in rclip,
; its parents' client areas, too.
INCLIP: jsr     INRECT
        bcs     @r
        cmp     rclip
        bcc     @out
        cmp     rclip + 2
        bcs     @r
        cpx     rclip + 1
        bcc     @out
        cpx     rclip + 3
        rts
@out:   sec
@r:     rts
INRECT: pha
        sec
        sbc     UI_RECT
        cmp     UI_RECT + 2
        bcs     @no
        txa
        sec
        sbc     UI_RECT + 1
        cmp     UI_RECT + 3
        bcs     @no
        pla
        clc
        rts
@no:    pla
        sec
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

        .segment "BRODATA"
THUMB:  .byte   $00, $22, $22, $00
        .byte   $02, $11, $11, $20
        .byte   $21, $11, $11, $12
        .byte   $21, $11, $11, $12
        .byte   $21, $11, $11, $12
        .byte   $21, $11, $11, $12
        .byte   $02, $11, $11, $20
        .byte   $00, $22, $22, $00
        .segment "BCODE"

; After a paint that drew (PAINT's upost): each UF_SPRITE slider's or (U3b)
; scroll bar's thumb placed if it shows, else out of sight (depth 0).
; upmode 1: all out of sight (ui_suspend); 2: OOUI's own images off its card
; memory too (ui_close, X3), a UF_OWNTHUMB image left as the program set it
; (U3b review, G1).
POST:   lda     #$FF
        sta     idx
@l:     inc     idx
        lda     idx
        cmp     utot
        bcc     @rec
        jmp     GX_SPRSHOW
@rec:   jsr     REC
        jsr     SNUM                    ; a sprite's thumb (U3b: a slider's or a scroll bar's)
        bcs     @l
        sec                             ; its last cell (Z3)
        sbc     usprite
        and     #$0F
        asl     a
        sta     pk
        lda     upmode
        bne     @off
        ldx     pk                      ; outside the last paint's damage, its thumb as it was:
        lda     plast,x                 ;   no RECT, no FRONT (U3a: Z3); its cells from the
        cmp     #$FF                    ;   first to the last (the U3b review's G2)
        beq     @go
        sta     UI_RECT
        lda     pend,x
        sec
        sbc     UI_RECT
        inc     a
        sta     UI_RECT + 2
        lda     plast + 1,x
        sta     UI_RECT + 1
        lda     pend + 1,x
        sec
        sbc     UI_RECT + 1
        inc     a
        sta     UI_RECT + 3
        jsr     TOUCHED
        bcs     @next
@go:    lda     idx
        sta     dh
        jsr     RECT
        php
        ldx     pk                      ; its place's first cell kept, till its thumb has one
        lda     UI_RECT
        sta     plast,x
        sta     pend,x
        lda     UI_RECT + 1
        sta     plast + 1,x
        sta     pend + 1,x
        plp
        bcs     @off
        lda     idx
        jsr     REC
        jsr     PLACE1
        bcc     @next
@off:   lda     idx
        jsr     REC
        jsr     SNUM
        sta     tmp
        ldx     #0
        ldy     #0
        jsr     GX_SPRATTR
        lda     upmode
        cmp     #2
        bne     @next
        ldy     #UW_FLAGS               ; the program's own image (UF_OWNTHUMB) left alone: it
        lda     (UIR),y                 ;   is in the program's memory, not OOUI's (U3b review, G1)
        and     #UF_OWNTHUMB
        bne     @next
        stz     GX_V                    ; (page 0: OODOS's, never freed)
        stz     GX_V + 1
        lda     tmp
        jsr     GX_SPRIMG
@next:  jmp     @l

; Record UIR's thumb placed (POST's): a scroll bar's by uiscroll.s, a
; slider's here. Carry as SPRITE's.
PLACE1: ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_VSLIDER
        bcs     @sl
        jsr     SCMEAS
        jmp     SCPLACE
@sl:    jsr     MEASURE
        jmp     SPRITE

; A = record UIR's thumb's sprite, carry clear; carry set if it has none: a
; slider with UF_SPRITE (UW_B's high byte), or (U3b) a scroll bar with
; UF_SPRITE (UW_TEXT's high byte).
SNUM:   ldy     #UW_TYPE
        lda     (UIR),y
        ldy     #UW_B + 1
        cmp     #UT_VSLIDER
        beq     @s
        cmp     #UT_HSLIDER
        beq     @s
        ldy     #UW_TEXT + 1
        cmp     #UT_VSCROLL
        beq     @s
        cmp     #UT_HSCROLL
        bne     @no
@s:     phy
        ldy     #UW_FLAGS
        lda     (UIR),y
        ply
        and     #UF_SPRITE
        beq     @no
        lda     (UIR),y
        clc
        rts
@no:    sec
        rts

; ui_open's (uib.s's BINIT): no thumb has a last cell.
PNONEW: ldx     #2 * 16 - 1
        lda     #$FF
@z:     sta     plast,x
        dex
        bpl     @z
        rts

; Carry clear if UI_RECT meets either of the last paint's damage boxes
; (pdmg: c0, r0, c1, r1 each, c1 and r1 past): only there can a thumb's
; place, or what is in front of it, have changed since the last POST, as
; whatever a paint draws is in its box (U3a: Z3, POST's cost on OO-Amp's
; screen, a FRONT and a RECT of every record for each sprite slider).
TOUCHED:
        ldx     #0
@box:   lda     pdmg,x                  ; an empty box: c0 not before c1
        cmp     pdmg + 2,x
        bcs     @next
        lda     UI_RECT                 ; columns: c < c1 and c + w > c0
        cmp     pdmg + 2,x
        bcs     @next
        clc
        adc     UI_RECT + 2
        beq     @next                   ; (a sum past 255 can't be: 80 columns)
        cmp     pdmg,x
        bcc     @next
        beq     @next
        lda     UI_RECT + 1             ; rows: the same
        cmp     pdmg + 3,x
        bcs     @next
        clc
        adc     UI_RECT + 3
        cmp     pdmg + 1,x
        bcc     @next
        beq     @next
        clc
        rts
@next:  cpx     #4
        beq     @no
        ldx     #4
        bra     @box
@no:    sec
        rts

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
        jsr     SPAN                    ; vertical: span - mpy, held to 0-span (above its top: the
        lda     mpy + 1                 ;   span, X2)
        bpl     @below
        lda     #$FF
        sta     sv
        sta     sv + 1
        bra     @v
@below: lda     su
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
@v:     jsr     SPAN                    ; value = sv x top / span, sv held to the span (X2)
        lda     su
        cmp     sv
        lda     su + 1
        sbc     sv + 1
        bcs     @in
        lda     su
        sta     sv
        lda     su + 1
        sta     sv + 1
@in:    lda     su
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
        lda     val
        sta     old
        lda     val + 1
        sta     old + 1
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
@ev:    lda     val                     ; held at an end: taken, no event (X13)
        cmp     old
        bne     @ch
        lda     val + 1
        cmp     old + 1
        bne     @ch
        lda     #UE_NONE
        clc
        rts
@ch:    lda     dh
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

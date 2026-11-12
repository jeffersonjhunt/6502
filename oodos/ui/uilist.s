; OOUI's list box, UT_LIST (hw/ooui.md, "U2"; proposed): UW_A rows, UW_B
; the first shown, UW_VALUE the selection ($FFFF none). The rows are
; UW_TEXT's array of string pointers, or with UF_ROUTINE a routine OOUI
; calls for each row it draws: A/X the row, Y the list's handle; it returns
; A/X the row's string (in the program's memory, below $4000 or in
; $8000-$BFFF) and Y its style (UL_NORMAL, UL_CURRENT, UL_DIM, and UL_MARKED
; added); it may map W1, which OOUI maps again after, may use the program's
; zero page but not OOUI's, and may call the BDOS and FARCALL, not ui_*.
; OOUI clears the decimal flag after it. A routine of 0: every row empty,
; in TC_LIST (the selection's colour still shown), none called (the U3b
; review's round 2: H2, H4, H6). A TAB in
; a row puts what follows it at the row's right. With UF_SCROLL its last
; column is its scroll bar. The selection in TC_LISTSEL (TC_LISTCUR when it
; hasn't the focus), a current row TC_LISTCUR, a dim one TC_LISTDIM, a
; marked one TC_LISTMARK, the rest TC_LIST.
;
; It takes the focus: Up, Down, PgUp, PgDn, Home and End move the
; selection (UE_CHANGE, the row in UV_VALUE), shown always; Enter chooses
; it (UE_SELECT); with UF_MULTI Space marks it (UE_MARK: the program
; changes its mark and calls ui_dirty). A click selects a row (UE_CHANGE),
; a double-click chooses it (UE_SELECT), on a UF_MULTI list a click marks it
; too (UE_MARK); a click on its scroll bar pages; the wheel scrolls it.
; OO-Amp: the playlist (oa_list.s's drawing and keys), and the Load Files
; dialog's list.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .export UIW_LIST

        .segment "BRODATA"
UIW_LIST:
        .byte   UT_LIST, WF_FOCUS | WF_LIST | WF_REPEAT
        .word   DRAW, KEY, DOWN, 0, 0, WHEEL
NOTEXT: .byte   0                       ; a row routine of 0's rows (the U3b review's round 2, H2)

        .bss
cnt:    .res    2                      ; the rows
topr:   .res    2                      ; the first shown
sel:    .res    2                      ; the selection
rows:   .res    1                      ; the rows shown (its height)
wid:    .res    1                      ; the text's width (less the scroll bar)
row:    .res    2                      ; a row being drawn
i:      .res    1
rsty:   .res    1                      ; its style
str:    .res    2                      ; its string
left:   .res    1                      ; the cells before a TAB, the TAB's index
tabat:  .res    1
rlen:   .res    1                      ; the part after it, its length
flg:    .res    1
myh:    .res    1
rcall:  .res    2                      ; the row routine

        .segment "BCODE"

; cnt, topr, sel, rows, wid, flg from the record.
GETS:   ldy     #UW_VALUE
        lda     (UIR),y
        sta     sel
        iny
        lda     (UIR),y
        sta     sel + 1
        iny
        lda     (UIR),y
        sta     cnt
        iny
        lda     (UIR),y
        sta     cnt + 1
        iny
        lda     (UIR),y
        sta     topr
        iny
        lda     (UIR),y
        sta     topr + 1
.assert UW_A = UW_VALUE + 2 && UW_B = UW_VALUE + 4, error, "GETS's order"
        ldy     #UW_FLAGS
        lda     (UIR),y
        sta     flg
        lda     UI_RECT + 3
        sta     rows
        lda     UI_RECT + 2
        sta     wid
        lda     flg
        and     #UF_SCROLL
        beq     @top
        dec     wid
@top:   lda     cnt                     ; topr at most cnt - rows (0 if fewer), in the record
        sec                             ;   too: a list that shrank (ui_setrange) shows its
        sbc     rows                    ;   rows (X6)
        sta     t0
        lda     cnt + 1
        sbc     #0
        sta     t1
        bcs     @h
        stz     t0
        stz     t1
@h:     lda     t0
        cmp     topr
        lda     t1
        sbc     topr + 1
        bcs     @r
        lda     t0
        sta     topr
        ldy     #UW_B
        sta     (UIR),y
        lda     t1
        sta     topr + 1
        iny
        sta     (UIR),y
@r:     rts

; topr and sel back in the record.
PUTS2:  ldy     #UW_VALUE
        lda     sel
        sta     (UIR),y
        iny
        lda     sel + 1
        sta     (UIR),y
        ldy     #UW_B
        lda     topr
        sta     (UIR),y
        iny
        lda     topr + 1
        sta     (UIR),y
        rts

DRAW:   jsr     GETS
        lda     dh
        sta     myh
        jsr     PFILL
        stz     i
@row:   lda     i
        cmp     rows
        bcc     @go
        jmp     SBAR
@go:    clc                             ; row = topr + i
        adc     topr
        sta     row
        lda     topr + 1
        adc     #0
        sta     row + 1
        lda     row                     ; past the rows: blank
        cmp     cnt
        lda     row + 1
        sbc     cnt + 1
        bcc     @text
        ldy     #TC_LIST
        jsr     TATTR
        lda     UI_RECT + 1
        clc
        adc     i
        tax
        lda     UI_RECT
        jsr     GOTO
        lda     #' '
        ldx     wid
        jsr     REPT
        bra     @next
@text:  jsr     GETROW
        jsr     ROWATTR
        jsr     ROWLINE
@next:  inc     i
        bra     @row

; The picker's list (its row routine PROWS, uirows.s): its shown rows filled
; in one crossing into block C, as the draw starts (U3a, E3); then W1 mapped
; again and UIR on the list again, as after a row routine.
PFILL:  lda     flg
        and     #UF_ROUTINE
        beq     @r
        ldy     #UW_TEXT
        lda     (UIR),y
        cmp     #<PROWS
        bne     @r
        iny
        lda     (UIR),y
        cmp     #>PROWS
        bne     @r
        lda     topr
        sta     prtop
        lda     topr + 1
        sta     prtop + 1
        lda     rows
        cmp     #PR_ROWS
        bcc     @n
        lda     #PR_ROWS
@n:     sta     prn
        XGO     FARC, XC_ROWS
        jsr     MAPW1
        lda     myh
        sta     dh
        jmp     REC
@r:     rts

; str and sty for row: the array's entry, or the routine's answer (W1
; mapped again after it, UIR on the list again).
GETROW: stz     rsty
        ldy     #UW_TEXT
        lda     (UIR),y
        sta     rcall
        iny
        lda     (UIR),y
        sta     rcall + 1
        lda     flg
        and     #UF_ROUTINE
        bne     @call
        lda     rcall                   ; the array: entry row
        sta     UIQ
        lda     rcall + 1
        sta     UIQ + 1
        lda     row
        asl     a
        sta     t0
        lda     row + 1
        rol     a
        clc
        adc     UIQ + 1
        sta     UIQ + 1
        ldy     t0
        lda     (UIQ),y
        sta     str
        iny
        bne     @hi
        inc     UIQ + 1
@hi:    lda     (UIQ),y
        sta     str + 1
        rts
@call:  lda     rcall                   ; a routine of 0: the row empty, no style (the U3b
        ora     rcall + 1               ;   review's round 2, H2)
        bne     @go
        lda     #<NOTEXT
        sta     str
        lda     #>NOTEXT
        sta     str + 1
        rts
@go:    lda     myh
        jsr     PUBH
        tay
        lda     row
        ldx     row + 1
        jsr     @jmp
        cld                             ; (the routine may leave D set: H4)
        sta     str
        stx     str + 1
        sty     rsty
        jsr     MAPW1                   ; the routine may have mapped W1
        lda     myh
        sta     dh
        jsr     REC
        rts
@jmp:   jmp     (rcall)

; uattr for the row: the selection's, or its style's.
ROWATTR:
        lda     row
        cmp     sel
        bne     @style
        lda     row + 1
        cmp     sel + 1
        bne     @style
        ldy     #TC_LISTSEL
        lda     myh
        cmp     ufocus
        beq     @a
        ldy     #TC_LISTCUR
        bra     @a
@style: ldy     #TC_LISTMARK
        lda     rsty
        bmi     @a
        ldy     #TC_LISTCUR
        cmp     #UL_CURRENT
        beq     @a
        ldy     #TC_LISTDIM
        cmp     #UL_DIM
        beq     @a
        ldy     #TC_LIST
@a:     jmp     TATTR

; The row's text on its line: before a TAB at the left, after it at the
; right, spaces between.
ROWLINE:
        lda     str                     ; the TAB's place, and the lengths
        sta     UIQ
        lda     str + 1
        sta     UIQ + 1
        ldy     #0
@f:     lda     (UIQ),y
        beq     @none
        cmp     #9
        beq     @tab
        iny
        bne     @f
@none:  lda     #$FF
        sta     tabat
        stz     rlen
        bra     @draw
@tab:   sty     tabat
        ldx     #0
@l:     iny
        lda     (UIQ),y
        beq     @len
        inx
        bra     @l
@len:   cpx     wid                     ; as wide as the list or wider: not shown, so the
        bcc     @rl                     ;   left part and its spaces fill the line (X7)
        ldx     #0
@rl:    stx     rlen
@draw:  lda     UI_RECT + 1
        clc
        adc     i
        tax
        lda     UI_RECT
        jsr     GOTO
        lda     wid                     ; the left part: up to the TAB, room left for the right
        sec
        sbc     rlen
        bcs     @w
        lda     #0
@w:     sta     left
        ldy     #0
        ldx     left
@c:     cpx     #0
        beq     @pad
        cpy     tabat
        beq     @pad
        lda     (UIQ),y
        beq     @pad
        phy
        phx
        jsr     EMIT
        plx
        ply
        iny
        dex
        bra     @c
@pad:   txa                             ; spaces to the right part
        tax
        lda     #' '
        jsr     REPT
        lda     rlen
        beq     @r
        lda     UIQ                     ; the right part
        sec
        adc     tabat
        sta     UIQ
        bcc     @p
        inc     UIQ + 1
@p:     ldx     rlen
        jmp     PUTS
@r:     rts

; Its scroll bar, in its last column: the track and a thumb for the rows
; shown.
SBAR:   lda     flg
        and     #UF_SCROLL
        beq     @r
        stz     i
        jsr     THUMB                   ; t2 its first row, t3 its rows
@c:     lda     i
        cmp     rows
        bcs     @r
        ldy     #TC_SCROLL
        lda     #$B0
        ldx     i
        cpx     t2
        bcc     @put
        txa
        sec
        sbc     t2
        cmp     t3
        bcs     @tr
        ldy     #TC_THUMB
        lda     #$DB
        bra     @put
@tr:    lda     #$B0
@put:   pha
        jsr     TATTR
        lda     UI_RECT + 1
        clc
        adc     i
        tax
        lda     UI_RECT
        clc
        adc     wid
        jsr     GOTO
        pla
        jsr     EMIT
        inc     i
        bra     @c
@r:     rts

; t3 = the thumb's rows (rows x rows / cnt, 1 to rows), t2 its first ((rows
; - t3) x topr / (cnt - rows)). Small lists only: counts under 256 scaled by
; their high bytes otherwise.
THUMB:  lda     cnt + 1
        bne     @big
        lda     cnt
        cmp     rows
        bcc     @all
        beq     @all
        bra     @calc
@big:   lda     #1                      ; a long list: the thumb a row, placed by topr's high part
        sta     t3
        lda     topr + 1
        sta     t0
        lda     cnt + 1
        sta     t1
        bra     @place
@all:   stz     t2
        lda     rows
        sta     t3
        rts
@calc:  lda     rows                    ; t3 = rows x rows / cnt
        jsr     MULROWS
        lda     t0
        ldx     cnt
        jsr     DIV
        bne     @t
        inc     a
@t:     sta     t3
        lda     topr
        sta     t0
        lda     cnt
        sec
        sbc     rows
        sta     t1
@place: lda     rows                    ; t2 = (rows - t3) x t0 / t1
        sec
        sbc     t3
        sta     t2
        lda     t0
        cmp     t1
        bcc     @f
        lda     t2                      ; at the end
        rts
@f:     lda     t2
        jsr     MULA                    ; t0 x t2 (both under 256), / t1
        ldx     t1
        jsr     DIVW
        sta     t2
        rts

; 16-bit helpers: prod = A x t0 (MULA) or A x A (MULROWS); A = prod / X.
MULROWS:
        sta     t0
MULA:   sta     mx
        stz     prod
        stz     prod + 1
        ldy     #8
@m:     asl     prod
        rol     prod + 1
        asl     mx
        bcc     @n
        clc
        lda     prod
        adc     t0
        sta     prod
        bcc     @n
        inc     prod + 1
@n:     dey
        bne     @m
        rts
DIV:
DIVW:   stx     dv
        lda     #0
        ldy     #16
@d:     asl     prod
        rol     prod + 1
        rol     a
        bcs     @s
        cmp     dv
        bcc     @z
@s:     sbc     dv
        inc     prod
@z:     dey
        bne     @d
        lda     prod
        rts

        .bss
mx:     .res    1
dv:     .res    1
prod:   .res    2
        .segment "BCODE"

; The selection's row kept on view: topr moved if it is off.
SHOW:   lda     sel
        and     sel + 1
        cmp     #$FF
        beq     @r
        lda     sel                     ; above the top: the top it
        cmp     topr
        lda     sel + 1
        sbc     topr + 1
        bcs     @below
        lda     sel
        sta     topr
        lda     sel + 1
        sta     topr + 1
        rts
@below: clc                             ; below the last shown: topr = sel - rows + 1
        lda     topr
        adc     rows
        sta     t0
        lda     topr + 1
        adc     #0
        sta     t1
        lda     sel
        cmp     t0
        lda     sel + 1
        sbc     t1
        bcc     @r
        lda     sel
        sec
        sbc     rows
        sta     topr
        lda     sel + 1
        sbc     #0
        sta     topr + 1
        inc     topr
        bne     @r
        inc     topr + 1
@r:     rts

KEY:    jsr     GETS
        lda     cnt
        ora     cnt + 1
        bne     @some
        sec                             ; no rows: nothing taken
        rts
@some:  lda     ku
        cmp     #K_ENTER
        beq     @enter
        cmp     #K_SPACE
        beq     @space
        ldx     #0                      ; the moves: A/X a signed step, or Home and End
        cmp     #K_UP
        beq     @back1
        cmp     #K_DOWN
        beq     @on1
        cmp     #K_PGUP
        beq     @pgup
        cmp     #K_PGDN
        beq     @pgdn
        cmp     #K_HOME
        beq     @home
        cmp     #K_END
        beq     @end
        sec
        rts
@enter: lda     sel
        and     sel + 1
        cmp     #$FF
        beq     @took
        lda     #UE_SELECT
        jmp     EVROW
@space: lda     flg
        and     #UF_MULTI
        beq     @no
        lda     sel
        and     sel + 1
        cmp     #$FF
        beq     @took
        lda     #UE_MARK
        jmp     EVROW
@no:    sec
        rts
@took:  lda     #UE_NONE
        clc
        rts
@back1: lda     #$FF
        tax
        bra     STEPS
@on1:   lda     #1
        bra     STEPS
@pgup:  lda     rows
        dec     a
        eor     #$FF
        inc     a
        ldx     #$FF
        bra     STEPS
@pgdn:  lda     rows
        dec     a
        bra     STEPS
@home:  stz     t0
        stz     t1
        bra     MOVETO
@end:   lda     cnt
        sec
        sbc     #1
        sta     t0
        lda     cnt + 1
        sbc     #0
        sta     t1
        bra     MOVETO

; The selection moved A/X rows (signed; from none, to the first), held to
; the rows.
STEPS:  sta     t2
        stx     t3
        lda     sel
        and     sel + 1
        cmp     #$FF
        bne     @add
        stz     t0
        stz     t1
        bra     MOVETO
@add:   clc
        lda     sel
        adc     t2
        sta     t0
        lda     sel + 1
        adc     t3
        sta     t1
        lda     t3
        bpl     @up
        bcs     HOLD
        stz     t0
        stz     t1
        bra     MOVETO
@up:    bcc     HOLD
        lda     #$FF
        sta     t0
        sta     t1
HOLD:   lda     t0                      ; below the count
        cmp     cnt
        lda     t1
        sbc     cnt + 1
        bcc     MOVETO
        lda     cnt
        sec
        sbc     #1
        sta     t0
        lda     cnt + 1
        sbc     #0
        sta     t1
; The selection to row t0/t1, on view: UE_CHANGE if it moved.
MOVETO: lda     t0
        cmp     sel
        bne     @set
        lda     t1
        cmp     sel + 1
        beq     @same
@set:   lda     t0
        sta     sel
        lda     t1
        sta     sel + 1
        jsr     SHOW
        jsr     PUTS2
        lda     dh
        jsr     DIRTY
        lda     #UE_CHANGE
        bra     EVROW
@same:  lda     #UE_NONE
        clc
        rts

; Event A with the selection's row.
EVROW:  pha
        lda     dh
        sta     evh
        lda     sel
        sta     evv
        lda     sel + 1
        sta     evv + 1
        pla
        clc
        rts

; A click: on the scroll bar a page; on a row its selection (UE_CHANGE),
; a double-click UE_SELECT, a UF_MULTI list's UE_MARK.
DOWN:   jsr     GETS
        lda     mpy                     ; its row
        lsr     a
        lsr     a
        lsr     a
        sta     i
        lda     flg
        and     #UF_SCROLL
        beq     @rows
        lda     mcol
        sec
        sbc     UI_RECT
        cmp     wid
        bcc     @rows
        jsr     THUMB                   ; the bar: above the thumb up a page, below down
        lda     i
        cmp     t2
        bcs     @below
        lda     rows
        bra     @scr
@below: sec
        sbc     t2
        cmp     t3
        bcc     @none
        lda     rows
        eor     #$FF
        inc     a
@scr:   jsr     SCROLL                  ; (up a page: topr - rows)
        lda     #UE_NONE
        clc
        rts
@rows:  clc
        lda     topr
        adc     i
        sta     t0
        lda     topr + 1
        adc     #0
        sta     t1
        lda     t0                      ; past the rows: nothing
        cmp     cnt
        lda     t1
        sbc     cnt + 1
        bcs     @none
        jsr     MOVETO
        lda     evf
        and     #UV_DOUBLE
        beq     @single
        lda     #UE_SELECT
        jmp     EVROW
@single:
        lda     flg
        and     #UF_MULTI
        beq     @ch
        lda     #UE_MARK
        jmp     EVROW
@ch:    lda     #UE_CHANGE
        jmp     EVROW
@none:  lda     #UE_NONE
        clc
        rts

; The wheel: its steps upward show rows above.
WHEEL:  jsr     GETS
        lda     wst
        jsr     SCROLL
        lda     #UE_NONE
        clc
        rts

; topr less A rows (signed: A negative, more), held from 0 to cnt - rows.
SCROLL: ldx     #0
        eor     #$FF                    ; -A
        inc     a
        cmp     #$80
        bcc     @p
        dex
@p:     clc
        adc     topr
        sta     t0
        txa
        adc     topr + 1
        sta     t1
        bmi     @zero
        lda     cnt                     ; the last top: cnt - rows, or 0
        sec
        sbc     rows
        sta     t2
        lda     cnt + 1
        sbc     #0
        sta     t3
        bcs     @hold
@zero:  stz     t0
        stz     t1
        bra     @set
@hold:  lda     t2
        cmp     t0
        lda     t3
        sbc     t1
        bcs     @set
        lda     t2
        sta     t0
        lda     t3
        sta     t1
@set:   lda     t0
        sta     topr
        lda     t1
        sta     topr + 1
        jsr     PUTS2
        lda     dh
        jmp     DIRTY

; OOUI's position cache and its walkers (hw/ooui.md, "U3b: speed step S2";
; proposed): block C's own memory holds, for each record (the table's and
; the dialogs', 254 at most), where it shows: its cells inside its parents'
; client areas (c0, r0, c1, r1, as RECT and rclip give them: empty when c0
; >= c1 or r0 >= r1), and whether it is hidden (or no record PASS or FRONT
; looks at: an unused one, a menu's), on the overlay, and casts a shadow.
; The walks that looked at every record by RECT read it instead: PASS's (a
; record placed only when it is dirty or meets the box), FRONT's (HIT's),
; MARGIN's own and SHOWS's (a sprite thumb's cells). A, the core, crosses
; here for each (XC_PASS, XC_HIT, XC_MARGIN; B's thumbs XC_SHOWS), and PASS
; crosses back to A's ONE for each record it draws (XA_ONE).
;
; The cache is stale (uany's UQ_CSTALE) after every geometry event but a
; menu's (BOXFULL, ui_open, DLGREC, STALE; ui_show, ui_hide and ui_flags
; through CSET), and found again whole by the next walk: CFILL, or PASS as
; it places every record and writes every unused record's and menu's entry
; hidden. ui_open makes every entry hidden first (CCLR).
; A dirty record is placed at each paint anyway: PASS compares it with its
; entry, and a difference (a record the program moved by its fields and
; only dirtied) makes the cache stale and is a geometry event (UQ_GEOM),
; the rest of that walk placing every record as before.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .export PASS, HIT, MARGIN, SHOWS, CSET, CCLR, CFILL, FRONT, cx0, cy0, cx1, cy1, cfl
        .import tc, tr                  ; uislider.s's: the thumb's first cell (SHOWS's)

CMAX    = UI_MAXREC + DLG_RECS          ; the records: the table's and the dialogs'
CF_HID  = $01                           ; an entry's flags: hidden, or not looked at
CF_OVER = $02                           ;   on the overlay (RECT's rover, x 2)
CF_SHAD = $04                           ;   casts a shadow (EXTRECT's)

        .segment "CBSS"                 ; block C's own (hw/ooui.md, S2): not UISTATE
cx0:    .res    CMAX                   ; the entries, an array a field
cy0:    .res    CMAX
cx1:    .res    CMAX
cy1:    .res    CMAX
cfl:    .res    CMAX
ce:     .res    5                      ; one entry made (CMAKE): c0, r0, c1, r1, its flags
cex:    .res    1                      ;   its ends with its shadow (CMEETS's)
cey:    .res    1
cbnd:   .res    1                      ;   and its layer's boxes' end
ch:     .res    1                      ; CFILL's record
cmode:  .res    1                      ; PASS's: 0 the cache read, 1 filled, 2 neither (stale)
hh:     .res    1                      ; FRONT's record and pass
hp:     .res    1
mb:     .res    1                      ; MARGIN's: what HIT found, and its record
mh:     .res    1
stc1:   .res    1                      ; SHOWS's: the thumb's last cell, its record
str1:   .res    1
sidx:   .res    1
wk:     .res    1                      ;   the first cell's answer

        .segment "CCODE"

; ce = the entry of the record RECT just placed (UI_RECT, rclip, rover,
; rtype; UIR on it), carry RECT's (set: hidden). Keeps nothing.
CMAKE:  bcs     CHID
        ldx     #1                      ; the columns (0) and the rows (1)
@ax:    lda     UI_RECT,x               ; the start: the greater of its own and the clip's
        cmp     rclip,x
        bcs     @s0
        lda     rclip,x
@s0:    sta     ce,x
        lda     UI_RECT,x               ; the end: the lesser (its own held at $FF)
        clc
        adc     UI_RECT + 2,x
        bcc     @e
        lda     #$FF
@e:     cmp     rclip + 2,x
        bcc     @s1
        lda     rclip + 2,x
@s1:    sta     ce + 2,x
        dex
        bpl     @ax
        lda     rover                   ; (0 or 1)
        asl     a
        sta     ce + 4
        lda     rtype                   ; a shadow: a framed window's, if the theme has them
        cmp     #UT_WINDOW              ;   (CASTS; the menus aren't in the cache)
        bne     @r
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_NOFRAME
        bne     @r
        ldy     #TH_FLAGS
        lda     (UIT),y
        and     #TF_SHADOW
        beq     @r
        lda     #CF_SHAD
        tsb     ce + 4
@r:     rts
; ce hidden.
CHID:   lda     #CF_HID
        sta     ce + 4
        stz     ce
        stz     ce + 1
        stz     ce + 2
        stz     ce + 3
        rts

; Entry X = ce (CSTORE); Z set if entry X is ce
; (CSAME).
CSTORE: lda     ce
        sta     cx0,x
        lda     ce + 1
        sta     cy0,x
        lda     ce + 2
        sta     cx1,x
        lda     ce + 3
        sta     cy1,x
        lda     ce + 4
        sta     cfl,x
        rts
CSAME:  lda     ce
        cmp     cx0,x
        bne     @r
        lda     ce + 1
        cmp     cy0,x
        bne     @r
        lda     ce + 2
        cmp     cx1,x
        bne     @r
        lda     ce + 3
        cmp     cy1,x
        bne     @r
        lda     ce + 4
        cmp     cfl,x
@r:     rts

; Carry clear if entry Y can meet one of layer pl's boxes (pbx the first; B5):
; shown, on it, and its cells (clipped to its parents' client areas), with its
; shadow, overlap the box. Narrower than ONE's OVERLAP (EXTRECT's er, its whole
; place unclipped): what it passes over meets the box only where its parents
; clip it, where ONE would draw nothing. Only the boxes up to udend's are
; looked at (those a paint has used). Keeps Y.
CMEETS: lda     cfl,y
        lsr     a                       ; CF_HID
        bcs     @r
        lsr     a                       ; CF_OVER: the layer
        lda     #0
        rol     a
        cmp     pl
        bne     @no
        tax                             ; the layer's boxes' end
        lda     udend,x
        sta     cbnd
        lda     cx1,y                   ; the ends, with the shadow
        sta     cex
        lda     cy1,y
        sta     cey
        lda     #CF_SHAD
        and     cfl,y
        beq     @go
        phy
        ldy     #TM_SHADOWX
        lda     (UIT),y
        clc
        adc     cex
        bcc     @sx
        lda     #$FF
@sx:    sta     cex
        iny
        lda     (UIT),y
        clc
        adc     cey
        bcc     @sy
        lda     #$FF
@sy:    sta     cey
        ply
@go:    ldx     pbx
@b:     lda     udmg,x                  ; the box empty
        cmp     udmg + 2,x
        bcs     @n
        lda     cx0,y
        cmp     udmg + 2,x
        bcs     @n
        lda     cy0,y
        cmp     udmg + 3,x
        bcs     @n
        lda     cex                     ; box c0 < c1 and box r0 < r1
        cmp     udmg,x
        beq     @n
        bcc     @n
        lda     cey
        cmp     udmg + 1,x
        beq     @n
        bcc     @n
        clc
        rts
@n:     txa
        clc
        adc     #8
        tax
        cpx     cbnd
        bcc     @b
@no:    sec
@r:     rts

; The cache found again, if it is stale: every record placed (RECT's
; sibling cache on, as the walks' are), its entry made. Keeps nothing.
CFILL:  lda     #UQ_CSTALE
        bit     uany
        beq     @r
        trb     uany
        lda     #1
        sta     rcv
        lda     #$FE
        sta     rcp
        stz     ch
@l:     lda     ch
        cmp     utot
        bcs     @e
        jsr     CPUT
        ldx     ch
        jsr     CSTORE
        inc     ch
        bra     @l
@e:     stz     rcv
@r:     rts

; Carry clear if entry X shows on layer hp (FRONT's and SHOWS's): not
; hidden, and on it.
CON:    lda     cfl,x
        lsr     a                       ; CF_HID
        bcs     @r
        lsr     a                       ; CF_OVER: the layer
        lda     #0
        rol     a
        eor     hp
        cmp     #1                      ; (0, the same: carry clear)
@r:     rts

; Every entry hidden (XC_CCLR: ui_open's, so no entry is the last table's
; or the bank's memory as ui_load left it; the review's round 8, P1); and
; udend set (as A's UDEND does: ui_open's own before).
CCLR:   jsr     CHID
        ldx     #0
@l:     jsr     CSTORE
        inx
        cpx     #CMAX
        bne     @l
        lda     #8                      ; (B5) and each layer's boxes' end its first's (UDEND's,
        sta     udend                   ;   block A's)
        lda     #4 + 8
        sta     udend + 1
        rts

; Record A's entry found again (XC_CSET: ui_show, ui_hide and ui_flags, the
; dialogs' too, after they changed its flags): only its own can change if
; no record is its child, so it alone is placed; else the cache is stale.
CSET:   sta     ch
        lda     #UQ_CSTALE
        bit     uany
        bne     @r                      ; stale already
        ldx     #0
        txa
        jsr     REC
@l:     cpx     utot
        bcs     @one
        ldy     #UW_PARENT
        lda     (UIR),y
        cmp     ch
        beq     @all
        inx
        txa
        jsr     RNEXT
        bra     @l
@one:   lda     ch
        jsr     CPUT
        ldx     ch
        jmp     CSTORE
@all:   lda     #UQ_CSTALE
        tsb     uany
@r:     rts

; ce = record A's entry, placed (RECT), A kept in ch: UIR on it. An unused
; record or a menu's: hidden, not placed.
CPUT:   sta     ch
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        beq     @none
        cmp     #UT_MENU
        bcc     @p
        cmp     #UT_SEP + 1
        bcc     @none
@p:     lda     ch
        jsr     RECT
        jmp     CMAKE
@none:  jmp     CHID

; One layer's walk (A's PASSA, which cleared the box: pl the layer, pbx
; its box): each record drawn (A's ONE) that is dirty, whole, or that meets
; the box, clipped to it; the others not placed, while the cache holds.
PASS:   stz     cmode                   ; the cache read; if it is stale, filled as each
        lda     #UQ_CSTALE              ;   record is placed
        bit     uany
        beq     @go
        inc     cmode
@go:    lda     #$FE                    ; RECT's sibling cache on for the walk (round 5)
        sta     rcv
        sta     rcp
        stz     ph
        lda     #0
        jsr     REC
@rec:   lda     ph
        cmp     utot
        bcs     @end
        jsr     PREC                    ; (carry set: RECT or ONE moved UIR)
        inc     ph
        lda     ph
        bcs     @re
        jsr     RNEXT                   ; (U3b, B4) the next record's a step on
        bra     @rec
@re:    jsr     REC                     ; the next found
        bra     @rec
@end:   stz     rcv
        lda     cmode                   ; filled: the cache holds
        cmp     #1
        bne     @r
        lda     #UQ_CSTALE
        trb     uany
@r:     rts

; PASS's record ph (UIR on it): drawn by ONE if it is to be; carry set if
; UIR moved (it was placed), clear if not.
PREC:   ldy     #UW_TYPE
        lda     (UIR),y
        beq     @none
        cmp     #UT_MENU
        bcc     @t
        cmp     #UT_SEP + 1
        bcc     @none                   ; the menus: MPASS
@t:     ldy     #UW_FLAGS               ; dirty: placed, and compared with its entry
        lda     (UIR),y
        and     #UF_DIRTY
        bne     @rect
        lda     cmode                   ; filling: every record placed
        cmp     #1
        beq     @rect
        ldx     pbx                     ; clean, and the boxes empty: nothing to draw (B5:
        lda     udmg,x                  ;   the first is empty only when all are, as BOXADD
        cmp     udmg + 2,x              ;   puts a box in the first empty one)
        bcs     @next
        lda     cmode                   ; the cache stale: placed, as before S2
        bne     @rect
        ldy     ph                      ; the cache: drawn only if it meets a box
        jsr     CMEETS
        bcs     @next
        lda     ph
        jsr     RECT
        bra     @one
@none:  jsr     CHID                    ; an unused record or a menu's: hidden in the cache,
        ldx     ph                      ;   so a fill writes every entry (the review's round 8, P1)
        jsr     CSTORE
@next:  clc
        rts
@rect:  lda     ph
        jsr     RECT
        jsr     CMAKE
        ldx     ph
        lda     cmode
        bne     @st
        jsr     CSAME                   ; (the cache read: only a dirty record here)
        beq     @chk
        lda     #UQ_CSTALE | UQ_GEOM    ; moved by its fields: the cache stale, and a
        tsb     uany                    ;   geometry event
        lda     #2
        sta     cmode
@st:    jsr     CSTORE                  ; (filling; or stale, where it does no harm)
@chk:   lda     ph
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DIRTY
        bne     @dirty
        ldy     ph                      ; clean: if it meets a box
        jsr     CMEETS
        bcs     @back
        bra     @one
@dirty: lda     ce + 4                  ; dirty: if it shows on this layer
        lsr     a
        bcs     @back
        lda     rover
        cmp     pl
        bne     @back
@one:   lda     ph
        XGO     FARA, XA_ONE            ; (block A's: it draws)
@back:  sec
        rts

; A the record frontmost at cell mcol, mrowp: the overlay's first, then the
; base's, each from the table's end down, inside its parents' client areas
; (X5); UI_NONE none. Menus aren't looked at. (From UICOM, S2: the cache.)
FRONT:  jsr     CFILL
        stz     rcv                     ; (RECT's sibling cache off after, as before S2)
        jsr     OVERS                   ; the overlay's pass only if something can be on it (Z3)
        sta     hp
@pass:  ldx     utot
@l:     dex
        cpx     #UI_NONE
        beq     @np
        jsr     CON
        bcs     @l
        lda     mcol
        cmp     cx0,x
        bcc     @l
        cmp     cx1,x
        bcs     @l
        lda     mrowp
        cmp     cy0,x
        bcc     @l
        cmp     cy1,x
        bcs     @l
        txa
        rts
@np:    dec     hp
        bpl     @pass
        lda     #UI_NONE
        rts

; A the widget at the pointer's cell: FRONT's, UI_NONE none, or one under a
; modal window. (XC_HIT: uipoll.s's, S2.)
HIT:    jsr     FRONT
        cmp     #UI_NONE
        beq     @r
        sta     hh
        ldx     umodal
        jsr     INSIDE
        lda     hh
        bcc     @r
        lda     #UI_NONE
@r:     rts

; A the record HIT found at the pointer's cell, UI_NONE none: A the record
; the press is for. (U3b, proposed: OO-Amp's targets.) A slider or a scroll
; bar with UF_MARGIN takes a press in its margin too, the cells around it
; that its margin byte names (a slider's UW_TEXT high byte, a scroll bar's
; UW_B high byte): the low four bits the cells on each side across the
; track, the high four (a slider's only) the cells beyond each end along
; it. Only where what HIT found is one of its parents or nothing, so the
; margin never takes a press from another widget, in front of it or behind
; it; inside its parents' client areas and the modal window, as HIT's are.
; The frontmost such one (the table's end first). (XC_MARGIN: uipoll.s's,
; S2; a hidden one refused by RECT.)
MARGIN: sta     mb
        jsr     CFILL
        lda     utot
        sta     mh
@l:     dec     mh
        lda     mh
        cmp     #UI_NONE
        beq     @none
        jsr     MTEST
        bcs     @l
        lda     mh
        rts
@none:  lda     mb
        rts

; Carry clear if record mh takes the press in its margin (MARGIN's).
MTEST:  jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_VSCROLL
        bcc     @n0
        cmp     #UT_HSCROLL + 1
        bcc     @t
        cmp     #UT_VSLIDER
        bcc     @n0
        cmp     #UT_HSLIDER + 1
        bcs     @n0
@t:     ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_MARGIN
        bne     @m
@n0:    sec
        rts
@m:     lda     mh
        jsr     RECT
        bcs     @n0
        ldy     #UW_TYPE                ; t0 the columns' margin, t1 the rows'
        lda     (UIR),y
        ldy     #UW_B + 1               ; a scroll bar's: across only
        cmp     #UT_VSLIDER
        bcc     @sc
        ldy     #UW_TEXT + 1
@sc:    pha
        lda     (UIR),y
        tax
        and     #$0F
        sta     t0                      ; across
        txa
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     t1                      ; along
        pla
        cmp     #UT_VSLIDER
        bcs     @sl
        stz     t1
@sl:    lsr     a                       ; (the vertical types odd)
        bcs     @v
        ldx     t0
        lda     t1
        sta     t0
        stx     t1
@v:     ldx     #0                      ; the columns, then the rows: the cell less the
@ax:    lda     UI_RECT,x               ;   margin's start, below the size and twice the
        sec                             ;   margin (modulo 256: the screen's edge)
        sbc     t0,x
        sta     t2
        lda     mcol,x
        sec
        sbc     t2
        sta     t2
        lda     t0,x
        asl     a
        clc
        adc     UI_RECT + 2,x
        cmp     t2
        bcc     @no
        beq     @no
        lda     mcol,x                  ; inside its parents' client areas (X5)
        cmp     rclip,x
        bcc     @no
        cmp     rclip + 2,x
        bcs     @r
        inx
        cpx     #2
        bcc     @ax
        lda     mh                      ; what HIT found one of its parents, or nothing
        ldx     mb
        jsr     INSIDE
        bcs     @r
        lda     mh                      ; and inside the modal window
        ldx     umodal
        jmp     INSIDE
@no:    sec
@r:     rts

; Carry clear if the thumb's cells show: its first and last along the
; track (tc, tr, and A, X: the last's; the U3b review's G2), each where the
; record frontmost is slider or scroll bar Y, and no menu that is down
; covers either. One walk in FRONT's order for both (the overlay's pass
; first if something can be on it, each from the table's end down, inside
; the parents' client areas: X5): the first record either cell is on must
; be Y with both on it. (XC_SHOWS: uislider.s's, S2: the cache.)
SHOWS:  sta     stc1
        stx     str1
        sty     sidx
        jsr     CFILL
        stz     rcv                     ; (RECT's sibling cache off, as before S2)
        jsr     OVERS
        sta     hp
@pass:  ldx     utot
@l:     dex
        cpx     #UI_NONE
        beq     @np
        jsr     CON
        bcs     @l
        lda     tc
        ldy     tr
        jsr     INENT
        rol     wk                      ; (bit 0: the first cell not on it)
        lda     stc1
        ldy     str1
        jsr     INENT
        lda     wk
        and     #1
        bcc     @on2
        bne     @l                      ; on neither: on down
        bra     @no                     ; on the first only: covered, or idx not whole
@on2:   bne     @no                     ; on the last only
        cpx     sidx                    ; on both: shown if it is idx
        bne     @no
        lda     umenu                   ; and under no menu that is down (X4)
        cmp     #UI_NONE
        beq     @yes
        jsr     RECT
        bcs     @yes
        lda     tc
        ldx     tr
        jsr     INRECT
        bcc     @no
        lda     stc1
        ldx     str1
        jsr     INRECT
        bcc     @no
@yes:   clc
        rts
@np:    dec     hp
        bmi     @no
        jmp     @pass
@no:    sec
        rts

; Carry clear if cell A, Y is in entry X's cells (INENT); cell A, X in
; UI_RECT (INRECT).
INENT:  cmp     cx0,x
        bcc     @out
        cmp     cx1,x
        bcs     @r
        tya
        cmp     cy0,x
        bcc     @out
        cmp     cy1,x
        rts
@out:   sec
@r:     rts
INRECT: sec
        sbc     UI_RECT
        cmp     UI_RECT + 2
        bcs     @r
        txa
        sec
        sbc     UI_RECT + 1
        cmp     UI_RECT + 3
@r:     rts

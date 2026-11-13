; OOUI's core (include/ooui.inc; hw/ooui.md, "The API"): ui_open and
; ui_close, the calls that change a widget, a widget's place on the screen,
; the focus, and what every module shares (ui.inc).
;
; A widget's place is worked out when it is needed, never kept: RECT walks
; from the record up its parents to the top (at most UI_DEPTH), then down
; again, each level placing itself in its parent's client area. Damage is
; kept as two boxes, the base layer's and the overlay's (udmg): what was
; hidden, or a dirty widget drawn, adds its rectangle; ui_poll's painting
; draws again whatever overlaps a box (uidraw.s).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

; UISTATE's start (hw/ooui.md, U3a): what the program reads, at fixed places,
; the program part's uistate.s naming them UISTATE + 0, + 12 and + 16.
        .segment "UIHEAD"
UI_EV:  .res    UV_LEN
UI_RECT: .res   4
UI_RAW: .res    8
.assert UV_LEN = 12, error, "UI_RECT is UISTATE + 12"

        .bss
ublk:                                  ; ui_open's block, as it gives them (UO_*)
uarea:  .res    2
utab:   .res    2
ucount: .res    1
utheme: .res    2
ukeys:  .res    2
ubase:  .res    1
uover:  .res    1
usprite: .res   1
usprites: .res  1
uoflags: .res   1
uwid:   .res    2                      ; UO_WIDGETS: the widget modules' records the table uses
UBLK_LEN = * - ublk
.assert UBLK_LEN = UO_WIDGETS + 2, error, "ublk is ui_open's block"
upage:  .res    2                      ; OOUI's card memory: its first card page
ufocus: .res    1                      ; the focus, UI_NONE none
ufwin:  .res    1                      ; the window that holds it (PAINT's)
umenu:  .res    1                      ; the menu down, UI_NONE none
umsel:  .res    1                      ;   its item chosen
upress: .res    1                      ; the button the left button went down on
ususp:  .res    1                      ; 1: suspended (ui_suspend)
upmode: .res    1                      ; upost's: 0 a paint's, 1 all thumbs off, 2 and their images (X3)
umodal: .res    1                      ; the modal window that shows, UI_NONE none (FINDMODAL)
udmg:   .res    8                      ; the damage: the base's box, the overlay's: c0, r0, c1, r1 each
uany:   .res    1                      ; (U3b) UQ_*: a paint to do, OVERS's and FINDMODAL's answers stale
ucli:   .res    4                      ; RECT's: the client area (column, row, width, height)
rhid:   .res    1                      ;   not 0: hidden (it or a parent, or a menu not down)
rover:  .res    1                      ;   1: on the overlay
rtype:  .res    1                      ;   its type
rcur:   .res    1                      ;   the record being placed (MRECT's)
chain:  .res    UI_DEPTH
utot:   .res    1                      ; the records: the table's, then the dialogs' (ui_msgbox, ui_pick)
udlgp:  .res    2                      ; the dialogs' records (uidlg.s's), 0 while none is linked
ufl:    .res    UT_COUNT               ; each widget type's WR_FLAGS, 0 for the core's own (U3a: block B's, at ui_open)
wtype:  .res    1                      ; WCALL's: the widget's type, for block B's dispatcher
rclip:  .res    4                      ; RECT's: its parents' client areas, c0, r0, c1, r1 (U2: clipping)
rcv:    .res    1                      ; not 0: RECT's cache on (FRONT's, Z3)
rcp:    .res    1                      ;   the parent it holds, and that parent placed:
rcs:    .res    10                     ;   ucli, rhid and rover, then rclip, after it was placed
rdep:   .res    1                      ; RECT's: the chain's length
wo:     .res    1
t0:     .res    1                      ; scratch
t1:     .res    1
t2:     .res    1
t3:     .res    1

        .code

        .segment "UCODE"                ; UICOM: every block's (hw/ooui.md, U3a)
; Every call's start: D clear, UIT the theme. Keeps A, X and Y.
ENTER:  cld
        pha
        lda     utheme
        sta     UIT
        lda     utheme + 1
        sta     UIT + 1
        pla
        rts

; UIR = record A: the table's, or past it a dialog's (the internal handles
; ucount on, udlgp's). Keeps A, X and Y.
REC:    cmp     ucount
        bcs     @dlg
        pha
        stz     UIR + 1
        asl     a
        rol     UIR + 1
        asl     a
        rol     UIR + 1
        asl     a
        rol     UIR + 1
        asl     a
        rol     UIR + 1
        clc
        adc     utab
        sta     UIR
        lda     UIR + 1
        adc     utab + 1
        sta     UIR + 1
        pla
        rts
@dlg:   pha
        sec
        sbc     ucount
        stz     UIR + 1
        asl     a
        rol     UIR + 1
        asl     a
        rol     UIR + 1
        asl     a
        rol     UIR + 1
        asl     a
        rol     UIR + 1
        clc
        adc     udlgp
        sta     UIR
        lda     UIR + 1
        adc     udlgp + 1
        sta     UIR + 1
        pla
        rts

; UIR on record A, when it was on record A - 1 (U3b, B4: a walk up the
; records steps, as REC finds; the dialogs' follow the table's). Keeps A,
; X and Y.
RNEXT:  cmp     ucount
        beq     @dlg
        pha
        lda     UIR
        clc
        adc     #UW_LEN
        sta     UIR
        bcc     @r
        inc     UIR + 1
@r:     pla
        rts
@dlg:   jmp     REC

; A a handle: UI_RECT its place, ucli its client area, rtype, rover; carry
; set if it is hidden. UIR the record's.
RECT:   ldx     rcv                     ; FRONT's cache: a sibling of the last placed (Z3)
        beq     @walk
        pha
        jsr     REC
        ldy     #UW_PARENT
        lda     (UIR),y
        cmp     rcp
        bne     @miss
        ldx     #5                      ; its parent's as it was placed: ucli, rhid, rover
@g:     lda     rcs,x
        sta     ucli,x
        dex
        bpl     @g
        ldx     #3                      ;   and rclip
@gc:    lda     rcs + 6,x
        sta     rclip,x
        dex
        bpl     @gc
        pla
        sta     rcur
        jsr     REC
        jsr     CLIPIN
        jsr     PLACE
        lda     rhid
        cmp     #1
        rts
@miss:  pla
@walk:  ldx     #0
@up:    cpx     #UI_DEPTH
        bcc     @in
        jmp     @deep
@in:    sta     chain,x
        inx
        jsr     REC
        ldy     #UW_PARENT
        lda     (UIR),y
        cmp     #UI_NONE
        bne     @up
        stz     rhid
        stz     rover
        stz     ucli
        stz     ucli + 1
        lda     #UI_COLS
        sta     ucli + 2
        sta     UI_RECT + 2
        lda     #UI_ROWS
        sta     ucli + 3
        sta     UI_RECT + 3
        stz     UI_RECT
        stz     UI_RECT + 1
        stz     rclip
        stz     rclip + 1
        lda     #UI_COLS
        sta     rclip + 2
        lda     #UI_ROWS
        sta     rclip + 3
        stx     rdep
@down:  dex
        bne     @one
        lda     rcv                     ; the record's parent placed: kept for FRONT's next
        beq     @one
        ldx     #5
@k:     lda     ucli,x
        sta     rcs,x
        dex
        bpl     @k
        ldx     #3
@kc:    lda     rclip,x
        sta     rcs + 6,x
        dex
        bpl     @kc
        lda     #UI_NONE                ; its parent: the screen for a top-level record
        ldx     rdep
        cpx     #1
        beq     @p
        lda     chain + 1
@p:     sta     rcp
        ldx     #0
@one:   lda     chain,x
        sta     rcur
        jsr     REC
        phx
        jsr     CLIPIN                  ; the parent's client area clips it (U2)
        jsr     PLACE
        plx
        cpx     #0
        bne     @down
        lda     rhid
        cmp     #1
        rts
@deep:  lda     #1
        sta     rhid
        sec
        rts

; rclip held to ucli (the parent's client area): a widget is drawn and hit
; only inside its parents. A menu's drop-down leaves its bar: rclip the
; screen again for it (its items are clipped to it).
CLIPIN: ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_MENU
        bne     @in
        stz     rclip
        stz     rclip + 1
        lda     #UI_COLS
        sta     rclip + 2
        lda     #UI_ROWS
        sta     rclip + 3
        rts
@in:    ldx     #1
@a:     lda     ucli,x                  ; the starts: the greater
        cmp     rclip,x
        bcc     @e
        sta     rclip,x
@e:     lda     ucli,x                  ; the ends: the lesser
        clc
        adc     ucli + 2,x
        bcc     @ok
        lda     #$FF
@ok:    cmp     rclip + 2,x
        bcs     @n
        sta     rclip + 2,x
@n:     dex
        bpl     @a
        rts

; The record at UIR placed in ucli (its parent's client area; UI_RECT its
; parent's place): UI_RECT and ucli its own.
PLACE:  ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_HIDDEN
        tsb     rhid
        ldy     #UW_TYPE
        lda     (UIR),y
        sta     rtype
        cmp     #UT_MENU
        bcc     @std
        cmp     #UT_SEP + 1
        bcs     @std
        jmp     MRECT                   ; a menu's (uimenu.s)
@std:   cmp     #UT_DESKTOP
        bne     @bar
        ldx     #3
@d:     lda     ucli,x
        sta     UI_RECT,x
        dex
        bpl     @d
        rts
@bar:   cmp     #UT_MENUBAR
        bne     @gen
        ldx     #2
@b:     lda     ucli,x
        sta     UI_RECT,x
        dex
        bpl     @b
        lda     #1
        sta     UI_RECT + 3
        bra     @cli
@gen:   ldx     #0                      ; the columns,
        ldy     #UW_X
        jsr     AXIS
        ldx     #1                      ;   the rows
        ldy     #UW_Y
        jsr     AXIS
@cli:   ldx     #3
@c:     lda     UI_RECT,x
        sta     ucli,x
        dex
        bpl     @c
        ldy     #UW_FLAGS
        lda     (UIR),y
        ldx     rtype
        cpx     #UT_WINDOW
        bne     @pan
        bit     #UF_OVERLAY
        beq     @base
        ldx     #1
        stx     rover
@base:  bit     #UF_NOFRAME
        bne     @r
        bra     SHRINK
@pan:   cpx     #UT_PANEL
        beq     @fr
        cpx     #UT_GROUP
        bne     @r
@fr:    bit     #UF_FRAME
        bne     SHRINK
@r:     rts
SHRINK: inc     ucli
        inc     ucli + 1
        ldx     #2
@d:     lda     ucli + 2 - 1,x          ; each size less 2, never below 0 (round 2's W4)
        sec
        sbc     #2
        bcs     @s
        lda     #0
@s:     sta     ucli + 2 - 1,x
        dex
        bne     @d
        rts

; One axis (X 0 the columns, 1 the rows; Y the position's field, the size's
; two on): UI_RECT,x and UI_RECT + 2,x from ucli's. The size is held to
; the room from the position to the client's end, and a fill that comes
; to less than nothing is 0 (the review's O7: never a size that wraps).
AXIS:   lda     ucli,x                  ; the client's end
        clc
        adc     ucli + 2,x
        sta     aend
        lda     (UIR),y                 ; the position: negative from the end
        bpl     @pos
        clc
        adc     ucli + 2,x
@pos:   clc
        adc     ucli,x
        sta     UI_RECT,x
        iny
        iny
        lda     (UIR),y                 ; the size
        beq     @room                   ; 0: to the end
        bmi     @less                   ; negative: to the end less it
        sta     asz
        jsr     ROOM
        cmp     asz
        bcc     @set                    ; more than the room: the room
        lda     asz
        bra     @set
@less:  sta     asz
        jsr     ROOM
        clc
        adc     asz
        bcs     @set                    ; (carry: the room covers it)
        lda     #0
        bra     @set
@room:  jsr     ROOM
@set:   sta     UI_RECT + 2,x
        rts
; A = the client's end - the position, 0 if the position is at or past it.
ROOM:   lda     aend
        sec
        sbc     UI_RECT,x
        bcs     @r
        lda     #0
@r:     rts

; UIP a string (0 none): A its length as drawn ('&' marks the next
; character; "&&" is one).
DLEN:   lda     UIP
        ora     UIP + 1
        beq     @r
                ldy     #0
        stz     dl
@c:     lda     (UIP),y
        beq     @end
        cmp     #$0D                    ; a CR ends a line (U2)
        beq     @end
        iny
        cmp     #'&'
        bne     @n
        lda     (UIP),y
        beq     @end
        iny
@n:     inc     dl
        bra     @c
@end:   lda     dl
@r:     rts
        .code

; UIP a string (0 none): A its key, the letter after its first single '&'
; in capitals, or 0.
ACCEL:  lda     UIP
        ora     UIP + 1
        beq     @r
        ldy     #0
@c:     lda     (UIP),y
        beq     @r
        iny
        cmp     #'&'
        bne     @c
        lda     (UIP),y
        beq     @r
        iny
        cmp     #'&'
        beq     @c
        jmp     UPPER
@r:     rts

        .segment "UCODE"                ; UICOM: every block's (hw/ooui.md, U3a)
; A in capitals if a small letter.
UPPER:  cmp     #'a'
        bcc     @r
        cmp     #'z' + 1
        bcs     @r
        and     #$DF
@r:     rts

; A a handle: marked dirty (a menu's bar too, an item's menu), and a paint
; to do (UQ_PAINT: U3b, B1). Keeps A, and leaves UIR on record A (the
; review's O1: not on the bar it went up to).
DIRTY:  cmp     utot
        bcs     @r
        pha
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        ora     #UF_DIRTY
        sta     (UIR),y
        lda     #UQ_PAINT               ; (U3b, B1) a paint to do
        tsb     uany
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_MENU
        bcc     @done
        cmp     #UT_SEP + 1
        bcs     @done
        ldy     #UW_PARENT
        lda     (UIR),y
        jsr     DIRTY
@done:  pla
        jmp     REC
@r:     rts

; A a handle: A the window that holds it (itself, if a window), or UI_NONE.
FWIN:   cmp     utot
        bcs     @none
        sta     t1
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_WINDOW
        beq     @me
        ldy     #UW_PARENT
        lda     (UIR),y
        bra     FWIN
@me:    lda     t1
        rts
@none:  lda     #UI_NONE
        rts

; A a handle, X another: carry clear if A is X or inside it (every handle
; is inside UI_NONE).
INSIDE: stx     t2
@l:     cmp     t2
        beq     @yes
        cmp     utot
        bcs     @no
        jsr     REC
        ldy     #UW_PARENT
        lda     (UIR),y
        bra     @l
@yes:   clc
        rts
@no:    sec
        rts

; A a handle: carry clear (A kept) if it can take the focus: a button or a
; check box, shown, enabled, focusable and not under a modal window.
CANFOCUS:
        cmp     utot
        bcs     @no
        sta     t3
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_BUTTON
        beq     @t
        cmp     #UT_CHECK
        beq     @t
        jsr     WFLAGS                  ; a widget module's that takes it (U2)
        and     #WF_FOCUS
        beq     @no
        lda     t3
        jsr     REC
@t:     ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED | UF_NOFOCUS
        bne     @no
        lda     t3
        jsr     RECT
        bcs     @no
        lda     t3
        ldx     umodal
        jsr     INSIDE
        bcs     @no
        lda     t3
        clc
        rts
@no:    sec
        rts

; The focus to A (UI_NONE: none): the old and the new drawn again, and their
; windows if the focus changes window (a title's colour).
SETFOCUS:
        cmp     ufocus
        beq     @r
        sta     t0
        pha
        lda     ufocus
        jsr     DIRTY
        jsr     FWIN
        sta     sf_old
        pla
        jsr     FWIN
        cmp     sf_old
        beq     @same
        jsr     DIRTY
        lda     sf_old
        jsr     DIRTY
@same:  lda     ufocus                  ; a field edited: its last UE_CHANGE waits (U2)
        cmp     #UI_NONE
        beq     @new
        jsr     REC
        ldy     #UW_STATE
        lda     (UIR),y
        bit     #US_EDIT
        beq     @new
        and     #<~US_EDIT
        sta     (UIR),y
        lda     #UE_CHANGE
        sta     upend
        lda     ufocus
        sta     upendh
        ldy     #UW_VALUE
        lda     (UIR),y
        sta     upendv
        iny
        lda     (UIR),y
        sta     upendv + 1
@new:   lda     t0
        sta     ufocus
        jsr     DIRTY
@r:     rts
        .code

; A the way (1 on, $FF back): A the next handle that can take the focus from
; the focus, round the table; carry set if none can.
NEXTFOCUS:
        sta     nf_dir
        lda     ufocus
        cmp     utot
        bcc     @from
        lda     utot                  ; no focus: from either end
        ldx     nf_dir
        bmi     @from
        lda     #$FF
@from:  sta     nf_h
        lda     utot
        sta     nf_n
@step:  lda     nf_h
        clc
        adc     nf_dir
        cmp     utot
        bcc     @in
        ldx     nf_dir                  ; round: past the end to 0, below 0 to the end
        bmi     @top
        lda     #0
        bra     @in
@top:   lda     utot
        dec     a
@in:    sta     nf_h
        jsr     CANFOCUS
        bcc     @got
        dec     nf_n
        bne     @step
        sec
        rts
@got:   lda     nf_h
        clc
        rts

; umodal = the last modal window that shows, or UI_NONE. (U3b, B3) Found
; again only when uany's UQ_MSTALE says so: REFOCUS sets it (ui_show,
; ui_hide, ui_flags, the dialogs), and DLGREC, ui_open, ui_resume, and
; ui_dirty on a window. Else it keeps A, X, Y and UIR.
FINDMODAL:
        bit     uany
        bmi     @go
        rts
@go:    lda     #UQ_MSTALE
        trb     uany
        lda     utot
        sta     fm_h
@l:     dec     fm_h
        lda     fm_h
        cmp     #UI_NONE
        beq     @set
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_WINDOW
        bne     @l
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_MODAL
        beq     @l
        lda     fm_h
        jsr     RECT
        bcs     @l
        lda     fm_h
@set:   sta     umodal
        rts

; er (c0, r0, c1, r1) added to box X (0 the base's, 4 the overlay's), and
; a paint to do (UQ_PAINT: U3b, B1); an empty er adds nothing.
BOXADD: lda     er
        cmp     er + 2
        bcs     @none
        lda     er + 1
        cmp     er + 3
        bcs     @none
        lda     #UQ_PAINT
        tsb     uany
        lda     udmg,x
        cmp     udmg + 2,x
        bcc     @merge
        ldy     #0                      ; empty: er itself
@cp:    lda     er,y
        sta     udmg,x
        inx
        iny
        cpy     #4
        bne     @cp
        rts
@merge: lda     er                      ; the lesser starts,
        cmp     udmg,x
        bcs     @a
        sta     udmg,x
@a:     lda     er + 1
        cmp     udmg + 1,x
        bcs     @b
        sta     udmg + 1,x
@b:     lda     udmg + 2,x              ;   the greater ends
        cmp     er + 2
        bcs     @c
        lda     er + 2
        sta     udmg + 2,x
@c:     lda     udmg + 3,x
        cmp     er + 3
        bcs     @d
        lda     er + 3
        sta     udmg + 3,x
@d:
@none:  rts

; Box X the whole screen, and a paint to do (U3b, B1) and a geometry event (S1).
BOXFULL:
        lda     #UQ_PAINT | UQ_GEOM | UQ_CSTALE ; (S1) a geometry event: ui_open, ui_redraw, ui_resume,
        tsb     uany                    ;   ui_theme (its thumb image's colours)
        stz     udmg,x
        stz     udmg + 1,x
        lda     #UI_COLS
        sta     udmg + 2,x
        lda     #UI_ROWS
        sta     udmg + 3,x
        rts

; Everything drawn again.
ALLDIRTY:
        ldx     #0
        jsr     BOXFULL
        ldx     #4
        jmp     BOXFULL

; A handle in A, checked: past the table, the caller returns carry set and
; UX_HANDLE (CHKH leaves the caller's return on the stack only when it's
; good).
CHKH:   cmp     ucount
        bcc     @r
        cmp     #UI_MSG                 ; a dialog's public handle: its internal one
        bcc     @bad
        cmp     #UI_MSG + DLG_RECS
        bcs     @bad
        ldx     utot
        cpx     ucount
        beq     @bad                    ; no dialog module linked and opened
        sec
        sbc     #UI_MSG
        tax
        lda     PUB2INT,x
        clc
        adc     ucount
@r:     rts
@bad:   pla
        pla
        lda     #UX_HANDLE
        sec
        rts

        .segment "UCODE"                ; UICOM: every block's (hw/ooui.md, U3a)
; A an internal handle: A the public one (a dialog's: $F0 on). Keeps X, Y.
PUBH:   cmp     ucount
        bcc     @r
        cmp     #UI_NONE
        beq     @r
        phx
        sec
        sbc     ucount
        tax
        lda     INT2PUB,x
        plx
@r:     rts

; A a widget type: A its module's WR_FLAGS, 0 for none (a core type). Keeps
; X and Y.
WFLAGS: cmp     #UT_COUNT
        bcs     @none
        phx
        tax
        lda     ufl,x
        plx
        rts
@none:  lda     #0
        rts
        .code

; The handler at Y (WR_DRAW ...) of widget dh's module, jumped to with UIR
; on dh's record and UI_RECT its place (the drawing's placed already);
; carry set and A 0 if it has none.
WCALL:  sty     wo
        cpy     #WR_DRAW
        beq     @placed
        lda     dh
        jsr     RECT
@placed:
        lda     dh
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        sta     wtype
        XGO     FARB, XB_WCALL          ; block B's dispatcher finds the handler (U3a)
        rts

        .segment "UCODE"                ; UICOM: every block's (hw/ooui.md, U3a)
; Record UIR's value held to its type's range (WF_RANGE: 0 to UW_A; WF_LIST:
; below UW_A, or $FFFF none). Keeps nothing.
CLAMP:  ldy     #UW_TYPE
        lda     (UIR),y
        jsr     WFLAGS
        tax
        and     #WF_RANGE | WF_LIST
        bne     @go
        rts
@go:    ldy     #UW_VALUE + 1
        lda     (UIR),y
        sta     t1
        dey
        lda     (UIR),y
        sta     t0
        ldy     #UW_A
        lda     (UIR),y
        sta     t2
        iny
        lda     (UIR),y
        sta     t3
        txa
        and     #WF_LIST
        beq     @range
        lda     t0                      ; a list: $FFFF stays; else below the count
        and     t1
        cmp     #$FF
        beq     @r
        lda     t2
        ora     t3
        bne     @some
        lda     #$FF                    ; no rows: none
        sta     t0
        sta     t1
        bra     @put
@some:  lda     t0
        cmp     t2
        lda     t1
        sbc     t3
        bcc     @r
        lda     t2                      ; the last row
        sec
        sbc     #1
        sta     t0
        lda     t3
        sbc     #0
        sta     t1
        bra     @put
@range: lda     t2                      ; 0 to the top
        cmp     t0
        lda     t3
        sbc     t1
        bcs     @r
        lda     t2
        sta     t0
        lda     t3
        sta     t1
@put:   ldy     #UW_VALUE
        lda     t0
        sta     (UIR),y
        iny
        lda     t1
        sta     (UIR),y
@r:     rts
        .code

; A UX_* error: carry set.
FAIL:   sec
        rts

; ---------------------------------------------------------------------------
; The calls.

ui_open:
        cld
        ldy     uopen                   ; open already: the session left as it is
        beq     @shut
        lda     #UX_BUSY
        sec
        rts
@shut:  sta     UIP
        stx     UIP + 1
        ldy     #UBLK_LEN - 1
@blk:   lda     (UIP),y
        sta     ublk,y
        dey
        bpl     @blk
        jsr     THEME0
        lda     #FT_INFO                ; interface 8, with input and DMA
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        cmp     #$FF
        beq     @ver
        cmp     #8
        bcc     @ver
        txa
        and     #OA_INPUT | OA_DMA
        cmp     #OA_INPUT | OA_DMA
        beq     @area
@ver:   lda     #UX_VERSION
        bra     FAIL
@area:  lda     uarea                   ; an area of layout 2
        sta     UIP
        lda     uarea + 1
        sta     UIP + 1
        ora     UIP
        beq     @noarea
        ldy     #EV_LAYOUT
        lda     (UIP),y
        cmp     #EV_LAYOUT2
        beq     @table
@noarea:
        lda     #UX_AREA
        bra     FAIL
@table: lda     ubase                   ; the layers: the base 0 or 1, the overlay $FF or above it
        cmp     #2
        bcs     @layer
        lda     uover
        cmp     #UI_NONE
        beq     @tab
        cmp     #2
        bcs     @layer
        cmp     ubase
        beq     @layer
        bcs     @tab
@layer: lda     #UX_LAYER
        bra     FAIL
@tab:   jsr     REGISTER
        bcs     FAIL
        jsr     CHECKTAB
        bcs     FAIL
        lda     #<UI_PAGES              ; one card bank
        sta     GX_V
        lda     #>UI_PAGES
        sta     GX_V + 1
        lda     #UI_PAGES
        jsr     GX_VALLOC
        bcs     @mem
        sta     upage
        stx     upage + 1
        jsr     GLYPHS
        bcs     @undo
        jsr     CLEARMAPS
        bcc     @got
@undo:  jsr     FREE
@mem:   lda     #UX_MEMORY
        sec
        rts
@got:   lda     #UI_NONE
        sta     ufocus
        sta     umenu
        sta     umsel
        sta     upress
        stz     ususp
        stz     udmg
        stz     udmg + 2
        stz     udmg + 4
        stz     udmg + 6
        lda     #UQ_STALE | UQ_GEOM | UQ_CSTALE ; (U3b, B3) the modal window and the overlay found again;
        sta     uany                    ;   (S1) a geometry event
        jsr     ENTER
        jsr     POLLINIT
        jsr     SETUP
        jsr     ALLDIRTY
        jsr     FINDMODAL
        lda     #1
        jsr     NEXTFOCUS
        bcs     @done
        jsr     SETFOCUS
@done:  lda     #1
        sta     uopen
        clc
        rts

; The widget types' flags from block B, which has every module (U3a:
; UO_WIDGETS is ignored), and the dialogs' records none.
REGISTER:
        lda     ucount
        sta     utot
        stz     udlgp
        stz     udlgp + 1
        stz     upost
        stz     upost + 1
        XGO     FARB, XB_INIT
        clc
        rts

; The default theme where utheme is 0.
THEME0: lda     utheme
        ora     utheme + 1
        bne     @r
        lda     #<UI_HOUSE
        sta     utheme
        lda     #>UI_HOUSE
        sta     utheme + 1
@r:     rts

; The table checked: carry set and A UX_TABLE for a type U1 doesn't draw, a
; parent at or after its child, a parent that is an unused record, a menu
; not on a menu bar, an item or a line not in a menu, or a record more
; than UI_DEPTH deep. An unused record (UT_NONE) is skipped whatever its
; parent, so a zeroed one is good. Each other record's state cleared, and
; its dirty bit set.
CHECKTAB:
        lda     ucount
        beq     @b0
        cmp     #UI_MAXREC + 1
        bcc     @go
@b0:    jmp     @bad
@go:    stz     t0
@rec:   lda     t0
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_COUNT
        bcs     @bad
        tax
        beq     @next                   ; UT_NONE: nothing more
        lda     OKTYPE,x
        bpl     @core
        lda     OKTYPE,x                ; a widget module's: block B has them all (U3a)
        and     #$7F
@core:  cmp     #0
        beq     @bad
        sta     t1                      ; the parent's type it needs: OKTYPE's
        ldy     #UW_STATE
        lda     #0
        sta     (UIR),y
        ldy     #UW_FLAGS
        lda     (UIR),y
        ora     #UF_DIRTY
        sta     (UIR),y
        ldy     #UW_PARENT
        lda     (UIR),y
        cmp     #UI_NONE
        beq     @top
        cmp     t0
        bcs     @bad
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        beq     @bad                    ; an unused record is no one's parent
        tax
        jsr     DEPTH
        bcs     @bad
        lda     t1
        cmp     #$7F                    ; any parent but a menu bar, a menu, an item or a line
        bne     @want
        cpx     #UT_MENUBAR
        bcc     @next
        cpx     #UT_SEP + 1
        bcs     @next
        bra     @bad
@want:  cmp     #1                      ; 1: any parent at all
        beq     @next
        txa
        cmp     t1
        bne     @bad
        bra     @next
@top:   lda     t1
        cmp     #$7F
        bne     @bad                    ; a menu, an item, a line or a radio button at the top
@next:  jsr     RECEND                  ; its thumb's sprite (X11), its value in its range (X12)
        bcs     @r
        inc     t0
        lda     t0
        cmp     ucount
        beq     @end
        jmp     @rec
@end:   clc
@r:     rts
@bad:   lda     #UX_TABLE
        sec
        rts

; Record t0's last checks: carry set and UX_SPRITE for a thumb's sprite that
; isn't OOUI's; else its value held to its range. Keeps t0.
RECEND: lda     t0
        pha
        jsr     REC
        jsr     SPRCHK
        bcs     @spr
        jsr     CLAMP
        pla
        sta     t0
        clc
        rts
@spr:   pla
        sta     t0
        lda     #UX_SPRITE
        sec
        rts

; Carry set if record UIR is a slider or (U3b) a scroll bar with a sprite's thumb (UF_SPRITE)
; whose sprite isn't one of OOUI's (UO_SPRITE to UO_SPRITE + UO_SPRITES - 1)
; or is the pointer's (UO_SPRITE), or (U3b review, G3) whose own thumb's
; size byte (UF_OWNTHUMB, UW_TEXT's low byte) is 0. Keeps UIR.
SPRCHK: ldy     #UW_TYPE
        lda     (UIR),y
        ldx     #UW_B + 1
        cmp     #UT_VSLIDER
        beq     @s
        cmp     #UT_HSLIDER
        beq     @s
        ldx     #UW_TEXT + 1            ; (U3b: a scroll bar's sprite thumb)
        cmp     #UT_VSCROLL
        beq     @s
        cmp     #UT_HSCROLL
        bne     @ok
@s:     ldy     #UW_FLAGS
        lda     (UIR),y
        bit     #UF_SPRITE
        beq     @ok
        and     #UF_OWNTHUMB
        beq     @num
        ldy     #UW_TEXT                ; the program's own thumb: a size byte
        lda     (UIR),y
        beq     @no
@num:   txa
        tay
        lda     (UIR),y
        sec
        sbc     usprite                 ; its place among OOUI's
        bcc     @no
        beq     @no                     ; the pointer's
        cmp     usprites
        bcs     @no
@ok:    clc
        rts
@no:    sec
        rts

; Carry set if record t0 is more than UI_DEPTH deep (its parents are
; checked already: they come first). Keeps X.
DEPTH:  lda     t0
        ldy     #0
@l:     iny
        cpy     #UI_DEPTH + 1
        bcs     @r
        jsr     REC
        phy
        ldy     #UW_PARENT
        lda     (UIR),y
        ply
        cmp     #UI_NONE
        bne     @l
        clc
@r:     rts

; OOUI's maps cleared (character 0, transparent) before the layers show
; them (the review's O15): the overlay's too when it is a layer. Carry set
; if function 83 refused.
CLEARMAPS:
        stz     GX_A
        lda     upage
        sta     GX_A + 1
        lda     upage + 1
        sta     GX_A + 2
        lda     #<(UI_COLS * UI_ROWS * 2)
        sta     GX_V
        lda     #>(UI_COLS * UI_ROWS * 2)
        sta     GX_V + 1
        lda     #0
        jsr     GX_VFILL
        bcs     @r
        lda     uover
        cmp     #UI_NONE
        clc
        beq     @r
        lda     GX_A + 1
        clc
        adc     #UI_OFS_OVER
        sta     GX_A + 1
        bcc     @f
        inc     GX_A + 2
@f:     lda     #0
        jmp     GX_VFILL
@r:     rts

        .rodata
; Each type U1 takes: the parent's type it needs ($FF any of the others, 1
; any at all); 0 refused (U2's).
; Bit 7: a widget module's type, which its record must be registered for.
OKTYPE: .byte   1                       ; UT_NONE
        .byte   $7F                     ; UT_DESKTOP
        .byte   $7F                     ; UT_WINDOW
        .byte   $7F                     ; UT_PANEL
        .byte   $7F                     ; UT_LABEL
        .byte   $7F                     ; UT_BUTTON
        .byte   $7F                     ; UT_CHECK
        .byte   $80 | $7F               ; UT_GROUP
        .byte   $80 | UT_GROUP          ; UT_RADIO
        .byte   $7F                     ; UT_MENUBAR
        .byte   UT_MENUBAR              ; UT_MENU
        .byte   UT_MENU                 ; UT_ITEM
        .byte   UT_MENU                 ; UT_SEP
        .res    UT_CHOICE - UT_VSCROLL + 1, $80 | $7F ; UT_VSCROLL ... UT_CHOICE
.assert * - OKTYPE = UT_COUNT, error, "OKTYPE: a byte a type"
.assert UT_NONE = 0 && UT_SEP = 12 && UT_MENUBAR = 9 && UT_VSCROLL = 13, error, "OKTYPE's order"

; The dialogs' handles: the public ones (UI_MSG on) and the internal ones
; (ucount on), in the order they are drawn: the picker's window and its
; widgets, then the message box's, so a box is over the picker.
PUB2INT: .byte  10, 0, 11, 12, 13, 1, 2, 3, 4, 5, 6, 7, 8, 9
        .segment "URODATA"              ; (PUBH's: UICOM's, U3a)
INT2PUB: .byte  UI_MSG + 1, UI_MSG + 5, UI_MSG + 6, UI_MSG + 7, UI_MSG + 8, UI_MSG + 9, UI_MSG + 10
        .byte   UI_MSG + 11, UI_MSG + 12, UI_MSG + 13, UI_MSG, UI_MSG + 2, UI_MSG + 3, UI_MSG + 4
.assert * - INT2PUB = DLG_RECS, error, "a public handle a dialog record"
        .rodata

        .code

ui_close:
        jsr     ENTER
        lda     uopen
        beq     @r                      ; not open: nothing to free
        stz     uopen
        jsr     POLLSTOP                ; (no drag left held: X10)
        jsr     PTROFF
        lda     usprites                ; the pointer's image off OOUI's memory too, which is
        beq     @nop                    ;   freed (U3a, from U2's review), if the sprite is OOUI's
        lda     uoflags
        and     #UO_OWNMOUSE
        bne     @nop
        stz     GX_V                    ; (page 0: OODOS's, never freed)
        stz     GX_V + 1
        lda     usprite
        jsr     GX_SPRIMG
        jsr     GX_SPRSHOW
@nop:   lda     #2                      ; the thumbs gone, off OOUI's memory (X3)
        jsr     THUMBSOFF
        jsr     FREE
        jsr     GX_CONSOLE
@r:     clc
        rts

; The pointer off, if OOUI set it (the review's O17): its sprite's depth 0
; at the next vertical blank, and OODOS no longer moving it.
PTROFF: lda     uptr
        beq     @r
        stz     uptr
        lda     usprite
        ldx     #0
        ldy     #1 << 6 | 1 << 4
        jsr     GX_SPRATTR
        jsr     GX_SPRSHOW
        lda     #$FF
        sta     dmp + 1
        jmp     POINTER
@r:     rts

; The widget modules' sprites (the sliders' and scroll bars' thumbs,
; through upost) out of sight: A 1 (ui_suspend), or 2 (ui_close: OOUI's own
; images off its card memory too, which is freed). Keeps nothing.
THUMBSOFF:
        sta     upmode
        lda     upost + 1
        beq     @r
        jsr     @go
@r:     stz     upmode
        rts
@go:    XGO     FARB, XB_POST           ; (a widget module's: block B's, U3a)
        rts

; OOUI's card memory free again.
FREE:   lda     #<UI_PAGES
        sta     GX_V
        lda     #>UI_PAGES
        sta     GX_V + 1
        lda     upage
        ldx     upage + 1
        jmp     GX_VFREE

; Function 83's DM_POINTER with dmp (+1 the sprite, $FF off).
POINTER:
        lda     #DM_POINTER
        sta     dmp
        stz     dmp + 2
        stz     dmp + 3
        lda     #<dmp
        ldx     #>dmp
        ldy     #O_DMA
        jmp     BDOS

ui_show:
        jsr     ENTER
        jsr     CHKH
        sta     sh_h
        jsr     MENUDMG
        lda     sh_h
        jsr     DIRTY                   ; (an item's menu too: round 2's W2)
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #<~UF_HIDDEN
        sta     (UIR),y
        bra     HIDSET

ui_hide:
        jsr     ENTER
        jsr     CHKH
; Record A (an internal handle) hidden: what it covered drawn again, a menu
; down in it put up, the focus moved on (the dialogs' too).
HIDEH:  sta     sh_h
        jsr     DROP                    ; a drag in it ended (X10)
        lda     sh_h
        jsr     DAMAGE
        jsr     MENUDMG
        lda     umenu                   ; a menu down inside it goes up first, while it shows
        ldx     sh_h
        jsr     INSIDE
        bcs     @flag
        jsr     MCLOSE
@flag:  lda     sh_h
        jsr     DIRTY                   ; (an item's menu drawn again: W2)
        ldy     #UW_FLAGS
        lda     (UIR),y
        ora     #UF_HIDDEN
        sta     (UIR),y
; (S2) record sh_h's entry in the position cache found again (block C's
; CSET: its own, or the whole cache stale if it has children); then the
; focus.
HIDSET: lda     sh_h
        XGO     FARC, XC_CSET
; The focus kept where it can be, else moved on (a modal window shown, or
; the focus's widget hidden). (U3b, B3) After a record shown, hidden, its
; flags changed or a dialog made: the modal window and OVERS's answer found
; again.
REFOCUS:
        lda     #UQ_STALE | UQ_GEOM     ; (S1: a geometry event; S2: its callers see to the cache)
        tsb     uany
        jsr     FINDMODAL
        lda     umenu                   ; a menu down outside the modal window goes up (W5)
        cmp     #UI_NONE
        beq     @f
        ldx     umodal
        jsr     INSIDE
        bcc     @f
        jsr     MCLOSE
@f:     lda     ufocus
        jsr     CANFOCUS
        bcc     @ok
        lda     #1
        jsr     NEXTFOCUS
        bcc     @set
        lda     #UI_NONE
@set:   jsr     SETFOCUS
@ok:    clc
        rts

; Record sh_h, if it is an item or a line of the menu down: the menu's
; place added to the damage, before the record's flags change its rows
; (round 2's W2). Keeps nothing.
MENUDMG:
        lda     umenu
        cmp     #UI_NONE
        beq     @r
        lda     sh_h
        jsr     REC
        ldy     #UW_PARENT
        lda     (UIR),y
        cmp     umenu
        bne     @r
        jmp     DAMAGE
@r:     rts

; Record A's place, with its shadow, added to its layer's damage, if it
; shows.
DAMAGE: pha
        jsr     RECT
        pla
        bcs     @r
        jsr     REC
        jsr     EXTRECT
        jsr     LBOX
        jmp     BOXADD
@r:     rts

; X the box for the record RECT placed: the overlay's if it is on the
; overlay and the overlay is a layer of its own.
LBOX:   ldx     #0
        lda     rover
        beq     @r
        lda     uover
        cmp     #UI_NONE
        beq     @r
        ldx     #4
@r:     rts

ui_flags:
        and     #<~(UF_DIRTY | UF_HIDDEN)
        sta     t0
        txa
        and     #<~(UF_DIRTY | UF_HIDDEN)
        eor     #$FF
        sta     t1
        tya
        jsr     ENTER
        jsr     CHKH
        sta     sh_h
        jsr     FLGCHK                  ; a thumb's checked (the U3b review's round 2, H1)
        bcc     @took
        lda     #UX_SPRITE
        rts
@took:  lda     sh_h
        jsr     DAMAGE                  ; where it was (the review's O11)
        jsr     MENUDMG
        lda     sh_h
        jsr     DIRTY
        ldy     #UW_FLAGS
        lda     (UIR),y
        ora     t0
        and     t1
        sta     (UIR),y
        and     #UF_DISABLED            ; the menu down, disabled: up (W5)
        beq     @re
        lda     sh_h
        cmp     umenu
        bne     @re
        jsr     MCLOSE
@re:    lda     sh_h                    ; (S2) its entry in the position cache again
        XGO     FARC, XC_CSET
        jsr     REFOCUS                 ; the focus and the modal window again
        lda     sh_h
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        clc
        rts

; Record sh_h's new flags ((old | t0) & t1) checked, if it is a slider or a
; scroll bar, as ui_open checks the table (SPRCHK; the U3b review's round
; 2, H1): carry set if refused, the record as it was. Refused too: a change
; to UF_OWNTHUMB while UF_SPRITE is on before and after (round 3, J2: the
; sprite's image would change hands while it shows); UF_SPRITE turned on or
; off with it is taken. Taken, a sprite thumb whose UF_SPRITE goes off has
; its sprite put out of sight (depth 0). Keeps t0, t1 and sh_h; the flags
; are ui_flags's to write.
FLGCHK: lda     sh_h
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        ldx     #UW_TEXT + 1            ; a scroll bar's sprite
        cmp     #UT_VSCROLL
        bcc     @ok
        cmp     #UT_HSCROLL + 1
        bcc     @chk
        ldx     #UW_B + 1               ; a slider's
        cmp     #UT_VSLIDER
        bcc     @ok
        cmp     #UT_HSLIDER + 1
        bcs     @ok
@chk:   phx
        ldy     #UW_FLAGS
        lda     (UIR),y
        pha                             ; the old flags
        sta     t2
        ora     t0
        and     t1
        sta     (UIR),y
        tax                             ; (J2) UF_OWNTHUMB changed,
        eor     t2
        and     #UF_OWNTHUMB
        beq     @spr
        txa                             ;   UF_SPRITE on before and after: refused
        and     t2
        and     #UF_SPRITE
        sec
        bne     @old
@spr:   jsr     SPRCHK
@old:   ply                             ; (the old flags in Y, carry kept)
        tya
        ldy     #UW_FLAGS
        sta     (UIR),y                 ; the old flags again, refused or not
        plx
        bcs     @r
        and     #UF_SPRITE              ; UF_SPRITE on before
        beq     @ok
        lda     t1                      ; and not cleared: nothing to hide
        and     #UF_SPRITE
        bne     @ok
        txa
        tay
        lda     t0                      ; GX_SPRATTR keeps no t0, t1
        pha
        lda     t1
        pha
        lda     (UIR),y                 ; the sprite out of sight
        ldx     #0
        ldy     #0
        jsr     GX_SPRATTR
        pla
        sta     t1
        pla
        sta     t0
@ok:    clc
@r:     rts

ui_settext:
        sta     t0
        stx     t1
        lda     #UW_TEXT
        bra     SETW
ui_setvalue:
        sta     t0
        stx     t1
        lda     #UW_VALUE
; Y the handle, t0 and t1 the word, A the field.
SETW:   sta     t2
        tya
        jsr     ENTER
        jsr     CHKH
        ldx     t2                      ; (S1, (viii)) a value, and UF_SPRITE's bit set: block B's
        cpx     #UW_VALUE               ;   VDIRTY, which draws a sprite thumb's slider's or scroll
        bne     @d                      ;   bar's cells not, only its thumb placed (the bit is
        pha                             ;   other types' own too: VDIRTY tells them apart)
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_SPRITE
        beq     @nd
        pla
        XGO     FARB, XB_VDIRTY
        bra     @c
@nd:    pla
@d:     jsr     DIRTY
@c:
        ldy     #UW_TYPE                ; a button's, a check box's, an item's value is 0 or 1
        lda     (UIR),y
        ldy     t2
        cpy     #UW_VALUE
        beq     @val
        jmp     TXTCHK                  ; UW_TEXT: a thumb's is checked (U3b review, G3)
@val:
        cmp     #UT_BUTTON
        beq     @bool
        cmp     #UT_CHECK
        beq     @bool
        cmp     #UT_ITEM
        bne     @put
@bool:  lda     t0
        ora     t1
        beq     @z
        lda     #1
@z:     sta     t0
        stz     t1
@put:   lda     t0
        sta     (UIR),y
        iny
        lda     t1
        sta     (UIR),y
        cpy     #UW_VALUE + 1
        bne     @done
        jsr     CLAMP                   ; held to its range (U2)
@done:  clc
        rts

; ui_settext's word (t0, t1) to record UIR, of type A (the U3b review's
; G3). A slider's or a scroll bar's UW_TEXT is its thumb's size byte (with
; UF_OWNTHUMB) and a scroll bar's its sprite, so the word is checked as
; ui_open checks the table (SPRCHK): refused, carry set and A UX_SPRITE,
; the record as it was. Taken, a scroll bar's thumb whose sprite changed
; has its old sprite put out of sight (depth 0, shown with the next paint's
; thumbs), and the new one no last place (POST's filter).
TXTCHK: cmp     #UT_VSCROLL
        bcc     @plain
        cmp     #UT_HSCROLL + 1
        bcc     @chk
        cmp     #UT_VSLIDER
        bcc     @plain
        cmp     #UT_HSLIDER + 1
        bcs     @plain
@chk:   ldy     #UW_TEXT + 1
        lda     (UIR),y
        pha                             ; the old word, its high byte on top
        dey
        lda     (UIR),y
        pha
        lda     t0
        sta     (UIR),y
        iny
        lda     t1
        sta     (UIR),y
        jsr     SPRCHK
        bcc     @took
        ldy     #UW_TEXT                ; refused: the old word again
        pla
        sta     (UIR),y
        iny
        pla
        sta     (UIR),y
        lda     #UX_SPRITE
        sec
        rts
@took:  pla
        pla                             ; the old sprite
        ldy     #UW_TYPE
        tax
        lda     (UIR),y
        cmp     #UT_VSLIDER
        bcs     @ok                     ; a slider's sprite is UW_B's: unchanged
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_SPRITE
        beq     @ok
        cpx     t1
        beq     @ok
        lda     t1                      ; the new sprite: no last place, so placed (Z3)
        sec
        sbc     usprite
        and     #$0F
        asl     a
        tay
        lda     #$FF
        sta     plast,y
        txa                             ; the old one out of sight
        ldx     #0
        ldy     #0
        jsr     GX_SPRATTR
@ok:    clc
        rts
@plain: ldy     #UW_TEXT
        lda     t0
        sta     (UIR),y
        iny
        lda     t1
        sta     (UIR),y
        clc
        rts

; Y a handle, A/X a list's count, or a bar's, slider's or scroll bar's top
; (UW_A): the value held to it; dirty.
ui_setrange:
        sta     t0
        stx     t1
        tya
        jsr     ENTER
        jsr     CHKH
        jsr     DIRTY
        ldy     #UW_A
        lda     t0
        sta     (UIR),y
        iny
        lda     t1
        sta     (UIR),y
        jsr     CLAMP
        clc
        rts

ui_getvalue:
        tya
        jsr     ENTER
        jsr     CHKH
        jsr     REC
        ldy     #UW_VALUE + 1
        lda     (UIR),y
        tax
        dey
        lda     (UIR),y
        clc
        rts

ui_dirty:
        jsr     ENTER
        jsr     CHKH
        jsr     DIRTY
        ldy     #UW_TYPE                ; (U3b, B3) a window: its flags may be the program's
        lda     (UIR),y                 ;   own writing (UF_MODAL, UF_OVERLAY), so the modal
        cmp     #UT_WINDOW              ;   window and the overlay are found again
        beq     STALE
        clc
        rts
STALE:  lda     #UQ_STALE | UQ_GEOM | UQ_CSTALE ; (S1: a geometry event; S2: the cache stale)
        tsb     uany
        clc
        rts

ui_redraw:
        jsr     ALLDIRTY
        bra     STALE

ui_focus:
        jsr     ENTER
        jsr     CHKH
        sta     t3
        jsr     FINDMODAL
        lda     t3
        jsr     CANFOCUS
        bcs     @no
        jsr     SETFOCUS
        clc
        rts
@no:    lda     #UX_HANDLE
        sec
        rts

ui_getfocus:
        lda     ufocus
        jsr     PUBH
        clc
        rts

ui_rect:
        jsr     ENTER
        jsr     CHKH
        jmp     RECT

ui_info:
        lda     #UI_COLS
        ldx     #UI_ROWS
        ldy     #UB_TEXT
        clc
        rts

ui_theme:
        sta     utheme
        stx     utheme + 1
        jsr     THEME0
        lda     uopen                   ; shut, or suspended (the layers the program's): only
        beq     @r                      ;   kept, for ui_open or ui_resume to set (round 2's W1)
        lda     ususp
        bne     @r
        jsr     ENTER
        jsr     LAYERS                  ; the groups (PALOFS), then the colours
        jsr     SETPAL
        lda     uptr
        beq     @all
        jsr     PTRATTR
@all:   jsr     ALLDIRTY
@r:     clc
        rts

ui_suspend:
        jsr     ENTER
        lda     ususp                   ; suspended already: nothing more
        bne     @r
        lda     uopen                   ; (shut: only kept)
        beq     @s
        jsr     POLLSTOP                ; no repeat, no first click, no drag (X1, X9, X10)
        jsr     PTROFF
        lda     #1                      ; the thumbs' sprites out of sight (X3)
        jsr     THUMBSOFF
@s:     lda     #1
        sta     ususp
@r:
        clc
        rts

ui_resume:
        stz     ususp
        jsr     ENTER
        jsr     SETUP
        jsr     ALLDIRTY
        jmp     STALE                   ; (U3b, B3)

        .bss
sf_old: .res    1
nf_dir: .res    1
nf_h:   .res    1
nf_n:   .res    1
fm_h:   .res    1
sh_h:   .res    1
dl:     .res    1
aend:   .res    1                      ; AXIS's
asz:    .res    1
dmp:    .res    4                      ; DM_POINTER's block

        .bss
uopen:  .res    1                      ; 1: open (ui_load zeroes UISTATE: hw/ooui.md, U3a, D2)

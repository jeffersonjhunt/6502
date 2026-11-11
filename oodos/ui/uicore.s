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
UBLK_LEN = * - ublk
.assert UBLK_LEN = UO_FLAGS + 1, error, "ublk is ui_open's block"
upage:  .res    2                      ; OOUI's card memory: its first card page
ufocus: .res    1                      ; the focus, UI_NONE none
ufwin:  .res    1                      ; the window that holds it (PAINT's)
umenu:  .res    1                      ; the menu down, UI_NONE none
umsel:  .res    1                      ;   its item chosen
upress: .res    1                      ; the button the left button went down on
ususp:  .res    1                      ; 1: suspended (ui_suspend)
umodal: .res    1                      ; the modal window that shows, UI_NONE none (FINDMODAL)
udmg:   .res    8                      ; the damage: the base's box, the overlay's: c0, r0, c1, r1 each
ucli:   .res    4                      ; RECT's: the client area (column, row, width, height)
rhid:   .res    1                      ;   not 0: hidden (it or a parent, or a menu not down)
rover:  .res    1                      ;   1: on the overlay
rtype:  .res    1                      ;   its type
rcur:   .res    1                      ;   the record being placed (MRECT's)
chain:  .res    UI_DEPTH
UI_EV:  .res    UV_LEN
UI_RECT: .res   4
UI_RAW: .res    8
t0:     .res    1                      ; scratch
t1:     .res    1
t2:     .res    1
t3:     .res    1

        .code

; Every call's start: D clear, UIT the theme. Keeps A, X and Y.
ENTER:  cld
        pha
        lda     utheme
        sta     UIT
        lda     utheme + 1
        sta     UIT + 1
        pla
        rts

; UIR = record A. Keeps A, X and Y.
REC:    pha
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

; A a handle: UI_RECT its place, ucli its client area, rtype, rover; carry
; set if it is hidden. UIR the record's.
RECT:   ldx     #0
@up:    cpx     #UI_DEPTH
        bcs     @deep
        sta     chain,x
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
@down:  dex
        lda     chain,x
        sta     rcur
        jsr     REC
        phx
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
        bne     @r
        bit     #UF_FRAME
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

; A in capitals if a small letter.
UPPER:  cmp     #'a'
        bcc     @r
        cmp     #'z' + 1
        bcs     @r
        and     #$DF
@r:     rts

; A a handle: marked dirty (a menu's bar too, an item's menu). Keeps A, and
; leaves UIR on record A (the review's O1: not on the bar it went up to).
DIRTY:  cmp     ucount
        bcs     @r
        pha
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        ora     #UF_DIRTY
        sta     (UIR),y
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
FWIN:   cmp     ucount
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
        cmp     ucount
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
        cmp     ucount
        bcs     @no
        sta     t3
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_BUTTON
        beq     @t
        cmp     #UT_CHECK
        bne     @no
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
@same:  lda     t0
        sta     ufocus
        jsr     DIRTY
@r:     rts

; A the way (1 on, $FF back): A the next handle that can take the focus from
; the focus, round the table; carry set if none can.
NEXTFOCUS:
        sta     nf_dir
        lda     ufocus
        cmp     ucount
        bcc     @from
        lda     ucount                  ; no focus: from either end
        ldx     nf_dir
        bmi     @from
        lda     #$FF
@from:  sta     nf_h
        lda     ucount
        sta     nf_n
@step:  lda     nf_h
        clc
        adc     nf_dir
        cmp     ucount
        bcc     @in
        ldx     nf_dir                  ; round: past the end to 0, below 0 to the end
        bmi     @top
        lda     #0
        bra     @in
@top:   lda     ucount
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

; umodal = the last modal window that shows, or UI_NONE.
FINDMODAL:
        lda     ucount
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

; er (c0, r0, c1, r1) added to box X (0 the base's, 4 the overlay's); an
; empty er adds nothing.
BOXADD: lda     er
        cmp     er + 2
        bcs     @none
        lda     er + 1
        cmp     er + 3
        bcs     @none
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

; Box X the whole screen.
BOXFULL:
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
        bcs     @bad
        rts
@bad:   pla
        pla
        lda     #UX_HANDLE
        sec
        rts

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
@tab:   jsr     CHECKTAB
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
        jsr     ENTER
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
        beq     @bad
        cmp     #UI_MAXREC + 1
        bcs     @bad
        stz     t0
@rec:   lda     t0
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_SEP + 1
        bcs     @bad
        tax
        beq     @next                   ; UT_NONE: nothing more
        lda     OKTYPE,x
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
        cmp     #$FF                    ; any parent but a menu bar, a menu, an item or a line
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
        cmp     #$FF
        bne     @bad                    ; a menu, an item or a line at the top
@next:  inc     t0
        lda     t0
        cmp     ucount
        bne     @rec
        clc
        rts
@bad:   lda     #UX_TABLE
        sec
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
OKTYPE: .byte   1                       ; UT_NONE
        .byte   $FF                     ; UT_DESKTOP
        .byte   $FF                     ; UT_WINDOW
        .byte   $FF                     ; UT_PANEL
        .byte   $FF                     ; UT_LABEL
        .byte   $FF                     ; UT_BUTTON
        .byte   $FF                     ; UT_CHECK
        .byte   0, 0                    ; UT_GROUP, UT_RADIO: U2
        .byte   $FF                     ; UT_MENUBAR
        .byte   UT_MENUBAR              ; UT_MENU
        .byte   UT_MENU                 ; UT_ITEM
        .byte   UT_MENU                 ; UT_SEP
.assert UT_NONE = 0 && UT_SEP = 12 && UT_MENUBAR = 9, error, "OKTYPE's order"

        .code

ui_close:
        jsr     ENTER
        lda     uopen
        beq     @r                      ; not open: nothing to free
        stz     uopen
        jsr     PTROFF
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
        bra     REFOCUS

ui_hide:
        jsr     ENTER
        jsr     CHKH
        sta     sh_h
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
; The focus kept where it can be, else moved on (a modal window shown, or
; the focus's widget hidden).
REFOCUS:
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
@re:    jsr     REFOCUS                 ; the focus and the modal window again
        lda     sh_h
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        clc
        rts

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
        jsr     DIRTY
        ldy     #UW_TYPE                ; a button's, a check box's, an item's value is 0 or 1
        lda     (UIR),y
        ldy     t2
        cpy     #UW_VALUE
        bne     @put
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
        clc
        rts

ui_redraw:
        jsr     ALLDIRTY
        clc
        rts

ui_focus:
        jsr     ENTER
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
        lda     #1
        sta     ususp
        jsr     ENTER
        jsr     PTROFF
        clc
        rts

ui_resume:
        stz     ususp
        jsr     ENTER
        jsr     SETUP
        jsr     ALLDIRTY
        clc
        rts

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

        .data
uopen:  .byte   0                      ; 1: open (DATA, not BSS: a program's BSS isn't cleared)

; OOUI's menus (hw/ooui.md, "Widgets and API shape", 2): the menu bar, its
; menus' titles along it, a menu's drop-down box under its title on the
; overlay, and the keys and clicks while one is down.
;
; A menu bar's titles start a column in from its left, each " title " with
; the theme's gap (TM_MENUGAP) after it; a hidden menu has no place. A
; drop-down is a frame round its items, one a row, under its title (moved
; left to stay on the screen), wide enough for the widest item's text and,
; two columns on, its key's name.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .bss
mbar:   .res    1                      ; MWALK's: the bar,
mc0:    .res    1                      ;   its first title's column,
mwmode: .res    1                      ;   0: find mtarget; 1: the title at column mcol
mtarget: .res   1
mcx:    .res    1                      ;   the title's column
twid:   .res    1                      ;   and its width
mh:     .res    1                      ; a record being looked at
mm:     .res    1
mn:     .res    1
mdir:   .res    1
mwid:   .res    1                      ; MWIDTH's: the drop-down's width, its items
mcnt:   .res    1
mk:     .res    1
mst:    .res    1                      ; DMENU's
mrow:   .res    1
mfl:    .res    1
mval:   .res    1
mtp:    .res    2
mtl:    .res    1
mav:    .res    1
malt:   .res    1
mbh:    .res    1
kp:     .res    1                      ; KID's parent
kbuf:   .res    16                     ; KEYTEXT's: a key's name
klen:   .res    1
kus:    .res    1
kmo:    .res    1

        .code

; UIQ = record A. Keeps A, X and Y.
REQ:    pha
        stz     UIQ + 1
        asl     a
        rol     UIQ + 1
        asl     a
        rol     UIQ + 1
        asl     a
        rol     UIQ + 1
        asl     a
        rol     UIQ + 1
        clc
        adc     utab
        sta     UIQ
        lda     UIQ + 1
        adc     utab + 1
        sta     UIQ + 1
        pla
        rts

; The menus of bar mbar, from column mc0: carry clear with mh the one
; mwmode asks for (mtarget, or the one whose title holds column mcol), mcx
; its title's column and twid its width; carry set if none.
MWALK:  lda     mc0
        sta     mcx
        lda     mbar
        sta     mh
@l:     inc     mh
        lda     mh
        cmp     ucount
        bcs     @none
        jsr     ISMENU
        bcs     @l
        jsr     QTEXT
        jsr     DLEN
        clc
        adc     #2
        sta     twid
        lda     mwmode
        bne     @hit
        lda     mh
        cmp     mtarget
        beq     @yes
        bra     @on
@hit:   lda     mcol
        sec
        sbc     mcx
        cmp     twid
        bcc     @yes
@on:    lda     mcx
        clc
        adc     twid
        ldy     #TM_MENUGAP
        clc
        adc     (UIT),y
        sta     mcx
        bra     @l
@yes:   clc
        rts
@none:  sec
        rts

; Carry clear if record mh (UIQ then its) is a shown child of kp, and not
; an unused record: what a drop-down counts, draws and is clicked on (the
; review's O3: a hidden item has no row).
KID:    lda     mh
        jsr     REQ
        ldy     #UW_PARENT
        lda     (UIQ),y
        cmp     kp
        bne     @no
        ldy     #UW_TYPE
        lda     (UIQ),y
        beq     @no
        ldy     #UW_FLAGS
        lda     (UIQ),y
        and     #UF_HIDDEN
        bne     @no
        clc
        rts
@no:    sec
        rts

; UIP = the text of the record at UIQ.
QTEXT:  ldy     #UW_TEXT
        lda     (UIQ),y
        sta     UIP
        iny
        lda     (UIQ),y
        sta     UIP + 1
        rts

; Carry clear if record mh (UIQ then its) is a shown menu of bar mbar.
ISMENU: lda     mh
        jsr     REQ
        ldy     #UW_PARENT
        lda     (UIQ),y
        cmp     mbar
        bne     @no
        ldy     #UW_TYPE
        lda     (UIQ),y
        cmp     #UT_MENU
        bne     @no
        ldy     #UW_FLAGS
        lda     (UIQ),y
        and     #UF_HIDDEN
        bne     @no
        clc
        rts
@no:    sec
        rts

; The item at UIQ's key's name in kbuf (Ctrl+, Shift+, Alt+, then a letter,
; a digit or F1-F12): A and klen its length, 0 for none.
KEYTEXT:
        ldx     #0
        ldy     #UW_A
        lda     (UIQ),y
        beq     @end
        sta     kus
        iny
        lda     (UIQ),y
        sta     kmo
        and     #UK_CTRL
        beq     @s
        ldy     #KT_CTRL - KTXT
        jsr     KCAT
@s:     lda     kmo
        and     #UK_SHIFT
        beq     @a
        ldy     #KT_SHIFT - KTXT
        jsr     KCAT
@a:     lda     kmo
        and     #UK_ALT
        beq     @k
        ldy     #KT_ALT - KTXT
        jsr     KCAT
@k:     lda     kus
        cmp     #$04                    ; A-Z
        bcc     @none
        cmp     #$1E
        bcs     @dig
        clc
        adc     #'A' - $04
        bra     @one
@dig:   cmp     #$27                    ; 1-9, 0
        bcc     @d19
        bne     @f
        lda     #'0'
        bra     @one
@d19:   clc
        adc     #'1' - $1E
        bra     @one
@f:     cmp     #$3A                    ; F1-F12
        bcc     @none
        cmp     #$46
        bcs     @none
        lda     #'F'
        jsr     KPUT
        lda     kus
        sec
        sbc     #$3A - 1
        cmp     #10
        bcc     @f9
        pha
        lda     #'1'
        jsr     KPUT
        pla
        sec
        sbc     #10
@f9:    clc
        adc     #'0'
@one:   jsr     KPUT
        bra     @end
@none:  ldx     #0                      ; a key with no name: none shown
@end:   stz     kbuf,x
        stx     klen
        txa
        rts
KPUT:   sta     kbuf,x
        inx
        rts
KCAT:   lda     KTXT,y
        beq     @r
        jsr     KPUT
        iny
        bra     KCAT
@r:     rts

; Menu A's drop-down: mwid its width, mcnt its rows.
MWIDTH: sta     kp
        sta     mh
        stz     mwid
        stz     mcnt
@l:     inc     mh
        lda     mh
        cmp     ucount
        bcs     @end
        jsr     KID
        bcs     @l
        inc     mcnt
        ldy     #UW_TYPE
        lda     (UIQ),y
        cmp     #UT_ITEM
        bne     @l
        jsr     KEYTEXT
        beq     @nk
        clc
        adc     #2
@nk:    sta     mk
        jsr     QTEXT
        jsr     DLEN
        clc
        adc     mk
        cmp     mwid
        bcc     @l
        sta     mwid
        bra     @l
@end:   lda     mwid
        clc
        adc     #4
        sta     mwid
        rts

; PLACE's for a menu, an item or a line (UIR the record, rcur its handle,
; UI_RECT and ucli its parent's). A menu's place is its drop-down's, shown
; only while it is down; an item's its row in it. All on the overlay.
MRECT:  lda     #1
        sta     rover
        lda     rtype
        cmp     #UT_MENU
        bne     @item
        ldy     #UW_STATE
        lda     (UIR),y
        and     #US_OPEN
        bne     @open
        lda     #1
        sta     rhid
@open:  ldy     #UW_PARENT
        lda     (UIR),y
        sta     mbar
        lda     rcur
        sta     mtarget
        stz     mwmode
        lda     UI_RECT
        inc     a
        sta     mc0
        lda     UI_RECT + 1
        inc     a
        sta     mrow
        jsr     MWALK
        bcc     @at
        lda     #1                      ; hidden: no place on the bar
        sta     rhid
@at:    lda     rcur
        jsr     MWIDTH
        lda     mcx                     ; on the screen: moved left if it would run off
        clc
        adc     mwid
        cmp     #UI_COLS + 1
        bcc     @fit
        lda     #UI_COLS
        sec
        sbc     mwid
        sta     mcx
@fit:   lda     mcx
        sta     UI_RECT
        inc     a
        sta     ucli
        lda     mrow
        sta     UI_RECT + 1
        inc     a
        sta     ucli + 1
        lda     mwid
        sta     UI_RECT + 2
        dec     a
        dec     a
        sta     ucli + 2
        lda     mcnt
        sta     ucli + 3
        inc     a
        inc     a
        sta     UI_RECT + 3
        rts
@item:  ldy     #UW_PARENT              ; its row: the menu's shown children before it
        lda     (UIR),y
        sta     kp
        sta     mh
        stz     mk
@l:     inc     mh
        lda     mh
        cmp     rcur
        bcs     @row
        jsr     KID
        bcs     @l
        inc     mk
        bra     @l
@row:   lda     ucli
        sta     UI_RECT
        lda     ucli + 1
        clc
        adc     mk
        sta     UI_RECT + 1
        sta     ucli + 1
        lda     ucli + 2
        sta     UI_RECT + 2
        lda     #1
        sta     UI_RECT + 3
        sta     ucli + 3
        rts

; The menu bar dh (UIR, at UI_RECT): its titles, the menu down's lit.
DMBAR:  ldy     #TC_MENUBAR
        jsr     TATTR
        lda     #' '
        jsr     FILLR
        lda     dh
        sta     mbar
        sta     mh
        lda     UI_RECT
        inc     a
        sta     mcx
@l:     inc     mh
        lda     mh
        cmp     ucount
        bcs     @r
        jsr     ISMENU
        bcs     @l
        ldy     #UW_FLAGS
        lda     (UIQ),y
        sta     mfl
        ldy     #UW_TEXT
        lda     (UIQ),y
        sta     mtp
        iny
        lda     (UIQ),y
        sta     mtp + 1
        ldy     #TC_MENUSEL
        lda     mh
        cmp     umenu
        beq     @a
        ldy     #TC_DISABLED
        lda     mfl
        and     #UF_DISABLED
        bne     @dis
        ldy     #TC_MENUBAR
@a:     jsr     TATTR
        bra     @t
@dis:   jsr     NOACC
@t:     lda     mtp
        sta     UIP
        sta     UIQ
        lda     mtp + 1
        sta     UIP + 1
        sta     UIQ + 1
        jsr     DLEN
        sta     twid
        lda     mcx
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #' '
        jsr     EMIT
        ldx     twid
        jsr     PUTS
        lda     #' '
        jsr     EMIT
        lda     mcx
        sec                             ; + its width and 2, and the gap
        adc     twid
        inc     a
        ldy     #TM_MENUGAP
        clc
        adc     (UIT),y
        sta     mcx
        bra     @l
@r:     rts

; The menu dh's drop-down (UIR, at UI_RECT): its frame, its items and
; lines, the chosen item lit, a check mark, each key's name at the right.
DMENU:  ldy     #TH_FRAMES + 3
        lda     (UIT),y
        sta     mst
        ldy     #TC_MENU
        lda     (UIT),y
        pha
        jsr     TATTR
        plx
        lda     mst
        jsr     BOXD
        lda     UI_RECT + 1
        sta     mrow
        lda     dh
        sta     mh
        sta     kp
@l:     inc     mh
        lda     mh
        cmp     ucount
        bcs     @sh
        jsr     KID
        bcs     @l
        inc     mrow
        ldy     #UW_TYPE
        lda     (UIQ),y
        cmp     #UT_SEP
        bne     @item
        ldy     #TC_MENU                ; a line across, joined to the frame
        jsr     TATTR
        lda     UI_RECT
        ldx     mrow
        jsr     GOTO
        lda     mst
        asl     a
        tax
        lda     SEPS,x
        jsr     EMIT
        lda     #$C4
        ldx     UI_RECT + 2
        dex
        dex
        jsr     REPT
        lda     mst
        asl     a
        tax
        lda     SEPS + 1,x
        jsr     EMIT
        bra     @l
@sh:    jmp     SHADOW
@item:  ldy     #UW_FLAGS
        lda     (UIQ),y
        sta     mfl
        ldy     #UW_VALUE
        lda     (UIQ),y
        sta     mval
        ldy     #UW_TEXT
        lda     (UIQ),y
        sta     mtp
        iny
        lda     (UIQ),y
        sta     mtp + 1
        jsr     KEYTEXT
        ldy     #TC_MENUSEL
        lda     mh
        cmp     umsel
        beq     @a
        ldy     #TC_DISABLED
        lda     mfl
        and     #UF_DISABLED
        bne     @dis
        ldy     #TC_MENU
@a:     jsr     TATTR
        bra     @t
@dis:   jsr     NOACC
@t:     lda     UI_RECT + 2             ; the text's room, and its length (before GOTO: UIP)
        sec
        sbc     #4
        sta     mav
        lda     mtp
        sta     UIP
        sta     UIQ
        lda     mtp + 1
        sta     UIP + 1
        sta     UIQ + 1
        jsr     DLEN
        cmp     mav
        bcc     @fit
        lda     mav
@fit:   sta     mtl
        lda     UI_RECT
        inc     a
        ldx     mrow
        jsr     GOTO
        lda     #' '
        ldx     mval
        beq     @nc
        lda     #$FB                    ; a check mark
@nc:    jsr     EMIT
        ldx     mtl
        jsr     PUTS
        lda     mav
        sec
        sbc     mtl
        sec
        sbc     klen
        bcs     @pad
        lda     #0
@pad:   tax
        lda     #' '
        jsr     REPT
        lda     #<kbuf
        sta     UIQ
        lda     #>kbuf
        sta     UIQ + 1
        ldx     klen
        jsr     PUTS
        lda     #' '
        jsr     EMIT
        jmp     @l

; ---------------------------------------------------------------------------
; Down and up.

; Menu A down (another up first), its first item chosen.
MOPEN:  cmp     umenu
        beq     @r
        jsr     REC                     ; a disabled menu stays up (the review's O4)
        tax
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED
        bne     @r
        txa
        pha
        jsr     MCLOSE
        pla
        sta     umenu
        jsr     REC
        ldy     #UW_STATE
        lda     (UIR),y
        ora     #US_OPEN
        sta     (UIR),y
        lda     umenu
        jsr     DIRTY
        jsr     MFIRST
        sta     umsel
@r:     rts

; The menu down up again: what it covered drawn again, its title too.
MCLOSE: lda     umenu
        cmp     #UI_NONE
        beq     @r
        jsr     RECT
        bcs     @h
        lda     umenu
        jsr     REC
        jsr     EXTRECT
        jsr     LBOX
        jsr     BOXADD
@h:     lda     umenu
        jsr     REC
        ldy     #UW_STATE
        lda     (UIR),y
        and     #<~US_OPEN
        sta     (UIR),y
        ldy     #UW_PARENT
        lda     (UIR),y
        jsr     DIRTY
        lda     #UI_NONE
        sta     umenu
        sta     umsel
@r:     rts

; Carry clear if record mh is an item of the menu down that can be chosen.
ISSEL:  lda     mh
        jsr     REQ
        ldy     #UW_PARENT
        lda     (UIQ),y
        cmp     umenu
        bne     @no
        ldy     #UW_TYPE
        lda     (UIQ),y
        cmp     #UT_ITEM
        bne     @no
        ldy     #UW_FLAGS
        lda     (UIQ),y
        and     #UF_DISABLED | UF_HIDDEN
        bne     @no
        clc
        rts
@no:    sec
        rts

; A the menu down's first item that can be chosen, or UI_NONE.
MFIRST: lda     umenu
        sta     mh
@l:     inc     mh
        lda     mh
        cmp     ucount
        bcs     @n
        jsr     ISSEL
        bcs     @l
        lda     mh
        rts
@n:     lda     #UI_NONE
        rts

; The next item to choose, A the way (1 down, $FF up), round the menu.
MNEXT:  sta     mdir
        lda     umsel
        cmp     #UI_NONE
        bne     @s
        jsr     MFIRST
        sta     umsel
        bra     @d
@s:     sta     mh
        lda     ucount
        sta     mn
@l:     lda     mh
        clc
        adc     mdir
        cmp     ucount
        bcc     @in
        ldx     mdir
        bmi     @top
        lda     umenu
        inc     a
        bra     @in
@top:   lda     ucount
        dec     a
@in:    sta     mh
        cmp     umenu
        bne     @t
        lda     ucount
        dec     a
        sta     mh
@t:     jsr     ISSEL
        bcc     @got
        dec     mn
        bne     @l
        rts
@got:   lda     mh
        sta     umsel
@d:     lda     umenu
        jmp     DIRTY

; Item A chosen: the menu up, and UE_MENU (A) with X the item, Y its menu,
; UV_VALUE its value; UE_NONE (A 0) for none or one disabled.
MCHOOSE:
        sta     mh
        cmp     ucount
        bcs     @none
        ldx     umodal                  ; not under a modal window (the review's O5)
        jsr     INSIDE
        bcs     @none
        lda     mh
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_ITEM
        bne     @none
        ldy     #UW_FLAGS               ; an item hidden or disabled,
        lda     (UIR),y
        and     #UF_DISABLED | UF_HIDDEN
        bne     @none
        ldy     #UW_PARENT              ;   or in a menu hidden or disabled (O4): none
        lda     (UIR),y
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED | UF_HIDDEN
        bne     @none
        ldy     #UW_PARENT              ;   or on a bar that doesn't show (round 2's W10)
        lda     (UIR),y
        jsr     RECT
        bcs     @none
        lda     mh
        jsr     REC
        ldy     #UW_VALUE
        lda     (UIR),y
        sta     evv
        iny
        lda     (UIR),y
        sta     evv + 1
        ldy     #UW_PARENT
        lda     (UIR),y
        sta     evy
        lda     mh                      ; (MCLOSE's RECT moves mh on)
        sta     evh
        jsr     MCLOSE
        lda     #UE_MENU
        rts
@none:  lda     #UE_NONE
        rts

; A key while a menu is down (ku, km, kc): A an event, or UE_NONE.
MKEY:   lda     ku
        cmp     #K_ESC
        bne     @lr
        jsr     MCLOSE
        bra     @none
@lr:    cmp     #K_LEFT
        bne     @r
        lda     #$FF
        bra     @sib
@r:     cmp     #K_RIGHT
        bne     @ud
        lda     #1
@sib:   jsr     MSIB
        jsr     MOPEN
        bra     @none
@ud:    cmp     #K_UP
        bne     @dn
        lda     #$FF
        jsr     MNEXT
        bra     @none
@dn:    cmp     #K_DOWN
        bne     @ent
        lda     #1
        jsr     MNEXT
        bra     @none
@ent:   cmp     #K_ENTER
        beq     @ch
        cmp     #K_SPACE
        bne     @let
@ch:    lda     umsel
        jmp     MCHOOSE
@let:   lda     kc                      ; an item's key letter
        beq     @none
        jsr     UPPER
        sta     malt
        lda     umenu
        sta     mh
@il:    inc     mh
        lda     mh
        cmp     ucount
        bcs     @none
        jsr     ISSEL
        bcs     @il
        jsr     QTEXT
        jsr     ACCEL
        cmp     malt
        bne     @il
        lda     mh
        jmp     MCHOOSE
@none:  lda     #UE_NONE
        rts

; A the menu next to the one down on its bar, A the way, round the bar.
MSIB:   sta     mdir
        lda     umenu
        jsr     REC
        ldy     #UW_PARENT
        lda     (UIR),y
        sta     mbar
        lda     umenu
        sta     mm
        lda     ucount
        sta     mn
@l:     lda     mm
        clc
        adc     mdir
        cmp     ucount
        bcc     @in
        ldx     mdir
        bmi     @top
        lda     #0
        bra     @in
@top:   lda     ucount
        dec     a
@in:    sta     mm
        sta     mh
        jsr     ISMENU
        bcs     @skip
        ldy     #UW_FLAGS               ; a disabled one passed by (O4)
        lda     (UIQ),y
        and     #UF_DISABLED
        beq     @got
@skip:
        dec     mn
        bne     @l
        lda     umenu
        rts
@got:   lda     mm
        rts

; The next menu bar down from mbh that shows and isn't under a modal window:
; carry clear, mbar it, UI_RECT its place; carry set if none.
NEXTBAR:
@b:     dec     mbh
        lda     mbh
        cmp     #UI_NONE
        beq     @no
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_MENUBAR
        bne     @b
        lda     mbh
        jsr     RECT
        bcs     @b
        lda     mbh
        ldx     umodal
        jsr     INSIDE
        bcs     @b
        lda     mbh
        sta     mbar
        rts
@no:    sec
        rts

; A the menu whose title is at column mcol, row mrowp, on a menu bar that
; shows and isn't under a modal window; UI_NONE none.
MBARAT: lda     #1
        sta     mwmode
        lda     ucount
        sta     mbh
@b:     jsr     NEXTBAR
        bcs     @none
        lda     UI_RECT + 1
        cmp     mrowp
        bne     @b
        lda     UI_RECT
        inc     a
        sta     mc0
        jsr     MWALK
        bcs     @b
        lda     mh
        rts
@none:  lda     #UI_NONE
        rts

; Alt and a letter (A; $FF: F10, the first menu): the menu with that key
; down, on a bar that shows and isn't under a modal window. Carry set if
; none.
MALT:   sta     malt
        lda     ucount
        sta     mbh
@b:     jsr     NEXTBAR
        bcs     @no
        lda     mbar
        sta     mh
@m:     inc     mh
        lda     mh
        cmp     ucount
        bcs     @b
        jsr     ISMENU
        bcs     @m
        ldy     #UW_FLAGS
        lda     (UIQ),y
        and     #UF_DISABLED
        bne     @m
        lda     malt
        cmp     #$FF
        beq     @open
        jsr     QTEXT
        jsr     ACCEL
        cmp     malt
        bne     @m
@open:  lda     mh
        jsr     MOPEN
        clc
@no:    rts

; A click (mcol, mrowp) while a menu is down: an item chosen (A UE_MENU), or
; another menu down, or the menu up (A UE_NONE).
MCLICK: lda     umenu
        jsr     RECT
        bcs     @bar
        lda     mcol
        sec
        sbc     UI_RECT
        cmp     UI_RECT + 2
        bcs     @bar
        lda     mrowp
        sec
        sbc     UI_RECT + 1
        cmp     UI_RECT + 3
        bcs     @bar
        dec     a                       ; the row in the box: 0 the frame
        cmp     mcnt
        bcs     @none
        sta     mk
        lda     umenu
        sta     mh
        sta     kp
@l:     inc     mh                      ; its mk'th shown child
        lda     mh
        cmp     ucount
        bcs     @none
        jsr     KID
        bcs     @l
        lda     mk
        beq     @this
        dec     mk
        bra     @l
@this:  jsr     ISSEL
        bcs     @none
        lda     mh
        jmp     MCHOOSE
@bar:   jsr     MBARAT
        cmp     #UI_NONE
        beq     @up
        cmp     umenu
        beq     @up
        jsr     MOPEN
        bra     @none
@up:    jsr     MCLOSE
@none:  lda     #UE_NONE
        rts

        .rodata
KTXT:
KT_CTRL:  .asciiz "Ctrl+"
KT_SHIFT: .asciiz "Shift+"
KT_ALT:   .asciiz "Alt+"
; A line's ends in a menu, a style's two.
SEPS:   .byte   ' ', ' ', $C3, $B4, $C7, $B6, $DB, $DB

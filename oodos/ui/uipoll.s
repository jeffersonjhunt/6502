; OOUI's ui_poll (hw/ooui.md, "Events"): the event area's queue read, each
; key and click routed to the widget it is for, one event returned a call,
; and what changed drawn before UE_NONE.
;
; While OOUI is open it is the queue's only reader (it moves EV_QTAIL on).
; A key goes to the menu that is down; then to the focus (Space and Enter
; press a button or a check box); then Tab and Shift+Tab move the focus,
; Alt and a letter (or F10) put a menu down, Enter presses the default
; button and Escape the cancel button (or closes the window); a letter
; alone presses the button whose key it is in the focus's window; then the
; accelerator table; else UE_KEY. The left button presses a button where it
; goes down and clicks it if it comes up there; on a menu bar's title it
; puts the menu down; on a window's close box it closes it. Key releases
; are taken and dropped; what else OOUI doesn't use is UE_RAW.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .bss
qev:    .res    EQ_LEN                 ; the event taken from the queue
qt:     .res    1
evc:    .res    1                      ; the event being returned: its code,
evh:    .res    1                      ;   its widget,
evy:    .res    1                      ;   its Y,
evv:    .res    2                      ;   UV_VALUE,
evf:    .res    1                      ;   UV_FLAGS
ku:     .res    1                      ; a key: its usage,
km:     .res    1                      ;   the modifiers,
kc:     .res    1                      ;   its character
mcol:   .res    1                      ; the pointer's cell
mrowp:  .res    1
hh:     .res    1                      ; HIT's
hp:     .res    1
sc:     .res    1                      ; the scope: the modal window or the focus's, UI_NONE none
fb:     .res    1                      ; FINDBTN's
fh:     .res    1
um:     .res    1                      ; UCHAR's

        .code

ui_poll:
        jsr     ENTER
@next:  lda     uarea
        sta     UIP
        lda     uarea + 1
        sta     UIP + 1
        ldy     #EV_QTAIL
        lda     (UIP),y
        sta     qt
        ldy     #EV_QHEAD
        lda     (UIP),y
        cmp     qt
        bne     @take
        jsr     PAINT
        stz     evv
        stz     evv + 1
        stz     evf
        stz     qev + EQ_MODS
        stz     qev + EQ_CODE
        stz     kc
        lda     #UE_NONE
        ldx     #UI_NONE
        ldy     #0
        jmp     EVOUT
@take:  lda     qt                      ; the slot: EV_QUEUE + tail x 8
        asl     a
        asl     a
        asl     a
        clc
        adc     #EV_QUEUE
        tay
        ldx     #0
@c:     lda     (UIP),y
        sta     qev,x
        iny
        inx
        cpx     #EQ_LEN
        bne     @c
        lda     qt                      ; the tail on, round the slots
        inc     a
        ldy     #EV_QSLOTS
        cmp     (UIP),y
        bcc     @t
        lda     #0
@t:     ldy     #EV_QTAIL
        sta     (UIP),y
        stz     evv
        stz     evv + 1
        stz     evf
        stz     evy
        stz     kc
        lda     #UI_NONE
        sta     evh
        lda     ususp
        bne     @raw
        jsr     FINDMODAL
        lda     qev + EQ_KIND
        cmp     #EK_KEY
        beq     @key
        cmp     #EK_BUTTON
        beq     @btn
        cmp     #EK_WHEEL
        beq     @whl
@raw:   ldx     #EQ_LEN - 1
@rc:    lda     qev,x
        sta     UI_RAW,x
        dex
        bpl     @rc
        lda     qev + EQ_KIND
        sta     evy
        lda     #UE_RAW
        bra     @out
@key:   lda     qev + EQ_VALUE
        beq     @again                  ; released
        jsr     KEYDOWN
        bra     @res
@btn:   lda     qev + EQ_CODE
        bne     @again                  ; the left button's only
        jsr     CELL
        lda     qev + EQ_VALUE
        beq     @up
        jsr     MDOWN
        bra     @res
@up:    jsr     MUP
        bra     @res
@whl:   jsr     CELL
        jsr     HIT
        sta     evh
        lda     qev + EQ_VALUE          ; the steps, signed
        sta     evv
        bpl     @w
        lda     #$FF
        sta     evv + 1
@w:     lda     #UE_WHEEL
@res:   cmp     #UE_NONE
        bne     @out
@again: jmp     @next
@out:   ldx     evh
        ldy     evy
; The event A (X its widget, Y its Y) returned, and in UI_EV.
EVOUT:  sta     UI_EV + UV_CODE
        stx     UI_EV + UV_HANDLE
        pha
        lda     evv
        sta     UI_EV + UV_VALUE
        lda     evv + 1
        sta     UI_EV + UV_VALUE + 1
        stz     UI_EV + UV_X
        stz     UI_EV + UV_X + 1
        stz     UI_EV + UV_Y
        stz     UI_EV + UV_KIND
        lda     qev + EQ_MODS
        sta     UI_EV + UV_MODS
        lda     qev + EQ_CODE
        sta     UI_EV + UV_USAGE
        lda     kc
        sta     UI_EV + UV_CHAR
        lda     evf
        sta     UI_EV + UV_FLAGS
        pla
        cld
        clc
        rts

; ---------------------------------------------------------------------------
; Keys.

KEYDOWN:
        lda     qev + EQ_CODE
        sta     ku
        cmp     #$E0                    ; a modifier alone: taken, nothing
        bcc     @usage
        lda     #UE_NONE
        rts
@usage:
        lda     qev + EQ_MODS
        sta     km
        ldx     km
        lda     ku
        jsr     UCHAR
        sta     kc
        lda     #UV_KEYED
        sta     evf
        lda     umenu
        cmp     #UI_NONE
        beq     @nomenu
        jmp     MKEY
@nomenu:
        jsr     SCOPE
        lda     ku                      ; Space and Enter: the focus's
        cmp     #K_SPACE
        beq     @act
        cmp     #K_ENTER
        bne     @tab
@act:   lda     ufocus
        jsr     CANFOCUS
        bcs     @def
        lda     ufocus
        jmp     PRESS
@def:   lda     ku                      ; Enter: the default button
        cmp     #K_ENTER
        bne     @tab
        lda     #UF_DEFAULT
        jsr     FINDBTN
        bcs     @tab
        jmp     PRESS
@tab:   lda     ku
        cmp     #K_TAB
        bne     @alt
        lda     #1
        ldx     km
        cpx     #0
        beq     @dir
        txa
        and     #UK_SHIFT
        beq     @dir1
        lda     #$FF
        bra     @dir
@dir1:  lda     #1
@dir:   jsr     NEXTFOCUS
        bcs     @none2
        jsr     SETFOCUS
        lda     ufocus
        sta     evh
        lda     #UE_FOCUS
        rts
@none2: lda     #UE_NONE
        rts
@alt:   cmp     #K_F10                  ; F10: the first menu
        bne     @alt2
        lda     #$FF
        jsr     MALT
        bcc     @none
        bra     @esc
@alt2:  lda     km
        and     #UK_ALT
        beq     @esc
        lda     kc
        beq     @esc
        jsr     UPPER
        jsr     MALT
        bcc     @none
@esc:   lda     ku
        cmp     #K_ESC
        bne     @letter
        lda     #UF_CANCEL
        jsr     FINDBTN
        bcs     @close
        jmp     PRESS
@close: lda     sc                      ; the scope's window, with a close box
        cmp     #UI_NONE
        beq     @table
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_CLOSE
        beq     @table
        lda     sc
        sta     evh
        lda     #UE_CLOSE
        rts
@letter:
        lda     km                      ; a letter alone: a button's key in the scope
        and     #UK_CTRL | UK_ALT
        bne     @table
        lda     kc
        beq     @table
        jsr     UPPER
        jsr     FINDACC
        bcs     @table
        pha
        jsr     SETFOCUS
        pla
        jmp     PRESS
@table: jsr     KEYTAB
        bcc     @r
        lda     ufocus                  ; nothing took it
        sta     evh
        lda     ku
        sta     evy
        lda     #UE_KEY
@r:     rts
@none:  lda     #UE_NONE
        rts

; sc = the modal window, or the focus's window, or UI_NONE.
SCOPE:  lda     umodal
        cmp     #UI_NONE
        bne     @s
        lda     ufocus
        jsr     FWIN
@s:     sta     sc
        rts

; A a flag (UF_DEFAULT, UF_CANCEL): carry clear and A the first button in
; the scope with it that shows and is enabled.
FINDBTN:
        sta     fb
        stz     fh
@l:     lda     fh
        cmp     ucount
        bcs     @no
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_BUTTON
        bne     @n
        ldy     #UW_FLAGS
        lda     (UIR),y
        bit     #UF_DISABLED
        bne     @n
        and     fb
        beq     @n
        jsr     INSCOPE
        bcc     @yes
@n:     inc     fh
        bra     @l
@yes:   lda     fh
        rts
@no:    sec
        rts

; Carry clear if record fh shows and is in the scope (and under no modal
; window).
INSCOPE:
        lda     fh
        jsr     RECT
        bcs     @r
        lda     fh
        ldx     umodal
        jsr     INSIDE
        bcs     @r
        lda     fh
        ldx     sc
        jsr     INSIDE
@r:     rts

; A a letter: carry clear and A the button or check box in the scope whose
; key it is, that can take the focus.
FINDACC:
        sta     fb
        stz     fh
@l:     lda     fh
        cmp     ucount
        bcs     @no
        jsr     CANFOCUS
        bcs     @n
        jsr     INSCOPE
        bcs     @n
        lda     fh
        jsr     REC
        ldy     #UW_TEXT
        lda     (UIR),y
        sta     UIP
        iny
        lda     (UIR),y
        sta     UIP + 1
        jsr     ACCEL
        cmp     fb
        bne     @n
        lda     fh
        clc
        rts
@n:     inc     fh
        bra     @l
@no:    sec
        rts

; Widget A pressed: a button's UE_CLICK (a toggle flipped), a check box's
; UE_CHANGE, an item's UE_MENU; UV_VALUE the value after.
PRESS:  sta     evh
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_ITEM
        bne     @btn
        lda     evh
        jmp     MCHOOSE
@btn:   ldx     #UE_CLICK
        cmp     #UT_BUTTON
        beq     @b
        cmp     #UT_CHECK
        bne     @none
        ldx     #UE_CHANGE
        bra     @flip
@b:     ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_TOGGLE
        beq     @val
@flip:  ldy     #UW_VALUE
        lda     (UIR),y
        eor     #1
        and     #1
        sta     (UIR),y
@val:   ldy     #UW_VALUE
        lda     (UIR),y
        sta     evv
        iny
        lda     (UIR),y
        sta     evv + 1
        phx
        lda     evh
        jsr     DIRTY
        pla
        rts
@none:  lda     #UE_NONE
        rts

; The accelerator table (UO_KEYS): carry clear and A an event (or UE_NONE)
; if an entry has the key ku with the modifiers km; carry set if none.
KEYTAB: lda     ukeys
        sta     UIQ
        lda     ukeys + 1
        sta     UIQ + 1
        ora     UIQ
        bne     @some
        sec
        rts
@some:
        lda     km
        jsr     MODS
        sta     fb
@e:     ldy     #UK_USAGE
        lda     (UIQ),y
        beq     @no
        cmp     ku
        bne     @n
        ldy     #UK_MODS
        lda     (UIQ),y
        jsr     MODS
        cmp     fb
        bne     @n
        ldy     #UK_HANDLE
        lda     (UIQ),y
        sta     fh
        cmp     ucount
        bcs     @took
        ldy     #UK_ACTION
        lda     (UIQ),y
        cmp     #UA_FOCUS
        beq     @focus
        cmp     #UA_PRESS
        bne     @took                   ; UA_UP, UA_DOWN: U2's widgets
        lda     fh                      ; a menu's item, whether its menu is down or not;
        jsr     REC                     ; a button or a check box that shows and is enabled
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_ITEM
        beq     @press
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED
        bne     @took
        lda     fh
        jsr     RECT
        bcs     @took
@press: lda     fh
        jsr     PRESS
        clc
        rts
@focus: lda     fh
        jsr     CANFOCUS
        bcs     @took
        jsr     SETFOCUS
        lda     fh
        sta     evh
        lda     #UE_FOCUS
        clc
        rts
@took:  lda     #UE_NONE
        clc
        rts
@n:     clc
        lda     UIQ
        adc     #UK_LEN
        sta     UIQ
        bcc     @e
        inc     UIQ + 1
        bra     @e
@no:    sec
        rts

; Modifiers A as Ctrl, Shift, Alt (bits 0-2), either side's.
MODS:   sta     um
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        ora     um
        and     #7
        rts

; A a key's usage, X the modifiers: A its character on a US keyboard, 0
; none (Ctrl and a letter its control character).
UCHAR:  stx     um
        cmp     #$04
        bcc     @z
        cmp     #$1E
        bcs     @tab
        clc                             ; a letter
        adc     #'a' - $04
        tax
        lda     um
        and     #UK_CTRL
        bne     @ctl
        lda     um
        and     #UK_SHIFT
        beq     @lo
        txa
        and     #$DF
        rts
@lo:    txa
        rts
@ctl:   txa
        and     #$1F
        rts
@tab:   cmp     #$39
        bcs     @z
        sec
        sbc     #$1E
        asl     a
        tax
        lda     um
        and     #UK_SHIFT
        beq     @u
        inx
@u:     lda     UCTAB,x
        rts
@z:     lda     #0
        rts

; ---------------------------------------------------------------------------
; The mouse.

; mcol and mrowp: the pointer's cell (EV_MX / 4, EV_MY / 8).
CELL:   lda     uarea
        sta     UIP
        lda     uarea + 1
        sta     UIP + 1
        ldy     #EV_MX
        lda     (UIP),y
        sta     mcol
        iny
        lda     (UIP),y
        lsr     a
        ror     mcol
        lsr     a
        ror     mcol
        ldy     #EV_MY
        lda     (UIP),y
        sta     mrowp
        iny
        lda     (UIP),y
        lsr     a
        ror     mrowp
        lsr     a
        ror     mrowp
        lsr     a
        ror     mrowp
        rts

; A the widget at the pointer's cell: the overlay's first, then the base's,
; each from the table's end down (the frontmost), UI_NONE none, or one
; under a modal window.
HIT:    lda     #1
        sta     hp
@pass:  lda     ucount
        sta     hh
@l:     dec     hh
        lda     hh
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
@p:     lda     hh
        jsr     RECT
        bcs     @l
        lda     rover
        cmp     hp
        bne     @l
        lda     mcol
        sec
        sbc     UI_RECT
        cmp     UI_RECT + 2
        bcs     @l
        lda     mrowp
        sec
        sbc     UI_RECT + 1
        cmp     UI_RECT + 3
        bcs     @l
        lda     hh
        ldx     umodal
        jsr     INSIDE
        bcs     @none
        lda     hh
        rts
@np:    dec     hp
        bpl     @pass
@none:  lda     #UI_NONE
        rts

; The left button down.
MDOWN:  lda     umenu
        cmp     #UI_NONE
        beq     @nm
        jmp     MCLICK
@nm:    jsr     MBARAT
        cmp     #UI_NONE
        beq     @hit
        jsr     MOPEN
        bra     @none
@hit:   jsr     HIT
        sta     evh
        cmp     #UI_NONE
        beq     @none
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_WINDOW
        beq     @win
        cmp     #UT_BUTTON
        beq     @btn
        cmp     #UT_CHECK
        bne     @none
@btn:   ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED
        bne     @none
        ldy     #UW_STATE
        lda     (UIR),y
        ora     #US_PRESSED
        sta     (UIR),y
        lda     evh
        sta     upress
        jsr     DIRTY
        jsr     CANFOCUS
        bcs     @none
        cmp     ufocus
        beq     @none
        jsr     SETFOCUS
        lda     #UE_FOCUS
        rts
@win:   ldy     #UW_FLAGS               ; its close box: "[x]" from its third column
        lda     (UIR),y
        bit     #UF_CLOSE
        beq     @none
        bit     #UF_NOFRAME
        bne     @none
        lda     evh
        jsr     RECT
        lda     mrowp
        cmp     UI_RECT + 1
        bne     @none
        lda     mcol
        sec
        sbc     UI_RECT
        sec
        sbc     #2
        cmp     #3
        bcs     @none
        lda     #UE_CLOSE
        rts
@none:  lda     #UE_NONE
        rts

; The left button up: the button it went down on clicked, if it is still
; over it.
MUP:    lda     upress
        cmp     #UI_NONE
        beq     @none
        jsr     REC
        ldy     #UW_STATE
        lda     (UIR),y
        and     #<~US_PRESSED
        sta     (UIR),y
        lda     upress
        jsr     DIRTY
        jsr     HIT
        ldx     upress
        ldy     #UI_NONE
        sty     upress
        stx     evh
        cmp     evh
        bne     @none
        lda     evh
        jmp     PRESS
@none:  lda     #UE_NONE
        rts

        .rodata
; Usages $1E-$38, each its character and its character with Shift.
UCTAB:  .byte   "1!2@3#4$5%6^7&8*9(0)"
        .byte   $0D, $0D, $1B, $1B, $08, $08, $09, $09, ' ', ' '
        .byte   "-_=+[{]}\|", 0, 0, ";:", "'", '"', "`~,<.>/?"
.assert * - UCTAB = 2 * ($39 - $1E), error, "UCTAB: two a usage"

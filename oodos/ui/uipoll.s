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
mpx:    .res    2                      ; the pointer from the widget's top left, in pointer units (U2)
mpy:    .res    2
evk:    .res    1                      ; UV_KIND, UV_X, UV_Y of the event (a canvas's)
evx:    .res    2
evyy:   .res    1
upend:  .res    1                      ; an event waiting (a field's last change, U2): its code, 0 none
upendh: .res    1
upendv: .res    2
lmx:    .res    2                      ; the pointer as the last poll saw it
lmy:    .res    2
moved:  .res    1                      ;   not 0: it has moved since
uhot:   .res    1                      ; the widget under it (US_HOT)
ulastp: .res    1                      ; the last press's widget, UI_NONE none or expired, and its frame
ulastt: .res    2                      ;   (16 bits: the double-click, X1)
lastp:  .res    1                      ; MDOWN's: ulastp as it was
urkey:  .res    1                      ; the key repeating, 0 none, and the frame it repeats at
urnext: .res    1
urfoc:  .res    1                      ;   the focus and the modal window it began with (X9)
urmod:  .res    1
umseen: .res    1                      ; 1: a modal window was up at the last UE_MODAL
umlast: .res    1
pw:     .res    1                      ; DOWNW's, HOTBIT's
wst:    .res    1                      ; WHEELW's: the steps
udf1:   .res    2                      ; the dialogs' event filters (the message box's, the picker's)
udf2:   .res    2
uidle:  .res    2                      ; a dialog's work at each idle poll (the picker's listing)

        .code

ui_poll:
        jsr     ENTER
@next:  stz     evv
        stz     evv + 1
        stz     evf
        stz     evy
        stz     evk
        stz     kc
        lda     #UI_NONE
        sta     evh
        lda     upend                   ; an event waiting first
        beq     @live
        stz     upend
        ldx     upendh
        stx     evh
        ldx     upendv
        stx     evv
        ldx     upendv + 1
        stx     evv + 1
        jmp     @out
@live:  lda     ususp
        bne     @queue
        jsr     DCLOCK                  ; a first click too long ago forgotten (X1)
        jsr     POINTED                 ; the pointer moved: a drag, the widget under it
        cmp     #UE_NONE
        beq     @queue
        jmp     @out
@queue: lda     uarea
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
        lda     ususp
        bne     @paint
        jsr     IDLE                    ; a dialog's work (the picker's listing, U2)
        cmp     #UE_NONE
        beq     @rep
        jmp     @out
@rep:   jsr     REPEAT                  ; a key held repeats (U2)
        bcs     @modal
        jmp     @key2
@modal: jsr     MODALEV                 ; the first modal window up, the last down (UE_MODAL)
        cmp     #UE_NONE
        beq     @paint
        jmp     @out
@paint: jsr     PAINT
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
        cmp     #EK_DEVICE
        bne     @raw
        jsr     PLUGGED                 ; a mouse plugged in: the pointer set up (U2, O13)
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
@key2:  jsr     FINDMODAL
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
        sta     dh
        jsr     WHEELW                  ; a list's or a scroll bar's (U2)
        bcc     @res
        lda     qev + EQ_VALUE          ; the steps, signed
        sta     evv
        bpl     @w
        lda     #$FF
        sta     evv + 1
@w:     lda     #UE_WHEEL
@res:   cmp     #UE_NONE
        bne     @out
@again: jmp     @next
@out:   jsr     DFILT                   ; the dialogs see it first (U2)
        cmp     #UE_NONE
        beq     @again
        ldx     evh
        ldy     evy
; The event A (X its widget, Y its Y) returned, and in UI_EV. A dialog's
; widget is given by its public handle.
EVOUT:  sta     UI_EV + UV_CODE
        pha
        txa
        jsr     PUBH
        tax
        stx     UI_EV + UV_HANDLE
        lda     evv
        sta     UI_EV + UV_VALUE
        lda     evv + 1
        sta     UI_EV + UV_VALUE + 1
        lda     evx
        sta     UI_EV + UV_X
        lda     evx + 1
        sta     UI_EV + UV_X + 1
        lda     evyy
        sta     UI_EV + UV_Y
        lda     evk
        sta     UI_EV + UV_KIND
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
        lda     #UI_NONE                ; a key between two clicks: not a double-click (X1)
        sta     ulastp
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
        lda     ufocus                  ; a widget module's focus takes its keys first (U2)
        cmp     #UI_NONE
        beq     @core
        sta     dh
        ldy     #WR_KEY
        jsr     WCALL
        bcs     @core
        jmp     TOOK
@core:  lda     ku                      ; Space and Enter: the focus's
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
        jsr     CANFOCUS                ; the focus there only if it takes it
        bcs     @nf
        jsr     SETFOCUS
@nf:    pla
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
        cmp     utot
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
; key it is, shown and enabled (one that can't take the focus too).
FINDACC:
        sta     fb
        stz     fh
@l:     lda     fh                      ; a button or a check box, enabled: focusable or not (O12)
        cmp     utot
        bcs     @no
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_BUTTON
        beq     @t
        cmp     #UT_CHECK
        beq     @t
        cmp     #UT_RADIO               ; (U2: a radio button's letter chooses it)
        bne     @n
@t:     ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED
        bne     @n
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
        bne     @mod
        lda     evh
        jmp     MCHOOSE
@mod:   cmp     #UT_GROUP               ; a widget module's: as Space to it (U2)
        bcc     @btn
@w:     lda     evh
        sta     dh
        lda     #K_SPACE
        sta     ku
        ldy     #WR_KEY
        jsr     WCALL
        bcc     @r
        lda     #UE_NONE
@r:     rts
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
        bne     @u
        sec
        rts
@u:     cmp     ku
        bne     @n
        ldy     #UK_MODS
        lda     (UIQ),y
        jsr     MODS
        cmp     fb
        bne     @n
        ldy     #UK_HANDLE
        lda     (UIQ),y
        sta     fh
        cmp     utot
        bcs     @took
        ldy     #UK_ACTION
        lda     (UIQ),y
        cmp     #UA_FOCUS
        beq     @focus
        cmp     #UA_PRESS
        beq     @press
        ldx     #K_UP                   ; UA_UP, UA_DOWN: as the arrow to the widget (U2)
        cmp     #UA_UP
        beq     @arrow
        ldx     #K_DOWN
        cmp     #UA_DOWN
        bne     @took
@arrow: stx     ku
        jsr     KPRESS
        bcs     @took
        lda     fh
        sta     dh
        ldy     #WR_KEY
        jsr     WCALL
        bcs     @took
        clc
        rts
@press: jsr     KPRESS
        bcs     @took
        lda     fh
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
        bcc     @j
        inc     UIQ + 1
@j:     jmp     @e
@no:    sec
        rts

; Carry clear if the table's record fh may be pressed: a menu's item,
; whether its menu is down or not (MCHOOSE checks it); a button or a check
; box that shows, is enabled, and isn't under a modal window (O5).
KPRESS: lda     fh
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_ITEM
        beq     @yes
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED
        bne     @no
        lda     fh
        jsr     RECT
        bcs     @r
        lda     fh
        ldx     umodal
        jmp     INSIDE
@yes:   clc
@r:     rts
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

; A the widget at the pointer's cell: FRONT's, UI_NONE none, or one under a
; modal window.
HIT:    jsr     FRONT
        cmp     #UI_NONE
        beq     @r
        ldx     umodal
        jsr     INSIDE
        lda     hh
        bcc     @r
        lda     #UI_NONE
@r:     rts

; A the record frontmost at cell mcol, mrowp: the overlay's first, then the
; base's, each from the table's end down, inside its parents' client areas
; (X5); UI_NONE none. Menus aren't looked at.
FRONT:  lda     #1
        sta     hp
@pass:  lda     utot
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
        lda     mcol                    ; and inside its parents' client areas (X5)
        cmp     rclip
        bcc     @l
        cmp     rclip + 2
        bcs     @l
        lda     mrowp
        cmp     rclip + 1
        bcc     @l
        cmp     rclip + 3
        bcs     @l
        lda     hh
        rts
@np:    dec     hp
        bpl     @pass
        lda     #UI_NONE
        rts

; The left button down.
MDOWN:  lda     ulastp                  ; only DOWNW keeps it (X1): a press anywhere else
        sta     lastp                   ;   ends a double-click
        lda     #UI_NONE
        sta     ulastp
        lda     umenu
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
        beq     @btn
        cmp     #UT_GROUP               ; the desktop, a panel, a label: nothing held, nothing
        bcc     @none                   ;   drawn again (X8)
        jmp     DOWNW                   ; a widget module's (U2)
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
; over it; a widget module's told (WR_UP).
MUP:    lda     upress
        cmp     #UI_NONE
        beq     @none
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_GROUP
        beq     @w
        cmp     #UT_SEP + 1
        bcc     @core
@w:     lda     upress
        sta     dh
        lda     #UI_NONE
        sta     upress
        jsr     MPOS
        ldy     #WR_UP
        jsr     WCALL
        bcc     @r
@none:  lda     #UE_NONE
@r:     rts
@core:  lda     upress
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
        bne     @no
        lda     evh
        jmp     PRESS
@no:    lda     #UE_NONE
        rts

; ---------------------------------------------------------------------------
; U2: the widget modules' mouse, the wheel, held keys, the modal event.

; The left button down on widget evh (A its type), a widget module's: it is
; held (upress), a second press soon after on it a double-click (UV_DOUBLE),
; the focus there if it takes it, and its WR_DOWN; UE_FOCUS if the focus
; moved and the widget said nothing.
DOWNW:  ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED
        bne     @none
        lda     evh
        sta     upress
        sta     dh
        lda     lastp                   ; the double-click: the same widget within TH_TIMES + 2
        cmp     evh
        bne     @first
        jsr     SINCE
        bcs     @first
        lda     #UV_DOUBLE              ; (ulastp UI_NONE: a third press is a first)
        tsb     evf
        bra     @focus
@first: lda     evh
        sta     ulastp
        jsr     FRAMEW
        sta     ulastt
        stx     ulastt + 1
@focus: stz     pw
        lda     evh
        jsr     CANFOCUS
        bcs     @call
        cmp     ufocus
        beq     @call
        jsr     SETFOCUS
        inc     pw
@call:  lda     evh
        sta     dh
        jsr     MPOS
        ldy     #WR_DOWN
        jsr     WCALL
        bcs     @nothing
        cmp     #UE_NONE
        bne     @r
@nothing:
        lda     pw
        beq     @none
        lda     evh
        sta     evh
        lda     #UE_FOCUS
@r:     rts
@none:  lda     #UE_NONE
        rts

; Carry clear if the last press (ulastt) is within the double-click's time
; (TH_TIMES + 2 frames).
SINCE:  jsr     FRAMEW
        sec
        sbc     ulastt
        sta     pw
        txa
        sbc     ulastt + 1
        bne     @no                     ; 256 frames or more
        lda     pw
        ldy     #TH_TIMES + 2
        cmp     (UIT),y
        rts
@no:    sec
        rts

; Each poll: a first press past the double-click's time forgotten, so a
; counter that comes round again can't make one (X1).
DCLOCK: lda     ulastp
        cmp     #UI_NONE
        beq     @r
        jsr     SINCE
        bcc     @r
        lda     #UI_NONE
        sta     ulastp
@r:     rts

; A/X the frame counter's low 16 bits (EV_FRAMES).
FRAMEW: jsr     FRAME
        iny
        lda     (UIP),y
        tax
        dey
        lda     (UIP),y
        rts

; A the frame counter's low byte (EV_FRAMES).
FRAME:  lda     uarea
        sta     UIP
        lda     uarea + 1
        sta     UIP + 1
        ldy     #EV_FRAMES
        lda     (UIP),y
        rts

; The pointer's cell (mcol, mrowp) and its place from widget dh's top left
; in pointer units (mpx, mpy). UI_RECT dh's place after.
MPOS:   jsr     CELL
        lda     dh
        jsr     RECT
        lda     uarea
        sta     UIP
        lda     uarea + 1
        sta     UIP + 1
        lda     UI_RECT                 ; mpx = EV_MX - column x 4
        stz     mpx + 1
        asl     a
        rol     mpx + 1
        asl     a
        rol     mpx + 1
        sta     mpx
        ldy     #EV_MX
        lda     (UIP),y
        sec
        sbc     mpx
        sta     mpx
        iny
        lda     (UIP),y
        sbc     mpx + 1
        sta     mpx + 1
        lda     UI_RECT + 1             ; mpy = EV_MY - row x 8
        stz     mpy + 1
        asl     a
        rol     mpy + 1
        asl     a
        rol     mpy + 1
        asl     a
        rol     mpy + 1
        sta     mpy
        ldy     #EV_MY
        lda     (UIP),y
        sec
        sbc     mpy
        sta     mpy
        iny
        lda     (UIP),y
        sbc     mpy + 1
        sta     mpy + 1
        rts

; The wheel over widget dh (UI_NONE none): its module's WR_WHEEL with wst
; the steps; carry set if it takes none.
WHEELW: lda     dh
        cmp     #UI_NONE
        beq     @no
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED
        bne     @no
        lda     qev + EQ_VALUE
        sta     wst
        jsr     MPOS                    ; (a canvas's UV_X and UV_Y)
        ldy     #WR_WHEEL
        jmp     WCALL
@no:    sec
        rts

; The pointer, if it moved since the last poll: US_HOT moved to the widget
; under it, and the widget held (upress) told (WR_MOVE). A its event, or
; UE_NONE.
POINTED:
        lda     uarea
        sta     UIP
        lda     uarea + 1
        sta     UIP + 1
        stz     moved
        ldx     #3
@cmp:   txa
        clc
        adc     #EV_MX
        tay
        lda     (UIP),y
        cmp     lmx,x
        beq     @same
        sta     lmx,x
        inc     moved
@same:  dex
        bpl     @cmp
.assert lmy = lmx + 2 && EV_MY = EV_MX + 2, error, "lmx and lmy follow EV_MX's order"
        lda     moved
        beq     @none
        jsr     CELL
        jsr     HIT
        cmp     uhot
        beq     @drag
        pha
        lda     uhot
        ldx     #0
        jsr     HOTBIT
        pla
        sta     uhot
        ldx     #US_HOT
        jsr     HOTBIT
@drag:  lda     upress
        cmp     #UI_NONE
        beq     @none
        sta     dh
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_SEP + 1
        bcs     @w
        cmp     #UT_GROUP
        bcc     @none
@w:     jsr     MPOS
        ldy     #WR_MOVE
        jsr     WCALL
        bcc     @r
@none:  lda     #UE_NONE
@r:     rts
; Record A's US_HOT: X it (US_HOT) or 0.
HOTBIT: cmp     #UI_NONE
        beq     @r
        jsr     REC
        stx     pw
        ldy     #UW_STATE
        lda     (UIR),y
        and     #<~US_HOT
        ora     pw
        sta     (UIR),y
@r:     rts

; A key a widget module took: A its event. One its module repeats
; (WF_REPEAT), but Enter, Escape, Tab and Space, repeats while it is held,
; after TH_TIMES' delay, then at TH_TIMES + 1's rate.
TOOK:   pha
        lda     ufocus
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        jsr     WFLAGS
        and     #WF_REPEAT
        beq     @r
        lda     ku
        cmp     #K_ENTER
        beq     @r
        cmp     #K_ESC
        beq     @r
        cmp     #K_TAB
        beq     @r
        cmp     #K_SPACE
        beq     @r
        cmp     urkey
        beq     @r                      ; repeating already: its time kept
        sta     urkey
        lda     ufocus
        sta     urfoc
        lda     umodal
        sta     urmod
        jsr     FRAME
        ldy     #TH_TIMES
        clc
        adc     (UIT),y
        sta     urnext
@r:     pla
        rts

; The key repeating, if it is still held and its time has come: qev that
; key down again, carry clear; carry set if none.
REPEAT: lda     urkey
        beq     @no
        lda     ufocus                  ; the focus moved, or a modal window came or went: it
        cmp     urfoc                   ;   stops (X9)
        bne     @up
        lda     umodal
        cmp     urmod
        bne     @up
        lda     uarea
        sta     UIP
        lda     uarea + 1
        sta     UIP + 1
        lda     urkey                   ; held? EV_KEYS's bit
        lsr     a
        lsr     a
        lsr     a
        clc
        adc     #EV_KEYS
        tay
        lda     urkey
        and     #7
        tax
        lda     BITS,x
        and     (UIP),y
        beq     @up
        jsr     FRAME
        sec
        sbc     urnext
        bmi     @no                     ; not yet
        jsr     FRAME
        ldy     #TH_TIMES + 1
        clc
        adc     (UIT),y
        sta     urnext
        lda     #EK_KEY
        sta     qev + EQ_KIND
        lda     urkey
        sta     qev + EQ_CODE
        lda     #1
        sta     qev + EQ_VALUE
        ldy     #EV_MODS
        lda     (UIP),y
        sta     qev + EQ_MODS
        clc
        rts
@up:    stz     urkey
@no:    sec
        rts

; UE_MODAL when the first modal window has come up (UV_VALUE 1, X it) or
; the last gone (0, X the last that was up); UE_NONE else.
MODALEV:
        jsr     FINDMODAL
        ldx     #0
        lda     umodal
        cmp     #UI_NONE
        beq     @n
        sta     umlast
        inx
@n:     cpx     umseen
        beq     @none
        stx     umseen
        stx     evv
        lda     umlast
        sta     evh
        lda     #UE_MODAL
        rts
@none:  lda     #UE_NONE
        rts

; A device plugged in or out (EK_DEVICE): a mouse, if OOUI hasn't the
; pointer yet, gets it (O13).
PLUGGED:
        lda     uptr
        bne     @r
        lda     qev + EQ_VALUE
        and     #ED_MOUSE
        beq     @r
        jsr     PTRSET
@r:     rts

; Event A through the dialogs' filters: A what they make of it.
DFILT:  ldx     udf1 + 1
        beq     @2
        jsr     @f1
@2:     ldx     udf2 + 1
        beq     @r
        jmp     (udf2)
@f1:    jmp     (udf1)
@r:     rts

; A dialog's idle work: A an event, UE_NONE none.
IDLE:   lda     uidle + 1
        beq     @none
        jmp     (uidle)
@none:  lda     #UE_NONE
        rts

; Record A being hidden (HIDEH), or UI_NONE for all (ui_suspend, ui_close):
; a press held on it or inside it let go. A widget module's is told (WR_UP),
; and its event waits for the next poll (a slider's last UE_CHANGE: X10); a
; button's comes up with no click. Keeps nothing.
DROP:   sta     pw
        lda     upress
        cmp     #UI_NONE
        beq     @r
        ldx     pw
        cpx     #UI_NONE
        beq     @all
        jsr     INSIDE
        bcs     @r
@all:   lda     upress
        sta     dh
        lda     #UI_NONE
        sta     upress
        lda     dh
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_GROUP
        beq     @w
        cmp     #UT_SEP + 1
        bcs     @w
        ldy     #UW_STATE               ; a button or a check box
        lda     (UIR),y
        and     #<~US_PRESSED
        sta     (UIR),y
        lda     dh
        jmp     DIRTY
@w:     jsr     MPOS
        ldy     #WR_UP
        jsr     WCALL
        bcs     @r
        cmp     #UE_CHANGE              ; (a canvas's UC_RELEASE isn't kept: no pointer to give)
        bne     @r
        ldx     upend                   ; one waiting already (a field's): it stays
        bne     @r
        sta     upend
        lda     evh
        sta     upendh
        lda     evv
        sta     upendv
        lda     evv + 1
        sta     upendv + 1
@r:     rts

; ui_suspend's: no key repeating, no first click, no press held.
POLLSTOP:
        stz     urkey
        lda     #UI_NONE
        sta     ulastp
        jmp     DROP

; ui_poll's own state at ui_open.
POLLINIT:
        stz     upend
        stz     udf1 + 1
        stz     udf2 + 1
        stz     uidle + 1
        stz     urkey
        stz     umseen
        lda     #UI_NONE
        sta     uhot
        sta     ulastp
        sta     umlast
        lda     #$FF
        ldx     #3
@m:     sta     lmx,x
        dex
        bpl     @m
        rts

        .rodata
BITS:   .byte   $01, $02, $04, $08, $10, $20, $40, $80
; Usages $1E-$38, each its character and its character with Shift.
UCTAB:  .byte   "1!2@3#4$5%6^7&8*9(0)"
        .byte   $0D, $0D, $1B, $1B, $08, $08, $09, $09, ' ', ' '
        .byte   "-_=+[{]}\|", 0, 0, ";:", "'", '"', "`~,<.>/?"
.assert * - UCTAB = 2 * ($39 - $1E), error, "UCTAB: two a usage"

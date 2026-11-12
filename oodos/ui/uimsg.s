; OOUI's message box, ui_msgbox (hw/ooui.md, "Message box and file
; picker"; U2, proposed): a modal window on the overlay, its title, its
; text (up to 4 lines split at CR) and its buttons (UM_OK: OK; UM_OKCANCEL:
; OK and Cancel; UM_YESNO: Yes and No, Y and N their keys), the focus on
; UM_DEFAULT's. Enter presses the focus's button, Escape the last. The
; answer is UE_CLOSE with X UI_MSG and UV_VALUE the button: 0 OK or Yes,
; 1 Cancel or No; the box goes. ui_hide(UI_MSG) takes it away with no
; answer. Refused (UX_BUSY) while one is up, or before ui_open. OO-Amp:
; CONFIRM QUIT, and its error messages.

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .import DLGON, DLGREC, DLGH

PW              = 9                    ; the dialog records' places: the window,
PT              = 10                   ;   the text,
PB0             = 11                   ;   the first button,
PB1             = 12                   ;   the second

        .bss
blk:    .res    UM_LEN
lines:  .res    1
widest: .res    1
rec:    .res    UW_LEN                 ; a record being made
ww:     .res    1
wh:     .res    1

        .segment "CCODE"

ui_msgbox:
        jsr     ENTER
        sta     UIQ
        stx     UIQ + 1
        lda     uopen
        beq     @busy
        jsr     DLGON
        lda     #PW                     ; one up already: refused, the live one untouched
        jsr     DLGH
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        beq     @take
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_HIDDEN
        bne     @take
@busy:  lda     #UX_BUSY
        sec
        rts
@take:  ldy     #UM_LEN - 1
@b:     lda     (UIQ),y
        sta     blk,y
        dey
        bpl     @b
        jsr     MEASURE
        jsr     MAKE
        lda     #<FILTER
        sta     udf1
        lda     #>FILTER
        sta     udf1 + 1
        XGO     FARA, XA_REFOCUS        ; the focus into the box, on the default's button
        lda     blk + UM_DEFAULT
        beq     @done
        lda     blk + UM_BUTTONS
        beq     @done
        lda     #PB1
        jsr     DLGH
        jsr     SETFOCUS
@done:  clc
        rts

; lines and widest: the text's lines (up to 4) and the widest of them and
; the title.
MEASURE:
        lda     blk + UM_TITLE
        sta     UIP
        lda     blk + UM_TITLE + 1
        sta     UIP + 1
        jsr     DLEN
        clc
        adc     #2
        sta     widest
        stz     lines
        lda     blk + UM_TEXT
        sta     UIP
        lda     blk + UM_TEXT + 1
        sta     UIP + 1
        ora     UIP
        beq     @r
@line:  inc     lines
        jsr     DLEN
        cmp     widest
        bcc     @n
        sta     widest
@n:     ldy     #$FF                    ; past its CR, if it has one
@f:     iny
        lda     (UIP),y
        beq     @r
        cmp     #$0D
        bne     @f
        iny
        tya
        clc
        adc     UIP
        sta     UIP
        bcc     @c
        inc     UIP + 1
@c:     lda     lines
        cmp     #4
        bcc     @line
@r:     rts

; The records: the window centred, as wide as its widest line and its
; buttons (at least 24) and four rows more than its lines.
MAKE:   lda     widest
        cmp     #22
        bcs     @w
        lda     #22
@w:     clc
        adc     #4
        cmp     #UI_COLS - 2
        bcc     @w2
        lda     #UI_COLS - 2
@w2:    sta     ww
        lda     lines
        clc
        adc     #4
        sta     wh
        jsr     CLEAR
        lda     #UT_WINDOW
        sta     rec + UW_TYPE
        lda     #UI_NONE
        sta     rec + UW_PARENT
        lda     #UI_COLS                ; centred
        sec
        sbc     ww
        lsr     a
        sta     rec + UW_X
        lda     #UI_ROWS
        sec
        sbc     wh
        lsr     a
        sta     rec + UW_Y
        lda     ww
        sta     rec + UW_W
        lda     wh
        sta     rec + UW_H
        lda     #UF_MODAL | UF_OVERLAY
        sta     rec + UW_FLAGS
        lda     blk + UM_TITLE
        sta     rec + UW_TEXT
        lda     blk + UM_TITLE + 1
        sta     rec + UW_TEXT + 1
        lda     #PW
        jsr     PUT
        jsr     CLEAR                   ; the text: its lines, a cell in from the frame
        lda     #UT_LABEL
        sta     rec + UW_TYPE
        lda     #PW
        sta     rec + UW_PARENT
        lda     #1
        sta     rec + UW_X
        lda     #<-1
        sta     rec + UW_W
        lda     lines
        sta     rec + UW_H
        lda     blk + UM_TEXT
        sta     rec + UW_TEXT
        lda     blk + UM_TEXT + 1
        sta     rec + UW_TEXT + 1
        lda     #PT
        jsr     PUT
        lda     blk + UM_BUTTONS        ; the buttons: one centred, or two side by side
        asl     a
        asl     a
        tax
        lda     ww                      ; the client's middle: (ww - 2) / 2
        sec
        sbc     #2
        lsr     a
        sta     t3
        jsr     CLEAR
        lda     TEXTS,x
        sta     rec + UW_TEXT
        lda     TEXTS + 1,x
        sta     rec + UW_TEXT + 1
        lda     blk + UM_BUTTONS
        bne     @two
        lda     t3                      ; OK alone: default and cancel both
        sec
        sbc     #5
        sta     rec + UW_X
        lda     #UF_DEFAULT | UF_CANCEL
        bra     @b0
@two:   lda     t3
        sec
        sbc     #11
        sta     rec + UW_X
        lda     #UF_DEFAULT
@b0:    sta     rec + UW_FLAGS
        phx
        jsr     BUTTON
        lda     #PB0
        jsr     PUT
        plx
        jsr     CLEAR
        lda     TEXTS + 2,x
        sta     rec + UW_TEXT
        lda     TEXTS + 3,x
        sta     rec + UW_TEXT + 1
        lda     t3
        inc     a
        sta     rec + UW_X
        lda     #UF_CANCEL
        ldy     blk + UM_BUTTONS
        bne     @b1
        lda     #UF_CANCEL | UF_HIDDEN  ; one button: the second hidden
@b1:    sta     rec + UW_FLAGS
        jsr     BUTTON
        lda     #PB1
; Place A from rec.
PUT:    pha
        lda     #<rec
        sta     UIQ
        lda     #>rec
        sta     UIQ + 1
        pla
        jmp     DLGREC

; rec a button: the window's, on its client's last row, 10 wide.
BUTTON: lda     #UT_BUTTON
        sta     rec + UW_TYPE
        lda     #PW
        sta     rec + UW_PARENT
        lda     #<-1
        sta     rec + UW_Y
        lda     #10
        sta     rec + UW_W
        lda     #1
        sta     rec + UW_H
        rts

CLEAR:  ldy     #UW_LEN - 1
@z:     lda     #0
        sta     rec,y
        dey
        bpl     @z
        rts

; The dialog's events, before the program sees them: a button pressed is
; the answer (the box goes); the rest of its widgets' events are dropped.
FILTER: sta     t0
        lda     evh
        cmp     #UI_NONE
        beq     @keep
        sec                             ; its place, if it's one of the box's
        sbc     ucount
        bcc     @keep
        cmp     #PW
        bcc     @keep
        cmp     #PB1 + 1
        bcs     @keep
        sta     t1
        lda     t0
        cmp     #UE_CLICK
        bne     @drop
        lda     t1
        cmp     #PB0
        bcc     @drop
        sec
        sbc     #PB0
        sta     evv                     ; 0 the first button, 1 the second
        stz     evv + 1
        lda     #PW
        jsr     DLGH
        sta     evh
        XGO     FARA, XA_HIDEH
        lda     #PW
        jsr     DLGH
        sta     evh
        lda     #UE_CLOSE
        rts
@drop:  lda     t0
        cmp     #UE_MODAL               ; its coming and going stay the program's to see
        beq     @keep
        lda     #UE_NONE
        rts
@keep:  lda     t0
        rts

        .segment "URODATA"              ; what its buttons point to, which block A draws (U3a)
TEXTS:  .word   T_OK, 0, T_OK, T_CANCEL, T_YES, T_NO
T_OK:   .asciiz "OK"
T_CANCEL: .asciiz "Cancel"
T_YES:  .asciiz "&Yes"
T_NO:   .asciiz "&No"

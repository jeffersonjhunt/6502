; OOUI's choice, UT_CHOICE (hw/ooui.md, "U2"; proposed, the gap OO-Amp's
; Settings named): one of a short list of texts, UW_TEXT an array of UW_A
; string pointers, UW_VALUE the one shown. Drawn "◄ text ►", the text
; centred, in TC_FIELD (TC_FIELDFOCUS with the focus). Right, Up and Space
; show the next, Left and Down the one before (round the list for Space,
; held at the ends for the arrows); a click on its left half the one before,
; on its right half the next. UE_CHANGE with UV_VALUE the one now shown.
; OO-Amp: its Settings' lines (a value changed by Left and Right), and the
; picker's drive and user.

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .export UIW_CHOICE

        .rodata
UIW_CHOICE:
        .byte   UT_CHOICE, WF_FOCUS | WF_LIST | WF_REPEAT
        .word   DRAW, KEY, DOWN, 0, 0, 0

        .bss
v:      .res    1                      ; the value (a choice has under 256)
n:      .res    1                      ; the count

        .code

DRAW:   ldy     #TC_FIELD
        lda     dh
        cmp     ufocus
        bne     @a
        ldy     #TC_FIELDFOCUS
@a:     jsr     TATTR
        lda     UI_RECT
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #$11
        jsr     EMIT
        stz     UIQ                     ; the text: the value's string, none past the count
        stz     UIQ + 1
        jsr     GETV
        lda     v
        cmp     n
        bcs     @txt
        jsr     ARRAY                   ; (UIP's: GOTO again below)
@txt:   lda     #1
        sta     tal
        lda     UI_RECT + 2
        sec
        sbc     #2
        bcc     @r
        tay
        lda     UI_RECT
        inc     a
        ldx     UI_RECT + 1
        jsr     TEXTLINE
        lda     UI_RECT
        clc
        adc     UI_RECT + 2
        dec     a
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #$10
        jmp     EMIT
@r:     rts

; v and n from the record (the low bytes).
GETV:   ldy     #UW_VALUE
        lda     (UIR),y
        sta     v
        ldy     #UW_A
        lda     (UIR),y
        sta     n
        rts

; UIQ = the array's (UW_TEXT) entry v.
ARRAY:  ldy     #UW_TEXT
        lda     (UIR),y
        sta     UIP
        iny
        lda     (UIR),y
        sta     UIP + 1
        lda     v
        asl     a
        tay
        lda     (UIP),y
        sta     UIQ
        iny
        lda     (UIP),y
        sta     UIQ + 1
        rts

KEY:    jsr     GETV
        lda     ku
        cmp     #K_RIGHT
        beq     NEXT
        cmp     #K_UP
        beq     NEXT
        cmp     #K_LEFT
        beq     PREV
        cmp     #K_DOWN
        beq     PREV
        cmp     #K_SPACE
        beq     ROUND
        sec
        rts

DOWN:   jsr     GETV
        lda     UI_RECT + 2             ; the left half: the one before
        lsr     a
        sta     t0
        lda     mcol
        sec
        sbc     UI_RECT
        cmp     t0
        bcc     PREV
NEXT:   lda     v
        inc     a
        cmp     n
        bcs     SAME
        bra     SET
PREV:   lda     v
        beq     SAME
        cmp     n
        bcc     @d
        lda     n                       ; past the count: the last
@d:     dec     a
        bra     SET
ROUND:  lda     v
        inc     a
        cmp     n
        bcc     SET
        lda     #0
SET:    ldy     #UW_VALUE
        sta     (UIR),y
        sta     evv
        lda     #0
        iny
        sta     (UIR),y
        stz     evv + 1
        lda     dh
        sta     evh
        jsr     DIRTY
        lda     #UE_CHANGE
        clc
        rts
SAME:   lda     #UE_NONE
        clc
        rts

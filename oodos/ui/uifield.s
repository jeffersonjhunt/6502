; OOUI's text field, UT_FIELD (hw/ooui.md, "U2"; proposed): UW_TEXT a
; buffer, 0-terminated, of UW_A's low byte bytes with its 0; UW_VALUE the
; cursor; UW_A's high byte the first character shown. In TC_FIELD
; (TC_FIELDFOCUS with the focus, the cursor's cell its colours swapped). It
; takes the focus and the printable keys: a character goes in at the
; cursor (in capitals with UF_UPPER) while there is room; Left, Right, Home
; and End move the cursor; Backspace and Delete take a character out. Each
; change is UE_CHANGE with UV_LIVE; Enter gives a last UE_CHANGE without
; it, and so does the focus leaving after a change. A click puts the cursor
; where it is. OO-Amp: the Load Files dialog's name, a jump-to-file search.

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .export UIW_FIELD

        .rodata
UIW_FIELD:
        .byte   UT_FIELD, WF_FOCUS | WF_REPEAT
        .word   DRAW, KEY, DOWN, 0, 0, 0

        .bss
cur:    .res    1                      ; the cursor
len:    .res    1                      ; the text's length
room:   .res    1                      ; the buffer's size, with its 0
first:  .res    1                      ; the first shown
wid:    .res    1
i:      .res    1
ch:     .res    1

        .code

; UIQ the buffer; cur, len, room, first, wid.
GETS:   ldy     #UW_TEXT
        lda     (UIR),y
        sta     UIQ
        iny
        lda     (UIR),y
        sta     UIQ + 1
        ldy     #UW_VALUE
        lda     (UIR),y
        sta     cur
        ldy     #UW_A
        lda     (UIR),y
        sta     room
        iny
        lda     (UIR),y
        sta     first
        ldy     #$FF
@l:     iny
        lda     (UIQ),y
        bne     @l
        sty     len
        lda     cur                     ; the cursor held to the text
        cmp     len
        bcc     @w
        lda     len
        sta     cur
@w:     lda     UI_RECT + 2
        sta     wid
        rts

; cur and first back (first moved to keep the cursor on view).
PUTS2:  lda     cur
        cmp     first
        bcs     @a
        sta     first
@a:     lda     first                   ; past the last cell shown: first = cur - wid + 1
        clc
        adc     wid
        beq     @put
        bcs     @put
        cmp     cur
        beq     @mv
        bcs     @put
@mv:    lda     cur
        sec
        sbc     wid
        inc     a
        sta     first
@put:   ldy     #UW_VALUE
        lda     cur
        sta     (UIR),y
        iny
        lda     #0
        sta     (UIR),y
        ldy     #UW_A + 1
        lda     first
        sta     (UIR),y
        rts

DRAW:   jsr     GETS
        ldy     #TC_FIELD
        lda     dh
        cmp     ufocus
        bne     @a
        ldy     #TC_FIELDFOCUS
@a:     jsr     TATTR
        lda     UI_RECT
        ldx     UI_RECT + 1
        jsr     GOTO
        stz     i
@c:     lda     i
        cmp     wid
        bcs     @r
        clc
        adc     first
        sta     ch
        tay
        lda     #' '
        cpy     len
        bcs     @sp
        lda     (UIQ),y
@sp:    pha
        lda     dh                      ; the cursor's cell: the colours swapped
        cmp     ufocus
        bne     @put
        lda     ch
        cmp     cur
        bne     @put
        lda     uattr
        pha
        asl     a                       ; the nibbles swapped
        adc     #$80
        rol     a
        asl     a
        adc     #$80
        rol     a
        sta     uattr
        pla
        sta     t0
        pla
        jsr     EMIT
        lda     t0
        sta     uattr
        bra     @n
@put:   pla
        jsr     EMIT
@n:     inc     i
        bra     @c
@r:     rts

KEY:    jsr     GETS
        lda     ku
        cmp     #K_ENTER
        beq     @enter
        cmp     #K_LEFT
        beq     @left
        cmp     #K_RIGHT
        beq     @right
        cmp     #K_HOME
        beq     @home
        cmp     #K_END
        beq     @end
        cmp     #K_BACK
        beq     @back
        cmp     #K_DEL
        beq     @del
        lda     km                      ; a printable character, with no Ctrl or Alt
        and     #UK_CTRL | UK_ALT
        bne     @no
        lda     kc
        cmp     #' '
        bcc     @no
        cmp     #$7F
        bcs     @no
        jmp     INSERT
@no:    sec
        rts
@enter: ldy     #UW_STATE               ; the last change: no more to come
        lda     (UIR),y
        and     #<~US_EDIT
        sta     (UIR),y
        jmp     EVENT
@left:  lda     cur
        beq     MOVED
        dec     cur
        bra     MOVED
@right: lda     cur
        cmp     len
        bcs     MOVED
        inc     cur
        bra     MOVED
@home:  stz     cur
        bra     MOVED
@end:   lda     len
        sta     cur
        bra     MOVED
@back:  lda     cur
        beq     MOVED
        dec     cur
@del:   lda     cur                     ; the character at the cursor out
        cmp     len
        bcs     MOVED
        tay
@sh:    iny
        lda     (UIQ),y
        dey
        sta     (UIQ),y
        iny
        cpy     len
        bcc     @sh
        dec     len
        bra     CHANGED

; The cursor moved: drawn again, nothing to say.
MOVED:  jsr     PUTS2
        lda     dh
        jsr     DIRTY
        lda     #UE_NONE
        clc
        rts

; kc in at the cursor, if there's room.
INSERT: lda     len
        inc     a
        cmp     room
        bcs     MOVED                   ; full
        lda     kc
        ldy     #UW_FLAGS
        pha
        lda     (UIR),y
        and     #UF_UPPER
        tax
        pla
        cpx     #0
        beq     @c
        jsr     UPPER
@c:     sta     ch
        ldy     len                     ; the tail moved up one, its 0 too
        iny
@sh:    dey
        lda     (UIQ),y
        iny
        sta     (UIQ),y
        dey
        cpy     cur
        bne     @sh
        lda     ch
        sta     (UIQ),y
        inc     cur
        inc     len
; The text changed: UE_CHANGE with UV_LIVE, the field marked edited.
CHANGED:
        jsr     PUTS2
        ldy     #UW_STATE
        lda     (UIR),y
        ora     #US_EDIT
        sta     (UIR),y
        lda     #UV_LIVE
        tsb     evf
EVENT:  lda     dh
        sta     evh
        jsr     DIRTY
        lda     cur
        sta     evv
        stz     evv + 1
        lda     #UE_CHANGE
        clc
        rts

; A click: the cursor where it is.
DOWN:   jsr     GETS
        lda     mpx + 1                 ; mpx / 4: its cell
        lsr     a
        lda     mpx
        ror     a
        lsr     a
        clc
        adc     first
        cmp     len
        bcc     @c
        lda     len
@c:     sta     cur
        jmp     MOVED

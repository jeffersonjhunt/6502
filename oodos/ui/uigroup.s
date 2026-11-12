; OOUI's group and radio buttons, UT_GROUP and UT_RADIO (hw/ooui.md, "U2";
; proposed): a group, framed with its title as a panel is (UF_FRAME), holds
; radio buttons, its children; UW_VALUE the one chosen, 0 its first radio.
; A radio button is (•) or ( ) and its text; it takes the focus. Space or
; Enter, its letter, or a click chooses it; Up and Left choose the one
; before, Down and Right the next, the focus with it. UE_CHANGE with X the
; group, UV_VALUE the radio's place. OO-Amp: its visualizer's mode and the
; repeat setting's choices, where they are radio-like.

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .export UIW_GROUP, UIW_RADIO

        .segment "BRODATA"
UIW_GROUP:
        .byte   UT_GROUP, 0
        .word   GDRAW, 0, 0, 0, 0, 0
UIW_RADIO:
        .byte   UT_RADIO, WF_FOCUS
        .word   RDRAW, RKEY, RDOWN, 0, 0, 0

        .bss
grp:    .res    1                      ; the radio's group,
pos:    .res    1                      ;   its place in it
h:      .res    1
want:   .res    1

        .segment "BCODE"

; A group: a panel's look (TC_CLIENT, a frame and its title with UF_FRAME).
GDRAW:  ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_FRAME
        bne     @frame
        ldy     #TC_CLIENT
        jsr     TATTR
        lda     #' '
        jmp     FILLR
@frame: ldy     #TC_CLIENT
        lda     (UIT),y
        pha
        ldy     #TC_FRAME
        jsr     TATTR
        ldy     #TH_FRAMES + 2
        lda     (UIT),y
        plx
        jsr     BOXD
        ldy     #UW_TEXT
        lda     (UIR),y
        sta     UIQ
        iny
        lda     (UIR),y
        sta     UIQ + 1
        ora     UIQ
        beq     @r
        ldy     #TC_LABEL
        jsr     TATTR
        lda     UI_RECT
        clc
        adc     #2
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #' '
        jsr     EMIT
        lda     UI_RECT + 2
        sec
        sbc     #6
        bcc     @r
        tax
        jsr     PUTS
        lda     #' '
        jmp     EMIT
@r:     rts

; grp and pos for radio dh (UIR its record after).
PLACE:  ldy     #UW_PARENT
        lda     (UIR),y
        sta     grp
        stz     pos
        sta     h
@l:     inc     h
        lda     h
        cmp     dh
        bcs     @done
        jsr     REC
        ldy     #UW_PARENT
        lda     (UIR),y
        cmp     grp
        bne     @l
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_RADIO
        bne     @l
        inc     pos
        bra     @l
@done:  lda     dh
        jmp     REC

RDRAW:  jsr     PLACE
        lda     grp                     ; on if the group's value is its place
        jsr     REC
        ldy     #UW_VALUE + 1
        lda     (UIR),y
        bne     @off
        dey
        lda     (UIR),y
        cmp     pos
        bne     @off
        lda     #UG_RADIOON
        bra     @g
@off:   lda     #UG_RADIO
@g:     pha
        lda     dh
        jsr     REC
        ldy     #UW_FLAGS               ; its colour: disabled, the focus's, or TC_CHECK
        lda     (UIR),y
        and     #UF_DISABLED
        beq     @en
        ldy     #TC_DISABLED
        jsr     NOACC
        bra     @go
@en:    ldy     #TC_CHECK
        lda     dh
        cmp     ufocus
        bne     @a
        ldy     #TC_BUTTONFOCUS
@a:     jsr     TATTR
@go:    lda     UI_RECT
        ldx     UI_RECT + 1
        jsr     GOTO
        pla
        jsr     EMIT
        lda     #' '
        jsr     EMIT
        stz     tal
        ldy     #UW_TEXT
        lda     (UIR),y
        sta     UIQ
        iny
        lda     (UIR),y
        sta     UIQ + 1
        lda     UI_RECT + 2
        sec
        sbc     #2
        bcc     @r
        tay
        lda     UI_RECT
        clc
        adc     #2
        ldx     UI_RECT + 1
        jmp     TEXTLINE
@r:     rts

RKEY:   lda     ku
        cmp     #K_SPACE
        beq     CHOOSE
        cmp     #K_ENTER
        beq     CHOOSE
        cmp     #K_UP
        beq     @back
        cmp     #K_LEFT
        beq     @back
        cmp     #K_DOWN
        beq     @on
        cmp     #K_RIGHT
        beq     @on
        sec
        rts
@back:  lda     #$FF
        bra     MOVE
@on:    lda     #1
        bra     MOVE

RDOWN:
CHOOSE: jsr     PLACE
        lda     pos
; The group's value A (its radio's place): UE_CHANGE if it changed.
SETG:   sta     want
        lda     grp
        jsr     REC
        ldy     #UW_VALUE
        lda     (UIR),y
        cmp     want
        bne     @set
        iny
        lda     (UIR),y
        beq     @same
@set:   ldy     #UW_VALUE
        lda     want
        sta     (UIR),y
        sta     evv
        iny
        lda     #0
        sta     (UIR),y
        stz     evv + 1
        lda     grp
        sta     evh
        jsr     DIRTY
        lda     #UE_CHANGE
        clc
        rts
@same:  lda     #UE_NONE
        clc
        rts

; The radio A away (1 the next, $FF the one before) in the group that can
; take the focus: chosen, the focus there. Held at the ends.
MOVE:   sta     t0
        jsr     PLACE
        lda     dh
        sta     h
@l:     lda     h
        clc
        adc     t0
        sta     h
        cmp     grp
        beq     @none                   ; back past the group's first
        cmp     utot
        bcs     @none
        jsr     REC
        ldy     #UW_PARENT
        lda     (UIR),y
        cmp     grp
        bne     @l
        ldy     #UW_TYPE
        lda     (UIR),y
        cmp     #UT_RADIO
        bne     @l
        lda     h
        jsr     CANFOCUS
        bcs     @l
        jsr     SETFOCUS
        lda     h
        sta     dh
        jsr     REC
        jmp     CHOOSE
@none:  lda     #UE_NONE
        clc
        rts

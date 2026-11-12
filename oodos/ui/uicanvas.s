; OOUI's canvas, UT_CANVAS (hw/ooui.md, "U2"; proposed): cells left
; transparent, so the program's own layer under OOUI's shows through (a
; visualizer, a waveform), and the pointer on it reported as UE_CANVAS:
; UV_KIND (UC_PRESS, UC_MOVE while the button is held, UC_RELEASE,
; UC_WHEEL with the steps in UV_VALUE), UV_X and UV_Y in pointer units
; from its top left. It takes no focus. OO-Amp: its spectrum and time
; click zones.
;
; U3b (proposed, the owner's Q1): with UF_ROUTINE its cells are the
; program's, from a routine (UW_TEXT) OOUI calls (JSR, block B in the bank's
; window) for each row it draws, top first: A/X the row (0 its first), Y the
; canvas's handle. It returns A/X its cells, as many as the canvas is wide
; (ui_rect's width), each a character and an attribute (2 bytes), outside
; $4000-$7FFF; or 0, the row transparent. As a list's row routine, it may
; map W1 (OOUI maps it again after) and use the program's zero page, not
; OOUI's; it may not call ui_* nor the BDOS. The program calls ui_dirty on
; the canvas when its cells change (OO-Amp: each frame, its scope and VU).

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .export UIW_CANVAS

        .segment "BBSS"                 ; block B's own, not UISTATE (U3b, Q1)
crow:   .res    1                      ; the row drawn
cleft:  .res    1                      ;   its cells left
cy:     .res    1                      ;   the next cell's offset (2 a cell: 80 cells at most)
cmy:    .res    1                      ; the canvas
ccall:  .res    2                      ; its row routine

        .segment "BRODATA"
UIW_CANVAS:
        .byte   UT_CANVAS, 0
        .word   DRAW, 0, DOWN, MOVE, UP, WHEEL

        .segment "BCODE"

; Transparent: character 0, attribute 0; with UF_ROUTINE, the routine's
; cells (U3b, Q1).
DRAW:   ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_ROUTINE
        bne     CNROWS
        stz     uattr
        lda     #0
        jmp     FILLR

CNROWS: ldy     #UW_TEXT
        lda     (UIR),y
        sta     ccall
        iny
        lda     (UIR),y
        sta     ccall + 1
        lda     dh
        sta     cmy
        stz     crow
@row:   lda     crow
        cmp     UI_RECT + 3
        bcs     @r
        lda     cmy
        jsr     PUBH
        tay
        lda     crow
        ldx     #0
        jsr     @jmp
        sta     UIQ
        stx     UIQ + 1
        jsr     MAPW1                   ; the routine may have mapped W1
        lda     cmy
        sta     dh
        jsr     REC
        lda     UI_RECT + 1
        clc
        adc     crow
        tax
        lda     UI_RECT
        jsr     GOTO
        lda     UI_RECT + 2
        sta     cleft
        stz     cy
        lda     UIQ
        ora     UIQ + 1
        bne     @cell
        stz     uattr                   ; 0: the row transparent
        lda     #0
        ldx     cleft
        jsr     REPT
        bra     @next
@cell:  ldy     cy                      ; a character, then its attribute
        lda     (UIQ),y
        pha
        iny
        lda     (UIQ),y
        sta     uattr
        iny
        sty     cy
        pla
        jsr     EMIT
        dec     cleft
        bne     @cell
@next:  inc     crow
        bra     @row
@jmp:   jmp     (ccall)
@r:
CNDONE: rts                             ; (test_oodos_ui times CNROWS to here)

DOWN:   lda     #UC_PRESS
        bra     EVENT
MOVE:   lda     #UC_MOVE
        bra     EVENT
UP:     lda     #UC_RELEASE
        bra     EVENT
WHEEL:  lda     wst                     ; the steps, signed
        sta     evv
        bpl     @p
        lda     #$FF
        sta     evv + 1
@p:     lda     #UC_WHEEL
        jsr     EVENT
        clc
        rts

; UE_CANVAS of kind A, the pointer (mpx, mpy) where it is on the canvas.
EVENT:  sta     evk
        lda     dh
        sta     evh
        lda     mpx
        sta     evx
        lda     mpx + 1
        sta     evx + 1
        lda     mpy
        sta     evyy
        lda     #UE_CANVAS
        clc
        rts

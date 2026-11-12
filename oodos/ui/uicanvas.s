; OOUI's canvas, UT_CANVAS (hw/ooui.md, "U2"; proposed): cells left
; transparent, so the program's own layer under OOUI's shows through (a
; visualizer, a waveform), and the pointer on it reported as UE_CANVAS:
; UV_KIND (UC_PRESS, UC_MOVE while the button is held, UC_RELEASE,
; UC_WHEEL with the steps in UV_VALUE), UV_X and UV_Y in pointer units
; from its top left. It takes no focus. OO-Amp: its spectrum and time
; click zones.

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .export UIW_CANVAS

        .rodata
UIW_CANVAS:
        .byte   UT_CANVAS, 0
        .word   DRAW, 0, DOWN, MOVE, UP, WHEEL

        .code

; Transparent: character 0, attribute 0.
DRAW:   stz     uattr
        lda     #0
        jmp     FILLR

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

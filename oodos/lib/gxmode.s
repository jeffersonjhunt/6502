; OOLIB's layers (include/oolib.inc, "Graphics"; hw/oolib.md, L4): a
; layer's type and its memory, the display, the border, and the console's
; screen back. Register work: a program may write every video register but
; IEN and SPLASH (Technical Reference chapter 6).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

        .code

; A the layer, X its CONFIG, Y its map unit, GX_V's low byte its tile unit.
GX_LAYER:
        and     #1
        asl     a
        asl     a
        asl     a
        asl     a
        sty     GXT
        tay                             ; Y: the layer's registers' offset
        txa
        sta     VID_L0 + VL_CONFIG,y
        lda     GXT
        sta     VID_L0 + VL_MAP,y
        lda     GX_V
        sta     VID_L0 + VL_TILE,y
        lda     #0
        sta     VID_L0 + VL_HSCROLL,y
        sta     VID_L0 + VL_HSCROLL + 1,y
        sta     VID_L0 + VL_VSCROLL,y
        sta     VID_L0 + VL_VSCROLL + 1,y
        sta     VID_L0 + VL_PALOFS,y
        rts

; A CTRL's layer and sprite bits: the display on with them.
GX_SHOW:
        and     #$0E
        ora     #$01
        sta     VID_CTRL
        rts

GX_BORDER:
        sta     VID_BORDER
        rts

GX_CONSOLE:
        lda     #DM_CONSOLE
        ldx     #0
        ldy     #O_DMA
        jsr     BDOS
        jmp     GX_W1BACK

; A a layer: its scrolls GX_X and GX_Y.
GX_SCROLL:
        and     #1
        asl     a
        asl     a
        asl     a
        asl     a
        tay
        lda     GX_X
        sta     VID_L0 + VL_HSCROLL,y
        lda     GX_X + 1
        sta     VID_L0 + VL_HSCROLL + 1,y
        lda     GX_Y
        sta     VID_L0 + VL_VSCROLL,y
        lda     GX_Y + 1
        sta     VID_L0 + VL_VSCROLL + 1,y
        rts

; A a layer, X the unit it shows from the next frame: MAPBASE (latched at
; the vertical blank), then that frame waited for (function 80's FT_WAIT).
GX_FLIP:
        and     #1
        asl     a
        asl     a
        asl     a
        asl     a
        tay
        txa
        sta     VID_L0 + VL_MAP,y
        lda     #FT_WAIT
        ldx     #0
        ldy     #O_FRAME
        jmp     BDOS

; A the palette's unit (PALBASE), or the sprite table's (SPRBASE): latched
; at the vertical blank.
GX_PALBASE:
        sta     VID_PALBASE
        rts
GX_SPRBASE:
        sta     VID_SPRBASE
        rts

; OOLIB's layers (include/oolib.inc, "Graphics"; hw/oolib.md, L4 and
; "Video card revision 2"): a layer's type and its memory, the display, the
; border, and the console's screen back. Register work: a program may write
; every video register but IEN, SPLASH, the port's and the slots but 1
; (Technical Reference chapter 6; the W1 contract). The base registers take
; card pages, two bytes, low first.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

        .code

; Y = layer A's registers' offset: 0 or $10.
LAYERY: and     #1
        asl     a
        asl     a
        asl     a
        asl     a
        tay
        rts

; A the layer, X its CONFIG, GX_V its map's card page, GX_R its tiles' or
; font's: scrolls and PALOFS 0.
GX_LAYER:
        jsr     LAYERY
        txa
        sta     VID_L0 + VL_CONFIG,y
        lda     GX_V
        sta     VID_L0 + VL_MAP,y
        lda     GX_V + 1
        sta     VID_L0 + VL_MAP + 1,y
        lda     GX_R
        sta     VID_L0 + VL_TILE,y
        lda     GX_R + 1
        sta     VID_L0 + VL_TILE + 1,y
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
        jsr     LAYERY
        lda     GX_X
        sta     VID_L0 + VL_HSCROLL,y
        lda     GX_X + 1
        sta     VID_L0 + VL_HSCROLL + 1,y
        lda     GX_Y
        sta     VID_L0 + VL_VSCROLL,y
        lda     GX_Y + 1
        sta     VID_L0 + VL_VSCROLL + 1,y
        rts

; A a layer, GX_V the card page it shows from the next frame: MAPBASE
; (latched at the vertical blank), then that frame waited for (function
; 80's FT_WAIT).
GX_FLIP:
        jsr     LAYERY
        lda     GX_V
        sta     VID_L0 + VL_MAP,y
        lda     GX_V + 1
        sta     VID_L0 + VL_MAP + 1,y
        lda     #FT_WAIT
        ldx     #0
        ldy     #O_FRAME
        jmp     BDOS

; A/X the palette's card page (PALBASE), or the sprite table's (SPRBASE):
; latched at the vertical blank.
GX_PALBASE:
        sta     VID_PALBASE
        stx     VID_PALBASE + 1
        rts
GX_SPRBASE:
        sta     VID_SPRBASE
        stx     VID_SPRBASE + 1
        rts

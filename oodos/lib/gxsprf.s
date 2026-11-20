; OOLIB's full-resolution sprites (include/oolib.inc, "Graphics";
; hw/sprites640.md; interface version 14): GX_SPRFULL turns the card's
; SPRCTL FULL bit on or off, and GX_SPRFPOS puts a sprite at a 640 x 480
; position with its entry's byte 3 bit 7 set, so that it is drawn a sprite
; pixel a screen pixel while FULL is on. A module of its own, so gxspr.s,
; and every program that doesn't call these (OOUI.BNK among them), links
; the same bytes as before. The shadow is gxspr.s's GX_SPR.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

GX_IFACE_FULL   = 14                   ; OODOS's warm boot clears SPRCTL from here on

        .code

; A 1 on, 0 off (bit 0). Function 80 asked for the interface, as GX_BMP
; does for GB_DIRECT: below 14, or no function 80, carry set and A
; GE_VERSION, SPRCTL untouched. Carry clear otherwise.
GX_SPRFULL:
        and     #VID_SPR_FULL
        pha
        lda     #FT_INFO
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        cmp     #$FF                    ; no function 80: A = X = $FF
        beq     @old
        cmp     #GX_IFACE_FULL
        bcc     @old
        pla
        sta     VID_SPRCTL
        clc
        rts
@old:   pla
        lda     #GE_VERSION
        sec
        rts

; A a sprite: GX_X and GX_Y into its shadow entry's bytes 2-5, byte 3 with
; FULL. As GX_SPRPOS but for the bit.
GX_SPRFPOS:
        jsr     ENTRY
        ldy     #2
        lda     GX_X
        sta     (GXP),y
        iny
        lda     GX_X + 1
        and     #3
        ora     #VID_SPRX_FULL
        sta     (GXP),y
        iny
        lda     GX_Y
        sta     (GXP),y
        iny
        lda     GX_Y + 1
        and     #3
        sta     (GXP),y
        rts

; GXP = sprite A's entry in the shadow (gxspr.s's ENTRY, its own copy:
; gxspr.s exports none).
ENTRY:  and     #63
        stz     GXP + 1
        asl     a
        asl     a
        asl     a
        rol     GXP + 1
        clc
        adc     #<GX_SPR
        sta     GXP
        lda     GXP + 1
        adc     #>GX_SPR
        sta     GXP + 1
        rts

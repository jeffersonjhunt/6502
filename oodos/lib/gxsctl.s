; OOLIB's full-resolution sprites switch (include/oolib.inc, "Graphics";
; hw/sprites640.md; interface version 14): GX_SPRFULL turns the card's
; SPRCTL FULL bit on or off. A module of its own, apart from GX_SPRFPOS
; (gxsprf.s), so a program that keeps its own sprite table and only sets
; FULL (VIEW) links neither gxspr.s nor its 512-byte shadow (review round 1,
; F3). Its name sorts before gxspr and gxsprf under C and en_US alike.

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

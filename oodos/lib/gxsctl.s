; OOLIB's full-resolution sprites switch (include/oolib.inc, "Graphics";
; hw/sprites640.md): GX_SPRFULL turns the card's
; SPRCTL FULL bit on or off. A module of its own, apart from GX_SPRFPOS
; (gxsprf.s), so a program that keeps its own sprite table and only sets
; FULL (VIEW) links neither gxspr.s nor its 512-byte shadow (review round 1,
; F3). Its name sorts before gxspr and gxsprf under C and en_US alike.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

        .code

; A 1 on, 0 off (bit 0). Carry clear. OODOS's warm boot clears SPRCTL.
GX_SPRFULL:
        and     #VID_SPR_FULL
        sta     VID_SPRCTL
        clc
        rts

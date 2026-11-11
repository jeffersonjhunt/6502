; OOLIB's palette (include/oolib.inc, "Graphics"; hw/oolib.md, L4): 256
; entries of 12 bits, two bytes each (GGGGBBBB, then 0000RRRR), kept in a
; 512-byte shadow in the program's memory and copied to the card's palette
; (two card pages) at the vertical blank by OODOS's slot 1 (function 83's
; DM_VBSET, through the card's port),
; so a change shows whole on the next frame. GX_FADE is gxfade.s's, in a
; module of its own, so that a program that only sets colours doesn't link
; its 3 KB of accumulators.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

SLOT            = 1

        .bss
GX_PAL: .res    512                    ; the shadow
GX_PALU: .res   2                      ; its card page

        .code

GX_PALINIT:
        lda     #512 / VM_PAGE          ; GX_V in card pages: 512 bytes are two (a unit)
        sta     GX_V
        stz     GX_V + 1
        lda     #1
        jsr     GX_VALLOC
        bcs     @no
        sta     GX_PALU
        stx     GX_PALU + 1
        sta     VID_PALBASE
        stx     VID_PALBASE + 1         ; the slot: the shadow to it, 512 bytes
        ldx     #2
@dst:   lda     GX_A,x
        sta     GXB + 5,x
        dex
        bpl     @dst
        lda     #<GX_PAL
        ldx     #>GX_PAL
        jsr     GXCPU
        ldx     #2
@src:   lda     GX_A,x
        sta     GXB + 2,x
        dex
        bpl     @src
        lda     #DM_VBSET
        sta     GXB
        lda     #SLOT
        sta     GXB + 1
        stz     GXB + 8
        lda     #>512
        sta     GXB + 9
        jmp     GXDMA
@no:    rts

; A an entry, X its first byte, Y its second.
GX_PALSET:
        sty     GXT
        stz     GXP + 1
        asl     a
        rol     GXP + 1
        clc
        adc     #<GX_PAL
        sta     GXP
        lda     GXP + 1
        adc     #>GX_PAL
        sta     GXP + 1
        txa
        sta     (GXP)
        ldy     #1
        lda     GXT
        sta     (GXP),y
        rts

; A/X 512 bytes into the shadow.
GX_PALLOAD:
        sta     GXQ
        stx     GXQ + 1
        lda     #<GX_PAL
        sta     GXP
        lda     #>GX_PAL
        sta     GXP + 1
        ldx     #2
        ldy     #0
@copy:  lda     (GXQ),y
        sta     (GXP),y
        iny
        bne     @copy
        inc     GXQ + 1
        inc     GXP + 1
        dex
        bne     @copy
        rts

GX_PALSHOW:
        lda     #1 << SLOT
        jmp     GXGO

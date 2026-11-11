; OOLIB's sprites (include/oolib.inc, "Graphics"; hw/oolib.md, L4): the
; card's 64 hardware sprites, 8 bytes each in its table at SPRBASE (image,
; X, Y, flags, size: Technical Reference chapter 6), kept in a 512-byte
; shadow in the program's memory and copied to the card at the vertical
; blank by OODOS's slot 0, so a frame's moves show together. Collisions are
; the card's: STATUS says which groups, ISR bit 2 that one came.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

SLOT            = 0

        .bss
GX_SPR: .res    512                    ; the shadow
GX_SPRU: .res   2                      ; the table's card page

        .code

GX_SPRINIT:
        lda     #<GX_SPR                ; all off (depth 0)
        sta     GXP
        lda     #>GX_SPR
        sta     GXP + 1
        ldx     #2
        ldy     #0
        lda     #0
@clr:   sta     (GXP),y
        iny
        bne     @clr
        inc     GXP + 1
        dex
        bne     @clr
        lda     #512 / VM_PAGE          ; GX_V in card pages: 512 bytes are two (a unit)
        sta     GX_V
        stz     GX_V + 1
        lda     #1
        jsr     GX_VALLOC
        bcs     @no
        sta     GX_SPRU
        stx     GX_SPRU + 1
        sta     VID_SPRBASE
        stx     VID_SPRBASE + 1
        ldx     #2
@dst:   lda     GX_A,x
        sta     GXB + 5,x
        dex
        bpl     @dst
        lda     #<GX_SPR
        ldx     #>GX_SPR
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

; GXP = sprite A's entry in the shadow.
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

GX_SPRPOS:
        jsr     ENTRY
        ldy     #2
        lda     GX_X
        sta     (GXP),y
        iny
        lda     GX_X + 1
        and     #3
        sta     (GXP),y
        iny
        lda     GX_Y
        sta     (GXP),y
        iny
        lda     GX_Y + 1
        and     #3
        sta     (GXP),y
        rts

GX_SPRIMG:
        jsr     ENTRY
        lda     GX_V
        sta     (GXP)
        ldy     #1
        lda     GX_V + 1                ; (revision 2: bits 14-8 of the image's card page, and bit 7 the depth)
        sta     (GXP),y
        rts

GX_SPRATTR:
        stx     GXT
        sty     GXT + 1
        jsr     ENTRY
        ldy     #6
        lda     GXT
        sta     (GXP),y
        iny
        lda     GXT + 1
        sta     (GXP),y
        rts

GX_SPRSHOW:
        lda     #1 << SLOT
        jmp     GXGO

GX_COLL:
        lda     VID_ISR
        and     #VID_COLLIDE
        beq     @none
        lda     #VID_COLLIDE            ; the flag cleared: the next one is the next frame's
        sta     VID_ISR
        lda     VID_STATUS
        and     #$0F
        sec
        rts
@none:  lda     VID_STATUS
        and     #$0F
        clc
        rts

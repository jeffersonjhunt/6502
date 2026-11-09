; OOLIB's tile maps (include/oolib.inc, "Graphics"; hw/oolib.md, L4): a
; map's 2-byte entries (VERA's: the tile's number, its flips and palette
; offset) put and got through W1, filled by function 83's DM_COPY, and a
; layer scrolled (gxmode.s's GX_SCROLL).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

        .bss
tmu:    .res    1                      ; the map's unit and its width
tmw:    .res    1
toff:   .res    2
tcnt:   .res    2

        .code

GX_TMAP:
        sta     tmu
        stx     tmw
        rts

; GX_A = the entry at column X, row Y: the unit's address + (row x width +
; column) x 2. Carry set if W1 can't have it.
TADDR:  stx     GXT
        sty     toff
        stz     toff + 1
        lda     tmu
        jsr     GX_UNIT
        ldx     #5                      ; row x 32, or x 64
        lda     tmw
        cmp     #64
        bne     @shift
        inx
@shift: asl     toff
        rol     toff + 1
        dex
        bne     @shift
        lda     toff                    ; + the column, x 2
        clc
        adc     GXT
        sta     toff
        bcc     @x2
        inc     toff + 1
@x2:    asl     toff
        rol     toff + 1
        lda     toff
        clc
        adc     GX_A
        sta     GX_A
        lda     toff + 1
        adc     GX_A + 1
        sta     GX_A + 1
        bcc     @map
        inc     GX_A + 2
@map:   jmp     GXPHYS

GX_TPUT:
        jsr     TADDR
        bcs     @no
        lda     GX_V
        sta     (GXP)
        ldy     #1
        lda     GX_V + 1
        sta     (GXP),y
        clc
@no:    rts

GX_TGET:
        jsr     TADDR
        bcs     @no
        ldy     #1
        lda     (GXP),y
        tax
        lda     (GXP)
        clc
@no:    rts

; X rows of the map all GX_V: the first entry put, then copied on 2 bytes
; at a time by DMA (a forward copy over itself repeats it).
GX_TFILL:
        txa                             ; no rows: nothing (never a length of 0 to function 83,
        bne     @rows                   ; which is 65,536: review F2)
        clc
        rts
@rows:  stx     tcnt                    ; the bytes: rows x width x 2, less the 2 put
        stz     tcnt + 1
        ldx     #0
        ldy     #0
        jsr     GX_TPUT
        bcs     @no
        ldx     #6                      ; x 32 x 2, or x 64 x 2
        lda     tmw
        cmp     #64
        bne     @shift
        inx
@shift: asl     tcnt
        rol     tcnt + 1
        dex
        bne     @shift
        lda     tcnt
        sec
        sbc     #2
        sta     GXB + 7
        lda     tcnt + 1
        sbc     #0
        sta     GXB + 8
        lda     tmu
        jsr     GX_UNIT
        ldx     #2
@src:   lda     GX_A,x
        sta     GXB + 1,x
        dex
        bpl     @src
        clc
        lda     GX_A
        adc     #2
        sta     GXB + 4
        lda     GX_A + 1
        adc     #0
        sta     GXB + 5
        lda     GX_A + 2
        adc     #0
        sta     GXB + 6
        lda     #DM_COPY
        sta     GXB
        jmp     GXDMA
@no:    rts

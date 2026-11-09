; OOLIB's palette (include/oolib.inc, "Graphics"; hw/oolib.md, L4): 256
; entries of 12 bits, two bytes each (GGGGBBBB, then 0000RRRR), kept in a
; 512-byte shadow in the program's memory and copied to the card's palette
; unit at the vertical blank by OODOS's slot 1 (function 83's DM_VBSET),
; so a change shows whole on the next frame, and a fade costs a TSB a frame.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

SLOT            = 1

        .zeropage
ap:     .res    2                      ; GX_FADE's: an accumulator, and its step
sp:     .res    2

        .bss
GX_PAL: .res    512                    ; the shadow
GX_PALU: .res   1                      ; its unit on the card
fsteps: .res    1                      ; GX_FADE's frames left
facc:   .res    2 * 768                ; each entry's three components, 8.8 (blue, green, red)
fstep:  .res    2 * 768                ;   and their steps a frame (signed)
fcomp:  .res    1

        .code

GX_PALINIT:
        lda     #1
        jsr     GX_ALLOC
        bcs     @no
        sta     GX_PALU
        sta     VID_PALBASE
        jsr     GX_UNIT                 ; the slot: the shadow to the unit, 512 bytes
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

; A frames, GX_V the palette to end at. Each component (blue, green, red:
; 4 bits) goes from the shadow's to the target's in A equal steps, kept to
; 8 fractional bits; a step a frame, shown at its vertical blank. The
; target exactly at the end.
GX_FADE:
        sta     fsteps
        lda     GX_AREA                 ; no area: no slot to show it by (refused)
        ora     GX_AREA + 1
        bne     @area
        lda     #GE_AREA
        sec
        rts
@area:  lda     fsteps
        bne     @go
        clc
        rts
@go:    jsr     FSETUP
@frame: jsr     FSTEP
        jsr     GX_PALSHOW
        lda     #FT_WAIT
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        dec     fsteps
        bne     @frame
        lda     GX_V
        ldx     GX_V + 1
        jsr     GX_PALLOAD
        jsr     GX_PALSHOW
        lda     #FT_WAIT
        ldx     #0
        ldy     #O_FRAME
        jmp     BDOS

; Component n (0-767) of entry n / 3 of the palette at GXP: its 4 bits in A.
; n % 3: 0 blue (byte 0's low), 1 green (byte 0's high), 2 red (byte 1's).
; FSETUP walks entries with GXP (the shadow) and GXQ (the target).
FSETUP: lda     #<GX_PAL
        sta     GXP
        lda     #>GX_PAL
        sta     GXP + 1
        lda     GX_V
        sta     GXQ
        lda     GX_V + 1
        sta     GXQ + 1
        lda     #<facc
        sta     ap
        lda     #>facc
        sta     ap + 1
        lda     #<fstep
        sta     sp
        lda     #>fstep
        sta     sp + 1
        stz     ent
        stz     ent + 1
@entry: ldy     #0
        lda     (GXP),y
        and     #$0F
        sta     c0
        lda     (GXP),y
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     c0 + 1
        iny
        lda     (GXP),y
        and     #$0F
        sta     c0 + 2
        dey
        lda     (GXQ),y
        and     #$0F
        sta     c1
        lda     (GXQ),y
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     c1 + 1
        iny
        lda     (GXQ),y
        and     #$0F
        sta     c1 + 2
        ldx     #0
@comp:  lda     c0,x                    ; the accumulator: the start, 8.8
        jsr     PUTACC
        sec                             ; the step: (target - start) x 256 / frames
        lda     c1,x
        sbc     c0,x
        jsr     STEPOF
        jsr     PUTSTEP
        inx
        cpx     #3
        bne     @comp
        lda     GXP
        clc
        adc     #2
        sta     GXP
        bcc     @q
        inc     GXP + 1
@q:     lda     GXQ
        clc
        adc     #2
        sta     GXQ
        bcc     @n
        inc     GXQ + 1
@n:     inc     ent
        bne     @entry
        rts

; The accumulator at ap: 0, A (the integer part), and ap moved on 2.
PUTACC: ldy     #0
        pha
        lda     #$80                    ; a half: the result rounds
        sta     (ap),y
        pla
        iny
        sta     (ap),y
        lda     ap
        clc
        adc     #2
        sta     ap
        bcc     @d
        inc     ap + 1
@d:     rts
PUTSTEP: ldy    #0
        lda     q16
        sta     (sp),y
        iny
        lda     q16 + 1
        sta     (sp),y
        lda     sp
        clc
        adc     #2
        sta     sp
        bcc     @d
        inc     sp + 1
@d:     rts

; q16 = A (a signed difference, -15 to 15) x 256 / fsteps, signed.
STEPOF: sta     dsign
        cmp     #$80
        bcc     @pos
        eor     #$FF
        inc     a
@pos:   sta     dvd + 1                 ; |d| x 256, divided by the frames
        stz     dvd
        stz     q16
        stz     q16 + 1
        stz     rem
        phx
        ldx     #16
@bit:   asl     dvd
        rol     dvd + 1
        rol     rem
        bcs     @sub                    ; past 255: more than the frames
        lda     rem
        cmp     fsteps
        bcc     @zero
@sub:   lda     rem
        sbc     fsteps                  ; (carry set)
        sta     rem
        sec
        bra     @in
@zero:  clc
@in:    rol     q16
        rol     q16 + 1
        dex
        bne     @bit
        plx
        lda     dsign
        bpl     @done
        sec
        lda     #0
        sbc     q16
        sta     q16
        lda     #0
        sbc     q16 + 1
        sta     q16 + 1
@done:  rts

; A step: every accumulator moved on its step, the shadow its integer parts.
FSTEP:  lda     #<GX_PAL
        sta     GXP
        lda     #>GX_PAL
        sta     GXP + 1
        lda     #<facc
        sta     ap
        lda     #>facc
        sta     ap + 1
        lda     #<fstep
        sta     sp
        lda     #>fstep
        sta     sp + 1
        stz     ent
@entry: ldx     #0
@comp:  ldy     #0
        clc
        lda     (ap),y
        adc     (sp),y
        sta     (ap),y
        iny
        lda     (ap),y
        adc     (sp),y
        sta     (ap),y
        and     #$0F
        sta     c0,x
        lda     ap
        clc
        adc     #2
        sta     ap
        bcc     @a
        inc     ap + 1
@a:     lda     sp
        clc
        adc     #2
        sta     sp
        bcc     @s
        inc     sp + 1
@s:     inx
        cpx     #3
        bne     @comp
        lda     c0 + 1                  ; GGGGBBBB, then 0000RRRR
        asl     a
        asl     a
        asl     a
        asl     a
        ora     c0
        sta     (GXP)
        ldy     #1
        lda     c0 + 2
        sta     (GXP),y
        lda     GXP
        clc
        adc     #2
        sta     GXP
        bcc     @n
        inc     GXP + 1
@n:     inc     ent
        bne     @entry
        rts


        .bss
ent:    .res    2
c0:     .res    3
c1:     .res    3
dsign:  .res    1
dvd:    .res    2
rem:    .res    1
q16:    .res    2

; OOLIB's bitmaps (include/oolib.inc, "Graphics"; hw/oolib.md, L4): plot
; and point at 8, 4 and 1 bits a pixel, lines, rectangles, circles, a flood
; fill by spans (GW-BASIC's PAINT), rectangles copied and filled by
; function 83 on the card (DM_VCOPY, DM_VFILL), and double buffering. The
; pixels are reached by the CPU through W1 and aperture slot 1 (gxmem.s's
; GXPHYS), a byte at a time; a bitmap is in the program's card memory.
;
; The bitmaps (Technical Reference chapter 6): bitmap 256 is 256 x 240, a
; byte a pixel, 256 bytes a row; bitmap 512 is 512 x 480, 4 bits a pixel
; (the left one in the high 4), 256 bytes a row; mono is 640 x 480, a bit
; a pixel (the left one in bit 7), 80 bytes a row. Coordinates are words;
; anything off the bitmap isn't drawn, and reads 0.

        .setcpu "65C02"
        .macpack longbranch
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

STACK           = 255                  ; GX_FLOOD's seeds (fsp a byte)

        .bss
bmode:  .res    1                      ; GB_*
bunit:  .res    2                      ; the bitmap's first card page
colour: .res    1
sub:    .res    1                      ; the pixel in its byte: 4 bits, 0 the high; mono, its bit
px:     .res    2                      ; the pixel the routines draw: PADDR's
py:     .res    2
ldx_:   .res    2                      ; GX_LINE's
ldy_:   .res    2
lsx:    .res    1
lsy:    .res    1
lerr:   .res    2
le2:    .res    2
cx:     .res    2                      ; GX_CIRCLE's
cy:     .res    2
cd:     .res    2
lo:     .res    2                      ; a run's ends (GX_FRECT, GX_FLOOD)
hi:     .res    2
row:    .res    2
mtmp:   .res    2                      ; PCALC's, for mono
target: .res    1                      ; GX_FLOOD's
fsp:    .res    1
fny:    .res    2
inrun:  .res    1
full:   .res    1
fstack: .res    STACK * 4
ta:     .res    1                      ; TWO's offsets
tb:     .res    1
cw:     .res    2                      ; GX_BCOPY's
crows:  .res    2

        .code

; The bitmap's width and height, by GB_*.
        .pushseg
        .rodata
WLO:    .byte   <256, <512, <640
WHI:    .byte   >256, >512, >640
HLO:    .byte   <240, <480, <480
HHI:    .byte   >240, >480, >480
        .popseg

; A the bitmap (GB_*), GX_V its first card page.
GX_BMP: sta     bmode
        lda     GX_V
        sta     bunit
        lda     GX_V + 1
        sta     bunit + 1
        rts

; Carry set if (px, py) is off the bitmap.
OFF:    ldx     bmode
        lda     px
        cmp     WLO,x
        lda     px + 1
        sbc     WHI,x
        bcs     @off
        lda     py
        cmp     HLO,x
        lda     py + 1
        sbc     HHI,x
@off:   rts

; GX_A = the byte of (px, py), sub its place in it; carry set if it is off
; the bitmap. PADDR maps it too: GXP its address in W1.
PCALC:  jsr     OFF
        jcs     @no
        lda     bunit
        ldx     bunit + 1
        jsr     GX_VADDR
        lda     bmode
        cmp     #GB_MONO
        beq     @mono
        cmp     #GB_512
        beq     @four
        lda     px                      ; 8 bits: y x 256 + x
        ldx     py
        ldy     #0
        bra     @add
@four:  lda     px + 1                  ; 4 bits: y x 256 + x / 2
        lsr     a
        lda     px
        ror     a
        pha
        lda     px
        and     #1
        sta     sub
        pla
        ldx     py
        ldy     py + 1
        bra     @add
@mono:  lda     py                      ; mono: y x 80 + x / 8: y x 64 + y x 16
        sta     GXT
        lda     py + 1
        sta     GXT + 1
        ldx     #4
@x16:   asl     GXT
        rol     GXT + 1
        dex
        bne     @x16
        lda     GXT                     ; (y x 16) x 4 + y x 16
        sta     mtmp
        lda     GXT + 1
        sta     mtmp + 1
        asl     GXT
        rol     GXT + 1
        asl     GXT
        rol     GXT + 1
        clc
        lda     GXT
        adc     mtmp
        sta     GXT
        lda     GXT + 1
        adc     mtmp + 1
        sta     GXT + 1
        lda     px + 1                  ; + x / 8
        sta     mtmp + 1
        lda     px
        lsr     mtmp + 1
        ror     a
        lsr     mtmp + 1
        ror     a
        lsr     mtmp + 1
        ror     a
        clc
        adc     GXT
        pha
        lda     GXT + 1
        adc     #0
        tax
        lda     px
        and     #7
        sta     sub
        pla
        ldy     #0
@add:   clc                             ; GX_A += Y:X:A
        adc     GX_A
        sta     GX_A
        txa
        adc     GX_A + 1
        sta     GX_A + 1
        tya
        adc     GX_A + 2
        sta     GX_A + 2
        clc
@no:    rts
PADDR:  jsr     PCALC
        bcs     @no
        jmp     GXPHYS
@no:    rts

        .pushseg
        .rodata
MASK1:  .byte   $80, $40, $20, $10, $08, $04, $02, $01
        .popseg

; colour at (px, py).
PPLOT:  jsr     PADDR
        bcs     @no
        lda     bmode
        cmp     #GB_MONO
        beq     @mono
        cmp     #GB_512
        beq     @four
        lda     colour
        sta     (GXP)
@no:    rts
@four:  lda     colour
        and     #$0F
        ldx     sub
        bne     @low
        asl     a
        asl     a
        asl     a
        asl     a
        sta     GXT
        lda     (GXP)
        and     #$0F
        bra     @put
@low:   sta     GXT
        lda     (GXP)
        and     #$F0
@put:   ora     GXT
        sta     (GXP)
        rts
@mono:  ldx     sub
        lda     MASK1,x
        sta     GXT
        lda     colour
        lsr     a
        bcc     @clear
        lda     (GXP)
        ora     GXT
        sta     (GXP)
        rts
@clear: lda     GXT
        eor     #$FF
        and     (GXP)
        sta     (GXP)
        rts

; A = the colour at (px, py), 0 off the bitmap.
PPOINT: jsr     PADDR
        bcs     @zero
        lda     bmode
        cmp     #GB_MONO
        beq     @mono
        cmp     #GB_512
        beq     @four
        lda     (GXP)
        rts
@four:  lda     (GXP)
        ldx     sub
        bne     @low
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        rts
@low:   and     #$0F
        rts
@mono:  ldx     sub
        lda     MASK1,x
        and     (GXP)
        beq     @zero
        lda     #1
        rts
@zero:  lda     #0
        rts

; (px, py) = (GX_X, GX_Y).
FROMXY: lda     GX_X
        sta     px
        lda     GX_X + 1
        sta     px + 1
        lda     GX_Y
        sta     py
        lda     GX_Y + 1
        sta     py + 1
        rts

GX_PLOT:
        sta     colour
        jsr     FROMXY
        jmp     PPLOT

GX_POINT:
        jsr     FROMXY
        jmp     PPOINT

; From (GX_X, GX_Y) to (GX_X2, GX_Y2), Bresenham's way: dx = |x2 - x|, dy
; = -|y2 - y|, err = dx + dy; at each pixel e2 = 2 err: e2 >= dy steps x
; (err += dy), e2 <= dx steps y (err += dx).
GX_LINE:
        sta     colour
        jsr     FROMXY
        sec                             ; dx and sx
        lda     GX_X2
        sbc     px
        sta     ldx_
        lda     GX_X2 + 1
        sbc     px + 1
        sta     ldx_ + 1
        lda     #1
        sta     lsx
        bit     ldx_ + 1
        bpl     @dy
        lda     #$FF
        sta     lsx
        jsr     NEGX
@dy:    sec                             ; dy = -|y2 - y| and sy
        lda     GX_Y2
        sbc     py
        sta     ldy_
        lda     GX_Y2 + 1
        sbc     py + 1
        sta     ldy_ + 1
        lda     #1
        sta     lsy
        bit     ldy_ + 1
        bmi     @neg
        jsr     NEGY                    ; positive: made negative
        bra     @err
@neg:   lda     #$FF
        sta     lsy
@err:   clc
        lda     ldx_
        adc     ldy_
        sta     lerr
        lda     ldx_ + 1
        adc     ldy_ + 1
        sta     lerr + 1
@pixel: jsr     PPLOT
        lda     px                      ; the end?
        cmp     GX_X2
        bne     @step
        lda     px + 1
        cmp     GX_X2 + 1
        bne     @step
        lda     py
        cmp     GX_Y2
        bne     @step
        lda     py + 1
        cmp     GX_Y2 + 1
        beq     @done
@step:  lda     lerr                    ; e2 = 2 err
        asl     a
        sta     le2
        lda     lerr + 1
        rol     a
        sta     le2 + 1
        sec                             ; e2 >= dy: x on
        lda     le2
        sbc     ldy_
        lda     le2 + 1
        sbc     ldy_ + 1
        bvc     @v1
        eor     #$80
@v1:    bmi     @noy
        clc
        lda     lerr
        adc     ldy_
        sta     lerr
        lda     lerr + 1
        adc     ldy_ + 1
        sta     lerr + 1
        lda     lsx
        jsr     STEPX
@noy:   sec                             ; e2 <= dx: y on (dx - e2 >= 0)
        lda     ldx_
        sbc     le2
        lda     ldx_ + 1
        sbc     le2 + 1
        bvc     @v2
        eor     #$80
@v2:    bmi     @pixel
        clc
        lda     lerr
        adc     ldx_
        sta     lerr
        lda     lerr + 1
        adc     ldx_ + 1
        sta     lerr + 1
        lda     lsy
        jsr     STEPY
        jmp     @pixel
@done:  rts

NEGX:   sec
        lda     #0
        sbc     ldx_
        sta     ldx_
        lda     #0
        sbc     ldx_ + 1
        sta     ldx_ + 1
        rts
NEGY:   sec
        lda     #0
        sbc     ldy_
        sta     ldy_
        lda     #0
        sbc     ldy_ + 1
        sta     ldy_ + 1
        rts
; px += A (1 or -1), and py.
STEPX:  ldx     #px - px
        bra     STEP
STEPY:  ldx     #py - px
STEP:   cmp     #1
        beq     @inc
        lda     px,x
        bne     @dec
        dec     px + 1,x
@dec:   dec     px,x
        rts
@inc:   inc     px,x
        bne     @done
        inc     px + 1,x
@done:  rts

; lo, hi = the smaller and the larger of A/X's two words: GX_X and GX_X2
; (ORDERX), GX_Y and GX_Y2 (ORDERY).
ORDERX: lda     GX_X
        sta     lo
        lda     GX_X + 1
        sta     lo + 1
        lda     GX_X2
        sta     hi
        lda     GX_X2 + 1
        sta     hi + 1
        bra     ORDER
ORDERY: lda     GX_Y
        sta     lo
        lda     GX_Y + 1
        sta     lo + 1
        lda     GX_Y2
        sta     hi
        lda     GX_Y2 + 1
        sta     hi + 1
ORDER:  lda     hi
        cmp     lo
        lda     hi + 1
        sbc     lo + 1
        bcs     @ok
        ldx     lo
        lda     hi
        sta     lo
        stx     hi
        ldx     lo + 1
        lda     hi + 1
        sta     lo + 1
        stx     hi + 1
@ok:    rts

; px from lo to hi at py, each plotted (HRUN), or py from lo to hi at px
; (VRUN); nothing if hi < lo (a clip that left nothing: review R1).
HRUN:   jsr     EMPTY
        bcc     @done
        lda     lo
        sta     px
        lda     lo + 1
        sta     px + 1
@x:     jsr     PPLOT
        lda     px
        cmp     hi
        bne     @on
        lda     px + 1
        cmp     hi + 1
        beq     @done
@on:    inc     px
        bne     @x
        inc     px + 1
        bra     @x
@done:  rts
VRUN:   jsr     EMPTY
        bcc     @done
        lda     lo
        sta     py
        lda     lo + 1
        sta     py + 1
@y:     jsr     PPLOT
        lda     py
        cmp     hi
        bne     @on
        lda     py + 1
        cmp     hi + 1
        beq     @done
@on:    inc     py
        bne     @y
        inc     py + 1
        bra     @y
@done:  rts

; Carry clear if hi < lo: an empty run.
EMPTY:  lda     hi
        cmp     lo
        lda     hi + 1
        sbc     lo + 1
        rts

GX_RECT:
        sta     colour
        jsr     ORDERX                  ; the top and bottom rows
        lda     GX_Y
        sta     py
        lda     GX_Y + 1
        sta     py + 1
        jsr     HRUN
        jsr     ORDERX
        lda     GX_Y2
        sta     py
        lda     GX_Y2 + 1
        sta     py + 1
        jsr     HRUN
        jsr     ORDERY                  ; the left and right columns
        lda     GX_X
        sta     px
        lda     GX_X + 1
        sta     px + 1
        jsr     VRUN
        jsr     ORDERY
        lda     GX_X2
        sta     px
        lda     GX_X2 + 1
        sta     px + 1
        jmp     VRUN

; Filled: rows from the top. A bitmap 256 or 512 row is 256 bytes, so rows
; the full width are one run, a DM_VFILL of rows x 256 bytes; part rows of a
; bitmap 256 a DM_VFILL each; the rest pixel by pixel.
GX_FRECT:
        sta     colour
        jsr     ORDERXC
        lda     bmode
        cmp     #GB_MONO
        bne     @wide
        lda     lo                      ; mono: whole rows by DMA (80 bytes a row), else pixels
        ora     lo + 1
        jne     @plot
        lda     hi
        cmp     #<639
        jne     @plot
        lda     hi + 1
        cmp     #>639
        jne     @plot
        jmp     MONOBAND
@wide:
        lda     lo
        ora     lo + 1
        jne     @part
        ldx     bmode                   ; x from 0 to the width - 1?
        lda     hi
        clc
        adc     #1
        cmp     WLO,x
        jne     @part
        lda     hi + 1
        adc     #0
        cmp     WHI,x
        jne     @part
        jsr     ORDERYC                 ; whole rows: a fill of up to 128 rows x 256 bytes
        lda     lo
        sta     row
        lda     lo + 1
        sta     row + 1
        sec
        lda     hi
        sbc     lo
        sta     crows
        lda     hi + 1
        sbc     lo + 1
        sta     crows + 1
        bcc     @done                   ; nothing on the bitmap
        inc     crows
        bne     @band
        inc     crows + 1
@band:  lda     crows + 1               ; n = the rows left, 128 at most
        bne     @128
        lda     crows
        cmp     #128
        bcc     @n
@128:   lda     #128
@n:     sta     GXB + 6
        stz     GXB + 5
        stz     px
        stz     px + 1
        lda     row
        sta     py
        lda     row + 1
        sta     py + 1
        jsr     PCALC
        bcs     @done
        jsr     FILLB
        jsr     GXDMA
        lda     GXB + 6
        tax
        clc
        adc     row
        sta     row
        bcc     @rl
        inc     row + 1
@rl:    stx     GXT
        sec
        lda     crows
        sbc     GXT
        sta     crows
        bcs     @rc
        dec     crows + 1
@rc:    ora     crows + 1
        bne     @band
@done:  rts
@part:  lda     bmode                   ; bitmap 256: a fill a row
        cmp     #GB_256
        bne     @plot
        jsr     ORDERYC
        lda     lo
        sta     row
        lda     lo + 1
        sta     row + 1
@row:   jsr     ORDERXC
        lda     lo
        sta     px
        lda     lo + 1
        sta     px + 1
        lda     row
        sta     py
        lda     row + 1
        sta     py + 1
        jsr     PCALC
        bcs     @next
        sec
        lda     hi
        sbc     lo
        sta     GXB + 5                 ; the run's bytes (256 at most: the width)
        lda     hi + 1
        sbc     lo + 1
        sta     GXB + 6
        inc     GXB + 5
        bne     @len
        inc     GXB + 6
@len:   jsr     FILLB
        jsr     GXDMA
@next:  jsr     ORDERYC
        lda     row
        cmp     hi
        lda     row + 1
        sbc     hi + 1
        bcs     @end                    ; the last row done
        inc     row
        bne     @row
        inc     row + 1
        bra     @row
@end:   rts
@plot:  jsr     ORDERYC                 ; pixel by pixel
        lda     lo
        sta     row
        lda     lo + 1
        sta     row + 1
@prow:  lda     row
        sta     py
        lda     row + 1
        sta     py + 1
        jsr     ORDERXC
        jsr     HRUN
        jsr     ORDERYC
        lda     row
        cmp     hi
        lda     row + 1
        sbc     hi + 1
        bcs     @end
        inc     row
        bne     @prow
        inc     row + 1
        bra     @prow

; Mono's whole rows: one DM_VFILL of rows x 80 bytes, $00 or $FF.
MONOBAND:
        jsr     ORDERYC
        jsr     EMPTY                   ; rows all past the last: nothing
        bcs     @rows
        rts
@rows:  lda     lo
        sta     py
        lda     lo + 1
        sta     py + 1
        stz     px
        stz     px + 1
        jsr     PCALC
        bcs     @done
        sec                             ; the rows: hi - lo + 1, then x 80 = x 16 + x 64
        lda     hi
        sbc     lo
        sta     GXT
        lda     hi + 1
        sbc     lo + 1
        sta     GXT + 1
        inc     GXT
        bne     @x
        inc     GXT + 1
@x:     ldx     #4
@x16:   asl     GXT
        rol     GXT + 1
        dex
        bne     @x16
        lda     GXT
        sta     row
        lda     GXT + 1
        sta     row + 1
        asl     GXT
        rol     GXT + 1
        asl     GXT
        rol     GXT + 1
        clc
        lda     GXT
        adc     row
        sta     GXB + 5
        lda     GXT + 1
        adc     row + 1
        sta     GXB + 6
        lda     #DM_VFILL
        sta     GXB
        lda     colour
        and     #1
        beq     @b
        lda     #$FF
@b:     sta     GXB + 1
        ldx     #2
@a:     lda     GX_A,x
        sta     GXB + 2,x
        dex
        bpl     @a
        jmp     GXDMA
@done:  rts

; ORDERX and ORDERY with hi clipped to the bitmap's last column or row.
ORDERXC:
        jsr     ORDERX
        ldx     bmode
        lda     WLO,x
        sec
        sbc     #1
        sta     GXT
        lda     WHI,x
        sbc     #0
        bra     CLIP
ORDERYC:
        jsr     ORDERY
        ldx     bmode
        lda     HLO,x
        sec
        sbc     #1
        sta     GXT
        lda     HHI,x
        sbc     #0
CLIP:   sta     GXT + 1                 ; hi = the last if past it
        lda     GXT
        cmp     hi
        lda     GXT + 1
        sbc     hi + 1
        bcs     @ok
        lda     GXT
        sta     hi
        lda     GXT + 1
        sta     hi + 1
@ok:    rts

; GXB a DM_VFILL of the colour (doubled for 4 bits) at GX_A; its length
; at GXB + 5-6 already.
FILLB:  lda     #DM_VFILL
        sta     GXB
        lda     colour
        ldx     bmode
        cpx     #GB_512
        bne     @byte
        and     #$0F
        sta     GXT
        asl     a
        asl     a
        asl     a
        asl     a
        ora     GXT
@byte:  sta     GXB + 1
        ldx     #2
@a:     lda     GX_A,x
        sta     GXB + 2,x
        dex
        bpl     @a
        rts

; About (GX_X, GX_Y), radius GX_R: the midpoint way. x = r, y = 0, d = 1 -
; r; while x >= y, the eight points (cx +- x, cy +- y) and (cx +- y, cy +-
; x); y + 1, then d < 0: d += 2y + 1, else x - 1 and d += 2(y - x) + 1.
GX_CIRCLE:
        sta     colour
        lda     GX_R
        sta     ldx_                    ; x
        lda     GX_R + 1
        sta     ldx_ + 1
        stz     ldy_                    ; y
        stz     ldy_ + 1
        sec
        lda     #1
        sbc     GX_R
        sta     cd
        lda     #0
        sbc     GX_R + 1
        sta     cd + 1
@loop:  lda     ldx_                    ; x >= y?
        cmp     ldy_
        lda     ldx_ + 1
        sbc     ldy_ + 1
        bcc     @done
        jsr     EIGHT
        inc     ldy_
        bne     @d
        inc     ldy_ + 1
@d:     bit     cd + 1
        bpl     @pos
        lda     ldy_                    ; d += 2y + 1
        asl     a
        tax
        lda     ldy_ + 1
        rol     a
        tay
        txa
        sec
        adc     cd
        sta     cd
        tya
        adc     cd + 1
        sta     cd + 1
        bra     @loop
@pos:   lda     ldx_                    ; x - 1, d += 2(y - x) + 1
        bne     @x
        dec     ldx_ + 1
@x:     dec     ldx_
        sec
        lda     ldy_
        sbc     ldx_
        tax
        lda     ldy_ + 1
        sbc     ldx_ + 1
        tay
        txa
        asl     a
        tax
        tya
        rol     a
        tay
        txa
        sec
        adc     cd
        sta     cd
        tya
        adc     cd + 1
        sta     cd + 1
        bra     @loop
@done:  rts

; The eight points of (ldx_, ldy_) about (GX_X, GX_Y).
EIGHT:  ldx     #ldx_ - ldx_
        ldy     #ldy_ - ldx_
        jsr     TWO                     ; (cx +- x, cy +- y)
        ldx     #ldy_ - ldx_
        ldy     #ldx_ - ldx_
        ; fall through: (cx +- y, cy +- x)
; (cx + a, cy + b), (cx - a, cy + b), (cx + a, cy - b), (cx - a, cy - b):
; a the word at ldx_ + X, b at ldx_ + Y.
TWO:    stx     ta
        sty     tb
        jsr     @pair                   ; cy + b
        ldy     tb
        sec
        lda     GX_Y
        sbc     ldx_,y
        sta     py
        lda     GX_Y + 1
        sbc     ldx_ + 1,y
        sta     py + 1
        bra     @xs
@pair:  ldy     tb
        clc
        lda     GX_Y
        adc     ldx_,y
        sta     py
        lda     GX_Y + 1
        adc     ldx_ + 1,y
        sta     py + 1
@xs:    ldx     ta
        clc
        lda     GX_X
        adc     ldx_,x
        sta     px
        lda     GX_X + 1
        adc     ldx_ + 1,x
        sta     px + 1
        jsr     PPLOT
        ldx     ta
        sec
        lda     GX_X
        sbc     ldx_,x
        sta     px
        lda     GX_X + 1
        sbc     ldx_ + 1,x
        sta     px + 1
        jmp     PPLOT

; The region of (GX_X, GX_Y)'s colour about it, filled A, a span at a time:
; each seed taken, its run on its row found left and right and filled, and
; the start of each run of the old colour in the rows above and below it
; within the span made a seed. Carry set if the seeds overflowed.
GX_FLOOD:
        sta     colour
        stz     full
        jsr     GX_POINT
        sta     target
        cmp     colour
        jeq     @done                   ; nothing to change
        jsr     OFF
        jcs     @done
        stz     fsp
        jsr     PUSH                    ; (px, py): FROMXY's
@pop:   lda     fsp
        jeq     @end
        jsr     POP
        jsr     PPOINT
        cmp     target
        bne     @pop
@left:  lda     px                      ; xl: left while the old colour
        ora     px + 1
        beq     @lend
        jsr     DECX
        jsr     PPOINT
        cmp     target
        beq     @left
        jsr     INCX
@lend:  lda     px
        sta     lo
        lda     px + 1
        sta     lo + 1
@right: jsr     INCX                    ; xr: right while the old colour
        jsr     OFF
        bcs     @rend
        jsr     PPOINT
        cmp     target
        beq     @right
@rend:  jsr     DECX
        lda     px
        sta     hi
        lda     px + 1
        sta     hi + 1
        jsr     HRUN                    ; the span filled
        lda     py
        sta     row
        lda     py + 1
        sta     row + 1
        lda     row                     ; the row above, if there is one
        ora     row + 1
        beq     @below
        sec
        lda     row
        sbc     #1
        sta     fny
        lda     row + 1
        sbc     #0
        sta     fny + 1
        jsr     SEEDS
@below: clc
        lda     row
        adc     #1
        sta     fny
        lda     row + 1
        adc     #0
        sta     fny + 1
        jsr     SEEDS
        jmp     @pop
@end:   lda     full
        cmp     #1                      ; carry set if it overflowed
@done:  rts

; Row fny from lo to hi: a seed at the start of each run of the old colour.
SEEDS:  lda     fny
        sta     py
        lda     fny + 1
        sta     py + 1
        lda     lo
        sta     px
        lda     lo + 1
        sta     px + 1
        stz     inrun
        jsr     OFF                     ; a row off the bitmap: none
        bcs     @done
@x:     jsr     PPOINT
        cmp     target
        bne     @not
        lda     inrun
        bne     @next
        inc     inrun
        jsr     PUSH
        bra     @next
@not:   stz     inrun
@next:  lda     px
        cmp     hi
        bne     @on
        lda     px + 1
        cmp     hi + 1
        beq     @done
@on:    jsr     INCX
        bra     @x
@done:  rts

INCX:   inc     px
        bne     @d
        inc     px + 1
@d:     rts
DECX:   lda     px
        bne     @d
        dec     px + 1
@d:     dec     px
        rts

; (px, py) onto the seeds, or full set if there's no room.
PUSH:   lda     fsp
        cmp     #<STACK
        bne     @room
        lda     #1
        sta     full
        rts
@room:  jsr     FSLOT
        ldy     #0
        lda     px
        sta     (GXQ),y
        iny
        lda     px + 1
        sta     (GXQ),y
        iny
        lda     py
        sta     (GXQ),y
        iny
        lda     py + 1
        sta     (GXQ),y
        inc     fsp
        rts
POP:    dec     fsp
        jsr     FSLOT
        ldy     #0
        lda     (GXQ),y
        sta     px
        iny
        lda     (GXQ),y
        sta     px + 1
        iny
        lda     (GXQ),y
        sta     py
        iny
        lda     (GXQ),y
        sta     py + 1
        rts
; GXQ = seed fsp's slot.
FSLOT:  lda     fsp
        stz     GXQ + 1
        asl     a
        rol     GXQ + 1
        asl     a
        rol     GXQ + 1
        clc
        adc     #<fstack
        sta     GXQ
        lda     GXQ + 1
        adc     #>fstack
        sta     GXQ + 1
        rts
.assert STACK < 256, error, "PUSH's room: fsp a byte"

; The rectangle at (GX_X, GX_Y), GX_X2 x GX_Y2, to (GX_DX, GX_DY): a
; DM_VCOPY a row, the rows from the bottom when it moves down (so a copy
; over itself downward is whole).
GX_BCOPY:
        lda     bmode
        cmp     #GB_MONO
        jeq     @out                    ; mono: no copy (GE_BOUNDS, review R3)
        cmp     #GB_512
        bne     @w
        lda     GX_X                    ; 4 bits: even x and widths
        ora     GX_DX
        ora     GX_X2
        lsr     a
        jcs     @out                    ; odd x or width at 4 bits: GE_BOUNDS
@w:     lda     GX_X2                   ; a width or a height of 0: nothing (never a length of 0 to
        ora     GX_X2 + 1               ; function 83, which is 65,536: review F2)
        jeq     @ok
        lda     GX_Y2
        ora     GX_Y2 + 1
        jeq     @ok
        ldx     bmode                   ; both rectangles inside the bitmap, or refused (GE_BOUNDS)
        lda     GX_X
        ldy     GX_X + 1
        jsr     OVERW
        jcs     @out
        lda     GX_DX
        ldy     GX_DX + 1
        jsr     OVERW
        jcs     @out
        lda     GX_Y
        ldy     GX_Y + 1
        jsr     OVERH
        jcs     @out
        lda     GX_DY
        ldy     GX_DY + 1
        jsr     OVERH
        jcs     @out
        lda     GX_X2                   ; the row's bytes
        sta     cw
        lda     GX_X2 + 1
        sta     cw + 1
        lda     bmode
        cmp     #GB_512
        bne     @rows
        lsr     cw + 1
        ror     cw
@rows:  lda     GX_Y2
        sta     crows
        lda     GX_Y2 + 1
        sta     crows + 1
        ora     crows
        jeq     @ok
        stz     row                     ; the row: 0 up, or from the last down
        stz     row + 1
        lda     GX_DY                   ; down (DY > Y): from the last
        cmp     GX_Y
        lda     GX_DY + 1
        sbc     GX_Y + 1
        bcc     @each
        sec
        lda     crows
        sbc     #1
        sta     row
        lda     crows + 1
        sbc     #0
        sta     row + 1
@each:  clc                             ; the source's byte
        lda     GX_X
        sta     px
        lda     GX_X + 1
        sta     px + 1
        lda     GX_Y
        adc     row
        sta     py
        lda     GX_Y + 1
        adc     row + 1
        sta     py + 1
        jsr     PCALC
        jcs     @no
        ldx     #2
@s:     lda     GX_A,x
        sta     GXB + 1,x
        dex
        bpl     @s
        clc                             ; the destination's
        lda     GX_DX
        sta     px
        lda     GX_DX + 1
        sta     px + 1
        lda     GX_DY
        adc     row
        sta     py
        lda     GX_DY + 1
        adc     row + 1
        sta     py + 1
        jsr     PCALC
        bcs     @no
        ldx     #2
@d:     lda     GX_A,x
        sta     GXB + 4,x
        dex
        bpl     @d
        lda     cw
        sta     GXB + 7
        lda     cw + 1
        sta     GXB + 8
        lda     #DM_VCOPY
        sta     GXB
        jsr     GXDMA
        bcs     @no
        lda     GX_DY                   ; the next row
        cmp     GX_Y
        lda     GX_DY + 1
        sbc     GX_Y + 1
        bcc     @up
        lda     row                     ; down: to row 0
        ora     row + 1
        beq     @ok
        lda     row
        bne     @dec
        dec     row + 1
@dec:   dec     row
        jmp     @each
@up:    inc     row
        bne     @cmp
        inc     row + 1
@cmp:   lda     row
        cmp     crows
        jne     @each
        lda     row + 1
        cmp     crows + 1
        jne     @each
@ok:    clc
        rts
@no:    sec
        rts
@out:   lda     #GE_BOUNDS
        sec
        rts

; Carry set if A/Y + GX_X2 (OVERW) runs past the bitmap's width, or A/Y +
; GX_Y2 (OVERH) past its height (bmode in X, kept).
OVERW:  clc
        adc     GX_X2
        sta     GXT
        tya
        adc     GX_X2 + 1
        sta     GXT + 1
        bcs     @past
        lda     WLO,x                   ; the width - the end: a borrow if past it
        cmp     GXT
        lda     WHI,x
        sbc     GXT + 1
        bcc     @past
        clc
        rts
@past:  sec
        rts
OVERH:  clc
        adc     GX_Y2
        sta     GXT
        tya
        adc     GX_Y2 + 1
        sta     GXT + 1
        bcs     @past
        lda     HLO,x
        cmp     GXT
        lda     HHI,x
        sbc     GXT + 1
        bcc     @past
        clc
        rts
@past:  sec
        rts

; OOLIB's text on a bitmap (include/oolib.inc, "Graphics"; hw/oolib.md,
; L4): an 8 x 16 font, 256 characters of 16 bytes (the leftmost pixel in
; bit 7), drawn a pixel at a time by gxbmp.s's GX_PLOT on the bitmap
; GX_BMP named. The console's font (card $000400, OODOS's) is read a
; character at a time, its 16 bytes copied by function 83's DM_VREAD into
; the library's own (OODOS's card memory is a source a program may read,
; never a destination, and slot 0's block 32 isn't in the W1 map); so the
; program keeps 4 KB of its memory below $4000, where the W1 contract
; wants its data.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

CONSOLE_FONT    = VM_FONT              ; the console's font, on the card
.assert <CONSOLE_FONT = 0, error, "CONGLYPH adds to the font's middle byte"

        .bss
font:   .res    2                      ; the font in use, a CPU address; 0 the console's
glyph:  .res    16                     ; the console's character, as copied
gphys:  .res    3                      ; glyph's physical address
ch:     .res    1
frow:   .res    1
bits:   .res    1
sx:     .res    2
sy:     .res    2
tp:     .res    2                      ; the string

        .code

; A/X a font in the program's memory, or 0: the console's, copied.
GX_FONT:
        sta     font
        stx     font + 1
        lda     #<glyph
        ldx     #>glyph
        jsr     GXCPU
        ldx     #2
@g:     lda     GX_A,x
        sta     gphys,x
        dex
        bpl     @g
        clc
        rts

; A/X a string ending in 0, at (GX_X, GX_Y): GX_FG, and GX_BG where
; GX_OPAQUE says.
GX_TEXT:
        sta     tp
        stx     tp + 1
@char:  lda     tp
        sta     GXQ
        lda     tp + 1
        sta     GXQ + 1
        lda     (GXQ)
        beq     @done
        sta     ch
        jsr     GLYPH
        inc     tp
        bne     @next
        inc     tp + 1
@next:  clc
        lda     GX_X
        adc     #8
        sta     GX_X
        bcc     @char
        inc     GX_X + 1
        bra     @char
@done:  rts

; Character ch at (GX_X, GX_Y), GX_X and GX_Y kept.
GLYPH:  lda     GX_X
        sta     sx
        lda     GX_X + 1
        sta     sx + 1
        lda     GX_Y
        sta     sy
        lda     GX_Y + 1
        sta     sy + 1
        stz     frow
        lda     font                    ; the console's: its 16 bytes copied first
        ora     font + 1
        bne     @row
        jsr     CONGLYPH
@row:   lda     font
        ora     font + 1
        bne     @own
        ldx     frow
        lda     glyph,x
        bra     @byte
@own:   lda     ch                      ; the row's byte: font + ch x 16 + row
        sta     GXQ
        stz     GXQ + 1
        ldx     #4
@x16:   asl     GXQ
        rol     GXQ + 1
        dex
        bne     @x16
        lda     GXQ
        ora     frow
        clc
        adc     font
        sta     GXQ
        lda     GXQ + 1
        adc     font + 1
        sta     GXQ + 1
        lda     (GXQ)
@byte:  sta     bits
        ldx     #8
@pix:   asl     bits
        bcc     @bg
        lda     GX_FG
        bra     @plot
@bg:    lda     GX_OPAQUE
        beq     @skip
        lda     GX_BG
@plot:  phx
        jsr     GX_PLOT
        plx
@skip:  inc     GX_X
        bne     @nx
        inc     GX_X + 1
@nx:    dex
        bne     @pix
        lda     sx                      ; the next row, from the left
        sta     GX_X
        lda     sx + 1
        sta     GX_X + 1
        inc     GX_Y
        bne     @ny
        inc     GX_Y + 1
@ny:    inc     frow
        lda     frow
        cmp     #16
        bne     @row
        lda     sy
        sta     GX_Y
        lda     sy + 1
        sta     GX_Y + 1
        rts

; The console's character ch into glyph: 16 bytes from the console's font
; + ch x 16.
CONGLYPH:
        lda     ch
        stz     GXB + 2
        asl     a
        rol     GXB + 2
        asl     a
        rol     GXB + 2
        asl     a
        rol     GXB + 2
        asl     a
        rol     GXB + 2
        sta     GXB + 1
        lda     GXB + 2
        clc
        adc     #>CONSOLE_FONT
        sta     GXB + 2
        lda     #^CONSOLE_FONT
        sta     GXB + 3
        ldx     #2
@d:     lda     gphys,x
        sta     GXB + 4,x
        dex
        bpl     @d
        lda     #16
        sta     GXB + 7
        stz     GXB + 8
        lda     #DM_VREAD
        sta     GXB
        jmp     GXDMA

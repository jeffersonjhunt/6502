; OOUI's glyphs (hw/ooui.md, "Text backend: resources"; S3-0): the
; console's font copied to OOUI's card memory, and OOUI's own glyphs (the
; bars, the check box, the radio button, a folder, a drive) written over
; it. ui_open's only, once, so block C's (XC_GLYPHS), not the core's: S3
; moved it here from uidraw.s to leave block A room.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .export GLYPHS

        .segment "CBSS"                 ; block C's own (S3-0), not UISTATE
gi:     .res    1
gn:     .res    1

        .segment "CCODE"

; The console's font copied to OOUI's, and OOUI's glyphs written over it:
; carry set if the copy was refused.
GLYPHS: lda     #<VM_FONT
        sta     GX_A
        lda     #>VM_FONT
        sta     GX_A + 1
        lda     #^VM_FONT
        sta     GX_A + 2
        stz     GX_C
        lda     upage
        clc
        adc     #UI_OFS_FONT
        sta     GX_C + 1
        lda     upage + 1
        adc     #0
        sta     GX_C + 2
        stz     GX_V
        lda     #>4096
        sta     GX_V + 1
        jsr     GX_VCOPY
        bcs     @r
        jsr     MAPW1
        lda     #1                      ; the vertical bars, 1-15 sixteenths from the bottom
        sta     gi
@v:     lda     gi
        jsr     GLYPHAT
        ldy     #15
@vr:    tya                             ; row y lit if y >= 16 - n
        clc
        adc     gi
        cmp     #16
        lda     #0
        bcc     @vb
        lda     #$FF
@vb:    sta     (UIP),y
        dey
        bpl     @vr
        inc     gi
        lda     gi
        cmp     #16
        bne     @v
        stz     gi                      ; the horizontal bars, 1-7 eighths from the left
@h:     ldx     gi
        lda     HCODES,x
        jsr     GLYPHAT
        ldx     gi
        lda     HMASK,x
        ldy     #15
@hr:    sta     (UIP),y
        dey
        bpl     @hr
        inc     gi
        lda     gi
        cmp     #7
        bne     @h
        stz     gi                      ; the drawn ones
        stz     gn
@d:     ldx     gi
        lda     DCODES,x
        jsr     GLYPHAT
        ldy     #0
        ldx     gn
@dr:    lda     GLYPHDATA,x
        sta     (UIP),y
        inx
        iny
        cpy     #16
        bne     @dr
        stx     gn
        inc     gi
        lda     gi
        cmp     #DCODES_LEN
        bne     @d
        clc
@r:     rts

; UIP = character A's 16 bytes in OOUI's font, through W1.
GLYPHAT:
        stz     UIP + 1
        asl     a
        rol     UIP + 1
        asl     a
        rol     UIP + 1
        asl     a
        rol     UIP + 1
        asl     a
        rol     UIP + 1
        clc
        adc     mapB
        sta     UIP
        lda     UIP + 1
        adc     mapB + 1
        adc     #UI_OFS_FONT
        sta     UIP + 1
        rts

        .segment "CRODATA"
HCODES: .byte   $12, $13, $14, $15, $16, $17, $1C
HMASK:  .byte   $80, $C0, $E0, $F0, $F8, $FC, $FE

; A glyph from 16 rows of 8: '#' lit.
.macro GROW s
        .local b
        b .set 0
        .repeat 8, i
                .if .strat(s, i) = '#'
                        b .set b | ($80 >> i)
                .endif
        .endrepeat
        .byte   b
.endmacro
DCODES: .byte   UG_BOX, UG_BOXON, UG_RADIO, UG_RADIOON, UG_FOLDER, UG_DRIVE
DCODES_LEN = * - DCODES
GLYPHDATA:
        ; UG_BOX
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "#######."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#######."
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_BOXON
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "#######."
        GROW    "#.....#."
        GROW    "#....##."
        GROW    "#...#.#."
        GROW    "##.#..#."
        GROW    "#.##..#."
        GROW    "#..#..#."
        GROW    "#######."
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_RADIO
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "..###..."
        GROW    ".#...#.."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    ".#...#.."
        GROW    "..###..."
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_RADIOON
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "..###..."
        GROW    ".#...#.."
        GROW    "#..#..#."
        GROW    "#.###.#."
        GROW    "#..#..#."
        GROW    ".#...#.."
        GROW    "..###..."
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_FOLDER
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "###....."
        GROW    "#..#...."
        GROW    "#...####"
        GROW    "#......#"
        GROW    "#......#"
        GROW    "#......#"
        GROW    "#......#"
        GROW    "########"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_DRIVE
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "########"
        GROW    "#......#"
        GROW    "#......#"
        GROW    "#....#.#"
        GROW    "#......#"
        GROW    "########"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
.assert * - GLYPHDATA = DCODES_LEN * 16, error, "a glyph is 16 rows"

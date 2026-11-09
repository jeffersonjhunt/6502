; OOLIB's asset files (include/oolib.inc, "Graphics"; hw/oolib.md, L4,
; question 8: format 3, oodos/tools/mkasset.py makes them): the header read
; with function 33 (record 0), then each part loaded by function 83's
; DM_LOAD into its units, straight from the card. A part that names no unit
; ($FF) gets units allocated (GX_ALLOC) and its header part says which.
; A sprite table's entries name their images by the sprite images' part:
; once both are loaded, each entry's image number is moved on by that
; part's unit's address / 64 (a unit is 16 images' worth).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

        .bss
GX_HDR: .res    128                    ; record 0: the header and the parts
fcbp:   .res    2
part:   .res    1                      ; the part, 0-6
lblk:   .res    11                     ; DM_LOAD's block
imgu:   .res    1                      ; the sprite images' unit ($FF none)
tabu:   .res    1                      ; and the table's

        .code

; A/X an FCB: carry clear, or set and A an error.
GX_ASSET:
        sta     fcbp
        stx     fcbp + 1
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #4
        bcs     @file                   ; $FF: no such file
        lda     #<GX_HDR                ; record 0 to GX_HDR
        ldx     #>GX_HDR
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     fcbp
        sta     GXQ
        lda     fcbp + 1
        sta     GXQ + 1
        ldy     #33
        lda     #0
        sta     (GXQ),y
        iny
        sta     (GXQ),y
        iny
        sta     (GXQ),y
        lda     fcbp
        ldx     fcbp + 1
        ldy     #F_READRAND
        jsr     BDOS
        pha
        lda     #<BP_DMA                ; the DMA address as the CCP left it
        ldx     #>BP_DMA
        ldy     #F_DMAOFF
        jsr     BDOS
        pla
        cmp     #0
        bne     @file
        lda     GX_HDR + AH_MAGIC       ; 'OGX', format 3, 1-7 parts
        cmp     #'O'
        bne     @format
        lda     GX_HDR + AH_MAGIC + 1
        cmp     #'G'
        bne     @format
        lda     GX_HDR + AH_MAGIC + 2
        cmp     #'X'
        bne     @format
        lda     GX_HDR + AH_FORMAT
        cmp     #ASSET_FORMAT
        bne     @format
        lda     GX_HDR + AH_PARTS
        beq     @format
        cmp     #AP_MAX + 1
        bcs     @format
        lda     #$FF
        sta     imgu
        sta     tabu
        stz     part
@part:  jsr     PARTP                   ; GXP: the part's 16 bytes
        jsr     LOADP
        bcs     @err
        inc     part
        lda     part
        cmp     GX_HDR + AH_PARTS
        bne     @part
        jmp     SPRFIX
@file:  lda     #GE_FILE
        sec
        rts
@format:
        lda     #GE_FORMAT
        sec
@err:   rts

; GXP = part `part` in GX_HDR.
PARTP:  lda     part
        asl     a
        asl     a
        asl     a
        asl     a
        clc
        adc     #<(GX_HDR + AH_LEN)
        sta     GXP
        lda     #>(GX_HDR + AH_LEN)
        adc     #0
        sta     GXP + 1
        rts

; The part at GXP loaded: its unit allocated if it says $FF (its bytes'
; units, rounded up), then DM_LOAD of its records there.
LOADP:  ldy     #AP_UNIT
        lda     (GXP),y
        cmp     #$FF
        bne     @unit
        ldy     #AP_BYTES + 1           ; units = (bytes + 1,023) / 1,024
        lda     (GXP),y
        sta     GXT
        iny
        lda     (GXP),y
        sta     GXT + 1
        ldy     #AP_BYTES               ; any bytes past whole units?
        lda     GXT
        and     #$03
        ora     (GXP),y
        sta     GXQ
        lda     GXT                     ; bytes >> 10, + 1 for a part unit
        lsr     GXT + 1
        ror     a
        lsr     GXT + 1
        ror     a
        ldx     GXQ
        beq     @whole
        inc     a
@whole: cmp     #0
        bne     @n
        lda     #1
@n:     jsr     GX_ALLOC
        bcc     @got
        rts
@got:   ldy     #AP_UNIT
        sta     (GXP),y
@unit:  ldy     #AP_TYPE                ; the sprite parts' units, for SPRFIX
        lda     (GXP),y
        cmp     #AT_SPRITES
        bne     @tab
        ldy     #AP_UNIT
        lda     (GXP),y
        sta     imgu
@tab:   ldy     #AP_TYPE
        lda     (GXP),y
        cmp     #AT_SPRTABLE
        bne     @load
        ldy     #AP_UNIT
        lda     (GXP),y
        sta     tabu
@load:  ldy     #AP_UNIT
        lda     (GXP),y
        jsr     GX_UNIT
        lda     #DM_LOAD
        sta     lblk
        lda     fcbp
        sta     lblk + 1
        lda     fcbp + 1
        sta     lblk + 2
        ldy     #AP_RECORD
        lda     (GXP),y
        sta     lblk + 3
        iny
        lda     (GXP),y
        sta     lblk + 4
        stz     lblk + 5
        ldy     #AP_RECORDS
        lda     (GXP),y
        sta     lblk + 6
        iny
        lda     (GXP),y
        sta     lblk + 7
        ldx     #2
@a:     lda     GX_A,x
        sta     lblk + 8,x
        dex
        bpl     @a
        lda     lblk + 6                ; 512 records a call at most
        ora     lblk + 7
        beq     @done
@call:  lda     lblk + 7
        cmp     #2
        bcc     @last
        bne     @big
        lda     lblk + 6
        beq     @last                   ; exactly 512
@big:   lda     #<512                   ; 512 now, the rest after
        sta     GXT
        lda     #>512
        sta     GXT + 1
        lda     lblk + 6
        sec
        sbc     #<512
        sta     rest
        lda     lblk + 7
        sbc     #>512
        sta     rest + 1
        stz     lblk + 6
        lda     #>512
        sta     lblk + 7
        jsr     ONE
        bcs     @no
        lda     rest                    ; on: 512 records (64 KB) further
        sta     lblk + 6
        lda     rest + 1
        sta     lblk + 7
        clc
        lda     lblk + 4
        adc     #>512
        sta     lblk + 4
        inc     lblk + 10               ; 64 KB on: the bank
        lda     lblk + 3                ; the record: + 512
        bra     @call
@last:  jsr     ONE
        bcs     @no
@done:  clc
@no:    rts

; DM_LOAD with lblk: carry set unless A = 0.
ONE:    lda     #<lblk
        ldx     #>lblk
        ldy     #O_DMA
        jsr     BDOS
        cmp     #0
        beq     @ok
        lda     #GE_FILE
        sec
        rts
@ok:    clc
        rts

; The sprite table's 64 entries' images moved on by the images' unit x 16
; (a unit is 1,024 bytes, an image number 64): the table copied into the
; library's memory by DM_COPY, changed there and copied back (its unit may
; be 13-15, in block 32, which W1 never has).
SPRFIX: lda     imgu
        cmp     #$FF
        beq     @done
        lda     tabu
        cmp     #$FF
        beq     @done
        jsr     TABIN                   ; the card's table to tbuf
        bcs     @no
        lda     imgu                    ; the step: unit x 16, 12 bits
        stz     GXT + 1
        ldx     #4
@x16:   asl     a
        rol     GXT + 1
        dex
        bne     @x16
        sta     GXT
        lda     #<tbuf
        sta     GXP
        lda     #>tbuf
        sta     GXP + 1
        ldx     #64
        ldy     #0
@entry: clc
        lda     (GXP),y
        adc     GXT
        sta     (GXP),y
        iny
        lda     (GXP),y
        pha
        and     #$80                    ; its 8-bit flag kept
        sta     GXQ
        pla
        adc     GXT + 1
        and     #$0F
        ora     GXQ
        sta     (GXP),y
        tya
        clc
        adc     #7
        tay
        bne     @n
        inc     GXP + 1
@n:     dex
        bne     @entry
        jmp     TABOUT
@done:  clc
@no:    rts

; DM_COPY of the 512-byte table between unit tabu and tbuf: TABIN in,
; TABOUT back.
TABIN:  jsr     TABSIDES
        jmp     GXDMA
TABOUT: jsr     TABSIDES
        ldx     #2                      ; the other way: the two sides swapped
@swap:  lda     GXB + 1,x
        pha
        lda     GXB + 4,x
        sta     GXB + 1,x
        pla
        sta     GXB + 4,x
        dex
        bpl     @swap
        jmp     GXDMA
TABSIDES:
        lda     #<tbuf
        ldx     #>tbuf
        jsr     GXCPU
        ldx     #2
@d:     lda     GX_A,x
        sta     GXB + 4,x
        dex
        bpl     @d
        lda     tabu
        jsr     GX_UNIT
        ldx     #2
@s:     lda     GX_A,x
        sta     GXB + 1,x
        dex
        bpl     @s
        stz     GXB + 7
        lda     #>512
        sta     GXB + 8
        lda     #DM_COPY
        sta     GXB
        rts

        .bss
rest:   .res    2
tbuf:   .res    512                    ; SPRFIX's copy of the table

; OOLIB's asset files (include/oolib.inc, "Graphics"; hw/oolib.md, L4,
; question 8: format 3, oodos/tools/mkasset.py makes them): the header read
; with function 33 (record 0), then each part loaded by function 83's
; DM_LOAD into its units, straight from the card. A part that names no unit
; ($FF) gets units allocated (GX_ALLOC) and its header part says which.
; A sprite table's entries name their images by the sprite images' part:
; once both are loaded, each entry's image number is moved on by that
; part's unit's address / 64 (a unit is 16 images' worth).

        .setcpu "65C02"
        .macpack longbranch
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
        jcs     @file                   ; $FF: no such file
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
        ldx     #AP_MAX - 1             ; no part's units taken yet
@none:  stz     pcount,x
        dex
        bpl     @none
        stz     part
@part:  jsr     PARTP                   ; GXP: the part's 16 bytes
        jsr     LOADP
        bcs     @undo
        inc     part
        lda     part
        cmp     GX_HDR + AH_PARTS
        bne     @part
        jsr     SPRFIX
        bcs     @undo
        rts
@undo:  pha                             ; a part refused: every unit the file took freed
        jsr     FREEALL
        pla
        sec
        rts
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

; The units every part took (pcount), freed.
FREEALL:
        stz     part
@p:     ldx     part
        lda     pcount,x
        beq     @n
        tax
        phx
        jsr     PARTP
        ldy     #AP_UNIT
        lda     (GXP),y
        plx
        jsr     GX_FREE
@n:     inc     part
        lda     part
        cmp     #AP_MAX
        bne     @p
        rts

; The part at GXP checked against what the library needs (review F5: the
; file is untrusted, whatever mkasset.py's check said): a type of 1-8,
; its first record a multiple of 8 and not 0, 1 to 131,072 bytes, its
; records the bytes' (rounded up); then its units, its bytes' rounded up
; to 1 KB: allocated if it says $FF, else taken where it says (13-255, the
; run inside 256, none of it taken already); then DM_LOAD of its records
; there. Carry set and A the error.
LOADP:  ldy     #AP_TYPE
        lda     (GXP),y
        beq     @bad
        cmp     #AT_RAW + 1
        bcs     @bad
        ldy     #AP_RECORD              ; on 1 KB of the file, past record 0's KB
        lda     (GXP),y
        tax
        and     #7
        bne     @bad
        iny
        txa
        ora     (GXP),y
        beq     @bad
        ldy     #AP_BYTES + 2           ; 1 to $20000 bytes
        lda     (GXP),y
        cmp     #3
        bcs     @bad
        cmp     #2
        bne     @nz
        dey
        lda     (GXP),y
        dey
        ora     (GXP),y
        bne     @bad
        bra     @recs
@nz:    ldy     #AP_BYTES
        lda     (GXP),y
        iny
        ora     (GXP),y
        iny
        ora     (GXP),y
        beq     @bad
@recs:  ldy     #AP_BYTES               ; records = (bytes + 127) / 128
        clc
        lda     (GXP),y
        adc     #127
        sta     GXT
        iny
        lda     (GXP),y
        adc     #0
        sta     GXT + 1
        iny
        lda     (GXP),y
        adc     #0
        sta     GXQ                     ; >> 7: the low byte (b1 << 1 | b0 >> 7), the high (b2 << 1 | b1 >> 7)
        asl     GXT
        rol     GXT + 1
        rol     GXQ
        ldy     #AP_RECORDS
        lda     (GXP),y
        cmp     GXT + 1
        bne     @bad
        iny
        lda     (GXP),y
        cmp     GXQ
        beq     @good
@bad:   lda     #GE_FORMAT
        sec
        rts
@good:  ldy     #AP_BYTES + 1           ; units = (bytes + 1,023) / 1,024
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
@whole: sta     units                   ; (1-128: the bytes are 1 to 131,072)
        ldy     #AP_UNIT
        lda     (GXP),y
        cmp     #$FF
        bne     @named
        lda     units
        jsr     GX_ALLOC
        bcc     @got
        rts
@named: ldx     units                   ; where the file says: free, and the program's
        jsr     GXTAKE
        bcc     @took
        rts
@got:   ldy     #AP_UNIT
        sta     (GXP),y
@took:  ldx     part                    ; its units, to free should a later part fail
        lda     units
        sta     pcount,x
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
units:  .res    1                      ; LOADP's part's units
pcount: .res    AP_MAX                 ; each part's units taken (0 none): freed on a failure
tbuf:   .res    512                    ; SPRFIX's copy of the table

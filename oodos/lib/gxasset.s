; OOLIB's asset files (include/oolib.inc, "Graphics"; hw/oolib.md, L4,
; question 8, and "Video card revision 2": format 4, oodos/tools/mkasset.py
; makes them): the header read with function 33 (record 0), then each part
; loaded into the program's card memory where it names (a card page) or
; where GX_VALLOC finds room ($FFFF, and its header part then says which):
; 4 KB at a time by function 83's DM_LOAD into a staging buffer in system
; memory (GX_STAGE, physical: $BF000 unless the program says), then
; DM_VWRITE through the card's port. A sprite table's entries name their
; images by the sprite images' part: once both are loaded, each entry's
; image (card pages, bits 0-14) is moved on by that part's first page.

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
imgu:   .res    2                      ; the sprite images' first page ($FFFF none)
tabu:   .res    2                      ; and the table's

        .data
GX_STAGE: .faraddr STAGE_DEF           ; the staging buffer's physical address (4 KB): the program's to move

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
        sta     imgu + 1
        sta     tabu
        sta     tabu + 1
        ldx     #2 * AP_MAX - 1         ; no part's pages taken yet
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

; The pages every part took (pcount), freed.
FREEALL:
        stz     part
@p:     lda     part
        asl     a
        tax
        lda     pcount,x
        ora     pcount + 1,x
        beq     @n
        lda     pcount,x
        sta     GX_V
        lda     pcount + 1,x
        sta     GX_V + 1
        jsr     PARTP
        ldy     #AP_UNIT
        lda     (GXP),y
        pha
        iny
        lda     (GXP),y
        tax
        pla
        jsr     GX_VFREE
@n:     inc     part
        lda     part
        cmp     #AP_MAX
        bne     @p
        rts

; The part at GXP checked against what the library needs (review F5: the
; file is untrusted, whatever mkasset.py's check said): a type of 1-8,
; its first record a multiple of 8 and not 0, 1 to 131,072 bytes, its
; records the bytes' (rounded up); then its card pages, its bytes' rounded
; up to 256: allocated if it says $FFFF, else taken where it says (a page
; on a unit, the run in the program's part, none of it taken already);
; then its records, 4 KB at a time, by DM_LOAD to GX_STAGE and DM_VWRITE
; on to the card. Carry set and A the error.
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
@good:  ldy     #AP_BYTES + 2           ; GX_V = the pages: (bytes + 255) / 256 (1-512)
        lda     (GXP),y
        sta     GXT + 1
        dey
        lda     (GXP),y
        sta     GXT
        dey
        lda     (GXP),y
        beq     @whole
        inc     GXT                     ; (a part page: never past $200, the bytes 1 to $20000)
        bne     @whole
        inc     GXT + 1
@whole: lda     GXT
        sta     GX_V
        lda     GXT + 1
        sta     GX_V + 1
        ldy     #AP_UNIT + 1
        lda     (GXP),y
        tax
        dey
        lda     (GXP),y
        cpx     #$FF
        bne     @named
        cmp     #$FF
        bne     @named
        lda     #VM_UNIT / VM_PAGE      ; anywhere: allocated
        jsr     GX_VALLOC
        bcc     @got
        rts
@named: jsr     GXTAKE                  ; where the file says: free, and the program's
        bcc     @took
        rts
@got:   ldy     #AP_UNIT
        sta     (GXP),y
        iny
        txa
        sta     (GXP),y
@took:  lda     part                    ; its pages, to free should a later part fail
        asl     a
        tax
        lda     GX_V
        sta     pcount,x
        lda     GX_V + 1
        sta     pcount + 1,x
        ldy     #AP_TYPE                ; the sprite parts' pages, for SPRFIX
        lda     (GXP),y
        ldx     #imgu - imgu
        cmp     #AT_SPRITES
        beq     @keep
        ldx     #tabu - imgu
        cmp     #AT_SPRTABLE
        bne     @load
@keep:  ldy     #AP_UNIT
        lda     (GXP),y
        sta     imgu,x
        iny
        lda     (GXP),y
        sta     imgu + 1,x
@load:  ldy     #AP_UNIT                ; GX_A: the card address it goes to
        lda     (GXP),y
        pha
        iny
        lda     (GXP),y
        tax
        pla
        jsr     GX_VADDR
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
        ldy     #AP_RECORDS             ; rest: the records left
        lda     (GXP),y
        sta     rest
        iny
        lda     (GXP),y
        sta     rest + 1
@chunk: lda     rest                    ; this piece: STAGE_RECS records, or what is left
        ora     rest + 1
        beq     @done
        lda     rest + 1
        bne     @full
        lda     rest
        cmp     #STAGE_RECS
        bcc     @some
@full:  lda     #STAGE_RECS
@some:  sta     lblk + 6
        stz     lblk + 7
        ldx     #2                      ; to the staging buffer
@st:    lda     GX_STAGE,x
        sta     lblk + 8,x
        dex
        bpl     @st
        jsr     ONE
        bcs     @no
        lda     lblk + 6                ; GX_V = its bytes: records x 128 (at most 4,096)
        lsr     a
        sta     GX_V + 1
        lda     #0
        ror     a
        sta     GX_V
        jsr     STAGEOUT                ; the staging buffer to GX_A on the card
        bcs     @no
        clc                             ; on: GX_A by the bytes, the record, what is left
        lda     GX_A
        adc     GX_V
        sta     GX_A
        lda     GX_A + 1
        adc     GX_V + 1
        sta     GX_A + 1
        bcc     @rec
        inc     GX_A + 2
@rec:   clc
        lda     lblk + 3
        adc     lblk + 6
        sta     lblk + 3
        bcc     @left
        inc     lblk + 4
@left:  sec
        lda     rest
        sbc     lblk + 6
        sta     rest
        bcs     @chunk
        dec     rest + 1
        bra     @chunk
@done:  clc
@no:    rts
STAGE_RECS      = 32                   ; 4 KB a piece: a CP/M block of oodos-8m, one READ
STAGE_DEF       = $BF000               ; bank B's last 4 KB: the program's RAM on revision 2
.assert STAGE_RECS * 128 = 4096, error, "the staging buffer is 4 KB"

; GX_V bytes from the staging buffer to GX_A on the card (DM_VWRITE).
STAGEOUT:
        lda     #DM_VWRITE
        sta     GXB
        ldx     #2
@a:     lda     GX_STAGE,x
        sta     GXB + 1,x
        lda     GX_A,x
        sta     GXB + 4,x
        dex
        bpl     @a
        lda     GX_V
        sta     GXB + 7
        lda     GX_V + 1
        sta     GXB + 8
        jmp     GXDMA

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

; The sprite table's 64 entries' images moved on by the images' first
; page (bits 0-14, card pages; bit 15, a byte a pixel, kept): the table
; read from the card into the library's memory (DM_VREAD), changed there
; and written back (DM_VWRITE).
SPRFIX: lda     imgu
        and     imgu + 1
        cmp     #$FF
        beq     @done
        lda     tabu
        and     tabu + 1
        cmp     #$FF
        beq     @done
        lda     #DM_VREAD               ; the card's table to tbuf
        jsr     TABSIDES
        jsr     GXDMA
        bcs     @no
        lda     #<tbuf
        sta     GXP
        lda     #>tbuf
        sta     GXP + 1
        ldx     #64
        ldy     #0
@entry: clc
        lda     (GXP),y
        adc     imgu
        sta     (GXP),y
        iny
        lda     (GXP),y
        pha
        and     #$80                    ; its 8-bit flag kept
        sta     GXQ
        pla
        adc     imgu + 1
        and     #$7F
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
        lda     #DM_VWRITE              ; and back
        jsr     TABSIDES
        jmp     GXDMA
@done:  clc
@no:    rts

; GXB: operation A (DM_VREAD or DM_VWRITE) of the 512-byte table between
; the card (page tabu) and tbuf.
TABSIDES:
        sta     GXB
        lda     #<tbuf
        ldx     #>tbuf
        jsr     GXCPU                   ; GX_A: tbuf's physical address
        lda     GXB                     ; the program's side: +4-6 for a read, +1-3 for a write
        ldy     #4
        cmp     #DM_VREAD
        beq     @sys
        ldy     #1
@sys:   ldx     #0
@s:     lda     GX_A,x
        sta     GXB,y
        iny
        inx
        cpx     #3
        bne     @s
        lda     tabu                    ; the card's: the other three
        ldx     tabu + 1
        jsr     GX_VADDR
        lda     GXB
        ldy     #1
        cmp     #DM_VREAD
        beq     @card
        ldy     #4
@card:  ldx     #0
@c:     lda     GX_A,x
        sta     GXB,y
        iny
        inx
        cpx     #3
        bne     @c
        stz     GXB + 7
        lda     #>512
        sta     GXB + 8
        rts

        .bss
rest:   .res    2                      ; LOADP's records left
pcount: .res    2 * AP_MAX             ; each part's pages taken (0 none): freed on a failure
tbuf:   .res    512                    ; SPRFIX's copy of the table

; OOLIB's card memory and W1 (include/oolib.inc, "Graphics"; vmem.inc;
; hw/oolib.md, "Video card revision 2"): the check of OODOS's interface,
; the W1 map, W1 and aperture slot 1 under the W1 contract, the program's
; card memory handed out, its copies through function 83, and what every
; graphics module shares (gx.inc).
;
; The W1 contract (interface version 8): OOLIB may write W1 with any block
; DM_INFO's W1 map allows, and SLOT1 with any of the program's card banks
; (16-111), and OODOS keeps both across a BDOS call, an interrupt and a
; hook, and puts block 1 back in W1 and the program's first card bank, 16
; (VM_PROG_BANK), in slot 1 at the warm boot. The W1 map is block 1, the
; allocated blocks, 33 and 36-47; OODOS's OSWIN refuses $80000-$83FFF and
; $88000-$8FFFF as a physical destination.
; GX_INIT checks the version, so an older OODOS is never mapped under. The
; block in W1 (GXCUR) and the bank in slot 1 (GXBANK) are kept, so a draw
; maps only when it moves to another.
;
; Card memory: the program's part, $040000-$1BFFFF, in 1,536 units of 1 KB
; (VM_UNIT), a bit each in GX_UNITS (set: taken); unit u is card page
; $0400 + u x 4. The program's whole: nothing else allocates them.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

        .bss
GX_X:   .res    2                      ; the routines' coordinates and values (oolib.inc)
GX_Y:   .res    2
GX_X2:  .res    2
GX_Y2:  .res    2
GX_DX:  .res    2
GX_DY:  .res    2
GX_R:   .res    2
GX_V:   .res    2
GX_A:   .res    3                      ; a card address (or, inside the library, a physical one)
GX_C:   .res    3                      ; a second card address
GX_FG:  .res    1
GX_BG:  .res    1
GX_OPAQUE: .res 1
GX_W1MAP: .res  8                      ; DM_INFO's: bit n & 7 of byte n / 8, block n allowed in W1
GX_UNITS: .res  VM_UNITS / 8           ; the program's card memory's units taken: bit u & 7 of byte u / 8
GX_VUNITS: .res 2                      ; the units the card has room for (1,536 on 2 MB)
GX_AREA: .res   2                      ; the program's event area (0: none)
GX_OK:  .res    1                      ; 1: GX_INIT found interface 8 and a card: W1 may be written
GXCUR:  .res    1                      ; the block W1 has (as the library put it)
GXBANK: .res    2                      ; the bank slot 1 shows (as the library put it; $FFFF not known)
GXB:    .res    20                     ; function 83's block
GXPB:   .res    4                      ; function 73's

        .data
; The vertical-blank go bits (EV_VBGO) set with TSB, which an interrupt
; can't split: the instruction's operand is the area's +12, written by
; GX_INIT. Here, not in CODE, so that it may be written.
GXTSB:  tsb     a:$0000
        rts

        .code

        .pushseg
        .rodata
BIT8:   .byte   $01, $02, $04, $08, $10, $20, $40, $80
        .popseg

; A/X the event area, or 0.
GX_INIT:
        sta     GX_AREA
        stx     GX_AREA + 1
        clc                             ; the TSB's operand: +12
        adc     #GX_EV_VBGO
        sta     GXTSB + 1
        txa
        adc     #0
        sta     GXTSB + 2
        stz     GX_OK
        lda     #FT_INFO
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        cmp     #$FF                    ; no function 80: A = X = $FF
        beq     @old
        cmp     #GX_IFACE
        bcc     @old
        txa
        and     #OA_DMA
        beq     @old
        jsr     GX_REMAP                ; the W1 map, and the card's room
        bcs     @old
        lda     GX_VUNITS
        ora     GX_VUNITS + 1
        beq     @old                    ; no revision 2 card
        ldx     #VM_UNITS / 8 - 1       ; every unit free
@free:  stz     GX_UNITS,x
        dex
        bne     @free
        stz     GX_UNITS
        lda     #1
        sta     GX_OK
        sta     GXCUR
        lda     #$FF
        sta     GXBANK
        sta     GXBANK + 1
        clc
        rts
@old:   lda     #GE_VERSION
        sec
        rts
.assert VM_UNITS / 8 - 1 < 256, error, "GX_INIT clears GX_UNITS with X"

; The W1 map again (DM_INFO's +5-12), and the card's room (+13-16: the
; program's pages, so GX_VUNITS = (the end - the first) / 4, at most
; VM_UNITS).
GX_REMAP:
        lda     #DM_INFO
        sta     GXB
        jsr     GXDMA
        bcs     @no
        ldx     #7
@map:   lda     GXB + 5,x
        sta     GX_W1MAP,x
        dex
        bpl     @map
        sec                             ; the end - the first, in pages
        lda     GXB + 15
        sbc     GXB + 13
        sta     GX_VUNITS
        lda     GXB + 16
        sbc     GXB + 14
        bcc     @none
        lsr     a
        ror     GX_VUNITS
        lsr     a
        ror     GX_VUNITS
        sta     GX_VUNITS + 1
        lda     #<VM_UNITS              ; at most VM_UNITS
        cmp     GX_VUNITS
        lda     #>VM_UNITS
        sbc     GX_VUNITS + 1
        bcs     @ok
        lda     #<VM_UNITS
        sta     GX_VUNITS
        lda     #>VM_UNITS
        sta     GX_VUNITS + 1
@ok:    clc
@no:    rts
@none:  stz     GX_VUNITS
        stz     GX_VUNITS + 1
        clc
        rts

; Function 83 with GXB: carry set if refused (A = $FF), A the error GE_DMA.
GXDMA:  lda     #<GXB
        ldx     #>GXB
        ldy     #O_DMA
        jsr     BDOS
        cmp     #$FF
        bne     @ok
        lda     #GE_DMA
        sec
        rts
@ok:    clc
        rts

GX_W1BACK:
        lda     #1
        sta     WIN1
        sta     GXCUR
        rts

; Block A into W1, if GX_INIT allowed it and the map has it.
GX_BLOCK:
        cmp     GXCUR
        beq     @same
        ldx     GX_OK
        beq     @no
        pha
        and     #7
        tax
        lda     BIT8,x
        sta     GXT
        pla
        pha
        lsr     a
        lsr     a
        lsr     a
        tax
        lda     GX_W1MAP,x
        and     GXT
        beq     @not
        pla
        sta     WIN1
        sta     GXCUR
@same:  clc
        rts
@not:   pla
@no:    lda     #GE_MAP
        sec
        rts

; GX_A, a card address, mapped: its bank in slot 1, slot 1's window
; (block 33) in W1, GXP its address there. GE_MAP for a bank that isn't
; the program's (16-111).
GXPHYS: lda     GX_A + 1                ; the bank: the address / $4000, 10 bits
        asl     a
        lda     GX_A + 2
        rol     a
        sta     GXT
        lda     #0
        rol     a
        sta     GXT + 1
        lda     GX_A + 1
        asl     a
        asl     a
        rol     GXT
        rol     GXT + 1
        lda     GXT + 1
        bne     @no                     ; past bank 255
        lda     GXT
        cmp     #VM_PROG_BANK
        bcc     @no
        cmp     #VM_PROG_BANK + VM_PROG_BANKS
        bcs     @no
        cmp     GXBANK
        bne     @slot
        ldx     GXBANK + 1
        beq     @w1
@slot:  ldx     GX_OK                   ; (as GX_BLOCK: nothing written before GX_INIT)
        beq     @no
        sta     VID_SLOT1               ; (the high byte 0: banks under 256)
        stz     VID_SLOT1 + 1
        sta     GXBANK
        stz     GXBANK + 1
@w1:    lda     #VM_SLOT_BLOCK
        jsr     GX_BLOCK
        bcs     @ret
        lda     GX_A
        sta     GXP
        lda     GX_A + 1
        and     #$3F
        ora     #>VM_SLOT_CPU
        sta     GXP + 1
        clc
@ret:   rts
@no:    lda     #GE_MAP
        sec
        rts
.assert VM_PROG_BANK + VM_PROG_BANKS < 256, error, "GXPHYS's banks are one byte"

; A/X a card page: GX_A its card address. Keeps A and X.
GX_VADDR:
        stz     GX_A
        sta     GX_A + 1
        stx     GX_A + 2
        rts

; A/X a card page: mapped (slot 1, W1): A/X and GXP its CPU address.
GX_VMAP:
        jsr     GX_VADDR
        jsr     GXPHYS
        bcs     @no
        lda     GXP
        ldx     GXP + 1
@no:    rts

; GX_A = the physical address of the CPU address A/X (function 73).
GXCPU:  sta     GXPB
        stx     GXPB + 1
        lda     #<GXPB
        ldx     #>GXPB
        ldy     #O_PHYSADDR
        jsr     BDOS
        lda     GXPB + 2                ; the paragraph x 16 + the offset
        asl     a
        sta     GX_A
        lda     GXPB + 3
        rol     a
        sta     GX_A + 1
        lda     #0
        rol     a
        sta     GX_A + 2
        ldx     #3
@x16:   asl     GX_A
        rol     GX_A + 1
        rol     GX_A + 2
        dex
        bne     @x16
        lda     GX_A
        ora     GXPB
        sta     GX_A
        rts

; The copies through function 83: GXB's addresses from GX_A (and GX_C, or
; the CPU address A/X made physical), GX_V the length (0: nothing done).
; GX_A is kept.

; A/X the program's bytes to GX_A on the card.
GX_VPUT:
        ldy     #DM_VWRITE
        sty     GXB
        ldy     #4                      ; the card's address at +4-6, the program's at +1-3
        bra     CPUCOPY
; GX_A's card bytes to the program's A/X.
GX_VGET:
        ldy     #DM_VREAD
        sty     GXB
        ldy     #1                      ; the card's at +1-3, the program's at +4-6
CPUCOPY:
        pha
        lda     GX_V
        ora     GX_V + 1
        bne     @some
        pla
        clc
        rts
@some:  phx
        ldx     #0
@card:  lda     GX_A,x
        sta     GXB,y
        sta     GXS,x
        iny
        inx
        cpx     #3
        bne     @card
        plx
        pla
        jsr     GXCPU
        ldy     #4                      ; the program's: the other three
        lda     GXB
        cmp     #DM_VWRITE
        bne     @at
        ldy     #1
@at:    ldx     #0
@phys:  lda     GX_A,x
        sta     GXB,y
        lda     GXS,x                   ; GX_A back
        sta     GX_A,x
        iny
        inx
        cpx     #3
        bne     @phys
        bra     LEN7

; GX_A's card bytes to GX_C's.
GX_VCOPY:
        lda     GX_V
        ora     GX_V + 1
        beq     NONE
        lda     #DM_VCOPY
        sta     GXB
        ldx     #2
@a:     lda     GX_A,x
        sta     GXB + 1,x
        lda     GX_C,x
        sta     GXB + 4,x
        dex
        bpl     @a
LEN7:   lda     GX_V
        sta     GXB + 7
        lda     GX_V + 1
        sta     GXB + 8
        jmp     GXDMA
NONE:   clc
        rts

; A the byte: GX_V bytes of it from GX_A on the card.
GX_VFILL:
        ldx     GX_V
        bne     @some
        ldx     GX_V + 1
        beq     NONE
@some:  sta     GXB + 1
        lda     #DM_VFILL
        sta     GXB
        ldx     #2
@a:     lda     GX_A,x
        sta     GXB + 2,x
        dex
        bpl     @a
        lda     GX_V
        sta     GXB + 5
        lda     GX_V + 1
        sta     GXB + 6
        jmp     GXDMA

; The go bits in A set in the event area's EV_VBGO, by GXTSB; refused,
; carry set and A GE_AREA, with no area (GX_INIT's 0): the TSB would hit
; the program's zero page (review F6).
GXGO:   ldx     GX_AREA
        bne     @go
        ldx     GX_AREA + 1
        bne     @go
        lda     #GE_AREA
        sec
        rts
@go:    jsr     GXTSB
        clc
        rts

; ---------------------------------------------------------------------------
; The units.

; Unit uk's bit: Y its byte in GX_UNITS, A its bit. Keeps X.
UBIT:   lda     uk + 1                  ; uk / 8: under 192
        sta     GXT
        lda     uk
        lsr     GXT
        ror     a
        lsr     GXT
        ror     a
        lsr     GXT
        ror     a
        tay
        lda     uk
        and     #7
        phx
        tax
        lda     BIT8,x
        plx
        rts

; ucnt = (GX_V + 3) / 4, the units GX_V pages take: carry set for 0, or
; more than the card has room for.
UCOUNT: clc
        lda     GX_V
        adc     #3
        sta     ucnt
        lda     GX_V + 1
        adc     #0
        bcs     @no
        lsr     a
        ror     ucnt
        lsr     a
        ror     ucnt
        sta     ucnt + 1
        ora     ucnt
        beq     @no
        lda     GX_VUNITS               ; GX_VUNITS >= ucnt?
        cmp     ucnt
        lda     GX_VUNITS + 1
        sbc     ucnt + 1
        bcc     @no
        clc
        rts
@no:    sec
        rts

; Carry clear if units ufirst to ufirst + ucnt - 1 are all free and on the
; card (uend the one past them); carry set if not, uk the first taken one,
; or $FFFF past the card's room.
URUN:   clc
        lda     ufirst
        adc     ucnt
        sta     uend
        lda     ufirst + 1
        adc     ucnt + 1
        sta     uend + 1
        lda     GX_VUNITS               ; GX_VUNITS >= uend?
        cmp     uend
        lda     GX_VUNITS + 1
        sbc     uend + 1
        bcc     @past
        lda     ufirst
        sta     uk
        lda     ufirst + 1
        sta     uk + 1
@bit:   jsr     UBIT
        and     GX_UNITS,y
        bne     @taken
        jsr     UNEXT
        bne     @bit
        clc
        rts
@past:  lda     #$FF
        sta     uk
        sta     uk + 1
@taken: sec
        rts

; Units ufirst to uend - 1 taken (URUN found them free), or freed (UFREE).
UTAKE:  jsr     UFROM
@bit:   jsr     UBIT
        ora     GX_UNITS,y
        sta     GX_UNITS,y
        jsr     UNEXT
        bne     @bit
        rts
UFREE:  jsr     UFROM
@bit:   jsr     UBIT
        eor     #$FF
        and     GX_UNITS,y
        sta     GX_UNITS,y
        jsr     UNEXT
        bne     @bit
        rts
UFROM:  lda     ufirst
        sta     uk
        lda     ufirst + 1
        sta     uk + 1
        rts
; uk on one: Z set when it reaches uend.
UNEXT:  inc     uk
        bne     @cmp
        inc     uk + 1
@cmp:   lda     uk
        cmp     uend
        bne     @ret
        lda     uk + 1
        cmp     uend + 1
@ret:   rts

; A/X = the card page of unit ufirst, GX_A its card address: $0400 +
; ufirst x 4.
UPAGE:  lda     ufirst + 1
        sta     GXT
        lda     ufirst
        asl     a
        rol     GXT
        asl     a
        rol     GXT
        pha
        lda     GXT
        clc
        adc     #>VM_PROG_PAGE
        tax
        pla
        jmp     GX_VADDR
.assert <VM_PROG_PAGE = 0, error, "UPAGE adds the first page's high byte alone"

; ufirst = the unit of the card page A/X: carry set if it isn't one of the
; program's or not on a unit (a page that isn't a multiple of 4).
UOFPAGE:
        sta     GXT
        and     #3
        bne     @no
        txa
        sec
        sbc     #>VM_PROG_PAGE
        bcc     @no
        sta     ufirst + 1
        lda     GXT
        lsr     ufirst + 1
        ror     a
        lsr     ufirst + 1
        ror     a
        sta     ufirst
        clc
        rts
@no:    sec
        rts

; GX_V pages, A the alignment in pages (a power of two, 1-128; 0 is 1):
; A/X the first page, GX_A its card address; carry set and A GE_MEMORY if
; no run is free.
GX_VALLOC:
        lsr     a                       ; the alignment in units: pages / 4, at least 1
        lsr     a
        bne     @al
        lda     #1
@al:    sta     ualign
        jsr     UCOUNT
        bcs     @none
        stz     ufirst
        stz     ufirst + 1
@try:   jsr     URUN
        bcc     @got
        lda     uk + 1                  ; past the card's room: none
        and     uk
        cmp     #$FF
        beq     @none
        lda     ualign                  ; on to the first aligned unit past uk: (uk + align)
        dec     a                       ; AND NOT (align - 1)
        eor     #$FF
        sta     GXT
        clc
        lda     uk
        adc     ualign
        and     GXT
        sta     ufirst
        lda     uk + 1
        adc     #0
        sta     ufirst + 1
        bra     @try
@got:   jsr     UTAKE
        jmp     UPAGE                   ; (carry clear: URUN's)
@none:  lda     #GE_MEMORY
        sec
        rts

; A/X the first page, GX_V the pages: taken where they are (an asset's
; part that names its place); carry set and A GE_MEMORY if any is taken
; already, isn't the program's, or the page isn't on a unit.
GXTAKE: jsr     UOFPAGE
        bcs     @no
        jsr     UCOUNT
        bcs     @no
        jsr     URUN
        bcs     @no
        jsr     UTAKE
        jmp     UPAGE
@no:    lda     #GE_MEMORY
        sec
        rts

; A/X the first page, GX_V the pages: free again. Only a run that
; GX_VALLOC or GXTAKE could have given is freed: one that isn't on a unit,
; or runs off the program's part, is left as it is.
GX_VFREE:
        jsr     UOFPAGE
        bcs     @done
        jsr     UCOUNT
        bcs     @done
        jsr     URUN                    ; (for uend and the room's check: taken units are freed too)
        lda     uk
        and     uk + 1
        cmp     #$FF
        beq     @done                   ; past the card's room: nothing
        jsr     UFREE
@done:  clc
        rts

        .bss
ucnt:   .res    2                      ; a run's units
ualign: .res    1                      ; its alignment, in units
ufirst: .res    2                      ; its first
uend:   .res    2                      ;   and the one past it
uk:     .res    2                      ; a unit
GXS:    .res    3                      ; GX_A kept across a copy

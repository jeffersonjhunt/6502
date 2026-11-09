; OOLIB's video memory and W1 (include/oolib.inc, "Graphics"; hw/oolib.md,
; L4): the check of OODOS's interface, the W1 map, W1 itself under the W1
; contract, video memory's units handed out, and what every graphics
; module shares (gx.inc).
;
; The W1 contract (interface version 4): OOLIB may write W1 with any block
; DM_INFO's W1 map allows, and OODOS keeps it across a BDOS call, an
; interrupt and a hook, and puts block 1 back at the warm boot. GX_INIT
; checks the version, so an older OODOS is never mapped under. The block
; in W1 is kept (GXCUR), so a draw maps only when it moves to another.
;
; Units: video memory's 256 units of 1 KB, a bit each in GX_UNITS (set:
; taken); 0-12 are the console's, never handed out. The program's whole:
; nothing else allocates them.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "gx.inc"

        .zeropage
GXP:    .res    2                      ; a pointer into W1, or anywhere
GXQ:    .res    2                      ; a second
GXT:    .res    2                      ; scratch

        .bss
GX_X:   .res    2                      ; the routines' coordinates and values (oolib.inc)
GX_Y:   .res    2
GX_X2:  .res    2
GX_Y2:  .res    2
GX_DX:  .res    2
GX_DY:  .res    2
GX_R:   .res    2
GX_V:   .res    2
GX_A:   .res    3                      ; a physical address
GX_FG:  .res    1
GX_BG:  .res    1
GX_OPAQUE: .res 1
GX_W1MAP: .res  8                      ; DM_INFO's: bit n & 7 of byte n / 8, block n allowed in W1
GX_UNITS: .res  32                     ; video memory's units taken: bit n & 7 of byte n / 8
GX_AREA: .res   2                      ; the program's event area (0: none)
GX_OK:  .res    1                      ; 1: GX_INIT found interface 4: W1 may be written
GXCUR:  .res    1                      ; the block W1 has (as the library put it)
GXB:    .res    16                     ; function 83's block
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
        cmp     #4
        bcc     @old
        txa
        and     #OA_DMA
        beq     @old
        ldx     #31                     ; every unit free but the console's 0-12
@free:  stz     GX_UNITS,x
        dex
        bpl     @free
        lda     #$FF
        sta     GX_UNITS
        lda     #$1F
        sta     GX_UNITS + 1
        lda     #1
        sta     GX_OK
        sta     GXCUR
        jmp     GX_REMAP
@old:   lda     #GE_VERSION
        sec
        rts
.assert UNIT_FIRST = 13, error, "GX_INIT marks units 0-12 taken as $FF and $1F"

; The W1 map again (DM_INFO's +5-12).
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
        clc
@no:    rts

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

; GX_A, a physical address, mapped: its block in W1, GXP its address there.
GXPHYS: lda     GX_A + 2                ; the block: A14-A19
        asl     a
        asl     a
        sta     GXT
        lda     GX_A + 1
        rol     a
        rol     a
        rol     a
        and     #3
        ora     GXT
        jsr     GX_BLOCK
        bcs     @no
        lda     GX_A
        sta     GXP
        lda     GX_A + 1
        and     #$3F
        ora     #$40
        sta     GXP + 1
        clc
@no:    rts

; GX_A = unit A's physical address, VRAM + A x 1,024.
GX_UNIT:
        stz     GX_A
        pha
        asl     a
        asl     a
        sta     GX_A + 1
        pla
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        ora     #^VRAM
        sta     GX_A + 2
        rts

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

; Unit X's bit in GX_UNITS: Y its byte, A its bit.
UBIT:   txa
        lsr     a
        lsr     a
        lsr     a
        tay
        txa
        and     #7
        phx
        tax
        lda     BIT8,x
        plx
        rts

; A the first unit, X the count, taken where they are: carry set (A
; GE_MEMORY) if any is the console's, past 255, or taken already.
GXTAKE: sta     ufirst
        stx     ucount
        cmp     #UNIT_FIRST
        bcc     @no
        txa
        beq     @no
        clc
        adc     ufirst
        bcc     @fits
        bne     @no                     ; past 256
@fits:  ldx     ufirst
        lda     ucount
        sta     GXT + 1
@check: jsr     UBIT
        and     GX_UNITS,y
        bne     @no
        inx
        dec     GXT + 1
        bne     @check
        ldx     ufirst
        lda     ucount
        sta     GXT + 1
@take:  jsr     UBIT
        ora     GX_UNITS,y
        sta     GX_UNITS,y
        inx
        dec     GXT + 1
        bne     @take
        lda     ufirst
        clc
        rts
@no:    lda     #GE_MEMORY
        sec
        rts

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

; A units, aligned: A the first, or carry set.
GX_ALLOC:
        sta     ucount
        cmp     #1
        bcc     @none
        lda     #8                      ; the alignment: 8, or the largest power of two <= the count
@al:    cmp     ucount
        beq     @set
        bcc     @set
        lsr     a
        bra     @al
@set:   sta     ualign
        dec     a                       ; the first: (13 + align - 1) & ~(align - 1)
        sta     GXT
        clc
        adc     #UNIT_FIRST
        sta     ufirst
        lda     GXT
        eor     #$FF
        and     ufirst
        sta     ufirst
@try:   lda     ufirst                  ; ufirst + count <= 256?
        clc
        adc     ucount
        bcc     @fits
        bne     @none                   ; past 256
@fits:  ldx     ufirst                  ; every unit of the run free?
        lda     ucount
        sta     GXT + 1
@check: jsr     UBIT
        and     GX_UNITS,y
        bne     @next
        inx
        dec     GXT + 1
        bne     @check
        ldx     ufirst                  ; taken
        lda     ucount
        sta     GXT + 1
@take:  jsr     UBIT
        ora     GX_UNITS,y
        sta     GX_UNITS,y
        inx
        dec     GXT + 1
        bne     @take
        lda     ufirst
        clc
        rts
@next:  lda     ufirst
        clc
        adc     ualign
        bcs     @none
        sta     ufirst
        bra     @try
@none:  lda     #GE_MEMORY
        sec
        rts

; A the first unit, X the count: free again (the console's are never).
GX_FREE:
        stx     GXT + 1
        tax
        lda     GXT + 1
        beq     @done
@unit:  cpx     #UNIT_FIRST
        bcc     @skip
        jsr     UBIT
        eor     #$FF
        and     GX_UNITS,y
        sta     GX_UNITS,y
@skip:  inx
        beq     @done
        dec     GXT + 1
        bne     @unit
@done:  rts

        .bss
ucount: .res    1
ualign: .res    1
ufirst: .res    1

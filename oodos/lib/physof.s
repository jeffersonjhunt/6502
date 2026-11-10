; OOLIB's PHYSOF (lib/physof.inc): a CPU address to the 20-bit physical
; address function 82's SN_LOAD and function 83 take, through function 73
; (O_PHYSADDR: the paragraph and the offset in it). sid.s's and mu.s's.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "physof.inc"

        .bss
pblk:   .res    4                       ; function 73's block
PHYS:   .res    3

        .code
PHYSOF: sta     pblk
        stx     pblk + 1
        stz     pblk + 2
        stz     pblk + 3
        lda     #<pblk
        ldx     #>pblk
        ldy     #O_PHYSADDR
        jsr     BDOS
        lda     pblk + 2                ; the paragraph x 16 + the offset (0-15)
        asl     a
        asl     a
        asl     a
        asl     a
        ora     pblk
        sta     PHYS
        lda     pblk + 2
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     PHYS + 1
        lda     pblk + 3
        asl     a
        asl     a
        asl     a
        asl     a
        ora     PHYS + 1
        sta     PHYS + 1
        lda     pblk + 3
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     PHYS + 2
        rts

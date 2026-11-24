; OOLIB's sound modules (lib/pb*.s, L5): is there a card OODOS drives
; (revision 3: SN_INFO's A), and function 82 for a block. Shared by the
; modules; not in oolib.inc.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .code

; Carry clear, A kept: a revision 3 card. Carry set, A = PE_NOCARD: an older
; card, or none. X and Y changed.
PBCARD:
        pha
        lda     #SN_INFO
        ldx     #0
        ldy     #O_SOUND
        jsr     BDOS
        cmp     #3
        bne     @no
        pla
        clc
        rts
@no:    pla
        lda     #PE_NOCARD
        sec
        rts

; Function 82 with the block A/X: Z set if done (A = 0).
PBF82:  ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        rts

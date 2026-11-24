; OOLIB's levels (include/oolib.inc, "Sound: playback"; hw/oolib.md, L5d):
; 8 bands and a VU pair, 0-255, in one form whatever is sounding, for a
; meter: the card's own, measured (function 82's SN_LEVELS), for every
; source alike.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"
        .importzp PBLEV_ZP              ; zppblev.s's

lp              = PBLEV_ZP

        .bss
lblk:   .res    PBL_LEN + 1             ; SN_LEVELS's block

        .code

; A/X a block of PBL_LEN bytes: +0-7 the bands, lowest first, +8-9 the VU,
; left and right. Nothing begun, or no card: all 0.
PB_LEVELS:
        sta     lp
        stx     lp + 1
        ldx     #PBL_LEN - 1
@z:     stz     lblk + 1,x
        dex
        bpl     @z
        lda     PBSRC
        beq     @put
        lda     #SN_LEVELS
        sta     lblk
        lda     #<lblk
        ldx     #>lblk
        jsr     PBF82
        beq     @put
        ldx     #PBL_LEN - 1
@z2:    stz     lblk + 1,x
        dex
        bpl     @z2
@put:   ldy     #PBL_LEN - 1
@cp:    lda     lblk + 1,y
        sta     (lp),y
        dey
        bpl     @cp
        lda     #0
        clc
        rts

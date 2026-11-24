; OOLIB's music: MU_TONE (include/oolib.inc, "Sound: music"; hw/oolib.md,
; L6b), GW-BASIC's SOUND. A/X a block of MU_NLEN: +0 the voice, +1-2 a
; frequency in Hz (0 a rest), +3-4 the length in ms, +5-6 the gate time, as
; MU_NOTE's. A frequency entry (Hz x 16, the card's exact pitch) or a rest,
; sent at once as a one-entry block (MUQ). Carry and A as mu.s's calls.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "mu.inc"

        .code

MU_TONE:
        sta     MUP
        stx     MUP + 1
        ldy     #1
        lda     (MUP),y
        iny
        ora     (MUP),y
        beq     @rest
        lda     #$10                    ; a frequency: +1-3 Hz x 16, +4-7 the length, +8-11 the gate
        sta     MUE
        stz     MUE + 3
        lda     (MUP),y                 ; Hz's high byte
        sta     MUE + 2
        dey
        lda     (MUP),y
        ldx     #4
@x16:   asl     a
        rol     MUE + 2
        rol     MUE + 3
        dex
        bne     @x16
        sta     MUE + 1
        ldx     #4
        sec
        jsr     MUTIME
        lda     #12
        bra     @q
@rest:  lda     #$20                    ; a rest: +1-4 the length
        sta     MUE
        ldx     #1
        clc
        jsr     MUTIME
        lda     #5
@q:     sta     MUELEN
        lda     (MUP)
        jmp     MUQ

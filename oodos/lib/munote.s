; OOLIB's music: MU_NOTE (include/oolib.inc, "Sound: music"; hw/oolib.md,
; L6b). A/X a block of MU_NLEN: +0 the voice, +1 the note, 0 (C-0) to 95
; (B-7; A-4, 440 Hz, is 57), or MU_REST; +3-4 the length in ms (0 is 1);
; +5-6 the gate time in ms (0: no sound; at the length or past it, the whole
; note). A note entry (the card's note number: 12 more) or a rest, sent at
; once as a one-entry block (MUQ). Carry and A as mu.s's calls: PE_RANGE for
; a note past 95 or a voice past 5.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "mu.inc"

        .code

MU_NOTE:
        sta     MUP
        stx     MUP + 1
        ldy     #1
        lda     (MUP),y
        cmp     #MU_REST
        beq     @rest
        cmp     #SID_NOTES
        bcs     @range
        adc     #MU_NOTE0               ; (carry clear)
        sta     MUE + 1
        stz     MUE                     ; a note: +1 the note, +2-5 the length, +6-9 the gate
        ldx     #2
        sec
        jsr     MUTIME
        lda     #10
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
@range: lda     #PE_RANGE
        sec
        rts

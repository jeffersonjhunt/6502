; OOLIB's music: MU_NOTE (include/oolib.inc, "Sound: music"; hw/oolib.md,
; L6b). A/X a block of MU_NLEN: +0 the voice, +1 the note, 0 (C-0) to 95
; (B-7; A-4, 440 Hz, is 57), or MU_REST; +3-4 the length in ms (0 is 1);
; +5-6 the gate time in ms (0: no sound; at the length or past it, the whole
; note). The note's frequency register value for the card's clock
; (munote.inc: mksidtab.py's), into the voice's queue. Carry and A as
; mu.s's calls: PE_RANGE for a note past 95 or a voice past 5.

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
        asl     a
        tax
        lda     MUCLK
        bne     @ntsc
        lda     mu_note_pal,x
        sta     MUF
        lda     mu_note_pal + 1,x
        sta     MUF + 1
        bra     @time
@ntsc:  lda     mu_note_ntsc,x
        sta     MUF
        lda     mu_note_ntsc + 1,x
        sta     MUF + 1
        bra     @time
@rest:  stz     MUF
        stz     MUF + 1
@time:  ldy     #3                      ; the length and the gate time, as they are
        ldx     #0
@t:     lda     (MUP),y
        sta     MUL,x
        iny
        inx
        cpx     #4
        bne     @t
        lda     (MUP)
        jmp     MUQ
@range: lda     #PE_RANGE
        sec
        rts

        .include "munote.inc"

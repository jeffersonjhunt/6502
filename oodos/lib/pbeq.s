; OOLIB's EQ (include/oolib.inc, "Sound: the EQ and the vis page"; hw/oolib.md,
; L5b): the sound card's preamp and ten bands, and its enable. Register work
; with nothing to share, so no claim: a program may set the EQ before its
; stream starts. The warm boot turns it off and flat (OODOS 0.7).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .zeropage
eqp:    .res    2

        .code

; A the band (0 the preamp, 1-10), X its gain in signed half-dB (-24 to 24).
PB_EQ:
        cmp     #EQ_BANDS + 1
        bcs     range
        tay
        txa
        jsr     ingain
        bcs     range
        jsr     PBCARD
        bcs     out
        sty     SND_EQIDX
        stx     SND_EQVAL
ok:     lda     #0
        clc
out:    rts
range:  lda     #PE_RANGE
        sec
        rts

; A 0: the EQ off; anything else, on. The gains are kept.
PB_EQON:
        jsr     PBCARD
        bcs     out
        cmp     #0
        beq     @set
        lda     #1
@set:   ldx     #SND_EQ_ENABLE
        stx     SND_EQIDX
        sta     SND_EQVAL
        bra     ok

; A/X 11 bytes: the preamp's gain and the ten bands', each as PB_EQ's X.
; All checked before any is written: one out of range, none are.
PB_EQALL:
        sta     eqp
        stx     eqp + 1
        ldy     #EQ_BANDS
@chk:   lda     (eqp),y
        jsr     ingain
        bcs     range
        dey
        bpl     @chk
        jsr     PBCARD
        bcs     out
        stz     SND_EQIDX
        ldy     #0
@put:   lda     (eqp),y
        sta     SND_EQVAL               ; EQIDX moves on
        iny
        cpy     #EQ_BANDS + 1
        bne     @put
        bra     ok

; Carry clear if A, signed, is -24 to 24: A + 24 is 0-48 then, 49-255
; otherwise. A changed.
ingain: clc
        adc     #EQ_GAIN_MAX
        cmp     #2 * EQ_GAIN_MAX + 1
        rts

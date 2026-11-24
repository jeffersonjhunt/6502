; OOLIB's EQ (include/oolib.inc, "Sound: the EQ and the vis page"; hw/oolib.md,
; L5b): the sound card's preamp and ten bands, and its enable, by function
; 82's SN_EQ. No claim: a program may set the EQ before its stream starts.
; The warm boot turns it off and flat.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"
        .importzp PBEQ_ZP               ; zppbeq.s's

eqp             = PBEQ_ZP

        .bss
eblk:   .res    13                      ; SN_EQ's block

        .code

; A the band (0 the preamp, 1-10), X its gain in signed half-dB (-24 to 24).
PB_EQ:
        cmp     #EQ_BANDS + 1
        bcs     range
        sta     eblk + 1
        txa
        sta     eblk + 2
        jsr     ingain
        bcs     range
send:   lda     #SN_EQ
        sta     eblk
        jsr     PBCARD
        bcs     out
        lda     #<eblk
        ldx     #>eblk
        jsr     PBF82
        bne     nocard
ok:     lda     #0
        clc
out:    rts
range:  lda     #PE_RANGE
        sec
        rts
nocard: lda     #PE_NOCARD
        sec
        rts

; A 0: the EQ off; anything else, on. The gains are kept.
PB_EQON:
        cmp     #0
        beq     @set
        lda     #1
@set:   sta     eblk + 2
        lda     #$FE
        sta     eblk + 1
        bra     send

; A/X 11 bytes: the preamp's gain and the ten bands', each as PB_EQ's X.
; All checked before any is set: one out of range, none are.
PB_EQALL:
        sta     eqp
        stx     eqp + 1
        ldy     #EQ_BANDS
@chk:   lda     (eqp),y
        sta     eblk + 2,y
        jsr     ingain
        bcs     range
        dey
        bpl     @chk
        lda     #$FF
        sta     eblk + 1
        bra     send

; Carry clear if A, signed, is -24 to 24: A + 24 is 0-48 then, 49-255
; otherwise. A changed.
ingain: clc
        adc     #EQ_GAIN_MAX
        cmp     #2 * EQ_GAIN_MAX + 1
        rts

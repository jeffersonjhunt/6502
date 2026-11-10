; OOLIB's SID envelopes (include/oolib.inc, "Sound: the SIDs' voices";
; hw/oolib.md, L5c): an estimate of each voice's level, a frame at a time,
; for a meter. The card can't give a voice's envelope (a SID's ENV3 is voice
; 3's alone, and not the card's to read), so the level is moved as the chip
; would move it, from the gate and the ADSR in a SID_VOICES block: up at the
; attack's rate to full, down at the decay's to the sustain level, and at
; the release's to 0 once the gate goes off. The rates are sidenv.inc's
; (tools/mksidtab.py: the data sheet's times, per frame of 1/59.94 s). A
; level is 16 bits, $FF00 full; SID_LVL has the high bytes. OO-Amp's rules
; (its oa_voices.s), so its meters move as they did.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

EN_QUIET        = 0                    ; a voice's phase
EN_ATTACK       = 1
EN_DECAY        = 2                    ; and sustain
EN_RELEASE      = 3
EN_FULL         = $FF00

        .importzp SIDENV_ZP             ; zpsidenv.s's
ep              = SIDENV_ZP            ; the SID_VOICES block

        .bss
SID_LVL:        .res    SID_NVOICES    ; each voice's level, 0-255 (the high byte)
elo:            .res    SID_NVOICES    ; and its low byte
ephase:         .res    SID_NVOICES
egate:          .res    SID_NVOICES    ; the gate at the last frame
es:             .res    2              ; a step
et:             .res    1              ; the sustain level's high byte

        .include "sidenv.inc"

        .rodata
; Each voice's control register in a SID_VOICES block (+1 AD, +2 SR).
vctl:   .byte   4, 11, 18, SID_REGS + 4, SID_REGS + 11, SID_REGS + 18

        .code

; Every voice quiet: for a new tune, or a song started again.
SID_ENV0:
        ldx     #SID_NVOICES - 1
@v:     stz     SID_LVL,x
        stz     elo,x
        stz     ephase,x
        stz     egate,x
        dex
        bpl     @v
        clc
        rts

; A/X a SID_VOICES block, read this frame: each voice's level moved on a
; frame. Once a frame; SID_LVL then.
SID_ENV:
        sta     ep
        stx     ep + 1
        ldx     #SID_NVOICES - 1
@v:     jsr     voice
        dex
        bpl     @v
        clc
        rts

; Voice X moved on (X kept).
voice:  ldy     vctl,x
        lda     (ep),y
        and     #1                      ; the gate
        cmp     egate,x
        beq     @phase
        sta     egate,x
        lsr     a                       ; on: the attack; off: the release
        lda     #EN_RELEASE
        bcc     @set
        lda     #EN_ATTACK
@set:   sta     ephase,x
@phase: lda     ephase,x
        cmp     #EN_ATTACK
        beq     attack
        cmp     #EN_DECAY
        beq     decay
        cmp     #EN_RELEASE
        beq     release
        rts

attack: iny                             ; AD: the attack's rate, the high nibble
        lda     (ep),y
        lsr     a
        lsr     a
        lsr     a
        and     #$1E                    ; x 2: a word's index
        tay
        lda     elo,x
        clc
        adc     sid_env_attack,y
        sta     elo,x
        lda     SID_LVL,x
        adc     sid_env_attack + 1,y
        bcs     @full
        sta     SID_LVL,x
        cmp     #>EN_FULL
        bcc     done
@full:  lda     #<EN_FULL
        sta     elo,x
        lda     #>EN_FULL
        sta     SID_LVL,x
        lda     #EN_DECAY
        sta     ephase,x
done:   rts

decay:  iny                             ; SR: the sustain level, its high nibble x $11
        iny
        lda     (ep),y
        and     #$F0
        sta     et
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        ora     et
        sta     et
        dey                             ; AD: the decay's rate, the low nibble
        lda     (ep),y
        jsr     rate
        lda     SID_LVL,x               ; at the sustain level or under: it stays
        cmp     et
        bcc     done
        bne     @down
        lda     elo,x
        beq     done
@down:  lda     elo,x
        sec
        sbc     es
        sta     elo,x
        lda     SID_LVL,x
        sbc     es + 1
        bcc     @sus                    ; below 0: the sustain level
        sta     SID_LVL,x
        cmp     et
        bcs     done
@sus:   lda     et
        sta     SID_LVL,x
        stz     elo,x
        rts

release:
        iny                             ; SR: the release's rate, the low nibble
        iny
        lda     (ep),y
        jsr     rate
        lda     elo,x
        sec
        sbc     es
        sta     elo,x
        lda     SID_LVL,x
        sbc     es + 1
        sta     SID_LVL,x
        bcs     done
        stz     elo,x                   ; quiet
        stz     SID_LVL,x
        stz     ephase,x
        rts

; es: sid_env_decay's step for the rate in A's low nibble (X kept).
rate:   and     #$0F
        asl     a
        tay
        lda     sid_env_decay,y
        sta     es
        lda     sid_env_decay + 1,y
        sta     es + 1
        rts

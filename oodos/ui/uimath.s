; OOUI's arithmetic for the widgets that scale a value to cells or pointer
; units and back (bars, scroll bars, sliders; U2, proposed): linked with
; the first of them a program uses.
;
;   SCALE   sr = sv x su / sa, rounded down (sa 0: 0); 16 bits each, the
;           product 32; a quotient past 16 bits is held at $FFFF (X2)

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .export SCALE, sv, su, sa, sr

        .bss
sv:     .res    2
su:     .res    2
sa:     .res    2
sr:     .res    2
prod:   .res    4                      ; sv x su
rem:    .res    2
n:      .res    1
ov:     .res    1                      ; not 0: the quotient is past 16 bits

        .segment "UCODE"

SCALE:  stz     prod                    ; prod = sv x su
        stz     prod + 1
        stz     prod + 2
        stz     prod + 3
        lda     sv
        sta     sr
        lda     sv + 1
        sta     sr + 1
        ldx     #16
@m:     lsr     sr + 1
        ror     sr
        bcc     @s
        clc
        lda     prod + 2
        adc     su
        sta     prod + 2
        lda     prod + 3
        adc     su + 1
        sta     prod + 3
@s:     ror     prod + 3
        ror     prod + 2
        ror     prod + 1
        ror     prod
        dex
        bne     @m
        stz     sr                      ; sr = prod / sa, held at $FFFF
        stz     sr + 1
        stz     ov
        lda     sa
        ora     sa + 1
        beq     @r
        stz     rem
        stz     rem + 1
        lda     #32
        sta     n
@d:     asl     prod
        rol     prod + 1
        rol     prod + 2
        rol     prod + 3
        rol     rem
        rol     rem + 1
        bcs     @sub                    ; past 16 bits: more than sa
        lda     rem
        cmp     sa
        lda     rem + 1
        sbc     sa + 1
        bcc     @z
@sub:   lda     rem
        sbc     sa
        sta     rem
        lda     rem + 1
        sbc     sa + 1
        sta     rem + 1
        sec
        bra     @q
@z:     clc
@q:     rol     sr
        rol     sr + 1
        bcc     @k
        inc     ov                      ; (32 steps: at most 16 overflow, ov can't wrap)
@k:     dec     n
        bne     @d
        lda     ov
        beq     @r
        lda     #$FF
        sta     sr
        sta     sr + 1
@r:     rts

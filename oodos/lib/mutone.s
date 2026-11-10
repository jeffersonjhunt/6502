; OOLIB's music: MU_TONE (include/oolib.inc, "Sound: music"; hw/oolib.md,
; L6b), GW-BASIC's SOUND. A/X a block of MU_NLEN: +0 the voice, +1-2 a
; frequency in Hz (0 a rest), +3-4 the length in ms, +5-6 the gate time, as
; MU_NOTE's. The register's value is Hz x 2^24 / the clock: Hz x 4,359 / 256
; on PAL (17.0284 a Hz), x 4,200 / 256 on NTSC (16.4044), within 0.01%. Past
; the register's top ($FFFF: 3,848 Hz PAL, 3,995 NTSC) it is held there, so a
; higher SOUND plays the highest tone. Carry and A as mu.s's calls.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "mu.inc"

        .bss
hz:     .res    2
k:      .res    2
prod:   .res    4

        .code
MU_TONE:
        sta     MUP
        stx     MUP + 1
        ldy     #1
        lda     (MUP),y
        sta     hz
        iny
        lda     (MUP),y
        sta     hz + 1
        ldx     MUCLK
        lda     klo,x
        sta     k
        lda     khi,x
        sta     k + 1
        stz     prod                    ; prod = hz x k, 32 bits
        stz     prod + 1
        stz     prod + 2
        stz     prod + 3
        ldx     #16
@bit:   asl     prod
        rol     prod + 1
        rol     prod + 2
        rol     prod + 3
        asl     k
        rol     k + 1
        bcc     @next
        clc
        lda     prod
        adc     hz
        sta     prod
        lda     prod + 1
        adc     hz + 1
        sta     prod + 1
        bcc     @next
        inc     prod + 2
        bne     @next
        inc     prod + 3
@next:  dex
        bne     @bit
        lda     prod + 3                ; / 256, held at $FFFF
        bne     @top
        lda     prod + 1
        sta     MUF
        lda     prod + 2
        sta     MUF + 1
        bra     @time
@top:   lda     #$FF
        sta     MUF
        sta     MUF + 1
@time:  ldy     #3
        ldx     #0
@t:     lda     (MUP),y
        sta     MUL,x
        iny
        inx
        cpx     #4
        bne     @t
        lda     (MUP)
        jmp     MUQ

        .rodata
klo:    .byte   <4359, <4200
khi:    .byte   >4359, >4200

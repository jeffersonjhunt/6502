; CLEAR.COM: clears the screen. It writes ESC E, the screen's clear (bios.s,
; crt.s), which the BIOS sends on the serial line as ANSI's ESC [H ESC [2J, so
; the text screen and a terminal on the serial line are both cleared, the
; cursor at the top left. The tail is ignored.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER

        .code
start:  lda     #<clear
        ldx     #>clear
        ldy     #C_WRITESTR
        jsr     BDOS
        jmp     WBOOT

        .rodata
clear:  .byte   27, 'E', '$'

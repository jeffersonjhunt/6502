; IRQTEST: a program running with interrupts on, for test_oodos_sys.cpp,
; which gives the CPU an IRQ meanwhile: OODOS's handler returns from it and
; the program goes on to say so.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER

        .code
start:  cli
        ldx     #0
        ldy     #40                     ; about 50,000 cycles
@wait:  dex
        bne     @wait
        dey
        bne     @wait
        sei
        lda     #<msg
        ldx     #>msg
        ldy     #C_WRITESTR
        jsr     BDOS
        jmp     WBOOT

        .rodata
msg:    .byte   "IRQ RETURNED", 13, 10, '$'

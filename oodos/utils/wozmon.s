; WOZMON.COM: leave OODOS for the ROM BIOS's Woz Monitor (function 75, A = 0),
; as Break does. Not OODOS's to come back from: the monitor starts afresh with
; the BIOS's windows, its cards quiet and its text screen, and a reset (or
; C80CR, with the card still in) boots OODOS again. The tail is ignored.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER

        .code
start:  lda     #0                      ; the monitor
        ldy     #O_ROM
        jsr     BDOS
        jmp     WBOOT                   ; never: 75 with A = 0 doesn't return

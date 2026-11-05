; OVLZP: an overlay with zero page of its own, which function 59 refuses
; (test_oodos_rsx; hw/oodos.md, "The .PRL file").

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER

        .zeropage
zv:     .res    2

        .code
start:  lda     zv
        rts

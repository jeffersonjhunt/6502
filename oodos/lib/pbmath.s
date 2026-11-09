; OOLIB's sound modules' arithmetic (lib/pb*.s, L5d): 8 by 8 bits, and 16
; by 8. Not in oolib.inc.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .zeropage
PBP:    .res    2                       ; a product, a dividend

        .bss
pm:     .res    1
pd:     .res    1

        .code

; PBP = A x X. Y kept.
PBMUL:
        sta     pm
        stx     pd
        stz     PBP
        stz     PBP + 1
        ldx     #8
@bit:   asl     PBP
        rol     PBP + 1
        asl     pm
        bcc     @next
        lda     PBP
        clc
        adc     pd
        sta     PBP
        bcc     @next
        inc     PBP + 1
@next:  dex
        bne     @bit
        rts

; A = PBP / A (PBP's high byte under A, so the quotient fits a byte); the
; remainder in PBP + 1. Y kept.
PBDIV:
        sta     pd
        ldx     #8
@bit:   asl     PBP
        rol     PBP + 1
        bcs     @sub                    ; 9 bits: at least the divisor
        lda     PBP + 1
        cmp     pd
        bcc     @next
@sub:   lda     PBP + 1
        sbc     pd                      ; (carry set either way)
        sta     PBP + 1
        inc     PBP                     ; the quotient's bit
@next:  dex
        bne     @bit
        lda     PBP
        rts

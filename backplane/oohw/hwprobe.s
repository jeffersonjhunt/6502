; HW_PROBE (oohw.inc): which cards answer, by their ID bytes alone
; (hw/diag.md, "Card discovery"). Nothing but the nine ID bytes is read,
; each twice: by LDA abs, whose last fetched byte is the address's high
; byte, and by LDA abs-$FF,X with X = $FF. For an even module (its ID at
; $xx7F) that one starts in the page before and crosses into it, and the
; W65C02S's extra cycle reads the last instruction byte again, so the bus
; last held the page before's high byte: a floating bus that gives back
; the last byte can't read the same both times. For an odd module ($xxFF)
; both end on the same high byte $Cn, but no odd module is $C, so such a
; read fails the nibble test anyway. A card is found when both reads agree
; and the high nibble is its module.

        .setcpu "65C02"
        .include "backplane.inc"
        .include "oohw.inc"

        .code
HW_PROBE:
        ldx     #$FF                    ; the second read's index
.repeat HW_LAST - HW_FIRST + 1, i
        lda     $C000 + (HW_FIRST + i) * $80 + $7F
        sta     HW_IDB + HW_FIRST + i
        lda     $C000 + (HW_FIRST + i) * $80 + $7F - $FF,x
        sta     id2 + HW_FIRST + i
.endrepeat
        stz     HW_FOUND
        stz     HW_FOUND + 1
        ldx     #15
@none:  lda     #HW_NONE                ; modules 0-5 and F have no ID byte
        sta     HW_REV,x
        dex
        bpl     @none
        ldx     #HW_LAST
@mod:   lda     HW_IDB,x
        cmp     id2,x
        bne     @next                   ; the two reads differ: no card
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        stx     mod
        cmp     mod
        bne     @next                   ; not this module's type
        lda     HW_IDB,x
        and     #$0F
        sta     HW_REV,x
        txa                             ; HW_FOUND's bit x
        and     #7
        tay
        lda     #1
@bit:   dey
        bmi     @set
        asl     a
        bra     @bit
@set:   cpx     #8
        bcs     @hi
        ora     HW_FOUND
        sta     HW_FOUND
        bra     @next
@hi:    ora     HW_FOUND + 1
        sta     HW_FOUND + 1
@next:  dex
        cpx     #HW_FIRST
        bcs     @mod
        rts

        .bss
HW_FOUND: .res  2
HW_REV: .res    16
HW_IDB: .res    16
id2:    .res    16
mod:    .res    1

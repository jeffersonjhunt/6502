; OOUI's block A's dispatcher (hw/ooui.md, "U3a: OOUI as a banked
; library"): AENT, the block's first bytes after UICOM, which another
; block far calls through uifar.s's FARA: xsel picks the routine in
; ATAB, and A, X, Y and the flags reach it as the caller left them.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .segment "AENTRY"
AENT:   stx     xsx
        pha
        ldx     xsel
        lda     ATAB,x
        sta     xjmp
        lda     ATAB + 1,x
        sta     xjmp + 1
        pla
        ldx     xsx
        jmp     (xjmp)

        .segment "RODATA"
ATAB:
        .word   HIDEH, REFOCUS
.assert * - ATAB = XA_REFOCUS + 2, error, "ATAB in XA_* order"

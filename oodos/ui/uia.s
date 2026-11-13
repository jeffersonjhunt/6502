; OOUI's block A's dispatcher (hw/ooui.md, "U3a: OOUI as a banked
; library"): AENT, the block's first bytes after UICOM, which another
; block far calls through uifar.s's FARA: xsel picks the routine in
; ATAB, and A, X, Y, the carry and V reach it as the caller left them;
; N and Z don't (its LDAs and PLA set them, as XGO's PLA does: review C5).

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
        .word   HIDEH, REFOCUS, ONE
.assert * - ATAB = XA_ONE + 2, error, "ATAB in XA_* order"

; OOUI's block C's dispatcher (hw/ooui.md, "U3a: OOUI as a banked
; library"): CENT, the block's first bytes after UICOM, which another
; block far calls through uifar.s's FARC: xsel picks the routine in
; CTAB, and A, X, Y, the carry and V reach it as the caller left them;
; N and Z don't (its LDAs and PLA set them, as XGO's PLA does: review C5).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .segment "CENTRY"
CENT:   stx     xsx
        pha
        ldx     xsel
        lda     CTAB,x
        sta     xjmp
        lda     CTAB + 1,x
        sta     xjmp + 1
        pla
        ldx     xsx
        jmp     (xjmp)

        .segment "CRODATA"
CTAB:
        .word   DF1, DF2, DIDLE, FILLROWS, PASS, HIT, MARGIN, SHOWS, CSET, CCLR
.assert * - CTAB = XC_CCLR + 2, error, "CTAB in XC_* order"
        .import FILLROWS                ; uipick.s's: the picker's shown rows into prbuf (U3a, E3)
        .import PASS, HIT, MARGIN, SHOWS, CSET, CCLR ; uicache.s's: the position cache's walks (S2)

        .segment "CCODE"
; ui_poll's (uipoll.s's DFILT and IDLE): the dialogs' filters and idle
; work, whose vectors point into this block.
DF1:    jmp     (udf1)
DF2:    jmp     (udf2)
DIDLE:  jmp     (uidle)

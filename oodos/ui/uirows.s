; The picker's rows, one crossing a draw (hw/ooui.md, "U3a", round 2's E3):
; block C's FILLROWS (uipick.s) fills prbuf with the text and style of every
; row the list shows, from prtop, prn of them, when block B's list starts to
; draw (uilist.s's PFILL); PROWS, the row routine the picker's list names,
; then gives them from prbuf, in whichever block calls it. UICOM's, and
; prbuf UISTATE's, so both blocks reach them.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .bss
prtop:  .res    2                      ; the first row in prbuf
prn:    .res    1                      ; the rows in it, PR_ROWS at most
prsty:  .res    PR_ROWS                ; each row's style (UL_*)
prbuf:  .res    PR_ROWS * PR_LEN       ; each row's text, 0-terminated

        .segment "URODATA"
PROFS:
.repeat PR_ROWS, i
        .word   prbuf + i * PR_LEN
.endrepeat
PRNONE: .byte   0

        .segment "UCODE"

; A list's row routine: A/X the row, Y the list: A/X its text, Y its style.
PROWS:  sec
        sbc     prtop
        cmp     prn
        bcs     @none
        tay
        lda     prsty,y
        pha
        tya
        asl     a
        tay
        lda     PROFS,y
        ldx     PROFS + 1,y
        ply
        rts
@none:  lda     #<PRNONE
        ldx     #>PRNONE
        ldy     #UL_NORMAL
        rts

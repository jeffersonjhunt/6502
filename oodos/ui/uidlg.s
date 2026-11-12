; OOUI's dialogs' records (hw/ooui.md, "U2"; proposed): the message box's
; and the file picker's widgets are records of OOUI's own, after the
; program's table (internal handles ucount on, ui.inc's DLG_RECS of them),
; given to the program by their public handles (UI_MSG, UI_PICKER, and
; $F2-$FC for their widgets: uicore.s's PUB2INT and INT2PUB). Linked with
; the first dialog a program uses.
;
;   DLGON   the records in use (udlgp, utot), all unused at the first call
;           after ui_open
;   DLGREC  A a record's place k (0-12): UIR it, A its internal handle,
;           filled from the 16 bytes at UIQ (its parent a place too, $FF
;           none)

        .setcpu "65C02"
        .include "ooui.inc"
        .include "ui.inc"

        .export DLGON, DLGREC, DLGH

        .bss
UDLG:   .res    DLG_RECS * UW_LEN
k:      .res    1

        .code

DLGON:  lda     udlgp + 1
        bne     @r
        lda     #<UDLG
        sta     udlgp
        lda     #>UDLG
        sta     udlgp + 1
        ldx     #0
@z:     stz     UDLG,x
        inx
        cpx     #DLG_RECS * UW_LEN
        bne     @z
        lda     ucount
        clc
        adc     #DLG_RECS
        sta     utot
@r:     rts
.assert DLG_RECS * UW_LEN < 256, error, "DLGON clears with X"

; A = the internal handle of place A.
DLGH:   clc
        adc     ucount
        rts

DLGREC: sta     k
        jsr     DLGH
        jsr     REC
        ldy     #UW_LEN - 1
@c:     lda     (UIQ),y
        sta     (UIR),y
        dey
        bpl     @c
        ldy     #UW_PARENT              ; the parent's place made a handle
        lda     (UIR),y
        cmp     #UI_NONE
        beq     @f
        jsr     DLGH
        sta     (UIR),y
@f:     ldy     #UW_FLAGS
        lda     (UIR),y
        ora     #UF_DIRTY
        sta     (UIR),y
        lda     k
        jmp     DLGH

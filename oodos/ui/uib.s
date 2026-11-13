; OOUI's block B's dispatcher (hw/ooui.md, "U3a: OOUI as a banked
; library"): BENT, the block's first bytes after UICOM, which another
; block far calls through uifar.s's FARB: xsel picks the routine in
; BTAB, and A, X, Y, the carry and V reach it as the caller left them;
; N and Z don't (its LDAs and PLA set them, as XGO's PLA does: review C5).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .segment "BENTRY"
BENT:   stx     xsx
        pha
        ldx     xsel
        lda     BTAB,x
        sta     xjmp
        lda     BTAB + 1,x
        sta     xjmp + 1
        pla
        ldx     xsx
        jmp     (xjmp)

        .segment "BRODATA"
BTAB:
        .word   BINIT, BWCALL, BPOST, VDIRTY
.assert * - BTAB = XB_VDIRTY + 2, error, "BTAB in XB_* order"

; Every widget module's record (WR_*): block B has them all (U3a).
MODS:   .word   UIW_GROUP, UIW_RADIO, UIW_VSCROLL, UIW_HSCROLL, UIW_LIST, UIW_FIELD, UIW_VBAR, UIW_HBAR
        .word   UIW_VSLIDER, UIW_HSLIDER, UIW_CANVAS, UIW_CHOICE, 0
        .import UIW_GROUP, UIW_RADIO, UIW_VSCROLL, UIW_HSCROLL, UIW_LIST, UIW_FIELD, UIW_VBAR, UIW_HBAR
        .import UIW_VSLIDER, UIW_HSLIDER, UIW_CANVAS, UIW_CHOICE, PNONEW, VDIRTY

        .segment "BBSS"
brec:   .res    2 * UT_COUNT           ; each type's record, 0 for a core type
bvec:   .res    2                      ; BWCALL's handler

        .segment "BCODE"

; ui_open's (REGISTER): ufl, each type's WR_FLAGS, and brec its record;
; and no sprite thumb (a slider's or a scroll bar's) has a last cell (uislider.s, Z3).
BINIT:  jsr     PNONEW
        ldx     #UT_COUNT - 1
@z:     stz     ufl,x
        dex
        bpl     @z
        ldx     #2 * UT_COUNT - 1
@zr:    stz     brec,x
        dex
        bpl     @zr
        ldx     #0
@m:     lda     MODS,x
        sta     UIP
        lda     MODS + 1,x
        sta     UIP + 1
        ora     UIP
        beq     @r
        phx
        lda     (UIP)                   ; WR_TYPE
        tax
        ldy     #WR_FLAGS
        lda     (UIP),y
        sta     ufl,x
        txa
        asl     a
        tax
        lda     UIP
        sta     brec,x
        lda     UIP + 1
        sta     brec + 1,x
        plx
        inx
        inx
        bra     @m
@r:     clc
        rts
.assert WR_TYPE = 0, error, "BINIT's (UIP)"

; WCALL's (uicore.s): the handler at wo of type wtype's module, jumped to
; with UIP on its record, UIR on dh's and A dh, as WCALL did when the
; modules were linked with it; carry set and A 0 if it has none.
BWCALL: lda     wtype
        cmp     #UT_COUNT
        bcs     @none
        asl     a
        tax
        lda     brec,x
        sta     UIP
        lda     brec + 1,x
        sta     UIP + 1
        ora     UIP
        beq     @none
        ldy     wo
        lda     (UIP),y
        sta     bvec
        iny
        lda     (UIP),y
        sta     bvec + 1
        ora     bvec
        beq     @none
        lda     dh
        jsr     REC
        jmp     (bvec)
@none:  lda     #0
        sec
        rts

; PAINT's last call, and THUMBSOFF's: upost, a widget module's (the
; sprite thumbs: the sliders' and scroll bars').
BPOST:  jmp     (upost)

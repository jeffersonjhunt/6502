; OOUI's crossings between its blocks (hw/ooui.md, "U3a: OOUI as a banked
; library"): UICOM's stubs, which far call a block's dispatcher (uia.s,
; uib.s, uic.s: its first bytes after UICOM), and the selector they read.
; Only these name another block's symbol (AENT, BENT, CENT: mkbank.py lets
; them). The XGO macro (ui.inc) sets the selector and calls the stub,
; keeping A, X, Y, the carry and V; FARCALL keeps A, X, Y and the flags across.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "ooui.inc"
        .include "ui.inc"
        .import FARCALL, BLK_A, BLK_B, BLK_C

        .bss
xsel:   .res    1                      ; the selector: an index into the dispatcher's table, x 2
xsx:    .res    1                      ; the dispatcher's X, kept
xjmp:   .res    2                      ; and its target

        .segment "UCODE"
FARA:   jsr     FARCALL
        .byte   <BLK_A
        .word   AENT
        rts
FARB:   jsr     FARCALL
        .byte   <BLK_B
        .word   BENT
        rts
FARC:   jsr     FARCALL
        .byte   <BLK_C
        .word   CENT
        rts

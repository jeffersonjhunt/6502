; OVL: test_oodos_rsx's overlay, a .PRL for function 59 (hw/oodos.md, "The
; RSXs, phase 5"): no zero page of its own, and every kind of page
; relocation (an absolute operand, a JSR, an address word, a high byte as an
; immediate), over a few records so the loader reads its image and bitmap
; in parts. Nothing runs it: test_oodos_rsx compares the bytes 59 left with
; mkprl.py's load of the same file.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER

        .code
start:  jsr     sub
        lda     table
        ldx     #>table
        ldy     #<table
        jmp     start
sub:    lda     words + 1
        rts

        .rodata
table:  .byte   1, 2, 3
words:  .word   start, sub, table, words
        .res    300, $5A                ; past two records
        .word   start
        .res    700, $A5                ; and past a bitmap record's first 1,024 bytes
        .word   sub, table

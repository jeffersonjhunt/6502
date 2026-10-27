; VT52TEST: the CRT's VT52 subset and lowercase (bios.s, crt.s), for
; test_oodos_sys.cpp to find on the video card's text screen. A character
; at a time by function 2, since ESC Y's operands can be '$'.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER

ESC     = 27
.macro  at      row, col
        .byte   ESC, 'Y', 32 + row, 32 + col
.endmacro

        .code
start:  ldx     #0
@next:  lda     text,x
        beq     @done
        phx
        ldy     #C_WRITE
        jsr     BDOS
        plx
        inx
        bne     @next
@done:  jmp     WBOOT

        .rodata
text:   .byte   ESC, 'E'                ; clear
        at      29, 0                   ; a scroll: rows 28 and 29
        .byte   "BOTTOM", 13, 10, "NEXT"
        .byte   ESC, 'H', "Line one", 13, 10, "lower case"
        at      5, 10
        .byte   "AT 5,10"
        .byte   ESC, 'H', ESC, 'B', ESC, 'B', ESC, 'B', ESC, 'C', ESC, 'C', "X"
        at      10, 0
        .byte   "ERASE ME TO THE END"
        at      10, 5
        .byte   ESC, 'K'
        at      12, 0
        .byte   "abc", ESC, 'D', ESC, 'D', "Z", ESC, 'A', "U"
        at      20, 0
        .byte   "JJJ", 13, 10, "KKK", 13, 10, "LLL"
        at      21, 1
        .byte   ESC, 'J'
        at      14, 0
        .byte   "AB", 8, "C"
        at      15, 0
        .byte   13, 9, "T"              ; a tab from column 0
        at      16, 78
        .byte   "WRAP"
        at      25, 0
        .byte   7, "END"
        .byte   0

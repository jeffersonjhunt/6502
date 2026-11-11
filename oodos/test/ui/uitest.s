; UITEST: OOUI's core and text backend (ui/*.s, phase U1), for
; test_oodos_ui.cpp.
; It opens a 256-byte event area at AREA (layout 2), gives it to OOLIB
; (GX_INIT, GX_PALINIT, GX_SPRINIT) and says "O aa xx cc", then waits for a
; command, spinning with no BDOS call, as GXTEST does: the test puts a
; letter in MBOX and A, X and Y after it. Each command calls a routine with
; them and prints "c aa xx cc": A and X as it returned them, cc 01 if the
; carry was set. 'p' is ui_poll until UE_NONE: each event "E cc xx yy vvvv"
; (its code, its widget, its Y, UV_VALUE), then "p 00 xx 00". The program is
; below $4000 whole (its code, its BSS, OOUI's and OOLIB's), as the W1
; contract wants.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"

        OODOS_HEADER
        .include "printzp.inc"

AREA    = $8000
MBOX    = $8100
TEXTBUF = $8200                         ; a string the test writes (c_text)

        .zeropage
rax:    .res    1
rxx:    .res    1
rc:     .res    1
letter: .res    1

        .code
start:  stz     MBOX
        lda     #<oblk
        ldx     #>oblk
        ldy     #O_FRAME
        jsr     BDOS
        lda     #<AREA
        ldx     #>AREA
        jsr     GX_INIT
        bcs     @say
        jsr     GX_PALINIT
        bcs     @say
        jsr     GX_SPRINIT
@say:   stx     rxx
        pha
        lda     #0
        rol     a
        sta     rc
        lda     #'O'
        jsr     PUTC
        pla
        jsr     report
wait:   lda     MBOX
        beq     wait
        sta     letter
        stz     MBOX
        ldx     #0
@find:  cmp     cmds,x
        beq     @go
        inx
        inx
        inx
        cpx     #CMDS_LEN
        bne     @find
        bra     wait
@go:    lda     cmds + 1,x
        sta     call + 1
        lda     cmds + 2,x
        sta     call + 2
        lda     MBOX + 1
        ldx     MBOX + 2
        ldy     MBOX + 3
        jsr     call
        stx     rxx
        pha
        lda     #0
        rol     a
        sta     rc
        lda     letter
        jsr     PUTC
        pla
        jsr     report
        bra     wait

; " aa xx cc" (A, rxx, rc) and the line's end.
report: pha
        jsr     SPACE
        pla
        jsr     HEX2
        jsr     SPACE
        lda     rxx
        jsr     HEX2
        jsr     SPACE
        lda     rc
        jsr     HEX2
        jmp     CRLF

; ui_open with block A (0-2).
c_open: asl     a
        tax
        lda     blocks + 1,x
        pha
        lda     blocks,x
        plx
        jmp     ui_open

; ui_theme A: 0 the default, 1 Turbo Vision's, 2 the default's colours in
; groups 2 (the base), 3 (the overlay) and 4 (dimmed, TF_DIM); 3 the same
; with TF_DIM clear.
c_theme:
        cmp     #1
        beq     @tv
        cmp     #2
        beq     @grp
        cmp     #3
        beq     @grp
        lda     #0
        tax
        jmp     ui_theme
@tv:    lda     #<UI_TURBO
        ldx     #>UI_TURBO
        jmp     ui_theme
@grp:   pha
        ldx     #TH_LEN - 1
@c:     lda     UI_HOUSE,x
        sta     tgroups,x
        dex
        bpl     @c
        pla
        cmp     #3
        lda     #TF_SHADOW | TF_DIM
        bcc     @dim
        lda     #TF_SHADOW
@dim:   sta     tgroups + TH_FLAGS
        lda     #2
        sta     tgroups + TH_GROUP
        inc     a
        sta     tgroups + TH_OVERGROUP
        inc     a
        sta     tgroups + TH_DIMGROUP
        lda     #<tgroups
        ldx     #>tgroups
        jmp     ui_theme

; ui_poll until UE_NONE, each event printed.
c_poll: jsr     ui_poll
        cmp     #UE_NONE
        beq     @done
        sta     rax
        stx     rxx
        sty     rc
        lda     #'E'
        jsr     PUTC
        jsr     SPACE
        lda     rax
        jsr     HEX2
        jsr     SPACE
        lda     rxx
        jsr     HEX2
        jsr     SPACE
        lda     rc
        jsr     HEX2
        jsr     SPACE
        lda     UI_EV + UV_VALUE
        ldx     UI_EV + UV_VALUE + 1
        jsr     HEX4
        jsr     CRLF
        bra     c_poll
@done:  clc
        lda     #0
        rts

; ui_flags: Y the handle, A to set, X to clear (as the command gives them).
c_flags = ui_flags

; ui_setvalue Y, A/X; ui_getvalue Y.
c_setv  = ui_setvalue
c_getv  = ui_getvalue

; ui_rect A: A/X its column and row, then (as "R ww hh 00") its size.
c_rect: jsr     ui_rect
        php
        lda     UI_RECT + 2
        sta     rax
        lda     UI_RECT + 3
        sta     rxx
        lda     UI_RECT
        ldx     UI_RECT + 1
        plp
        rts

; ui_settext Y with the test's string at TEXTBUF.
c_text: lda     #<TEXTBUF
        ldx     #>TEXTBUF
        jmp     ui_settext

c_wboot:
        jmp     WBOOT

        .rodata
.macro CMD l, r
        .byte   l
        .word   r
.endmacro
cmds:   CMD     'o', c_open
        CMD     'c', ui_close
        CMD     'p', c_poll
        CMD     't', c_theme
        CMD     's', ui_show
        CMD     'h', ui_hide
        CMD     'f', ui_focus
        CMD     'g', ui_getfocus
        CMD     'r', c_rect
        CMD     'v', c_setv
        CMD     'V', c_getv
        CMD     'F', c_flags
        CMD     'd', ui_dirty
        CMD     'D', ui_redraw
        CMD     'i', ui_info
        CMD     'x', c_text
        CMD     'S', ui_suspend
        CMD     'U', ui_resume
        CMD     '!', c_wboot
CMDS_LEN = * - cmds

; ---------------------------------------------------------------------------
; The scenes.

blocks: .word   blk_main, blk_one, blk_desk

; 0: the main scene, menus and overlay windows on layer 1.
blk_main:
        .word   AREA, main
        .byte   main_COUNT
        .word   0, keys
        .byte   0, 1, 0, 1, 0, 0, 0
; 1: the same on one layer (UO_OVER $FF): the menus over the widgets.
blk_one:
        .word   AREA, main
        .byte   main_COUNT
        .word   0, keys
        .byte   0, $FF, 0, 1, 0, 0, 0
; 2: the desktop and its menu bar alone: the main table's first records.
blk_desk:
        .word   AREA, main
        .byte   I_ABOUT + 1
        .word   0, 0
        .byte   0, 1, 0, 1, 0, 0, 0
.assert blk_one - blk_main = UO_LEN, error, "a block is UO_LEN"

; HID usages
U_A     = $04
U_O     = $12
U_Q     = $14
U_S     = $16
U_T     = $17
U_X     = $1B
U_F1    = $3A

        UI_BEGIN main
        UI_DESKTOP W_DESK
        UI_MENUBAR W_BAR, W_DESK
        UI_MENU    M_FILE, W_BAR, "&File"
        UI_ITEM    I_OPEN, M_FILE, "&Open...", U_O, UK_CTRL, 0
        UI_ITEM    I_SAVE, M_FILE, "&Save", U_S, UK_CTRL, 0
        UI_SEP     I_SEP, M_FILE
        UI_ITEM    I_EXIT, M_FILE, "E&xit", U_X, UK_ALT, 0
        UI_MENU    M_EDIT, W_BAR, "&Edit"
        UI_ITEM    I_CUT, M_EDIT, "Cu&t", 0, 0, 0
        UI_ITEM    I_COPY, M_EDIT, "&Copy", 0, 0, UF_DISABLED
        UI_MENU    M_HELP, W_BAR, "&Help"
        UI_ITEM    I_ABOUT, M_HELP, "&About", U_F1, 0, 0
        UI_WINDOW  W_MAIN, W_DESK, 6, 3, 46, 15, "OOUI", UF_CLOSE
        UI_LABEL   L_HEAD, W_MAIN, 1, 1, -1, "Phase U1: the core and the text backend", UF_CENTRE
        UI_PANEL   P_OPTS, W_MAIN, 1, 3, 20, 6, "Options", UF_FRAME
        UI_CHECK   C_SHAD, P_OPTS, 1, 1, 0, "&Shadows", 1, 0
        UI_CHECK   C_MOUS, P_OPTS, 1, 2, -1, "&Mouse", 0, 0
        UI_LABEL   L_RIGHT, W_MAIN, 23, 4, 20, "Right-aligned", UF_RIGHT
        UI_LABEL   L_DIS, W_MAIN, 23, 5, 20, "Dis&abled", UF_DISABLED
        UI_BUTTON  B_OK, W_MAIN, -24, -2, 10, "&OK", UF_DEFAULT
        UI_BUTTON  B_CAN, W_MAIN, -12, -2, 10, "&Cancel", UF_CANCEL
        UI_WINDOW  W_SIDE, W_DESK, 48, 12, 28, 9, "Second", 0
        UI_BUTTON  B_TOG, W_SIDE, 2, 2, 12, "&Toggle", UF_TOGGLE
        UI_LABEL   L_SIDE, W_SIDE, 2, 4, -1, "In front of the first", 0
        UI_WINDOW  W_OVER, W_DESK, 20, 19, 34, 8, "Overlay", UF_OVERLAY | UF_HIDDEN | UF_CLOSE
        UI_LABEL   L_OVER, W_OVER, 2, 1, -1, "On layer 1, over layer 0", 0
        UI_BUTTON  B_HIDE, W_OVER, 2, 3, 10, "&Hide", 0
        ; Sizes that would wrap (the review's O7): a label past its window's
        ; edge with a width of 0, a window whose negative height passes the
        ; bottom. Both are nothing when shown.
        UI_WINDOW  W_TALL, W_DESK, 10, 26, 20, -6, "Tall", UF_HIDDEN
        UI_LABEL   L_EDGE, W_SIDE, 30, 1, 0, "Edge", UF_HIDDEN
        ; A framed window of no width at column 0 (round 2's W4): its client
        ; area is 0 wide, not 254, so its label is nothing too.
        UI_WINDOW  W_THIN, W_DESK, 0, 27, -80, 3, "Thin", UF_HIDDEN
        UI_LABEL   L_THIN, W_THIN, 0, 0, 0, "Thin label", 0
        UI_END     main

; The accelerator table: Ctrl+Q presses Cancel, F1 chooses About, Ctrl+T
; puts the focus on Toggle.
keys:   .byte   U_Q, UK_CTRL, B_CAN, UA_PRESS
        .byte   U_F1, 0, I_ABOUT, UA_PRESS
        .byte   U_T, UK_CTRL, B_TOG, UA_FOCUS
        .byte   0

        .bss
tgroups: .res   TH_LEN                  ; c_theme's theme 2

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0                       ; 256 bytes
; The call, its operand written for each command (in DATA: written).
call:   jmp     $FFFF

        .code
        .include "print.inc"

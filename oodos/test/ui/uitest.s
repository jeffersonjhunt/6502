; UITEST: OOUI (ui/*.s, phases U1 and U2), for test_oodos_ui.cpp.
; It opens a 256-byte event area at AREA (layout 2), gives it to OOLIB
; (GX_INIT, GX_PALINIT, GX_SPRINIT) and says "O aa xx cc", then waits for a
; command, spinning with no BDOS call, as GXTEST does: the test puts a
; letter in MBOX and A, X and Y after it. Each command calls a routine with
; them and prints "c aa xx cc": A and X as it returned them, cc 01 if the
; carry was set. 'p' is ui_poll until UE_NONE: each event "E cc xx yy vvvv
; kk xxxx yy ff" (its code, its widget, its Y, UV_VALUE, UV_KIND, UV_X,
; UV_Y, UV_FLAGS), then "p 00 xx 00". The program is
; out of $4000-$7FFF whole, as the W1 contract wants: linked by uitest.cfg,
; its tables, data and BSS below $4000, its code (OOUI's and OOLIB's too)
; from $8000; the area, the mailbox and the buffers at $3000-$38FF are
; absolute, in its own memory below $4000 that its image leaves free.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"

; Four programs from this source (U2): UIVAR 0 UITEST, the U1 scenes; 1
; UITWID, the lists, field, choice, group, scroll bars and canvas; 2
; UITBAR, the bars, sliders and the message box; 3 UITPICK, the picker.
; Each with all of OOUI is past what one region outside W1 holds.
.ifndef UIVAR
UIVAR   = 0
.endif

; The header: OODOS_HEADER's, but the image runs to CODE's end (uitest.cfg)
; and its BSS is in it.
        .import __OODOS_FLAGS__, __TPA_START__, __CODE_RUN__, __CODE_SIZE__
        .import __ZP_START__, __ZEROPAGE_SIZE__
        .segment "HEADER"
        .byte   OODOS_MAGIC
        .byte   <__OODOS_FLAGS__
        .byte   >__TPA_START__
        .word   __CODE_RUN__ + __CODE_SIZE__ - __TPA_START__
        .word   0
        .byte   <__ZP_START__, <__ZEROPAGE_SIZE__
        .byte   0, 0
        .word   0
        .include "printzp.inc"

        .segment "STARTUP"              ; $0300 (uitest.cfg: the code is at $8000)
        jmp     start

AREA    = $3000
MBOX    = $3100
TEXTBUF = $3200                         ; a string the test writes (c_text)
WORK    = $3400                         ; the picker's work buffer: 64 files (U2)
WORKLEN = 64 * UP_ELEN
RESULT  = $3800                         ; the picker's result: 16 entries

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
        jsr     SPACE                   ; (U2) UV_KIND, UV_X, UV_Y, UV_FLAGS
        lda     UI_EV + UV_KIND
        jsr     HEX2
        jsr     SPACE
        lda     UI_EV + UV_X
        ldx     UI_EV + UV_X + 1
        jsr     HEX4
        jsr     SPACE
        lda     UI_EV + UV_Y
        jsr     HEX2
        jsr     SPACE
        lda     UI_EV + UV_FLAGS
        jsr     HEX2
        jsr     CRLF
        jmp     c_poll
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

; U2: ui_msgbox with block A (0-2) (UITBAR), ui_pick with block A (0-2)
; (UITPICK), ui_setrange (Y the handle, A/X the range).
.if UIVAR = 2
c_msg:  asl     a
        tax
        lda     msgs + 1,x
        pha
        lda     msgs,x
        plx
        jmp     ui_msgbox
.endif
.if UIVAR = 3
c_pick: asl     a
        tax
        lda     picks + 1,x
        pha
        lda     picks,x
        plx
        jmp     ui_pick
.endif
c_range = ui_setrange

.if UIVAR = 1
; The second list's rows (U2): "Item nn"; every third current, the one
; after it dim, row 5 marked.
rowfn:  sta     rax
        ldx     #0
@d:     cmp     #10
        bcc     @u
        sbc     #10
        inx
        bra     @d
@u:     ora     #'0'
        sta     rbuf + 6
        txa
        ora     #'0'
        sta     rbuf + 5
        lda     rax
        ldy     #UL_NORMAL
@m:     cmp     #3
        bcc     @s
        sbc     #3
        bra     @m
@s:     cmp     #0
        bne     @dim
        ldy     #UL_CURRENT
        bra     @k
@dim:   cmp     #1
        bne     @k
        ldy     #UL_DIM
@k:     lda     rax
        cmp     #5
        bne     @r
        tya
        ora     #UL_MARKED
        tay
@r:     lda     #<rbuf
        ldx     #>rbuf
        rts

.endif
.if UIVAR = 3
; The picker's column (U2): "c" and the row's number, for each file.
colfn:  tya
        and     #7
        ora     #'0'
        sta     cbuf + 1
        lda     #<cbuf
        ldx     #>cbuf
        rts

.endif

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
.if UIVAR = 2
        CMD     'm', c_msg
.endif
.if UIVAR = 3
        CMD     'k', c_pick
        CMD     'l', ui_relist
.endif
        CMD     'q', c_range
        CMD     '!', c_wboot
CMDS_LEN = * - cmds

; ---------------------------------------------------------------------------
; The scenes.

blocks: .word   blk_main, blk_one, blk_desk
.if UIVAR = 1 || UIVAR = 2
        .word   blk_u2, blk_u2one
.endif

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
.if UIVAR = 1 || UIVAR = 2
; 3: U2's widgets, its sprites 0 (the pointer) and 1 (a slider's thumb).
blk_u2: .word   AREA, u2
        .byte   u2_COUNT
        .word   0, keys2
        .byte   0, 1, 0, 2, 0
        .word   u2_WIDGETS
; 4: the same on one layer.
blk_u2one:
        .word   AREA, u2
        .byte   u2_COUNT
        .word   0, keys2
        .byte   0, $FF, 0, 2, 0
        .word   u2_WIDGETS
.endif

.if UIVAR = 1
U_F5    = $3E
U_F6    = $3F
; F5 and F6: the vertical scroll bar up and down (UA_UP, UA_DOWN).
keys2:  .byte   U_F5, 0, X_VSC, UA_UP
        .byte   U_F6, 0, X_VSC, UA_DOWN
        .byte   0

        UI_BEGIN u2
        UI_DESKTOP X_DESK
        UI_WINDOW  X_WIN, X_DESK, 2, 1, 76, 28, "Widgets", 0
        UI_GROUP   X_GRP, X_WIN, 1, 0, 20, 6, "Mode", 0, UF_FRAME
        UI_RADIO   X_R0, X_GRP, 0, 0, 0, "&Low", 0
        UI_RADIO   X_R1, X_GRP, 0, 1, 0, "Me&dium", 0
        UI_RADIO   X_R2, X_GRP, 0, 2, 0, "&High", 0
        UI_CHOICE  X_CHO, X_WIN, 23, 0, 20, choices, 3, 1, 0
        UI_FIELD   X_FLD, X_WIN, 23, 2, 20, fieldbuf, 16, 0
        UI_LIST    X_LST, X_WIN, 1, 6, 30, 10, rows, ROWS_N, UF_SCROLL
        UI_VSCROLL X_VSC, X_WIN, 32, 6, 10, 50, 10, 0
        UI_LIST    X_LS2, X_WIN, 36, 6, 14, 10, rowfn, 30, UF_ROUTINE | UF_MULTI
        UI_CANVAS  X_CNV, X_WIN, 52, 6, 20, 5, 0
        UI_HSCROLL X_HSC, X_WIN, 1, 17, 30, 100, 20, 0
        UI_LABEL   X_LBL, X_WIN, 1, 19, -1, "Phase U2: lists, a field, a choice, scroll bars", 0
        UI_LABEL   X_OUT, X_GRP, -30, 3, 8, "Outside", UF_HIDDEN
        UI_BUTTON  X_DONE, X_WIN, 1, -2, 12, "D&one", 0
        UI_END     u2

choices: .word  ch0, ch1, ch2
ch0:    .asciiz "Low"
ch1:    .asciiz "Medium"
ch2:    .asciiz "High"
.macro ROW n
        .byte   "Track ", '0' + n / 10, '0' + n .mod 10, 9, '0' + n .mod 4, ":", '0' + n .mod 6, "0", 0
.endmacro
ROWS_N  = 20
rows:
.repeat ROWS_N, i
        .word   row0 + i * 14
.endrepeat
row0:
.repeat ROWS_N, i
        ROW     i
.endrepeat
.endif

.if UIVAR = 2
keys2:  .byte   0
        UI_BEGIN u2
        UI_DESKTOP Y_DESK
        UI_WINDOW  Y_WIN, Y_DESK, 2, 1, 76, 28, "Levels", 0
        UI_VBAR    Y_VBR, Y_WIN, 1, 0, 10, 100, 0
        UI_VSLIDER Y_VSL, Y_WIN, 3, 0, 10, 100, 50, 5, 0, 0
        UI_HBAR    Y_HBR, Y_WIN, 6, 0, 30, 100, 0
        UI_HSLIDER Y_HSL, Y_WIN, 6, 2, 30, 100, 25, 5, 0, 0
        UI_HSLIDER Y_HSP, Y_WIN, 6, 4, 30, 100, 75, 5, 1, UF_SPRITE
        UI_LABEL   Y_LBL, Y_WIN, 6, 6, -1, "Phase U2: bars and sliders", 0
        UI_BUTTON  Y_MSG, Y_WIN, 1, -2, 14, "&Message", 0
        UI_END     u2

; ui_msgbox's blocks: OK; Yes and No (No the default); OK and Cancel.
msgs:   .word   msgA, msgB, msgC
msgA:   .word   textA, titleA
        .byte   UM_OK, 0
msgB:   .word   textB, titleB
        .byte   UM_YESNO, 1
msgC:   .word   textC, titleA
        .byte   UM_OKCANCEL, 0
textA:  .byte   "Hello from OOUI.", $0D, "A second line, longer than the first.", 0
textB:  .asciiz "Quit the program?"
textC:  .byte   "Overwrite", $0D, "the file?", 0
titleA: .asciiz "Note"
titleB: .asciiz "Confirm"
.endif

.if UIVAR = 3
; ui_pick's blocks: 0 every file, several, drives, users, an extra button
; and a column; 1 the TXT and BIN files of M:; 2 a name to save on B:.
picks:  .word   pickA, pickB, pickC
pickA:  .word   ptitleA, 0
        .byte   UP_MULTI | UP_DRIVES | UP_USERS, 0, $FF
        .word   RESULT
        .byte   16, 0
        .word   colfn, pextra, WORK, WORKLEN
        .byte   0
pickB:  .word   ptitleB, ptypes
        .byte   UP_DRIVES, 13, $FF
        .word   RESULT
        .byte   16, 0
        .word   0, 0, WORK, WORKLEN
        .byte   0
pickC:  .word   ptitleC, 0
        .byte   UP_NEW | UP_DRIVES, 2, $FF
        .word   RESULT
        .byte   16, 0
        .word   0, 0, WORK, WORKLEN
        .byte   0
.assert pickB - pickA = UP_LEN, error, "a pick block is UP_LEN"
ptitleA: .asciiz "Load Files"
ptitleB: .asciiz "Text and data on M:"
ptitleC: .asciiz "Save As"
ptypes: .byte   "TXTBIN", 0
pextra: .asciiz "&Mount"
.endif

keys:   .byte   U_Q, UK_CTRL, B_CAN, UA_PRESS
        .byte   U_F1, 0, I_ABOUT, UA_PRESS
        .byte   U_T, UK_CTRL, B_TOG, UA_FOCUS
        .byte   0

        .bss
tgroups: .res   TH_LEN                  ; c_theme's theme 2


        .data
fieldbuf: .res  16                      ; U2's field, empty (UITWID)
rbuf:   .asciiz "Item 00"               ; rowfn's
cbuf:   .asciiz "c0"                    ; colfn's (UITPICK)
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0                       ; 256 bytes
; The call, its operand written for each command (in DATA: written).
call:   jmp     $FFFF

        .code
        .include "print.inc"

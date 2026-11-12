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
; From U3a each is an ordinary .COM, linked with OOUI's program part
; (build/ooui.lib) and test/ui/uiprog.cfg (UISTATE at $8000), and loads
; OOUI.BNK from M:, user 15, at its start (ui_load), unless the test has put 'N' at
; MBOX + 4 before it runs: then 'L' loads it (A 0: the default, A:OOUI.BNK;
; 1: M:OOUI.BNK, user 15; 2: B:OOUI.BNK). 'G' there: not even GX_INIT.
.ifndef UIVAR
UIVAR   = 0
.endif

        OODOS_HEADER
        .include "printzp.inc"


AREA    = $3000
MBOX    = $3100
TEXTBUF = $3200                         ; a string the test writes (c_text)
WORK    = $3400                         ; the picker's work buffer: 64 files (U2)
WORKLEN = 64 * UP_ELEN
RESULT  = $3800                         ; the picker's result: 16 entries
STATE   = $3900                         ; c_state's (UITPICK): the user, the DMA address and segment, the error mode
PDMA    = $3A00                         ; the program's DMA buffer (c_setst)
BIGWORK = $1C00                         ; a work buffer of 256 files, to $2BFF (uitest.cfg: TPA ends below it)

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
        lda     MBOX + 4                ; 'G': no GX_INIT, nor anything after it (U3a: UX_GX)
        cmp     #'G'
        beq     @nol
        lda     #<AREA
        ldx     #>AREA
        jsr     GX_INIT
        bcs     @say
        jsr     GX_PALINIT
        bcs     @say
        jsr     GX_SPRINIT
        bcs     @say
        lda     MBOX + 4                ; OOUI.BNK from M: (U3a), unless the test says not
        cmp     #'N'
        beq     @nol
        jsr     mload
        bra     @say
@nol:   lda     #0
        tax
        clc
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
; with TF_DIM clear; 4 (U3b) the default with TF_HOT.
c_theme:
        cmp     #4
        beq     @hot
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
        bra     @g2
@hot:   ldx     #TH_LEN - 1             ; (U3b) the default, its buttons lit under the pointer
@hc:    lda     UI_HOUSE,x
        sta     tgroups,x
        dex
        bpl     @hc
        lda     #TF_HOT
        tsb     tgroups + TH_FLAGS
        lda     #<tgroups
        ldx     #>tgroups
        jmp     ui_theme
@g2:
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
PRET:                                   ; (test_oodos_ui times a call to here: U3a)
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

; U2: ui_msgbox with block A (0-2) (UITBAR), ui_pick with block A (0-4)
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
; The program's own: its user A, its DMA PDMA, its error mode X, its DMA
; segment $1234 if Y isn't 0, else none (c_setst; Y4b);
; them at STATE (c_state); a search of every file of A: (c_search: A 0 the
; first, else the next; A the BDOS's answer, the entry at PDMA + 32 x A).
c_setst:
        phx
        phy
        ldy     #F_USERNUM
        jsr     BDOS
        pla
        beq     @none
        lda     #$34
        ldx     #$12
        bra     @seg
@none:  lda     #$FF
        tax
@seg:   ldy     #F_DMASEG
        jsr     BDOS
        lda     #<PDMA
        ldx     #>PDMA
        ldy     #F_DMAOFF
        jsr     BDOS
        pla
        ldy     #F_ERRMODE
        jsr     BDOS
        clc
        rts
c_state:
        lda     #$FF
        ldy     #F_USERNUM
        jsr     BDOS
        sta     STATE
        lda     #<(STATE + 1)
        ldx     #>(STATE + 1)
        ldy     #F_DMAGET
        jsr     BDOS
        lda     #<scbq
        ldx     #>scbq
        ldy     #S_SCB
        jsr     BDOS
        sta     STATE + 5
        clc
        rts
; The file whose FCB the test put at TEXTBUF deleted (c_erase; Z5).
c_erase:
        lda     #<TEXTBUF
        ldx     #>TEXTBUF
        ldy     #F_DELETE
        jsr     BDOS
        clc
        rts
; A file made with the name the test put at TEXTBUF, and closed (W2: in
; the first free directory entry, which a file deleted mid-listing left).
c_make: lda     #<TEXTBUF
        ldx     #>TEXTBUF
        ldy     #F_MAKE
        jsr     BDOS
        lda     #<TEXTBUF
        ldx     #>TEXTBUF
        ldy     #F_CLOSE
        jsr     BDOS
        clc
        rts
c_search:
        ldy     #F_SFIRST
        cmp     #0
        beq     @c
        ldy     #F_SNEXT
@c:     lda     #<sfcb
        ldx     #>sfcb
        jsr     BDOS
        clc
        rts
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
        bit     cdec                    ; (the U3b review's round 2, H4: D set)
        bpl     @x
        sed
@x:     rts

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

; The blocks free (U3a's cases: what ui_load took is free again): each
; taken by function 55, a block at a time, till there is none, counted,
; then each given back (57). A the count.
c_free: stz     nfree
@a:     jsr     mcb1
        ldy     #MC_ALLOC
        jsr     BDOS
        cmp     #0
        bne     @back
        lda     nfree                   ; its base kept
        asl     a
        tax
        lda     tmcb
        sta     bases,x
        lda     tmcb + 1
        sta     bases + 1,x
        inc     nfree
        bra     @a
@back:  ldx     nfree
        phx
@f:     dec     nfree
        bmi     @done
        jsr     mcb1
        lda     nfree
        asl     a
        tax
        lda     bases,x
        sta     tmcb
        lda     bases + 1,x
        sta     tmcb + 1
        lda     #<tmcb
        ldx     #>tmcb
        ldy     #MC_FREE
        jsr     BDOS
        bra     @f
@done:  pla
        ldx     #0
        clc
        rts
; Blocks taken and kept, all but A of them free (c_eat), or given back
; (c_release): ui_load's UX_MEMORY.
c_eat:  sta     nleave
        jsr     c_free
        sec
        sbc     nleave
        sta     neaten
        stz     nfree
@a:     lda     nfree
        cmp     neaten
        beq     @done
        jsr     mcb1
        ldy     #MC_ALLOC
        jsr     BDOS
        lda     nfree
        asl     a
        tax
        lda     tmcb
        sta     eaten,x
        lda     tmcb + 1
        sta     eaten + 1,x
        inc     nfree
        bra     @a
@done:  lda     neaten
        clc
        rts
c_release:
        stz     nfree
@f:     lda     nfree
        cmp     neaten
        beq     @done
        jsr     mcb1
        lda     nfree
        asl     a
        tax
        lda     eaten,x
        sta     tmcb
        lda     eaten + 1,x
        sta     tmcb + 1
        lda     #<tmcb
        ldx     #>tmcb
        ldy     #MC_FREE
        jsr     BDOS
        inc     nfree
        bra     @f
@done:  stz     neaten
        clc
        rts
; tmcb: base 0, a block's 1,024 paragraphs, the extra byte 0; A/X it.
mcb1:   stz     tmcb
        stz     tmcb + 1
        stz     tmcb + 2
        lda     #4
        sta     tmcb + 3
        stz     tmcb + 4
        lda     #<tmcb
        ldx     #>tmcb
        rts

; ui_load: A 0 the default (A:OOUI.BNK, user 0), 1 M:OOUI.BNK (user 15),
; 2 B:OOUI.BNK (the current user) (U3a).
c_load: cmp     #0
        bne     @m
        tax
        jmp     ui_load
@m:     cmp     #2
        bne     mload
        lda     #<bfcb
        ldx     #>bfcb
        jmp     ui_load
; M:OOUI.BNK, in user 15 (the test puts it there, out of the picker's
; listings of user 0): the user set for the call and put back after.
mload:  lda     #$FF
        ldy     #F_USERNUM
        jsr     BDOS
        pha
        lda     #15
        ldy     #F_USERNUM
        jsr     BDOS
        lda     #<mfcb
        ldx     #>mfcb
        jsr     ui_load
        php
        sta     rax
        stx     rxx
        pla
        sta     rc
        pla
        ldy     #F_USERNUM
        jsr     BDOS
        lda     rc
        pha
        lda     rax
        ldx     rxx
        plp
        rts

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
        CMD     'T', ui_settext         ; (U3b review, G3: A/X the word itself)
        CMD     'S', ui_suspend
        CMD     'U', ui_resume
.if UIVAR = 2
        CMD     'm', c_msg
.endif
.if UIVAR = 3
        CMD     'k', c_pick
        CMD     'l', ui_relist
        CMD     'e', c_setst
        CMD     'w', c_state
        CMD     'n', c_search
        CMD     'z', c_erase
        CMD     'Z', c_make
.endif
        CMD     'q', c_range
        CMD     'L', c_load
        CMD     'B', c_free
        CMD     'E', c_eat
        CMD     'R', c_release
        CMD     '!', c_wboot
CMDS_LEN = * - cmds

; ---------------------------------------------------------------------------
; The scenes.

blocks: .word   blk_main, blk_one, blk_desk
.if UIVAR = 1 || UIVAR = 2
        .word   blk_u2, blk_u2one
.endif
.if UIVAR = 2
        .word   blk_amp, blk_u3b, blk_own
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
; (U3a, the U2 review's X4: a menu down over the sprite thumb hides it.
; After the rest, so their handles stay as they were; the bar at the top,
; hidden till the test shows it, so the scene's frames are as they were.)
        _UI_REC    Y_BAR, UT_MENUBAR, UI_NONE, 0, 0, 0, 1, UF_HIDDEN, 0, 0, 0, 0
        UI_MENU    Y_MENU, Y_BAR, "&Levels"
        UI_ITEM    Y_I1, Y_MENU, "A first item, as wide as the thumb", 0, 0, 0
        UI_ITEM    Y_I2, Y_MENU, "A second", 0, 0, 0
        UI_ITEM    Y_I3, Y_MENU, "A third", 0, 0, 0
        UI_ITEM    Y_I4, Y_MENU, "A fourth", 0, 0, 0
        UI_ITEM    Y_I5, Y_MENU, "A fifth, over the slider's row", 0, 0, 0
        UI_ITEM    Y_I6, Y_MENU, "A sixth", 0, 0, 0
; (The U3a review's C1: a second menu, whose drop-down covers the thumb
; but not the slider's first cell.)
        UI_MENU    Y_MENU2, Y_BAR, "&Thumb"
        UI_ITEM    Y_J1, Y_MENU2, "Over the thumb, not its start", 0, 0, 0
        UI_ITEM    Y_J2, Y_MENU2, "Two", 0, 0, 0
        UI_ITEM    Y_J3, Y_MENU2, "Three", 0, 0, 0
        UI_ITEM    Y_J4, Y_MENU2, "Four", 0, 0, 0
        UI_ITEM    Y_J5, Y_MENU2, "Five", 0, 0, 0
        UI_ITEM    Y_J6, Y_MENU2, "Six", 0, 0, 0
        UI_END     u2

; 5 (U3a, Z3): a screen shaped as OO-Amp's, to time the sliders' thumbs
; placed after a paint: 15 sliders with sprite thumbs (11 bands, seek,
; volume, balance, the playlist's), each band's label, buttons, bars and
; canvases, 59 records; sprites 0 (the pointer) to 15.
blk_amp: .word  AREA, amp
        .byte   amp_COUNT
        .word   0, keys2
        .byte   0, 1, 0, 16, 0
        .word   amp_WIDGETS

        UI_BEGIN amp
        UI_DESKTOP A_DESK
        UI_WINDOW  A_WIN, A_DESK, 1, 1, 78, 28, "Amp", 0
        UI_LABEL   A_TIME, A_WIN, 1, 0, 10, "00:00", 0
        UI_LABEL   A_TITLE, A_WIN, 12, 0, 40, "A title, scrolling", 0
        UI_CANVAS  A_CTIME, A_WIN, 1, 1, 10, 2, 0
        UI_CANVAS  A_CVIS, A_WIN, 54, 0, 20, 4, 0
        UI_HSLIDER A_SEEK, A_WIN, 1, 4, 50, 1000, 100, 10, 1, UF_SPRITE
        UI_HSLIDER A_VOL, A_WIN, 1, 6, 20, 100, 70, 5, 2, UF_SPRITE
        UI_HSLIDER A_BAL, A_WIN, 24, 6, 20, 100, 50, 5, 3, UF_SPRITE
        UI_VBAR    A_VUL, A_WIN, 46, 5, 3, 100, 0
        UI_VBAR    A_VUR, A_WIN, 48, 5, 3, 100, 0
.repeat 11, i
        UI_VSLIDER .ident(.sprintf("A_EQ%d", i)), A_WIN, 2 + i * 4, 9, 8, 100, 50, 5, 4 + i, UF_SPRITE
.endrepeat
.repeat 11, i
        UI_LABEL   .ident(.sprintf("A_EQL%d", i)), A_WIN, 1 + i * 4, 18, 4, "Band", 0
.endrepeat
        UI_VSLIDER A_PLS, A_WIN, 74, 9, 12, 100, 0, 5, 15, UF_SPRITE
        UI_BUTTON  A_PREV, A_WIN, 1, 20, 6, "&Z", 0
        UI_BUTTON  A_PLAY, A_WIN, 8, 20, 6, "&X", 0
        UI_BUTTON  A_PAUSE, A_WIN, 15, 20, 6, "&C", 0
        UI_BUTTON  A_STOP, A_WIN, 22, 20, 6, "&V", 0
        UI_BUTTON  A_NEXT, A_WIN, 29, 20, 6, "&B", 0
        UI_BUTTON  A_SHUF, A_WIN, 36, 20, 8, "&Shuffle", UF_TOGGLE
        UI_BUTTON  A_REP, A_WIN, 45, 20, 8, "&Repeat", UF_TOGGLE
        UI_BUTTON  A_EQB, A_WIN, 54, 20, 6, "&EQ", UF_TOGGLE
        UI_BUTTON  A_PLB, A_WIN, 61, 20, 6, "&PL", UF_TOGGLE
        UI_BUTTON  A_LOAD, A_WIN, 68, 20, 8, "&Load", 0
.repeat 15, i
        UI_LABEL   .ident(.sprintf("A_ST%d", i)), A_WIN, 1 + (i .mod 4) * 18, 22 + i / 4, 17, "Status", 0
.endrepeat
        UI_END     amp
.assert amp_COUNT = 59, error, "the amp scene's records"

; 6 (U3b): the gaps OO-Amp's port found: a slider with a thumb of the
; program's own (16 x 8, group 1: UF_OWNTHUMB), one with OOUI's; scroll bars
; with sprite thumbs, the program's 8 x 32 and OOUI's 8 x 8; a label in a
; colour of its own; a button for TF_HOT. Sprites 0 (the pointer) to 5
; (5 free, for the U3b review's G3: ui_settext moves a thumb to it).
blk_u3b: .word  AREA, u3b
        .byte   u3b_COUNT
        .word   0, keys2
        .byte   0, 1, 0, 6, 0
        .word   0
; 7 (U3b): the same with UO_OWNMOUSE: the program's pointer, OOUI's hits.
blk_own: .word  AREA, u3b
        .byte   u3b_COUNT
        .word   0, keys2
        .byte   0, 1, 0, 5, UO_OWNMOUSE
        .word   0

        UI_BEGIN u3b
        UI_DESKTOP Z_DESK
        UI_WINDOW  Z_WIN, Z_DESK, 2, 1, 76, 28, "U3b", 0
        UI_HSLIDER Z_OWN, Z_WIN, 6, 2, 30, 100, 50, 5, 1, UF_SPRITE | UF_OWNTHUMB, $11
        UI_HSLIDER Z_STD, Z_WIN, 6, 4, 30, 100, 50, 5, 2, UF_SPRITE
        UI_VSCROLL Z_VSC, Z_WIN, 40, 2, 12, 100, 10, UF_SPRITE | UF_OWNTHUMB, 3, $81
        UI_HSCROLL Z_HSC, Z_WIN, 6, 8, 30, 100, 10, UF_SPRITE, 4
        UI_LABEL   Z_LBL, Z_WIN, 6, 10, 20, "Its own colour", 0, $4E
        UI_BUTTON  Z_BTN, Z_WIN, 6, 12, 12, "&Hot", 0
; (The U3b review's G2: the 8 x 32 thumb, rows 5-8 at 0, its ends covered
; one at a time: a small window over its last row, 8, and a menu bar's
; drop-down over its first, 5, each hidden till the test shows it.)
        UI_WINDOW  Z_COVER, Z_DESK, 42, 8, 4, 3, 0, UF_HIDDEN
        _UI_REC    Z_BAR, UT_MENUBAR, UI_NONE, 0, 0, 0, 1, UF_HIDDEN, 0, 0, 0, 0
        UI_MENU    Z_MENU, Z_BAR, "&Ends"
        UI_ITEM    Z_I1, Z_MENU, "An item wider than the scroll bar's column, as far", 0, 0, 0
        UI_ITEM    Z_I2, Z_MENU, "A second", 0, 0, 0
        UI_ITEM    Z_I3, Z_MENU, "A third", 0, 0, 0
; (U3b, the owner's Q1: a canvas whose cells are the program's, 40 x 4, as
; OO-Amp's zone C scope and VU: crows, rows 18-21 of the window.)
        UI_CANVAS  Z_CNV, Z_WIN, 6, 18, 40, 4, UF_ROUTINE, crows
        UI_END     u3b

; The canvas's row routine (U3b, Q1): row A's 40 cells from ccells, a
; character ($21 + column + row) and an attribute (the column's low four bits
; above, column + 5 x row's below) each; row cnone's 0 (transparent). Row
; 0 maps the program's last card bank into W1 first, as a routine may (OOUI
; maps its own again after). Each row's calls counted in ccalls, and with
; cdec's bit 7 it returns with the decimal flag set (the U3b review's round
; 2: H3, H4).
        .pushseg
        .code
crows:  tax
        inc     ccalls,x
        txa
        cmp     cnone
        beq     @none
        cmp     #0
        bne     @get
        lda     #<((VM_PROG_BANK + VM_PROG_BANKS - 1) * 64)
        ldx     #>((VM_PROG_BANK + VM_PROG_BANKS - 1) * 64)
        jsr     GX_VMAP
        lda     #0
@get:
        asl     a
        tax
        lda     crowp + 1,x
        pha
        lda     crowp,x
        plx
        bra     @out
@none:  lda     #0
        tax
@out:   bit     cdec
        bpl     @r
        sed
@r:     rts
        .popseg
crowp:  .word   ccells, ccells + 80, ccells + 160, ccells + 240
ccells:
.repeat 4, R
.repeat 40, C
        .byte   $21 + C + R, ((C & 15) << 4) | ((C + R * 5) & 15)
.endrepeat
.endrepeat

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
; and a column; 1 the TXT and BIN files of M:; 2 a name to save on B:; 3
; every file of M: into a work buffer of 8; 4 every file of A: into one of
; 256 (BIGWORK).
picks:  .word   pickA, pickB, pickC, pickD, pickE, pickF
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
pickD:  .word   ptitleB, 0
        .byte   0, 13, $FF
        .word   RESULT
        .byte   16, 0
        .word   0, 0, WORK, 8 * UP_ELEN
        .byte   0
pickE:  .word   ptitleA, 0
        .byte   0, 1, $FF
        .word   RESULT
        .byte   16, 0
        .word   0, 0, BIGWORK, 256 * UP_ELEN
        .byte   0
; (U3b) OO-Amp's Load Files: OK is Add and Add all, each leaving the
; picker up (UP_KEEP, UP_ALL), the marks kept; the extra button.
pickF:  .word   ptitleA, 0
        .byte   UP_MULTI | UP_DRIVES | UP_KEEP | UP_ALL, 13, $FF
        .word   RESULT
        .byte   16, 0
        .word   0, pextra, WORK, WORKLEN
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
tmcb:   .res    5                       ; c_free's and c_eat's MCB
nfree:  .res    1
nleave: .res    1
neaten: .res    1
bases:  .res    2 * 20
eaten:  .res    2 * 20


        .data
fieldbuf: .res  16                      ; U2's field, empty (UITWID)
rbuf:   .asciiz "Item 00"               ; rowfn's
cbuf:   .asciiz "c0"                    ; colfn's (UITPICK)
cdec:   .byte   0                       ; bit 7: rowfn and crows return with D set (U3b review round 2, H4)
.if UIVAR = 2
cnone:  .byte   $FF                     ; crows's row given as 0 (the test writes it; U3b, Q1)
ccalls: .res    4                       ; crows's calls for each row (the U3b review's round 2, H3)
.endif
.if UIVAR = 3
scbq:   .byte   $4B, 0                  ; c_state's: the error mode, got
sfcb:   .byte   1, "???????????"        ; c_search's: A:, every name and type, extent 0
        .res    24
.endif
mfcb:   .byte   13, "OOUI    BNK"        ; M:OOUI.BNK (U3a)
        .res    24
bfcb:   .byte   2, "OOUI    BNK"         ; B:OOUI.BNK (a crowded M:'s tests)
        .res    24
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0                       ; 256 bytes
; The call, its operand written for each command (in DATA: written).
call:   jmp     $FFFF

        .code
PRINT_HERE = 1
        .include "print.inc"

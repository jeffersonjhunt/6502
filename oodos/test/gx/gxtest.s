; GXTEST: OOLIB's graphics library (lib/gx*.s, L4; video card revision 2),
; for test_oodos_gfx.cpp.
; It opens a 16-byte event area at AREA ($3800, layout 1) and says OPEN aa
; xx, then waits for a command, spinning with no BDOS call: the test puts a
; letter in MBOX ($3900) and the arguments after it, and pokes the library's
; GX_* variables itself (their addresses from gxtest.lbl). Each command is
; a library routine called with A = MBOX + 1, X = MBOX + 2, Y = MBOX + 3,
; and prints "c aa xx cc": A and X as it returned them, cc 01 if the carry
; was set. The program is below $4000 whole (its code, its BSS, the
; library's), as the W1 contract wants.
;
; For B7 (GX_BANK8, the two-byte W1 cache; test_oodos_gfx.cpp's L1-L8):
;   8   GX_BANK8
;   h   the read-back: A = the north bridge's WIN1H, X = $4000 as W1 shows it
;   s   A stored at $4000, through W1 as it is
;   j   A = 1: a frame hook set (FT_FRAMEHOOK) that puts block 1 back
;       (GX_W1BACK) and maps card page X/Y (GX_VMAP), so it leaves WIN1H as
;       GX_BANK8 made slot 1's window, and counts its calls in HOOKN; A = 0:
;       the hook off. A/X function 80's
;   q   A lines of 60 characters by C_WRITESTR (the console busy)
;
; For full-resolution sprites (hw/sprites640.md, interface version 14):
;   e   GX_SPRFULL (A 1 on, 0 off)
;   t   GX_SPRFPOS (A the sprite; GX_X and GX_Y poked)

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        OODOS_HEADER
        .include "printzp.inc"

AREA    = $3800
MBOX    = $3900
NBWIN1H = $C6A1                         ; the north bridge's WIN1H (a test reads it back)

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
        pha
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

c_init: lda     #<AREA
        ldx     #>AREA
        jmp     GX_INIT

c_init0:
        lda     #0                      ; no event area
        tax
        jmp     GX_INIT

c_wboot:
        jmp     WBOOT

c_w1h:  ldx     $4000
        lda     NBWIN1H
        clc
        rts

c_st:   sta     $4000
        clc
        rts

c_hook: stx     HPAGE
        sty     HPAGE + 1
        cmp     #0
        beq     @off
        stz     HOOKN
        lda     #<hkblk
        ldx     #>hkblk
        bra     @call
@off:   lda     #FT_FRAMEHOOK           ; no block: none
        ldx     #0
@call:  ldy     #O_FRAME
        jsr     BDOS
        clc
        rts

; The frame hook, in W0: block 1 back, then slot 1's window over card page
; HPAGE, left in W1 for OODOS to clear.
FHOOK:  jsr     GX_W1BACK
        lda     HPAGE
        ldx     HPAGE + 1
        jsr     GX_VMAP
        inc     HOOKN
        rts

c_lines:
        sta     qn
@line:  lda     qn
        beq     @done
        lda     #<qline
        ldx     #>qline
        ldy     #C_WRITESTR
        jsr     BDOS
        dec     qn
        bra     @line
@done:  clc
        rts

        .rodata
.macro CMD l, r
        .byte   l
        .word   r
.endmacro
cmds:   CMD     'I', c_init
        CMD     'i', c_init0
        CMD     '@', GX_ASSET
        CMD     'A', GX_VALLOC
        CMD     'a', GX_VFREE
        CMD     'd', GX_VADDR
        CMD     'm', GX_VMAP
        CMD     'v', GX_VPUT
        CMD     'g', GX_VGET
        CMD     'k', GX_VCOPY
        CMD     'f', GX_VFILL
        CMD     'b', GX_BLOCK
        CMD     'w', GX_W1BACK
        CMD     'L', GX_LAYER
        CMD     'l', GX_PALOFS
        CMD     'S', GX_SHOW
        CMD     'r', GX_BORDER
        CMD     'c', GX_CONSOLE
        CMD     'K', GX_PALINIT
        CMD     'E', GX_PALSET
        CMD     'H', GX_PALSHOW
        CMD     'D', GX_FADE
        CMD     'Z', GX_SPRINIT
        CMD     'X', GX_SPRPOS
        CMD     'V', GX_SPRIMG
        CMD     'Y', GX_SPRATTR
        CMD     'z', GX_SPRSHOW
        CMD     'x', GX_COLL
        CMD     'M', GX_TMAP
        CMD     'U', GX_TPUT
        CMD     'u', GX_TGET
        CMD     'W', GX_TFILL
        CMD     'Q', GX_SCROLL
        CMD     'B', GX_BMP
        CMD     'P', GX_PLOT
        CMD     'p', GX_POINT
        CMD     'N', GX_LINE
        CMD     'R', GX_RECT
        CMD     'F', GX_FRECT
        CMD     'C', GX_CIRCLE
        CMD     'O', GX_FLOOD
        CMD     'y', GX_BCOPY
        CMD     'J', GX_FLIP
        CMD     'G', GX_FONT
        CMD     'T', GX_TEXT
        CMD     '!', c_wboot
        CMD     '8', GX_BANK8
        CMD     'h', c_w1h
        CMD     's', c_st
        CMD     'j', c_hook
        CMD     'q', c_lines
        CMD     'e', GX_SPRFULL
        CMD     't', GX_SPRFPOS
CMDS_LEN = * - cmds

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   16
; The call, its operand written for each command (in DATA: written).
call:   jmp     $FFFF
hkblk:  .byte   FT_FRAMEHOOK
        .word   FHOOK
qline:  .byte   "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwx", 13, 10, "$"

        .bss
HOOKN:  .res    1                       ; the frame hook's calls
HPAGE:  .res    2
qn:     .res    1

        .code
        .include "print.inc"

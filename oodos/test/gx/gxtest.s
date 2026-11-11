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

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        OODOS_HEADER
        .include "printzp.inc"

AREA    = $3800
MBOX    = $3900

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
CMDS_LEN = * - cmds

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   16
; The call, its operand written for each command (in DATA: written).
call:   jmp     $FFFF

        .code
        .include "print.inc"

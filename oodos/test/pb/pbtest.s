; PBTEST: OOLIB's sound library (lib/pb*.s, L5), for test_oodos_pb.cpp. It
; says "O 00 00 00" when it has started (it opens no event area), then
; waits for a command, spinning with no BDOS call: the test puts a letter in
; MBOX ($3900) and the arguments after it, and any block the routine takes
; at BUF ($3A00). Each command is a library routine called with A = MBOX + 1,
; X = MBOX + 2, Y = MBOX + 3, and prints "c aa xx cc": A and X as it
; returned them, cc 01 if the carry was set. W is the warm boot.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        OODOS_HEADER
        .include "printzp.inc"

MBOX    = $3900

        .zeropage
rax:    .res    1
rxx:    .res    1
rc:     .res    1
letter: .res    1

        .code
start:  stz     MBOX
        stz     rxx
        stz     rc
        lda     #'O'
        jsr     PUTC
        lda     #0
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

c_wboot:
        jmp     WBOOT

        .rodata
.macro CMD l, r
        .byte   l
        .word   r
.endmacro
cmds:   CMD     'E', PB_EQ
        CMD     'N', PB_EQON
        CMD     'A', PB_EQALL
        CMD     'V', PB_VIS
        CMD     'W', c_wboot
CMDS_LEN = * - cmds

        .data
; The call, its operand written for each command (in DATA: written).
call:   jmp     $FFFF

        .code
        .include "print.inc"

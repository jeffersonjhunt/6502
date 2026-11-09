; PBTEST: OOLIB's sound library (lib/pb*.s, L5), for test_oodos_pb.cpp. It
; says "O 00 00 00" when it has started (it opens no event area), then
; waits for a command, spinning with no BDOS call: the test puts a letter in
; MBOX ($3900) and the arguments after it, and any block the routine takes
; at BUF ($3A00). Each command is a library routine called with A = MBOX + 1,
; X = MBOX + 2, Y = MBOX + 3, and prints "c aa xx cc": A and X as it
; returned them, cc 01 if the carry was set. W is the warm boot. F (L5c)
; waits A frames, and at each reads the SIDs into VBLK ($3B00) by SID_VOICES
; and moves the envelopes by SID_ENV, logging SID_LVL's six bytes at LOG
; ($3C00) a frame (up to 128 frames). L5d: O opens a 256-byte event area
; at AREA ($3800, layout 2); K calls function 82 with A/X a block, k with A
; the operation and no block; L waits A frames (up to 100) and at each
; calls PB_LEVELS into LOG, 10 bytes a frame.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        OODOS_HEADER
        .include "printzp.inc"

MBOX    = $3900

AREA    = $3800
VBLK    = $3B00
LOG     = $3C00

        .zeropage
logp:   .res    2
frames: .res    1
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

c_open: lda     #<oblk
        ldx     #>oblk
        ldy     #O_FRAME
        jsr     BDOS
        clc                             ; (the BDOS gives the flags back as they were)
        rts

c_f82:  ldy     #O_SOUND
        jmp     BDOS

c_f82op:
        ldx     #0
        ldy     #O_SOUND
        jmp     BDOS

c_levels:
        sta     frames
        lda     #<LOG
        sta     logp
        lda     #>LOG
        sta     logp + 1
@frame: lda     frames
        beq     @done
        lda     #FT_WAIT
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        lda     logp
        ldx     logp + 1
        jsr     PB_LEVELS
        bcs     @out
        lda     logp
        clc
        adc     #PBL_LEN
        sta     logp
        bcc     @next
        inc     logp + 1
@next:  dec     frames
        bra     @frame
@done:  lda     #0
        clc
@out:   rts

c_frames:
        sta     frames
        lda     #<LOG
        sta     logp
        lda     #>LOG
        sta     logp + 1
@frame: lda     frames
        beq     @done
        lda     #FT_WAIT
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        lda     #<VBLK
        ldx     #>VBLK
        jsr     SID_VOICES
        lda     #<VBLK
        ldx     #>VBLK
        jsr     SID_ENV
        ldy     #SID_NVOICES - 1
@log:   lda     SID_LVL,y
        sta     (logp),y
        dey
        bpl     @log
        lda     logp
        clc
        adc     #SID_NVOICES
        sta     logp
        bcc     @next
        inc     logp + 1
@next:  dec     frames
        bra     @frame
@done:  lda     #0
        clc
        rts

        .rodata
.macro CMD l, r
        .byte   l
        .word   r
.endmacro
cmds:   CMD     'E', PB_EQ
        CMD     'N', PB_EQON
        CMD     'A', PB_EQALL
        CMD     'V', PB_VIS
        CMD     'S', SID_VOICES
        CMD     'T', SID_NOTE
        CMD     'Z', SID_ENV0
        CMD     'F', c_frames
        CMD     'O', c_open
        CMD     'K', c_f82
        CMD     'k', c_f82op
        CMD     'I', PB_INIT
        CMD     'B', PB_BEGIN
        CMD     'P', PB_PAUSE
        CMD     'R', PB_RESUME
        CMD     'v', PB_VOLUME
        CMD     'Q', PB_STATUS
        CMD     'L', c_levels
        CMD     'W', c_wboot
CMDS_LEN = * - cmds

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0                       ; 256 bytes: layout 2
; The call, its operand written for each command (in DATA: written).
call:   jmp     $FFFF

        .code
        .include "print.inc"

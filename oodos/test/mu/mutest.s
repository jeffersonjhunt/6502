; MUTEST: OOLIB's music (lib/mu*.s, L6b), for test_oodos_mu.cpp. It says
; "O 00 00 00" when it has started, then waits for a command, spinning with
; no BDOS call: the test puts a letter in MBOX ($3900) and A, X and Y after
; it, and any block the routine takes at BUF ($3A00). Each command calls a
; routine with A = MBOX + 1, X = MBOX + 2, Y = MBOX + 3 and prints
; "c aa xx cc": A and X as it returned them, cc 01 if the carry was set. O
; opens a 256-byte event area at AREA ($3800, layout 2); D takes every event
; waiting (function 81's IN_NEXT) and hands each EK_SOUND one's byte to
; MU_EVENT, logging A, X and Y at LOG ($3C00) from LOGN ($3B00, the count,
; 2 bytes) on, and returns A = the events it handed over; W is the warm boot.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        OODOS_HEADER
        .include "printzp.inc"

MBOX    = $3900
AREA    = $3800
LOGN    = $3B00
LOG     = $3C00
LOGMAX  = 1000                          ; 3 bytes each: $3C00-$47B7

        .zeropage
logp:   .res    2
rax:    .res    1
rxx:    .res    1
rc:     .res    1
letter: .res    1
handed: .res    1

        .code
start:  stz     MBOX
        stz     rxx
        stz     rc
        stz     LOGN
        stz     LOGN + 1
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

; Every event waiting; each EK_SOUND one's byte to MU_EVENT, A X Y logged.
c_drain:
        stz     handed
@next:  lda     #IN_NEXT
        sta     eblk
        lda     #<eblk
        ldx     #>eblk
        ldy     #O_INPUT
        jsr     BDOS
        cmp     #0
        beq     @done
        cmp     #EK_SOUND
        bne     @next
        lda     eblk + 1 + EQ_VALUE
        jsr     MU_EVENT
        pha
        phx
        phy
        lda     LOGN                    ; LOG + 3 x LOGN
        sta     logp
        lda     LOGN + 1
        sta     logp + 1
        asl     logp
        rol     logp + 1
        clc
        lda     logp
        adc     LOGN
        sta     logp
        lda     logp + 1
        adc     LOGN + 1
        clc
        adc     #>LOG
        sta     logp + 1
        lda     LOGN + 1                ; full: the rest not logged
        cmp     #>LOGMAX
        bcc     @room
        lda     LOGN
        cmp     #<LOGMAX
        bcc     @room
        ply
        plx
        pla
        bra     @count
@room:  ldy     #2
        pla                             ; Y
        sta     (logp),y
        dey
        pla                             ; X
        sta     (logp),y
        dey
        pla                             ; A
        sta     (logp),y
        inc     LOGN
        bne     @count
        inc     LOGN + 1
@count: inc     handed
        bra     @next
@done:  lda     handed
        ldx     #0
        clc
        rts

        .rodata
.macro CMD l, r
        .byte   l
        .word   r
.endmacro
cmds:   CMD     'I', MU_INIT
        CMD     'Q', MU_QUIT
        CMD     'N', MU_NOTE
        CMD     'T', MU_TONE
        CMD     'S', MU_INSTR
        CMD     'X', MU_STOP
        CMD     'P', MU_PAUSE
        CMD     'R', MU_RESUME
        CMD     'M', MU_MARK
        CMD     'V', MU_VOLUME
        CMD     'C', MU_COUNT
        CMD     'E', MU_EVENT
        CMD     'O', c_open
        CMD     'D', c_drain
        CMD     'W', c_wboot
CMDS_LEN = * - cmds

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0                       ; 256 bytes: layout 2
eblk:   .res    9                       ; IN_NEXT's block: +0 the operation, +1-8 the event
; The call, its operand written for each command (in DATA: written).
call:   jmp     $FFFF

        .code
        .include "print.inc"

; LINETEST: the stand-in BDOS's console functions (bdos.s), for
; test_oodos_sys.cpp, which types when it sees each question:
;
;   VERSION 0031             function 12
;   STAT 00 RAW 00           11 and 6 ($FF) with no key waiting
;   KEY?k GOT 6B             1: a key, echoed
;   RAW? GOT 72              6 ($FD): a key, not echoed
;   WAIT 01 GOT 73           11 until a key waits, then 6 ($FF) reads it
;   Q                        6: out, raw
;   a       b                2: a tab to column 8
;   LINE?... COUNT 08 hello wo   10 into the DMA buffer (A/X = 0), its size 8
;
; then function 0, the warm boot.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  lda     #<vermsg
        ldx     #>vermsg
        jsr     PUTS
        ldy     #S_BDOSVER
        jsr     BDOS
        jsr     HEX4
        jsr     CRLF
        lda     #<statmsg
        ldx     #>statmsg
        jsr     PUTS
        ldy     #C_STAT
        jsr     BDOS
        jsr     HEX2
        lda     #<rawmsg
        ldx     #>rawmsg
        jsr     PUTS
        lda     #$FF
        ldy     #C_RAWIO
        jsr     BDOS
        jsr     HEX2
        jsr     CRLF

        lda     #<keymsg
        ldx     #>keymsg
        jsr     PUTS
        ldy     #C_READ
        jsr     BDOS
        jsr     got
        lda     #<raw2msg
        ldx     #>raw2msg
        jsr     PUTS
        lda     #$FD
        ldy     #C_RAWIO
        jsr     BDOS
        jsr     got
        lda     #<waitmsg
        ldx     #>waitmsg
        jsr     PUTS
@wait:  ldy     #C_STAT
        jsr     BDOS
        cmp     #0
        beq     @wait
        jsr     HEX2
        lda     #$FF
        ldy     #C_RAWIO
        jsr     BDOS
        jsr     got

        lda     #'Q'
        ldy     #C_RAWIO
        jsr     BDOS
        jsr     CRLF
        lda     #'a'
        jsr     PUTC
        lda     #9
        jsr     PUTC
        lda     #'b'
        jsr     PUTC
        jsr     CRLF

        lda     #8                      ; function 10 into the DMA buffer, $0280
        sta     BP_DMA
        lda     #<linemsg
        ldx     #>linemsg
        jsr     PUTS
        lda     #0
        tax
        ldy     #C_READSTR
        jsr     BDOS
        lda     #<countmsg
        ldx     #>countmsg
        jsr     PUTS
        lda     BP_DMA + 1
        jsr     HEX2
        jsr     SPACE
        ldx     #0
@chars: cpx     BP_DMA + 1
        beq     @done
        lda     BP_DMA + 2,x
        phx
        jsr     PUTC
        plx
        inx
        bra     @chars
@done:  jsr     CRLF
        ldy     #P_TERMCPM
        jmp     BDOS

; " GOT hh" and a new line.
got:    pha
        lda     #<gotmsg
        ldx     #>gotmsg
        jsr     PUTS
        pla
        jsr     HEX2
        jmp     CRLF

        .rodata
vermsg: .byte   "VERSION $"
statmsg: .byte  "STAT $"
rawmsg: .byte   " RAW $"
keymsg: .byte   "KEY?$"
raw2msg: .byte  "RAW?$"
waitmsg: .byte  "WAIT $"
gotmsg: .byte   " GOT $"
linemsg: .byte  "LINE?$"
countmsg: .byte " COUNT $"

        .include "print.inc"

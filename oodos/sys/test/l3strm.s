; L3STRM: sound revision 2's acceptance (hw/sound-rev2.md, item 4), for
; test_oodos_sound.cpp: a file streamed through function 82, its two
; buffers refilled with ordinary BDOS reads (function 20, 96 records a
; call) as OODOS frees them. The test puts the stream's codec, rate (2
; bytes) and format at PARMS ($2100) and the file's FCB at FCB ($2120)
; before GO. The program opens a 256-byte event area at AREA ($2000) and
; watches its sound part (EV_SBUFS) for a free buffer, then:
;
;   OPEN aa xx      FT_OPEN's
;   CLAIM aa xx     SN_CLAIM's (the stream)
;   FILE aa         function 15's
;   STREAM aa xx    SN_STREAM's
;   a line SLOT kk when SN_BUFFER takes another buffer than the one
;   refilled (it never should), or BUSY when it is refused
;   SENT nnnn       the buffers given, when the file has all gone
;   END aa xx and SN_STATUS's 12 bytes, once the stream has ended
;
; then releases the stream and returns to the CCP.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

AREA    = $2000
PARMS   = $2100
FCB     = $2120
BUF0    = $4000                         ; the buffers: in W1's block 1 and W1-W2, so their CPU
BUF1    = $7000                         ;   addresses are their physical ones (blocks 0-2)
RECS    = 96                            ; 12 KB a buffer
BUFLEN  = RECS * 128

        .zeropage
t0:     .res    2
sayx:   .res    1
eof:    .res    1
sent:   .res    2
k:      .res    1                       ; the buffer being refilled
n:      .res    1                       ; its records

        .code
.macro SAY label
        .local  s
        pha
        lda     #<s
        sta     t0
        lda     #>s
        sta     t0 + 1
        pla
        jsr     say
        .pushseg
        .rodata
s:      .byte   label, " $"
        .popseg
.endmacro

start:  stz     eof
        stz     sent
        stz     sent + 1
        lda     #<oblk
        ldx     #>oblk
        ldy     #O_FRAME
        jsr     BDOS
        SAY     "OPEN"
        lda     #<cblk
        ldx     #>cblk
        jsr     f82
        SAY     "CLAIM"
        lda     #RECS
        ldy     #F_MULTISEC
        jsr     BDOS
        lda     #<FCB
        ldx     #>FCB
        ldy     #F_OPEN
        jsr     BDOS
        SAY     "FILE"
        cmp     #4
        bcc     @open
        jmp     WBOOT
@open:  ldx     #3
@parms: lda     PARMS,x
        sta     tblk + 1,x
        dex
        bpl     @parms
        lda     #<tblk
        ldx     #>tblk
        jsr     f82
        SAY     "STREAM"
        lda     #0                      ; both buffers filled and given; k then 1
        jsr     refill
        lda     #1
        jsr     refill

; Each buffer refilled and given again as OODOS frees it (the area's
; EV_SBUFS), in turn, as SN_BUFFER takes them, till the file has all gone.
loop:   lda     eof
        bne     drain
        ldx     k
        txa
        eor     #1                      ; the other one now
        tax
        lda     bufbit,x
@wait:  bit     AREA + EV_SBUFS
        beq     @wait
        txa
        jsr     refill
        bra     loop

; All of it given: wait for both buffers to go, then EOF, then the end.
drain:  lda     AREA + EV_SBUFS
        cmp     #3
        bne     drain
        lda     #SN_END
        ldx     #0
        jsr     f82
        lda     #<sline
        ldx     #>sline
        jsr     PUTS
        lda     sent
        ldx     sent + 1
        jsr     HEX4
        jsr     CRLF
@end:   lda     AREA + EV_SSTATE        ; ended (5), or an error ($80 up)
        cmp     #5
        bcc     @end
        lda     #<sblk
        ldx     #>sblk
        jsr     f82
        pha
        lda     #<eline
        ldx     #>eline
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     sblk + 2
        jsr     HEX2
        jsr     SPACE
        lda     #<(sblk + 1)
        sta     PTR
        lda     #>(sblk + 1)
        sta     PTR + 1
        ldx     #12
        jsr     HEXN
        jsr     CRLF
        lda     #<rblk
        ldx     #>rblk
        jsr     f82
        jmp     WBOOT

; Buffer A filled from the file (RECS records, fewer at its end) and given;
; nothing given if the file had none left.
refill: sta     k
        tax
        lda     blo,x
        pha
        lda     bhi,x
        tax
        pla
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<FCB
        ldx     #>FCB
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        beq     rf_all
        stx     n                       ; 1: the file's end, X records read before it
        lda     #1
        sta     eof
        lda     n
        bne     rf_give
        rts
rf_all:   lda     #RECS
        sta     n
rf_give:  ldx     k
        lda     blo,x
        sta     bblk + 1
        lda     bhi,x
        sta     bblk + 2
        stz     bblk + 3
        lda     n                       ; n x 128
        lsr     a
        sta     bblk + 5
        lda     #0
        ror     a
        sta     bblk + 4
        lda     #<bblk
        ldx     #>bblk
        jsr     f82
        cmp     #SN_BUSY
        beq     rf_busy
        cmp     k
        beq     rf_ok
        SAY     "SLOT"
rf_ok:    inc     sent
        bne     rf_done
        inc     sent + 1
rf_done:  rts
rf_busy:  SAY     "BUSY"
        rts

f82:    ldy     #O_SOUND
        jmp     BDOS

; Print the label at t0, then A and X.
say:    pha
        stx     sayx
        lda     t0
        ldx     t0 + 1
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     sayx
        jsr     HEX2
        jmp     CRLF

        .rodata
bufbit: .byte   $01, $02
blo:    .byte   <BUF0, <BUF1
bhi:    .byte   >BUF0, >BUF1
sline:  .byte   "SENT $"
eline:  .byte   "END $"
cblk:   .byte   SN_CLAIM, SC_STREAM
rblk:   .byte   SN_RELEASE, SC_STREAM
.assert BUF1 + BUFLEN <= $BC00, error, "the second buffer stays under the BDOS's page"

        .data
oblk:   .byte   FT_OPEN
        .word   AREA
        .byte   0
tblk:   .byte   SN_STREAM, 0, 0, 0, 0
bblk:   .byte   SN_BUFFER, 0, 0, 0, 0, 0
sblk:   .byte   SN_STATUS
        .res    12

        .include "print.inc"

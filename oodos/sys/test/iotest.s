; IOTEST: test_oodos_utils_rsx's program for GET and PUT (hw/oodos.md, "The
; RSXs, phase 5", 5b). It does what its tail says, a character at a time:
;
;   L       a line by 10 (a buffer of 20): "L[...]"
;   C       a byte by 1: "Cxx"
;   R, W    6 with $FF, $FD: "Rxx", "Wxx"
;   S, T    11, 6 with $FE: "Sxx", "Txx"
;   Q, U    11, 6 with $FE, twice with no call between: "Qxxyy", "Uxxyy"
;   Mh      the console mode's high byte (bits 8 and 9) made h (a digit), by 109
;   2c      c by 2; 6c by 6; 5c by 5 (LST:)
;   9       "nine" by 9; B "block" by 111; X "list" by 112
;   E       ESC, a tab and ^A by 2; Y DEL by 2
;   D       a line by 10 with A/X 0: the DMA address (a buffer of 20) holding
;           "AB" and a 0 first: "D[...]"
;   N       CR LF by 2
;   V       the DMA address $9000 and segment $0123, user 5, error mode $FE and
;           multi-sector count 3 set; a line read by 10 (not shown) and 130
;           dots written by 2; then "Vdddd ssss uu ee mm": the five read back
;           (52, 32, 49), the segment none again
;   blanks  nothing
;
; then the end by RTS. Answers are followed by a blank.

        .setcpu "65C02"
        .include "oodos.inc"
        .macpack longbranch

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  stz     at
next:   jsr     nextc
        bne     :+
        rts
:       cmp     #'L'
        jeq     rline
        cmp     #'C'
        jeq     rchar
        cmp     #'R'
        jeq     rawff
        cmp     #'W'
        jeq     rawfd
        cmp     #'S'
        jeq     stat
        cmp     #'T'
        jeq     rawfe
        cmp     #'Q'
        jeq     stat2
        cmp     #'U'
        jeq     rawfe2
        cmp     #'M'
        jeq     cmode
        cmp     #'2'
        jeq     out2
        cmp     #'6'
        jeq     out6
        cmp     #'5'
        jeq     out5
        cmp     #'9'
        jeq     out9
        cmp     #'B'
        jeq     outb
        cmp     #'X'
        jeq     outx
        cmp     #'E'
        jeq     outesc
        cmp     #'Y'
        jeq     outdel
        cmp     #'D'
        jeq     dline
        cmp     #'N'
        jeq     outnl
        cmp     #'V'
        jeq     state
        jmp     next

rline:  lda     #20
        sta     buf
        lda     #<buf
        ldx     #>buf
        ldy     #C_READSTR
        jsr     BDOS
        lda     #'L'
        jsr     PUTC
        lda     #'['
        jsr     PUTC
        ldx     #0
@c:     cpx     buf + 1
        beq     @e
        lda     buf + 2,x
        phx
        jsr     PUTC
        plx
        inx
        bra     @c
@e:     lda     #']'
        jsr     PUTC
        jmp     blank

rchar:  ldy     #C_READ
        jsr     BDOS
        pha
        lda     #'C'
        bra     said
rawff:  lda     #$FF
        ldy     #C_RAWIO
        jsr     BDOS
        pha
        lda     #'R'
        bra     said
rawfd:  lda     #$FD
        ldy     #C_RAWIO
        jsr     BDOS
        pha
        lda     #'W'
        bra     said
stat:   ldy     #C_STAT
        jsr     BDOS
        pha
        lda     #'S'
        bra     said
rawfe:  lda     #$FE
        ldy     #C_RAWIO
        jsr     BDOS
        pha
        lda     #'T'
said:   jsr     PUTC
        pla
        jsr     HEX2
        jmp     blank

stat2:  ldy     #C_STAT
        jsr     BDOS
        sta     two
        ldy     #C_STAT
        jsr     BDOS
        pha
        lda     #'Q'
        bra     said2
rawfe2: lda     #$FE
        ldy     #C_RAWIO
        jsr     BDOS
        sta     two
        lda     #$FE
        ldy     #C_RAWIO
        jsr     BDOS
        pha
        lda     #'U'
said2:  jsr     PUTC
        lda     two
        jsr     HEX2
        pla
        jsr     HEX2
        jmp     blank

cmode:  jsr     nextc
        sec
        sbc     #'0'
        tax
        lda     #0
        ldy     #C_MODE
        jsr     BDOS
        jmp     next

out2:   jsr     nextc
        jsr     PUTC
        jmp     next
out6:   jsr     nextc
        ldy     #C_RAWIO
        jsr     BDOS
        jmp     next
out5:   jsr     nextc
        ldy     #L_WRITE
        jsr     BDOS
        jmp     next
out9:   lda     #<snine
        ldx     #>snine
        jsr     PUTS
        jmp     next
outb:   lda     #<ccb
        ldx     #>ccb
        ldy     #C_WRITEBLK
        jsr     BDOS
        jmp     next
outx:   lda     #<lcb
        ldx     #>lcb
        ldy     #L_WRITEBLK
        jsr     BDOS
        jmp     next
outesc: lda     #$1B
        jsr     PUTC
        lda     #$09
        jsr     PUTC
        lda     #$01
        jsr     PUTC
        jmp     next
outdel: lda     #$7F
        jsr     PUTC
        jmp     next
dline:  lda     #20                     ; "AB", then a 0, in the DMA buffer
        sta     buf
        stz     buf + 1
        lda     #'A'
        sta     buf + 2
        lda     #'B'
        sta     buf + 3
        stz     buf + 4
        lda     #<buf
        ldx     #>buf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #0
        tax
        ldy     #C_READSTR
        jsr     BDOS
        lda     #<BP_DMA
        ldx     #>BP_DMA
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #'D'
        jsr     PUTC
        lda     #'['
        jsr     PUTC
        ldx     #0
@c:     cpx     buf + 1
        beq     @e
        lda     buf + 2,x
        phx
        jsr     PUTC
        plx
        inx
        bra     @c
@e:     lda     #']'
        jsr     PUTC
        jmp     blank
outnl:  jsr     CRLF
        jmp     next

state:  lda     #<$9000
        ldx     #>$9000
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<$0123
        ldx     #>$0123
        ldy     #F_DMASEG
        jsr     BDOS
        lda     #5
        ldy     #F_USERNUM
        jsr     BDOS
        lda     #$FE
        ldy     #F_ERRMODE
        jsr     BDOS
        lda     #3
        ldy     #F_MULTISEC
        jsr     BDOS
        lda     #20
        sta     buf
        lda     #<buf
        ldx     #>buf
        ldy     #C_READSTR
        jsr     BDOS
        lda     #130
        sta     two
@dot:   lda     #'.'
        jsr     PUTC
        dec     two
        bne     @dot
        lda     #<dget
        ldx     #>dget
        ldy     #F_DMAGET
        jsr     BDOS
        lda     #$FF
        ldy     #F_USERNUM
        jsr     BDOS
        sta     su
        lda     #<pberr
        ldx     #>pberr
        ldy     #S_SCB
        jsr     BDOS
        sta     se
        lda     #<pbmul
        ldx     #>pbmul
        ldy     #S_SCB
        jsr     BDOS
        sta     sm
        lda     #$FF
        tax
        ldy     #F_DMASEG
        jsr     BDOS
        lda     #'V'
        jsr     PUTC
        lda     dget
        ldx     dget + 1
        jsr     HEX4
        jsr     SPACE
        lda     dget + 2
        ldx     dget + 3
        jsr     HEX4
        jsr     SPACE
        lda     su
        jsr     HEX2
        jsr     SPACE
        lda     se
        jsr     HEX2
        jsr     SPACE
        lda     sm
        jsr     HEX2
        jmp     blank

blank:  lda     #' '
        jsr     PUTC
        jmp     next

; The tail's next character, Z set at its end.
nextc:  ldx     at
        cpx     BP_DMA
        beq     @none
        lda     BP_DMA + 1,x
        inc     at
        cmp     #0
        rts
@none:  lda     #0
        rts

        .include "print.inc"

        .rodata
snine:  .byte   "nine$"
sblock: .byte   "block"
slist:  .byte   "list"
ccb:    .word   sblock, 5
pberr:  .byte   $4B, 0, 0, 0
pbmul:  .byte   $4A, 0, 0, 0
lcb:    .word   slist, 4

        .bss
at:     .res    1
two:    .res    1
su:     .res    1
se:     .res    1
sm:     .res    1
dget:   .res    4
buf:    .res    24

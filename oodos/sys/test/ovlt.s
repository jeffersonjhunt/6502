; OVLT: test_oodos_rsx's function 59 by a program (hw/oodos.md, "The RSXs,
; phase 5"). OVLT d:FILE.TYP aaaa: the file opened (15), then the program's
; state made unusual (the DMA address $9000 and segment $0123, error mode
; $FE, multi-sector count 3), then 59 at address aaaa (hex, the second
; specification's name). It prints
;
;   L=aaxx D=dddd G=ssss E=ee M=mm R=rrrrrr
;
; 59's A and X; then, read back after it, the DMA address and segment (52),
; the error mode and multi-sector count (49), and the FCB's R0-R2.

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  ldx     #0                      ; the address: four hex digits at $026D,
        jsr     hex2                    ; read before the open writes over them
        sta     addr + 1
        ldx     #2
        jsr     hex2
        sta     addr
        lda     #<BP_FCB
        ldx     #>BP_FCB
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     @open
        lda     #<snof
        ldx     #>snof
        jsr     PUTS
        jmp     CRLF
@open:  lda     addr
        sta     BP_FCB + 33
        lda     addr + 1
        sta     BP_FCB + 34
        stz     BP_FCB + 35
        lda     #<$9000
        ldx     #>$9000
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<$0123
        ldx     #>$0123
        ldy     #F_DMASEG
        jsr     BDOS
        lda     #$FE
        ldy     #F_ERRMODE
        jsr     BDOS
        lda     #3
        ldy     #F_MULTISEC
        jsr     BDOS
        lda     #<BP_FCB
        ldx     #>BP_FCB
        ldy     #P_LOAD
        jsr     BDOS
        sta     ra
        stx     rx
        lda     #<dget
        ldx     #>dget
        ldy     #F_DMAGET
        jsr     BDOS
        lda     #<pberr
        ldx     #>pberr
        ldy     #S_SCB
        jsr     BDOS
        sta     re
        lda     #<pbmul
        ldx     #>pbmul
        ldy     #S_SCB
        jsr     BDOS
        sta     rm
        lda     #$FF                    ; the segment back to none, for the prints
        tax
        ldy     #F_DMASEG
        jsr     BDOS
        lda     #<sl
        ldx     #>sl
        jsr     PUTS
        lda     ra
        jsr     HEX2
        lda     rx
        jsr     HEX2
        lda     #<sd
        ldx     #>sd
        jsr     PUTS
        lda     dget
        ldx     dget + 1
        jsr     HEX4
        lda     #<sg
        ldx     #>sg
        jsr     PUTS
        lda     dget + 2
        ldx     dget + 3
        jsr     HEX4
        lda     #<se
        ldx     #>se
        jsr     PUTS
        lda     re
        jsr     HEX2
        lda     #<sm
        ldx     #>sm
        jsr     PUTS
        lda     rm
        jsr     HEX2
        lda     #<sr
        ldx     #>sr
        jsr     PUTS
        lda     BP_FCB + 33
        jsr     HEX2
        lda     BP_FCB + 34
        jsr     HEX2
        lda     BP_FCB + 35
        jsr     HEX2
        jmp     CRLF

; A = the byte of two hex digits at $026D + X.
hex2:   lda     BP_FCB2 + 1,x
        jsr     digit
        asl     a
        asl     a
        asl     a
        asl     a
        sta     hb
        lda     BP_FCB2 + 2,x
        jsr     digit
        ora     hb
        rts
digit:  cmp     #'A'
        bcc     @num
        sbc     #'A' - 10
        rts
@num:   sec
        sbc     #'0'
        rts

        .include "print.inc"

        .rodata
snof:   .byte   "NO FILE$"
sl:     .byte   "L=$"
sd:     .byte   " D=$"
sg:     .byte   " G=$"
se:     .byte   " E=$"
sm:     .byte   " M=$"
sr:     .byte   " R=$"
pberr:  .byte   $4B, 0, 0, 0
pbmul:  .byte   $4A, 0, 0, 0

        .bss
ra:     .res    1
rx:     .res    1
re:     .res    1
rm:     .res    1
hb:     .res    1
dget:   .res    4
addr:   .res    2

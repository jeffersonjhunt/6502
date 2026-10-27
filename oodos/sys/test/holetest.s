; HOLETEST: program memory the BDOS and the BIOS mustn't take for RAM (hw/oodos.md,
; "The system, phase 2"), for test_oodos_sys.cpp. A: is selected, its last
; track's sector 13, the program's bank. Each line a result:
;
;   STRING hhhh   function 9 of a string at $C000, the I/O hole: FFFF
;   HOLE rr       READ to $C100, in the hole: 01
;   ROM rr        READ to $4000 with W1 on flash block 0: 01
;   RO rr         READ to $4000 with W1 on block 4 read-only: 01
;   ROW rr        WRITE from there: 00 (a read-only window is read)
;   MOVE hhhh     MOVE of 16 bytes from $0300 to $C400: FFFF
;   MOVER hhhh    MOVE of 16 bytes from W1 on flash to $2000: FFFF
;   OK rr         READ to $2000: 00
;
; then W1 back on block 1, and the warm boot.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"

NB_ROM  = $80

        .code
start:  lda     #<$C000
        ldx     #>$C000
        ldy     #C_WRITESTR
        jsr     BDOS
        pha
        phx
        lda     #<strmsg
        ldx     #>strmsg
        jsr     PUTS
        plx
        pla
        jsr     HEX4
        jsr     CRLF

        lda     #BIOS::SELDSK
        ldx     #0                      ; A:, selected before
        ldy     #1
        jsr     bios
        lda     #BIOS::SETTRK
        ldx     #<1023
        ldy     #>1023
        jsr     bios
        lda     #BIOS::SETSEC
        ldx     #13
        ldy     #0
        jsr     bios
        lda     #BIOS::SETBNK
        ldx     #1
        jsr     bios

        lda     #<$C100                 ; the hole
        ldx     #>$C100
        jsr     readat
        lda     #<holemsg
        ldx     #>holemsg
        jsr     result
        lda     #NB_ROM                 ; flash
        sta     NB_WIN1
        lda     #<$4000
        ldx     #>$4000
        jsr     readat
        lda     #<rommsg
        ldx     #>rommsg
        jsr     result
        lda     #NB_RO | 4              ; read-only
        sta     NB_WIN1
        lda     #<$4000
        ldx     #>$4000
        jsr     readat
        lda     #<romsg
        ldx     #>romsg
        jsr     result
        lda     #BIOS::WRITE            ; and written from: the sector as it was read
        jsr     bios
        sta     rr
        lda     #<rowmsg
        ldx     #>rowmsg
        jsr     result

        lda     #BIOS::SELMEM           ; MOVEs in the program's bank
        ldx     #1
        jsr     bios
        lda     #<$0300
        ldx     #>$0300
        ldy     #>$C400
        jsr     move
        lda     #<movemsg
        ldx     #>movemsg
        jsr     result4
        lda     #NB_ROM
        sta     NB_WIN1
        lda     #<$4000
        ldx     #>$4000
        ldy     #>$2000
        jsr     move
        lda     #<movermsg
        ldx     #>movermsg
        jsr     result4
        lda     #1
        sta     NB_WIN1

        lda     #<$2000                 ; and memory still is
        ldx     #>$2000
        jsr     readat
        lda     #<okmsg
        ldx     #>okmsg
        jsr     result
        jmp     WBOOT

; READ to A/X; rr its result.
readat: sta     pb+1
        stx     pb+2
        lda     #BIOS::SETDMA
        sta     pb
        jsr     biospb
        lda     #BIOS::READ
        jsr     bios
        sta     rr
        rts

; MOVE of 16 bytes from A/X to page Y; rr4 A/X after.
move:   sta     pb+4
        stx     pb+5
        stz     pb+6
        sty     pb+7
        lda     #BIOS::MOVE
        ldx     #16
        ldy     #0
        jsr     bios
        sta     rr4
        stx     rr4+1
        rts

; The name at A/X, then rr; or rr4.
result: jsr     PUTS
        lda     rr
        jsr     HEX2
        jmp     CRLF
result4:
        jsr     PUTS
        lda     rr4
        ldx     rr4+1
        jsr     HEX4
        jmp     CRLF

; BIOS function A, with X and Y as its A and X, through function 50.
bios:   sta     pb
        stx     pb+1
        sty     pb+2
biospb: lda     #<pb
        ldx     #>pb
        ldy     #S_BIOS
        jmp     BDOS

        .rodata
strmsg: .byte   "STRING $"
holemsg: .byte  "HOLE $"
rommsg: .byte   "ROM $"
romsg:  .byte   "RO $"
rowmsg: .byte   "ROW $"
movemsg: .byte  "MOVE $"
movermsg: .byte "MOVER $"
okmsg:  .byte   "OK $"

        .bss
pb:     .res    8
rr:     .res    1
rr4:    .res    2

        .include "print.inc"

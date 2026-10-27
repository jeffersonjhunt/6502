; BANKTEST: what a program leaves in the BIOS doesn't reach the next, and
; function 50 won't run BOOT (hw/oodos.md, "BIOS"), for test_oodos_sys.cpp.
; The tail's first word says what it does:
;
;   SET    SETBNK 1, SELMEM 1 and an XMOVE from bank 1 to bank 1 left
;          pending, then the warm boot
;   USE    with the banks as they are: a MOVE of 16 bytes from $4000 to
;          $1800, then a READ of A:'s first sector to $1000, both in bank 0
;          (the system's: the BDOS's first bytes, $F0000, to $7D800, and the
;          sector to $7D000) unless something was left; prints
;            READ rr
;   BOOT   function 50 of BOOT, which is refused; prints
;            BOOT hhhh

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  lda     BP_DMA + 2              ; the word's first letter, after its space
        cmp     #'S'
        beq     set
        cmp     #'U'
        beq     use
        cmp     #'B'
        beq     boot
        jmp     WBOOT

set:    lda     #BIOS::SETBNK
        ldx     #1
        jsr     bios
        lda     #BIOS::SELMEM
        ldx     #1
        jsr     bios
        lda     #BIOS::XMOVE
        ldx     #1
        ldy     #1
        jsr     bios
        jmp     WBOOT

use:    lda     #<$4000
        sta     pb+4
        lda     #>$4000
        sta     pb+5
        lda     #<$1800
        sta     pb+6
        lda     #>$1800
        sta     pb+7
        lda     #BIOS::MOVE
        ldx     #16
        ldy     #0
        jsr     bios
        lda     #BIOS::SELDSK
        ldx     #0                      ; A:, selected before
        ldy     #1
        jsr     bios
        lda     #BIOS::SETTRK
        ldx     #0
        ldy     #0
        jsr     bios
        lda     #BIOS::SETSEC
        ldx     #0
        ldy     #0
        jsr     bios
        lda     #BIOS::SETDMA
        ldx     #<$1000
        ldy     #>$1000
        jsr     bios
        lda     #BIOS::READ
        jsr     bios
        pha
        lda     #<readmsg
        ldx     #>readmsg
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     CRLF
        jmp     WBOOT

boot:   lda     #BIOS::BOOT
        jsr     bios
        pha
        phx
        lda     #<bootmsg
        ldx     #>bootmsg
        jsr     PUTS
        plx
        pla
        jsr     HEX4
        jsr     CRLF
        jmp     WBOOT

; BIOS function A, with X and Y as its A and X, through function 50.
bios:   sta     pb
        stx     pb+1
        sty     pb+2
        lda     #<pb
        ldx     #>pb
        ldy     #S_BIOS
        jmp     BDOS

        .rodata
readmsg: .byte  "READ $"
bootmsg: .byte  "BOOT $"

        .bss
pb:     .res    8

        .include "print.inc"

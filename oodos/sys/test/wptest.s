; WPTEST: the BIOS's disk errors from the card itself, for test_oodos_sys.cpp.
; B: was writable at boot; the test has since mounted it again read-only.
;
;   WP rr        WRITE B without a first select: the card refuses it (2)
;   EJECT?       a key once the test has taken the card out
;   NOCARD rr    READ B with no card (1)
;   GONE hhhh    SELDSK B, a first select: the units table is gone (0000)

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"

BUF     = $2000

        .code
start:  lda     #BIOS::SELDSK
        ldx     #1                      ; B:
        ldy     #1                      ; selected before: the table as it was
        jsr     bios
        lda     #BIOS::SETTRK
        ldx     #<1023
        ldy     #>1023
        jsr     bios
        lda     #BIOS::SETSEC
        ldx     #14
        ldy     #0
        jsr     bios
        lda     #BIOS::SETDMA
        ldx     #<BUF
        ldy     #>BUF
        jsr     bios
        lda     #BIOS::SETBNK
        ldx     #1
        jsr     bios
        lda     #BIOS::WRITE
        jsr     bios
        pha
        lda     #<wpmsg
        ldx     #>wpmsg
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     CRLF
        lda     #<ejectmsg
        ldx     #>ejectmsg
        jsr     PUTS
        ldy     #C_READ
        jsr     BDOS
        jsr     CRLF
        lda     #BIOS::READ
        jsr     bios
        pha
        lda     #<nocardmsg
        ldx     #>nocardmsg
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     CRLF
        lda     #BIOS::SELDSK
        ldx     #1
        ldy     #0                      ; a first select
        jsr     bios
        pha
        phx
        lda     #<gonemsg
        ldx     #>gonemsg
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
wpmsg:  .byte   "WP $"
ejectmsg: .byte "EJECT?$"
nocardmsg: .byte "NOCARD $"
gonemsg: .byte  "GONE $"

        .bss
pb:     .res    8

        .include "print.inc"

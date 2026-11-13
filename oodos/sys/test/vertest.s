; VERTEST: function 77 (O_ROMVER, interface 9; hw/diag.md, D1-1), the ROM
; BIOS's version, and function 72 as it always was, for test_oodos_sysinfo.
;
;   FINFO aa xx    function 80's FT_INFO: the interface version, the areas
;   ROMV aa xx     function 77: the major and the minor, $FF $FF unknown
;   V72 <12 bytes> function 72's 10 bytes into a block with $A5 $5A after
;                  it: they are left alone

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  lda     #<sfinfo
        ldx     #>sfinfo
        jsr     PUTS
        lda     #FT_INFO
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        jsr     pair
        lda     #<sromv
        ldx     #>sromv
        jsr     PUTS
        ldy     #O_ROMVER
        jsr     BDOS
        jsr     pair
        lda     #$A5
        sta     blk + 10
        lda     #$5A
        sta     blk + 11
        lda     #<blk
        ldx     #>blk
        ldy     #O_VERSION
        jsr     BDOS
        lda     #<sv72
        ldx     #>sv72
        jsr     PUTS
        lda     #<blk
        sta     PTR
        lda     #>blk
        sta     PTR + 1
        ldx     #12
        jsr     HEXN
        jsr     CRLF
        jmp     WBOOT

; A and X as "aa xx", CR LF.
pair:   phx
        jsr     HEX2
        jsr     SPACE
        pla
        jsr     HEX2
        jmp     CRLF

        .rodata
sfinfo: .byte   "FINFO $"
sromv:  .byte   "ROMV $"
sv72:   .byte   "V72 $"

        .bss
blk:    .res    12

        .include "print.inc"

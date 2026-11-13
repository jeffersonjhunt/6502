; VERTEST: function 72's block before interface 9 and after (hw/diag.md, D1-1):
; a caller's block of 10 is never written past, and one that puts 'R' 'V'
; (OV_ROMKEY, OV_ROMKEY2) at +10-11 gets the ROM BIOS's version there.
;
;   OLD <12 bytes>   a block with $A5 $5A at +10-11: the 10 bytes, then
;                    +10-11 as they were
;   NEW <12 bytes>   the same with 'R' 'V' there: the 10 bytes, then the
;                    ROM BIOS's version, major and minor

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  lda     #$A5
        ldx     #$5A
        jsr     call
        lda     #<sold
        ldx     #>sold
        jsr     show
        lda     #OV_ROMKEY
        ldx     #OV_ROMKEY2
        jsr     call
        lda     #<snew
        ldx     #>snew
        jsr     show
        jmp     WBOOT

; Function 72 into blk, A and X at its +10-11 first.
call:   sta     blk + OV_ROM
        stx     blk + OV_ROM + 1
        lda     #<blk
        ldx     #>blk
        ldy     #O_VERSION
        jmp     BDOS

; The '$' string at A/X, the block's 12 bytes, CR LF.
show:   jsr     PUTS
        lda     #<blk
        sta     PTR
        lda     #>blk
        sta     PTR + 1
        ldx     #OV_LEN9
        jsr     HEXN
        jmp     CRLF

        .rodata
sold:   .byte   "OLD $"
snew:   .byte   "NEW $"

        .bss
blk:    .res    OV_LEN9

        .include "print.inc"

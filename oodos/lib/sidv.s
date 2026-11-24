; OOLIB's SID reads (include/oolib.inc, "Sound: the SIDs' voices"; hw/oolib.md,
; L5c): both SIDs' registers into the program's memory, as the card's shadows
; have them (function 82's SN_SIDREGS: the last value written to each, by a
; tune's 6510 or by the card's own music and effects), so a program can show
; what a tune is doing while it plays.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .importzp SIDV_ZP               ; zpsidv.s's

svp             = SIDV_ZP

        .rodata
SID_VOFF:       .byte   0, 7, 14, SID_REGS, SID_REGS + 7, SID_REGS + 14

        .bss
rblk:   .res    SID_BLOCK + 1           ; SN_SIDREGS's block

        .code

; A/X a block of SID_BLOCK (50) bytes: SID A's registers $00-$18, then SID
; B's. Voice v's 7 start at SID_VOFF's byte v. No card: all 0.
SID_VOICES:
        sta     svp
        stx     svp + 1
        lda     #SN_SIDREGS
        sta     rblk
        lda     #<rblk
        ldx     #>rblk
        ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        beq     @copy
        ldy     #SID_BLOCK - 1
@z:     lda     #0
        sta     rblk + 1,y
        dey
        bpl     @z
@copy:  ldy     #SID_BLOCK - 1
@c:     lda     rblk + 1,y
        sta     (svp),y
        dey
        bpl     @c
        clc
        rts

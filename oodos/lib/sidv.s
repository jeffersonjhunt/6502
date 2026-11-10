; OOLIB's SID reads (include/oolib.inc, "Sound: the SIDs' voices"; hw/oolib.md,
; L5c): both SIDs' registers into the program's memory. The card answers a
; read of a SID register with the last value written to it, by the 6510
; running a tune or by the 65C02, so a program can show what a tune is
; doing while it plays. The library only reads: it never writes a SID.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .importzp SIDV_ZP               ; zpsidv.s's
svp             = SIDV_ZP

        .rodata
SID_VOFF:       .byte   0, 7, 14, SID_REGS, SID_REGS + 7, SID_REGS + 14

        .code

; A/X a block of SID_BLOCK (50) bytes: SID A's registers $00-$18, then SID
; B's. Voice v's 7 start at SID_VOFF's byte v.
SID_VOICES:
        sta     svp
        stx     svp + 1
        ldy     #SID_REGS - 1
@copy:  lda     SND_SIDA,y
        sta     (svp),y
        dey
        bpl     @copy
        lda     svp                     ; SID B's after
        clc
        adc     #SID_REGS
        sta     svp
        bcc     @b
        inc     svp + 1
@b:     ldy     #SID_REGS - 1
@copyb: lda     SND_SIDB,y
        sta     (svp),y
        dey
        bpl     @copyb
        clc
        rts

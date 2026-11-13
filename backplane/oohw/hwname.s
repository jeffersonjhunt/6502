; HW_NAME and HW_KNOWN (oohw.inc): a module's name for a report, and the
; cards at the revisions this library knows (hw/diag.md, "What the hardware
; gives"; backplane.inc's IDs).

        .setcpu "65C02"
        .include "backplane.inc"
        .include "oohw.inc"

        .code
HW_NAME:
        and     #$0F
        asl     a
        tax
        lda     names + 1,x
        pha
        lda     names,x
        plx
        rts

; Carry clear if module A at revision X is in the table.
HW_KNOWN:
        sta     want
        txa
        and     #$0F
        sta     want + 1
        lda     want
        asl     a
        asl     a
        asl     a
        asl     a
        ora     want + 1                ; the ID byte it would have
        ldx     #KNOWN_LEN - 1
@find:  cmp     known,x
        beq     @yes
        dex
        bpl     @find
        sec
        rts
@yes:   clc
        rts

        .rodata
names:  .word   n0, n1, n2, n3, n4, n5, n6, n7, n8, n9, nA, nB, nC, nD, nE, nF
n0:     .byte   "RAM A", 0
n1:     .byte   "RAM B", 0
n2:     .byte   "RAM C", 0
n3:     .byte   "RAM D", 0
n4:     .byte   "ROM A (the flash)", 0
n5:     .byte   "ROM B (the cartridge)", 0
n6:     .byte   "serial, parallel and VIA", 0
n7:     .byte   "mass storage (SD)", 0
n8:     .byte   "DMA controller", 0
n9:     .byte   "video", 0
nA:     .byte   "sound", 0
nB:     .byte   "USB, game port and speaker", 0
nC:     .byte   "Disk II controller", 0
nD:     .byte   "north bridge", 0
nE:     .byte   "console (front panel)", 0
nF:     .byte   "(free)", 0
; The IDs with tests here: the serial card's $61 (hw/design.md, from D1),
; and backplane.inc's.
known:  .byte   HW_SERIAL_ID, SD_ID_V1, DMA_ID_V1, VIDEO_ID_V2, SOUND_ID_V1, SOUND_ID_V2, USB_ID_V1
        .byte   DISK2_ID_V1, HW_NB_ID, CONSOLE_ID_V1
KNOWN_LEN = * - known

        .bss
want:   .res    2

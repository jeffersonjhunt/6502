; HW_NAME and HW_KNOWN (oohw.inc): a module's name for a report, and the
; cards at the revisions this library knows (hw/diag.md, "What the hardware
; gives"; backplane.inc's IDs).

        .setcpu "65C02"
        .include "backplane.inc"
        .include "oohw.inc"

        OOHW_CODE
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

        OOHW_RODATA
; IDs 0-5 are free slots since B6 (memory is chosen by address, not by ID),
; and F is reserved (hw/design.md, the ID table).
names:  .word   nfree, nfree, nfree, nfree, nfree, nfree, n6, n7, n8, n9, nA, nB, nC, nD, nE, nF
nfree:  .byte   "(free)", 0
n6:     .byte   "serial, parallel and VIA", 0
n7:     .byte   "mass storage (SD)", 0
n8:     .byte   "DMA controller", 0
n9:     .byte   "video", 0
nA:     .byte   "sound", 0
nB:     .byte   "USB, game port and speaker", 0
nC:     .byte   "Disk II controller", 0
nD:     .byte   "north bridge", 0
nE:     .byte   "console (front panel)", 0
nF:     .byte   "(reserved)", 0
; The IDs with tests here: the serial card's $61 (hw/design.md, from D1),
; and backplane.inc's.
known:  .byte   HW_SERIAL_ID, SD_ID_V1, DMA_ID_V2, VIDEO_ID_V2, SOUND_ID_V1, SOUND_ID_V2, USB_ID_V1
        .byte   DISK2_ID_V1, HW_NB_ID, HW_NB_ID2, CONSOLE_ID_V1
KNOWN_LEN = * - known

        OOHW_BSS
want:   .res    2

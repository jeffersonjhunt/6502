; HW_SD_ID (oohw.inc): the SD card's IDENTIFY sector, read as OODOS's own
; BIOS reads it (oodos/sys/bios.s, UNITS): the card idle first, CTRL 0 (no
; DMA), UNIT 0, the command, the sector by the CPU through DATA, the card
; idle again; then CTRL put back as it was. Under OODOS it is called between
; BDOS calls only, when no command of OODOS's is under way (hw/diag.md,
; answer 1). IDENTIFY changes nothing on the card; LBA and COUNT stay.

        .setcpu "65C02"
        .include "backplane.inc"
        .include "oohw.inc"

        .code
HW_SD_ID:
        lda     SD_ID
        cmp     #SD_ID_V1
        beq     @card
        lda     #HE_NOCARD
        sec
        rts
@card:  lda     SD_STATUS
        and     #SD_PRESENT | SD_READY
        cmp     #SD_PRESENT | SD_READY
        beq     @ready
        lda     #HE_EMPTY
        sec
        rts
@ready: jsr     idle
        bcs     @busy
        lda     SD_CTRL
        sta     ctrl
        stz     SD_CTRL
        stz     SD_UNIT
        lda     #SD_IDENTIFY
        sta     SD_CMD
        jsr     data
        bcs     @ended
        ldy     #0
@lo:    lda     SD_DATA
        sta     (HW_PTR),y
        iny
        bne     @lo
        inc     HW_PTR + 1
@hi:    lda     SD_DATA
        sta     (HW_PTR),y
        iny
        bne     @hi
        dec     HW_PTR + 1
        jsr     idle
        bcs     @stuck
        lda     ctrl
        sta     SD_CTRL
        clc
        rts
@ended: lda     SD_STATUS               ; no sector: the card's error
        and     #SD_ERR
        beq     @err
        lda     SD_ERROR
@err:   ora     #HE_ERROR
        pha
        lda     ctrl
        sta     SD_CTRL
        pla
        sec
        rts
@stuck: lda     ctrl
        sta     SD_CTRL
@busy:  lda     #HE_BUSY
        sec
        rts

; BUSY clear: carry clear; still on after about a second: set.
idle:   lda     #10
        sta     tmo
        ldx     #0
        ldy     #0
@wait:  bit     SD_STATUS               ; N: BUSY
        bpl     @ok
        dex
        bne     @wait
        dey
        bne     @wait
        dec     tmo
        bne     @wait
        sec
        rts
@ok:    clc
        rts

; A sector for DATA: carry clear; the command ended without one, or about a
; second went by: set.
data:   lda     #7
        sta     tmo
        ldx     #0
        ldy     #0
@wait:  lda     SD_STATUS
        bit     #SD_DATA_READY
        bne     @ok
        bit     #SD_BUSY
        beq     @no
        dex
        bne     @wait
        dey
        bne     @wait
        dec     tmo
        bne     @wait
@no:    sec
        rts
@ok:    clc
        rts

        .bss
ctrl:   .res    1
tmo:    .res    1

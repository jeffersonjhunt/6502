; OOLIB's vis page (include/oolib.inc, "Sound: the EQ and the vis page";
; hw/oolib.md, L5b): a snapshot of the sound card's spectrum, VU and recent
; samples, copied into the program's memory. No claim.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .importzp PBVIS_ZP              ; zppbvis.s's
visp            = PBVIS_ZP
vlen            = PBVIS_ZP + 2

        .code

; A/X the buffer, Y how many bytes (1-98; 0 is 98): a snapshot taken
; (VISIDX 0), then the page's first Y bytes copied in.
PB_VIS:
        sta     visp
        stx     visp + 1
        tya
        bne     @len
        lda     #SND_VIS_BYTES
@len:   cmp     #SND_VIS_BYTES + 1
        bcs     @range
        sta     vlen
        jsr     PBCARD
        bcs     @out
        stz     SND_VISIDX              ; the snapshot
        ldy     #0
@copy:  lda     SND_VISDATA
        sta     (visp),y
        iny
        cpy     vlen
        bne     @copy
        lda     #0
        clc
@out:   rts
@range: lda     #PE_RANGE
        sec
        rts

; OOLIB's volume and balance (include/oolib.inc, "Sound: playback";
; hw/oolib.md, L5d): a level and a balance turned into the card's master
; volumes by one law, then function 82's SN_VOLUME, so every program's
; balance moves alike. The law is OO-Amp's: at the centre both sides have
; the level; away from it, the far side falls in a straight line to 0 at
; the end, and the near side keeps the level.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .bss
vblk:   .res    3                       ; SN_VOLUME's: the operation, VOLL, VOLR
lev:    .res    1

        .code

; A the level (0-255, 255 unity), X the balance (0 left, 128 the centre,
; 255 right). Left of the centre VOLR = level x balance / 128; right of
; it VOLL = level x (255 - balance) / 127.
PB_VOLUME:
        sta     lev
        sta     vblk + 1
        sta     vblk + 2
        cpx     #128
        beq     @put
        bcc     @right
        txa                             ; right of the centre: the left side cut
        eor     #$FF                    ; 255 - balance
        ldx     lev
        jsr     PBMUL
        lda     #127
        jsr     PBDIV
        sta     vblk + 1
        bra     @put
@right: txa                             ; left of it: the right side cut
        ldx     lev
        jsr     PBMUL
        asl     PBP                     ; / 128: the high byte of x 2
        rol     PBP + 1
        lda     PBP + 1
        sta     vblk + 2
@put:   lda     #SN_VOLUME
        sta     vblk
        lda     #<vblk
        ldx     #>vblk
        ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        bne     @no                     ; refused: revision 1, no master volumes
        clc
        rts
@no:    lda     #PE_NOCARD
        sec
        rts

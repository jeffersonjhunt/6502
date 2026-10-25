; A music driver for the sound card's 6510 (hw/design.md, "Sound"), small
; enough to check by hand: test_sound_6510 loads it at $1000, points the
; card's reset vector at it and sets CTRL RUN | RESET.
;
; It does what a real driver does, with the parts a C64 player uses:
;   - the 6510's port: DDR $2F, data $37 (I/O in), as the KERNAL leaves it;
;   - CIA 1 timer A at 50 Hz on the PAL clock (985,248 / 19,705), its
;     interrupt counting frames with undocumented opcodes (ISC, LAX, SAX,
;     DCP), as tune players do;
;   - the mailbox: a byte from the host is a command (1-3: play that note
;     on voice 1; 0: release it), answered with the command | $80;
;   - I/O banked out ($01 = $34) to reach the RAM under $D400, then back.

        .setcpu "6502X"

SID      = $D400
FREQ_LO  = SID + $00
FREQ_HI  = SID + $01
CTRL1    = SID + $04
AD1      = SID + $05
SR1      = SID + $06
VOLUME   = SID + $18
TA_LO    = $DC04
TA_HI    = $DC05
ICR      = $DC0D
CRA      = $DC0E
MAILBOX  = $DE00
MSTATUS  = $DE01

FRAMES   = $F0       ; IRQs so far, low byte (ISC'd)
COPY     = $F1       ; FRAMES, stored by SAX (A & X, both FRAMES)
COUNTDN  = $F2       ; DCP'd down from $FF each frame
UNDER    = $D400     ; the RAM under SID A's first register

PERIOD   = 19705 - 1 ; the latch: an underflow every latch + 1 clocks

        .code
init:   sei
        cld
        ldx     #$FF
        txs
        lda     #$2F
        sta     $00
        lda     #$37
        sta     $01
        lda     #<irq
        sta     $FFFE
        lda     #>irq
        sta     $FFFF
        lda     #0
        sta     FRAMES
        sta     COPY
        lda     #$FF
        sta     COUNTDN

        lda     #$34            ; I/O out: $D400 is RAM
        sta     $01
        lda     #$5A
        sta     UNDER
        lda     #$37            ; and back in
        sta     $01

        lda     #$0F
        sta     VOLUME
        lda     #$09
        sta     AD1
        lda     #$A0
        sta     SR1

        lda     #<PERIOD
        sta     TA_LO
        lda     #>PERIOD
        sta     TA_HI
        lda     #$81            ; timer A's interrupt on
        sta     ICR
        lda     #$11            ; start, loading the latch
        sta     CRA
        cli

main:   lda     MSTATUS
        lsr     a               ; bit 0: a byte from the host
        bcc     main
        lda     MAILBOX
        beq     release
        cmp     #4
        bcs     reply
        tax
        lda     notes_lo-1,x
        sta     FREQ_LO
        lda     notes_hi-1,x
        sta     FREQ_HI
        lda     #$21            ; saw, gate on
        sta     CTRL1
        txa
        bne     reply
release:
        lda     #$20            ; saw, gate off
        sta     CTRL1
        lda     #0
reply:  ora     #$80
        sta     MAILBOX
        jmp     main

irq:    pha
        txa
        pha
        lda     ICR             ; acknowledge
        lda     #0
        sec
        isc     FRAMES          ; INC FRAMES, then SBC it
        lax     FRAMES          ; A = X = FRAMES
        sax     COPY            ; A & X: FRAMES again
        lda     #0
        dcp     COUNTDN         ; DEC COUNTDN, then CMP it
        pla
        tax
        pla
        rti

; A4, C5, E5 on the PAL clock: freq = Hz x 16,777,216 / 985,248
notes_lo:
        .byte   <7493, <8911, <11228
notes_hi:
        .byte   >7493, >8911, >11228

; OOLIB's SID notes (include/oolib.inc, "Sound: the SIDs' voices"; hw/oolib.md,
; L5c): a voice's frequency register to the note nearest it, C-0 to B-7, by
; the thresholds in sidnote.inc (tools/mksidtab.py: the geometric mean of
; each note's value and the one below's), for the SIDs' clock.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        .importzp SIDNOTE_ZP            ; zpsidnote.s's
ntp             = SIDNOTE_ZP

        .bss
nf:     .res    2
nk:     .res    1
nstep:  .res    1

        .include "sidnote.inc"

        .code

; A/X the frequency (low, high), Y CTRL's bits (SID_CTRL's: bit 0 NTSC):
; A = the note, 0 (C-0) to 95 (B-7), the last whose threshold the frequency
; reaches; or SID_NONE for a frequency of 0. A binary search of 7 steps.
SID_NOTE:
        sta     nf
        stx     nf + 1
        ora     nf + 1
        bne     @go
        lda     #SID_NONE
        clc
        rts
@go:    tya
        and     #1
        beq     @pal
        lda     #<sid_note_ntsc
        ldx     #>sid_note_ntsc
        bra     @tab
@pal:   lda     #<sid_note_pal
        ldx     #>sid_note_pal
@tab:   sta     ntp
        stx     ntp + 1
        stz     nk                      ; note 0's threshold is 0: always reached
        lda     #64
        sta     nstep
@step:  lda     nk                      ; table[k + step] reached: k += step
        clc
        adc     nstep
        cmp     #SID_NOTES
        bcs     @next                   ; past B-7
        tax
        asl     a
        tay
        lda     nf
        cmp     (ntp),y
        iny
        lda     nf + 1
        sbc     (ntp),y
        bcc     @next
        stx     nk
@next:  lsr     nstep
        bne     @step
        lda     nk
        clc
        rts

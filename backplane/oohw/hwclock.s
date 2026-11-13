; HW_CLOCK, HW_FRAMEHZ and HW_DIV (oohw.inc): the CPU's clock against a time
; base that isn't PHI2 (hw/diag.md, "What the hardware gives": the DS3231's
; second, the video frame).
;
; The VIA's timer 2 counts PHI2 whatever the CPU is doing, so the count is
; whole across OODOS's interrupts: each pass of the poll reads it, adds what
; it went down by since the last pass to HW_CYC, and looks at the watched
; byte. A pass is about 80 cycles, so a change is seen within one; a pass
; must come at least once each 65,536 cycles (9 ms), which an interrupt
; handler never holds off. Polling NB_SEC latches the time's other
; registers; how that meets the north bridge's own I2C reads of the DS3231
; on real hardware is unknown (the emulator latches whole).

        .setcpu "65C02"
        .include "backplane.inc"
        .include "oohw.inc"

        .code
HW_CLOCK:
        lda     HW_WATCH
        sta     HW_PTR
        lda     HW_WATCH + 1
        sta     HW_PTR + 1
        lda     #$FF                    ; timer 2 from $FFFF, counting down and on past 0
        sta     VIA_T2CL
        sta     VIA_T2CH
        jsr     t2now
        sta     prev
        stx     prev + 1
        jsr     zero
        lda     (HW_PTR)
        sta     last
@first: jsr     tick                    ; the first change starts the count
        bcs     @out
        lda     (HW_PTR)
        cmp     last
        beq     @first
        sta     last
        jsr     zero
        lda     HW_EDGES
        sta     left
@more:  jsr     tick
        bcs     @out
        lda     (HW_PTR)
        cmp     last
        beq     @more
        sta     last
        dec     left
        bne     @more
        clc
@out:   rts

; HW_CYC = 0.
zero:   stz     HW_CYC
        stz     HW_CYC + 1
        stz     HW_CYC + 2
        stz     HW_CYC + 3
        rts

; HW_CYC += what timer 2 went down by since the last; carry set once HW_CYC
; reaches 2^24.
tick:   jsr     t2now
        sta     now
        stx     now + 1
        sec
        lda     prev
        sbc     now
        tay
        lda     prev + 1
        sbc     now + 1
        tax
        lda     now
        sta     prev
        lda     now + 1
        sta     prev + 1
        clc
        tya
        adc     HW_CYC
        sta     HW_CYC
        txa
        adc     HW_CYC + 1
        sta     HW_CYC + 1
        lda     HW_CYC + 2
        adc     #0
        sta     HW_CYC + 2
        lda     HW_CYC + 3
        adc     #0
        sta     HW_CYC + 3
        cmp     #1                      ; carry set at 2^24
        rts

; Timer 2 now: A low, X high, the high byte read on both sides of the low
; so the pair is one moment's.
t2now:  ldx     VIA_T2CH
        lda     VIA_T2CL
        cpx     VIA_T2CH
        bne     t2now
        rts

; 60 frames are 60 x 800 x 525 / 25,175,000 s = 25,200,000 / 25,175,000 s,
; so the Hz are HW_CYC x 25,175 / 25,200: HW_CYC less HW_CYC / 1008.
HW_FRAMEHZ:
        ldx     #3
@cp:    lda     HW_CYC,x
        sta     HW_NUM,x
        dex
        bpl     @cp
        lda     #<1008
        ldx     #>1008
        jsr     HW_DIV
        sec
        ldx     #0
        ldy     #4
@sub:   lda     HW_CYC,x
        sbc     HW_NUM,x
        sta     HW_NUM,x
        inx
        dey
        bne     @sub
        rts

; HW_NUM / A/X: the quotient in HW_NUM, the remainder in A/X (shift and
; subtract, a bit at a time).
HW_DIV: sta     dvs
        stx     dvs + 1
        stz     rem
        stz     rem + 1
        stz     rem + 2
        ldy     #32
@bit:   asl     HW_NUM
        rol     HW_NUM + 1
        rol     HW_NUM + 2
        rol     HW_NUM + 3
        rol     rem
        rol     rem + 1
        rol     rem + 2
        sec
        lda     rem
        sbc     dvs
        tax
        lda     rem + 1
        sbc     dvs + 1
        sta     tmp
        lda     rem + 2
        sbc     #0
        bcc     @no
        sta     rem + 2
        lda     tmp
        sta     rem + 1
        stx     rem
        inc     HW_NUM
@no:    dey
        bne     @bit
        lda     rem
        ldx     rem + 1
        rts

        .bss
HW_WATCH: .res  2
HW_EDGES: .res  1
HW_CYC: .res    4
HW_NUM: .res    4
prev:   .res    2
now:    .res    2
last:   .res    1
left:   .res    1
dvs:    .res    2
rem:    .res    3
tmp:    .res    1

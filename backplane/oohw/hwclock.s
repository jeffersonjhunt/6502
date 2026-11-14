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
;
; Where an interrupt can bias it: only at a window's ends. A change is seen
; at the first poll after it, so the count runs from the poll that saw the
; first change to the poll that saw the last. Anything that makes the change
; at a window's end be seen late (an interrupt handler between the change and
; the poll; for the frames, one that holds the vertical blank's own handler
; off, which makes the change itself late) makes that window's count too big,
; and the next window's, which starts at the same change, too small by as much.
; (An interrupt in between costs nothing: the timer counts through it.) So
; HW_CLOCK takes HW_WINDOWS = 3 windows back to back, each HW_EDGES changes
; long, and keeps the middle count of the three: one disturbed change makes
; one window too long and its neighbour too short, and the middle one is then
; the undisturbed one (hw/diag.md, D3a review). Measured by the emulator with a
; burst of USB interrupts at a window's end: 7,157,126, 7,161,084 and
; 7,159,092 cycles, the middle one exact. The polls' own gaps didn't show that
; disturbance, so the choice is by the counts themselves.

        .setcpu "65C02"
        .include "backplane.inc"
        .include "oohw.inc"

HW_WINDOWS      = 3                     ; the middle of three is kept: wbuf and the swaps are for three

        .code
clkfar: rts                             ; carry set: no change in time (a branch's reach from HW_CLOCK)
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
@first: jsr     tick                    ; the first change starts the first window
        bcs     clkfar
        lda     (HW_PTR)
        cmp     last
        beq     @first
        sta     last
        stz     wins
@win:   jsr     zero
        lda     HW_EDGES
        sta     left
@more:  jsr     tick
        bcs     clkfar
        lda     (HW_PTR)
        cmp     last
        beq     @more
        sta     last
        dec     left
        bne     @more
        lda     wins                    ; this window's count into its place in wbuf
        asl     a
        asl     a
        tay
        ldx     #0
@keep:  lda     HW_CYC,x
        sta     wbuf,y
        iny
        inx
        cpx     #4
        bne     @keep
        inc     wins
        lda     wins
        cmp     #HW_WINDOWS
        bne     @win
        ldx     #0                      ; the middle of three: compare and swap 0-1, 1-2, 0-1
        ldy     #4
        jsr     cswap
        ldx     #4
        ldy     #8
        jsr     cswap
        ldx     #0
        ldy     #4
        jsr     cswap
        ldx     #3
@ret:   lda     wbuf + 4,x
        sta     HW_CYC,x
        dex
        bpl     @ret
        clc
        rts

; wbuf's counts at X and Y in order: swapped if the one at X is the bigger.
cswap:  stx     ci
        sty     cj
        lda     wbuf + 3,x              ; compare from the top byte
        cmp     wbuf + 3,y
        bne     @dec
        lda     wbuf + 2,x
        cmp     wbuf + 2,y
        bne     @dec
        lda     wbuf + 1,x
        cmp     wbuf + 1,y
        bne     @dec
        lda     wbuf,x
        cmp     wbuf,y
@dec:   bcc     @done                   ; X's is the smaller
        beq     @done
        lda     #4
        sta     cn
@sw:    ldx     ci
        ldy     cj
        lda     wbuf,x
        pha
        lda     wbuf,y
        sta     wbuf,x
        pla
        sta     wbuf,y
        inc     ci
        inc     cj
        dec     cn
        bne     @sw
@done:  rts

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
wbuf:   .res    12                      ; the windows' counts, 4 bytes each
wins:   .res    1
ci:     .res    1
cj:     .res    1
cn:     .res    1

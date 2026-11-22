; HW_CLOCK, HW_FRAMEHZ and HW_DIV (oohw.inc): the CPU's clock against a time
; base that isn't PHI2 (hw/diag.md, "What the hardware gives": the DS3231's
; second, the video frame).
;
; The VIA's timer 2 counts PHI2 whatever the CPU is doing, so the count is
; whole across OODOS's interrupts. Each pass of the poll (watch, 42 cycles)
; reads timer 2 and then the watched byte; while the byte is unchanged the
; reading is kept. A change is stamped with the reading of the last pass
; that saw the byte as it was, which comes before the change; its gap is
; the time from that reading to one taken just after the poll that saw the
; change, so the change came within the gap after its stamp, wherever an
; interrupt fell between the two: the interrupt widens the gap and leaves
; the stamp where it was. A gap with no interrupt is 79 cycles, the
; change 11-53 cycles after its stamp. Every 128 passes, and
; at each change, what timer 2 went down by since the last is added to
; HW_CYC, so it must go no more than 65,536 cycles (9 ms) between two; 128
; passes are 5,376 cycles, and an interrupt handler never holds the rest
; off. Polling NB_SEC latches the time's other registers; how that meets
; the north bridge's own I2C reads of the DS3231 on real hardware is
; unknown (the emulator latches whole).
;
; HW_CLOCK stamps four changes, HW_EDGES apart (three windows back to back),
; and keeps the span between the two whose gaps say least: of each pair i <
; j, the bigger gap over the windows between them (j - i), the least of
; those, the wider span of equals; HW_CYC is that span over its windows.
; An interrupt running when a change comes puts the change anywhere in a
; gap as long as its handler (OODOS's millisecond one: 230-370 cycles; a
; burst of USB events: 11,000), and for the frames one that holds the
; vertical blank's handler off makes the change itself late, the gap with
; it; the pair leaves such a change out, and spreads what is left over the
; most windows. (An interrupt between two changes costs nothing: the timer
; counts through it.) When every change has a handler at it, as OODOS's
; millisecond one can for several seconds running (since OODOS 0.16 the
; north bridge's tick, which slides against the clock chip's second by the
; crystals' error alone; before it the VIA's 7,159 cycles, 90 a second),
; each stamp is early by where the change fell in it, which moves only as
; slowly as that slide.
;
; Why (B5, 2026-11-16; hw/diag.md, D3a review, has the first forms): the
; form before read the timer at the start of a 142-cycle pass and the byte
; at its end, stamped a change with the pass that saw it, and kept the
; middle of the three windows. OODOS's millisecond handler at a second's
; change, taken between that pass's read and its poll, left the stamp
; before the change; taken after the poll, after the handler. Two windows
; of three were out by 250-500 cycles and the middle one with them
; (7,159,342 Hz on BIOS 0.16's first build). Stamped with the pass before,
; that handler can't move the stamp past the change; and with the gaps, a
; burst at one change and the millisecond handler at another (the middle of
; three: 7,159,270 Hz) leave the span between the other two.

        .setcpu "65C02"
        .include "backplane.inc"
        .include "oohw.inc"

HW_WINDOWS      = 3                     ; windows back to back: wbuf, gapbuf and the pairs' table are for three
HW_PASSES       = 128                   ; passes of the poll between two additions to HW_CYC

        OOHW_CODE
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
        sta     old
        stx     old + 1
        lda     #HW_PASSES
        sta     cnt
        jsr     zero
        lda     (HW_PTR)
        sta     last
        jsr     watch                   ; the first change starts the first window
        bcs     clkfar
        ldy     #0
        jsr     keepgap
        stz     wins
@win:   jsr     zero
        lda     HW_EDGES
        sta     left
@more:  jsr     watch
        bcs     clkfar
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
        lda     wins                    ; and its last change's gap into gapbuf
        asl     a
        tay
        jsr     keepgap
        lda     wins
        cmp     #HW_WINDOWS
        bne     @win

        lda     #$FF                    ; the pair: the least score, max(gap i, gap j) x 6 / (j - i),
        sta     best                    ; best in HW_CYC (the windows are in wbuf)
        sta     best + 1
        sta     best + 2
        ldx     #0
@pair:  stx     pn
        lda     pairi,x
        asl     a
        tay
        lda     pairj,x
        asl     a
        tax
        lda     gapbuf + 1,y            ; the bigger of the two gaps into m
        cmp     gapbuf + 1,x
        bne     @cmp
        lda     gapbuf,y
        cmp     gapbuf,x
@cmp:   bcs     @gi
        txa
        tay
@gi:    lda     gapbuf,y
        sta     m
        lda     gapbuf + 1,y
        sta     m + 1
        ldx     pn
        ldy     pairx,x
        stz     sc
        stz     sc + 1
        stz     sc + 2
@mul:   clc
        lda     sc
        adc     m
        sta     sc
        lda     sc + 1
        adc     m + 1
        sta     sc + 1
        lda     sc + 2
        adc     #0
        sta     sc + 2
        dey
        bne     @mul
        lda     sc                      ; kept if less than the best so far
        cmp     best
        lda     sc + 1
        sbc     best + 1
        lda     sc + 2
        sbc     best + 2
        bcs     @next
        lda     sc
        sta     best
        lda     sc + 1
        sta     best + 1
        lda     sc + 2
        sta     best + 2
        stx     bp
@next:  inx
        cpx     #PAIRS
        beq     @span
        jmp     @pair
@span:

        ldx     #3                      ; the span's sum in HW_NUM
@clr:   stz     HW_NUM,x
        dex
        bpl     @clr
        ldx     bp
        lda     pairj,x
        sec
        sbc     pairi,x
        sta     span
        sta     left
        lda     pairi,x
@sum:   asl     a                       ; HW_NUM += wbuf's window A
        asl     a
        tay
        clc
        lda     HW_NUM
        adc     wbuf,y
        sta     HW_NUM
        lda     HW_NUM + 1
        adc     wbuf + 1,y
        sta     HW_NUM + 1
        lda     HW_NUM + 2
        adc     wbuf + 2,y
        sta     HW_NUM + 2
        lda     HW_NUM + 3
        adc     wbuf + 3,y
        sta     HW_NUM + 3
        tya
        lsr     a
        lsr     a
        inc     a
        dec     left
        bne     @sum
        lda     span                    ; over its windows, to the nearest
        lsr     a
        clc
        adc     HW_NUM
        sta     HW_NUM
        bcc     @div
        inc     HW_NUM + 1
        bne     @div
        inc     HW_NUM + 2
        bne     @div
        inc     HW_NUM + 3
@div:   lda     span
        ldx     #0
        jsr     HW_DIV
        ldx     #3
@ret:   lda     HW_NUM,x
        sta     HW_CYC,x
        dex
        bpl     @ret
        clc
        rts

        OOHW_RODATA
; The pairs of changes, i and j, and 6 / (j - i): the widest spans first, so
; the first of equal scores is the widest.
pairi:  .byte   0, 0, 1, 0, 1, 2
pairj:  .byte   3, 2, 3, 1, 2, 3
pairx:  .byte   2, 3, 3, 6, 6, 6
PAIRS   = * - pairx
.assert PAIRS = 6 && HW_WINDOWS = 3, error, "the pairs are of four changes, three windows"

        OOHW_CODE

; gapbuf at Y (the change's number x 2) = the last change's gap.
keepgap:
        lda     gap
        sta     gapbuf,y
        lda     gap + 1
        sta     gapbuf + 1,y
        rts

; The next change of the byte at HW_PTR: last its new value, HW_CYC brought
; up to the change's stamp (old, the last unchanged pass's timer 2), gap
; from the stamp to a reading after the poll that saw the change; carry set
; once HW_CYC reaches 2^24 first. A pass: timer 2 into X (high) and Y (low), the high byte read on
; both sides of the low; the byte; Y and X kept in old if it is as it was.
; 42 cycles.
watch:
@pass:  ldx     VIA_T2CH
        ldy     VIA_T2CL
        cpx     VIA_T2CH
        bne     @pass
        lda     (HW_PTR)
        cmp     last
        bne     @chg
        sty     old
        stx     old + 1
        dec     cnt
        bne     @pass
        lda     #HW_PASSES
        sta     cnt
        jsr     acc
        bcc     @pass
        rts
@chg:   sta     last
        jsr     t2now                   ; the gap: old less timer 2 now, after the poll that saw the change
        eor     #$FF                    ; (timer 2 counts down)
        sec
        adc     old
        sta     gap
        txa
        eor     #$FF
        adc     old + 1
        sta     gap + 1
        ; fall through: HW_CYC up to the stamp

; HW_CYC += what timer 2 went down by from prev to old, and prev = old; carry
; set once HW_CYC reaches 2^24.
acc:    sec
        lda     prev
        sbc     old
        tay
        lda     prev + 1
        sbc     old + 1
        tax
        lda     old
        sta     prev
        lda     old + 1
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

; HW_CYC = 0.
zero:   stz     HW_CYC
        stz     HW_CYC + 1
        stz     HW_CYC + 2
        stz     HW_CYC + 3
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

        OOHW_BSS
HW_WATCH: .res  2
HW_EDGES: .res  1
HW_CYC: .res    4
HW_NUM: .res    4
prev:   .res    2
old:    .res    2
cnt:    .res    1
last:   .res    1
left:   .res    1
dvs:    .res    2
rem:    .res    3
tmp:    .res    1
wbuf:   .res    12                      ; the windows' counts, 4 bytes each
gapbuf: .res    8                       ; the four changes' gaps, 2 bytes each
wins:   .res    1
gap:    .res    2

; The pair's choice and its span, after the windows and before HW_DIV, in
; what is free then.
m       = dvs                           ; the bigger gap of a pair
sc      = rem                           ; its score
best    = HW_CYC                        ; the least score so far
pn      = tmp                           ; the pair's place in the table
bp      = last                          ; the best pair's
span    = wins                          ; its windows

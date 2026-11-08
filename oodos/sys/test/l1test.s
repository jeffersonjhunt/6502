; L1TEST: OOLIB's L1 (hw/oolib.md, "The plan"): the clocks, the event area,
; the hooks and function 80, for test_oodos_clock*.cpp, a case by the command
; tail's first letter (the second for H), a line a result. "aa xx" is A and
; X as the BDOS returned them.
;
;   I   INFO aa xx (80's FT_INFO, no block: OOLIB 01, areas 01); INFOB and its
;       block's 4 bytes (01 01, the clocks 03, layout 01); VER and 12 bytes of
;       a buffer of $EE that 72 fills (its 10, then EE EE)
;   A   the event area's refusals, each R? aa: R1 15 bytes, R2 across $4000,
;       R3 in the I/O hole, R4 in W3, R5 no block, R6 W1 on the system block;
;       then OPEN aa xx (AREA, 32 bytes, filled with $5A first), DELAY aa xx
;       (500 ms), AREA and its 32 bytes, COUNT and FT_COUNT's 8, CLOSE aa
;       xx; then 300 ms more (DELAY2) with it closed, and AFTER and its 16
;   W   FT_COUNT's 8 (W0), FT_WAIT for 30 frames: WAIT aa xx; FT_COUNT's 8
;       (W1); FT_WAIT with no block: NEXT aa xx
;   D   D0 and the ms counter's low word, FT_DELAY of 1000: DELAY aa xx; D1
;   H?  the hooks: set with W1 on block 5 (HOOKBLK), W1 back on 1 after. The
;       frame hook counts at $4000 (W1's view, block 5's $14000), keeps the
;       windows it ran with at $4004 and its P at $4008, counts in zero page
;       (FZ); the tick hook (every 10 ms) the same at $4010, $4014, $4018 (TZ).
;       Each puts A, X and Y to $A5, $5A, $C3. FHOOK aa xx, THOOK aa xx, then:
;       B  waits 1000 ms in the BDOS (FT_DELAY)
;       P  runs 1000 ms outside it (by the event area's ms counter), checking
;          its registers, windows and a value on its stack all the while
;       C  waits in function 10 (a ^C ends it)
;       N  loops for ever, no BDOS call (Break ends it)
;       W  nothing: the warm boot at once
;       then (B, P) FZ ff TZ tt BAD bb (registers, windows or stack found
;       changed) and JMP WBOOT
;   B   the budgets: a frame hook of about 8,000 cycles and a tick hook of
;       about 300 every millisecond, 2,000 ms in the BDOS (FT_DELAY); then
;       COSTS aa xx and FT_COSTS's 6 bytes
;   E   interrupts OODOS doesn't answer: the video card's line interrupt and
;       the VIA's timer 2 turned on (against the rules); 200 ms later ALIVE,
;       and the enables as they are (VIEN vv, VIER ii); then timer 2's enable
;       on again, and the warm boot

        .setcpu "65C02"
        .include "oodos.inc"
        .include "backplane.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"

HOOKBLK = 5
HW1     = $4000                         ; the hooks' records, in W1 as they see it

        .zeropage
fz:     .res    1                       ; the frame hook's count
tz:     .res    1                       ; the tick hook's
bad:    .res    1
t0:     .res    2
spin:   .res    2
sayx:   .res    1

        .code
start:  lda     BP_DMA + 1              ; the tail's first letter, after its space
        cmp     #' '
        bne     @l
        lda     BP_DMA + 2
        ldx     BP_DMA + 3
        bra     @go
@l:     ldx     BP_DMA + 2
@go:    stx     mode2
        cmp     #'I'
        bne     :+
        jmp     case_i
:       cmp     #'A'
        bne     :+
        jmp     case_a
:       cmp     #'W'
        bne     :+
        jmp     case_w
:       cmp     #'D'
        bne     :+
        jmp     case_d
:       cmp     #'H'
        bne     :+
        jmp     case_h
:       cmp     #'B'
        bne     :+
        jmp     case_b
:       cmp     #'E'
        bne     :+
        jmp     case_e
:       jmp     WBOOT

; ---- small helpers ---------------------------------------------------------

; Function 80 with the block at A/X; prints "name aa xx" if A/X (name) given in t0.
f80:    ldy     #O_FRAME
        jmp     BDOS
; Print the label at t0, then A and X.
say:    pha
        stx     sayx                    ; the BDOS keeps no X
        lda     t0
        ldx     t0 + 1
        jsr     PUTS
        pla
        jsr     HEX2
        jsr     SPACE
        lda     sayx
        jsr     HEX2
        jmp     CRLF
.macro SAY label
        .local  s
        pha
        lda     #<s
        sta     t0
        lda     #>s
        sta     t0 + 1
        pla
        jsr     say
        .pushseg
        .rodata
s:      .byte   label, " $"
        .popseg
.endmacro
; Print the label, then X bytes at A/Y (low/high).
.macro DUMP label, addr, n
        .local  s
        lda     #<s
        ldx     #>s
        jsr     PUTS
        lda     #<(addr)
        sta     PTR
        lda     #>(addr)
        sta     PTR + 1
        ldx     #n
        jsr     HEXN
        jsr     CRLF
        .pushseg
        .rodata
s:      .byte   label, " $"
        .popseg
.endmacro
.macro CALL80 blk
        lda     #<(blk)
        ldx     #>(blk)
        jsr     f80
.endmacro
delay:  sta     dblk + 1                ; A/X ms
        stx     dblk + 2
        CALL80  dblk
        rts

; ---- I ---------------------------------------------------------------------

case_i: lda     #FT_INFO
        ldx     #0
        jsr     f80
        SAY     "INFO"
        CALL80  iblk
        DUMP    "INFOB", iblk + 1, 4
        ldx     #11
        lda     #$EE
@fill:  sta     vbuf,x
        dex
        bpl     @fill
        lda     #<vbuf
        ldx     #>vbuf
        ldy     #O_VERSION
        jsr     BDOS
        DUMP    "VER", vbuf, 12
        jmp     WBOOT

; ---- A ---------------------------------------------------------------------

case_a: CALL80  r1
        SAY     "R1"
        CALL80  r2
        SAY     "R2"
        CALL80  r3
        SAY     "R3"
        CALL80  r4
        SAY     "R4"
        lda     #FT_OPEN
        ldx     #0
        jsr     f80
        SAY     "R5"
        lda     #SYS_BLOCK              ; W1 on the system block: an area there refused
        sta     NB_WIN1
        CALL80  r6
        ldy     #1
        sty     NB_WIN1
        SAY     "R6"
        ldx     #31
        lda     #$5A
@fill:  sta     area,x
        dex
        bpl     @fill
        CALL80  oblk
        SAY     "OPEN"
        lda     #<500
        ldx     #>500
        jsr     delay
        SAY     "DELAY"
        DUMP    "AREA", area, 32
        CALL80  cblk
        DUMP    "COUNT", cblk + 1, 8
        lda     #FT_CLOSE
        ldx     #0
        jsr     f80
        SAY     "CLOSE"
        lda     #<300
        ldx     #>300
        jsr     delay
        SAY     "DELAY2"
        DUMP    "AFTER", area, 16
        jmp     WBOOT

; ---- W, D ------------------------------------------------------------------

case_w: CALL80  cblk
        DUMP    "W0", cblk + 1, 8
        CALL80  wblk
        SAY     "WAIT"
        CALL80  cblk
        DUMP    "W1", cblk + 1, 8
        lda     #FT_WAIT
        ldx     #0
        jsr     f80
        SAY     "NEXT"
        jmp     WBOOT

case_d: lda     #FT_COUNT
        ldx     #0
        jsr     f80
        SAY     "D0"
        lda     #<1000
        ldx     #>1000
        jsr     delay
        SAY     "DELAY"
        lda     #FT_COUNT
        ldx     #0
        jsr     f80
        SAY     "D1"
        jmp     WBOOT

; ---- H ---------------------------------------------------------------------

case_h: stz     fz
        stz     tz
        stz     bad
        lda     #HOOKBLK
        sta     NB_WIN1
        ldx     #$1F
@clr:   stz     HW1,x
        dex
        bpl     @clr
        CALL80  fhblk
        pha                             ; W1 back before anything is said
        lda     #1
        sta     NB_WIN1
        pla
        SAY     "FHOOK"
        lda     #HOOKBLK
        sta     NB_WIN1
        CALL80  thblk
        pha
        lda     #1
        sta     NB_WIN1
        pla
        SAY     "THOOK"
        lda     mode2
        cmp     #'B'
        beq     @b
        cmp     #'P'
        beq     @p
        cmp     #'C'
        beq     @c
        cmp     #'N'
        beq     @n
        jmp     WBOOT                   ; W
@b:     lda     #<1000
        ldx     #>1000
        jsr     delay
        bra     @end
@c:     lda     #<lbuf
        ldx     #>lbuf
        ldy     #C_READSTR
        jsr     BDOS
        bra     @end
@n:     bra     @n
@p:     CALL80  oblk                    ; the event area, for the time
        lda     area + EV_MS
        sta     t0
        lda     area + EV_MS + 1
        sta     t0 + 1
        lda     #$77                    ; a value on the stack, watched
        pha
@round: lda     #$11
        ldx     #$22
        ldy     #$33
        stz     spin
@spin:  cmp     #$11
        bne     @bad
        cpx     #$22
        bne     @bad
        cpy     #$33
        bne     @bad
        dec     spin
        bne     @spin
        lda     NB_WIN1
        cmp     #1
        bne     @bad
        pla
        pha
        cmp     #$77
        bne     @bad
        sec                             ; 1000 ms by the area's counter? (the low byte
        lda     area + EV_MS            ; first: a carry between the two reads at worst
        sbc     t0                      ; makes it a round late)
        tax
        lda     area + EV_MS + 1
        sbc     t0 + 1
        cmp     #>1000
        bcc     @round
        bne     @done
        cpx     #<1000
        bcc     @round
        bra     @done
@bad:   inc     bad
        bra     @round
@done:  pla
@end:   lda     fz
        ldx     tz
        SAY     "FZ TZ"
        lda     bad
        ldx     #0
        SAY     "BAD"
        jmp     WBOOT

; The hooks, in W0 (this program's block, as when they were set).
fhook:  inc     fz
        inc     HW1
        bne     :+
        inc     HW1 + 1
:       lda     NB_WIN0
        sta     HW1 + 4
        lda     NB_WIN1
        sta     HW1 + 5
        lda     NB_WIN2
        sta     HW1 + 6
        lda     NB_WIN3
        sta     HW1 + 7
        php
        pla
        sta     HW1 + 8
        lda     #$A5
        ldx     #$5A
        ldy     #$C3
        rts
thook:  inc     tz
        inc     HW1 + $10
        bne     :+
        inc     HW1 + $11
:       lda     NB_WIN0
        sta     HW1 + $14
        lda     NB_WIN1
        sta     HW1 + $15
        lda     NB_WIN2
        sta     HW1 + $16
        lda     NB_WIN3
        sta     HW1 + $17
        php
        pla
        sta     HW1 + $18
        lda     #$A5
        ldx     #$5A
        ldy     #$C3
        rts

; ---- B ---------------------------------------------------------------------

case_b: CALL80  bfblk
        SAY     "FHOOK"
        CALL80  btblk
        SAY     "THOOK"
        lda     #<2000
        ldx     #>2000
        jsr     delay
        SAY     "DELAY"
        CALL80  costs
        SAY     "COSTS"
        DUMP    "COST", costs + 1, 6
        jmp     WBOOT

; About 8,000 cycles: 1,600 rounds of 5.
bfhook: ldx     #<1600
        ldy     #>1600 + 1
@l:     dex
        bne     @l
        dey
        bne     @l
        rts
; About 300: 55 rounds of 5, and the call.
bthook: ldx     #55
@l:     dex
        bne     @l
        rts

; ---- E ---------------------------------------------------------------------

case_e: lda     #200                    ; the video card's line interrupt at line 200
        sta     VIDEO_LINE
        stz     VIDEO_LINE + 1
        lda     VIDEO_IEN
        ora     #VIDEO_IRQ_LINE
        sta     VIDEO_IEN
        lda     #VIA_IER_SET | $20      ; the VIA's timer 2: enabled and started
        sta     VIA_IER
        lda     #$FF
        sta     VIA_T2CL
        sta     VIA_T2CH
        lda     #<200
        ldx     #>200
        jsr     delay
        lda     #FT_INFO
        ldx     #0
        jsr     f80
        SAY     "ALIVE"
        lda     VIDEO_IEN
        ldx     VIA_IER
        SAY     "VIEN VIER"
        lda     #VIA_IER_SET | $20
        sta     VIA_IER
        jmp     WBOOT

        .rodata
r1:     .byte   FT_OPEN
        .word   area
        .byte   15
r2:     .byte   FT_OPEN
        .word   $3FF8
        .byte   16
r3:     .byte   FT_OPEN
        .word   $C010
        .byte   16
r4:     .byte   FT_OPEN
        .word   $D000
        .byte   16
r6:     .byte   FT_OPEN
        .word   $4100
        .byte   16
oblk:   .byte   FT_OPEN
        .word   area
        .byte   32
wblk:   .byte   FT_WAIT, 30
fhblk:  .byte   FT_FRAMEHOOK
        .word   fhook
thblk:  .byte   FT_TICKHOOK
        .word   thook
        .word   10
bfblk:  .byte   FT_FRAMEHOOK
        .word   bfhook
btblk:  .byte   FT_TICKHOOK
        .word   bthook
        .word   1

        .data
iblk:   .byte   FT_INFO, 0, 0, 0, 0
dblk:   .byte   FT_DELAY, 0, 0
cblk:   .byte   FT_COUNT
        .res    8
costs:  .byte   FT_COSTS
        .res    6
lbuf:   .byte   20, 0
        .res    20

        .bss
mode2:  .res    1
vbuf:   .res    12
area:   .res    32

        .include "print.inc"

; OOLIB's playback status (include/oolib.inc, "Sound: playback";
; hw/oolib.md, L5d): PB_STATUS, apart from pb.s so a program that only
; pauses and resumes doesn't link the stream's arithmetic. It reads pb.s's
; times and state (pb.inc's PB*).

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .importzp PBSTAT_ZP             ; zppbstat.s's
pbp             = PBSTAT_ZP            ; the caller's block
ap              = PBSTAT_ZP + 2        ; the event area

        .bss
ms:     .res    4                       ; the status's milliseconds
under:  .res    2
state:  .res    1
sblk:   .res    13                      ; SN_STATUS's
dq:     .res    4                       ; a dividend, then the quotient
dv:     .res    2                       ; the divisor
rm:     .res    2                       ; the remainder
ma:     .res    4                       ; a multiplicand

        .code

; A/X a block of PBS_LEN bytes: +0 the state (PS_*), +1-4 the milliseconds
; played (paused time left out; a stream's from the frames the card has
; played), +5-6 a stream's underruns.
PB_STATUS:
        sta     pbp
        stx     pbp + 1
        jsr     PBNOW
        stz     under
        stz     under + 1
        ldx     #3                      ; ms: (paused ? t_pause : now) - t_begin - t_away
@ms:    lda     PBTNOW,x
        ldy     PBHELD
        beq     @n
        lda     PBTPAUSE,x
@n:     sta     ms,x
        dex
        bpl     @ms
        ldx     #0
        ldy     #4
        sec
@b:     lda     ms,x
        sbc     PBTBEGIN,x
        sta     ms,x
        inx
        dey
        bne     @b
        ldx     #0
        ldy     #4
        sec
@a:     lda     ms,x
        sbc     PBTAWAY,x
        sta     ms,x
        inx
        dey
        bne     @a
        lda     PBSRC
        bne     @on
        ldx     #3
@z:     stz     ms,x
        dex
        bpl     @z
        lda     #PS_STOPPED
        bra     @put
@on:    cmp     #PB_SID + 1
        bne     @strm
        jsr     sidstate
        bra     @put
@strm:  cmp     #PB_STREAM + 1
        bne     @doc
        jsr     strmstate
        bra     @put
@doc:   lda     SND_DOCCTL              ; the ES5503: DOCCTL's RUN
        and     #SND_DOC_RUN
        beq     @put                    ; (PS_STOPPED is 0)
        lda     #PS_PLAYING
@put:   sta     state
        ldy     #0
        sta     (pbp),y
@cp:    iny
        lda     ms - 1,y
        sta     (pbp),y
        cpy     #PBS_LEN - 1
        bne     @cp
        lda     #0
        clc
        rts
.assert under = ms + 4, error, "PB_STATUS copies ms and under as one run"
.assert PS_STOPPED = 0 && PBS_LEN = 7, error, "PB_STATUS's block: the state, ms, the underruns"

; A SID's state: paused (by PB_PAUSE), stopped (RUN off), playing if it
; doesn't report (PB_BEGIN's X) or there's no event area, or else from the
; mailbox's stamp: a byte within 2 s, or (before the first)
; within 10 s of the start, playing or starting; else stalled. The start is
; the later of EV_SMMS and t_floor (an area opened after SN_START has
; EV_SMMS 0; the 6510 sends nothing while held).
sidstate:
        lda     PBHELD
        beq     @run
        lda     #PS_PAUSED
        rts
@run:   lda     SND_CTRL
        and     #SND_RUN
        bne     @area
        lda     #PS_STOPPED
        rts
@area:  lda     PBRPT                   ; a tune that doesn't report: running is playing
        beq     @play
        lda     PBAREA
        ora     PBAREA + 1
        bne     @stamp
@play:
        lda     #PS_PLAYING
        rts
@stamp: lda     PBAREA
        sta     ap
        lda     PBAREA + 1
        sta     ap + 1
        ldy     #EV_SMMS + 3            ; dq = EV_SMMS
        ldx     #3
@mms:   lda     (ap),y
        sta     dq,x
        dey
        dex
        bpl     @mms
        ldx     #3                      ; the later of it and t_floor
@cmp:   lda     dq,x
        cmp     PBTFLOOR,x
        bne     @diff
        dex
        bpl     @cmp
        bra     @later                  ; equal
@diff:  bcs     @later
        ldx     #3
@fl:    lda     PBTFLOOR,x
        sta     dq,x
        dex
        bpl     @fl
@later: ldx     #0                      ; dq = now - it
        ldy     #4
        sec
@sub:   lda     PBTNOW,x
        sbc     dq,x
        sta     dq,x
        inx
        dey
        bne     @sub
        ldy     #EV_SMCNT               ; none yet: 10 s; else 2 s
        lda     (ap),y
        iny
        ora     (ap),y
        tax                             ; X 0: none yet
        lda     #<2000
        sta     dv
        lda     #>2000
        sta     dv + 1
        cpx     #0
        bne     @lim
        lda     #<10000
        sta     dv
        lda     #>10000
        sta     dv + 1
@lim:   lda     dq + 3                  ; past the limit: stalled
        ora     dq + 2
        bne     @stall
        lda     dv
        cmp     dq
        lda     dv + 1
        sbc     dq + 1
        bcc     @stall
        lda     #PS_PLAYING
        cpx     #0
        bne     @ret
        lda     #PS_STARTING
@ret:   rts
@stall: lda     #PS_STALLED
        rts

; The stream's state, its underruns and its milliseconds, from SN_STATUS:
; refused (not claimed) is stopped.
strmstate:
        lda     pbp                     ; the caller's block, kept
        pha
        lda     pbp + 1
        pha
        lda     #SN_STATUS
        sta     sblk
        lda     #<sblk
        ldx     #>sblk
        ldy     #O_SOUND
        jsr     BDOS
        cpx     #$FF
        bne     @got
        ldx     #3
@z:     stz     ms,x
        dex
        bpl     @z
        lda     #PS_STOPPED
        bra     @out
@got:   lda     sblk + 11
        sta     under
        lda     sblk + 12
        sta     under + 1
        jsr     strmms
        lda     sblk + 1                ; the card's state to PS_*
        cmp     #6
        bcs     @err
        tax
        lda     stmap,x
        bra     @out
@err:   lda     #PS_ERROR
@out:   tax
        pla
        sta     pbp + 1
        pla
        sta     pbp
        txa
        rts

; ms = the frames played (sblk + 5) x 1,000 / the stream's rate: (frames /
; rate) x 1,000 + (the remainder x 1,000) / rate. A rate of 0: 0.
strmms: lda     SND_RATE
        sta     dv
        lda     SND_RATE + 1
        sta     dv + 1
        ora     dv
        bne     @go
        ldx     #3
@z:     stz     ms,x
        dex
        bpl     @z
        rts
@go:    ldx     #3
@f:     lda     sblk + 5,x
        sta     dq,x
        dex
        bpl     @f
        jsr     div32
        ldx     #3                      ; ms = the seconds x 1,000
@q:     lda     dq,x
        sta     ma,x
        dex
        bpl     @q
        jsr     mul1000
        ldx     #3
@s:     lda     dq,x
        sta     ms,x
        dex
        bpl     @s
        lda     rm                      ; + the remainder x 1,000 / rate
        sta     ma
        lda     rm + 1
        sta     ma + 1
        stz     ma + 2
        stz     ma + 3
        jsr     mul1000
        jsr     div32
        ldx     #0
        ldy     #4
        clc
@add:   lda     ms,x
        adc     dq,x
        sta     ms,x
        inx
        dey
        bne     @add
        rts

; dq = dq / dv, rm the remainder (32 by 16 bits).
div32:  stz     rm
        stz     rm + 1
        ldx     #32
@bit:   asl     dq
        rol     dq + 1
        rol     dq + 2
        rol     dq + 3
        rol     rm
        rol     rm + 1
        bcs     @sub                    ; 17 bits: at least the divisor
        lda     rm
        cmp     dv
        lda     rm + 1
        sbc     dv + 1
        bcc     @next
@sub:   lda     rm
        sec
        sbc     dv
        sta     rm
        lda     rm + 1
        sbc     dv + 1
        sta     rm + 1
        inc     dq
@next:  dex
        bne     @bit
        rts

; dq = ma x 1,000 (32 bits kept): ma x 1,024 - ma x 16 - ma x 8. ma changed.
mul1000:
        jsr     @shl3                   ; ma x 8
        ldx     #3
@c8:    lda     ma,x
        sta     dq,x
        dex
        bpl     @c8
        jsr     @shl1                   ; x 16: dq += it
        jsr     @addma
        ldx     #6                      ; x 1,024
@k:     jsr     @shl1
        dex
        bne     @k
        ldx     #0                      ; dq = ma - dq
        ldy     #4
        sec
@d:     lda     ma,x
        sbc     dq,x
        sta     dq,x
        inx
        dey
        bne     @d
        rts
@shl3:  jsr     @shl1
        jsr     @shl1
@shl1:  asl     ma
        rol     ma + 1
        rol     ma + 2
        rol     ma + 3
        rts
@addma: phx
        ldx     #0
        ldy     #4
        clc
@ad:    lda     dq,x
        adc     ma,x
        sta     dq,x
        inx
        dey
        bne     @ad
        plx
        rts

        .rodata
; The card's STATE (idle, buffering, playing, paused, underrun, ended) to PS_*.
stmap:  .byte   PS_STOPPED, PS_STARTING, PS_PLAYING, PS_PAUSED, PS_STALLED, PS_ENDED

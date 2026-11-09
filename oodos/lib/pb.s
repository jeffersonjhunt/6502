; OOLIB's playback layer (include/oolib.inc, "Sound: playback"; hw/oolib.md,
; L5d): the verbs any player uses, whichever part is sounding: a .sid on the
; 6510 (PB_SID), the stream (PB_STREAM), the ES5503 (PB_DOC). Begin, pause,
; resume, and a status in one form for each, over function 82 and the event
; area's sound part. The program still starts and stops the part itself
; (SID_PLAY, SN_STREAM, its own ES5503 code); PB_BEGIN says which it was.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .zeropage
pbp:    .res    2                       ; the caller's block
ap:     .res    2                       ; the event area

        .bss
PBSRC:  .res    1                       ; the source + 1; 0 none begun
PBGEN:  .res    1
area:   .res    2                       ; the event area (PB_INIT), 0 none
held:   .res    1                       ; 1: paused by PB_PAUSE
t_begin: .res   4                       ; the ms counter at PB_BEGIN,
t_floor: .res   4                       ;   at PB_BEGIN or the last PB_RESUME (the stall rule's start),
t_pause: .res   4                       ;   at PB_PAUSE,
t_away: .res    4                       ;   the paused time summed
now:    .res    4
ms:     .res    4                       ; the status's milliseconds
under:  .res    2
state:  .res    1
cblk:   .res    9                       ; FT_COUNT's block
sblk:   .res    13                      ; SN_STATUS's
dq:     .res    4                       ; a dividend, then the quotient
dv:     .res    2                       ; the divisor
rm:     .res    2                       ; the remainder
ma:     .res    4                       ; a multiplicand

        .code

; A/X the event area (0: none). Without one, a SID's status can't see a
; stall: it says playing while the 6510 runs.
PB_INIT:
        sta     area
        stx     area + 1
        bra     ok

; A the source (PB_SID, PB_STREAM, PB_DOC), once the program has started it:
; its time from now, not paused.
PB_BEGIN:
        cmp     #PB_DOC + 1
        bcs     range
        inc     a
        sta     PBSRC
        stz     held
        inc     PBGEN
        jsr     getnow
        ldx     #3
@t:     lda     now,x
        sta     t_begin,x
        sta     t_floor,x
        stz     t_away,x
        dex
        bpl     @t
ok:     lda     #0
        clc
        rts
range:  lda     #PE_RANGE
        sec
        rts
nostate: lda    #PE_STATE
        sec
        rts
refused: lda    #PE_REFUSED
        sec
        rts

; The source held where it is: the 6510 by SN_HOLD, the stream by SN_PAUSE.
; The ES5503's is refused (PE_REFUSED) until function 82 has one.
PB_PAUSE:
        lda     PBSRC
        beq     nostate
        ldx     held
        bne     nostate
        ldx     #SN_HOLD
        cmp     #PB_SID + 1
        beq     @call
        ldx     #SN_PAUSE
        cmp     #PB_STREAM + 1
        bne     refused
@call:  txa
        jsr     f82op
        bne     refused
        lda     #1
        sta     held
        jsr     getnow
        ldx     #3
@t:     lda     now,x
        sta     t_pause,x
        dex
        bpl     @t
        bra     ok

; Held by PB_PAUSE, let go: SN_GO or SN_RESUME. The time held is left out
; of the time played, and the stall rule starts afresh.
PB_RESUME:
        lda     held
        beq     nostate
        lda     #SN_GO
        ldx     PBSRC
        cpx     #PB_SID + 1
        beq     @call
        lda     #SN_RESUME
@call:  jsr     f82op
        bne     refused
        stz     held
        jsr     getnow
        ldx     #0                      ; t_floor = now; t_away += now - t_pause
        ldy     #4
        sec
@t:     lda     now,x
        sta     t_floor,x
        sbc     t_pause,x
        sta     dq,x
        inx
        dey
        bne     @t
        ldx     #0
        ldy     #4
        clc
@sum:   lda     t_away,x
        adc     dq,x
        sta     t_away,x
        inx
        dey
        bne     @sum
        jmp     ok

; A/X a block of PBS_LEN bytes: +0 the state (PS_*), +1-4 the milliseconds
; played (paused time left out; a stream's from the frames the card has
; played), +5-6 a stream's underruns.
PB_STATUS:
        sta     pbp
        stx     pbp + 1
        jsr     getnow
        stz     under
        stz     under + 1
        ldx     #3                      ; ms: (paused ? t_pause : now) - t_begin - t_away
@ms:    lda     now,x
        ldy     held
        beq     @n
        lda     t_pause,x
@n:     sta     ms,x
        dex
        bpl     @ms
        ldx     #0
        ldy     #4
        sec
@b:     lda     ms,x
        sbc     t_begin,x
        sta     ms,x
        inx
        dey
        bne     @b
        ldx     #0
        ldy     #4
        sec
@a:     lda     ms,x
        sbc     t_away,x
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
        jmp     ok
.assert under = ms + 4, error, "PB_STATUS copies ms and under as one run"
.assert PS_STOPPED = 0 && PBS_LEN = 7, error, "PB_STATUS's block: the state, ms, the underruns"

; A SID's state: paused (by PB_PAUSE), stopped (RUN off), or with an event
; area, from the mailbox's stamp: a byte within 2 s, or (before the first)
; within 10 s of the start, playing or starting; else stalled. The start is
; the later of EV_SMMS and t_floor (an area opened after SN_START has
; EV_SMMS 0; the 6510 sends nothing while held).
sidstate:
        lda     held
        beq     @run
        lda     #PS_PAUSED
        rts
@run:   lda     SND_CTRL
        and     #SND_RUN
        bne     @area
        lda     #PS_STOPPED
        rts
@area:  lda     area
        ora     area + 1
        bne     @stamp
        lda     #PS_PLAYING
        rts
@stamp: lda     area
        sta     ap
        lda     area + 1
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
        cmp     t_floor,x
        bne     @diff
        dex
        bpl     @cmp
        bra     @later                  ; equal
@diff:  bcs     @later
        ldx     #3
@fl:    lda     t_floor,x
        sta     dq,x
        dex
        bpl     @fl
@later: ldx     #0                      ; dq = now - it
        ldy     #4
        sec
@sub:   lda     now,x
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

getnow: lda     #FT_COUNT
        sta     cblk
        lda     #<cblk
        ldx     #>cblk
        ldy     #O_FRAME
        jsr     BDOS
        ldx     #3
@c:     lda     cblk + 1,x
        sta     now,x
        dex
        bpl     @c
        rts

; Function 82's operation A, with no block: Z set if it was done (A = 0).
f82op:  ldx     #0
        ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        rts

        .rodata
; The card's STATE (idle, buffering, playing, paused, underrun, ended) to PS_*.
stmap:  .byte   PS_STOPPED, PS_STARTING, PS_PLAYING, PS_PAUSED, PS_STALLED, PS_ENDED

; OOLIB's playback status (include/oolib.inc, "Sound: playback";
; hw/oolib.md, L5d): PB_STATUS, from the card's own status (function 82's
; SN_TSTATUS, SN_STATUS, SN_MSTATUS), apart from pb.s so a program that only
; pauses and resumes doesn't link the stream's arithmetic.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"
        .importzp PBSTAT_ZP             ; zppbstat.s's

pbp             = PBSTAT_ZP            ; the caller's block

        .bss
ms:     .res    4                       ; the status's milliseconds
under:  .res    2
state:  .res    1
sblk:   .res    25                      ; the status's block (the largest: SN_MSTATUS's)
.assert 25 >= SN_STATLEN + 1, error, "sblk holds SN_STATUS's block"
dq:     .res    4                       ; a dividend, then the quotient
dv:     .res    2                       ; the divisor
rm:     .res    2                       ; the remainder
ma:     .res    4                       ; a multiplicand

        .code

; A/X a block of PBS_LEN bytes: +0 the state (PS_*), +1-4 the milliseconds
; played (paused time left out: the card's count for a tune and music; a
; stream's from the frames the card has played), +5-6 a stream's underruns.
PB_STATUS:
        sta     pbp
        stx     pbp + 1
        ldx     #5
@z:     stz     ms,x
        dex
        bpl     @z
        lda     PBSRC
        beq     @stop
        cmp     #PB_SID + 1
        bne     @strm
        lda     #SN_TSTATUS
        jsr     status
        bcs     @stop
        ldx     #3                      ; +4-7 the ms played
@t:     lda     sblk + 5,x
        sta     ms,x
        dex
        bpl     @t
        ldx     sblk + 1                ; the state to PS_*
        cpx     #7
        bcs     @stop
        lda     tmap,x
        bra     @held
@strm:  cmp     #PB_STREAM + 1
        bne     @music
        jsr     strmstate
        bra     @held
@music: lda     #SN_MSTATUS
        jsr     status
        bcs     @stop
        ldx     #3                      ; +18-21 the ms played
@m:     lda     sblk + 19,x
        sta     ms,x
        dex
        bpl     @m
        ldx     sblk + 1
        cpx     #3
        bcs     @stop
        lda     mmap,x
        bra     @held
@stop:  ldx     #5
@zz:    stz     ms,x
        dex
        bpl     @zz
        lda     #PS_STOPPED
@held:  ldx     PBHELD                  ; paused by PB_PAUSE: paused, whatever the card's word
        beq     @put
        cmp     #PS_STOPPED
        beq     @put
        lda     #PS_PAUSED
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

; Function 82's status A into sblk: carry set if refused (not claimed, no card).
status: sta     sblk
        lda     #<sblk
        ldx     #>sblk
        ldy     #O_SOUND
        jsr     BDOS
        cmp     #1
        rts

; The stream's state, its underruns and its milliseconds, from SN_STATUS:
; refused (not claimed) is stopped.
strmstate:
        lda     #SN_STATUS              ; (A the state: refused is A $FD up)
        sta     sblk
        lda     #<sblk
        ldx     #>sblk
        ldy     #O_SOUND
        jsr     BDOS
        cmp     #SN_REFUSED
        bcc     @got
        lda     #PS_STOPPED
        rts
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
        rts
@err:   lda     #PS_ERROR
        rts

; ms = the frames played (sblk + 5) x 1,000 / the stream's rate (sblk + 13):
; (frames / rate) x 1,000 + (the remainder x 1,000) / rate. A rate of 0: 0.
strmms: lda     sblk + 13
        sta     dv
        lda     sblk + 14
        sta     dv + 1
        ora     dv
        bne     @go
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
; The stream's state (idle, buffering, playing, paused, underrun, ended) to PS_*.
stmap:  .byte   PS_STOPPED, PS_STARTING, PS_PLAYING, PS_PAUSED, PS_STALLED, PS_ENDED
; A tune's (stopped, starting, playing, paused, stalled, jammed, ended).
tmap:   .byte   PS_STOPPED, PS_STARTING, PS_PLAYING, PS_PAUSED, PS_STALLED, PS_ERROR, PS_ENDED
; Music's (stopped, playing, paused).
mmap:   .byte   PS_STOPPED, PS_PLAYING, PS_PAUSED

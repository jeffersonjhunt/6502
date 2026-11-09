; OOLIB's playback layer (include/oolib.inc, "Sound: playback"; hw/oolib.md,
; L5d): the verbs any player uses, whichever part is sounding: a .sid on the
; 6510 (PB_SID), the stream (PB_STREAM), the ES5503 (PB_DOC). Begin, pause,
; resume, over function 82; PB_STATUS (pbstat.s) gives a status in one form
; for each. The program still starts and stops the part itself (SID_PLAY,
; SN_STREAM, its own ES5503 code); PB_BEGIN says which it was. pb.s's times
; and state are pb.inc's PB* for pbstat.s and pblev.s.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .bss
PBSRC:  .res    1                       ; the source + 1; 0 none begun
PBGEN:  .res    1
PBAREA: .res    2                       ; the event area (PB_INIT), 0 none
PBHELD: .res    1                       ; 1: paused by PB_PAUSE
PBTBEGIN: .res  4                       ; the ms counter at PB_BEGIN,
PBTFLOOR: .res  4                       ;   at PB_BEGIN or the last PB_RESUME (the stall rule's start),
PBTPAUSE: .res  4                       ;   at PB_PAUSE,
PBTAWAY: .res   4                       ;   the paused time summed
PBTNOW: .res    4                       ;   now (PBNOW)
PBRPT:  .res    1                       ; 1: the 6510 reports through the mailbox (PB_BEGIN's X)
cblk:   .res    9                       ; FT_COUNT's block
dq:     .res    4                       ; a difference

        .code

; A/X the event area (0: none). Without one, a SID's status can't see a
; stall: it says playing while the 6510 runs.
PB_INIT:
        sta     PBAREA
        stx     PBAREA + 1
        bra     ok

; A the source (PB_SID, PB_STREAM, PB_DOC), once the program has started it:
; its time from now, not paused. For a SID, X bit 0: the 6510 reports
; through the mailbox (a .sid with a play routine: SID_PLAYA not 0), so its
; silence is a stall; clear, a running 6510 is playing.
PB_BEGIN:
        cmp     #PB_DOC + 1
        bcs     range
        pha
        txa
        and     #1
        sta     PBRPT
        pla
        inc     a
        sta     PBSRC
        stz     PBHELD
        inc     PBGEN
        jsr     PBNOW
        ldx     #3
@t:     lda     PBTNOW,x
        sta     PBTBEGIN,x
        sta     PBTFLOOR,x
        stz     PBTAWAY,x
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
        ldx     PBHELD
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
        sta     PBHELD
        jsr     PBNOW
        ldx     #3
@t:     lda     PBTNOW,x
        sta     PBTPAUSE,x
        dex
        bpl     @t
        bra     ok

; Held by PB_PAUSE, let go: SN_GO or SN_RESUME. The time held is left out
; of the time played, and the stall rule starts afresh.
PB_RESUME:
        lda     PBHELD
        beq     nostate
        lda     #SN_GO
        ldx     PBSRC
        cpx     #PB_SID + 1
        beq     @call
        lda     #SN_RESUME
@call:  jsr     f82op
        stz     PBHELD                  ; refused too: the part was stopped meanwhile, so
        cmp     #0                      ;   its own state is the status (review F2)
        bne     refused
        jsr     PBNOW
        ldx     #0                      ; t_floor = now; t_away += now - t_pause
        ldy     #4
        sec
@t:     lda     PBTNOW,x
        sta     PBTFLOOR,x
        sbc     PBTPAUSE,x
        sta     dq,x
        inx
        dey
        bne     @t
        ldx     #0
        ldy     #4
        clc
@sum:   lda     PBTAWAY,x
        adc     dq,x
        sta     PBTAWAY,x
        inx
        dey
        bne     @sum
        jmp     ok

PBNOW: lda     #FT_COUNT
        sta     cblk
        lda     #<cblk
        ldx     #>cblk
        ldy     #O_FRAME
        jsr     BDOS
        ldx     #3
@c:     lda     cblk + 1,x
        sta     PBTNOW,x
        dex
        bpl     @c
        rts

; Function 82's operation A, with no block: Z set if it was done (A = 0).
f82op:  ldx     #0
        ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        rts


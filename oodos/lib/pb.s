; OOLIB's playback layer (include/oolib.inc, "Sound: playback"; hw/oolib.md,
; L5d): the verbs any player uses, whichever part is sounding: a .sid tune
; (PB_SID), the stream (PB_STREAM), music (PB_MUSIC). Begin, pause, resume,
; over function 82; PB_STATUS (pbstat.s) gives a status in one form for each,
; from the card's own. The program still starts and stops the part itself
; (SID_PLAY, SN_STREAM, MU_*); PB_BEGIN says which it was. pb.s's state is
; pb.inc's PB* for pbstat.s and pblev.s.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .bss
PBSRC:  .res    1                       ; the source + 1; 0 none begun
PBGEN:  .res    1
PBAREA: .res    2                       ; the event area (PB_INIT), 0 none
PBHELD: .res    1                       ; 1: paused by PB_PAUSE
mblk:   .res    2                       ; SN_MCTL's block

        .code

; A/X the event area (0: none): kept for PB_STATUS (the card says a tune's
; state itself now).
PB_INIT:
        sta     PBAREA
        stx     PBAREA + 1
        bra     ok

; A the source (PB_SID, PB_STREAM, PB_MUSIC), once the program has started
; it: begun, not paused. X is no longer used (the card knows a tune's
; stall). PB_DOC is refused (PE_REFUSED: the ES5503 is the card's).
PB_BEGIN:
        cmp     #PB_MUSIC + 1
        bcs     range
        cmp     #PB_DOC
        beq     refused
        inc     a
        sta     PBSRC
        stz     PBHELD
        inc     PBGEN
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

; The source held where it is: the tune by SN_TPAUSE, the stream by
; SN_PAUSE, music by SN_MCTL's pause.
PB_PAUSE:
        lda     PBSRC
        beq     nostate
        ldx     PBHELD
        bne     nostate
        ldx     #SN_TPAUSE
        ldy     #1
        jsr     verb
        bne     refused
        lda     #1
        sta     PBHELD
        bra     ok

; Held by PB_PAUSE, let go: SN_TRESUME, SN_RESUME or music's resume.
PB_RESUME:
        lda     PBHELD
        beq     nostate
        lda     PBSRC
        ldx     #SN_TRESUME
        ldy     #2
        jsr     verb
        stz     PBHELD                  ; refused too: the part was stopped meanwhile, so
        cmp     #0                      ;   its own state is the status (review F2)
        bne     refused
        bra     ok

; PBSRC's verb: X the tune's operation (SN_TPAUSE, SN_TRESUME; the stream's is
; 6 more: SN_PAUSE, SN_RESUME), Y music's SN_MCTL. Z set if done.
verb:   cmp     #PB_MUSIC + 1
        beq     @music
        cmp     #PB_SID + 1
        beq     @op
        txa
        clc
        adc     #SN_PAUSE - SN_TPAUSE
        tax
@op:    txa
        ldx     #0
        bra     f82
@music: lda     #SN_MCTL
        sta     mblk
        sty     mblk + 1
        lda     #<mblk
        ldx     #>mblk
f82:    ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        rts
.assert SN_TRESUME - SN_TPAUSE = SN_RESUME - SN_PAUSE, error, "verb: the stream's verbs a fixed step from the tune's"

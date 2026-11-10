; OOLIB's music (include/oolib.inc, "Sound: music"; hw/oolib.md, L6b): the
; library side of the music driver (lib/mudrv/mudrv.s, L6a), which plays
; notes on the six SID voices from the card's 6510. MU_INIT loads it and
; starts it; each call sends a command through function 82's SN_SEND; the
; program hands each EK_SOUND event's byte to MU_EVENT, which keeps each
; voice's count of entries waiting (GW-BASIC's PLAY(n), ON PLAY). MU_NOTE
; (munote.s) and MU_TONE (mutone.s) put a note or a tone in Hz in a voice's
; queue through MUQ here.
;
; The counts: one up for each NOTE or MARK sent, one down for each entry the
; driver says finished, reached or refused. Its "queue empty" doesn't set 0:
; it says what was so when the driver sent it, and the program may have
; queued more before handing it over. MU_STOP sets 0 and follows the STOP
; with marker 15, the library's own: the voice's events before it come from
; what STOP dropped, so they are let go until it is back, and a count of what
; was queued after the STOP is kept true. An event lost on the way (the
; driver's 64 waiting, or the program's queue full) leaves the count high
; until the voice's next MU_STOP.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "mu.inc"
        .include "physof.inc"

        .zeropage
MUP:    .res    2                       ; a block's pointer (munote.s's and mutone.s's too)

        .data
MUON:   .byte   0                       ; 1 once MU_INIT has started the driver (in DATA: a .COM's BSS
                                        ;   isn't cleared, so a program run again would find the last run's)

        .bss
MUCLK:  .res    1                       ; 0 PAL, 1 NTSC
MUCNT:  .res    MU_VOICES               ; each voice's entries waiting
MUDRAIN: .res   MU_VOICES               ; 1: STOP's marker not back yet
MUF:    .res    2                       ; MUQ's entry: the frequency word,
MUL:    .res    2                       ;   the length,
MUG:    .res    2                       ;   the gate time
mblk:   .res    8                       ; function 82's block
tries:  .res    2                       ; send's: the millisecond counter when the card was busy
mvoice: .res    1
mmark:  .res    1

        .data
; The driver, loaded at $1000 of the card's RAM; its latch is set here for
; the clock before it goes (in DATA: written).
image:  .incbin "mudrv.bin"
IMAGE_LEN = * - image
LATCH   = image + 3

        .code

; A = the clock: 0 PAL, 1 NTSC (CTRL's NTSC for SN_START, and the driver's
; millisecond). PE_RANGE past 1; PE_STATE if started already; PE_REFUSED if
; the 6510 can't be claimed (another program's, or no sound card) or the load
; is refused.
MU_INIT:
        cmp     #2
        bcc     @clk
        jmp     range
@clk:   ldx     MUON
        beq     @new
        jmp     nostate
@new:   sta     MUCLK
        lda     #SN_CLAIM
        sta     mblk
        lda     #SC_6510
        sta     mblk + 1
        jsr     f82blk
        beq     @stop
        jmp     refused
@stop:  lda     #SN_STOP
        jsr     f82op
        ldx     MUCLK                   ; 985 or 1,023 of the SID clock a millisecond
        lda     latlo,x
        sta     LATCH
        lda     lathi,x
        sta     LATCH + 1
        lda     #<image
        ldx     #>image
        jsr     PHYSOF
        lda     #SN_LOAD
        sta     mblk
        ldx     #2
@p:     lda     PHYS,x
        sta     mblk + 1,x
        dex
        bpl     @p
        lda     #<MU_BASE
        sta     mblk + 4
        lda     #>MU_BASE
        sta     mblk + 5
        lda     #<IMAGE_LEN
        sta     mblk + 6
        lda     #>IMAGE_LEN
        sta     mblk + 7
        jsr     f82blk
        bne     @fail
        lda     #SN_START               ; the vector written, the 6510 reset and run
        sta     mblk
        lda     #<MU_BASE
        sta     mblk + 1
        lda     #>MU_BASE
        sta     mblk + 2
        lda     MUCLK                   ; CTRL's bit 0: NTSC
        sta     mblk + 3
        jsr     f82blk
        bne     @fail
        ldx     #MU_VOICES - 1
@z:     stz     MUCNT,x
        stz     MUDRAIN,x
        dex
        bpl     @z
        lda     #1
        sta     MUON
        jmp     ok
@fail:  jsr     release
        jmp     refused

        .rodata
latlo:  .byte   <(985 - 1), <(1023 - 1)
lathi:  .byte   >(985 - 1), >(1023 - 1)
        .code

; The driver stopped, the SIDs silenced, the 6510 given back. PE_STATE if
; not started.
MU_QUIT:
        lda     MUON
        bne     @on
        jmp     nostate
@on:    stz     MUON
        jsr     release
        jmp     ok

release:
        lda     #SN_STOP
        jsr     f82op
        lda     #SN_RELEASE
        sta     mblk
        lda     #SC_6510
        sta     mblk + 1
        jmp     f82blk

; MUQ (munote.s's, mutone.s's): voice A, MUF, MUL, MUG queued. PE_RANGE for
; a voice past 5, PE_FULL with 32 waiting.
MUQ:    jsr     voice
        bcs     @out
        lda     MUCNT,x
        cmp     #MU_QLEN
        bcc     @room
        lda     #PE_FULL
        sec
@out:   rts
@room:  txa                             ; $0v and the six bytes
        jsr     send
        bcs     @out
        ldy     #0
@b:     lda     MUF,y
        phy
        jsr     send
        ply
        bcs     @out
        iny
        cpy     #6
        bne     @b
        ldx     mvoice
        inc     MUCNT,x
        jmp     ok

; A/X a block: +0 the voice, +1 the control register's bits (waveform,
; ring, sync), +2 attack/decay, +3 sustain/release, +4-5 the pulse width:
; the voice's instrument from its next note on.
MU_INSTR:
        sta     MUP
        stx     MUP + 1
        lda     (MUP)
        jsr     voice
        bcs     @out
        txa
        ora     #$10
        jsr     send
        bcs     @out
        ldy     #1
@b:     lda     (MUP),y
        phy
        jsr     send
        ply
        bcs     @out
        iny
        cpy     #6
        bne     @b
        jmp     ok
@out:   rts

; A = a voice, or MU_ALL: its queue emptied and its gate off. Its count is
; 0 from now; the events still on their way from before are let go.
MU_STOP:
        jsr     started
        bcs     @out
        cmp     #MU_ALL
        bne     @one
        ldx     #MU_VOICES - 1
@all:   phx
        txa
        jsr     stop1
        plx
        bcs     @out
        dex
        bpl     @all
        jmp     ok
@one:   jsr     voice
        bcs     @out
        txa
        jsr     stop1
        bcs     @out
        jmp     ok
@out:   rts

; Voice A: STOP, then the library's marker 15, which ends the drain.
stop1:  sta     mvoice
        tax
        stz     MUCNT,x
        lda     #1
        sta     MUDRAIN,x
        txa
        ora     #$20
        jsr     send
        bcs     @out
        lda     mvoice
        ora     #$30
        jsr     send
        bcs     @out
        lda     #MU_MARKS
        jmp     send
@out:   rts

MU_PAUSE:
        lda     #$40
        bra     cmd0
MU_RESUME:
        lda     #$50
cmd0:   pha
        jsr     started
        pla
        bcs     @out
        jsr     send
        bcs     @out
        jmp     ok
@out:   rts

; A = a voice, X = a marker, 0-14: back from MU_EVENT when the voice
; reaches it. PE_RANGE for 15 up (15 is the library's).
MU_MARK:
        cpx     #MU_MARKS
        bcc     @m
        jmp     range
@m:     stx     mmark
        jsr     voice
        bcs     @out
        lda     MUCNT,x
        cmp     #MU_QLEN
        bcc     @room
        lda     #PE_FULL
        sec
@out:   rts
@room:  lda     mvoice
        ora     #$30
        jsr     send
        bcs     @out
        lda     mmark
        jsr     send
        bcs     @out
        ldx     mvoice
        inc     MUCNT,x
        jmp     ok

; A = a SID (0 A, 1 B, MU_ALL both), X = its volume, 0-15.
MU_VOLUME:
        stx     mmark
        cpx     #16
        bcs     @range
        cmp     #MU_ALL
        bne     @one
        lda     #7
@one:   cmp     #7
        beq     @go
        cmp     #2
        bcc     @go
@range: jmp     range
@go:    sta     mvoice
        jsr     started
        bcs     @out
        lda     mvoice
        ora     #$60
        jsr     send
        bcs     @out
        lda     mmark
        jsr     send
        bcs     @out
        jmp     ok
@out:   rts

; A = an EK_SOUND event's byte (its EQ_VALUE): A = what it was (MV_*), X =
; the voice, Y = a marker's number. MV_NONE for a byte the library lets go
; (STOP's leftovers) or doesn't know. Never fails.
MU_EVENT:
        cmp     #$01
        bne     @kind
        lda     #MV_HELLO
        ldx     #$FF
        bra     @ret
@kind:  cmp     #$80
        bcs     @mark
        pha
        and     #$0F
        tax
        pla
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        cpx     #MU_VOICES
        bcs     @none                   ; $37: a command refused, no voice's
        cmp     #1
        beq     @done
        cmp     #2
        beq     @empty
        cmp     #3
        beq     @refused
@none:  lda     #MV_NONE
@ret:   ldy     #0
        clc
        rts
@done:  lda     MUDRAIN,x
        bne     @none
        jsr     down
        lda     #MV_DONE
        bra     @ret
@empty: lda     MUDRAIN,x
        bne     @none
        lda     #MV_EMPTY
        bra     @ret
@refused:
        lda     MUDRAIN,x
        bne     @none
        jsr     down
        lda     #MV_REFUSED
        bra     @ret
@mark:  pha                             ; $80 + voice x 16 + the marker
        and     #$0F
        tay
        pla
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        and     #$07
        tax
        cpx     #MU_VOICES
        bcs     @none
        lda     MUDRAIN,x
        beq     @user
        cpy     #MU_MARKS               ; draining: only STOP's own ends it
        bne     @none
        stz     MUDRAIN,x
        bra     @none
@user:  jsr     down
        lda     #MV_MARK
        clc
        rts

down:   lda     MUCNT,x
        beq     @z
        dec     MUCNT,x
@z:     rts

; A = a voice: A = its entries waiting (PLAY(n)).
MU_COUNT:
        cmp     #MU_VOICES
        bcs     range
        tax
        lda     MUCNT,x
        clc
        rts

; Carry clear and X = mvoice = A if A is a voice and the driver is started;
; else carry set and A the error.
voice:  sta     mvoice
        jsr     started
        bcs     @out
        lda     mvoice
        cmp     #MU_VOICES
        bcs     range
        tax
        clc
@out:   rts

; Carry clear if MU_INIT has started the driver (A kept, X changed); else
; carry set and A = PE_STATE.
started:
        ldx     MUON
        beq     nostate
        clc
        rts

ok:     lda     #0
        clc
        rts
range:  lda     #PE_RANGE
        sec
        rts
nostate:
        lda     #PE_STATE
        sec
        rts
refused:
        lda     #PE_REFUSED
        sec
        rts

; A byte to the driver by SN_SEND, again while the card hasn't taken the last
; (SN_BUSY), for up to SEND_MS by the millisecond counter: a running driver
; takes a byte within microseconds, and its start is about 3 ms. Carry set
; and PE_REFUSED if refused, or never taken.
SEND_MS = 50

send:   sta     mblk + 1
        lda     #SN_SEND
        sta     mblk
        jsr     f82blk
        beq     @sent
        cmp     #SN_BUSY
        bne     refused
        jsr     msnow                   ; busy: from now, SEND_MS
        sta     tries
        stx     tries + 1
@try:   jsr     f82blk
        beq     @sent
        cmp     #SN_BUSY
        bne     refused
        jsr     msnow                   ; now - then, 16 bits
        sec
        sbc     tries
        tay
        txa
        sbc     tries + 1
        bne     refused                 ; 256 ms or more
        cpy     #SEND_MS
        bcc     @try
        bra     refused
@sent:  clc
        rts

; A/X = the millisecond counter's low word (function 80's FT_COUNT).
msnow:  lda     #FT_COUNT
        ldx     #0
        ldy     #O_FRAME
        jmp     BDOS

; Function 82 with mblk (f82blk) or with operation A and no block (f82op):
; Z set if done (A = 0).
f82blk: lda     #<mblk
        ldx     #>mblk
        bra     f82
f82op:  ldx     #0
f82:    ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        rts

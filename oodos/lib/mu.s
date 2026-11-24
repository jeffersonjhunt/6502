; OOLIB's music (include/oolib.inc, "Sound: music"; hw/oolib.md, L6b), on
; sound card revision 3: the card's music sequencer plays note blocks (the
; Technical Reference, chapter 7). MU_INIT claims music (SC_MUSIC) and loads
; nothing. MU_NOTE (munote.s), MU_TONE (mutone.s) and MU_MARK send an entry
; at once, as a block of one (the owner's answer 9); MU_BLOCK sends a block
; the program built. Each goes by function 82's SN_MUSIC into the next of the
; card's music slots in turn, queued behind what plays, freed when done; with
; all 32 playing or queued, PE_FULL. The counts are the card's
; (MU_COUNT: SN_MSTATUS), and its events come to the program's queue as
; EK_SOUND, which MU_EVENT names.
;
; A logical voice (0-5) keeps its instrument (MU_INSTR: a SID voice's
; waveform, ADSR and pulse width) and volume (MU_VOLUME) for its next
; entries; the card gives each block's voices a free SID voice when it starts.
; A marker (MU_MARK) is sent as the card's marker byte voice x 16 + the
; marker, so MU_EVENT can say both.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "mu.inc"
        .include "physof.inc"

        .data
MUON:   .byte   0                       ; 1 once MU_INIT has claimed music (in DATA: a .COM's BSS
                                        ;   isn't cleared, so a program run again would find the last run's)
MUSLOT: .byte   MU_SLOTS - 1            ; the slot the last block went to

        .bss
MUCLK:  .res    1                       ; 0 PAL, 1 NTSC (kept: the card's music is in µs and Hz)
MUINS:  .res    MU_VOICES * 5           ; each voice's instrument: waveform bits, AD, SR, the pulse width
MUVOL:  .res    MU_VOICES               ; each voice's volume, 0-255
MUE:    .res    12                      ; an entry for MUQ
MUELEN: .res    1
MUT:    .res    4                       ; MUTIME's µs
mt8:    .res    4                       ; and its x 8, x 16
mt16:   .res    4
mlen:   .res    2                       ; MUTIME's length and gate, ms
mgate:  .res    2
nblk:   .res    8 + 8 + 12              ; a block of one: the header, an instrument, the entry
blen:   .res    2                       ; SNDBLK's length
mslot:  .res    1
mblk:   .res    9                       ; function 82's block (SN_MUSIC, SN_MCTL, SN_CLAIM)
sblk:   .res    25                      ; SN_MSTATUS's
mvoice: .res    1
mmark:  .res    1

        .code

; A = the clock: 0 PAL, 1 NTSC (kept: nothing on the card depends on it).
; PE_RANGE past 1; PE_STATE if started already; PE_REFUSED if music can't be
; claimed (another program's, or no card OODOS drives). Every voice a 50%
; pulse ($40, $00, $F0, $0800) at full volume.
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
        lda     #SC_MUSIC
        sta     mblk + 1
        jsr     f82blk
        beq     @init
        jmp     refused
@init:  ldx     #MU_VOICES * 5 - 1
@i:     lda     defins,x
        sta     MUINS,x
        dex
        bpl     @i
        ldx     #MU_VOICES - 1
@v:     lda     #255
        sta     MUVOL,x
        dex
        bpl     @v
        lda     #1
        sta     MUON
        jmp     ok

        .rodata
defins: .repeat MU_VOICES
        .byte   $40, $00, $F0, $00, $08
        .endrepeat
        .code

; Music stopped and released (SN_RELEASE stops it). PE_STATE if not started.
MU_QUIT:
        lda     MUON
        bne     @on
        jmp     nostate
@on:    stz     MUON
        lda     #SN_RELEASE
        sta     mblk
        lda     #SC_MUSIC
        sta     mblk + 1
        jsr     f82blk
        jmp     ok

; MUQ (munote.s's, mutone.s's, MU_MARK's): voice A's entry MUE (MUELEN bytes)
; as a block of one: the header, the voice's instrument, the entry; then
; SNDBLK. PE_RANGE for a voice past 5.
MUQ:    jsr     voice
        bcs     @out
        lda     MUE                     ; the voice into the entry's low nibble
        and     #$F0
        ora     mvoice
        sta     MUE
        lda     #1                      ; the header: version 1, the voice's bit, one entry
        sta     nblk
        sta     nblk + 4
        stz     nblk + 5
        stz     nblk + 6
        stz     nblk + 7
        stz     nblk + 2
        stz     nblk + 3
        lda     #0
        sec
@bit:   rol     a
        dex
        bpl     @bit
        sta     nblk + 1
        stz     nblk + 8                ; the instrument: a SID voice,
        lda     mvoice                  ;   its five bytes,
        asl     a
        asl     a
        adc     mvoice                  ; (5 x: carry clear)
        tax
        ldy     #0
@ins:   lda     MUINS,x
        sta     nblk + 9,y
        inx
        iny
        cpy     #5
        bne     @ins
        ldx     mvoice                  ;   its volume, the pan unused (a SID's place)
        lda     MUVOL,x
        sta     nblk + 14
        lda     #128
        sta     nblk + 15
        ldx     #0
@e:     lda     MUE,x
        sta     nblk + 16,x
        inx
        cpx     MUELEN
        bne     @e
        txa
        clc
        adc     #16
        sta     blen
        stz     blen + 1
        lda     #<nblk
        ldx     #>nblk
        jmp     SNDBLK
@out:   rts

; A/X a block of MU_BLEN bytes: +0-1 a note block's address (the card's
; layout, in one run of the program's memory), +2-3 its length: sent and
; queued behind what plays. PE_FULL with all 32 slots in use; PE_REFUSED
; if the card refused it (a block that doesn't parse: E_DATA).
MU_BLOCK:
        sta     MUP
        stx     MUP + 1
        jsr     started
        bcc     :+
        jmp     mout
:
        ldy     #2
        lda     (MUP),y
        sta     blen
        iny
        lda     (MUP),y
        sta     blen + 1
        ldy     #1
        lda     (MUP),y
        tax
        lda     (MUP)
        ; fall through
; A/X the block (a CPU address), blen its length: into the next slot, as
; MUSIC_PLAY's "queued, freed when done". The slots go in turn, so the next
; is free unless all are playing or queued (SN_MSTATUS).
SNDBLK: jsr     PHYSOF
        jsr     mstatus
        bcc     :+
        jmp     refused
:
        lda     sblk + 1                ; stopped: every slot free
        beq     @free
        lda     sblk + 2                ; the blocks queued, and the one playing
        cmp     #MU_SLOTS - 1
        bcc     @free
        lda     #PE_FULL
        sec
        rts
@free:  lda     MUSLOT
        inc     a
        and     #MU_SLOTS - 1
        sta     mslot
        lda     #SN_MUSIC
        sta     mblk
        lda     mslot
        sta     mblk + 1
        ldx     #2
@p:     lda     PHYS,x
        sta     mblk + 2,x
        dex
        bpl     @p
        lda     blen
        sta     mblk + 5
        lda     blen + 1
        sta     mblk + 6
        stz     mblk + 7
        lda     #3                      ; queued, freed when done
        sta     mblk + 8
        jsr     f82blk
        beq     :+
        jmp     refused
:
        lda     mslot
        sta     MUSLOT
        jmp     ok
.assert MU_SLOTS = 32, error, "SNDBLK's slots: AND #31, the card's MUSIC_SLOTS"

; A/X a block: +0 the voice, +1 the control register's bits (waveform,
; ring, sync), +2 attack/decay, +3 sustain/release, +4-5 the pulse width:
; the voice's instrument from its next entry on.
MU_INSTR:
        sta     MUP
        stx     MUP + 1
        lda     (MUP)
        jsr     voice
        bcc     :+
        jmp     mout
:
        txa
        asl     a
        asl     a
        adc     mvoice
        tax
        ldy     #1
        lda     (MUP),y                 ; the control bits, the gate and TEST left out (the entries' and none)
        and     #$F6
        bra     @s
@b:     lda     (MUP),y
@s:     sta     MUINS,x
        inx
        iny
        cpy     #6
        bne     @b
        jmp     ok

; A = a voice, or MU_ALL: music stopped (the card has one queue: every
; voice's), its blocks dropped.
MU_STOP:
        jsr     started
        bcc     :+
        jmp     mout
:
        cmp     #MU_ALL
        beq     @all
        jsr     voice
        bcc     :+
        jmp     mout
:
@all:   lda     #0
        bra     mctl

MU_PAUSE:
        lda     #1
        bra     mctl0
MU_RESUME:
        lda     #2
mctl0:  pha
        jsr     started
        pla
        bcc     :+
        jmp     mout
:
; SN_MCTL's A (0 stop, 1 pause, 2 resume, 3 the threshold with mmark).
mctl:   sta     mblk + 1
        lda     mmark
        sta     mblk + 2
        lda     #SN_MCTL
        sta     mblk
        jsr     f82blk
        beq     :+
        jmp     refused
:
        jmp     ok
mout:   rts

; A = n: a MUSIC_COUNT event (MU_EVENT's MV_COUNT) when the notes waiting, on
; every voice, fall from n to n - 1 (0: none).
MU_THRESH:
        sta     mmark
        jsr     started
        bcc     :+
        jmp     mout
:
        lda     #3
        bra     mctl

; A = a voice, X = a marker, 0-14: MU_EVENT says MV_MARK when the voice
; reaches it. PE_RANGE for 15 up.
MU_MARK:
        cpx     #15
        bcc     @m
        jmp     range
@m:     stx     mmark
        sta     mvoice
        asl     a                       ; the card's marker: voice x 16 + the marker
        asl     a
        asl     a
        asl     a
        ora     mmark
        sta     MUE + 1
        lda     #$50
        sta     MUE
        lda     #2
        sta     MUELEN
        lda     mvoice
        jmp     MUQ

; A = a SID (0 A, 1 B, MU_ALL both), X = its volume, 0-15: the volume of the
; voices that name it (0-2, 3-5) from their next entry on.
MU_VOLUME:
        cpx     #16
        bcc     :+
        jmp     range
:
        cmp     #MU_ALL
        beq     @ok
        cmp     #2
        bcc     :+
        jmp     range
:
@ok:    sta     mvoice                  ; the SID asked for
        txa
        sta     mmark                   ; x 17: 0-255
        asl     a
        asl     a
        asl     a
        asl     a
        ora     mmark
        sta     mmark
        jsr     started
        bcc     :+
        jmp     mout
:
        ldx     #MU_VOICES - 1
@v:     lda     mvoice
        cmp     #MU_ALL
        beq     @set
        cpx     #3                      ; this voice's SID: carry set for B
        lda     #0
        rol     a
        cmp     mvoice
        bne     @next
@set:   lda     mmark
        sta     MUVOL,x
@next:  dex
        bpl     @v
        jmp     ok

; A = an EK_SOUND event's code (EQ_CODE: the card's event kind), X its value:
; A = what it was (MV_*): MV_DONE when music's last queued block has ended (X
; the block's slot), MV_MARK when a voice reached a marker (X the voice, Y the
; marker), MV_COUNT when the notes waiting fell below MU_THRESH's n; MV_NONE
; for any other. Carry clear.
MU_EVENT:
        ldy     #0
        cmp     #SK_MUSIC_DONE
        bne     @mark
        lda     #MV_DONE
        clc
        rts
@mark:  cmp     #SK_MUSIC_MARK
        bne     @count
        txa
        and     #$0F
        tay
        txa
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        tax
        lda     #MV_MARK
        clc
        rts
@count: cmp     #SK_MUSIC_COUNT
        bne     @none
        lda     #MV_COUNT
        clc
        rts
@none:  lda     #MV_NONE
        clc
        rts

; A = a voice: A = its notes waiting, not yet begun (the card's count, held at
; 255), X = music's state (0 stopped, 1 playing, 2 paused). Carry clear (no
; card: 0, 0).
MU_COUNT:
        cmp     #MU_VOICES
        bcc     :+
        jmp     range
:
        sta     mvoice
        jsr     mstatus
        bcc     @have
        lda     #0
        tax
        clc
        rts
@have:  lda     mvoice
        asl     a
        tax
        lda     sblk + 4,x              ; its count's high byte: held
        beq     @lo
        lda     #$FF
        bra     @ret
@lo:    lda     sblk + 3,x
@ret:   ldx     sblk + 1
        clc
        rts

; SN_MSTATUS into sblk: carry set if refused.
mstatus:
        lda     #SN_MSTATUS
        sta     sblk
        lda     #<sblk
        ldx     #>sblk
        jsr     f82
        cmp     #1                      ; carry: not done
        rts

; The block's length (+3-4, 0 is 1) and gate (+5-6, at most the length; 0
; stays 0), ms, at MUE + X as µs, 4 bytes each; carry clear: the length alone.
MUTIME:
        php
        ldy     #3
        lda     (MUP),y
        sta     mlen
        iny
        lda     (MUP),y
        sta     mlen + 1
        ora     mlen
        bne     @g
        inc     mlen
@g:     iny
        lda     (MUP),y
        sta     mgate
        iny
        lda     (MUP),y
        sta     mgate + 1
        lda     mlen
        ldy     mlen + 1
        jsr     msus
        plp
        bcc     @done
        lda     mgate + 1               ; the gate past the length: the length
        cmp     mlen + 1
        bcc     @gate
        bne     @len
        lda     mgate
        cmp     mlen
        bcc     @gate
@len:   lda     mlen
        sta     mgate
        lda     mlen + 1
        sta     mgate + 1
@gate:  lda     mgate
        ldy     mgate + 1
        jsr     msus
@done:  rts

; A/Y ms x 1,000 into MUE + X, 4 bytes (x 1,024 - x 16 - x 8); X moved on.
msus:   sta     MUT
        sty     MUT + 1
        stz     MUT + 2
        stz     MUT + 3
        jsr     @sh
        jsr     @sh
        jsr     @sh                     ; x 8
        ldy     #3
@c8:    lda     MUT,y
        sta     mt8,y
        dey
        bpl     @c8
        jsr     @sh                     ; x 16
        ldy     #3
@c16:   lda     MUT,y
        sta     mt16,y
        dey
        bpl     @c16
        ldy     #6                      ; x 1,024
@k:     jsr     @sh
        dey
        bne     @k
        ldy     #0
        sec
@s16:   lda     MUT,y
        sbc     mt16,y
        sta     MUT,y
        iny
        tya
        eor     #4
        bne     @s16
        ldy     #0
        sec
@s8:    lda     MUT,y
        sbc     mt8,y
        sta     MUE,x
        inx
        iny
        tya
        eor     #4
        bne     @s8
        rts
@sh:    asl     MUT
        rol     MUT + 1
        rol     MUT + 2
        rol     MUT + 3
        rts

; Carry clear and X = mvoice = A if A is a voice and music is claimed; else
; carry set and A the error.
voice:  sta     mvoice
        jsr     started
        bcs     @out
        lda     mvoice
        cmp     #MU_VOICES
        bcc     :+
        jmp     range
:
        tax
        clc
@out:   rts

; Carry clear if MU_INIT has claimed music (A kept, X changed); else carry
; set and A = PE_STATE.
started:
        ldx     MUON
        bne     :+
        jmp     nostate
:
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

; Function 82 with mblk (f82blk), or A/X a block (f82): Z set if done (A = 0).
f82blk: lda     #<mblk
        ldx     #>mblk
f82:    ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        rts

; OOLIB's music driver (hw/oolib.md, "The plan", L6): a program of ours for
; the sound card's 6510, which plays notes on the two SIDs' six voices from
; commands the host sends through the mailbox, and sends back what it did.
; The host never writes a SID while it runs. L6b's library (lib/mu.s) loads
; it at $1000 by function 82 and sends the commands; a program can send
; them itself with SN_SEND.
;
; The image (mudrv.cfg: $1000 in the card's RAM; the reset vector at $FFFC
; points at $1000):
;   $1000-$1002  JMP init
;   $1003-$1004  LATCH: CIA 1 timer A's latch for a 1 ms step, set by the
;                loader before it starts the driver: 984 on the PAL clock
;                (985,248 Hz: 985 clocks a step), 1,022 on NTSC (1,022,727
;                Hz: 1,023). The default is PAL's.
;   $1005        VERSION: 1
;
; Every millisecond the timer's interrupt steps the six voices (voice 0-2
; SID A's, at $D400, $D407, $D40E; 3-5 SID B's, $D420, $D427, $D42E). Each
; voice has a queue of 32 entries. A note plays for its length in
; milliseconds, its gate on for its gate time (PLAY's articulation: the
; whole length legato, 7/8 normal, 3/4 staccato), then the next entry starts
; in the same step, so notes follow each other exactly. A note of frequency 0
; is a rest. A marker takes no time: its byte goes back when the voice
; reaches it.
;
; Commands, host to card (the high nibble the command, the low a voice
; 0-5, then the operands):
;   $0v f.lo f.hi l.lo l.hi g.lo g.hi  NOTE: the SID frequency word, the
;          length in ms (0 is taken as 1), the gate time in ms (0: none)
;   $1v w ad sr p.lo p.hi              INSTR: the control register's bits
;          (waveform, ring, sync; the gate bit is ignored), attack/decay,
;          sustain/release, pulse width; from the voice's next note on.
;          At start every voice is a 50% pulse, AD $00, SR $F0
;   $2v    STOP: the voice's queue emptied and its gate off; $27 all six
;   $3v m  MARK: m (0-15) back when the voice reaches it
;   $40    PAUSE: the steps held and both SIDs' volume 0
;   $50    RESUME
;   $6s n  VOLUME: SID s's volume nibble (s 0 A, 1 B, 7 both), 15 at start
;   $70    HELLO: answered $01 (the version)
; Anything else, or a voice past 5, is refused ($37). A command's operands
; are taken as they come, so a byte lost on the way puts the rest out of
; step: the host sends only while STATUS says the card has taken the last.
;
; Events, card to host (one byte each, sent only once the host has taken the
; one before: $DE01 bit 1; 64 wait here meanwhile, and past that they are
; counted in LOST):
;   $01        HELLO's answer
;   $1v        an entry (a note or a rest) on voice v finished
;   $2v        voice v's queue is empty: after its last entry, and after STOP
;   $3v        a NOTE or MARK refused: voice v's queue had 32 ($37: a
;              command refused)
;   $80 + v x 16 + m  voice v reached marker m
; So a voice's entries waiting are those sent less its $1v and markers; $2v
; says 0.
;
; What it costs the 6510 (test_mudrv measures it): with six voices holding
; notes, the step takes about 18% of each millisecond, 134 clocks; a voice
; whose note ends and the next starts, about 180 more. Six starting in one
; step take about 1,100 clocks, past the millisecond, so the next step runs
; late but none is lost. The main loop has the rest, taking commands and
; sending events; a load that keeps the step busy (several voices with notes
; of a few milliseconds) sends events faster than they go, and the 65th
; waiting is dropped. PLAY's shortest note is about 15 ms.

        .setcpu "6502"

SID      = $D400
SID_B    = $D420
CIA_TAL  = $DC04
CIA_TAH  = $DC05
CIA_ICR  = $DC0D
CIA_CRA  = $DC0E
MAILBOX  = $DE00
MSTATUS  = $DE01            ; bit 0 a byte from the host, bit 1 ours not taken

VOICES   = 6
QLEN     = 32               ; entries a voice
EVLEN    = 64               ; events waiting

; The queues, a voice's 32 entries at voice x 32 (a byte index for all
; 192). The main loop works out each entry's two waits when it queues it,
; so the step only counts: the first, from the start to the gate's end (or
; the entry's), and the second, from the gate's end to the entry's (0 if
; none). Each is kept as its negative, counted up to 0.
QFL      = $0400            ; the frequency (a marker's: its byte)
QFH      = QFL + 192
QAL      = QFH + 192        ; the first wait, negated
QAH      = QAL + 192
QBL      = QAH + 192        ; the second, negated (0: none)
QBH      = QBL + 192
QK       = QBH + 192        ; bit 0 a marker, bit 1 the gate on at the start
EVQ      = $0300            ; the events waiting

K_MARK   = 1
K_GATE   = 2

        .zeropage                   ; $10 on (mudrv.cfg)
ACT:    .res    VOICES      ; 1 while an entry plays
WL:     .res    VOICES      ; the wait under way, negated
WH:     .res    VOICES
W2L:    .res    VOICES      ; and the one after it (0: none)
W2H:    .res    VOICES
HEADI:  .res    VOICES      ; the next entry's slot: voice x 32 + 0-31
CNT:    .res    VOICES      ; entries waiting
WAVE:   .res    VOICES      ; the instrument, for the next note
AD:     .res    VOICES
SR:     .res    VOICES
PWL:    .res    VOICES
PWH:    .res    VOICES
CWAVE:  .res    VOICES      ; the playing note's control bits, gate clear
EVH:    .res    1           ; the next event to send
EVN:    .res    1           ; events waiting
LOST:   .res    1           ; events dropped, EVQ full
PAUSED: .res    1
VOLA:   .res    1
VOLB:   .res    1
TICKS:  .res    3           ; milliseconds stepped since the start
NEED:   .res    1           ; the command's operands still to come
PIX:    .res    1           ; the next operand's place in ARG
OP:     .res    1           ; the command byte
ARG:    .res    6
TMP:    .res    1           ; the main loop's own: the step never writes it
LEN:    .res    2           ; NOTE's length (at least 1) and gate (at most it)
GATE:   .res    2
NEW:    .res    VOICES      ; 1: the voice's instrument changed since its last note
PY:     .res    1           ; IPUSH's Y

        .code
        jmp     init
LATCH:  .word   985 - 1
VERSION:.byte   1

SIDOFF: .byte   0, 7, 14, SID_B - SID, SID_B - SID + 7, SID_B - SID + 14
QBASE:  .byte   0, 32, 64, 96, 128, 160
NEEDS:  .byte   6, 5, 0, 1, 0, 0, 1, 0      ; each command's operands

init:   sei
        cld
        ldx     #$FF
        txs
        lda     #$2F            ; the port as the KERNAL leaves it: I/O in
        sta     $00
        lda     #$37
        sta     $01
        lda     #<irq
        sta     $FFFE
        lda     #>irq
        sta     $FFFF
        lda     #<nmi
        sta     $FFFA
        lda     #>nmi
        sta     $FFFB

        lda     #0              ; our zero page, and both SIDs silent
        ldx     #$10
@zp:    sta     $00,x
        inx
        bne     @zp
        ldx     #$18
@sid:   sta     SID,x
        sta     SID_B,x
        dex
        bpl     @sid

        ldx     #VOICES - 1     ; every voice a 50% pulse, as a speaker
@inst:  lda     #$40
        sta     WAVE,x
        sta     CWAVE,x
        lda     #$F0
        sta     SR,x
        lda     #$08
        sta     PWH,x
        lda     #1
        sta     NEW,x
        lda     QBASE,x
        sta     HEADI,x
        dex
        bpl     @inst
        lda     #15
        sta     VOLA
        sta     VOLB
        jsr     vols

        lda     LATCH           ; timer A a millisecond, its interrupt on
        sta     CIA_TAL
        lda     LATCH + 1
        sta     CIA_TAH
        lda     #$81
        sta     CIA_ICR
        lda     #$11            ; started, the latch loaded
        sta     CIA_CRA
        cli

; The main loop: the host's bytes taken as they come, events sent as the
; host takes them.
main:   lda     MSTATUS
        lsr     a
        bcc     @send
        lda     MAILBOX
        jsr     byte
@send:  lda     EVN
        beq     main
        lda     MSTATUS
        and     #2
        bne     main            ; the last not taken yet
        sei
        ldx     EVH
        lda     EVQ,x
        sta     MAILBOX
        inx
        txa
        and     #EVLEN - 1
        sta     EVH
        dec     EVN
        cli
        jmp     main

; A byte from the host: a command, or the next of its operands.
byte:   ldx     NEED
        bne     @arg
        sta     OP
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        cmp     #8
        bcs     refuse
        tax
        lda     NEEDS,x
        beq     exec
        sta     NEED
        lda     #0
        sta     PIX
        rts
@arg:   ldx     PIX
        sta     ARG,x
        inc     PIX
        dec     NEED
        beq     exec
        rts

refuse: lda     #$37
        jmp     push

; Run the command OP, its operands in ARG.
exec:   lda     OP
        and     #$0F
        tax                     ; X = the voice (or SID)
        lda     OP
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        tay
        cpy     #2
        bne     @nstop
        jmp     cstop           ; STOP takes 7
@nstop: cpy     #4
        bcs     @any            ; PAUSE, RESUME, VOLUME, HELLO: their own
        cpx     #VOICES
        bcs     refuse
@any:   lda     JTH,y           ; to the command's routine - 1, by RTS
        pha
        lda     JTL,y
        pha
        rts

JTL:    .lobytes cnote - 1, cinstr - 1, cstop - 1, cmark - 1, cpause - 1, cresume - 1, cvol - 1, chello - 1
JTH:    .hibytes cnote - 1, cinstr - 1, cstop - 1, cmark - 1, cpause - 1, cresume - 1, cvol - 1, chello - 1

; NOTE: the two waits worked out here, not in the step. LEN = the length,
; at least 1; GATE = the gate time, at most LEN, 0 for a rest. Then the
; first wait is GATE (or LEN with no gate), the second LEN - GATE.
cnote:  lda     ARG + 2
        sta     LEN
        lda     ARG + 3
        sta     LEN + 1
        ora     LEN
        bne     @len
        inc     LEN             ; 0 is 1
@len:   lda     ARG + 4
        sta     GATE
        lda     ARG + 5
        sta     GATE + 1
        lda     ARG             ; a rest has no gate
        ora     ARG + 1
        bne     @tone
        sta     GATE
        sta     GATE + 1
@tone:  lda     LEN             ; GATE at most LEN
        cmp     GATE
        lda     LEN + 1
        sbc     GATE + 1
        bcs     @fits
        lda     LEN
        sta     GATE
        lda     LEN + 1
        sta     GATE + 1
@fits:  lda     GATE
        ora     GATE + 1
        bne     @gated
        lda     LEN             ; no gate: one wait, LEN
        sta     ARG + 2
        lda     LEN + 1
        sta     ARG + 3
        lda     #0
        sta     ARG + 4
        sta     ARG + 5
        sta     TMP
        jmp     entry
@gated: sec                     ; the second: LEN - GATE
        lda     LEN
        sbc     GATE
        sta     ARG + 4
        lda     LEN + 1
        sbc     GATE + 1
        sta     ARG + 5
        lda     GATE
        sta     ARG + 2
        lda     GATE + 1
        sta     ARG + 3
        lda     #K_GATE
        sta     TMP
        jmp     entry

cmark:  lda     ARG
        and     #$0F
        sta     ARG
        lda     #0
        sta     ARG + 2
        sta     ARG + 3
        sta     ARG + 4
        sta     ARG + 5
        lda     #K_MARK
        sta     TMP

; The entry (ARG: the frequency, the first wait, the second; TMP the kind) at
; the voice's queue's end, the waits negated, or refused. Under SEI from
; reading CNT to raising it: a step taking an entry between would put this
; one a slot too far.
entry:  sec
        lda     #0
        sbc     ARG + 2
        sta     ARG + 2
        lda     #0
        sbc     ARG + 3
        sta     ARG + 3
        sec
        lda     #0
        sbc     ARG + 4
        sta     ARG + 4
        lda     #0
        sbc     ARG + 5
        sta     ARG + 5
        sei
        lda     CNT,x
        cmp     #QLEN
        bcc     @room
        cli
        txa
        ora     #$30
        jmp     push
@room:  clc
        adc     HEADI,x
        and     #QLEN - 1
        ora     QBASE,x
        tay
        lda     ARG
        sta     QFL,y
        lda     ARG + 1
        sta     QFH,y
        lda     ARG + 2
        sta     QAL,y
        lda     ARG + 3
        sta     QAH,y
        lda     ARG + 4
        sta     QBL,y
        lda     ARG + 5
        sta     QBH,y
        lda     TMP
        sta     QK,y
        inc     CNT,x           ; last: the step sees the entry whole or not at all
        cli
        rts

cinstr: sei                     ; whole: a note starting now takes the old or the new
        lda     ARG
        and     #$FE
        sta     WAVE,x
        lda     ARG + 1
        sta     AD,x
        lda     ARG + 2
        sta     SR,x
        lda     ARG + 3
        sta     PWL,x
        lda     ARG + 4
        sta     PWH,x
        lda     #1
        sta     NEW,x
        cli
        rts

cstop:  cpx     #7
        beq     @all
        cpx     #VOICES
        bcc     stop1
        jmp     refuse
@all:   ldx     #VOICES - 1
@each:  jsr     stop1
        dex
        bpl     @each
        rts

; Voice X stopped: nothing waiting, its gate off, and said ($2v).
stop1:  sei
        lda     #0
        sta     CNT,x
        sta     ACT,x
        ldy     SIDOFF,x
        lda     CWAVE,x
        sta     SID + 4,y
        cli
        txa
        ora     #$20
        jmp     push

cpause: lda     #1
        sta     PAUSED
        jmp     vols
cresume:lda     #0
        sta     PAUSED
        jmp     vols

cvol:   lda     ARG
        and     #$0F
        cpx     #1
        beq     @b
        bcs     @both
        sta     VOLA
        jmp     vols
@both:  cpx     #7
        bne     @bad
        sta     VOLA
@b:     sta     VOLB
        jmp     vols
@bad:   jmp     refuse

chello: lda     #$01
        jmp     push

; Both SIDs' $18: their volumes, or 0 while paused.
vols:   lda     PAUSED
        bne     @zero
        lda     VOLA
        sta     SID + $18
        lda     VOLB
        sta     SID_B + $18
        rts
@zero:  lda     #0
        sta     SID + $18
        sta     SID_B + $18
        rts

; An event (A) to send, from the main loop; X and Y kept.
push:   php
        sei
        jsr     ipush
        plp
        rts

; The same with interrupts already off (the step's); X and Y kept. With EVQ
; full it is counted in LOST.
ipush:  sty     PY
        ldy     EVN
        cpy     #EVLEN
        bcs     @lost
        pha
        tya
        clc
        adc     EVH
        and     #EVLEN - 1
        tay
        pla
        sta     EVQ,y
        inc     EVN
        ldy     PY
        rts
@lost:  inc     LOST
        ldy     PY
        rts

; The millisecond: each voice stepped, unless paused. A voice holding a note
; costs an INC and a branch; one whose wait is over, its gate dropped and,
; at the entry's end, the next taken (next).
irq:    pha
        txa
        pha
        tya
        pha
        lda     CIA_ICR         ; taken
        inc     TICKS
        bne     @t
        inc     TICKS + 1
        bne     @t
        inc     TICKS + 2
@t:     lda     PAUSED
        bne     @out
        ldx     #VOICES - 1
@v:     lda     ACT,x
        beq     @idle
        inc     WL,x
        bne     @nv
        inc     WH,x
        bne     @nv
        ldy     SIDOFF,x        ; a wait over: the gate off
        lda     CWAVE,x
        sta     SID + 4,y
        lda     W2L,x
        ora     W2H,x
        beq     @end
        lda     W2L,x           ; the gate's end: the rest of the length
        sta     WL,x
        lda     W2H,x
        sta     WH,x
        lda     #0
        sta     W2L,x
        sta     W2H,x
        beq     @nv
@end:   sta     ACT,x           ; the entry's end (A = 0), said
        txa
        ora     #$10
        jsr     ipush
        lda     CNT,x
        bne     @take
        txa
        ora     #$20            ; and nothing more
        jsr     ipush
        jmp     @nv
@idle:  lda     CNT,x
        beq     @nv
@take:  jsr     next
@nv:    dex
        bpl     @v
@out:   pla
        tay
        pla
        tax
        pla
nmi:    rti

; Voice X's next entry (CNT not 0): a marker said and the next taken, a note
; or a rest started.
next:   ldy     HEADI,x
        tya
        clc
        adc     #1
        and     #QLEN - 1
        ora     QBASE,x
        sta     HEADI,x
        dec     CNT,x
        lda     QK,y
        lsr     a
        bcc     @note
        txa                     ; a marker: $80 + voice x 16 + its byte
        asl     a
        asl     a
        asl     a
        asl     a
        ora     QFL,y
        ora     #$80
        jsr     ipush
        lda     CNT,x
        bne     next
        txa
        ora     #$20
        jmp     ipush
@note:  lda     QAL,y
        sta     WL,x
        lda     QAH,y
        sta     WH,x
        lda     QBL,y
        sta     W2L,x
        lda     QBH,y
        sta     W2H,x
        lda     #1
        sta     ACT,x
        lda     WAVE,x
        sta     CWAVE,x
        lda     QK,y
        and     #K_GATE
        beq     @rest           ; a rest, or a note with no gate: no sound
        lda     QFL,y
        pha
        lda     QFH,y
        ldy     SIDOFF,x
        sta     SID + 1,y
        pla
        sta     SID + 0,y
        lda     NEW,x
        beq     @gate
        lda     #0              ; the instrument changed: its registers too
        sta     NEW,x
        lda     PWL,x
        sta     SID + 2,y
        lda     PWH,x
        sta     SID + 3,y
        lda     AD,x
        sta     SID + 5,y
        lda     SR,x
        sta     SID + 6,y
@gate:  lda     CWAVE,x
        ora     #1
        sta     SID + 4,y
        rts
@rest:  ldy     SIDOFF,x
        lda     CWAVE,x
        sta     SID + 4,y
        rts

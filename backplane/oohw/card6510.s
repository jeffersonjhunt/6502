; The diagnostics' program for the sound card's 6510 (hw/diag.md, "The 6510
; test program"): the card's own code, NMOS 6502 opcodes only, assembled on
; its own (card6510.cfg: $0200-$03FF in the card's RAM, its zero page
; $02-$1F) and taken whole into the ROM's diagnostics' block 22, which loads
; it through RAMADDR and RAMDATA, writes its reset vector and starts it
; (stage 10, S4). Not a module of oohw.lib. D2's RAM test (C65_TEST, a march
; at the 6510's 1 MHz) is gone: in the ROM the host tests all 64 KB itself
; through RAMDATA, faster (S2), so the 6510 has no use for it.
;
; It starts by reading the mailbox from the host once (a byte a program
; left there, which only a /RESET empties) and sending $A5. Then it answers
; commands, each a byte from the host and its operands, each answered:
;
;   C65_ECHO   b                 b sent back
;   C65_SUM    a.lo a.hi n.lo n.hi
;                                the 16-bit sum of n bytes from a (n 0:
;                                65,536): its low byte, then its high
;   C65_SID    s                 SID s (0 A at $D400, 1 B at $D420): its
;                                voice 3 set running (sawtooth, gate on,
;                                attack 2 ms) with the volume 0 and voice 3
;                                cut off from the output, OSC3 and ENV3 read
;                                as it runs; answered by a byte: bit 0 OSC3
;                                changed, bit 1 ENV3 rose; voice 3 stopped
;   C65_TONE   s on              SID s's voice 1: a 440 Hz triangle, gate
;                                on and the volume 15 (on 1), or off and the
;                                volume 0 (on 0); answered by C65_TONE
;   anything else                answered by $FF
;
; It never writes $0000-$01FF but its zero page bytes and the stack, and
; never $0200-$03FF (itself), nor its vectors. A byte to the host is sent once it has taken the last
; ($DE01 bit 1 clear); one from it is waited for ($DE01 bit 0).

        .setcpu "6502"
        .include "oohw.inc"

MBOX    = $DE00                         ; the mailbox: read from the host, write to it
MBST    = $DE01                         ; bit 0 a byte from the host waits, bit 1 ours not taken yet
PORTDDR = $00
PORT    = $01
SIDA    = $D400
SIDB    = $D420

        .zeropage
p:      .res    2
n:      .res    2
sum:    .res    2
sid:    .res    2
t:      .res    1
got:    .res    1
o1:     .res    1

        .code
start:  sei
        cld
        ldx     #$FF
        txs
        lda     MBOX                    ; a byte left from before: dropped
        lda     #C65_HELLO
        jsr     send
loop:   jsr     recv
        cmp     #C65_ECHO
        beq     echo
        cmp     #C65_SUM
        beq     sumcmd
        cmp     #C65_SID
        bne     @n2
        jmp     sidcmd
@n2:    cmp     #C65_TONE
        bne     @n3
        jmp     tone
@n3:    lda     #$FF
        jsr     send
        jmp     loop

echo:   jsr     recv
        jsr     send
        jmp     loop

sumcmd: jsr     recv
        sta     p
        jsr     recv
        sta     p + 1
        jsr     recv
        sta     n
        jsr     recv
        sta     n + 1
        lda     #0
        sta     sum
        sta     sum + 1
        ldy     #0
@b:     lda     (p),y
        clc
        adc     sum
        sta     sum
        bcc     @nc
        inc     sum + 1
@nc:    iny
        bne     @np
        inc     p + 1
@np:    lda     n                       ; one less to go; 0 0 at the start is 65,536
        bne     @lo
        dec     n + 1
@lo:    dec     n
        lda     n
        ora     n + 1
        bne     @b
        lda     sum
        jsr     send
        lda     sum + 1
        jsr     send
        jmp     loop

; A byte to the host, once it has taken the last.
send:   pha
@w:     lda     MBST
        and     #$02
        bne     @w
        pla
        sta     MBOX
        rts
; A byte from the host.
recv:   lda     MBST
        and     #$01
        beq     recv
        lda     MBOX
        rts

; ---------------------------------------------------------------------------
; C65_SID and C65_TONE

; sid = SID A's or B's base, from the next byte.
which:  jsr     recv
        ldx     #<SIDA
        cmp     #0
        beq     @a
        ldx     #<SIDB
@a:     stx     sid
        lda     #>SIDA
        sta     sid + 1
        rts

sidcmd: jsr     which
        ldy     #$18                    ; volume 0, voice 3 cut off from the output
        lda     #$80
        sta     (sid),y
        ldy     #$13                    ; voice 3: attack 2 ms, decay 6 ms; sustain 15, release 6 ms
        lda     #$00
        sta     (sid),y
        iny
        lda     #$F0
        sta     (sid),y
        ldy     #$0E                    ; a high frequency: OSC3 moves between reads
        lda     #$00
        sta     (sid),y
        iny
        lda     #$40
        sta     (sid),y
        ldy     #$1C                    ; ENV3 before the gate
        lda     (sid),y
        sta     t
        ldy     #$12                    ; sawtooth, gate on
        lda     #$21
        sta     (sid),y
        ldy     #$1B
        lda     (sid),y
        sta     o1
        lda     #0
        sta     got
        ldx     #0
@osc:   lda     (sid),y                 ; OSC3 again, some 50 cycles on
        cmp     o1
        beq     @same
        lda     #$01
        sta     got
@same:  dex
        bne     @osc
        ldx     #8                      ; some 10 ms: past the attack
@w1:    ldy     #0
@w2:    dey
        bne     @w2
        dex
        bne     @w1
        ldy     #$1C
        lda     (sid),y
        cmp     t
        beq     @flat
        bcc     @flat
        lda     got
        ora     #$02
        sta     got
@flat:  ldy     #$12                    ; voice 3: gate off, then silent
        lda     #$20
        sta     (sid),y
        lda     #$08
        sta     (sid),y                 ; the test bit: the oscillator held
        lda     got
        jsr     send
        jmp     loop

tone:   jsr     which
        jsr     recv
        beq     @off
        ldy     #$00                    ; voice 1 at 440 Hz (7,493 on PAL's clock)
        lda     #<7493
        sta     (sid),y
        iny
        lda     #>7493
        sta     (sid),y
        ldy     #$05
        lda     #$09
        sta     (sid),y
        iny
        lda     #$F0
        sta     (sid),y
        ldy     #$18
        lda     #$0F
        sta     (sid),y
        ldy     #$04
        lda     #$11                    ; triangle, gate on
        sta     (sid),y
        jmp     @ack
@off:   ldy     #$04
        lda     #$10
        sta     (sid),y
        ldy     #$18
        lda     #$00
        sta     (sid),y
@ack:   lda     #C65_TONE
        jsr     send
        jmp     loop


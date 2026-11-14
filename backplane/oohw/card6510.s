; The diagnostics' program for the sound card's 6510 (hw/diag.md, "The 6510
; test program"): the card's own code, NMOS 6502 opcodes only, assembled on
; its own (card6510.cfg: $0200-$05FF in the card's RAM, its zero page
; $02-$1F; it is about 800 bytes, so it has four pages, not two) and
; taken whole into DIAG.COM (and, from D3, the ROM core), which load it by
; function 82 (SN_LOAD) and start it at $0200 (SN_START). Not a module of
; oohw.lib.
;
; It starts by reading the mailbox from the host once (a byte a program
; left there, which only a /RESET empties) and sending $A5. Then it answers
; commands, each a byte from the host and its operands, each answered:
;
;   C65_ECHO   b                 b sent back
;   C65_SUM    a.lo a.hi n.lo n.hi
;                                the 16-bit sum of n bytes from a (n 0:
;                                65,536): its low byte, then its high
;   C65_TEST   a.lo a.hi e.lo e.hi f
;                                its RAM from a to e - 1 tested: address in
;                                address (each byte its address's low byte
;                                XOR its high), then with f bit 0 the march
;                                (March C-, background $00), a page at a
;                                time (a must start a page); $D000-$DFFF is
;                                reached as RAM meanwhile (the port's bits
;                                0-2 low), so the mailbox is out of reach
;                                until it ends. Answered by five bytes: 0
;                                passed or 1 failed, then the first failing
;                                address (low, high), what was read and what
;                                was expected
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
; never $0200-$05FF (itself), nor its vectors; the host asks for $0600-
; $FFF9 to be tested (C65_FIRST). A byte to the host is sent once it has taken the last
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
e:      .res    2
a0:     .res    2
n:      .res    2
sum:    .res    2
bad:    .res    2
npg:    .res    1
npg0:   .res    1
cnt:    .res    1
dirn:   .res    1
flags:  .res    1
sid:    .res    2
t:      .res    1
got:    .res    1
want:   .res    1
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
        cmp     #C65_TEST
        bne     @n1
        jmp     test
@n1:    cmp     #C65_SID
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
; C65_TEST: a page at a time, as hwmem.s's (a starts a page: the host's
; runs do).

test:   jsr     recv
        sta     a0
        jsr     recv
        sta     a0 + 1
        jsr     recv
        sta     e
        jsr     recv
        sta     e + 1
        jsr     recv
        sta     flags
        sec                             ; the pages, the last perhaps a part one
        lda     e + 1
        sbc     a0 + 1
        sta     npg0
        lda     e
        beq     @whole
        inc     npg0
@whole: lda     #$07                    ; the port's bits 0-2 out, low: RAM everywhere
        sta     PORTDDR
        lda     #$00
        sta     PORT
        jsr     aiaw
        jsr     aiac
        bcs     @end
        lda     flags
        and     #$01
        beq     @ok
        jsr     march
        bcs     @end
@ok:    lda     #0
        sta     bad
        sta     bad + 1
        sta     got
        sta     want
        clc
@end:   lda     #$07                    ; the I/O back: bits 0-2 high, then inputs again
        sta     PORT
        lda     #$00
        sta     PORTDDR
        rol     a                       ; 1 failed
        jsr     send
        lda     bad
        jsr     send
        lda     bad + 1
        jsr     send
        lda     got
        jsr     send
        lda     want
        jsr     send
        jmp     loop

; From the first page up: p, and npg the pages to go.
up:     lda     #0
        sta     p
        sta     dirn
        lda     a0 + 1
        sta     p + 1
        lda     npg0
        sta     npg
        rts
; From the last page down.
down:   jsr     up
        clc
        adc     a0 + 1
        sta     p + 1
        dec     p + 1
        inc     dirn
        rts
; This page's bytes: cnt (0: 256), the last page's e's low byte (the
; first to go down).
count:  lda     dirn
        beq     @up
        lda     npg
        cmp     npg0
        bne     @full
        beq     @part
@up:    lda     npg
        cmp     #1
        bne     @full
@part:
        lda     e
        sta     cnt
        rts
@full:  lda     #0
        sta     cnt
        rts

aiaw:   jsr     up
@pg:    jsr     count
        ldy     #0
@b:     tya
        eor     p + 1
        sta     (p),y
        iny
        cpy     cnt
        bne     @b
        inc     p + 1
        dec     npg
        bne     @pg
        rts

aiac:   jsr     up
@pg:    jsr     count
        ldy     #0
@b:     tya
        eor     p + 1
        cmp     (p),y
        bne     @bad
        iny
        cpy     cnt
        bne     @b
        inc     p + 1
        dec     npg
        bne     @pg
        clc
        rts
@bad:   sta     want
        jmp     failed

; The failure at p + Y: what is there read again.
failed: lda     (p),y
        sta     got
        sty     bad
        lda     p + 1
        sta     bad + 1
        sec
        rts

; March C-, background $00.
march:  jsr     up                      ; (w0)
@w0:    jsr     count
        ldy     #0
        lda     #$00
@w0b:   sta     (p),y
        iny
        cpy     cnt
        bne     @w0b
        inc     p + 1
        dec     npg
        bne     @w0
        lda     #$00                    ; up (r0, w1)
        sta     want
        jsr     up
@e1:    jsr     count
        ldy     #0
@e1b:   lda     (p),y
        bne     @b1
        lda     #$FF
        sta     (p),y
        iny
        cpy     cnt
        bne     @e1b
        inc     p + 1
        dec     npg
        bne     @e1
        jmp     @n1
@b1:    jmp     failed
@n1:
        lda     #$FF                    ; up (r1, w0)
        sta     want
        jsr     up
@e2:    jsr     count
        ldy     #0
@e2b:   lda     (p),y
        cmp     #$FF
        bne     @b2
        lda     #$00
        sta     (p),y
        iny
        cpy     cnt
        bne     @e2b
        inc     p + 1
        dec     npg
        bne     @e2
        jmp     @n2
@b2:    jmp     failed
@n2:
        lda     #$00                    ; down (r0, w1)
        sta     want
        jsr     down
@e3:    jsr     count
        ldy     cnt
@e3b:   dey
        lda     (p),y
        bne     @b3
        lda     #$FF
        sta     (p),y
        cpy     #0
        bne     @e3b
        dec     p + 1
        dec     npg
        bne     @e3
        jmp     @n3
@b3:    jmp     failed
@n3:
        lda     #$FF                    ; down (r1, w0)
        sta     want
        jsr     down
@e4:    jsr     count
        ldy     cnt
@e4b:   dey
        lda     (p),y
        cmp     #$FF
        bne     @b4
        lda     #$00
        sta     (p),y
        cpy     #0
        bne     @e4b
        dec     p + 1
        dec     npg
        bne     @e4
        jmp     @n4
@b4:    jmp     failed
@n4:
        lda     #$00                    ; (r0)
        sta     want
        jsr     up
@e5:    jsr     count
        ldy     #0
@e5b:   lda     (p),y
        bne     @b5
        iny
        cpy     cnt
        bne     @e5b
        inc     p + 1
        dec     npg
        bne     @e5
        jmp     @n5
@b5:    jmp     failed
@n5:
        clc
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


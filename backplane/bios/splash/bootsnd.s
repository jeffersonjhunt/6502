; bootsnd.s - the OO-1 boot splash's sound, for the sound card's 6510.
;
; Timed to the video card's splash (hw/design.md "Boot splash"), 5 s:
;
;   * Four MotoGP bikes scream past, one for each part of the logo. Each bike
;     is a voice played on both SIDs: SID A is the left channel and SID B the
;     right. The chips' master volumes pan, a low-pass on each chip dulls the
;     bikes on its far side (so each bike pans on its own), and each voice's
;     sustain level carries its distance as it goes away. The pitch is
;     Doppler and rpm from soundgen.py's tables, glided between frames, with a
;     random wobble a tick for the engine's roughness and a quickshifter's
;     crack (two ticks of noise) at the upshift. The lone fourth bike also has
;     a sawtooth an octave down and a noise roar.
;   * The VINTAGE FAST stamp: a quick push of air as it comes down, then the
;     thunk: a deep thump falling from 120 Hz, a rubbery smack of noise through
;     a resonant low-pass snapping shut, and the wooden knock of the handle
;     (196 Hz left, 203 Hz right).
;
; Play runs 240 times a second (CIA 1's timer, NTSC clock), four ticks to a
; video frame. Two ways in:
;
;   * As a PSID tune: init at $1000 (it sets the timer), play at $1003. Any
;     SID player plays bootsnd.sid.
;   * As the card's whole program, which is how the BIOS runs it (splash.s):
;     the card's reset vector at BOOT ($1006). It calls init and the first
;     play at once, so frame n sounds with the splash's frame n, then play
;     from the timer. When the tune is over it zeroes the SIDs' oscillators
;     (TEST) and registers and writes $DE02, which stops the card's 6510 (CTRL RUN clears): the host
;     has the SIDs as after a reset.
;
; Assemble: ca65 bootsnd.s && ld65 -C bootsnd.cfg bootsnd.o -o bootsnd.bin
; (backplane/bios's Makefile does both; soundgen.py remakes tables.inc).

SIDA    = $D400
SIDB    = $D420
CIA_TA  = $DC04                 ; timer A latch, low then high
CIA_ICR = $DC0D
CIA_CRA = $DC0E
STOP    = $DE02                 ; the card: any write stops its 6510
TICK_CLOCKS = 1022727 / 240 - 1 ; the timer underflows after latch + 1 clocks

REL     = $08                   ; release 300 ms once an engine's gate drops
RES     = $20                   ; engines: filter resonance 2 ($D417's high nibble)
FARCUT  = $18                   ; the far ear's low-pass, about 1 kHz
LOWPASS = $10                   ; $D418 mode bits

; zero page
tick    = $E0                   ; 0-3: the tick within the frame
flo     = $E1                   ; the frame, 16 bits
fhi     = $E2
lfsr    = $E3                   ; roughness and the crack
claimed = $E4                   ; voices taken this frame (a later entry wins)
maskl   = $E5                   ; voices SID A (left) hears from the far side
maskr   = $E6                   ; ... and SID B
voll    = $E7                   ; the loudest call on each chip's volume ...
volr    = $E8
outl    = $E9                   ; ... and the volume as last set
outr    = $EA
idx     = $EB                   ; the frame in the engine tables
ent     = $EC                   ; the engine entry being done
vo      = $ED                   ; its voice's register offset
vown    = $EE                   ; voices the engines glide this frame
pop     = $EF                   ; voices back from the crack to their wave on tick 2
curlo   = $F0                   ; per voice (3 each): the frequency now ...
curhi   = $F3
dlo     = $F6                   ; ... and its step a tick
dhi     = $F9
vctl    = $FC                   ; ... and its control byte (wave and gate)
flo2    = $FF                   ; scratch: a frequency's low byte

.segment "CODE"

        jmp     init            ; $1000
        jmp     play            ; $1003
        jmp     boot            ; $1006: the card's reset vector

boot:   sei
        cld
        ldx     #$FF
        txs
        jsr     init
        jsr     play            ; tick 0 of frame 0, now
        lda     #<irq
        sta     $FFFE
        lda     #>irq
        sta     $FFFF
        lda     #$81            ; timer A interrupts
        sta     CIA_ICR
        lda     #$11            ; load the latch, start, continuous
        sta     CIA_CRA
        cli
@idle:  jmp     @idle

irq:    pha
        txa
        pha
        tya
        pha
        lda     CIA_ICR         ; acknowledge
        jsr     play
        lda     done
        beq     :+
        lda     #$08            ; the tune is over: TEST zeroes every oscillator,
        sta     SIDA+4          ; as /RES does...
        sta     SIDA+11
        sta     SIDA+18
        sta     SIDB+4
        sta     SIDB+11
        sta     SIDB+18
        lda     #0              ; ...then every register 0, upwards: each voice's
        tax                     ; frequency before the control that lets TEST go
@clr:   sta     SIDA,x
        sta     SIDB,x
        inx
        cpx     #$19
        bne     @clr
        sta     STOP            ; ...and the card stops itself
:       pla
        tay
        pla
        tax
        pla
        rti

init:   lda     #0
        ldx     #$18
@sid:   sta     SIDA,x
        sta     SIDB,x
        dex
        bpl     @sid
        ldx     #$1F
@zp:    sta     tick,x
        dex
        bpl     @zp
        sta     mode
        sta     done
        lda     #$A5
        sta     lfsr
        ldx     #0
@voice: lda     #$12            ; attack 8 ms; decay 24 ms, how fast a lowered sustain lands
        sta     SIDA+5,x
        sta     SIDB+5,x
        lda     #$05            ; pulse width about 1/3
        sta     SIDA+3,x
        sta     SIDB+3,x
        txa
        clc
        adc     #7
        tax
        cpx     #21
        bne     @voice
        lda     #FARCUT
        sta     SIDA+$16
        sta     SIDB+$16
        lda     #<TICK_CLOCKS   ; 240 Hz
        sta     CIA_TA
        lda     #>TICK_CLOCKS
        sta     CIA_TA+1
        rts

play:   lda     done
        bne     @r
        lda     tick
        bne     @sub
        jsr     frame_tick      ; once a frame: tables, gates, the mix
@sub:   jsr     tick_work       ; every tick: glides, roughness, the impact's sweeps
        inc     tick
        lda     tick
        cmp     #4
        bcc     @r
        lda     #0
        sta     tick
        inc     flo
        bne     :+
        inc     fhi
:       lda     flo
        cmp     #<FRAMES
        bne     @r
        lda     fhi
        cmp     #>FRAMES
        bne     @r
        lda     #0              ; over: both chips silent
        sta     SIDA+$18
        sta     SIDB+$18
        inc     done
@r:     rts

; --- once a frame -------------------------------------------------------------

frame_tick:
        lda     lfsr            ; 8-bit Galois LFSR, a step a frame as well as a tick
        lsr     a
        bcc     :+
        eor     #$B8
:       sta     lfsr
        lda     fhi
        bne     @r              ; past frame 255: only the impact's tail, in the ticks
        lda     flo
        cmp     #IMPACT
        bcc     engines
        bne     @r
        jmp     impact
@r:     rts

engines:
        lda     #0
        sta     voll
        sta     volr
        sta     claimed
        sta     maskl
        sta     maskr
        sta     vown
        lda     flo
        cmp     #WHOOSH
        bcc     :+
        jsr     whoosh          ; the stamp coming down has voice 3
:       ldx     #NVOICES-1      ; last to first: a later entry takes its voice over
@ent:   lda     flo
        sec
        sbc     ent_t0,x
        bcc     @next           ; not started
        clc
        adc     ent_i0,x
        cmp     #LEN
        bcs     @next           ; gone
        sta     idx
        lda     ent_vb,x
        bit     claimed
        bne     @next           ; a later entry has this voice
        ora     claimed
        sta     claimed
        ; which side is it on? before the pass, the side it came from
        lda     idx
        cmp     #PRE            ; C set: passed
        lda     ent_fl,x
        and     #RIGHT
        adc     #0              ; bit 0: 1 = on the right
        lsr     a
        lda     ent_vb,x
        bcs     @onr
        ora     maskr           ; on the left: dull in the right ear
        sta     maskr
        bcc     @side
@onr:   ora     maskl
        sta     maskl
@side:  stx     ent
        jsr     engine
        ldx     ent
@next:  dex
        bpl     @ent

        ; Master volume (pan), slewed: a jump in $D418 is a click.
        lda     voll
        ldx     outl
        jsr     slew
        sta     outl
        ora     #LOWPASS
        sta     SIDA+$18
        lda     volr
        ldx     outr
        jsr     slew
        sta     outr
        ora     #LOWPASS
        sta     SIDB+$18
        lda     maskl           ; far-side voices through the low-pass
        ora     #RES
        sta     SIDA+$17
        lda     maskr
        ora     #RES
        sta     SIDB+$17
        lda     #FARCUT
        ldx     flo
        cpx     #WHOOSH
        bcc     :+
        lda     wcut            ; the push opens the filter as the stamp nears
:       sta     SIDA+$16
        sta     SIDB+$16
        rts

; One engine voice for this frame: X = ent = the entry, idx its table frame.
engine: ldy     ent_vi,x        ; Y = the voice, 0-2
        lda     ent_vb,x
        ora     vown
        sta     vown
        lda     ent_vo,x
        sta     vo
        ; the frequency it heads for: the table, the detune, the layer's octave
        ldx     idx
        lda     pitch_lo,x
        ldx     ent
        clc
        adc     ent_dlo,x
        sta     flo2
        ldx     idx
        lda     pitch_hi,x
        ldx     ent
        adc     ent_dhi,x
        sta     fhi2
        lda     ent_fl,x
        and     #BODY
        beq     :+
        lsr     fhi2            ; an octave down
        ror     flo2
:       lda     ent_fl,x
        and     #WIND
        beq     :+
        asl     flo2            ; the roar: noise twice as bright
        rol     fhi2
:       lda     idx
        cmp     ent_i0,x
        bne     @glide
        lda     flo2            ; its first frame: straight there
        sta     curlo,y
        lda     fhi2
        sta     curhi,y
        lda     #0
        sta     dlo,y
        sta     dhi,y
        beq     @env
@glide: sec                     ; a quarter of the way a tick
        lda     flo2
        sbc     curlo,y
        sta     flo2
        lda     fhi2
        sbc     curhi,y
        cmp     #$80
        ror     a
        ror     flo2
        cmp     #$80
        ror     a
        ror     flo2
        sta     dhi,y
        lda     flo2
        sta     dlo,y

@env:   ldx     idx             ; distance: the sustain level
        lda     sus_tab,x
        ldx     ent
        pha
        lda     ent_fl,x
        and     #WIND
        beq     :+
        pla                     ; the roar sits 6 dB under its bike
        lsr     a
        and     #$F0
        pha
:       pla
        ora     #REL
        ldx     vo
        sta     SIDA+6,x
        sta     SIDB+6,x

        ldx     ent             ; the wave, and the gate
        lda     ent_fl,x
        and     #BODY|WIND
        tax
        lda     waves,x
        sta     wave
        ldx     ent
        lda     idx
        cmp     ent_i0,x
        beq     @off            ; its first frame: gate off a frame (retriggers a taken voice)
        cmp     #LEN-1
        beq     @off            ; its last: release
        inc     wave            ; gate on
@off:   lda     wave
        sta     vctl,y          ; kept, for after the crack
        lda     ent_fl,x
        and     #MAIN
        beq     @plain
        lda     idx
        cmp     #SHIFT
        bne     @plain
        lda     ent_vb,x        ; the upshift: the quickshifter's crack,
        ora     pop             ; two ticks of noise
        sta     pop
        lda     #$81
        bne     @write
@plain: lda     wave
@write: ldx     vo
        sta     SIDA+4,x
        sta     SIDB+4,x

        ldx     idx             ; this bike's call on each chip's volume
        lda     pan_tab,x
        ldx     ent
        pha
        lda     ent_fl,x
        lsr     a               ; C: from the right
        pla
        bcc     :+              ; from the right: swap left and right
        asl     a
        adc     #$80
        rol     a
        asl     a
        adc     #$80
        rol     a
:       pha
        and     #$0F
        cmp     volr
        bcc     :+
        sta     volr
:       pla
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        cmp     voll
        bcc     :+
        sta     voll
:       rts

waves:  .byte   $40, 0, $20, 0, $80     ; pulse; BODY a sawtooth; WIND noise

; The stamp coming down (WHOOSH to IMPACT, 4 frames): voice 3, a push of
; noise rising through the low-pass as it opens, on both chips.
whoosh: lda     claimed
        ora     #4
        sta     claimed
        lda     maskl
        ora     #4
        sta     maskl
        lda     maskr
        ora     #4
        sta     maskr
        lda     flo
        sec
        sbc     #WHOOSH
        tax                     ; frames into the fall
        bne     @on
        lda     #$40            ; attack 38 ms: about its way down
        sta     SIDA+14+5
        sta     SIDB+14+5
        lda     #$F0
        sta     SIDA+14+6
        sta     SIDB+14+6
        lda     #$80
        sta     SIDA+14+4
        sta     SIDB+14+4
        lda     #$81
        sta     SIDA+14+4
        sta     SIDB+14+4
        lda     #<WHOOSH_HZ
        sta     wlo
        lda     #>WHOOSH_HZ
        sta     whi
        lda     #$20
        sta     wcut
@on:    lda     wlo
        sta     SIDA+14
        sta     SIDB+14
        lda     whi
        sta     SIDA+15
        sta     SIDB+15
        clc
        lda     wlo
        adc     #<WHOOSH_STEP
        sta     wlo
        lda     whi
        adc     #>WHOOSH_STEP
        sta     whi
        lda     wcut
        clc
        adc     #28
        sta     wcut
        txa                     ; its call on both volumes: 3, up 2 a frame, so the
        asl     a               ; landing still jumps out of it
        clc
        adc     #3
        cmp     voll
        bcc     :+
        sta     voll
:       cmp     volr
        bcc     :+
        sta     volr
:       rts

; The stamp lands (IMPACT): every voice, both chips.
impact: lda     #1
        sta     mode
        lda     #0
        sta     itick
        sta     vown
        sta     pop
        lda     #15
        sta     ivol
        lda     #LOWPASS | 15
        sta     SIDA+$18
        sta     SIDB+$18
        lda     #$C6            ; resonance 12: the thump and the smack through the low-pass
        sta     SIDA+$17
        sta     SIDB+$17
        lda     #$A0
        sta     icut
        sta     SIDA+$16
        sta     SIDB+$16
        ; the thump, voice 2: a pulse for its punch, a triangle after
        lda     #$08            ; attack 2 ms, decay 300 ms
        sta     SIDA+7+5
        sta     SIDB+7+5
        lda     #0
        sta     SIDA+7+6
        sta     SIDB+7+6
        lda     #$04
        sta     SIDA+7+3
        sta     SIDB+7+3
        lda     #<THUMP_HZ
        sta     thlo
        sta     SIDA+7
        sta     SIDB+7
        lda     #>THUMP_HZ
        sta     thhi
        sta     SIDA+8
        sta     SIDB+8
        lda     #$40
        sta     SIDA+7+4
        sta     SIDB+7+4
        lda     #$41
        sta     SIDA+7+4
        sta     SIDB+7+4
        ; the smack, voice 3: noise, falling
        lda     #$04            ; decay 114 ms
        sta     SIDA+14+5
        sta     SIDB+14+5
        lda     #0
        sta     SIDA+14+6
        sta     SIDB+14+6
        lda     #<SMACK_HZ
        sta     crlo
        sta     SIDA+14
        sta     SIDB+14
        lda     #>SMACK_HZ
        sta     crhi
        sta     SIDA+15
        sta     SIDB+15
        lda     #$80
        sta     SIDA+14+4
        sta     SIDB+14+4
        lda     #$81
        sta     SIDA+14+4
        sta     SIDB+14+4
        ; the knock, voice 1: a triangle, a touch apart in each ear
        lda     #$05            ; decay 168 ms
        sta     SIDA+5
        sta     SIDB+5
        lda     #0
        sta     SIDA+6
        sta     SIDB+6
        lda     #<KNOCK_A
        sta     SIDA+0
        lda     #>KNOCK_A
        sta     SIDA+1
        lda     #<KNOCK_B
        sta     SIDB+0
        lda     #>KNOCK_B
        sta     SIDB+1
        lda     #$10
        sta     SIDA+4
        sta     SIDB+4
        lda     #$11
        sta     SIDA+4
        sta     SIDB+4
        rts

; --- every tick -----------------------------------------------------------------

tick_work:
        lda     lfsr
        lsr     a
        bcc     :+
        eor     #$B8
:       sta     lfsr
        lda     mode
        beq     :+
        jmp     impact_tick
:       ldy     #0
@v:     lda     vbits,y
        and     vown
        bne     :+
        jmp     @nv
:       lda     voffs,y
        sta     vo
        clc                     ; the glide
        lda     curlo,y
        adc     dlo,y
        sta     curlo,y
        lda     curhi,y
        adc     dhi,y
        sta     curhi,y
        sta     fhi2            ; roughness: plus or minus cur / 64, at random
        lda     curlo,y
        ldx     #6
:       lsr     fhi2
        ror     a
        dex
        bne     :-
        sta     flo2
        bit     lfsr
        bmi     @minus
        clc
        lda     curlo,y
        adc     flo2
        sta     flo2
        lda     curhi,y
        adc     fhi2
        jmp     @out
@minus: sec
        lda     curlo,y
        sbc     flo2
        sta     flo2
        lda     curhi,y
        sbc     fhi2
@out:   ldx     vo
        sta     SIDA+1,x
        sta     SIDB+1,x
        lda     flo2
        sta     SIDA+0,x
        sta     SIDB+0,x
        lda     lfsr            ; the pulse width wanders: a raspy exhaust
        sta     SIDA+2,x
        sta     SIDB+2,x
        lda     tick            ; tick 2: back from the crack
        cmp     #2
        bne     @nv
        lda     vbits,y
        and     pop
        beq     @nv
        eor     pop
        sta     pop
        lda     vctl,y
        sta     SIDA+4,x
        sta     SIDB+4,x
@nv:    iny
        cpy     #3
        beq     :+
        jmp     @v
:       rts

vbits:  .byte   1, 2, 4
voffs:  .byte   0, 7, 14

impact_tick:
        lda     itick
        cmp     #$FF
        beq     :+
        inc     itick
:       ; the thump: down 1/32 a tick, to its floor
        lda     thhi
        sta     fhi2
        lda     thlo
        ldx     #5
:       lsr     fhi2
        ror     a
        dex
        bne     :-
        sta     flo2
        sec
        lda     thlo
        sbc     flo2
        sta     thlo
        lda     thhi
        sbc     fhi2
        sta     thhi
        cmp     #>THUMP_FLOOR
        bcc     @thfl
        bne     @thok
        lda     thlo
        cmp     #<THUMP_FLOOR
        bcs     @thok
@thfl:  lda     #<THUMP_FLOOR
        sta     thlo
        lda     #>THUMP_FLOOR
        sta     thhi
@thok:  lda     thlo
        sta     SIDA+7
        sta     SIDB+7
        lda     thhi
        sta     SIDA+8
        sta     SIDB+8

        lda     crhi            ; the smack: down 1/32 a tick, to its floor
        sta     fhi2
        lda     crlo
        ldx     #5
:       lsr     fhi2
        ror     a
        dex
        bne     :-
        sta     flo2
        sec
        lda     crlo
        sbc     flo2
        sta     crlo
        lda     crhi
        sbc     fhi2
        sta     crhi
        cmp     #>SMACK_FLOOR
        bcs     :+
        lda     #<SMACK_FLOOR
        sta     crlo
        lda     #>SMACK_FLOOR
        sta     crhi
:       lda     crlo
        sta     SIDA+14
        sta     SIDB+14
        lda     crhi
        sta     SIDA+15
        sta     SIDB+15
@tone:  lda     itick
        cmp     #6
        bne     :+
        lda     #$11            ; the thump: a triangle now, for the sub
        sta     SIDA+7+4
        sta     SIDB+7+4
:       lda     icut            ; the low-pass snaps shut
        cmp     #$0F
        bcc     :+
        dec     icut
        dec     icut
        dec     icut
:       lda     icut
        sta     SIDA+$16
        sta     SIDB+$16
        lda     itick           ; after 0.25 s, the volume down a step every 4 ticks, to 0
        cmp     #60             ; (in steps: a jump in $D418 is a click)
        bcc     @r
        and     #3
        bne     @r
        lda     ivol
        beq     @r
        dec     ivol
        lda     ivol
        ora     #LOWPASS
        sta     SIDA+$18
        sta     SIDB+$18
@r:     rts

; A = the volume wanted, X = the volume now: returns the next one. Up at most
; 3 a frame, down 1 a frame, so a chip never drops to silence while a voice
; is still releasing.
slew:   stx     flo2
        cmp     flo2
        beq     @done
        bcc     @down
        sec
        sbc     flo2
        cmp     #4
        bcc     @up
        lda     #3
@up:    clc
        adc     flo2
        rts
@down:  dex
        txa
@done:  rts

.include "tables.inc"            ; the tables, after the code: $1000 is the jump table

.segment "BSS"
fhi2:   .res    1               ; scratch: a frequency's high byte
wave:   .res    1               ; scratch: an engine's control byte
mode:   .res    1               ; 0 engines, 1 the landing
done:   .res    1               ; the tune is over
wlo:    .res    1               ; the whoosh: its frequency ...
whi:    .res    1
wcut:   .res    1               ; ... and its filter cutoff
itick:  .res    1               ; ticks since the landing (to 255)
thlo:   .res    1               ; the thump's frequency
thhi:   .res    1
crlo:   .res    1               ; the smack's
crhi:   .res    1
icut:   .res    1               ; the landing's cutoff
ivol:   .res    1               ; ... and volume

; docsnd.s - the OO-1 boot splash's sound on the ES5503, for the card's 6510
; (sound card revision 2; revision 1 plays bootsnd.s on the SIDs instead).
;
; Timed to the video card's splash (hw/design.md "Boot splash"), 5 s, from the
; samples in wave RAM (docsamples.py; the BIOS loads them from flash first):
;
;   * Four MotoGP bikes scream past, one for each part of the logo, each the
;     sampled engine on two oscillators in each ear (a voice peaks at an
;     eighth of the card's bus, which mixes the chip at a half: two for 6 dB
;     more). Every tick each glides to its pitch (Doppler and rpm) and each
;     ear's volume (distance and an equal-power pan) from docgen.py's tables,
;     so every bike pans on its own. At each upshift the crack plays, as the
;     ignition cut dips the engine. The bike that passes alone, DIGITAL
;     MANUFACTURING, also has the engine an octave down for body and the
;     wind's roar, on an oscillator to an ear: half level, and a quarter.
;   * The stamp: its push of air as it comes down, then at the landing the
;     stereo thunk on twenty oscillators, ten to an ear.
;
; The DOC's registers are at $DF00-$DFFF while DOCCTL RUN is set (the BIOS
; sets it). 24 oscillators run, so it makes 34,419 samples a second. Play
; runs 240 times a second (CIA 1's timer, NTSC clock), four ticks to a video
; frame. BOOT ($1006) is the card's reset vector: init, the first play at
; once (frame n sounds with the splash's frame n), then the timer. When it's
; over every oscillator is halted and it writes 1 to $DE02: the card's 6510
; stops and DOCCTL RUN clears, so the host has the card as after a reset and
; PCM may run again.
;
; Assemble: ca65 docsnd.s && ld65 -C bootsnd.cfg docsnd.o -o docsnd.bin
; (docgen.py makes doctables.inc).

DOC     = $DF00
FREQLO  = DOC + $00
FREQHI  = DOC + $20
VOL     = DOC + $40
PTR     = DOC + $80
CTRL    = DOC + $A0
TABLE   = DOC + $C0
ENABLE  = DOC + $E1

CIA_TA  = $DC04
CIA_ICR = $DC0D
CIA_CRA = $DC0E
STOP    = $DE02                 ; bit 0 set: the 6510 stops and DOCCTL RUN clears
TICK_CLOCKS = 1022727 / 240 - 1

HALT    = $01                   ; control: halted
ONESHOT = $02                   ; control: mode 1
THUNKS  = 20                    ; oscillators 0-19 play the thunk at the landing, ten to an ear
RIGHTCH = $10                   ; control: channel 1, the right (even channels are left)

; zero page
tick    = $E0                   ; 0-3: the tick within the frame
flo     = $E1                   ; the frame, 16 bits
fhi     = $E2
idx     = $E3                   ; the frame in the tables
ent     = $E4                   ; the voice being done (0-5)
run     = $E5                   ; voices playing, a bit each
mode    = $E6                   ; 0 the bikes, 1 the stamp has landed
done    = $E7
tlo     = $E8                   ; scratch: a frequency
thi     = $E9
tvl     = $EA                   ; scratch: an ear's volume, half scale
tvr     = $EB

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
        lda     #1              ; over: the 6510 stops and the DOC with it
        sta     STOP
:       pla
        tay
        pla
        tax
        pla
        rti

init:   lda     #0
        ldx     #$1F
@zp:    sta     tick,x
        dex
        bpl     @zp
        lda     #(OSCS - 1) << 1
        sta     ENABLE
        ldx     #OSCS - 1
@osc:   lda     init_ctl,x      ; halted, its ear, its mode
        sta     CTRL,x
        lda     #0
        sta     VOL,x
        lda     init_table,x
        sta     TABLE,x
        lda     init_ptr,x
        sta     PTR,x
        dex
        bpl     @osc
        lda     #<F_CRACK
        sta     FREQLO+20
        sta     FREQLO+21
        lda     #>F_CRACK
        sta     FREQHI+20
        sta     FREQHI+21
        lda     #<F_PUSH
        sta     FREQLO+22
        sta     FREQLO+23
        lda     #>F_PUSH
        sta     FREQHI+22
        sta     FREQHI+23
        lda     #<TICK_CLOCKS   ; 240 Hz
        sta     CIA_TA
        lda     #>TICK_CLOCKS
        sta     CIA_TA+1
        rts

play:   lda     done
        bne     @r
        lda     tick
        bne     @sub
        jsr     frame_tick
@sub:   jsr     tick_work
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
        ldx     #OSCS - 1       ; over: every oscillator halted
@h:     lda     init_ctl,x
        sta     CTRL,x
        dex
        bpl     @h
        inc     done
@r:     rts

; --- once a frame ---------------------------------------------------------------

frame_tick:
        lda     fhi
        bne     @r              ; past frame 255: the thunk plays out by itself
        lda     flo
        cmp     #IMPACT
        bcc     @bikes
        bne     @r
        jmp     impact
@bikes: jsr     bikes
        lda     flo
        cmp     #WHOOSH
        bne     @r
        lda     #200            ; the stamp's push of air, from the top
        sta     VOL+22
        sta     VOL+23
        lda     #HALT | ONESHOT
        sta     CTRL+22
        lda     #ONESHOT
        sta     CTRL+22
        lda     #HALT | ONESHOT | RIGHTCH
        sta     CTRL+23
        lda     #ONESHOT | RIGHTCH
        sta     CTRL+23
@r:     rts

bikes:  ldx     #NVOICES - 1
@ent:   stx     ent
        lda     flo
        sec
        sbc     ent_t0,x
        bcc     @next           ; not started
        clc
        adc     ent_i0,x
        cmp     #LEN
        bcs     @over
        sta     idx
        jsr     voice
        jmp     @next
@over:  jsr     halt_voice      ; past its tables: silent
@next:  ldx     ent
        dex
        bpl     @ent
        rts

; One bike's voice for this frame: X = ent, idx its table frame.
voice:  ldy     idx
        lda     pitch_lo,y      ; the frequency it heads for
        clc
        adc     ent_dlo,x
        sta     tlo
        lda     pitch_hi,y
        adc     ent_dhi,x
        sta     thi
        lda     ent_fl,x
        and     #BODY
        beq     :+
        lsr     thi             ; the body: an octave down
        ror     tlo
:       lda     ent_fl,x
        and     #WIND
        beq     :+
        lsr     thi             ; the roar: the wind's half-rate table, at half the engine's
        ror     tlo             ; (the engine's is oversampled 4 times)
:       lda     vol_l,y         ; each ear's volume
        sta     tvl
        lda     vol_r,y
        sta     tvr
        lda     ent_fl,x
        lsr     a               ; C: from the right, so the ears swap
        bcc     :+
        lda     tvl
        ldy     tvr
        sta     tvr
        sty     tvl
:       lda     ent_fl,x
        and     #WIND
        beq     :+
        lsr     tvl             ; the roar: half on its one oscillator, a quarter in all
        lsr     tvr
:       lda     idx
        cmp     ent_i0,x
        bne     @glide
        lda     tlo             ; its first frame: straight there, from the top
        sta     fcl,x
        lda     thi
        sta     fch,x
        lda     tvl
        sta     vlh,x
        lda     tvr
        sta     vrh,x
        lda     #0
        sta     fdl,x
        sta     fdh,x
        sta     vll,x
        sta     dll,x
        sta     dlh,x
        sta     vrl,x
        sta     drl,x
        sta     drh,x
        jsr     start_voice
        jmp     @crack
@glide: sec                     ; a quarter of the way a tick: frequency ...
        lda     tlo
        sbc     fcl,x
        sta     tlo
        lda     thi
        sbc     fch,x
        cmp     #$80
        ror     a
        ror     tlo
        cmp     #$80
        ror     a
        ror     tlo
        sta     fdh,x
        lda     tlo
        sta     fdl,x
        sec                     ; ... the left ear (7.8 fixed point) ...
        lda     #0
        sbc     vll,x
        sta     tlo
        lda     tvl
        sbc     vlh,x
        cmp     #$80
        ror     a
        ror     tlo
        cmp     #$80
        ror     a
        ror     tlo
        sta     dlh,x
        lda     tlo
        sta     dll,x
        sec                     ; ... and the right
        lda     #0
        sbc     vrl,x
        sta     tlo
        lda     tvr
        sbc     vrh,x
        cmp     #$80
        ror     a
        ror     tlo
        cmp     #$80
        ror     a
        ror     tlo
        sta     drh,x
        lda     tlo
        sta     drl,x
@crack: lda     ent_fl,x        ; a bike at its upshift: the quickshifter's crack
        and     #MAIN
        beq     @done
        lda     idx
        cmp     #SHIFT
        bne     @done
        lda     ent_fl,x
        lsr     a               ; C: from the right, so the ears swap
        lda     #CRACK_VL
        ldy     #CRACK_VR
        bcc     :+
        lda     #CRACK_VR
        ldy     #CRACK_VL
:       sta     VOL+20
        sty     VOL+21
        lda     #HALT | ONESHOT
        sta     CTRL+20
        lda     #ONESHOT
        sta     CTRL+20
        lda     #HALT | ONESHOT | RIGHTCH
        sta     CTRL+21
        lda     #ONESHOT | RIGHTCH
        sta     CTRL+21
@done:  rts

; Start voice X's oscillators, free run from the top, as it stands.
start_voice:
        lda     run
        ora     vbit,x
        sta     run
        jsr     write_voice
        lda     #HALT
        jsr     voice_ctl       ; halted, then free run: from the top
        lda     #0
        jmp     voice_ctl

; Halt voice X's oscillators.
halt_voice:
        lda     vbit,x
        eor     #$FF
        and     run
        sta     run
        lda     #HALT
        ; fall into voice_ctl

; A (its halt bit) to voice X's oscillators' control: free run, each in its ear.
voice_ctl:
        ldy     voice_osc,x
        sta     CTRL,y
        ora     #RIGHTCH
        sta     CTRL+1,y
        and     #<~RIGHTCH
        dec     voice_dbl,x     ; (0 or 1: back as it was below)
        bmi     :+
        sta     CTRL+2,y
        ora     #RIGHTCH
        sta     CTRL+3,y
:       inc     voice_dbl,x
        rts

; Voice X's frequency and volumes to its oscillators.
write_voice:
        ldy     voice_osc,x
        lda     vlh,x
        bpl     :+
        lda     #0              ; a glide a hair below 0
:       asl     a
        sta     VOL,y
        sta     tvl
        lda     vrh,x
        bpl     :+
        lda     #0
:       asl     a
        sta     VOL+1,y
        sta     tvr
        lda     fcl,x
        sta     FREQLO,y
        sta     FREQLO+1,y
        lda     fch,x
        sta     FREQHI,y
        sta     FREQHI+1,y
        lda     voice_dbl,x
        beq     @r
        lda     tvl             ; the second pair, the same
        sta     VOL+2,y
        lda     tvr
        sta     VOL+3,y
        lda     fcl,x
        sta     FREQLO+2,y
        sta     FREQLO+3,y
        lda     fch,x
        sta     FREQHI+2,y
        sta     FREQHI+3,y
@r:     rts

; The stamp lands: every bike stops, and 0-19 play the thunk, ten to an ear.
impact: lda     #1
        sta     mode
        lda     #0
        sta     run
        ldx     #21
@h:     lda     init_ctl,x      ; 0-21 halted (the push plays out)
        sta     CTRL,x
        dex
        bpl     @h
        ldx     #THUNKS - 1
@t:     lda     thunk_table,x
        sta     TABLE,x
        lda     thunk_ptr,x
        sta     PTR,x
        lda     #<F_THUNK
        sta     FREQLO,x
        lda     #>F_THUNK
        sta     FREQHI,x
        lda     #255
        sta     VOL,x
        lda     thunk_ctl,x     ; one-shot, halted
        sta     CTRL,x
        dex
        bpl     @t
        ldx     #THUNKS - 1
@go:    lda     thunk_ctl,x
        and     #<~HALT         ; from the top, all together
        sta     CTRL,x
        dex
        bpl     @go
        rts

; --- every tick -------------------------------------------------------------------

tick_work:
        lda     mode
        bne     @r              ; landed: the thunk plays by itself
        ldx     #0
@v:     lda     run
        and     vbit,x
        beq     @nv
        clc                     ; the glides
        lda     fcl,x
        adc     fdl,x
        sta     fcl,x
        lda     fch,x
        adc     fdh,x
        sta     fch,x
        clc
        lda     vll,x
        adc     dll,x
        sta     vll,x
        lda     vlh,x
        adc     dlh,x
        sta     vlh,x
        clc
        lda     vrl,x
        adc     drl,x
        sta     vrl,x
        lda     vrh,x
        adc     drh,x
        sta     vrh,x
        jsr     write_voice
@nv:    inx
        cpx     #NVOICES
        bne     @v
@r:     rts

vbit:      .byte   1, 2, 4, 8, 16, 32
voice_osc: .byte   0, 4, 8, 12, 16, 18 ; each voice's first oscillator ...
voice_dbl: .byte   1, 1, 1, 1, 0, 0    ; ... and 1 where it is two to an ear
; per oscillator, as init leaves it: halted, even ones left and odd ones right
init_ctl:  .repeat 10
           .byte   HALT, HALT|RIGHTCH
           .endrepeat
           .byte   HALT|ONESHOT, HALT|ONESHOT|RIGHTCH, HALT|ONESHOT, HALT|ONESHOT|RIGHTCH
init_table: .repeat 18
           .byte   T_ENGINE
           .endrepeat
           .byte   T_WIND, T_WIND, T_CRACK, T_CRACK, T_PUSH, T_PUSH
init_ptr:  .repeat 18
           .byte   P_ENGINE
           .endrepeat
           .byte   P_WIND, P_WIND, P_CRACK, P_CRACK, P_PUSH, P_PUSH
thunk_table: .repeat THUNKS / 2
           .byte   T_THUNK_L, T_THUNK_R
           .endrepeat
thunk_ptr: .repeat THUNKS / 2
           .byte   P_THUNK_L, P_THUNK_R
           .endrepeat
thunk_ctl: .repeat THUNKS / 2
           .byte   HALT|ONESHOT, HALT|ONESHOT|RIGHTCH
           .endrepeat

.include "doctables.inc"        ; after the code: $1000 is the jump table

.segment "BSS"
fcl:    .res    NVOICES         ; per voice: the frequency now ...
fch:    .res    NVOICES
fdl:    .res    NVOICES         ; ... and its step a tick
fdh:    .res    NVOICES
vll:    .res    NVOICES         ; the left ear's volume, 7.8 fixed point ...
vlh:    .res    NVOICES
dll:    .res    NVOICES         ; ... and its step
dlh:    .res    NVOICES
vrl:    .res    NVOICES         ; the right ear's
vrh:    .res    NVOICES
drl:    .res    NVOICES
drh:    .res    NVOICES

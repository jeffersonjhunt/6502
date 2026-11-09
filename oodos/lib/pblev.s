; OOLIB's levels (include/oolib.inc, "Sound: playback"; hw/oolib.md, L5d):
; 8 bands and a VU pair, 0-255, in one form whatever is sounding, for a
; meter. A stream's and the ES5503's come from the vis page (PB_VIS): its 32
; bands averaged four at a time, its VU. A SID's are estimated: each voice's
; level (SID_ENV's) scaled by its SID's volume, in the band of its note's
; octave (noise in the top band), its SID's side (A left, B right) taking the
; loudest. So for a SID PB_LEVELS is called once a frame, as SID_ENV is.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "pb.inc"

        .zeropage
lp:     .res    2

        .bss
out:    .res    PBL_LEN
vb:     .res    SID_BLOCK               ; the SIDs' registers this frame
vis:    .res    PB_VIS_WAVE             ; the vis page's bands and VU
seen:   .res    1                       ; PBGEN at the last call
vi:     .res    1                       ; the voice
side:   .res    1
lvl:    .res    1

        .code

; A/X a block of PBL_LEN bytes: +0-7 the bands, lowest first, +8-9 the VU,
; left and right. Nothing begun: all 0.
PB_LEVELS:
        sta     lp
        stx     lp + 1
        ldx     #PBL_LEN - 1
@z:     stz     out,x
        dex
        bpl     @z
        lda     PBSRC
        beq     @put
        cmp     #PB_SID + 1
        beq     @sid
        lda     #<vis                   ; the stream or the ES5503: the vis page
        ldx     #>vis
        ldy     #PB_VIS_WAVE
        jsr     PB_VIS
        bcs     @ret
        ldx     #0                      ; band b: vis 4b to 4b + 3, averaged
        ldy     #0
@band:  lda     vis,y
        sta     PBP
        stz     PBP + 1
        lda     #3
        sta     lvl
@sum:   iny
        lda     vis,y
        clc
        adc     PBP
        sta     PBP
        bcc     @nc
        inc     PBP + 1
@nc:    dec     lvl
        bne     @sum
        iny
        lsr     PBP + 1
        ror     PBP
        lsr     PBP + 1
        ror     PBP
        lda     PBP
        sta     out,x
        inx
        cpx     #8
        bne     @band
        lda     vis + PB_VIS_VU
        sta     out + 8
        lda     vis + PB_VIS_VU + 1
        sta     out + 9
@put:   ldy     #PBL_LEN - 1
@cp:    lda     out,y
        sta     (lp),y
        dey
        bpl     @cp
        lda     #0
        clc
@ret:   rts

@sid:   lda     PBGEN                   ; a new tune: from quiet
        cmp     seen
        beq     @env
        sta     seen
        jsr     SID_ENV0
@env:   lda     #<vb
        ldx     #>vb
        jsr     SID_VOICES
        lda     #<vb
        ldx     #>vb
        jsr     SID_ENV
        lda     #SID_NVOICES - 1
        sta     vi
@voice: jsr     voice
        dec     vi
        bpl     @voice
        bra     @put

; Voice vi into its band and its side.
voice:  ldx     vi
        lda     SID_LVL,x
        beq     @no
        sta     lvl
        stz     side
        cpx     #3
        bcc     @a
        inc     side
@a:     ldy     SID_VOFF,x
        lda     vb + SID_V_CTRL,y
        and     #$F0                    ; no waveform: no sound
        beq     @no
        ldx     side                    ; x its SID's volume / 15
        lda     vb + SID_F_MODEVOL      ; (A's)
        cpx     #0
        beq     @vol
        lda     vb + SID_REGS + SID_F_MODEVOL
@vol:   and     #$0F
        ldx     lvl
        jsr     PBMUL
        lda     #15
        jsr     PBDIV
        sta     lvl
        beq     @no
        ldx     vi
        ldy     SID_VOFF,x
        lda     vb + SID_V_CTRL,y       ; noise: the top band
        bmi     @band
        lda     vb + SID_V_FREQ,y
        pha
        lda     vb + SID_V_FREQ + 1,y
        tax
        lda     SND_CTRL                ; the SIDs' clock
        tay
        pla
        jsr     SID_NOTE
        cmp     #SID_NONE
        beq     @no
        ldx     #0                      ; its octave: note / 12
@oct:   cmp     #12
        bcc     @o
        sbc     #12
        inx
        bra     @oct
@o:     txa
        bra     @in
@band:  lda     #7
@in:    tax
        lda     lvl
        cmp     out,x
        bcc     @vu
        sta     out,x
@vu:    ldx     side
        cmp     out + 8,x
        bcc     @no
        sta     out + 8,x
@no:    rts

; The boot splash (hw/design.md, "Boot splash").
;
; SPLASH, at boot: if the video card (revision 2) answers, start its splash, the One Off
; Endeavors logo and the VINTAGE FAST stamp, built from the card's own ROM in
; 5 s, after which the card goes back to its layers by itself. If the sound
; card answers too, start the splash's sound with it, on the card's 6510 from
; the vertical blank where the splash starts, so the sound's frame n plays
; with the picture's frame n:
;
;   * revision 2 or later ($A2 up): the ES5503. Its samples, the first
;     80 KB of wave RAM as an image in flash blocks 24-28 (splash/wave.bin),
;     go in through DOCDATA, a block at a time through W1; the player
;     (splash/docsnd.bin) goes in at $1000; DOCCTL RUN gives the 6510 the
;     chip at $DF00. About 0.13 s, before the splash starts.
;   * revision 1: the SIDs, splash/bootsnd.bin at $1000.
;
; Either way the card's reset vector points at the player's boot entry.
;
; The BIOS doesn't wait for either. The monitor is up behind the splash (the
; serial line has it at once, the screen when the splash ends), and the
; player stops the card itself when it's over ($DE02 in the card's map: the
; ES5503's writes 1 there, which stops the chip too), so the card is the
; host's again, as after a reset. Break (QUIET) stops both.

.setcpu "65C02"
.include "backplane.inc"
.export SPLASH

PTR       = $00                        ; copy pointer: bios.s's SRC, free by now
LEFT      = $02                        ; bytes left to copy: bios.s's DST, free too
TUNE_AT   = $1000                      ; in the card's 64 KB (bootsnd.cfg)
TUNE_BOOT = $1006                      ; its entry as the card's whole program
WAVE_BLOCK = 24                        ; flash blocks 24-28: wave RAM's image (bios.cfg)
WAVE_BLOCKS = 5
W1        = $4000                      ; where W1 shows a block

.segment "SPLASH"

SPLASH:
                LDA     VIDEO_ID
                CMP     #VIDEO_ID_V2
                BEQ     :+
                RTS                    ; no revision 2 video card: no splash, and no sound for it
:
                LDA     SOUND_ID
                AND     #$F0           ; the sound card at any revision: each runs the 6510 the same
                CMP     #SOUND_TYPE
                BEQ     @card
                JMP     @video         ; no sound card: the picture alone

; The player into the card, stopped: CTRL RUN 0 also gives the host the SIDs.
@card:          STZ     SOUND_CTRL
                LDA     SOUND_ID
                CMP     #SOUND_ID_V2
                BCS     @doc
                JMP     @sids          ; revision 1: the SIDs
@doc:

                LDA     #SOUND_DOC_AUTOINC | SOUND_DOC_RAM ; the ES5503: wave RAM, from 0
                STA     SOUND_DOCCTL
                STZ     SOUND_DOCADDR
                STZ     SOUND_DOCADDR + 1
                STZ     SOUND_DOCBANK
                LDA     NB_WIN1
                PHA
                LDX     #WAVE_BLOCK
@block:         TXA
                ORA     #NB_ROM | NB_RO
                STA     NB_WIN1        ; the next 16 KB of the image
                STZ     PTR
                LDA     #>W1
                STA     PTR + 1
                LDY     #0
@wave:
.repeat 8                              ; eight at a time: about 11 cycles a byte
                LDA     (PTR),Y
                STA     SOUND_DOCDATA  ; DOCADDR moves on, into the bank
                INY
.endrepeat
                BNE     @wave
                INC     PTR + 1
                LDA     PTR + 1
                CMP     #>(W1 + $4000)
                BNE     @wave
                INX
                CPX     #WAVE_BLOCK + WAVE_BLOCKS
                BNE     @block
                PLA
                STA     NB_WIN1
; Every oscillator halted before the 6510 has the chip: after C800R its
; registers are as the last program left them (QUIET only froze it), and the
; player halts only the 24 it uses, a frame after RUN.
                LDA     #SOUND_DOC_AUTOINC ; the registers, from $A0: the 32 controls
                STA     SOUND_DOCCTL
                LDA     #$A0
                STA     SOUND_DOCADDR
                STZ     SOUND_DOCADDR + 1
                LDA     #$01           ; HALT
                LDX     #32
@halt:          STA     SOUND_DOCDATA
                DEX
                BNE     @halt
                LDA     #SOUND_DOC_RUN ; the chip's registers, at the 6510's $DF00
                STA     SOUND_DOCCTL
                LDA     #<DOCTUNE
                LDX     #>DOCTUNE
                LDY     #<(DOCTUNE_END - DOCTUNE)
                STY     LEFT
                LDY     #>(DOCTUNE_END - DOCTUNE)
                JMP     @load
@sids:          LDA     #<TUNE
                LDX     #>TUNE
                LDY     #<(TUNE_END - TUNE)
                STY     LEFT
                LDY     #>(TUNE_END - TUNE)
@load:          STA     PTR            ; A, X: where; LEFT, Y: how much
                STX     PTR + 1
                STY     LEFT + 1
                STZ     SOUND_RAMADDR
                LDA     #>TUNE_AT
                STA     SOUND_RAMADDR + 1
                LDY     #0
                LDX     LEFT + 1       ; whole pages
                BEQ     @part
@page:          LDA     (PTR),Y
                STA     SOUND_RAMDATA  ; RAMADDR moves on by itself
                INY
                BNE     @page
                INC     PTR + 1
                DEX
                BNE     @page
@part:          LDX     LEFT           ; and the rest
                BEQ     @vector
@rest:          LDA     (PTR),Y
                STA     SOUND_RAMDATA
                INY
                DEX
                BNE     @rest
@vector:        LDA     #$FC           ; the card's reset vector: the player's boot entry
                STA     SOUND_RAMADDR
                LDA     #$FF
                STA     SOUND_RAMADDR + 1
                LDA     #<TUNE_BOOT
                STA     SOUND_RAMDATA
                LDA     #>TUNE_BOOT
                STA     SOUND_RAMDATA

@video:         LDA     #VIDEO_SPLASH_ON
                STA     VIDEO_SPLASH   ; latched: it starts at the next vertical blank
                LDA     SOUND_ID
                AND     #$F0
                CMP     #SOUND_TYPE
                BNE     @done
@out:           BIT     VIDEO_STATUS   ; a vertical blank under way: wait it out
                BMI     @out
@in:            BIT     VIDEO_STATUS   ; the next one: the splash's frame 0 begins
                BPL     @in
                LDA     #SOUND_NTSC | SOUND_RUN | SOUND_RESET
                STA     SOUND_CTRL     ; the tune's frame 0, now
@done:          RTS

TUNE:           .incbin "splash/bootsnd.bin"      ; the SIDs' (revision 1)
TUNE_END:
DOCTUNE:        .incbin "splash/docsnd.bin"       ; the ES5503's (revision 2)
DOCTUNE_END:

.segment "WAVE"
                .incbin "splash/wave.bin"         ; wave RAM's image: flash blocks 24-28

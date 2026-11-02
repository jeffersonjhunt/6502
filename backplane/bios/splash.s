; The boot splash (hw/design.md, "Boot splash").
;
; SPLASH, at boot: if the video card answers, start its splash, the One Off
; Endeavors logo and the VINTAGE FAST stamp, built from the card's own ROM in
; 5 s, after which the card goes back to its layers by itself. If the sound
; card answers too, start the splash's sound with it: load splash/bootsnd.bin
; into the card at $1000, point the card's reset vector at its boot entry,
; and start the card's 6510 at the vertical blank where the splash starts,
; so the tune's frame n plays with the picture's frame n.
;
; The BIOS doesn't wait for either. The monitor is up behind the splash (the
; serial line has it at once, the screen when the splash ends), and the tune
; stops the card's 6510 itself when it's over ($DE02 in the card's map), so
; the SIDs are the host's again, as after a reset. Break (QUIET) stops both.

.setcpu "65C02"
.include "backplane.inc"
.export SPLASH

PTR       = $00                        ; copy pointer: bios.s's SRC, free by now
TUNE_AT   = $1000                      ; in the card's 64 KB (bootsnd.cfg)
TUNE_BOOT = $1006                      ; its entry as the card's whole program

.segment "SPLASH"

SPLASH:
                LDA     VIDEO_ID
                CMP     #VIDEO_ID_V1
                BNE     @done          ; no video card: no splash, and no sound for it
                LDA     SOUND_ID
                CMP     #SOUND_ID_V1
                BNE     @video

; The tune into the card, stopped: CTRL RUN 0 also gives the host the SIDs.
                STZ     SOUND_CTRL
                STZ     SOUND_RAMADDR
                LDA     #>TUNE_AT
                STA     SOUND_RAMADDR + 1
                LDA     #<TUNE
                STA     PTR
                LDA     #>TUNE
                STA     PTR + 1
                LDY     #0
                LDX     #>(TUNE_END - TUNE) ; whole pages
                BEQ     @part
@page:          LDA     (PTR),Y
                STA     SOUND_RAMDATA  ; RAMADDR moves on by itself
                INY
                BNE     @page
                INC     PTR + 1
                DEX
                BNE     @page
@part:          LDX     #<(TUNE_END - TUNE) ; and the rest
                BEQ     @vector
@rest:          LDA     (PTR),Y
                STA     SOUND_RAMDATA
                INY
                DEX
                BNE     @rest
@vector:        LDA     #$FC           ; the card's reset vector: the tune's boot entry
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
                CMP     #SOUND_ID_V1
                BNE     @done
@out:           BIT     VIDEO_STATUS   ; a vertical blank under way: wait it out
                BMI     @out
@in:            BIT     VIDEO_STATUS   ; the next one: the splash's frame 0 begins
                BPL     @in
                LDA     #SOUND_NTSC | SOUND_RUN | SOUND_RESET
                STA     SOUND_CTRL     ; the tune's frame 0, now
@done:          RTS

TUNE:           .incbin "splash/bootsnd.bin"
TUNE_END:

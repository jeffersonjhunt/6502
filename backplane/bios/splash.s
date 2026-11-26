; The boot splash (hw/design.md, "Boot splash").
;
; SPLASH, at boot: if the video card (revision 2) answers, start its splash, the One Off
; Endeavors logo and the VINTAGE FAST stamp, built from the card's own ROM in
; 5 s, after which the card goes back to its layers by itself. If a revision 3
; sound card answers too, its boot sound plays with it: the card's firmware
; plays it from the card's own flash (the samples and the sequence revision 2's
; ES5503 player had), begun by one record, BOOTSOUND, at the vertical blank
; where the splash starts, so the sound's frame n plays with the picture's
; frame n (hw/future/sound-device.md, "Boot, reset and Apple II mode").
;
; CTRL's RESET first, so whatever a program left playing before C800R stops and
; a record it left half written is dropped; then READY waited for, 250 ms at
; most (power-on brings it within 100 ms). With no READY by then the splash
; runs silent, as it does on revisions 1 and 2, which the ROM doesn't drive.
;
; The BIOS doesn't wait for the sound, nor for BOOTSOUND's reply (tag STAG,
; left in the event FIFO: OODOS's cold boot empties it). The monitor is up
; behind the splash (the serial line has it at once, the screen when the
; splash ends), and the sound ends by itself. Break (QUIET) stops both.

.setcpu "65C02"
.include "backplane.inc"
.import SWAIT
.export SPLASH

PLAYS     = $00                        ; $FF: the card is up, BOOTSOUND to go (bios.s's SRC, free by now)
STAG      = $FE                        ; BOOTSOUND's tag ($C818's is $FF)

.segment "SPLASH"

SPLASH:
                LDA     VIDEO_ID
                CMP     #VIDEO_ID_V2
                BEQ     :+
                RTS                    ; no video card: no splash, and no sound for it
:               STZ     PLAYS
                LDA     SOUND_ID
                CMP     #SOUND_ID_V3
                BNE     @video         ; no revision 3 sound card: the picture alone
                LDA     #SOUND_CTRL_RESET
                STA     SOUND_CTRL
                LDA     #SOUND_ST_READY
                LDY     #250
                JSR     SWAIT
                LDA     #0
                SBC     #0             ; $FF if READY came (carry clear), else 0
                STA     PLAYS

@video:         LDA     #VIDEO_SPLASH_ON
                STA     VIDEO_SPLASH   ; latched: it starts at the next vertical blank
                LDA     PLAYS
                BEQ     @done
@out:           BIT     VIDEO_STATUS   ; a vertical blank under way: wait it out
                BMI     @out
@in:            BIT     VIDEO_STATUS   ; the next one: the splash's frame 0 begins
                BPL     @in
                LDA     #SOUND_BOOTSOUND ; the boot sound's frame 0, now: n 0
                STA     SOUND_CMD
                STZ     SOUND_CMD
                LDA     #STAG
                STA     SOUND_CMD
@done:          RTS

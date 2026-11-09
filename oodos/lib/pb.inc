; OOLIB's sound modules' own (lib/pb*.s, L5): the sound card's registers
; they read or write (Technical Reference chapter 7; backplane.inc's
; SOUND_* names, which programs don't include), and what the modules share.
; The interface is include/oolib.inc's.

.ifndef PB_INC
PB_INC = 1

SND             = $C500                ; the sound card
SND_EQIDX       = SND + $59            ; 0 the preamp, 1-10 the bands; bit 7: EQVAL is the enable
SND_EQVAL       = SND + $5A            ; the gain in half-dB, +-24, or the enable; EQIDX moves on
SND_VISIDX      = SND + $5B            ; the vis page's index: 0 written takes a snapshot
SND_VISDATA     = SND + $5C            ; the byte there, and the index moves on
SND_ID          = SND + $7F            ; high nibble the module ($A0), low the revision
SND_TYPE        = $A0
SND_EQ_ENABLE   = $80
SND_VIS_BYTES   = 98                   ; 32 bands, the VU pair, 64 samples
EQ_GAIN_MAX     = 24                   ; half-dB, either way
EQ_BANDS        = 10

; pbcard.s's: carry clear with a revision-2 card or later (A, X, Y kept);
; carry set and A = PE_NOCARD without one
.global PBCARD

.endif

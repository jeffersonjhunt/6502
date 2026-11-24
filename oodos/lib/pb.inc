; OOLIB's sound modules' own (lib/pb*.s, L5; revision 3, the card as a
; device: every one through function 82, none touching the card), and what
; the modules share. The interface is include/oolib.inc's.

.ifndef PB_INC
PB_INC = 1

SND_VIS_BYTES   = 98                   ; 32 bands, the VU pair, 64 samples
EQ_GAIN_MAX     = 24                   ; half-dB, either way
EQ_BANDS        = 10

; pbcard.s's: carry clear with a revision 3 card that OODOS drives (A kept);
; carry set and A = PE_NOCARD without one. PBF82: function 82 with A/X a
; block, Y kept nothing: Z set if done (A = 0)
.global PBCARD, PBF82

; pb.s's, for pbstat.s and pblev.s: the source begun (PB_SID + 1 ...; 0
; none), a count PB_BEGIN moves on, the event area, paused, and the ms
; counter's times (4 bytes each): the begin, the pause, the paused time
; summed, and now, which PBNOW reads
.global PBSRC, PBGEN, PBAREA, PBHELD, PBTBEGIN, PBTPAUSE, PBTAWAY, PBTNOW, PBNOW

; pbmath.s's: PBMUL A x X into PBP (16 bits); PBDIV PBP / A into A (the
; quotient's low byte; PBP's high byte must be under A)
.global PBMUL, PBDIV
.globalzp PBP

.endif

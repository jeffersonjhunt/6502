; OOLIB's music modules' own (lib/mu*.s, L6b): what mu.s shares with
; munote.s and mutone.s. The interface is include/oolib.inc's; the driver's
; protocol is lib/mudrv/mudrv.s's.

.ifndef MU_INC
MU_INC = 1

MU_BASE         = $1000                ; the driver's place in the card's RAM
MU_VOICES       = 6
MU_QLEN         = 32                   ; entries a voice
MU_MARKS        = 15                   ; the library's own marker (STOP's); a program's are 0-14

; mu.s's: MUQ queues voice A's entry (MUF the frequency word, MUL the
; length, MUG the gate time; carry and A as the calls'); MUCLK 0 PAL, 1
; NTSC; MUP a zero-page pointer for a block
.global MUQ, MUF, MUL, MUG, MUCLK
.globalzp MUP

.endif

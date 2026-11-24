; OOLIB's music modules' own (lib/mu*.s, L6b; revision 3, the card's music
; sequencer): what mu.s shares with munote.s and mutone.s. The interface is
; include/oolib.inc's; the note block's layout the card's (the Technical
; Reference, chapter 7).

.ifndef MU_INC
MU_INC = 1

MU_VOICES       = 6
MU_SLOTS        = 8                    ; the card's music slots: blocks playing or queued
MU_NOTE0        = 12                   ; MU_NOTE's note 0 (C-0) as the block's note number (A-4 57: 69)

; mu.s's: MUQ sends voice A's entry MUE (MUELEN bytes, its kind in +0's high
; nibble; the voice is put in its low) as a one-entry block, queued (carry
; and A as the calls'); MUTIME puts the length and the gate of MUP's block
; (+3-4, +5-6, ms) at MUE + X as µs, 4 bytes each (X 4 on: the length then
; the gate; with carry clear the length alone: a rest's); MUCLK 0 PAL, 1
; NTSC (MU_INIT's, kept); MUP a zero-page pointer for a block
.global MUQ, MUE, MUELEN, MUTIME, MUCLK
.globalzp MUP

.endif

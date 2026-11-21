; MACLNG.ASM's macro language driver, assembled a second time, into block P7
; for PLAY (7f's plan; the space plan's answer 4): one source, p3/maclng.s
; (DRAW's, in P3), two copies, each with its own state in its own block's
; BRAM, so PLAY needs no crossing to P3 for each command.

.include "p3/maclng.s"

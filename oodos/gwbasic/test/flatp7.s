; The flat test image's stand-in for block P7 (the project's own; hw/oodos.md,
; "The plan, phase 7": 7f). The flat image (test/flat.cfg) runs on the bare
; W65C02S core with no OODOS, so P7, the sound card's music, the clock and
; the controllers, is left out of it (the Makefile's FLAT_SRC) and its
; entries come here, each an RTS.

.include "gwbasic.inc"

.export SNDINI, SNDRST, EVP7, TRPPOL, JOYFP, XFUNP, SOUNDP, BEEPP, BELLP, PLAYP

.code

SNDINI:
SNDRST:
EVP7:
TRPPOL:
JOYFP:
XFUNP:
SOUNDP:
BEEPP:
BELLP:
PLAYP:          RTS

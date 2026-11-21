; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Block P7 (hw/oodos.md, "The plan, phase 7": 7f, sound, input and events;
; the space plan's answer 8: the eighth block, allocated at 7f). It links
; OOLIB's music (../lib/mu.s, mutone.s, physof.s: the Makefile's LIB_p7),
; its zero page word MUP gwbasic.inc's alias of GXQ, exported here. This
; file holds GWSTS.ASM's sound reset, the OEM's part on the music driver:
;
;   SNDINI    CLEARC's: PLAY's state to its defaults, and SNDRST
;   SNDRST    the break's: the music stopped
;
; U0: both stubs, an RTS each; U2 fills them.

.include "gwbasic.inc"

;@entry SNDINI
;@entry SNDRST
.export SNDINI, SNDRST
.exportzp MUP                          ; mu.s's and mutone.s's (lib/zpmu.s's), at gwbasic.inc's address

.code

SNDINI:         RTS
SNDRST:         RTS

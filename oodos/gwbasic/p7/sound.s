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
; And the resident core's and the home block's hand-offs (7f's U1), each a
; stub until its unit:
;
;   EVP7      EVPOLL's: an event that isn't a key's, Y its slot's offset in
;             the queue (EVAREA + EV_QUEUE + Y; EK_SOUND: SNDEV; EK_CTL:
;             STREV; the rest dropped); dropped for now
;   TRPPOL    TRPOLL's, every POLLN statements while POLSRC is nonzero: the
;             test-only trigger (bit 7: P3's TRPTSP), then the polled sources
;             armed (ON TIMER, ON COM(1)), none for now
;   JOYFP     PEN(n), STICK(n), STRIG(n) from FUNDSP (the resident JOYF), X the
;             function as FINGO has it: Illegal function call for now
;   XFUNP     the home block's ERDEVP: DATE$, TIME$ (Advanced Feature for
;             now), PLAY(n), TIMER (Syntax error for now), A the token
;
; U0: SNDINI and SNDRST stubs, an RTS each; U2 fills them.

.include "gwbasic.inc"

;@entry SNDINI
;@entry SNDRST
;@entry EVP7
;@entry TRPPOL
;@entry JOYFP
;@entry XFUNP
.export SNDINI, SNDRST, EVP7, TRPPOL, JOYFP, XFUNP
.import FCERR, SNERR, ADVERR
.exportzp MUP                          ; mu.s's and mutone.s's (lib/zpmu.s's), at gwbasic.inc's address

.code

SNDINI:         RTS
SNDRST:         RTS
EVP7:           RTS
TRPPOL:         BIT     POLSRC         ; the test-only trigger: P3's
                BPL     :+
                FJSR    TRPTSP
:               RTS
JOYFP:          JMP     FCERR
XFUNP:          CMP     #DATES_XS
                BEQ     :+
                CMP     #TIMES_XS
                BEQ     :+
                JMP     SNERR
:               JMP     ADVERR

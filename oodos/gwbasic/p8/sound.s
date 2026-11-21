; The home block's stubs for 7f's statements (hw/oodos.md, "The plan, phase
; 7": 7f, sound, input and events; block P7's), the project's own. In a file
; of its own, the home block's last, so its entry comes after the others
; (tools/gwgen.py numbers them by file, then by line):
;
;   HPENST    PEN and STRIG as statements (the resident NEWSTX's, TXTPTR at
;             the function's byte; GWSTS.ASM's PENS and STRIGS): PEN ON, OFF
;             or STOP (both modes: the owner's answer 4) by P3's SETEV1 on
;             PENOFF (the source's RDPEN call has no light pen to switch:
;             none, 7e4's answer 1); STRIG(n) ON, OFF or STOP by P3's SEVSTT,
;             STRIG ON and STRIG OFF taken and doing nothing (the source's),
;             both Advanced Feature in console mode (answer 4)
;   SOUND     SOUND x,y: P7's SOUNDP in screen mode; Advanced Feature in
;             console mode (the owner's answer 4: it needs the event queue)
;   BEEP      BEEP: P7's BEEPP, in both modes (answer 3)
;   PLAYS     PLAY string: P7's PLAYP in screen mode; Advanced Feature in
;             console mode (answer 4). OO-1 (7f): PLAY ON, OFF or STOP, ON
;             PLAY(n)'s trap, by P3's SETEV1 on PLYOFF

.include "gwbasic.inc"

;@entry HPENST
.export HPENST, SOUND, BEEP, PLAYS
.import ADVERR, SCRMOD, CHRGTR, SNERR

.code

HPENST:         LDA     (TXTPTR)
                CMP     #PEN_FN
                BEQ     @pen
                BIT     SCRMOD         ; STRIG: screen mode only
                BPL     SADV
                JSR     CHRGTR
                CMP     #'('
                BNE     @onoff
                LDA     #NMSTRT        ; STRIG(n): SEVSTT's class
                STA     RCL
                LDA     #STROFF
                STA     RCH
                FJSR    SEVSTT
                RTS
@onoff:         CMP     #ON_TK         ; STRIG ON, STRIG OFF: nothing more
                BEQ     :+
                CMP     #OFF_TK
                BNE     SSNERR
:               JSR     CHRGTR
                BNE     SSNERR
                RTS
@pen:           LDX     #PENOFF
; EVSTM: X the event; TXTPTR before its ON, OFF or STOP: the token, the
; statement's end, then SETEV1 (Syntax error for another token).
EVSTM:          PHX
                JSR     CHRGTR
                BEQ     SSNERR         ; nothing: Syntax error
                PHA
                JSR     CHRGTR         ; the statement's end
                BNE     SSNERR
                PLA
                PLX
                FJSR    SETEV1
                RTS
SSNERR:         JMP     SNERR

SOUND:          BIT     SCRMOD
                BPL     SADV
                FJSR    SOUNDP
                RTS
SADV:           JMP     ADVERR
BEEP:           FJSR    BEEPP
                RTS
PLAYS:          BIT     SCRMOD
                BPL     SADV
                CMP     #ON_TK         ; PLAY ON, OFF, STOP: its trap
                BEQ     :+
                CMP     #OFF_TK
                BEQ     :+
                CMP     #STOP_TK
                BNE     @play
:               DECW    TXTPTR         ; (EVSTM takes the token by CHRGTR)
                LDX     #PLYOFF
                BRA     EVSTM
@play:          FJSR    PLAYP
                RTS

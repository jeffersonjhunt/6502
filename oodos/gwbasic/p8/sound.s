; The home block's stubs for 7f's statements (hw/oodos.md, "The plan, phase
; 7": 7f, sound, input and events; block P7's), the project's own. In a file
; of its own, the home block's last, so its entry comes after the others
; (tools/gwgen.py numbers them by file, then by line):
;
;   HPENST    PEN and STRIG as statements (the resident NEWSTX's, TXTPTR at
;             the function's byte): U1 a stub, Advanced Feature as before
;   SOUND     SOUND x,y: P7's SOUNDP in screen mode; Advanced Feature in
;             console mode (the owner's answer 4: it needs the event queue)
;   BEEP      BEEP: P7's BEEPP, in both modes (answer 3)
;   PLAYS     PLAY string: P7's PLAYP in screen mode; Advanced Feature in
;             console mode (answer 4)

.include "gwbasic.inc"

;@entry HPENST
.export HPENST, SOUND, BEEP, PLAYS
.import ADVERR, SCRMOD

.code

HPENST:         JMP     ADVERR

SOUND:          BIT     SCRMOD
                BPL     SADV
                FJSR    SOUNDP
                RTS
SADV:           JMP     ADVERR
BEEP:           FJSR    BEEPP
                RTS
PLAYS:          BIT     SCRMOD
                BPL     SADV
                FJSR    PLAYP
                RTS

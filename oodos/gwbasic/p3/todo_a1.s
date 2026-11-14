; A stand-in of step a1 (7b), the project's own: what P3's ON reaches that
; is step e's (the event traps), until step e translates it (ledger.txt):
;
;   ONGOTP    GWSTS.ASM's test for ON event GOSUB (ON KEY(n), ON COM(n),
;             ON PEN, ON STRIG(n), ...): here nothing is an event, so C is
;             set and A the character at TXTPTR, as ONGOTP returns for
;             anything else, and ON's numeric form goes on

.include "gwbasic.inc"

.export ONGOTP

.code

ONGOTP:         LDA     (TXTPTR)
                SEC
                RTS

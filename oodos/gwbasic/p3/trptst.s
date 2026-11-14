; The event traps' test-only trigger, the project's own (7b step e): no
; source raises an event trap before 7c-7f, so test_gwbasic_stmts raises
; them through TRPTST (gwbasic.inc), a resident zero page byte it writes by
; its label (gwbasic.lbl) at a chosen statement, with TRPON nonzero, so that
; NEWSTT's poll (res/gwmain.s TRPOLL) runs and calls this:
;
;   TRPTSP    TRPTST cleared, and TRPON counted again from the table
;             (TRPCNT), since the test wrote it; then TRPTST's operation on
;             its event, as a source or a statement would do it, which keeps
;             TRPON itself (ONTRP, OFFTRP), as it will for 7f's statements
;
; TRPTST:
;
;   bit 5     set: a trigger (so that a request of event 0, the pen, isn't
;             TRPTST's 0, "nothing")
;   bits 7-6  00 a request, as a source raises one (EVTRP); 01 ON, 10 OFF,
;             11 STOP, as KEY(n) ON, OFF and STOP set a trap (EVSET)
;   bits 4-0  the event's id (gwbasic.inc: PENOFF, KEYOFF + n - 1, COMOFF +
;             n - 1, STROFF + n / 2)
;
; 7d-7f's sources replace the trigger's place in the poll; this goes then.

.include "gwbasic.inc"

;@entry TRPTSP
.export TRPTSP
.import EVTRP, EVADR, EVSET, TRPCNT

.code

TRPTSP:         LDA     TRPTST
                STZ     TRPTST
                PHA
                JSR     TRPCNT         ; TRPON as the table has it
                PLA
                TAY                    ; Y: the operation
                AND     #$1F           ; A: the event's id
                CPY     #$40
                BCS     @set
                JMP     EVTRP          ; 00: the event has happened
@set:           JSR     EVADR          ; DX: the event's flag
                TYA                    ; 1-3: ON, OFF, STOP
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                TAX
                LDA     TSTOPS - 1,X
                JMP     EVSET

.rodata
TSTOPS:         .byte   ON_TK, OFF_TK, STOP_TK

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Block P7's events (hw/oodos.md, "The plan, phase 7": 7f), the hand-offs
; from the resident core and the home block (their entries are listed in
; p7/sound.s, so the jump table's order is as U1 made it):
;
;   EVP7      EVPOLL's: an event that isn't a key's, Y its slot's offset in
;             the queue (EVAREA + EV_QUEUE + Y). EK_SOUND: SNDEV, its byte to
;             MU_EVENT (the voices' counts), and ON PLAY(n)'s request when
;             the notes waiting in MB fall from n to n - 1 (OO-1: the source
;             has no PLAY event); EK_CTL: STREV; the rest dropped
;   STREV     a controller's change (EC_BITS, controllers 1 and 2): each
;             fire button newly pressed latched for STRIG(0), (2), (4), (6)
;             (pressed since the last read) and its trap requested (P3's
;             EVTRP: GWSTS.ASM's POLSTR, on the events)
;   TRPPOL    TRPOLL's, every POLLN statements while POLSRC is nonzero: the
;             test-only trigger (bit 7: P3's TRPTSP), ON COM(1)'s byte
;             waiting (bit 1: P4's COMRDY; GIOCOM.ASM's POLCOM)
;   ONSETP    ON PLAY(n) and ON TIMER(n) (P3's ONGOTP): A 0 PLAY, n 1-32
;             (the manual's p. 211), kept for SNDEV; A the event's id back
;   JOYFP     PEN(n), STICK(n), STRIG(n) from FUNDSP (the resident JOYF), X
;             the function as FINGO has it: PEN(n) GWSTS.ASM's PENF, n 0-9,
;             the OEM's RDPEN reporting no pen (0; PEN(0) the trap's
;             request tested and cleared, TSTCEV), past 9 Illegal function
;             call (7e4's answer 1); STICK, STRIG later (U6)
;   XFUNP     the home block's ERDEVP: PLAY(n) (p7/play.s PLAYN); DATE$,
;             TIME$ (Advanced Feature for now), TIMER (Syntax error for
;             now), A the token
;
; The far call rule: each returns by RTS, or ends in an error.

.include "gwbasic.inc"

.export EVP7, TRPPOL, JOYFP, XFUNP, ONSETP, STRLAT
.import FCERR, SNERR, ADVERR, CONINT, MAKINT, EVAREA, PLAYN, MMODE
.import MU_EVENT, MU_COUNT             ; (oolib.inc not included: its .globalzp would export gwbasic.inc's GXP again)

CB_FIRE2        = CB_FIRE * 2          ; the second fire button (the DE-9 sticks have one)

.code

; --- EVP7, SNDEV, STREV --------------------------------------------------------------------

EVP7:           LDA     EVAREA + EV_QUEUE + EQ_KIND,Y
                CMP     #EK_CTL
                BEQ     STREV
                CMP     #EK_SOUND
                BNE     EVPX
; SNDEV: the count before and after MU_EVENT; in MB with ON PLAY(n) given,
; n to n - 1 is the request (EVTRP tests the trap's T_ON).
SNDEV:          LDA     #0
                JSR     MU_COUNT
                STA     SNB4
                LDA     EVAREA + EV_QUEUE + EQ_VALUE,Y
                JSR     MU_EVENT
                LDA     MMODE          ; MB only
                BEQ     EVPX
                LDA     PLYN           ; ON PLAY(n) given
                BEQ     EVPX
                CMP     SNB4
                BNE     EVPX
                LDA     #0
                JSR     MU_COUNT
                INC     A              ; n - 1 now?
                CMP     SNB4
                BNE     EVPX
                LDA     #PLYOFF
                FJSR    EVTRP
EVPX:           RTS

STREV:          LDA     EVAREA + EV_QUEUE + EQ_CODE,Y
                DEC     A              ; controllers 1 and 2: 0, 1
                CMP     #2
                BCS     EVPX
                TAX
                LDA     EVAREA + EV_QUEUE + EQ_VALUE,Y
                PHA
                EOR     CTLB,X         ; changed,
                STA     STT
                PLA
                STA     CTLB,X
                AND     STT            ; and down now: pressed
                STA     STT
                AND     #CB_FIRE       ; STRIG(0) or (2)
                BEQ     :+
                TXA
                JSR     STRHIT
:               LDA     STT
                AND     #CB_FIRE2      ; STRIG(4) or (6)
                BEQ     EVPX
                TXA
                ORA     #2
; STRHIT: trigger A (0-3: STRIG(2A)) pressed: latched, and its trap asked.
STRHIT:         PHX
                TAX
                LDA     #$FF
                STA     STRLAT,X
                TXA
                CLC
                ADC     #STROFF
                FJSR    EVTRP
                PLX
                RTS

; --- TRPPOL, ONSETP ------------------------------------------------------------------------

TRPPOL:         BIT     POLSRC         ; the test-only trigger: P3's
                BPL     :+
                FJSR    TRPTSP
:               LDA     POLSRC         ; ON COM(1): a byte waiting
                AND     #PS_COM
                BEQ     :+
                FJSR    COMRDY
                BCC     :+
                LDA     #COMOFF
                FJSR    EVTRP
:               RTS

ONSETP:         CMP     #0
                BNE     @tim
                JSR     CONINT         ; PLAY(n): 1-32
                CMP     #1
                BCC     @fc
                CMP     #MUQMAX + 1
                BCS     @fc
                STA     PLYN
                LDA     #PLYOFF
                RTS
@tim:           JMP     SNERR          ; (TIMER: U5)
@fc:            JMP     FCERR

; --- JOYFP ---------------------------------------------------------------------------------

; PENF (GWSTS.ASM 1640): ONESUB's n, 0-9, else Illegal function call; PEN(0)
; the trap's request tested and cleared (TSTCEV), PEN(1)-(9) the OEM's RDPEN:
; no pen, 0. As FUNDSP's: the argument in the FAC, the value back in it.
JOYFP:          CPX     #2 * (PEN_FN - ONEFUN)
                BNE     @fc            ; STICK, STRIG: U6
                JSR     CONINT
                CMP     #10
                BCS     @fc
                STZ     RBL            ; RDPEN: 0
                STZ     RBH
                CMP     #0
                BNE     :+
                LDA     #PENOFF        ; PEN(0): TSTCEV's -1 or 0 in BX
                FJSR    TSTCEV
:               JMP     MAKINT
@fc:            JMP     FCERR

XFUNP:          CMP     #PLAY_XS
                BNE     :+
                JMP     PLAYN
:               CMP     #DATES_XS
                BEQ     :+
                CMP     #TIMES_XS
                BEQ     :+
                JMP     SNERR
:               JMP     ADVERR

MUQMAX          = 32                   ; the driver's queue a voice (lib/mu.inc's MU_QLEN)

.segment "BRAM"
SNB4:           .res    1              ; SNDEV's count before
PLYN:           .res    1              ; ON PLAY(n)'s n, 0 none
CTLB:           .res    2              ; controllers 1 and 2's EC_BITS as last seen
STT:            .res    1              ; STREV's
STRLAT:         .res    4              ; STRIG(0), (2), (4), (6): pressed since the last read ($FF)

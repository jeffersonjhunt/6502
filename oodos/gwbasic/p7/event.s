; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Block P7's events (hw/oodos.md, "The plan, phase 7": 7f), the hand-offs
; from the resident core and the home block (their entries are listed in
; p7/sound.s, so the jump table's order is as U1 made it):
;
;   EVP7      EVPOLL's: an event that isn't a key's, Y its slot's offset in
;             the queue (EVAREA + EV_QUEUE + Y). EK_SOUND: SNDEV, ON PLAY(n)'s
;             request when the notes in MB fall from n to n - 1 (the card's
;             MUSIC_COUNT, or MUSIC_DONE for n = 1; OO-1: the source has no
;             PLAY event); EK_CTL: STREV; the rest dropped
;   STREV     a controller's change (EC_BITS, controllers 1 and 2): each
;             fire button newly pressed: its trap requested (P3's EVTRP:
;             GWSTS.ASM's POLSTR, on the events) and, if that didn't trap
;             it, latched for STRIG(0), (2), (4), (6) (pressed since the last
;             read: the manual's p. 333)
;   TRPPOL    retired: an RTS (the owner's answers 23-24: ON TIMER by the tick
;             hook, ON COM's trap deferred until AUX: has a receive buffer)
;   ONSETP    ON PLAY(n) and ON TIMER(n) (P3's ONGOTP): A 0 PLAY, n 1-32
;             (the manual's p. 211), kept for SNDEV and the card's threshold
;             set for it at once if music has started (TH0); A 1 TIMER
;             (p7/time.s TIMSET); A the event's id back
;   PLONP     PLAY ON's (the home block's PLAYS, after P3's SETEV1): with
;             nothing in the buffer (GWCNT 0), a trap remembered under PLAY
;             STOP (or still pending) dropped, not taken (the manual's p.
;             212, its note 2: the owner's answer 6 to branch C's plan; PLAY
;             OFF remembers nothing)
;   JOYFP     PEN(n), STICK(n), STRIG(n) from FUNDSP (the resident JOYF), X
;             the function as FINGO has it: PEN(n) GWSTS.ASM's PENF, n 0-9,
;             the OEM's RDPEN reporting no pen (0; PEN(0) the trap's
;             request tested and cleared, TSTCEV), past 9 Illegal function
;             call (7e4's answer 1); STICK(n) (STICKF, the OEM's RDSTIK on
;             EV_CTL): STICK(0) latches controllers 1 and 2's X and Y (0-255:
;             a digital stick 0, 128, 255) and gives 1's X, STICK(1)-(3) the
;             latched 1's Y, 2's X and Y, past 3 Illegal function call;
;             STRIG(n) (STRIGF, the OEM's RDTRIG): an even n pressed since the
;             last read (STREV's latch, cleared), an odd n down now
;             (EC_BITS), STRIG(0)-(3) the controllers' first fire button,
;             (4)-(7) their second (the DE-9 sticks have none: 0), past 7
;             Illegal function call. STICK and STRIG Advanced Feature in
;             console mode (the owner's answer 4: no event area)
;   XFUNP     the home block's ERDEVP, A the token: PLAY(n) (p7/play.s
;             PLAYN), TIMER, DATE$ and TIME$ (p7/time.s TIMEP, DATEF, TIMEF)
;
; The far call rule: each returns by RTS, or ends in an error.

.include "gwbasic.inc"

.export EVP7, TRPPOL, JOYFP, XFUNP, ONSETP, PLONP, STRLAT, PLYN
.import FCERR, SNERR, ADVERR, CONINT, MAKINT, EVAREA, PLAYN, MMODE, SCRMOD, TIMEP, DATEF, TIMEF, TIMSET
.import GWCNT, TH0, DRVON             ; p7/sound.s's

CB_FIRE2        = CB_FIRE * 2          ; the second fire button (the DE-9 sticks have one)

.code

; --- EVP7, SNDEV, STREV --------------------------------------------------------------------

EVP7:           LDA     EVAREA + EV_QUEUE + EQ_KIND,Y
                CMP     #EK_CTL
                BEQ     STREV
                CMP     #EK_SOUND
                BNE     EVPX
; SNDEV (OO-1, sound card revision 3): the card's threshold is ON PLAY(n)'s
; alone (p7/sound.s TH0: n - 1), so in MB with ON PLAY(n) given a
; MUSIC_COUNT (n 2-32) or a MUSIC_DONE (n = 1) is the count's fall from n to
; n - 1: the request (EVTRP tests the trap's T_ON).
SNDEV:          LDA     EVAREA + EV_QUEUE + EQ_CODE,Y
                LDX     PLYN
                CMP     #SK_MUSIC_COUNT
                BEQ     @count
                CMP     #SK_MUSIC_DONE
                BNE     EVPX
                DEX                    ; MUSIC_DONE: n = 1
                BNE     EVPX
                BRA     @mb
@count:         CPX     #2             ; MUSIC_COUNT: n 2-32
                BCC     EVPX
@mb:            LDA     MMODE          ; MB only
                BEQ     EVPX
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
; STRHIT: trigger A (0-3: STRIG(2A)) pressed: its trap asked (EVTRP), and,
; unless that trapped it, latched (the manual's p. 333: a press that a trap
; takes doesn't count for the even STRIG functions; the source's STRIGF tests
; and clears the trap's request there instead: the manual's meaning followed).
STRHIT:         PHX
                PHA
                CLC
                ADC     #STROFF
                FJSR    EVTRP          ; Z clear: trapped
                BNE     @t
                PLX
                LDA     #$FF
                STA     STRLAT,X
                PLX
                RTS
@t:             PLX                    ; (k dropped)
                PLX
                RTS

; --- TRPPOL, ONSETP ------------------------------------------------------------------------

; TRPPOL: retired (an RTS: 7f's ON TIMER is the tick hook's, ON COM deferred;
; the owner's answers 23-24): its entry stays, as a retired entry does.
TRPPOL:         RTS

ONSETP:         CMP     #0
                BNE     @tim
                JSR     CONINT         ; PLAY(n): 1-32
                CMP     #1
                BCC     @fc
                CMP     #MUQMAX + 1
                BCS     @fc
                STA     PLYN
                LDA     DRVON          ; music started: the card's threshold now
                BEQ     :+             ;   (else FLUSH's, at the first block)
                JSR     TH0
:               LDA     #PLYOFF
                RTS
@tim:           JSR     TIMSET         ; TIMER(n): 1-86,400
                LDA     #TIMOFF
                RTS
@fc:            JMP     FCERR

; PLONP: PLAY ON just done (SETEV1's): the buffer empty, the trap's request
; freed (P3's TSTCEV: after ONTRP has counted it, so ONGSBF stays right).
PLONP:          JSR     GWCNT
                CMP     #0             ; (GWCNT's flags aren't A's)
                BNE     :+
                LDA     #PLYOFF
                FJSR    TSTCEV
:               RTS

; --- JOYFP ---------------------------------------------------------------------------------

; PENF (GWSTS.ASM 1640): ONESUB's n, 0-9, else Illegal function call; PEN(0)
; the trap's request tested and cleared (TSTCEV), PEN(1)-(9) the OEM's RDPEN:
; no pen, 0. As FUNDSP's: the argument in the FAC, the value back in it.
JOYFP:          CPX     #2 * (PEN_FN - ONEFUN)
                BNE     JOYSS          ; STICK, STRIG
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

; STICKF, STRIGF (GWSTS.ASM 1771, 1813): console mode Advanced Feature.
JOYSS:          BIT     SCRMOD
                BMI     :+
                JMP     ADVERR
:               PHX
                JSR     CONINT         ; n
                PLX
                STZ     RBH
                CPX     #2 * (STICK_FN - ONEFUN)
                BNE     STRIGF
                CMP     #4             ; STICK: 0-3
                BCS     JFC
                TAY
                BNE     @lat
                LDA     EVAREA + EV_CTL + EC_X ; STICK(0): both sticks latched
                STA     STKL
                LDA     EVAREA + EV_CTL + EC_Y
                STA     STKL + 1
                LDA     EVAREA + EV_CTL + EC_LEN + EC_X
                STA     STKL + 2
                LDA     EVAREA + EV_CTL + EC_LEN + EC_Y
                STA     STKL + 3
@lat:           LDA     STKL,Y
                STA     RBL
                JMP     MAKINT
STRIGF:         CMP     #8             ; STRIG: 0-7
                BCS     JFC
                LSR     A              ; k, C: odd (now)
                TAY
                LDX     #0
                BCS     @now
                LDA     STRLAT,Y       ; pressed since: the latch, cleared
                PHA
                LDA     #0
                STA     STRLAT,Y
                PLA
                BRA     @ret
@now:           AND     #1             ; controller k AND 1
                BEQ     :+
                LDX     #EC_LEN
:               LDA     #CB_FIRE       ; the first button, or (k 2, 3) the second
                CPY     #2
                BCC     :+
                ASL     A
:               AND     EVAREA + EV_CTL + EC_BITS,X
                BEQ     @ret
                LDA     #$FF
@ret:           STA     RBL            ; -1 or 0
                STA     RBH
                JMP     MAKINT
JFC:            JMP     FCERR

XFUNP:          CMP     #PLAY_XS
                BNE     :+
                JMP     PLAYN
:               CMP     #TIMER_XS
                BNE     :+
                JMP     TIMEP
:               CMP     #DATES_XS
                BNE     :+
                JMP     DATEF
:               JMP     TIMEF          ; (TIME$: ERDEVP's last)

.segment "BRAM"
PLYN:           .res    1              ; ON PLAY(n)'s n, 0 none
CTLB:           .res    2              ; controllers 1 and 2's EC_BITS as last seen
STT:            .res    1              ; STREV's
STRLAT:         .res    4              ; STRIG(0), (2), (4), (6): pressed since the last read ($FF)
STKL:           .res    4              ; STICK(0)'s latch: 1's X, Y, 2's X, Y

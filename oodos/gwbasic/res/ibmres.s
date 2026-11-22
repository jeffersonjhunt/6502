; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; IBMRES.ASM's resident part, translated: the extended statements' and
; functions' dispatch (the $FE and $FD tokens). The reserved word tables and
; CRUNCH's and LIST's code for the extended words are P2's (p2/ibmres.s).
;
;   NEWSTX    a statement token past LOCATE: $FE and an extended statement
;             (STMDSX, in the home block; SYSTEM is 7a's), or $FF and PEN or STRIG as a
;             statement (7e's); anything else back to GONE2's caller, for
;             MID$ = (ISMID) or Syntax error. 7f: PEN and STRIG as
;             statements are the home block's HPENST
;   EVALX     a formula's $FE DATE$, TIME$, PLAY(n) or TIMER (7f), ERDEV,
;             ERDEV$ or (7e4) PMAP (the home block's ERDEVP), or $FD and a
;             function (EVALNF: CVI to MKD$, P4's, by one far call; KTN to
;             KLEN, Kanji's, dropped)

.include "gwbasic.inc"

.export NEWSTX, EVALX
.import CHRGTR, SNERR, ADVERR, SPUSHA, SPOPA, SPUSH, PARCHK, LABBCK
; STMDSX, the extended statements' table, is the home block's (p8/home.s),
; right after STMDSP, read here in W3.
.ifdef FLAT
.import STMDSX
.else
STMDSX          = HOMTAB + 2 * NUMCMD
.endif

.code

; NEWSTX (IBMRES.ASM 901): A = the token less $81, TXTPTR at the token.
NEWSTX:         CMP     #$FE - $81     ; an extended statement?
                BEQ     GONE4
                CMP     #$FF - $81     ; a function?
                BNE     GIIRET
                INCW    TXTPTR
                LDA     (TXTPTR)
                CMP     #PEN_FN        ; PEN as a statement?
                BEQ     PENV
                CMP     #STRIG_FN      ; STRIG as a statement?
                BEQ     PENV
                DECW    TXTPTR
                LDA     #$FF - $81
GIIRET:         RTS                    ; MID$ or Syntax error
; PENV, STRIGV (7f): NEWSTX's return dropped, as GONE4 drops it; the home
; block's HPENST (TXTPTR at the function's byte) returns to NEWSTT.
PENV:           PLA
                PLA
                FJSR    HPENST
                RTS
; GONE4: NEWSTX's return dropped, the statement started as GONE2 starts
; one: its address (less one) pushed for CHRGTR's RTS. The 8086 checks the
; token only from below; a token past CALLS can't come from CRUNCH, but
; Syntax error is checked for here too, rather than a wild jump.
GONE4:          PLA
                PLA
                INCW    TXTPTR
                LDA     (TXTPTR)
                SEC
                SBC     #FILES_XS
                BCC     SNERRS
                CMP     #NUMSTX
                BCS     SNERRS
                ASL     A
                TAX
                LDA     STMDSX+1,X
                PHA
                LDA     STMDSX,X
                PHA
                JMP     CHRGTR
SNERRS:         JMP     SNERR

; EVALX (IBMRES.ASM 961): EVAL's hook. $FE then DATE$ or TIME$ (DATEF,
; TIMEF), PLAY(n) or TIMER (OO-1, 7f: the source has neither), ERDEV or
; ERDEV$ (OO-1: read-only variables), PMAP (OO-1, 7e4: the source's table
; has no PMAP function): each the home block's ERDEVP, the $FE functions'
; dispatcher (the space plan's answer 10), its value and EVAL's return; or
; $FD and a function (EVALNF); anything else back, A as it was.
EVALX:          CMP     #$FE
                BEQ     EVALNS
                CMP     #$FD
                BEQ     EVALNF
                RTS
EVALNS:         LDY     #1
                LDA     (TXTPTR),Y
                CMP     #DATES_XS      ; 7f: DATE$, TIME$, PLAY(n), TIMER
                BEQ     :+
                CMP     #TIMES_XS
                BEQ     :+
                CMP     #PLAY_XS
                BEQ     :+
                CMP     #TIMER_XS
                BEQ     :+
                CMP     #PMAP_XS       ; OO-1, 7e4: PMAP, the home block's
                BEQ     :+
                CMP     #ERDEV_XS      ; OO-1: ERDEV and ERDEV$, the home
                BEQ     :+             ; block's (the IBM manual's p. 91: read-
                CMP     #ERDEVS_XS     ; only variables)
                BNE     EVALN0
:               PLA                    ; the hook's return dropped
                PLA
                FJSR    ERDEVP
                RTS
EVALN0:         LDA     #$FE
                RTS
; EVALNF: the hook's return dropped (POP CX); the function's place in FUNDSX
; (2 x (its token - $81)) kept, its argument in parentheses (PARCHK); then,
; as ISFUN calls a function (res/gweval.s), the text pointer kept for
; LABBCK, which the function returns to, and FLGOVC 1. OO-1: FUNDSX and the
; functions are P4's (p4/dskcom.s): one far call, X the place. A token past
; MKD$'s (Kanji's, dropped, which CRUNCH never makes) is Syntax error.
EVALNF:         PLA
                PLA
                INCW    TXTPTR
                LDA     (TXTPTR)
                SEC
                SBC     #$81
                CMP     #MKDS_XF - $81 + 1
                BCS     SNERRS
                ASL     A
                JSR     SPUSHA         ; PUSH CX
                JSR     CHRGTR
                JSR     PARCHK
                JSR     SPOPA
                PHA
                LDX     #TXTPTR        ; XTHL: the text pointer kept
                LDA     #2
                JSR     SPUSH
                PLX
                PUSHRET LABBCK
                LDA     #1
                STA     FLGOVC
                FJSR    FUNDXP         ; JMP FUNDSX[SI]
                RTS

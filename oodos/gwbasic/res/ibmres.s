; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; IBMRES.ASM's resident part, translated: the extended statements' and
; functions' dispatch (the $FE and $FD tokens). The reserved word tables and
; CRUNCH's and LIST's code for the extended words are P2's (p2/ibmres.s).
;
;   NEWSTX    a statement token past LOCATE: $FE and an extended statement
;             (STMDSX; SYSTEM alone is 7a's), or $FF and PEN or STRIG as a
;             statement (7e's); anything else back to GONE2's caller, for
;             MID$ = (ISMID) or Syntax error
;   EVALX     a formula's $FE DATE$ or TIME$, or $FD and a function (CVI to
;             KLEN): all later sub-phases', so Advanced Feature for now

.include "gwbasic.inc"

.export NEWSTX, EVALX, STMDSX
.import CHRGTR, SNERR, SYSTEM, ADVERR

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
PENV:           JMP     ADVERR         ; PENV, STRIGV: PENS and STRIGS are 7e's
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
; TIMEF), or $FD and a function (FUNDSX): Advanced Feature; anything else
; back, A as it was.
EVALX:          CMP     #$FE
                BEQ     EVALNS
                CMP     #$FD
                BEQ     EVALNF
                RTS
EVALNS:         LDY     #1
                LDA     (TXTPTR),Y
                CMP     #DATES_XS
                BEQ     EVALNF
                CMP     #TIMES_XS
                BEQ     EVALNF
                LDA     #$FE
                RTS
EVALNF:         JMP     ADVERR

.rodata
; STMDSX (IBMRES.ASM 553): the extended statements, $FE $81 to $FE $A1,
; each address less one. SYSTEM is 7a's; the rest are the files' (7c), the
; screen's and graphics' (7d), the sound's (7f) and the system's, later.
.define S(addr) .word addr-1
STMDSX:         S       ADVERR         ; FILES
                S       ADVERR         ; FIELD
                S       SYSTEM         ; SYSTEM
.repeat NUMSTX - 3
                S       ADVERR         ; NAME .. CALLS
.endrepeat

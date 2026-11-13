; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; The resident core's way into P1's math (hw/oodos.md, "The plan, phase 7":
; far calls). A dispatch table entry, or a routine pushed as a return
; address, must be resident, so each banked routine they name has a stub here
; that far calls it. From MATH1.ASM's F4, the interpreter's entries:
;
;   DADD DSUB DMULT DDIV DCOMP   DBLDSP's double operators
;   FPWRQ FPWRT                  the formula evaluator's ^
;   RNDMON RNDMN2                RND without an argument, and RANDOMIZE's
;                                seed (GWMAIN.ASM's RANDOMIZE is 7b's)
;
; and the project's own F_ stubs for FUNDSP (SQR to ATN) and for EVAL's
; digits typed (F_FIN). RNDX, the generator's state, is GWDATA.ASM's; RUN
; starting it again from RNDCOP is 7b's (BIMISC.ASM's RUN).

.include "gwbasic.inc"

.export DADD, DSUB, DMULT, DDIV, DCOMP, FPWRQ, FPWRT, RNDMON, RNDMN2
.export F_SQR, F_RND, F_SIN, F_LOG, F_EXP, F_COS, F_TAN, F_ATN, F_FIN
.export RNDX, RNDCOP
.import FRCSNG, POPR, CHRGTR

.code

; DADD (MATH1.ASM 3136, $FADDD): FAC = FAC + ARG.
DADD:           FJSR    FADDD
                RTS
; DSUB (MATH1.ASM 2408): FAC = FAC - ARG: both signs flipped, then $FSUBD.
DSUB:           LDA     FACM1
                EOR     #$80
                STA     FACM1
                LDA     ARGM1
                EOR     #$80
                STA     ARGM1
                FJSR    FSUBD
                RTS
; DMULT (MATH2.ASM 321, $FMULD), DDIV (MATH1.ASM 3441, DDIVFA).
DMULT:          FJSR    FMULD
                RTS
DDIV:           FJSR    DDIVFA
                RTS
; DCOMP (MATH1.ASM 2730): the FAC (left) against the ARG (right), the other
; way round from $DCMPA, as ICOMP: A = 1 (C clear) if left < right, 0 if
; equal, $FF (C set) if greater; Z and N by A.
DCOMP:          FJSR    DCMPA
                BEQ     @eq
                BMI     @lt
                LDA     #$FF
                SEC
                RTS
@lt:            LDA     #1
                CLC
                RTS
@eq:            CLC
                RTS

; FPWRQ (MATH1.ASM 2691): the right operand forced to single, the left off
; the BASIC stack, then $FPWR.
FPWRQ:          JSR     FRCSNG
FPWRT:          JSR     POPR
                FJSR    FPWR
                RTS

; RNDMON (MATH1.ASM 2583): RND with no "(", as RND(1). RNDMN2: from
; RANDOMIZE, which has put the seed in RNDX+1.
RNDMON:         JSR     CHRGTR
RNDMN2:         STZ     FACLO
                STZ     FACLO+1
                STZ     FACM1
                LDA     #$81
                STA     FACEXP
                LDA     #VT_SNG
                STA     VALTYP
                FJSR    RND
                LDA     #VT_SNG
                STA     VALTYP
                RTS

; FUNDSP's functions in P1 (IBMRES.ASM's SQR RND SIN LOG EXP COS TAN ATN),
; and $FIN for EVAL.
F_SQR:          FJSR    SQR
                RTS
F_RND:          FJSR    RND
                RTS
F_SIN:          FJSR    SIN
                RTS
F_LOG:          FJSR    LOG
                RTS
F_EXP:          FJSR    EXP
                RTS
F_COS:          FJSR    COS
                RTS
F_TAN:          FJSR    TAN
                RTS
F_ATN:          FJSR    ATN
                RTS
F_FIN:          FJSR    FIN
                RTS

.segment "RAM"

; GWDATA.ASM 841: the last random number (its low 3 bytes the seed), and the
; copy RUN starts again from.
RNDCOP:         .byte   $52, $C7, $4F, $80
RNDX:           .byte   $52, $C7, $4F, $80

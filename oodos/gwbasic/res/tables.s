; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; The dispatch tables: GWDATA.ASM's operator tables (segment GWDATA) and
; IBMRES.ASM's statement and function tables (segment IBMRES). Entries not
; translated yet point at res/todo.s's NOTSPK, Illegal function call, or
; at res/todo_i1.s's ADVERR, Advanced Feature, if a later sub-phase's.

.include "gwbasic.inc"

.export OPTAB, RELBIT, FRCTBL, DBLDSP, SNGDSP, INTDSP, FUNDSP, NUMCON
.import FADD, FSUB, FMULT, FDIV, FCOMP, IADD, ISUB, IMULT, INTDIV, ICOMP
.import DMULT, DDIV, DADD, DSUB, DCOMP, FRCDBL, FRCINT, CHKSTR, FRCSNG
.import SGN, VINT, VABS, FIXER, NOTSPK, FILFUN, LPOS, F_PEEK, F_SPOS, JOYF
.import LEFT, RIGHT, MID, FRE, LEN, ASC, CHR, SPACE, F_STR, F_VAL, F_STRO, F_STRH
.import F_SQR, F_RND, F_SIN, F_LOG, F_EXP, F_COS, F_TAN, F_ATN

.rodata

; OPTAB: the precedences of + - * / ^ AND OR XOR EQV IMP MOD \.
OPTAB:          .byte   121, 121, 124, 124, 127, 80, 70, 60, 50, 40, 122, 123
; RELBIT: > = < as the bits DOCMP tests (the 8086 computes them: CMP, RCL).
RELBIT:         .byte   1, 2, 4

; FRCTBL: forcing to a VALTYP, at 2 * (VALTYP AND 7).
FRCTBL:         .word   FRCDBL, 0, FRCINT, CHKSTR, FRCSNG

; The operator routines once APPLOP has matched the types: + - * / compare.
DBLDSP:         .word   DADD, DSUB, DMULT, DDIV, DCOMP
SNGDSP:         .word   FADD, FSUB, FMULT, FDIV, FCOMP
INTDSP:         .word   IADD, ISUB, IMULT, INTDIV, ICOMP

; NUMCON: the fake text CHRGTR leaves TXTPTR at after an embedded constant,
; to re-scan it (CONCON) and then return to the real text (CONCN2). DATA
; and REM step back one before CHRGTR, so the byte before it is read too: in
; the 8086 it is a RET ("a kludge to make CHRGET work"), $C3, which reads as
; a harmless token. A zero there would end the line.
                .byte   $C3
NUMCON:         .byte   CONCON, CONCN2

.rodata

; FUNDSP: the functions LEFT$ ($FF $81) to LOF ($FF $A4).
FUNDSP:
                .word   LEFT, RIGHT, MID ; LEFT$ RIGHT$ MID$
                .word   SGN, VINT, VABS
                .word   F_SQR, F_RND, F_SIN, F_LOG, F_EXP, F_COS, F_TAN, F_ATN
                .word   FRE, NOTSPK, F_SPOS ; FRE INP POS (res/oem.s: 7d1's, in screen mode)
                .word   LEN, F_STR, F_VAL, ASC, CHR ; LEN STR$ VAL ASC CHR$
                .word   F_PEEK, SPACE  ; PEEK (P1's: res/bistrs.s) SPACE$
                .word   F_STRO, F_STRH ; OCT$ HEX$
                .word   LPOS           ; LPOS (P4's: res/far4.s)
                .word   FRCINT, FRCSNG, FRCDBL, FIXER
.repeat 3
                .word   JOYF           ; PEN STICK STRIG (7f: P7's, res/far4.s)
.endrepeat
                .word   FILFUN, FILFUN, FILFUN ; EOF LOC LOF (P4's: res/far4.s)

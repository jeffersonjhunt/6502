; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; The dispatch tables: GWDATA.ASM's operator tables (segment GWDATA) and
; IBMRES.ASM's statement and function tables (segment IBMRES). Entries not
; translated yet point at res/todo.s's NOTSPK, Illegal function call, or
; at res/todo_i1.s's ADVERR, Advanced Feature, if a later sub-phase's.

.include "gwbasic.inc"

.export OPTAB, RELBIT, FRCTBL, DBLDSP, SNGDSP, INTDSP, STMDSP, FUNDSP, NUMCON
.import FADD, FSUB, FMULT, FDIV, FCOMP, IADD, ISUB, IMULT, INTDIV, ICOMP
.import DMULT, DDIV, DADD, DSUB, DCOMP, FRCDBL, FRCINT, CHKSTR, FRCSNG
.import ENDST, GOTO, IFS, LET, REM, DATA, ELSES, PRINT, SNERR, NOTSPK
.import RUN, STOP, LIST, LLIST, SCRATH, CONT, TON, TOFF, DELETE, ADVERR
.import SGN, VINT, VABS, FIXER, DIM, DEFSTR, DEFINT, DEFREA, DEFDBL
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

; STMDSP: the statements END ($81) to LOCATE ($CA), each address less one
; for GONE2's RTS, in IBMRES.ASM's order. NOTSPK (Illegal function call):
; 7a's, not translated yet (step I2's); ADVERR (Advanced Feature): a later
; sub-phase's; SNERR: IBMRES.ASM's padding.
.define S(addr) .word addr-1
STMDSP:         S       ENDST          ; END
                S       NOTSPK         ; FOR
                S       NOTSPK         ; NEXT
                S       DATA           ; DATA
                S       NOTSPK         ; INPUT
                S       DIM            ; DIM
                S       NOTSPK         ; READ
                S       LET            ; LET
                S       GOTO           ; GOTO
                S       RUN            ; RUN
                S       IFS            ; IF
                S       NOTSPK         ; RESTORE
                S       NOTSPK         ; GOSUB
                S       NOTSPK         ; RETURN
                S       REM            ; REM
                S       STOP           ; STOP
                S       PRINT          ; PRINT
                S       ADVERR         ; CLEAR (7b)
                S       LIST           ; LIST
                S       SCRATH         ; NEW
                S       ADVERR         ; ON (7b)
                S       ADVERR         ; WAIT (refused, 7g)
                S       ADVERR         ; DEF (7b)
                S       NOTSPK         ; POKE
                S       CONT           ; CONT
                S       SNERR          ; (padding)
                S       SNERR          ; (padding)
                S       ADVERR         ; OUT (refused, 7g)
                S       NOTSPK         ; LPRINT
                S       LLIST          ; LLIST
                S       SNERR          ; (padding)
                S       ADVERR         ; WIDTH (7d)
                S       ELSES          ; ELSE
                S       TON            ; TRON
                S       TOFF           ; TROFF
                S       ADVERR         ; SWAP (7b)
                S       ADVERR         ; ERASE (7b)
                S       ADVERR         ; EDIT (7b)
                S       ADVERR         ; ERROR (7b)
                S       ADVERR         ; RESUME (7b)
                S       DELETE         ; DELETE
                S       ADVERR         ; AUTO (7b)
                S       ADVERR         ; RENUM (7b)
                S       DEFSTR         ; DEFSTR
                S       DEFINT         ; DEFINT
                S       DEFREA         ; DEFSNG
                S       DEFDBL         ; DEFDBL
                S       ADVERR         ; LINE (7b, 7d)
                S       NOTSPK         ; WHILE
                S       NOTSPK         ; WEND
                S       ADVERR         ; CALL (7g)
                S       SNERR          ; (padding)
                S       SNERR          ; (padding)
                S       SNERR          ; (padding)
                S       ADVERR         ; WRITE (7b)
                S       ADVERR         ; OPTION (7b)
                S       ADVERR         ; RANDOMIZE (7b)
.repeat LOCATE_TK - OPEN_TK + 1
                S       ADVERR         ; OPEN .. LOCATE (7c-7f)
.endrepeat
.assert * - STMDSP = 2 * NUMCMD, error, "STMDSP: one entry a statement"

; FUNDSP: the functions LEFT$ ($FF $81) to LOF ($FF $A4).
FUNDSP:
                .word   LEFT, RIGHT, MID ; LEFT$ RIGHT$ MID$
                .word   SGN, VINT, VABS
                .word   F_SQR, F_RND, F_SIN, F_LOG, F_EXP, F_COS, F_TAN, F_ATN
                .word   FRE, NOTSPK, NOTSPK ; FRE INP POS
                .word   LEN, F_STR, F_VAL, ASC, CHR ; LEN STR$ VAL ASC CHR$
                .word   NOTSPK, SPACE  ; PEEK SPACE$
                .word   F_STRO, F_STRH ; OCT$ HEX$
                .word   NOTSPK         ; LPOS
                .word   FRCINT, FRCSNG, FRCDBL, FIXER
.repeat 6
                .word   NOTSPK         ; PEN .. LOF
.endrepeat

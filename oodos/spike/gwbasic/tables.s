; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; The dispatch tables: GWDATA.ASM's operator tables (segment GWDATA) and
; IBMRES.ASM's statement and function tables (segment IBMRES). Entries the
; spike doesn't translate point at stubs.s's NOTSPK, Illegal function call.

.include "gwbasic.inc"

.export OPTAB, RELBIT, FRCTBL, DBLDSP, SNGDSP, INTDSP, STMDSP, FUNDSP, NUMCON
.import FADD, FSUB, FMULT, FDIV, FCOMP, IADD, ISUB, IMULT, INTDIV, ICOMP
.import DMULT, DDIV, DADD, DSUB, DCOMP, FRCDBL, FRCINT, CHKSTR, FRCSNG
.import ENDST, GOTO, IFS, LET, REM, DATA, ELSES, PRINT, SNERR, NOTSPK
.import SGN, VINT, VABS, FIXER

.segment "GWDATA"

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

.segment "IBMRES"

; STMDSP: the statements END ($81) to LOCATE ($CA), each address less one
; for GONE2's RTS.
.define S(addr) .word addr-1
STMDSP:         S       ENDST          ; END
                S       NOTSPK         ; FOR
                S       NOTSPK         ; NEXT
                S       DATA           ; DATA
.repeat 3
                S       NOTSPK         ; INPUT DIM READ
.endrepeat
                S       LET            ; LET
                S       GOTO           ; GOTO
                S       NOTSPK         ; RUN
                S       IFS            ; IF
.repeat 3
                S       NOTSPK         ; RESTORE GOSUB RETURN
.endrepeat
                S       REM            ; REM
                S       NOTSPK         ; STOP
                S       PRINT          ; PRINT
.repeat 15
                S       NOTSPK         ; CLEAR .. WIDTH
.endrepeat
                S       ELSES          ; ELSE
.repeat NUMCMD - 33
                S       NOTSPK         ; TRON .. LOCATE
.endrepeat

; FUNDSP: the functions LEFT$ ($FF $81) to LOF ($FF $A4).
FUNDSP:
.repeat 3
                .word   NOTSPK         ; LEFT$ RIGHT$ MID$
.endrepeat
                .word   SGN, VINT, VABS
.repeat 21
                .word   NOTSPK         ; SQR .. HEX$ LPOS
.endrepeat
                .word   FRCINT, FRCSNG, FRCDBL, FIXER
.repeat 6
                .word   NOTSPK         ; PEN .. LOF
.endrepeat

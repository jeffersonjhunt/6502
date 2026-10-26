; Derived from Microsoft GW-BASIC (1983), MIT licence, see LICENSE.
;
; The spike's zero page, tokens and shared names (README.md). OODOS gives a
; program $00-$EF at fixed addresses (hw/oodos.md, "Zero page"); this uses
; $00-$8F.
;
; The 8086 source keeps the 8080's register pairs (HL in BX, DE in DX, BC in
; CX). Here they are zero page pseudo-registers in the 8086's byte order, so
; a translated line reads like its original:
;
;   RDL RDH RBL RBH   DX and BX. For single precision they are one MBF
;                     number in memory order (the 8086's BX:DX, the 8080's
;                     B,C,D,E): RDL the low mantissa byte, RBL the sign and
;                     high mantissa, RBH the exponent. For integers DX is the
;                     left operand and BX the right.
;   RAL RAH           AX. RAH is the rounding ("overflow") byte below the
;                     mantissa in the single precision routines.
;   RCL RCH           CX: counts; in the formula evaluator CH is the
;                     precedence just popped.
;
; BX is also the 8086 interpreter's text pointer. Here that is TXTPTR, which
; the math package never touches, so the 8086's PUSH BX / POP BX around math
; calls has no counterpart.
;
; Pointers. The 8086 passes addresses of numbers in BX, DI and SI, and they
; may point at the FAC. OODOS's rule 2 forbids 16-bit pointers to zero page
; (a 65C816's direct page can move), so here a number in zero page is named
; by its zero page address in X, and one in memory by PTR (or PTR2), used as
; (PTR),Y.
;
; The FAC, ARG and their neighbours are laid out as in the 8086's data
; segment, because the double precision code walks across them: DFACL is the
; low half of a double, FACLO the low byte of a single, FACM1 the sign and
; high mantissa ($FAC-1), FACEXP the exponent ($FAC) and FACSGN the unpacked
; sign ($FAC+1).
;
; Two stacks. The 8086 keeps BASIC's temporaries (the formula evaluator's
; operands and operators) on the CPU stack beside return addresses. Here the
; return addresses stay on the 6502's stack and the data go on the BASIC
; stack at SSP (SPUSH and SPOP in bimisc.s), which grows down through the
; TPA, so a deep formula costs page 1 only its return addresses.

.setcpu "65C02"

; --- Pseudo-registers ($00-$0F) ---------------------------------------------
RDL             = $00
RDH             = $01
RBL             = $02
RBH             = $03
RAL             = $04
RAH             = $05
RCL             = $06
RCH             = $07
RSI             = $08                  ; SI, 2 bytes
RDI             = $0A                  ; DI, 2 bytes
RBP             = $0C                  ; BP, 2 bytes
PTR             = $0E                  ; a pointer into memory, for (PTR),Y

; --- Math scratch and state ($10-$1F) ---------------------------------------
T0              = $10                  ; T0-T7: one routine's work bytes
T1              = $11
T2              = $12
T3              = $13
T4              = $14
T5              = $15
T6              = $16
T7              = $17
FSUBF           = $18                  ; $FADDS: bit 7 set to subtract the mantissas
FLGOV           = $19                  ; $FLGOV: 0 print overflow, 1 once more, 2 not
FLGOVC          = $1A                  ; FLGOVC: a function's overflow was printed
VALTYP          = $1B                  ; 2 integer, 3 string, 4 single, 8 double
FMTCX           = $1C                  ; $FMTCX: CL commas, CH digits to the point
FMTAL           = $1E                  ; $FMTAL: PRINT USING's format flags
FMTAH           = $1F

; --- The FAC and its neighbours ($20-$58) -----------------------------------
FACX            = $20                  ; $DFACL-8..$DFACL-1: the double product's low half
DFACL           = $28                  ; $DFACL: a double's low 4 bytes
FACLO           = $2C                  ; $FACLO
FACM1           = $2E                  ; $FAC-1: sign and high mantissa
FACEXP          = $2F                  ; $FAC: exponent
FACSGN          = $30                  ; $FAC+1: the sign while unpacked
ARGX            = $31                  ; $ARGLO-1: ARG's overflow byte
ARGLO           = $32                  ; $ARGLO, 8 bytes
ARGM1           = $38                  ; $ARG-1
ARGEXP          = $39                  ; $ARG
DBUFF           = $3A                  ; $DBUFF, 9 bytes: the multiplier's copy
ZLO             = $43                  ; $ZLO, 8 bytes: $SIGD's Z
Z1LO            = $4B                  ; $Z1LO, 8 bytes: $SIGD's Z1
FBPTR           = $53                  ; into FBUFF: the 8086's BX in the output code
DPADR           = $55                  ; $DPADR: where FOTED put the "."
FOEXP           = $57                  ; $FOTNV's decimal exponent (the 8086's DI)
SPN             = $58                  ; SPUSH and SPOP's byte count

; --- The interpreter ($60-$8F) ----------------------------------------------
TXTPTR          = $60                  ; the text pointer (the 8086's BX)
TEMP2           = $62
TEMP3           = $64
TEMP            = $66
CONTXT          = $68                  ; the text after an embedded constant
CONSAV          = $6A                  ; the constant's token
CONTYP          = $6B                  ; its VALTYP
CONLO           = $6C                  ; its value, 8 bytes
OPRTYP          = $74                  ; the operator APPLOP applies
CURLIN          = $75                  ; the current line number
SAVTXT          = $77                  ; the text pointer at the statement's start
TXTTAB          = $79                  ; the program's first line
SSP             = $7B                  ; the BASIC stack's pointer (it grows down)
SAVSTK          = $7D                  ; S, then SSP, at the statement's start
ONGSBF          = $80                  ; an ON event is waiting
TRCFLG          = $81                  ; TRON
PTRFLG          = $82                  ; line numbers in the text have become pointers
ERRFLG          = $83                  ; the last error's number
ERRLIN          = $84                  ; its line
ONELIN          = $86                  ; ON ERROR's line
STKLOW          = $88                  ; how low the BASIC stack may grow
SYNPTR          = $8A                  ; SYNCHR's return address
PTR2            = $8C                  ; a second pointer into memory

; --- Value types ------------------------------------------------------------
VT_INT          = 2
VT_STR          = 3
VT_SNG          = 4
VT_DBL          = 8

; --- Embedded constants in program text (GWMAIN.ASM) -----------------------
OCTCON          = 11                   ; &O constant, 2 bytes
HEXCON          = 12                   ; &H constant, 2 bytes
PTRCON          = 13                   ; a line pointer, 2 bytes
LINCON          = 14                   ; a line number, 2 bytes
IN2CON          = 15                   ; an integer 11-255, 1 byte
CONCN2          = 16                   ; scan past the constant (NUMCON+1)
ONECON          = 17                   ; 17-26: the integers 0-9
INTCON          = 28                   ; an integer, 2 bytes
SNGCON          = 29                   ; a single, 4 bytes
CONCON          = 30                   ; re-scan the constant (NUMCON)
DBLCON          = 31                   ; a double, 8 bytes

; --- Tokens (IBMRES.ASM) ----------------------------------------------------
END_TK          = $81
LET_TK          = $88
GOTO_TK         = $89
IF_TK           = $8B
GOSUB_TK        = $8D
REM_TK          = $8F
PRINT_TK        = $91
ELSE_TK         = $A1
NUMCMD          = $CA - END_TK + 1     ; the statements END-LOCATE
THEN_TK         = $CD
USR_TK          = $D0
FN_TK           = $D1
NOT_TK          = $D3
ERL_TK          = $D4
ERR_TK          = $D5
INSTR_TK        = $D8
VARPTR_TK       = $DA
GREATK          = $E6
EQULTK          = $E7
LESSTK          = $E8
PLUSTK          = $E9
MINUTK          = $EA
LSTOPK          = $F4 + 1 - PLUSTK     ; + - * / ^ AND OR XOR EQV IMP MOD \
NMREL           = LESSTK - GREATK + 1
OPCNT           = 4                    ; the compare's place in the dispatch tables
ONEFUN          = $81                  ; functions follow $FF: LEFT$ is $FF $81
NUMGFN          = 6                    ; LEFT$ RIGHT$ MID$, times 2
RNDFN           = $88 - ONEFUN
BOTCON          = 2 * ($87 - ONEFUN)   ; SQR...
TOPCON          = 2 * ($8E - ONEFUN) + 2 ; ...ATN: these take a single

; --- OODOS ------------------------------------------------------------------
BDOS            = $0203                ; JSR here: the function in Y, a byte in A
C_WRITE         = 2
P_TERMCPM       = 0

; --- 16-bit helpers ---------------------------------------------------------
.macro  INCW    addr
                INC     addr
                BNE     :+
                INC     addr+1
:
.endmacro

.macro  DECW    addr
                LDA     addr
                BNE     :+
                DEC     addr+1
:               DEC     addr
.endmacro

; Push the address of a routine for RTS to go to: the 8086's PUSH of a
; return address, MOV CX,OFFSET RETAOP / PUSH CX.
.macro  PUSHRET target
                LDA     #>(target-1)
                PHA
                LDA     #<(target-1)
                PHA
.endmacro

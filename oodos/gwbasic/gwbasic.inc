; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GW-BASIC's zero page, tokens and shared names (hw/oodos.md, "The plan,
; phase 7"), from the spike's (oodos/spike/gwbasic/). OODOS gives a program
; $00-$EF at fixed addresses (hw/oodos.md, "Zero page"); this uses $00-$EF,
; in the ranges below. Every module includes it, resident or banked: the
; blocks share the resident core's zero page, so it is all equates here.
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
.include "oodos.inc"

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
FLGOVC          = FLGOV                ; FLGOVC: the same byte in the 8086 (GWDATA.ASM labels both)
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

; --- The rest of the math, in P1 ($90-$9F) ----------------------------------
FINDI           = $90                  ; $FIN: the 8086's DI, digits past the point (XTON: N)
FINCX           = $92                  ; its CX: $FFFF before a point, 0 after
FINSI           = $94                  ; its SI: the exponent's sign, 0 or $FF
FINDX           = $95                  ; its DX: the exponent typed
FINNEG          = $97                  ; the number's sign: $FF for a "-"
FINDIG          = $98                  ; the digit $FIDIG adds
PX              = $99                  ; $POLY's x, 4 bytes
PCP             = $9D                  ; $POLY's coefficient pointer
PCN             = $9F                  ; $POLY's coefficients left

; --- Variables and strings ($A0-$BF): BIPTRG.ASM's and BISTRS.ASM's ----------
;
; The workspace, from TXTTAB up: the program, the simple variables (VARTAB),
; the arrays (ARYTAB) to STREND, free space, then the strings down from
; MEMSIZ, the lowest at FRETOP+1. The pointers the search and the string
; space use most are here; the rest (MEMSIZ, TEMPST, DSCTMP, NAMBUF, DEFTBL)
; are fixed RAM (res/biptrg.s, res/bistrs.s).
VARTAB          = $A0                  ; the simple variables
ARYTAB          = $A2                  ; the arrays
STREND          = $A4                  ; the first byte past them
FRETOP          = $A6                  ; the top of free space: strings above it
TEMPPT          = $A8                  ; the first free string temporary
ARYTA2          = $AA                  ; where the simple search stops
VPTR            = $AC                  ; the symbol table search (the 8086's SI)
DIMFLG          = $AE                  ; DIM's: bit 7 set when DIM called PTRGET
NDIM            = $AF                  ; an array's subscripts so far (the 8086's D)
ATEMP3          = $B0                  ; NOTFDD's TEMP3: the new array's dimension count
NCNT            = $B2                  ; NOTFDD's dimensions to go
GSLEN           = $B3                  ; GETSPA's length
GSDONE          = $B4                  ; GETSPA's: nonzero once it has collected
GCMAX           = $B5                  ; the garbage collector's highest string so far
GCVAR           = $B7                  ; and the place past its descriptor, or 0
TEMP8           = $B9                  ; the end of the string array being collected
S1              = $BB                  ; a string routine's descriptor (or pointer)
S2              = $BD                  ; and a second
SLEN            = $BF                  ; and a length

; --- Program entry, listing and the console ($C0-$D5): step I1's ------------
;
; CRUNCH and BUFLIN (P2) keep in zero page what the 8086 keeps in DX, CX and
; on its stack while they call CHRGTR, LINGET and $FIN, which use the
; pseudo-registers; the console's column and device are here because every
; character out tests them.
KDP             = $C0                  ; CRUNCH's and BUFLIN's deposit pointer (the 8086's DX; CX)
KCNT            = $C2                  ; the bytes left there (CX; BUFLIN's DH); CRUNCH: then the count
KRP             = $C4                  ; the reserved word table's pointer (DX in the search)
KSRC            = $C6                  ; the text pointer saved while a word is tried
DONUM           = $C8                  ; CRUNCH: 0 a number, 1 a line number, $FF in a name
DORES           = $C9                  ; CRUNCH: crunching stopped (DATA); BUFLIN: quote, DATA, REM
TEMPA           = $CA                  ; BUFLIN: what went last, for its spaces
KCH             = $CB                  ; a character kept (the 8086's CL, DL)
KTRM            = $CC                  ; CRUNCH: a string's terminator; BUFLIN: the token sought
KLEAD           = $CD                  ; BUFLIN: the reserved word's first letter
PTRFIL          = $CE                  ; output: 0 the console, $FFFF LPT1 (GIO86.ASM's file pointer)
TTYPOS          = $D0                  ; the console's column
LPTPOS          = $D1                  ; LPT1's
LSTMAX          = $D2                  ; SCNLIN: the range's last line (the 8086 keeps it on its stack)
LSTLIN          = $D4                  ; LIST: the line being listed
BUFLEN          = 255                  ; OEM.H: a line's characters (BUF)
KBFLEN          = BUFLEN + BUFLEN / 4  ; GWMAIN.ASM: the crunch buffer (KBUF), somewhat larger

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
;
; Every reserved word's token, in IBMRES.ASM's order (its R, R2 and T macros
; count them; a DUMMY takes a number and has no word). NAME_TK a statement or
; another one-byte token; NAME_FN the byte after $FF of a function; NAME_XS
; the byte after $FE of an extended statement; NAME_XF the byte after $FD of
; an extended function. A "$" in a name is an S here: STRINGS_TK is STRING$.
END_TK          = $81
FOR_TK          = $82
NEXT_TK         = $83
DATA_TK         = $84
INPUT_TK        = $85
DIM_TK          = $86
READ_TK         = $87
LET_TK          = $88
GOTO_TK         = $89
RUN_TK          = $8A
IF_TK           = $8B
RESTORE_TK      = $8C
GOSUB_TK        = $8D
RETURN_TK       = $8E
REM_TK          = $8F
STOP_TK         = $90
PRINT_TK        = $91
CLEAR_TK        = $92
LIST_TK         = $93
NEW_TK          = $94
ON_TK           = $95
WAIT_TK         = $96
DEF_TK          = $97
POKE_TK         = $98
CONT_TK         = $99
OUT_TK          = $9C
LPRINT_TK       = $9D
LLIST_TK        = $9E
WIDTH_TK        = $A0
ELSE_TK         = $A1
TRON_TK         = $A2
TROFF_TK        = $A3
SWAP_TK         = $A4
ERASE_TK        = $A5
EDIT_TK         = $A6
ERROR_TK        = $A7
RESUME_TK       = $A8
DELETE_TK       = $A9
AUTO_TK         = $AA
RENUM_TK        = $AB
DEFSTR_TK       = $AC
DEFINT_TK       = $AD
DEFSNG_TK       = $AE
DEFDBL_TK       = $AF
LINE_TK         = $B0
WHILE_TK        = $B1
WEND_TK         = $B2
CALL_TK         = $B3
WRITE_TK        = $B7
OPTION_TK       = $B8
RANDOMIZE_TK    = $B9
OPEN_TK         = $BA
CLOSE_TK        = $BB
LOAD_TK         = $BC
MERGE_TK        = $BD
SAVE_TK         = $BE
COLOR_TK        = $BF
CLS_TK          = $C0
MOTOR_TK        = $C1
BSAVE_TK        = $C2
BLOAD_TK        = $C3
SOUND_TK        = $C4
BEEP_TK         = $C5
PSET_TK         = $C6
PRESET_TK       = $C7
SCREEN_TK       = $C8
KEY_TK          = $C9
LOCATE_TK       = $CA
TO_TK           = $CC
THEN_TK         = $CD
TAB_TK          = $CE
STEP_TK         = $CF
USR_TK          = $D0
FN_TK           = $D1
SPC_TK          = $D2
NOT_TK          = $D3
ERL_TK          = $D4
ERR_TK          = $D5
STRINGS_TK      = $D6
USING_TK        = $D7
INSTR_TK        = $D8
VARPTR_TK       = $DA
CSRLIN_TK       = $DB
POINT_TK        = $DC
OFF_TK          = $DD
INKEYS_TK       = $DE
GREATK          = $E6                  ; the operators: > = < + - * / ^
EQULTK          = $E7
LESSTK          = $E8
PLUSTK          = $E9
MINUTK          = $EA
MULTK           = $EB
DIVTK           = $EC
EXPTK           = $ED
AND_TK          = $EE
OR_TK           = $EF
XOR_TK          = $F0
EQV_TK          = $F1
IMP_TK          = $F2
MOD_TK          = $F3
IDIVTK          = $F4                  ; \
SNGQTK          = $D9                  ; ' (a DUMMY: REM's other form)
LEFTS_FN        = $81
RIGHTS_FN       = $82
MIDS_FN         = $83
SGN_FN          = $84
INT_FN          = $85
ABS_FN          = $86
SQR_FN          = $87
RND_FN          = $88
SIN_FN          = $89
LOG_FN          = $8A
EXP_FN          = $8B
COS_FN          = $8C
TAN_FN          = $8D
ATN_FN          = $8E
FRE_FN          = $8F
INP_FN          = $90
POS_FN          = $91
LEN_FN          = $92
STRS_FN         = $93
VAL_FN          = $94
ASC_FN          = $95
CHRS_FN         = $96
PEEK_FN         = $97
SPACES_FN       = $98
OCTS_FN         = $99
HEXS_FN         = $9A
LPOS_FN         = $9B
CINT_FN         = $9C
CSNG_FN         = $9D
CDBL_FN         = $9E
PEN_FN          = $A0
STICK_FN        = $A1
STRIG_FN        = $A2
EOF_FN          = $A3
LOC_FN          = $A4
LOF_FN          = $A5
FIX_FN          = $9F                  ; (IBMRES.ASM: R2 DUMMY,FIXER)
FILES_XS        = $81
FIELD_XS        = $82
SYSTEM_XS       = $83
NAME_XS         = $84
LSET_XS         = $85
RSET_XS         = $86
KILL_XS         = $87
PUT_XS          = $88
GET_XS          = $89
RESET_XS        = $8A
COMMON_XS       = $8B
CHAIN_XS        = $8C
DATES_XS        = $8D
TIMES_XS        = $8E
PAINT_XS        = $8F
COM_XS          = $90
CIRCLE_XS       = $91
DRAW_XS         = $92
PLAY_XS         = $93
TIMER_XS        = $94
ERDEV_XS        = $95
IOCTL_XS        = $96
CHDIR_XS        = $97
MKDIR_XS        = $98
RMDIR_XS        = $99
SHELL_XS        = $9A
ENVIRON_XS      = $9B
VIEW_XS         = $9C
WINDOW_XS       = $9D
PMAP_XS         = $9E
PALETTE_XS      = $9F
LCOPY_XS        = $A0
CALLS_XS        = $A1
CVI_XF          = $81
CVS_XF          = $82
CVD_XF          = $83
MKIS_XF         = $84
MKSS_XF         = $85
MKDS_XF         = $86
KTN_XF          = $87
JIS_XF          = $88
KPOS_XF         = $89
KLEN_XF         = $8A
NUMCMD          = LOCATE_TK - END_TK + 1 ; the statements END-LOCATE
NUMSTX          = CALLS_XS - FILES_XS + 1 ; the extended statements FILES-CALLS
LSTOPK          = IDIVTK + 1 - PLUSTK  ; + - * / ^ AND OR XOR EQV IMP MOD \
NMREL           = LESSTK - GREATK + 1
OPCNT           = 4                    ; the compare's place in the dispatch tables
ONEFUN          = LEFTS_FN             ; functions follow $FF: LEFT$ is $FF $81
MIDTK           = MIDS_FN
NUMGFN          = 6                    ; LEFT$ RIGHT$ MID$, times 2
RNDFN           = RND_FN - ONEFUN
BOTCON          = 2 * (SQR_FN - ONEFUN) ; SQR...
TOPCON          = 2 * (ATN_FN - ONEFUN) + 2 ; ...ATN: these take a single
DATCO           = DATA_TK - ':'        ; $DATCO and $REMCO, CRUNCH's
REMCO           = REM_TK - ':'

; --- Far calls --------------------------------------------------------------
;
; GW-BASIC's code is the resident core (the TPA) and banked blocks P1-P8, each
; linked at $C800 for W3's code window (hw/oodos.md, "The plan, phase 7").
; A block starts with a header (BLK_FIX, BLK_NFIX: its far call sites) and a
; jump table, entry n at BLK_ENTRY + 3n. tools/gwgen.py makes build/entries.inc
; from the blocks' ";@entry NAME" lines: PG_NAME the block's page (1-8) and
; E_NAME its entry. FJSR NAME calls it from anywhere: a JSR FARCALL with the
; block and the address after it, as FARCALL takes them. It is assembled as a
; JSR FARMISS (the resident core's: "far call before load"), and the loader
; puts FARCALL's address and the page's block in for every site the FARFIX
; segment lists, before anything runs: so OODOS's rules gate sees a JSR
; inside the program (rules.py --farcall FARMISS), and FARCALL is OODOS's,
; unchanged.
BLK_BASE        = $C800
BLK_FIX         = BLK_BASE             ; +0-1 the block's FARFIX list, +2-3 its count
BLK_NFIX        = BLK_BASE + 2
BLK_ENTRY       = BLK_BASE + 4         ; the jump table
BLK_END         = $F800                ; common memory from here, copied in by the loader

.macro  FAR     page, target
        .local  @site                  ; a cheap local: the caller's @ labels
@site:  JSR     FARMISS                ; stay in scope across it
        .byte   page
        .word   target
        .pushseg
        .segment "FARFIX"
        .word   @site + 1
        .popseg
.endmacro

.ifdef FLAT
; The flat test build (test/, the project's own): every module, resident or
; banked, in one image for the W65C02S core alone, so a far call is a JSR.
.macro  FJSR    name
        .global name
        JSR     name
.endmacro
.else
.macro  FJSR    name
        FAR     .ident(.concat("PG_", .string(name))), .ident(.concat("E_", .string(name)))
.endmacro
.endif

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

.global FARMISS
.include "entries.inc"

; Long branches: a branch past +-127 bytes, as a short one round a JMP.
.macro  JNE     t
                BEQ     :+
                JMP     t
:
.endmacro
.macro  JEQ     t
                BNE     :+
                JMP     t
:
.endmacro
.macro  JCS     t
                BCC     :+
                JMP     t
:
.endmacro

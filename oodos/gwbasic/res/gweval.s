; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWEVAL.ASM, translated: the formula evaluator (README.md):
;
;   FRMEVL LPOPER RETAOP   operator precedence, the operands stacked
;   APPLOP                 the types matched, then the operator dispatched
;   EVAL                   a constant, a variable, a function, "(", -, NOT
;   ISFUN                  function dispatch
;   DOCMP NOTER DANDOR     relational, NOT, AND OR XOR EQV IMP MOD \
;   DOCNVF                 force a value to a type
;   GETBYT CONINT GETIN2   a byte or an integer from a formula
;   VARPTR GIVDBL          a variable's address; BX - DX as a number
;   MAKUPL MAKUPS          a character in capitals (step I1)
;   OCTCNS                 &O and &H: CRUNCH's, VAL's and INPUT's (step I1)
;   ISMID                  MID$ = as a statement (step I1), to P3's LHSMID
;
; Strings take BISTRS.ASM's routines (res/bistrs.s): a literal (STRLTI), "+"
; (CAT), the relations (STRCMP), INSTR. INKEY$ is the OEM's (res/oem.s).
; STRING$ and VARPTR$ are P3's (7b step a2), through res/far3.s's stubs.
; Left out (stubs): USR, FN; the extended functions (res/ibmres.s's EVALX)
; give Advanced Feature.
;
; The 8086 keeps each pending operator's entry on its stack: the address to
; go on at (RETAOP), the left operand, the operator and its type, and the
; addresses of APPLOP or DOCMP to apply it, interleaved. Here the addresses
; go on the 6502's stack and the data on the BASIC stack, in the same order,
; so each comes off where the 8086's does.

.include "gwbasic.inc"

.export FRMEQL, FRMPRN, FRMEVL, FRMCHK, LPOPER, RETAOP, APPLOP, EVAL, PARCHK
.export DOCMP, NOTER, DANDOR, GETYPR, DOCNVF, INTDIV, LABBCK, SNGFLT
.export TSTOP, RETVAR, GETBYT, GTBYTC, CONINT, GETIN2, INTFR2, VARPTR, GIVDBL
.import SYNCHR, CHRGTR, GETSTK, ISLET2, SPUSH, SPOP, SPUSHA, SPOPA, PUSHF
.import VMOVAF, POPR, FRCSNG, FRCINT, FRCDBL, CONSIH, MOVRF, MOVFR, FDIVT, CONIA
.import MAKINT, IMOD, IDIV, VNEG, VMOVFM, INEG2, CHKSTR, GETYP, VFAC
.import OPTAB, RELBIT, INTDSP, SNGDSP, DBLDSP, FRCTBL, FUNDSP
.import TMERR, SNERR, MOERR, CAT, STRCMP, FPWRQ, F_FIN, EVALX, STRLTI
.import PTRGET, USRFN, INSTR, FNDOER, RNDMON, CONFAC
.import PTRGTN, FCERR, NOTYET, INKEY, OVERR, CRDO, STROUT, LHSMID, STRNGS, VARPT2
.export MAKUPL, MAKUPS, OCTCNS, ISMID, FLGSCN

GETYPR          = GETYP                ; the 8086 has a copy of $GETYP here

.code

; --- FRMEVL ----------------------------------------------------------------------

FRMEQL:         JSR     SYNCHR         ; "=" then a formula
                .byte   EQULTK
                BRA     FRMEVL
FRMPRN:         JSR     SYNCHR         ; "(" then a formula
                .byte   '('
FRMEVL:         DECW    TXTPTR
FRMCHK:         LDA     #0             ; the dummy precedence
LPOPER:         JSR     SPUSHA         ; the precedence
                LDA     #1
                JSR     GETSTK
                JSR     EVAL
                STZ     FLGOVC
TSTOP:          LDA     TXTPTR
                STA     TEMP2
                LDA     TXTPTR+1
                STA     TEMP2+1
RETAOP:         LDA     TEMP2          ; an operator applied: back to the text
                STA     TXTPTR
                LDA     TEMP2+1
                STA     TXTPTR+1
                JSR     SPOPA          ; the old operator's precedence
                STA     RCH
NOTSTV:         LDA     TXTPTR
                STA     TEMP3
                LDA     TXTPTR+1
                STA     TEMP3+1
                LDA     (TXTPTR)
                CMP     #GREATK        ; an operator?
                BCS     :+
                RTS                    ; no: apply what's pending, or done
:               CMP     #LESSTK+1
                BCS     :+
                JMP     DORELS
:               SEC
                SBC     #PLUSTK
                STA     RDL            ; the operator
                BNE     NTPLUS
                LDA     VALTYP         ; + on a string
                CMP     #VT_STR
                BNE     :+
                JMP     CAT
:               LDA     RDL
NTPLUS:         CMP     #LSTOPK
                BCC     :+
                RTS
:               TAX
                LDA     OPTAB,X
                STA     RDH            ; its precedence
                LDA     RCH
                CMP     RDH
                BCC     :+
                RTS                    ; the old one first
:               LDA     RCH
                JSR     SPUSHA
                PUSHRET RETAOP
                LDA     RDH
                CMP     #127           ; ^
                BEQ     EXPSTK
                CMP     #81            ; AND OR XOR EQV IMP
                BCC     ANDORD
                AND     #$FE
                CMP     #122           ; MOD \
                BEQ     ANDORD
NUMREL:         LDA     VALTYP         ; the left operand, by type
                CMP     #VT_STR
                BNE     :+
                JMP     TMERR
:               JSR     VFAC
                LDA     VALTYP
                JSR     SPUSH
VPUSHD:         LDA     RDL            ; the operator, and the operand's type
                JSR     SPUSHA
                LDA     VALTYP
                JSR     SPUSHA
                PUSHRET APPLOP
FINTMP:         LDA     TEMP3
                STA     TXTPTR
                LDA     TEMP3+1
                STA     TXTPTR+1
                LDA     RDH
                JMP     LPOPER

EXPSTK:         JSR     FRCSNG         ; ^ works in single precision
                JSR     PUSHF
                PUSHRET FPWRQ
                LDA     #127
                STA     RDH
                JMP     FINTMP

ANDORD:         LDA     RDH            ; AND OR MOD \: integers
                PHA
                JSR     FRCINT
                PLA
                STA     RDH
                LDX     #RBL
                LDA     #2
                JSR     SPUSH
                PUSHRET DANDOR
                JMP     FINTMP

DORELS:         STZ     RDH            ; the relations seen: > = <
LOPREL:         SEC
                SBC     #GREATK
                BCC     FINREL
                CMP     #NMREL
                BCS     FINREL
                TAX
                LDA     RELBIT,X       ; 1, 2, 4
                EOR     RDH
                CMP     RDH
                BCS     :+
                JMP     SNERR          ; the same one twice
:               STA     RDH
                LDA     TXTPTR
                STA     TEMP3
                LDA     TXTPTR+1
                STA     TEMP3+1
                JSR     CHRGTR
                BRA     LOPREL
FINREL:         LDA     RCH
                CMP     #100
                BCC     :+
                RTS
:               JSR     SPUSHA         ; the old precedence
                LDA     RDH
                JSR     SPUSHA         ; the relations
                LDA     #100
                STA     RDH
                LDA     #OPCNT
                STA     RDL
                PUSHRET DOCMP
                JSR     GETYPR
                BEQ     :+
                JMP     NUMREL
:               LDX     #FACLO         ; strings: the descriptor's address
                LDA     #2
                JSR     SPUSH
                PUSHRET STRCMP
                JMP     FINTMP

; --- APPLOP -----------------------------------------------------------------------

; Match the two operands' types, then dispatch: integers left in DX and
; right in BX; singles left in BXDX and right in the FAC; doubles left in
; the FAC and right in the ARG.
APPLOP:         JSR     SPOPA          ; the stacked operand's type
                STA     RCH
                JSR     SPOPA          ; the operator
                STA     RCL
                STA     OPRTYP
                LDA     VALTYP
                CMP     RCH
                BNE     VALNSM
                CMP     #VT_INT
                BEQ     INTDPC
                CMP     #VT_SNG
                BNE     :+
                JMP     SNGDPC
:               BCS     DBLDPC
VALNSM:         STA     RDH
                LDA     RCH
                CMP     #VT_DBL
                BEQ     STKDBL
                LDA     RDH
                CMP     #VT_DBL
                BEQ     FACDBL
                LDA     RCH
                CMP     #VT_SNG
                BEQ     STKSNG
                LDA     RDH
                CMP     #VT_STR
                BNE     :+
                JMP     TMERR
:               BCS     FACSNG
INTDPC:         LDX     #RDL           ; integer, integer
                LDA     #2
                JSR     SPOP
                LDA     FACLO
                STA     RBL
                LDA     FACLO+1
                STA     RBH
                LDA     OPRTYP
                ASL     A
                TAX
                JMP     (INTDSP,X)
STKDBL:         JSR     FRCDBL
DBLDPC:         JSR     VMOVAF         ; the FAC to the ARG, the stacked to the FAC
                LDX     #DFACL
                LDA     #8
                JSR     SPOP
                BRA     SETDBL
FACDBL:         LDA     RCH
                PHA
                JSR     VMOVAF
                PLA
                STA     VALTYP
                CMP     #VT_SNG
                BNE     :+
                JSR     POPR           ; SNGDBL
                JSR     MOVFR
                BRA     SETDBL
:               LDX     #FACLO
                LDA     #2
                JSR     SPOP
SETDBL:         JSR     FRCDBL
                LDA     OPRTYP
                ASL     A
                TAX
                JMP     (DBLDSP,X)
STKSNG:         JSR     FRCSNG
SNGDPC:         JSR     POPR
SNGDO:          LDA     OPRTYP
                ASL     A
                TAX
                JMP     (SNGDSP,X)
FACSNG:         LDX     #RBL           ; the stacked integer
                LDA     #2
                JSR     SPOP
                JSR     PUSHF
                JSR     CONSIH
                JSR     MOVRF
                LDX     #FACLO
                LDA     #4
                JSR     SPOP
                BRA     SNGDO

; INTDIV: "/" of two integers is single: 1/3 is .3333333.
INTDIV:         LDX     #RBL
                LDA     #2
                JSR     SPUSH
                LDA     RDL
                STA     RBL
                LDA     RDH
                STA     RBH
                JSR     CONSIH
                LDX     #RBL
                LDA     #2
                JSR     SPOP
                JSR     PUSHF
                JSR     CONSIH
                JMP     FDIVT

; --- EVAL -----------------------------------------------------------------------------

EVAL:           JSR     CHRGTR
                BNE     :+
                JMP     MOERR          ; Missing operand
:               BCC     :+
                JMP     F_FIN          ; digits typed, not tokenised
:               JSR     ISLET2
                BCS     :+
                JMP     ISVAR
:               CMP     #DBLCON+1
                BCS     :+
                JMP     CONFAC
:               JSR     EVALX
                INC     A
                BNE     :+
                JMP     ISFUN          ; $FF: a function
:               DEC     A
                CMP     #PLUSTK        ; unary + is ignored
                BEQ     EVAL
                CMP     #MINUTK
                BNE     :+
                JMP     DOMIN
:               CMP     #'"'
                BNE     :+
                JMP     STRLTI
:               CMP     #NOT_TK
                BNE     :+
                JMP     NOTER
:               CMP     #'&'
                BNE     :+
                JMP     OCTCNS
:               CMP     #ERR_TK
                BNE     NTERC
                JSR     CHRGTR
                LDA     ERRFLG
NTDERC:         JMP     SNGFLT
NTERC:          CMP     #ERL_TK
                BNE     NTERL
                JSR     CHRGTR
                LDA     ERRLIN
                STA     RBL
                LDA     ERRLIN+1
                STA     RBH
                JMP     INEG2
NTERL:          CMP     #VARPTR_TK
                BNE     :+
                JMP     VARPTR
:               CMP     #USR_TK
                BNE     :+
                JMP     USRFN
:               CMP     #INSTR_TK
                BNE     :+
                JMP     INSTR
:               CMP     #INKEYS_TK     ; (SCREEN and POINT before it are 7d's)
                BNE     :+
                JMP     INKEY
:               CMP     #STRINGS_TK    ; STRING$ (P3's: res/far3.s)
                BNE     :+
                JMP     STRNGS
:               CMP     #FN_TK
                BNE     PARCHK
                JMP     FNDOER
; Nothing else: a formula in parentheses.
PARCHK:         JSR     FRMPRN
                JSR     SYNCHR
                .byte   ')'
                RTS
DOMIN:          LDA     #125           ; below ^, above the rest
                JSR     LPOPER
                LDA     TEMP2
                STA     TXTPTR
                LDA     TEMP2+1
                STA     TXTPTR+1
                JMP     VNEG
; ISVAR, RETVAR: a variable's value; a string's descriptor address.
ISVAR:          JSR     PTRGET
RETVAR:         LDA     PTR
                STA     FACLO
                LDA     PTR+1
                STA     FACLO+1
                JSR     GETYPR
                BEQ     :+
                JMP     VMOVFM
:               RTS

; ISFUN: $FF and a function token. Most take one argument in parentheses;
; LEFT$ RIGHT$ MID$ take a string and a byte first.
ISFUN:          INCW    TXTPTR
                LDA     (TXTPTR)
                SEC
                SBC     #ONEFUN
                CMP     #RNDFN         ; RND without "("?
                BNE     NTMRND
                LDA     TXTPTR
                PHA
                LDA     TXTPTR+1
                PHA
                JSR     CHRGTR
                TAX
                PLA
                STA     TXTPTR+1
                PLA
                STA     TXTPTR
                CPX     #'('
                BEQ     :+
                JMP     RNDMON
:               LDA     #RNDFN
NTMRND:         ASL     A
                STA     RCL
                JSR     SPUSHA         ; the function, times 2
                JSR     CHRGTR
                LDA     RCL
                CMP     #NUMGFN
                BCS     OKNORM
                JSR     FRMPRN         ; LEFT$ RIGHT$ MID$: the string
                JSR     SYNCHR
                .byte   ','
                JSR     CHKSTR
                JSR     SPOPA
                PHA
                LDX     #FACLO         ; its descriptor
                LDA     #2
                JSR     SPUSH
                JSR     GETBYT         ; the byte, in DX
                PLA
                TAX
                PHX
                LDX     #RDL
                LDA     #2
                JSR     SPUSH
                PLX
                BRA     FINGO
OKNORM:         JSR     PARCHK
                JSR     SPOPA
                PHA
                LDX     #TXTPTR        ; LABBCK will want the text pointer
                LDA     #2
                JSR     SPUSH
                PLA
                CMP     #BOTCON        ; SQR..ATN take a single
                BCC     NOTFRF
                CMP     #TOPCON
                PHA
                BCS     :+
                JSR     FRCSNG
:               PLA
NOTFRF:         TAX
                PUSHRET LABBCK
                LDA     #1             ; overflow printed once per function
                STA     FLGOVC
FINGO:          JMP     (FUNDSP,X)
LABBCK:         LDX     #TXTPTR        ; a function's return: the text pointer back
                LDA     #2
                JMP     SPOP

; --- Relational, NOT, logical --------------------------------------------------------

; DOCMP: A and C from a compare routine, with the relations wanted stacked:
; true is -1, false 0.
DOCMP:          INC     A              ; 4 less, 2 equal, 1 greater
                STA     T0
                ADC     T0
                STA     T1
                JSR     SPOPA
                AND     T1
                BEQ     :+
                LDA     #$FF
:               JSR     CONIA
                JMP     RETAOP
NOTER:          LDA     #90            ; NOT's precedence
                JSR     LPOPER
                JSR     FRCINT
                LDA     RBL
                EOR     #$FF
                STA     FACLO
                LDA     RBH
                EOR     #$FF
                STA     FACLO+1
                PLA                    ; drop the return into LPOPER
                PLA
                JMP     RETAOP

; DANDOR: AND OR XOR EQV IMP (and MOD \): the right operand made integer,
; the left one stacked; the precedence tells them apart.
DANDOR:         LDA     RCH
                PHA
                JSR     FRCINT
                PLA
                STA     T0
                LDX     #RDL
                LDA     #2
                JSR     SPOP
                LDA     T0
                CMP     #122
                BNE     :+
                JMP     IMOD
:               CMP     #123
                BNE     :+
                JMP     IDIV
:               PUSHRET MAKINT
                LDA     T0
                CMP     #70
                BNE     NOTOR
                LDA     RBL            ; OR
                ORA     RDL
                STA     RBL
                LDA     RBH
                ORA     RDH
                STA     RBH
                RTS
NOTOR:          CMP     #80
                BNE     NOTAND
                LDA     RBL            ; AND
                AND     RDL
                STA     RBL
                LDA     RBH
                AND     RDH
                STA     RBH
                RTS
NOTAND:         CMP     #60
                BNE     NOTXOR
                LDA     RBL            ; XOR
                EOR     RDL
                STA     RBL
                LDA     RBH
                EOR     RDH
                STA     RBH
                RTS
NOTXOR:         CMP     #50
                BNE     NOTEQV
                LDA     RBL            ; EQV: NOT (A XOR B)
                EOR     RDL
                EOR     #$FF
                STA     RBL
                LDA     RBH
                EOR     RDH
                EOR     #$FF
                STA     RBH
                RTS
NOTEQV:         LDA     RBL            ; IMP: NOT (A AND NOT B)
                EOR     #$FF
                AND     RDL
                EOR     #$FF
                STA     RBL
                LDA     RBH
                EOR     #$FF
                AND     RDH
                EOR     #$FF
                STA     RBH
                RTS

; SNGFLT: A, unsigned, as an integer.
SNGFLT:         STA     RBL
                STZ     RBH
                JMP     MAKINT

; DOCNVF: force the FAC to the type in A, through FRCTBL.
DOCNVF:         AND     #7
                ASL     A
                TAX
                JMP     (FRCTBL,X)

; --- GETBYT, CONINT, GETIN2 (GWEVAL.ASM 1602) ----------------------------------------

; GETIN2: a formula, as an integer in DX; A = DH, N and Z by it. INTFR2: the
; FAC's.
GETIN2:         JSR     FRMEVL
INTFR2:         JSR     FRCINT
                LDA     RBL
                STA     RDL
                LDA     RBH
                STA     RDH
                RTS

; GETBYT: a formula, as a byte: 0-255, or Illegal function call. CONINT: the
; FAC's. A and DL return it, with CHRGTR's flags for the character after.
GTBYTC:         JSR     CHRGTR
GETBYT:         JSR     FRMEVL
CONINT:         JSR     INTFR2
                BEQ     :+
                JMP     FCERR
:               DECW    TXTPTR
                JSR     CHRGTR
                PHP
                LDA     RDL
                PLP
                RTS

; --- VARPTR (GWEVAL.ASM 678) ----------------------------------------------------------

; From EVAL, TXTPTR at VARPTR: VARPTR(variable), the address of its value as
; an integer; Illegal function call if it has none. VARPTR$ is P3's (a
; stub, res/far3.s); a file's VARPTR(#n) is 7c's: NOTYET.
VARPTR:         JSR     CHRGTR
                CMP     #'$'
                BNE     :+
                JMP     VARPT2         ; VARPTR$ (P3's: res/far3.s)
:               JSR     SYNCHR
                .byte   '('
                CMP     #'#'
                BNE     :+
                JMP     NOTYET         ; GETPTR
:               JSR     PTRGTN
VARRET:         JSR     SYNCHR
                .byte   ')'
                LDA     PTR
                STA     RBL
                ORA     PTR+1
                BNE     :+
                JMP     FCERR          ; not there
:               LDA     PTR+1
                STA     RBH
                JMP     MAKINT

; GIVDBL (GWEVAL.ASM 1113): BX - DX, unsigned, as a single.
GIVDBL:         SEC
                LDA     RBL
                SBC     RDL
                STA     RBL
                LDA     RBH
                SBC     RDH
                STA     RBH
                JMP     INEG2

; --- MAKUPL, MAKUPS (GWEVAL.ASM 793) ------------------------------------------------

; MAKUPL: the character at TXTPTR, MAKUPS: A, as a capital if a small letter.
MAKUPL:         LDA     (TXTPTR)
MAKUPS:         CMP     #'a'
                BCC     @r
                CMP     #'z'+1
                BCS     @r
                AND     #$5F
@r:             RTS

; --- OCTCNS (GWEVAL.ASM 811) -----------------------------------------------------------

; &O or &H (or & alone: octal) at TXTPTR's "&": the number as an integer in
; the FAC and DX, TXTPTR at the character after it. Hex: up to 4 digits, a
; fifth is Overflow; octal: Overflow past 16 bits. A non-octal digit is
; Syntax error. In INPUT (FLGSCN set, 7a's I2) these print "Overflow" and
; count in FLGOVC instead, for INPUT to ask again.
OCTCNS:         STZ     RDL            ; DX: the value
                STZ     RDH
                JSR     CHRGTR
                JSR     MAKUPS
                CMP     #'O'
                BEQ     LOPOCT
                CMP     #'H'
                BNE     LOPOC2
                LDA     #5             ; CH: the digits allowed
                STA     RCH
LOPHEX:         INCW    TXTPTR
                LDA     (TXTPTR)
                JSR     MAKUPS
                JSR     ISLET2
                BCC     ALPTST
                CMP     #'9'+1         ; neither: the end
                BCS     OCTFIN
                SEC
                SBC     #'0'
                BCC     OCTFIN
                BRA     NXTHEX
ALPTST:         CMP     #'F'+1
                BCS     OCTFIN
                SEC
                SBC     #'A'-10
NXTHEX:         LDX     #4             ; DX * 16, the digit in
@s:             ASL     RDL
                ROL     RDH
                DEX
                BNE     @s
                ORA     RDL
                STA     RDL
                DEC     RCH
                BNE     LOPHEX
CKOVER:         LDA     FLGSCN         ; too many digits
                BNE     :+
                JMP     OVERR
:               LDA     #<OVRMSG
                STA     PTR
                LDA     #>OVRMSG
                STA     PTR+1
                JSR     STROUT
                JSR     CRDO
CONER2:         INC     FLGOVC         ; INPUT's: an error came
                RTS
CONERR:         LDA     FLGSCN
                BNE     CONER2
                JMP     SNERR
LOPOC2:         DECW    TXTPTR         ; that character again
LOPOCT:         JSR     CHRGTR
                BCC     OCTFIN         ; not a digit: the end
                CMP     #'8'
                BCC     :+
                JMP     CONERR
:               LDX     #3             ; DX * 8
@s:             ASL     RDL
                ROL     RDH
                BCS     CKOVER
                DEX
                BNE     @s
                SEC
                SBC     #'0'
                CLC
                ADC     RDL
                STA     RDL
                BCC     LOPOCT
                INC     RDH
                BRA     LOPOCT
HEXFIN:
OCTFIN:
HOCFIN:         LDA     RDL            ; MAKINT: the FAC an integer
                STA     FACLO
                LDA     RDH
                STA     FACLO+1
                LDA     #VT_INT
                STA     VALTYP
                RTS

; --- ISMID$ (GWEVAL.ASM 1524) -----------------------------------------------------------

; A statement token NEWSTX didn't take: $FF and MID$ is MID$ = (LHSMID,
; P3's, through res/far3.s's stub); anything else is Syntax error.
ISMID:          CMP     #$FF - $81
                BEQ     :+
                JMP     SNERR
:               INCW    TXTPTR
                LDA     (TXTPTR)
                INCW    TXTPTR
                CMP     #MIDTK
                BEQ     :+
                JMP     SNERR
:               JMP     LHSMID

.rodata
OVRMSG:         .byte   "Overflow", 0  ; GWDATA.ASM's $OVMSG, as INPUT prints it

.segment "RAM"
FLGSCN:         .byte   0              ; GWDATA.ASM's: INPUT is scanning (I2's)

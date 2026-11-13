; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; MATH1.ASM, translated: the resident part (hw/oodos.md, "The plan, phase
; 7": single and integer arithmetic). In the 8086's order:
;
;   $OVFLS $DIV0S DOINF TRAPER  Overflow and Division by zero
;   UTILS                       zero, sign, type, compare, move, negate, .5
;   F4                          the interpreter's entries: integer
;                               arithmetic, compare and conversion
;   $AEXPS $SEXPS               the exponents of a product
;   $FADDS $FSUBS               single add and subtract
;   SDIV $FDIVS                 single divide
;
; The rest is block P1's (p1/): number output, $FIN, double precision, the
; powers of ten and the functions; the far call stubs to it are
; res/mathfar.s. Left out: the PRINT USING routines (7b's) and the variable
; search inner loop (LOPFND, BIPTRG's).
;
; Where the 8086 points BX or DI at a number, this passes a zero page
; address in X, or a memory address in PTR (gwbasic.inc).

.include "gwbasic.inc"

.export OVFLS, DIV0S, STROUT, DZERO, ZERO, SIGNS, SIGNAL, GETYP, FADDH
.export MOVFR, MOVRF, FCOMP, COMPM, VABS, VNEG, NEG, INEG, VMOVM, VMVMF, VMVFM
.export VCMPM, VPSHF, VPOPF, VINT, FADD, FSUB, FMULT, FDIV, CONSIH, ISUB
.export IADD, IMULT, IDIV, PUSHF, CHKSTR, INEG2, VMOVFM, VMOVE, VSIGN, CONIA
.export MAKINT, VALSNG, SGN, FDIVT, FIXER, ICOMP, IMOD, AEXPS
.export FADDS, FSUBS, FDIVS, POPR, VFAC, VMOVAF
.export ZCMPCK, ZEREXP, CPM05, CPM06, CPM10, SIS05
.import FRCINT, CSI, FMULS, NORMS, ROUNS, INT, FLT, SPUSH, SPOP
.import TMERR, OVERR, DV0ERR, OUTDO, CRFIN

.code

; --- $OVFLS, $DIV0S ------------------------------------------------------------

; Put the right infinity in the FAC; trap the error if ON ERROR is set, or
; print the message (once, if $FLGOV says so) and carry on.
OVFLS:          JSR     DOINF
                LDA     ONELIN
                ORA     ONELIN+1
                BEQ     :+
                JMP     OVERR
:               LDA     #<OVMSG
                LDX     #>OVMSG
                BRA     DV010
DIV0S:          JSR     DOINF
                LDA     ONELIN
                ORA     ONELIN+1
                BEQ     :+
                JMP     DV0ERR
:               LDA     #<DIV0M
                LDX     #>DIV0M
DV010:          STA     PTR
                STX     PTR+1
                LDA     FLGOV
                CMP     #1
                BCC     DV015          ; 0: print
                BNE     DV020          ; 2: don't
                LDA     #2             ; 1: print, and not again
                STA     FLGOV
DV015:          LDA     PTRFIL+1       ; $STPRN, $CATTY: to the console, whatever
                PHA                    ; the output (LPRINT's LPT1)
                LDA     PTRFIL
                PHA
                STZ     PTRFIL
                STZ     PTRFIL+1
                JSR     STROUT
                LDA     #13
                JSR     OUTDO
                LDA     #10
                JSR     OUTDO
                PLA
                STA     PTRFIL
                PLA
                STA     PTRFIL+1
DV020:          RTS

; DOINF: +infinity for zero to a negative power (BH zero), else the FAC's
; sign's.
DOINF:          LDY     #0
                LDA     RBH
                BEQ     INF10
                BIT     FACSGN
                BPL     INF10
                LDY     #INFMD - INFPD
INF10:          JSR     GETYP
                BCS     INF20
                LDX     #0             ; single: the second half
INF15:          LDA     INFPD+4,Y
                STA     FACLO,X
                INY
                INX
                CPX     #4
                BNE     INF15
                RTS
INF20:          LDX     #0             ; double
INF25:          LDA     INFPD,Y
                STA     DFACL,X
                INY
                INX
                CPX     #8
                BNE     INF25
                RTS

; --- UTILS -------------------------------------------------------------------------

; STROUT: print the string at PTR, to its zero; after a CR, CRFIN.
STOULP:         JSR     OUTDO
                CMP     #13
                BNE     STROUT
                JSR     CRFIN
STROUT:         LDA     (PTR)
                INCW    PTR
                ORA     #0
                BNE     STOULP
                RTS

DZERO:          LDX     #7             ; zero a double
@z:             STZ     DFACL,X
                DEX
                BPL     @z
                RTS

ZERO:           STZ     FACLO          ; zero the FAC
                STZ     FACLO+1
                STZ     FACM1
                STZ     FACEXP
                RTS

; $SIGNS: A = 0, 1 or $FF, with Z and N, for the FAC zero, positive or
; negative.
SIGNS:          JSR     GETYP
                BNE     SIS01
                JMP     TMERR
SIS01:          BPL     SIS05
                LDA     FACLO          ; an integer
                ORA     FACLO+1
                BEQ     SIS10
                LDA     FACLO+1
                BRA     SIGNAL
SIS05:          LDA     FACEXP
                BEQ     SIS10
                LDA     FACM1
SIGNAL:         ASL     A              ; the sign bit decides
                LDA     #1
                BCC     SIS10
                LDA     #$FF
SIS10:          RTS

; $GETYP: A = VALTYP-3, so Z for a string and N for an integer; C set for a
; double. (The 8086's carry is the other way round, and its parity flag picks
; out a single: here that is none of Z, N and C.)
GETYP:          LDA     VALTYP
                CMP     #VT_DBL
                DEC     A
                DEC     A
                DEC     A
                RTS

; $FADDH: add .5. ($VADDH and $DADDH are P1's.)
FADDH:          STZ     RDL            ; BXDX = .5
                STZ     RDH
                STZ     RBL
                LDA     #$80
                STA     RBH
                JMP     FADDS

; $MOVFR, $MOVRF: BXDX to the FAC, and back.
MOVFR:          LDX     #3
@m:             LDA     RDL,X
                STA     FACLO,X
                DEX
                BPL     @m
                RTS
MOVRF:          LDX     #3
@m:             LDA     FACLO,X
                STA     RDL,X
                DEX
                BPL     @m
                RTS

; ZCMPCK: a zero exponent on either side decides the compare. C set if it
; did, with the result in A.
ZCMPCK:         LDA     RBH
                BNE     NZCOMP
                JSR     SIS05          ; the comperand is zero: the FAC's sign
                SEC
                RTS
NZCOMP:         LDA     FACEXP
                BNE     ZCMPRT
                LDA     RBL            ; the FAC is zero: the other's sign, reversed
                EOR     #$FF
                JSR     SIGNAL
                SEC
                RTS
ZCMPRT:         CLC
                RTS

; $FCOMP: the FAC against BXDX. A = 1 if the FAC is greater, 0 if equal, $FF
; (and C set) if less; Z and N follow A.
FCOMP:          JSR     ZCMPCK
                BCS     ZEREXP
                LDA     RBL
                EOR     FACM1
                BMI     CPM05          ; the signs differ
                LDA     RBL
                BMI     UFC10
                LDA     FACEXP         ; both positive
                CMP     RBH
                BNE     TSTFLG
                LDA     FACM1
                CMP     RBL
                BNE     TSTFLG
                LDA     FACLO+1
                CMP     RDH
                BNE     TSTFLG
                LDA     FACLO
                CMP     RDL
                BRA     TSTFLG
UFC10:          LDA     RBH            ; both negative: the larger magnitude is less
                CMP     FACEXP
                BNE     TSTFLG
                LDA     RBL
                CMP     FACM1
                BNE     TSTFLG
                LDA     RDH
                CMP     FACLO+1
                BNE     TSTFLG
                LDA     RDL
                CMP     FACLO
TSTFLG:         BCC     CPM10
                BNE     CPM06
                LDA     #0
                CLC
                RTS
CPM05:          LDA     FACM1
                BMI     CPM10
CPM06:          LDA     #1
                CLC
                RTS
CPM10:          LDA     #$FF
                SEC
                RTS
ZEREXP:         CMP     #$FF           ; C for "less", then Z and N from A
                ORA     #0
                RTS

; $COMPM: the FAC against the single at PTR.
COMPM:          LDY     #3
                LDX     #3
@l:             LDA     (PTR),Y
                STA     RDL,X
                DEY
                DEX
                BPL     @l
                JMP     FCOMP

; $VABS, $VNEG, $NEG, $INEG.
VABS:           JSR     GETYP
                BMI     IABS
                LDA     FACM1
                BMI     NEG
                RTS
IABS:           LDA     FACLO+1
                BMI     VN15
                RTS
VNEG:           JSR     GETYP
                BMI     INEG
NEG:            LDA     FACM1
                EOR     #$80
                STA     FACM1
                RTS
INEG:
VN15:           LDA     FACLO+1        ; -32768 becomes the single 32768
                CMP     #$80
                BNE     VN20
                LDA     FACLO
                BNE     VN20
                JSR     CSI
                JMP     NEG
VN20:           SEC
                LDA     #0
                SBC     FACLO
                STA     FACLO
                LDA     #0
                SBC     FACLO+1
                STA     FACLO+1
                RTS

; $VMOVM, $VMVMF, $VMVFM, $VCMPM: by type, between zero page numbers (X the
; source, Y the destination), and the FAC. Only $SIGD uses them here.
VMOVM:          STX     T0
                STY     T1
VMOVM1:         JSR     VLEN
                STA     T2
@m:             LDX     T0
                LDA     0,X
                LDX     T1
                STA     0,X
                INC     T0
                INC     T1
                DEC     T2
                BNE     @m
                RTS
VMVMF:          STX     T1             ; the FAC to X
                JSR     VFAC
                STX     T0
                BRA     VMOVM1
VMVFM:          STX     T0             ; X to the FAC
                JSR     VFAC
                STX     T1
                BRA     VMOVM1
VCMPM:          JSR     GETYP          ; the FAC against X
                BCC     @s
                FJSR    DCMPM          ; a double: P1's
                RTS
@s:             STX     T0
                LDX     #0
@l:             STX     T1
                LDX     T0
                LDA     0,X
                INC     T0
                LDX     T1
                STA     RDL,X
                INX
                CPX     #4
                BNE     @l
                JMP     FCOMP
VLEN:           LDA     #4             ; a number's length: 4 or 8
                LDX     VALTYP
                CPX     #VT_DBL
                BNE     :+
                LDA     #8
:               RTS
VFAC:           LDX     #FACLO         ; where it starts in the FAC: VDFACS
                LDA     VALTYP
                CMP     #VT_DBL
                BNE     :+
                LDX     #DFACL
:               RTS

; $VPSHF, $VPOPF: the FAC to and from the BASIC stack, by type.
VPSHF:          JSR     VLEN
                PHA
                JSR     VFAC
                PLA
                JMP     SPUSH
VPOPF:          JSR     VLEN
                PHA
                JSR     VFAC
                PLA
                JMP     SPOP

; $VINT: INT() by type.
VINT:           JSR     GETYP
                BMI     VIN00
                BCC     VIN10
                FJSR    DINT           ; a double: P1's
                RTS
VIN10:          JMP     INT
VIN00:          RTS

; --- F4: the interpreter's entries ------------------------------------------------

; The 8086 swaps BX and CX before each: the interpreter's left operand comes
; in CX:DX and the text pointer in BX. Here the operand is already in BXDX
; and the text pointer is TXTPTR, so these are the routines themselves.
; (The double ones, DADD DSUB DMULT DDIV DCOMP, are far call stubs:
; res/mathfar.s.)
FADD            = FADDS
FSUB            = FSUBS
FDIV            = FDIVS
FMULT           = FMULS

; CONSIH: the integer in BX to a single in the FAC.
CONSIH:         LDA     RBL
                STA     FACLO
                LDA     RBH
                STA     FACLO+1
                JMP     CSI

; ISUB: BX = DX - BX, overflowing into single precision. Only -32768 can't
; be negated, so that one is done in single precision and forced back.
ISUB:           LDA     RBH
                CMP     #$80
                BNE     IS10
                LDA     RBL
                BNE     IS10
                JSR     FLT            ; DX as a single
                STZ     RDL            ; BXDX = -32768.0
                STZ     RDH
                LDA     #$80
                STA     RBL
                LDA     #$90
                STA     RBH
                JSR     FSUBS
                JSR     NEG
                JMP     FRCINT
IS10:           SEC                    ; NEG BX
                LDA     #0
                SBC     RBL
                STA     RBL
                LDA     #0
                SBC     RBH
                STA     RBH
; IADD: BX = DX + BX; if it overflows, the sum of the two as singles.
IADD:           CLC
                LDA     RDL
                ADC     RBL
                STA     T0
                LDA     RDH
                ADC     RBH
                BVS     IAD10
                STA     RBH
                LDA     T0
                STA     RBL
                JMP     MAKINT
IAD10:          PUSHRET FADDS
; Float both and go on to the single routine pushed, as IAD10 and IM10 do.
; (Not JMP (PTR2): that reads zero page by an absolute address, rule 2.)
FLOAT2:         LDA     RBL            ; the right one
                PHA
                LDA     RBH
                PHA
                JSR     FLT            ; the left one, DX
                PLA
                STA     RDH
                PLA
                STA     RDL
                LDA     FACLO          ; push it
                PHA
                LDA     FACLO+1
                PHA
                LDA     FACM1
                PHA
                LDA     FACEXP
                PHA
                JSR     FLT            ; the right one
                PLA                    ; and the left one in BXDX
                STA     RBH
                PLA
                STA     RBL
                PLA
                STA     RDH
                PLA
                STA     RDL
                RTS

; IMULT: BX = BX * DX, or the single product if it doesn't fit.
IMULT:          JSR     IMUL16
                BCS     IM10
                LDA     T0
                STA     RBL
                LDA     T1
                STA     RBH
                JMP     MAKINT
IM10:           PUSHRET FMULS
                BRA     FLOAT2

; IDIV: BX = DX \ BX. -32768 \ -1 is the single 32768.
IDIV:           LDA     RBL
                ORA     RBH
                BNE     IDIV10
IDIV0:          LDA     RDH            ; the sign of the infinity
                STA     FACSGN
                INC     RBH            ; and a non-zero BH for DOINF
                LDA     #VT_SNG
                STA     VALTYP
                JMP     DIV0S
IDIV10:         JSR     MINCHK
                BNE     IDIV15
                JSR     ONECHK
                BEQ     IDIV15
                BPL     IDIV11
                JSR     FLT            ; -32768 \ -1
                JMP     NEG
IDIV11:         LDA     RDL            ; -32768 \ 1
                STA     RBL
                LDA     RDH
                STA     RBH
                JMP     MAKINT
IDIV15:         JSR     IDIV16
                LDA     T0
                STA     RBL
                LDA     T1
                STA     RBH
                JMP     MAKINT

; IMOD: FACLO = DX MOD BX, with the dividend's sign.
IMOD:           LDA     RBL
                CMP     RDL
                BNE     IMD10
                LDA     RBH
                CMP     RDH
                BNE     IMD10
IMD05:          STZ     RBL
                STZ     RBH
                BRA     IMD40
IMD10:          JSR     MINCHK         ; -32768 MOD 1 or -1
                BNE     IMD15
                JSR     ONECHK
                BNE     IMD05
IMD15:          LDA     RBL
                ORA     RBH
                BEQ     IDIV0
                JSR     IDIV16
                LDA     T2
                STA     RBL
                LDA     T3
                STA     RBH
IMD40:          LDA     RBL
                STA     FACLO
                LDA     RBH
                STA     FACLO+1
                RTS

; Z if DX is -32768.
MINCHK:         LDA     RDH
                CMP     #$80
                BNE     :+
                LDA     RDL
:               RTS
; A = 1 if BX is 1, $FF if it is -1, else 0; N and Z by A.
ONECHK:         LDA     RBH
                BNE     @m
                LDA     RBL
                CMP     #1
                BEQ     @one
@no:            LDA     #0
                RTS
@one:           LDA     #1
                RTS
@m:             CMP     #$FF
                BNE     @no
                LDA     RBL
                CMP     #$FF
                BNE     @no
                LDA     #$FF
                RTS

; IMUL16 (the 8086's IMUL): T3..T0 = DX * BX, signed; C set if the product
; doesn't fit in 16 bits. Keeps DX and BX.
IMUL16:         LDA     RDH
                EOR     RBH
                PHA                    ; the product's sign
                LDX     #RDL
                JSR     ABS16
                STA     T4
                STY     T5
                LDX     #RBL
                JSR     ABS16
                STA     T6
                STY     T7
                STZ     T2
                STZ     T3
                LDX     #16
@bit:           LSR     T7
                ROR     T6
                BCC     @shift
                CLC
                LDA     T2
                ADC     T4
                STA     T2
                LDA     T3
                ADC     T5
                STA     T3
@shift:         ROR     T3
                ROR     T2
                ROR     T1
                ROR     T0
                DEX
                BNE     @bit
                PLA
                BPL     @fit
                LDX     #0             ; negative: negate all 32 bits
                LDY     #4
                SEC
@neg:           LDA     #0
                SBC     T0,X
                STA     T0,X
                INX
                DEY
                BNE     @neg
@fit:           LDX     #0             ; the high half must copy bit 15
                LDA     T1
                BPL     :+
                DEX
:               CPX     T2
                BNE     @no
                CPX     T3
                BNE     @no
                CLC
                RTS
@no:            SEC
                RTS

; IDIV16 (the 8086's IDIV): T1:T0 = DX / BX truncated, T3:T2 the remainder
; with DX's sign.
IDIV16:         LDX     #RBL
                JSR     ABS16
                STA     T4
                STY     T5
                LDX     #RDL
                JSR     ABS16
                STA     T0
                STY     T1
                STZ     T2
                STZ     T3
                LDX     #16
@bit:           ASL     T0
                ROL     T1
                ROL     T2
                ROL     T3
                SEC
                LDA     T2
                SBC     T4
                TAY
                LDA     T3
                SBC     T5
                BCC     @next
                STA     T3
                STY     T2
                INC     T0
@next:          DEX
                BNE     @bit
                LDA     RDH            ; the quotient's sign
                EOR     RBH
                BPL     @rem
                LDX     #T0
                JSR     NEG16
@rem:           LDA     RDH            ; the remainder's
                BPL     @done
                LDX     #T2
                JSR     NEG16
@done:          RTS

; A:Y = |the word at X|; NEG16 negates the word at X.
ABS16:          LDA     0,X
                LDY     1,X
                CPY     #$80
                BCC     @pos
                SEC
                EOR     #$FF
                ADC     #0
                PHA
                TYA
                EOR     #$FF
                ADC     #0
                TAY
                PLA
@pos:           RTS
NEG16:          SEC
                LDA     #0
                SBC     0,X
                STA     0,X
                LDA     #0
                SBC     1,X
                STA     1,X
                RTS

; PUSHF: the FAC onto the BASIC stack; POPR: from it into BXDX.
PUSHF:          LDX     #FACLO
                LDA     #4
                JMP     SPUSH
POPR:           LDX     #RDL
                LDA     #4
                JMP     SPOP

; CHKSTR: Type mismatch unless a string.
CHKSTR:         JSR     GETYP
                BEQ     :+
                JMP     TMERR
:               RTS

; INEG2: the unsigned integer in BX to a single. FLOATR floats CL:DX with
; the exponent in CH.
INEG2:          LDA     RBL
                STA     RDL
                LDA     RBH
                STA     RDH
                JSR     VALSNG
                LDA     #0
                LDX     #$98           ; INEGAD: 2^24
FLOATR:         STX     FACEXP
                STA     RCL
                STZ     RCH            ; no overflow byte
                STZ     FACSGN
                ASL     A
                BCC     FL10           ; FADFLT
                JSR     NEGR
FL10:           LDA     RCH
                STA     RAH
                LDA     RCL
                STA     RBL
                JMP     NORMS

; VMOVFM: the variable at PTR to the FAC, VALTYP bytes. VMOVE: the FAC to the
; variable at PTR.
VMOVFM:         LDX     #FACLO
                LDA     VALTYP
                CMP     #VT_DBL
                BNE     :+
                LDX     #DFACL
:               LDY     #0
@m:             LDA     (PTR),Y
                STA     0,X
                INX
                INY
                CPY     VALTYP
                BNE     @m
                RTS
VMOVE:          LDX     #FACLO
                LDA     VALTYP
                CMP     #VT_DBL
                BNE     :+
                LDX     #DFACL
:               LDY     #0
@m:             LDA     0,X
                STA     (PTR),Y
                INX
                INY
                CPY     VALTYP
                BNE     @m
                RTS

; VMOVAF: the FAC to the ARG (a double).
VMOVAF:         LDX     #7
@m:             LDA     DFACL,X
                STA     ARGLO,X
                DEX
                BPL     @m
                RTS

; VSIGN: the FAC's sign by type, as $SIGNS.
VSIGN:          JSR     GETYP
                BNE     VS10
                JMP     TMERR
VS10:           BMI     ISI10
                JMP     SIGNS
ISI10:          LDA     FACLO
                ORA     FACLO+1
                BEQ     ISIGN1
                LDA     FACLO+1
                BMI     @n
                LDA     #1
                RTS
@n:             LDA     #$FF
ISIGN1:         RTS

; CONIA: A, signed, as an integer. MAKINT: BX as the integer result.
CONIA:          STA     RBL
                LDX     #0
                CMP     #$80
                BCC     :+
                DEX
:               STX     RBH
MAKINT:         LDA     #VT_INT
                STA     VALTYP
                LDA     RBL
                STA     FACLO
                LDA     RBH
                STA     FACLO+1
                RTS
VALSNG:         LDA     #VT_SNG
                STA     VALTYP
                RTS

; SGN() and FDIVT, the formula evaluator's "/" (the left operand stacked).
SGN:            JSR     VSIGN
                JMP     CONIA
FDIVT:          JSR     POPR
                JMP     FDIVS

; NEGR: negate CL:DX:CH and the sign.
NEGR:           LDA     FACSGN
                EOR     #$80
                STA     FACSGN
                SEC
                LDA     #0
                SBC     RCH
                STA     RCH
                LDA     #0
                SBC     RDL
                STA     RDL
                LDA     #0
                SBC     RDH
                STA     RDH
                LDA     #0
                SBC     RCL
                STA     RCL
NG10:           RTS

; FIXER: FIX(), INT() towards zero.
FIXER:          JSR     GETYP
                BMI     NG10
                JSR     SIGNS
                BMI     F4FX20
                JMP     VINT
F4FX20:         JSR     NEG
                JSR     VINT
                JMP     NEG

; ICOMP: DX against BX: A = 1 if DX < BX, 0 if equal, $FF (C set) if
; greater, as $FCOMP with the FAC on the right.
ICOMP:          SEC
                LDA     RBL
                SBC     RDL
                STA     T0
                LDA     RBH
                SBC     RDH
                STA     T1
                ORA     T0
                BEQ     IC40
                LDA     T1             ; the sign, through any overflow
                BVC     :+
                EOR     #$80
:               BMI     IC30
                LDA     #1
                CLC
                RTS
IC30:           LDA     #$FF
                SEC
                RTS
IC40:           CLC                    ; A is 0
                RTS

; --- $AEXPS, $SEXPS: the exponent of a product or quotient ----------------------

; The FAC's and BH's exponents added ($AEXPS) or subtracted ($SEXPS) into the
; FAC; the sign into FACSGN; the implied bits set in FACM1 and BL, BH zero.
; Overflow and underflow return to the caller's caller, as the 8086's ADD
; SP,2 does.
AEXPS:          SEC
                BRA     SES00
SEXPS:          CLC
SES00:          PHP
                LDA     RBL
                EOR     FACM1
                STA     FACSGN
                PLP
                BCC     SES05
                LDA     RBH            ; AX = BH + FAC - $101
                CLC
                ADC     FACEXP
                STA     T0
                LDA     #0
                ROL     A
                STA     T1
                SEC
                LDA     T0
                SBC     #1
                STA     T0
                LDA     T1
                SBC     #1
                STA     T1
                BRA     SES07
SES05:          LDA     RBH            ; AX = BH - FAC
                SEC
                SBC     FACEXP
                STA     T0
                LDA     #0
                SBC     #0
                STA     T1
SES07:          LDA     T1
                BMI     SES10
                BNE     @ovf
                LDA     T0
                CMP     #$80
                BCC     SES20
@ovf:           PLA
                PLA
                JMP     OVFLS
SES10:          LDA     T0             ; maybe underflow
                CLC
                ADC     #$80
                STA     T0
                LDA     T1
                ADC     #0
                BPL     SES30
                PLA
                PLA
                JMP     ZERO
SES20:          LDA     T0
                CLC
                ADC     #$80
                STA     T0
SES30:          LDA     T0
                STA     FACEXP
                LDA     FACM1
                ORA     #$80
                STA     FACM1
                STZ     RBH
                LDA     RBL
                ORA     #$80
                STA     RBL
                RTS

; --- $FADDS, $FSUBS: FAC = BXDX + FAC, or BXDX - FAC ---------------------------

; The larger number goes in AX:SI (AH the exponent), the smaller in BX:DX,
; which shifts right until the points line up, its bits falling into AH;
; bit 5 of AH is sticky. Then the mantissas are added or subtracted.
FEXIT1:         JSR     MOVFR          ; BXDX is the answer
EXIT2:          RTS
FSUBS:          LDA     FACEXP
                BEQ     FEXIT1
                LDA     FACM1
                EOR     #$80
                STA     FACM1
FADDS:          LDA     RBH
                BEQ     EXIT2          ; adding zero
                LDA     FACEXP
                BEQ     FEXIT1
                LDA     FACM1          ; AX:SI = the FAC
                STA     RAL
                STA     FACSGN
                LDA     FACEXP
                STA     RAH
                LDA     FACLO
                STA     RSI
                LDA     FACLO+1
                STA     RSI+1
                LDA     RAH            ; CL = the difference of the exponents
                SEC
                SBC     RBH
                STA     RCL
                BCS     FA20
                EOR     #$FF           ; BXDX is larger: swap
                INC     A
                STA     RCL
                LDA     RBH
                STA     FACEXP
                LDA     RBL
                STA     FACSGN
                LDX     #1             ; XCHG DX,SI
@swap:          LDA     RDL,X
                LDY     RSI,X
                STA     RSI,X
                STY     RDL,X
                DEX
                BPL     @swap
                LDA     RBL            ; XCHG BX,AX
                LDY     RAL
                STA     RAL
                STY     RBL
                LDA     RBH
                LDY     RAH
                STA     RAH
                STY     RBH
FA20:           LDA     RAL            ; the signs: add or subtract?
                EOR     RBL
                STA     FSUBF
                LDA     RAL            ; the understood 1s
                ORA     #$80
                STA     RAL
                LDA     RBL
                ORA     #$80
                STA     RBL
                STZ     RAH            ; the overflow byte
                STZ     RBH
FA22:           LDA     RCL
                BEQ     FA40           ; lined up
                CMP     #25
                BCC     FA23
                LDA     RSI            ; too far apart: the larger is the answer
                STA     FACLO
                LDA     RSI+1
                STA     FACLO+1
                LDA     RAL
                AND     #$7F
                BIT     FACSGN
                BPL     :+
                ORA     #$80
:               STA     FACM1
                RTS
FA23:           CMP     #8             ; whole bytes first
                BCC     FA27
                LDX     RAH            ; a byte falling out of AH is sticky
                LDA     RDL
                STA     RAH
                CPX     #0
                BEQ     FA24
                ORA     #$20
                STA     RAH
FA24:           LDA     RDH
                STA     RDL
                LDA     RBL
                STA     RDH
                STZ     RBL
                LDA     RCL
                SEC
                SBC     #8
                STA     RCL
                LDA     RAH
                BIT     #$1F           ; and so are bits below ST
                BEQ     FA22
                ORA     #$20
                STA     RAH
                BRA     FA22
FA25:           LDA     RAH            ; shifted through ST: put it back
                ORA     #$20
                STA     RAH
                DEC     RCL
                BNE     FA30
                BRA     FA40
FA27:
FA30:           LSR     RBL
                ROR     RDH
                ROR     RDL
                ROR     RAH
                LDA     RAH
                AND     #$10
                BNE     FA25
                DEC     RCL
                BNE     FA30
FA40:           BIT     FSUBF
                BPL     FA50
                SEC                    ; subtract: AX:SI - BX:DX
                LDA     #0
                SBC     RAH
                STA     RAH
                LDA     RSI
                SBC     RDL
                STA     RDL
                LDA     RSI+1
                SBC     RDH
                STA     RDH
                LDA     RAL
                SBC     RBL
                STA     RBL
                BCS     FA90
                LDA     FACSGN         ; the FAC wasn't larger: negate
                EOR     #$FF
                STA     FACSGN
                SEC
                LDA     #0
                SBC     RAH
                STA     RAH
                LDA     #0
                SBC     RDL
                STA     RDL
                LDA     #0
                SBC     RDH
                STA     RDH
                LDA     #0
                SBC     RBL
                STA     RBL
                BRA     FA90
FA50:           CLC                    ; add
                LDA     RDL
                ADC     RSI
                STA     RDL
                LDA     RDH
                ADC     RSI+1
                STA     RDH
                LDA     RBL
                ADC     RAL
                STA     RBL
                BCC     FA70
FA60:           INC     FACEXP         ; a carry out: one bit right
                BEQ     FA80
                ROR     RBL
                ROR     RDH
                ROR     RDL
                ROR     RAH
FA70:           JMP     ROUNS
FA80:           JMP     OVFLS
FA90:           JMP     NORMS

; --- SDIV, $FDIVS: FAC = BXDX / FAC ------------------------------------------------

; A 32-bit quotient of the mantissas, a bit at a time (the 8086: two 16-bit
; DIVs), a sticky bit for the remainder, and round to even. The remainder is
; CL:CH:SI, with SI+1 its 25th bit; the divisor is FACLO, FACLO+1 and DI.
FDIVS:          JSR     SIGNS
                BNE     FDS00
                LDA     RBL            ; the sign of the infinity
                STA     FACSGN
                JMP     DIV0S
FDS00:          LDA     RBH
                BNE     SDIV
FDS05:          JMP     ZERO
SDIV:           LDA     RBL            ; the sign
                EOR     FACM1
                STA     FACSGN
                STA     T6
                LDA     RBH            ; the exponent, unbiased, through V
                EOR     #$80
                STA     T7
                LDA     FACEXP
                EOR     #$80
                STA     T5
                LDA     T7
                SEC
                SBC     T5
                BVC     @1
                BPL     FDS05          ; OVCHKJ
                JMP     OVFLS
@1:             EOR     #$80
                STA     T7
                LDA     RDL            ; the remainder starts as the dividend
                STA     RCL
                LDA     RDH
                STA     RCH
                LDA     RBL
                ORA     #$80
                STA     RSI
                STZ     RSI+1
                LDA     FACM1
                ORA     #$80
                STA     RDI
                LDX     #32
@bit:           SEC
                LDA     RCL
                SBC     FACLO
                STA     T0
                LDA     RCH
                SBC     FACLO+1
                STA     T1
                LDA     RSI
                SBC     RDI
                STA     T2
                LDA     RSI+1
                SBC     #0
                BCC     @q
                LDA     T0
                STA     RCL
                LDA     T1
                STA     RCH
                LDA     T2
                STA     RSI
                STZ     RSI+1
@q:             ROL     RDL            ; the quotient bit
                ROL     RDH
                ROL     RBL
                ROL     RBH
                DEX
                BEQ     @done
                ASL     RCL
                ROL     RCH
                ROL     RSI
                ROL     RSI+1
                BRA     @bit
@done:          LDA     RCL            ; sticky
                ORA     RCH
                ORA     RSI
                BEQ     NORMCHK
                LDA     RDL
                ORA     #1
                STA     RDL
NORMCHK:        LDA     RBH
                BMI     INCEXP
                ASL     RDL            ; normalise
                ROL     RDH
                ROL     RBL
                ROL     RBH
                LDA     T7
                BNE     SROUND
                JMP     ZERO
INCEXP:         INC     T7
                BEQ     SDIVOVFL
SROUND:         LDA     RDL            ; $SRND: the extended bits
                CMP     #$80
                BCC     SAVE
                BNE     ROUNDUP
                LDA     RDH
                LSR     A
                BCC     SAVE
ROUNDUP:        INC     RDH
                BNE     SAVE
                INC     RBL
                BNE     SAVE
                INC     RBH
                BNE     SAVE
                INC     T7
                BNE     SAVE
SDIVOVFL:       JMP     OVFLS
SAVE:           LDA     RBH
                AND     #$7F
                BIT     T6
                BPL     :+
                ORA     #$80
:               STA     FACM1
                LDA     T7
                STA     FACEXP
                LDA     RBL
                STA     FACLO+1
                LDA     RDH
                STA     FACLO
                RTS

.rodata

; The messages (GWDATA's in the 8086). $FOUT's constants are P1's.
OVMSG:          .byte   "Overflow", 0
DIV0M:          .byte   "Division by zero", 0
; The infinities: a double, then a single, positive then negative.
INFPD:          .byte   $FF,$FF,$FF,$FF,$FF,$FF,$7F,$FF
INFMD:          .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF

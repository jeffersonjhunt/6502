; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; The functions, in block P1 (hw/oodos.md, "The plan, phase 7"), from
; MATH1.ASM and MATH2.ASM:
;
;   $SQR                    square root, a bit at a time (MATH1)
;   $FPWR XTON              x^y, and x^n by squaring (MATH1)
;   $EXP                    e^x by Hart 1302 (MATH1)
;   $LOG                    by Hart 2524 (MATH2)
;   $SIN $COS $TAN RR       by Hart 3341, reduced in double precision (MATH1)
;   $ATAN                   by Hart 4940 (MATH1)
;   $POLY $POLYX            Horner's rule, and x*P(x^2) (MATH2)
;   $RND                    the linear congruential generator (MATH2)
;
; The resident core's FUNDSP reaches them through its stubs (res/mathfar.s),
; ^ through FPWRQ. The 8086 keeps x and partial results on its stack; here
; numbers go on the BASIC stack (PUSHF, POPR) and flags and return addresses
; on the 6502's, in the 8086's order. $POLY's x and coefficient pointer are
; in zero page (PX, PCP, PCN): it is never re-entered.

.include "gwbasic.inc"

;@entry SQR
;@entry FPWR
;@entry EXP
;@entry LOG
;@entry SIN
;@entry COS
;@entry TAN
;@entry ATN
;@entry RND
.export SQR, FPWR, EXP, LOG, SIN, COS, TAN, ATN, RND, POLY, POLYX, XTON
.import FCERR, SIGNS, ZERO, DZERO, DIV0S, OVFLS, INT, QINT, FCOMP, MOVFR, MOVRF
.import FMULS, FDIVS, FADDS, FSUBS, FLT, NORMS, CSD, CDS, NEG, PUSHF, POPR
.import SPUSH, SPOP, VPSHF, VPOPF, VMOVAF, FADDD, FSUBD, FMULD, DINT, MOVAC
.import DP00, RNDX

; BXDX = a single constant: LDBX lo, mid, sign|high, exponent.
.macro  LDBX    b0, b1, b2, b3
                LDA     #b0
                STA     RDL
                LDA     #b1
                STA     RDH
                LDA     #b2
                STA     RBL
                LDA     #b3
                STA     RBH
.endmacro
; PUSHB, POPF: BXDX onto the BASIC stack; the top of it into the FAC.
.macro  PUSHB
                LDX     #RDL
                LDA     #4
                JSR     SPUSH
.endmacro
.macro  POPF
                LDX     #FACLO
                LDA     #4
                JSR     SPOP
.endmacro

.code

; --- $SQR (MATH1.ASM 2177) ---------------------------------------------------------

; FC error for a negative number (even a negative zero). The exponent is
; halved (the mantissa too, if it is odd); then 25 bits of root from the
; mantissa's 32 (DX:BX, shifted into BP:DI two bits a step), the partial
; root (twice the root) in AX:SI; the 25th bit rounds the 24th up.
SQR:            LDA     FACM1
                BPL     :+
                JMP     FCERR
:               LDA     FACEXP
                BNE     :+
                RTS                    ; SQR(0) = 0
:               LDA     FACLO+1        ; DX = the mantissa's top 16 bits
                STA     RDL
                LDA     FACM1
                ORA     #$80
                STA     RDH
                STZ     RBL            ; BX = its low byte, then 0
                LDA     FACLO
                STA     RBH
                LDA     FACEXP
                LSR     A
                BCC     EVENEX
                LSR     RDH            ; odd: half the mantissa
                ROR     RDL
                ROR     RBH
                ROR     RBL
                SEC
EVENEX:         ADC     #$40           ; half the bias back
                STA     FACEXP
                LDX     #5             ; BP DI SI AX = 0
@z:             STZ     RSI,X
                DEX
                BPL     @z
                STZ     RAL
                STZ     RAH
                LDA     #25
                STA     RCL
RTLP:           LDX     #2             ; the input two bits left
@sh:            ASL     RBL
                ROL     RBH
                ROL     RDL
                ROL     RDH
                ROL     RDI
                ROL     RDI+1
                ROL     RBP
                ROL     RBP+1
                DEX
                BNE     @sh
                SEC                    ; the test bit into the partial root
                ROL     RSI
                ROL     RSI+1
                ROL     RAL
                ROL     RAH
                SEC                    ; does it fit?
                LDA     RDI
                SBC     RSI
                STA     T0
                LDA     RDI+1
                SBC     RSI+1
                STA     T1
                LDA     RBP
                SBC     RAL
                STA     T2
                LDA     RBP+1
                SBC     RAH
                BCC     @no
                STA     RBP+1
                LDA     T2
                STA     RBP
                LDA     T1
                STA     RDI+1
                LDA     T0
                STA     RDI
                INC     RSI            ; ROOTBT: the bit is 1 (SI's low bit
                BRA     @next          ; is 1 here: no carry)
@no:            DEC     RSI            ; it isn't: 0 (no borrow, likewise)
@next:          DEC     RCL
                BNE     RTLP
FPWRND:         LDX     #2             ; to a 24-bit result
@r:             LSR     RAH
                ROR     RAL
                ROR     RSI+1
                ROR     RSI
                DEX
                BNE     @r
                LDA     RSI            ; rounded up by the bit shifted out
                ADC     #0
                STA     FACLO
                LDA     RSI+1
                ADC     #0
                STA     FACLO+1
                LDA     RAL
                ADC     #0
                AND     #$7F
                STA     FACM1
                RTS

; --- $FPWR: FAC = BXDX ^ FAC (MATH1.ASM 2238) --------------------------------------

; In once-only overflow mode, and back to normal at the end ($CLROV). y = 0
; gives 1 (EXP(0)); 0 to a negative power, Division by zero; 0 to a positive
; one, 0. A negative x needs an integer y. A negative y is x^|y| inverted;
; an integer |y| < 32768 is XTON's, the rest EXP(y*LOG(x)).
FPWR:           PUSHRET CLROV
                LDA     #1
                STA     FLGOV
                JSR     SIGNS          ; y = 0?
                BNE     FP10
                JMP     EXP
FP10:           BPL     FP20
                LDA     RBH
                BNE     FP25
                JMP     DIV0S          ; 0 to a negative power (BH 0: +infinity)
FP20:           LDA     RBH
                BNE     FP25
                JMP     ZERO
FP25:           LDA     RBL            ; x negative?
                BPL     FP30
                LDA     FACEXP         ; then y must be an integer
                CMP     #$99
                BCC     FP27
FPFC:           JMP     FCERR
FP27:           PUSHB                  ; x
                JSR     PUSHF          ; y
                JSR     INT
                JSR     POPR           ; y
                JSR     FCOMP
                PHP
                JSR     MOVFR          ; y back to the FAC
                JSR     POPR           ; x
                PLP
                BNE     FPFC
FP30:           LDA     FACM1          ; y negative?
                BPL     FP30A
                AND     #$7F           ; x^|y|, then inverted
                STA     FACM1
                LDA     RBL            ; x's sign
                PHA
                JSR     PUSHF          ; |y|
                JSR     FP30A
                LDA     FACEXP
                BNE     INVERT
                POPF                   ; underflowed: |y|
                JSR     QINT           ; DL: its low byte
                PLA                    ; negative if x is and y is odd
                STA     T0
                LDA     RDL
                LSR     A
                LDA     #0
                ROR     A
                AND     T0
                STA     FACSGN
                LDA     #$FF           ; BH not zero: the sign is FACSGN's
                STA     RBH
                JMP     OVFLS
INVERT:         PLA                    ; drop x's sign and |y|
                LDX     #RDL
                LDA     #4
                JSR     SPOP
                LDBX    $00, $00, $00, $81
                JMP     FDIVS          ; 1/x^|y|

FP30A:          PUSHB                  ; x
                LDA     RBL            ; its sign
                PHA
                JSR     PUSHF          ; y
                JSR     INT
                JSR     POPR           ; y
                JSR     FCOMP          ; an integer?
                BNE     FP34
                PUSHB                  ; y
                LDBX    $00, $00, $00, $90 ; under 32768?
                JSR     FCOMP
                PHP
                JSR     POPR           ; y
                PLP
                BPL     FP34
                PLA
                JSR     POPR           ; x
                JMP     XTON
FP34:           PLA                    ; x negative?
                CLC
                BPL     FP40
                PUSHB                  ; y
                JSR     QINT           ; even or odd
                LDA     RDL            ; (the 8086's $NORMS here only
                PHA                    ; rebuilds the FAC, which x replaces)
                JSR     POPR           ; y
                PLA
                LSR     A              ; C: odd
FP40:           PHP
                POPF                   ; x
                LDA     FACM1
                AND     #$7F
                STA     FACM1
                PLP
                BCC     FP60
                PUSHRET NEG            ; negated at the end if y is odd
FP60:           PUSHB                  ; y
                JSR     LOG
                JSR     POPR
                JSR     FMULS          ; y*LOG(x)
                JMP     EXP

; $CLROV (GWMAIN.ASM 3544): the overflow mode back to normal. A and the flags
; kept.
CLROV:          STZ     FLGOV
                RTS

; XTON (MATH1.ASM 2373): FAC = BXDX ^ FAC, FAC an integer 0 < n < 32768.
; Knuth's algorithm: y = 1, z = x; for each bit of n from the bottom, y = y*z
; if it is 1, then z = z*z. z is on the BASIC stack, n in FINDI.
XTON:           PUSHB                  ; z
                JSR     QINT
                LDA     RDL
                STA     FINDI
                LDA     RDH
                STA     FINDI+1
                STZ     FACLO          ; y = 1.0
                STZ     FACLO+1
                STZ     FACM1
                LDA     #$81
                STA     FACEXP
XTON10:         LSR     FINDI+1
                ROR     FINDI
                BCC     XTON20
                JSR     POPR           ; z
                PUSHB
                JSR     FMULS          ; y = y*z
XTON20:         LDA     FINDI
                ORA     FINDI+1
                BEQ     XTON30
                JSR     POPR           ; z
                JSR     PUSHF          ; y
                JSR     MOVFR
                JSR     FMULS          ; z*z
                JSR     POPR           ; y
                JSR     PUSHF          ; the new z
                JSR     MOVFR
                BRA     XTON10
XTON30:         JMP     POPR           ; z off the stack

; --- $EXP (MATH1.ASM 3028) ---------------------------------------------------------

; e^x = 2^y, y = x*LOG2(e): 2^INT(y) is an exponent, 2^(y-INT(y)) Hart 1302.
; |y| >= 128 is Overflow (or 0 for a negative y); |y| < 2^-24, 1.
EXP:            LDBX    $3B, $AA, $38, $81 ; LOG2(e)
                JSR     FMULS
                LDA     FACEXP
                CMP     #$88
                BCS     EXP110
                CMP     #$68
                BCC     EXP200
                JSR     PUSHF          ; y
                JSR     QINT           ; INT(y)
                LDA     RDL            ; its exponent as a power of 2
                CLC
                ADC     #$81
                BEQ     EXP100
                PHA
                BIT     FACSGN         ; (QINT keeps the sign there)
                BPL     :+
                SEC                    ; NGBLDX: its magnitude
                LDA     #0
                SBC     RDL
                STA     RDL
                LDA     #0
                SBC     RDH
                STA     RDH
                LDA     #0
                SBC     RBL
                STA     RBL
:               STZ     RAH
                JSR     NORMS          ; the FAC = INT(y)
                JSR     POPR           ; y
                JSR     FSUBS          ; y - INT(y)
                LDA     #<EXPCN
                LDX     #>EXPCN
                JSR     POLY
                PLA                    ; times 2^INT(y)
                STA     RBH
                STZ     RBL
                STZ     RDL
                STZ     RDH
                JMP     FMULS
EXP100:         LDX     #RDL           ; y off the stack
                LDA     #4
                JSR     SPOP
EXP110:         LDA     FACM1          ; positive: too big
                AND     #$80
                STA     FACM1
                BEQ     EXP115
                JMP     ZERO           ; negative: too small
EXP115:         STZ     FACSGN         ; +infinity
                JMP     OVFLS
EXP200:         STZ     FACLO          ; 1.0
                STZ     FACLO+1
                STZ     FACM1
                LDA     #$81
                STA     FACEXP
                RTS

; --- $LOG (MATH2.ASM 1009) ---------------------------------------------------------

; FC error unless positive; LOG(1) = 0. The mantissa as x in .5..1:
; P(x)/Q(x) is its log to base 2, plus the exponent, times LN(2).
LOG:            JSR     SIGNS
                BEQ     LG100
                BPL     LOG05
LG100:          JMP     FCERR
LOG05:          LDBX    $00, $00, $00, $81 ; 1.0?
                JSR     FCOMP
                BNE     LOG10
                STZ     FACLO
                STZ     FACLO+1
                STZ     FACM1
                STZ     FACEXP
                RTS
LOG10:          LDA     FACEXP         ; the exponent, unbiased
                SEC
                SBC     #$80
                PHA
                LDA     #$80
                STA     FACEXP
                JSR     PUSHF          ; x
                LDA     #<LOGP
                LDX     #>LOGP
                JSR     POLY
                JSR     POPR           ; x
                JSR     PUSHF          ; P(x)
                JSR     MOVFR
                LDA     #<LOGQ
                LDX     #>LOGQ
                JSR     POLY
                JSR     POPR           ; P(x)
                JSR     FDIVS          ; P(x)/Q(x)
                PLA                    ; DX = the exponent (CBW)
                STA     RDL
                LDX     #0
                CMP     #$80
                BCC     :+
                DEX
:               STX     RDH
                JSR     PUSHF
                JSR     FLT
                JSR     POPR
                JSR     FADDS
MLLN2:          LDBX    $18, $72, $31, $80 ; LN(2)
                JMP     FMULS

; --- $SIN, $COS, $TAN (MATH1.ASM 951) ----------------------------------------------

; The angle divided by 2*PI in double precision (RR), its fraction times 4:
; the integer part is the quadrant, the fraction the angle in it, from the
; other end in quadrants 1 and 3, negated in 2 and 3. Then single precision:
; Hart 3341, or 2*PI times a tiny angle. SIN(-x) = -SIN(x); a tiny x is its
; own sine. COS(x) = SIN(x + PI/2).
COS:            LDA     FACM1          ; COS(-x) = COS(x)
                AND     #$7F
                STA     FACM1
                JSR     RR
                JSR     ONEARG
                LDA     #$7F           ; .25: a quarter turn
                STA     ARGEXP
                JSR     FADDD
                JSR     RR1
                BRA     SIN30
SIN:            LDA     FACEXP
                CMP     #$77
                BCS     SIN10
                RTS
SIN10:          LDA     FACM1
                BPL     SIN20
                AND     #$7F
                STA     FACM1
                PUSHRET NEG
SIN20:          JSR     RR
SIN30:          LDA     FACEXP         ; times 4
                BEQ     SIN31
                CLC
                ADC     #2
                STA     FACEXP
SIN31:          JSR     RR3            ; the ARG = INT(FAC): the quadrant
                LDA     ARGEXP
                CMP     #$82           ; 2 or 3: negate at the end
                PHP
                AND     #1             ; 1, or 3: from the other end
                BNE     SIN40
                LDA     ARGM1
                AND     #$40
SIN40:          PHP
                JSR     RR2            ; the fraction
                PLP
                BEQ     SIN50
                LDA     #<DP00         ; 1 - it
                STA     PTR
                LDA     #>DP00
                STA     PTR+1
                JSR     MOVAC
                JSR     FSUBD
SIN50:          LDA     FACEXP         ; divided by 4
                SEC
                SBC     #2
                STA     FACEXP
                BCS     SIN51
                JSR     DZERO          ; underflow
SIN51:          JSR     CSD
                LDA     FACEXP
                CMP     #$74           ; tiny: times 2*PI
                BCS     SIN60
                LDBX    $DB, $0F, $49, $83
                JSR     FMULS
                BRA     SIN65
SIN60:          LDA     #<SINCN
                LDX     #>SINCN
                JSR     POLYX
SIN65:          PLP
                BNE     SIN70
                LDA     FACM1
                EOR     #$80
                STA     FACM1
SIN70:          RTS

; RR: the single in the FAC to double, divided by 2*PI, and its fraction.
; RR1: the fraction of the FAC. RR2: the FAC less the ARG (its INT),
; negated: X - INT(X). RR3: the ARG = INT(FAC), the FAC kept.
RR:             LDA     #<IN2PI
                STA     PTR
                LDA     #>IN2PI
                STA     PTR+1
                JSR     MOVAC
                JSR     CDS
                JSR     FMULD
RR1:            JSR     RR3
RR2:            JSR     FSUBD
                JMP     NEG
RR3:            JSR     VPSHF
                JSR     DINT
                JSR     VMOVAF
                JMP     VPOPF
ONEARG:         LDA     #<DP00
                STA     PTR
                LDA     #>DP00
                STA     PTR+1
                JMP     MOVAC

; TAN = SIN/COS.
TAN:            JSR     PUSHF          ; x
                JSR     SIN
                JSR     POPR           ; x
                JSR     PUSHF          ; SIN(x)
                JSR     MOVFR
                JSR     COS
                JSR     POPR           ; SIN(x)
                JMP     FDIVS

; --- $ATAN (MATH1.ASM 1093) --------------------------------------------------------

; ATN(-x) = -ATN(x); past 1, PI/2 - ATN(1/x); past TAN(PI/12),
; PI/6 + ATN((x*SQR(3)-1)/(x+SQR(3))); then Hart 4940. The corrections are
; return addresses, pushed as the 8086 pushes them.
ATN:            LDA     FACM1
                BPL     ATN10
                AND     #$7F
                STA     FACM1
                PUSHRET NEG
ATN10:          LDA     FACEXP
                CMP     #$81
                BCC     ATN20
                PUSHRET ATN100
                LDBX    $00, $00, $00, $81 ; 1/x
                JSR     FDIVS
ATN20:          LDBX    $A2, $30, $09, $7F ; TAN(PI/12)
                JSR     FCOMP
                BMI     ATN30
                PUSHRET ATN200
                JSR     PUSHF          ; x
                LDBX    $D7, $B3, $5D, $81 ; SQR(3)
                JSR     FADDS          ; x+SQR(3)
                JSR     POPR           ; x
                JSR     PUSHF          ; x+SQR(3)
                JSR     MOVFR
                LDA     #<ATNC1
                LDX     #>ATNC1
                JSR     POLY           ; x*SQR(3)-1
                JSR     POPR           ; x+SQR(3)
                JSR     PUSHF          ; x*SQR(3)-1
                JSR     MOVFR
                JSR     POPR
                JSR     FDIVS
ATN30:          LDA     #<ATNC2
                LDX     #>ATNC2
                JMP     POLYX
ATN100:         LDBX    $DB, $0F, $49, $81 ; PI/2 - it
                JMP     FSUBS
ATN200:         LDBX    $92, $0A, $06, $80 ; PI/6 + it
                JMP     FADDS

; --- $POLY, $POLYX (MATH2.ASM 1167) ------------------------------------------------

; $POLY: the polynomial at A (low), X (high) in x, the FAC: the coefficient
; count, then each, the highest first. $POLYX: x * P(x^2).
POLYX:          PHA
                PHX
                JSR     PUSHF          ; x
                JSR     MOVRF
                JSR     FMULS          ; x^2
                PLX
                PLA
                JSR     POLY
                JSR     POPR           ; x
                JMP     FMULS
POLY:           STA     PCP
                STX     PCP+1
                LDA     (PCP)          ; the count
                STA     PCN
                INCW    PCP
                LDX     #3             ; x
@x:             LDA     FACLO,X
                STA     PX,X
                DEX
                BPL     @x
                JSR     PCOEF          ; the first coefficient to the FAC
                JSR     MOVFR
POL10:          DEC     PCN
                BEQ     POL30
                LDX     #3             ; times x
@m:             LDA     PX,X
                STA     RDL,X
                DEX
                BPL     @m
                JSR     FMULS
                JSR     PCOEF          ; plus the next
                JSR     FADDS
                BRA     POL10
POL30:          RTS
PCOEF:          LDY     #0             ; BXDX = the coefficient at PCP
                LDX     #0
@c:             LDA     (PCP),Y
                STA     RDL,X
                INY
                INX
                CPX     #4
                BNE     @c
                CLC
                LDA     PCP
                ADC     #4
                STA     PCP
                BCC     :+
                INC     PCP+1
:               RTS

; --- $RND (MATH2.ASM 1675) ---------------------------------------------------------

; RND(0) repeats the last; a negative argument reseeds from its own bytes;
; then RNDX = (RNDX * 214013 + 2531011) MOD 2^24, the result RNDX / 2^24. The
; 8086 multiplies by MUL in three partial products; here a shift and add,
; the same 24 bits.
RND:            JSR     SIGNS
                BNE     :+
                JMP     OLDRN
:               BPL     RND10
                LDA     FACLO          ; reseed
                STA     RNDX
                LDA     FACLO+1
                STA     RNDX+1
                LDA     FACM1
                STA     RNDX+2
RND10:          STZ     T0             ; T2:T1:T0 the product
                STZ     T1
                STZ     T2
                LDA     #<214013       ; T5:T4:T3 = A, shifted left
                STA     T3
                LDA     #>214013
                STA     T4
                LDA     #^214013
                STA     T5
                LDA     RNDX           ; RAL RAH RCL: RNDX, shifted right
                STA     RAL
                LDA     RNDX+1
                STA     RAH
                LDA     RNDX+2
                STA     RCL
                LDX     #24
@b:             LSR     RCL
                ROR     RAH
                ROR     RAL
                BCC     @s
                CLC
                LDA     T0
                ADC     T3
                STA     T0
                LDA     T1
                ADC     T4
                STA     T1
                LDA     T2
                ADC     T5
                STA     T2
@s:             ASL     T3
                ROL     T4
                ROL     T5
                DEX
                BNE     @b
                CLC                    ; plus C
                LDA     T0
                ADC     #<2531011
                STA     RNDX
                STA     RDL
                LDA     T1
                ADC     #>2531011
                STA     RNDX+1
                STA     RDH
                LDA     T2
                ADC     #^2531011
                STA     RNDX+2
                STA     RBL
                STZ     RAH
                STZ     FACSGN         ; positive, under 1
                LDA     #$80
                STA     FACEXP
                LDA     #VT_SNG
                STA     VALTYP
                JMP     NORMS
OLDRN:          LDA     RNDX           ; the last one again
                STA     RDL
                LDA     RNDX+1
                STA     RDH
                LDA     RNDX+2
                STA     RBL
                STZ     RAH
                LDA     #$80
                STA     FACEXP
                STZ     FACSGN
                JMP     NORMS

.rodata

; The constants (MATH1.ASM's CON86, octal there): each polynomial its count,
; then its coefficients, the highest first.
LOGP:           .byte   4                       ; Hart 2524, P
                .byte   $9A, $F7, $19, $83      ; 4.8114746
                .byte   $24, $63, $43, $83      ; 6.105852
                .byte   $75, $CD, $8D, $84      ; -8.86266
                .byte   $A9, $7F, $83, $82      ; -2.054667
LOGQ:           .byte   4                       ; and Q
                .byte   $00, $00, $00, $81      ; 1.
                .byte   $E2, $B0, $4D, $83      ; 6.427842
                .byte   $0A, $72, $11, $83      ; 4.545171
                .byte   $F4, $04, $35, $7F      ; .3535534
EXPCN:          .byte   7                       ; Hart 1302
                .byte   $7C, $88, $59, $74      ; .00020745577403-
                .byte   $E0, $97, $26, $77      ; .00127100574569-
                .byte   $C4, $1D, $1E, $7A      ; .00965065093202+
                .byte   $5E, $50, $63, $7C      ; .05549656508324+
                .byte   $1A, $FE, $75, $7E      ; .24022713817633-
                .byte   $18, $72, $31, $80      ; .69314717213716+
                .byte   $00, $00, $00, $81      ; 1.0
SINCN:          .byte   5                       ; Hart 3341
                .byte   $FB, $D7, $1E, $86      ; 39.71091766+
                .byte   $65, $26, $99, $87      ; -76.57498378-
                .byte   $58, $34, $23, $87      ; 81.60223119+
                .byte   $E1, $5D, $A5, $86      ; -41.34167747+
                .byte   $DB, $0F, $49, $83      ; 6.283185272+
ATNC1:          .byte   2                       ; the reduction by PI/6
                .byte   $D7, $B3, $5D, $81      ; SQR(3)
                .byte   $00, $00, $80, $81      ; -1.0
ATNC2:          .byte   4                       ; Hart 4940
                .byte   $62, $35, $83, $7E      ; -.1281333
                .byte   $50, $24, $4C, $7E      ; .1993573
                .byte   $79, $A9, $AA, $7F      ; -.3333324
                .byte   $00, $00, $00, $81      ; 1.0
; $IN2PI: 1/(2*PI), a double.
IN2PI:          .byte   $0B, $44, $4E, $6E, $83, $F9, $22, $7E

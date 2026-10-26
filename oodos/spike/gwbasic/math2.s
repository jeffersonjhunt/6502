; Derived from Microsoft GW-BASIC (1983), MIT licence, see LICENSE.
;
; MATH2.ASM, translated: the parts the spike measures (README.md). In the
; 8086's order:
;
;   $FS $CSD $FD $CDS $CSI $FI $CINC   forcing a value's type (FINFC)
;   $FLT                               integer to single
;   $FMULD                             double multiply
;   $FMULS                             single multiply
;   $FOTAN $FOTED                      the decimal point and commas
;   $FOTCV                             digits of a single (not a double's)
;   $FOTNV                             bracketing a single (not a double)
;   $FOUT                              free-format output
;   $INT $QINT                         INT, and the integer part for $FOTCV
;   $NORMS                             single normalisation
;   $ROUND $ROUNX                      double rounding
;   $ROUNS $ROUNM PAKSP                single rounding and packing
;   $SIGD                              trailing zeros, for $FOUT
;
; Left out: $FIDIG and $FINEX (input), $LOG, $NORMD, $POLY, the PRINT USING
; code ($PUF, $PUFOT, $PUI), $RND, $DINT and the double paths of $FOTCV and
; $FOTNV.
;
; The 8086 multiplies with MUL and divides with DIV; the 6502 shifts and
; adds. Each routine gives the 8086's result bit for bit: where the 8086
; keeps only part of a product, so does this, and the reference model
; (mbf.py) checks it.

.include "gwbasic.inc"

.export FRCSNG, CSD, FRCDBL, CDS, CSI, FRCINT, CINC, VALINT, FLT
.export FMULD, FMULS, FOTAN, FOTED, FOTCV, FOTNV, FOUT, FOUT2
.export INT, QINT, NORMS, ROUND, ROUNX, ROUNS, ROUNM, PAKSP, SIGD
.import GETYP, TMERR, OVERR, ZERO, DZERO, SIGNS, AEXPS, OVFLS, FCOMP, FADDH
.import FADDS, MDPTEN, MUL10, DIV10, COMPM, VNEG, FOFMT, FOTCI, FCI4, FOTZS
.import FOTB, DP06, FOSTB, FOITB, VADDH, VINT, VMOVM, VMVFM, VMVMF, VCMPM
.import VPSHF, VPOPF, FBUFF

.segment "MATH2"

; --- FINFC: forcing types -----------------------------------------------------

; $FS: force the FAC to single precision.
FRCSNG:         JSR     GETYP
                BCS     CSD            ; double: round it
                BEQ     @tm            ; a string can't be
                BMI     CSI            ; integer: float it
                RTS                    ; already single
@tm:            JMP     TMERR

; $CSD: double to single. The byte below the single's mantissa rounds it,
; with $40 set so an exact half rounds up rather than to even.
CSD:            LDA     #VT_SNG
                STA     VALTYP
                LDA     FACM1
                STA     FACSGN
                ORA     #$80           ; the understood 1
                STA     RBL
                LDA     FACLO
                STA     RDL
                LDA     FACLO+1
                STA     RDH
                LDA     DFACL+3
                ORA     #$40
                STA     RAH
                JMP     ROUNM

; $FD: force the FAC to double precision.
FRCDBL:         JSR     GETYP
                BCS     FC200          ; already double
                BEQ     FDTM
                BPL     CDS            ; single
                JSR     CSI            ; integer: float it first
CDS:            LDA     #VT_DBL
                STA     VALTYP
                STZ     DFACL          ; zero the low half
                STZ     DFACL+1
                STZ     DFACL+2
                STZ     DFACL+3
FC200:          RTS
FDTM:           JMP     TMERR

; $CSI: integer to single, keeping DX.
CSI:            LDA     RDL
                PHA
                LDA     RDH
                PHA
                LDA     FACLO
                STA     RDL
                LDA     FACLO+1
                STA     RDH
                JSR     FLT
                PLA
                STA     RDH
                PLA
                STA     RDL
                RTS

; $FI: force the FAC to integer, also returned in BX.
FRCINT:         JSR     GETYP
                BMI     @int
                BEQ     @tm
                BRA     CINC
@int:           LDA     FACLO
                STA     RBL
                LDA     FACLO+1
                STA     RBH
                RTS
@tm:            JMP     TMERR

; $CINC: a single (or the top of a double) to the nearest integer, halves
; away from zero; Overflow unless it fits in 16 bits.
CINC:           STZ     RBL            ; zero, if no integer part
                STZ     RBH
                LDA     FACEXP
                SEC
                SBC     #$80
                BCC     CXRET
                STA     RCH            ; the exponent, unbiased
                LDA     FACM1          ; BX = the mantissa's top 16 bits
                STA     RBH
                LDA     FACLO+1
                STA     RBL
                LDA     #16
                SEC
                SBC     RCH            ; bits to shift right
                BCC     OVRFLW         ; more than 16 bits
                BEQ     OVCHK          ; exactly 16: only -32768 fits
                TAX
                LDA     RBH
                STA     RAH            ; the sign
                ORA     #$80           ; the implied bit
                STA     RBH
@shift:         LSR     RBH
                ROR     RBL
                DEX
                BNE     @shift
                LDA     RBL            ; round, with the last bit shifted out
                ADC     #0
                STA     RBL
                LDA     RBH
                ADC     #0
                STA     RBH
                BVS     POSBOVER
                LDA     RAH
                BPL     CXRET
                SEC                    ; negative: NEG BX
                LDA     #0
                SBC     RBL
                STA     RBL
                LDA     #0
                SBC     RBH
                STA     RBH
CXRET:          LDA     RBL
                STA     FACLO
                LDA     RBH
                STA     FACLO+1
VALINT:         LDA     #VT_INT
                STA     VALTYP
                RTS
POSBOVER:       LDA     RAH            ; rounded up to $8000: -32768, or too big
                BMI     CXRET
                BRA     OVRFLW
OVCHK:          LDA     RBH            ; no shift: only $8000, and not rounding up
                CMP     #$80
                BNE     OVRFLW
                LDA     RBL
                BNE     OVRFLW
                LDA     FACLO
                BPL     CXRET
OVRFLW:         JMP     OVERR

; --- $FLT ---------------------------------------------------------------------

; $FLT: the signed integer in DX to a single in the FAC.
FLT:            STZ     RBL
                STZ     RBH
                STZ     RAH
                LDA     #$90           ; 2^16
                STA     FACEXP
                STZ     FACSGN
                LDA     RDH
                BPL     FLT10
                SEC                    ; NEG DX
                LDA     #0
                SBC     RDL
                STA     RDL
                LDA     #0
                SBC     RDH
                STA     RDH
                LDA     #$80
                STA     FACSGN
FLT10:          LDA     RDH            ; BL:DH:DL = DX:00
                STA     RBL
                LDA     RDL
                STA     RDH
                STZ     RDL
                LDA     #VT_SNG
                STA     VALTYP
                JMP     NORMS

; --- $FMULD: FAC = FAC * ARG, double ------------------------------------------

; Knuth's algorithm M on 16-bit digits in the 8086; here a shift-and-add
; over the 56 bits of the FAC's mantissa. Both give the exact 112-bit
; product, of which the top 64 bits (FACX+7 .. FACM1) are kept and the rest
; becomes a sticky bit.
FMULD:          LDA     FACEXP
                BEQ     FMD10
                LDA     ARGEXP
                BNE     FMD20
                JMP     DZERO
FMD10:          RTS
FMD20:          LDA     ARGM1
                STA     RBL
                LDA     ARGEXP
                STA     RBH
                JSR     AEXPS          ; the exponent and sign; implied bits on
                LDA     FACEXP
                PHA
                LDA     FACSGN
                PHA
                LDA     RBL            ; the ARG's mantissa, unpacked
                STA     ARGM1
                LDA     RBH
                STA     ARGEXP
                JSR     SETDB          ; FAC to DBUFF+1, and zero the product
                LDX     #6             ; the multiplier: DBUFF+1..+7, in FACX+1..+7
@copy:          LDA     DBUFF+1,X
                STA     FACX+1,X
                DEX
                BPL     @copy
                LDA     #56
                STA     T0
@bit:           LDA     FACX+1         ; add the ARG in if the multiplier's bit is set
                LSR     A
                BCC     @shift
                LDX     #0
                LDY     #7
                CLC
@add:           LDA     DFACL,X
                ADC     ARGLO,X
                STA     DFACL,X
                INX
                DEY
                BNE     @add
@shift:         LDX     #13            ; shift the product and multiplier right
@ror:           ROR     FACX+1,X
                DEX
                BPL     @ror
                DEC     T0
                BNE     @bit
                LDX     #6             ; M5: a sticky bit for the low 7 bytes
@low:           LDA     FACX,X
                BNE     @sticky
                DEX
                BPL     @low
                BRA     M5AB
@sticky:        LDA     FACX+7
                ORA     #$20
                STA     FACX+7
M5AB:           LDA     FACM1          ; normalised already?
                TAX
                PLA
                STA     FACSGN
                PLA
                STA     FACEXP
                TXA
                BMI     M6
                ASL     FACX+7         ; no: one bit left
                LDX     #0
                LDY     #7
@rol:           ROL     DFACL,X
                INX
                DEY
                BNE     @rol
                JMP     ROUND
M6:             INC     FACEXP
                BEQ     @ovf
                JMP     ROUND
@ovf:           LDA     #1             ; the 8086's BX points at $DBUFF here:
                STA     RBH            ; not zero, so a signed infinity
                JMP     OVFLS

; $SETDB (MATH1): the FAC to DBUFF+1, then zero the 16 product bytes and the
; overflow bytes. Kept beside its one caller.
SETDB:          LDX     #7
@move:          LDA     DFACL,X
                STA     DBUFF+1,X
                DEX
                BPL     @move
                LDX     #15
@zero:          STZ     FACX,X
                DEX
                BPL     @zero
                STZ     DBUFF
                STZ     ARGX
                RTS

; --- $FMULS: FAC = BXDX * FAC, single -----------------------------------------

; The 8086 forms the product in 16-bit partial products and drops the low 16
; bits of the lowest: that is floor(A * B / 2^16), which a 24-step
; shift-and-add gives exactly (its low 16 bits are never used). The 3 bits
; below the mantissa round it; nothing below them is kept.
FMULS:          JSR     SIGNS
                BEQ     FMS00
                LDA     RBH
                BNE     FMS05
FMS00:          JMP     ZERO
FMS05:          JSR     AEXPS
                STZ     T0             ; T2:T1:T0 the product's high half
                STZ     T1
                STZ     T2
                LDX     #24
@bit:           LDA     RDL
                LSR     A
                BCC     @shift
                CLC
                LDA     T0
                ADC     FACLO
                STA     T0
                LDA     T1
                ADC     FACLO+1
                STA     T1
                LDA     T2
                ADC     FACM1
                STA     T2
@shift:         ROR     T2
                ROR     T1
                ROR     T0
                ROR     RBL
                ROR     RDH
                ROR     RDL
                DEX
                BNE     @bit
                LDA     T2             ; BX:CX = T2 T1 T0 RBL
                BMI     @inc
                ASL     RBL            ; FMS35: left justify
                ROL     T0
                ROL     T1
                ROL     T2
                BRA     FMS37
@inc:           INC     FACEXP
                BNE     FMS37
                JMP     OVFLS
FMS37:          LDA     RBL            ; the overflow byte
                STA     RAH
                LDA     T0
                STA     RDL
                LDA     T1
                STA     RDH
                LDA     T2
                STA     RBL
                JMP     ROUNS

; --- $FOTAN, $FOTED: the decimal point and commas ------------------------------

; With CH the digits before the point, counted down, and CL to the next
; comma. FBPTR is the output position. Z is set on return if a point or a
; comma was put in.
FOTAN:          DEC     RCH
                BPL     FTD05
                LDA     FBPTR          ; leading zeros: ".000"
                STA     DPADR
                LDA     FBPTR+1
                STA     DPADR+1
                LDA     #'.'
                STA     (FBPTR)
FTN10:          INCW    FBPTR
                LDA     #'0'
                STA     (FBPTR)
                INC     RCH
                BNE     FTN10
                INCW    FBPTR
                BRA     FTD15
FOTED:          DEC     RCH
FTD05:          BNE     FTD10
                LDA     #'.'
                STA     (FBPTR)
                LDA     FBPTR
                STA     DPADR
                LDA     FBPTR+1
                STA     DPADR+1
                INCW    FBPTR
FTD15:          STZ     RCL            ; XOR CX,CX, which sets Z
                STZ     RCH
                LDA     #0
                BRA     FTD20
FTD10:          DEC     RCL
                BNE     FTD20
                LDA     #','
                STA     (FBPTR)
                INCW    FBPTR
                LDA     #3
                STA     RCL
                LDA     #0             ; Z as the 8086 leaves it
FTD20:          PHP
                LDA     RCL
                STA     FMTCX
                LDA     RCH
                STA     FMTCX+1
                PLP
                RTS

; --- $FOTCV: the digits of a single -------------------------------------------

; After $FOTNV the FAC holds the 7 digits as its integer part: round it to
; an integer (unless it is one), then 3 digits by 24-bit powers of ten and
; the last 4 by $FCI4's 16-bit ones.
FOTCV:          JSR     GETYP
                BCC     FCV40
                JMP     TMERR          ; the double path isn't in the spike
FCV40:          LDA     RCL            ; save CX
                PHA
                LDA     RCH
                PHA
                LDA     FACLO          ; and the FAC
                PHA
                LDA     FACLO+1
                PHA
                LDA     FACM1
                PHA
                LDA     FACEXP
                PHA
                JSR     INT
                PLA                    ; and get it back in BXDX
                STA     RBH
                PLA
                STA     RBL
                PLA
                STA     RDH
                PLA
                STA     RDL
                JSR     FCOMP          ; already an integer?
                BEQ     FCV45
                LDA     RDL            ; no: put it back and add .5
                STA     FACLO
                LDA     RDH
                STA     FACLO+1
                LDA     RBL
                STA     FACM1
                LDA     RBH
                STA     FACEXP
                JSR     FADDH
FCV45:          JSR     QINT           ; BL:DX = the integer
                PLA                    ; CX back
                STA     RCH
                PLA
                STA     RCL
                LDA     RDL
                STA     FACLO
                LDA     RDH
                STA     FACLO+1
                LDA     RBL
                STA     FACM1
                LDA     RBH
                STA     FACEXP
                LDA     #3             ; FCV50: 3 digits
                STA     T0
                LDA     #<FOSTB
                STA     RBP
                LDA     #>FOSTB
                STA     RBP+1
FCV60:          JSR     FOTED
                LDX     #'0'-1
FCV70:          INX                    ; $RSUBM until it borrows
                LDY     #0
                SEC
                LDA     FACLO
                SBC     (RBP),Y
                STA     FACLO
                INY
                LDA     FACLO+1
                SBC     (RBP),Y
                STA     FACLO+1
                INY
                LDA     FACM1
                SBC     (RBP),Y
                STA     FACM1
                BCS     FCV70
                LDY     #0             ; one too many: add it back
                CLC
                LDA     FACLO
                ADC     (RBP),Y
                STA     FACLO
                INY
                LDA     FACLO+1
                ADC     (RBP),Y
                STA     FACLO+1
                INY
                LDA     FACM1
                ADC     (RBP),Y
                STA     FACM1
                CLC                    ; the next power of ten
                LDA     RBP
                ADC     #3
                STA     RBP
                BCC     :+
                INC     RBP+1
:               TXA
                STA     (FBPTR)
                INCW    FBPTR
                DEC     T0
                BNE     FCV60
                CLC                    ; past 10000: $FCI4 does 1000 down
                LDA     RBP
                ADC     #2
                STA     RBP
                BCC     :+
                INC     RBP+1
:               LDA     #4
                STA     T0
                JMP     FCI4

; --- $FOTNV: bracket a single into 10^6..10^7 ---------------------------------

; Multiply the FAC by powers of ten until its 7 significant digits are its
; integer part. Returns the decimal exponent in A, with N and Z set by it;
; keeps CX and FBPTR.
FOTNV:          LDA     RCL
                PHA
                LDA     RCH
                PHA
                LDA     FBPTR
                PHA
                LDA     FBPTR+1
                PHA
                STZ     FOEXP
FNV10:          LDX     FACEXP         ; the bracket table: a power of ten
                LDA     FOTB,X
                BEQ     FNV20
                STA     RDL            ; DX = that power, signed
                LDX     #0
                ORA     #0
                BPL     :+
                DEX
:               STX     RDH
                SEC
                LDA     FOEXP          ; DI = DI - AX
                SBC     RDL
                STA     FOEXP
                JSR     MDPTEN
                BRA     FNV10
FNV20:          LDA     #<(DP06+4)     ; one more times 10 if below 10^6
                STA     PTR
                LDA     #>(DP06+4)
                STA     PTR+1
                JSR     COMPM
                BCC     FNV40
                JSR     MUL10
                DEC     FOEXP
FNV40:          PLA
                STA     FBPTR+1
                PLA
                STA     FBPTR
                PLA
                STA     RCH
                PLA
                STA     RCL
                LDA     FOEXP
                RTS

; --- $FOUT: free-format output ------------------------------------------------

; S: the sign and a "0" in case the number is zero. Returns Z if it is.
S:              LDA     #<(FBUFF+1)
                STA     FBPTR
                LDA     #>(FBUFF+1)
                STA     FBPTR+1
                LDA     #' '
                STA     (FBPTR)
                JSR     SIGNS
                PHP
                BPL     FO20
                LDA     #'-'
                STA     (FBPTR)
                JSR     VNEG           ; work on the magnitude
FO20:           INCW    FBPTR
                LDA     #'0'
                STA     (FBPTR)
                PLP
                RTS

; $FOUT: the FAC as text in FBUFF; returns FBPTR at its start.
FOUT:           JSR     S
                BNE     FOUT2
                INCW    FBPTR
                LDA     #0
                STA     (FBPTR)
                LDA     #<(FBUFF+1)
                STA     FBPTR
                LDA     #>(FBUFF+1)
                STA     FBPTR+1
                RTS
FOUT2:          JSR     GETYP
                BPL     FO50
                STZ     RCL            ; CX = 7 digits before the point, no commas
                LDA     #7
                STA     RCH
                STZ     FMTAL
                STZ     FMTAH
                LDA     RCL
                STA     FMTCX
                LDA     RCH
                STA     FMTCX+1
                JSR     FOTCI
                JMP     FOTZS
FO50:           JMP     FOFMT

; --- $INT: INT() of a single ------------------------------------------------

; Shift the mantissa right until the binary point is at its end, rounding
; towards minus infinity (a negative number is decremented first and
; incremented after), then normalise. $QINT leaves the integer in BL:DX,
; two's complement, instead.
INT:            LDA     FACEXP
                SEC
                SBC     #$98
                BCS     INT20          ; no fraction bits: already an integer
                EOR     #$FF           ; NEG CL
                INC     A
                STA     RCL
                LDA     #$80           ; not a QINT call
                STA     T7
QINTX:          LDA     FACLO
                STA     RDL
                LDA     FACLO+1
                STA     RDH
                LDA     FACM1
                STA     RBL
                LDA     FACEXP
                STA     RBH
                BNE     QINTNZ
                STZ     RBL            ; zero, whatever the mantissa holds
                STZ     RDL
                STZ     RDH
INT20:          RTS
QINTNZ:         LDA     RBL
                STA     FACSGN         ; the sign, for $NORMS
                ORA     #$80           ; the hidden 1
                STA     RBL
                LDA     #$98
                STA     FACEXP
                BIT     FACSGN
                BPL     INT10
                LDA     RDL            ; negative: subtract 1 first
                SEC
                SBC     #1
                STA     RDL
                LDA     RDH
                SBC     #0
                STA     RDH
                LDA     RBL
                SBC     #0
                STA     RBL
INT10:          LDX     RCL
                BEQ     INT12
INT11:          LSR     RBL
                ROR     RDH
                ROR     RDL
                DEX
                BNE     INT11
INT12:          BIT     FACSGN         ; and add it back
                BPL     INT15
                INC     RDL
                BNE     INT15
                INC     RDH
                BNE     INT15
                INC     RBL
INT15:          BIT     T7
                BPL     INT25
                STZ     RAH
                JMP     NORMS
INT25:          BIT     FACSGN         ; QINT: NGBLDX
                BPL     INT30
                SEC
                LDA     #0
                SBC     RDL
                STA     RDL
                LDA     #0
                SBC     RDH
                STA     RDH
                LDA     #0
                SBC     RBL
                STA     RBL
INT30:          RTS
QINT:           LDA     #$98
                SEC
                SBC     FACEXP
                STA     RCL
                STZ     T7             ; a QINT call
                JMP     QINTX

; --- $NORMS: normalise BL:DX:AH, then round ----------------------------------

NORMS:          LDA     FACEXP
                STA     RBH
                LDA     #4
                STA     RCL
NOR10:          LDA     RBL
                BMI     NOR20
                BNE     NOR15
                LDA     RBH            ; a zero byte: move a byte at a time
                SEC
                SBC     #8
                BCC     NOR17
                BEQ     NOR17
                STA     RBH
                LDA     RDH
                STA     RBL
                LDA     RDL
                STA     RDH
                LDA     RAH
                STA     RDL
                STZ     RAH
                DEC     RCL
                BNE     NOR10
                BRA     NOR17          ; all zero
NOR15:          ASL     RAH
                ROL     RDL
                ROL     RDH
                ROL     RBL
                DEC     RBH
                BNE     NOR10
NOR17:          JMP     ZERO
NOR20:          LDA     RBH
                STA     FACEXP
                ; falls into ROUNS

; --- $ROUNS: round BL:DX by AH and pack the FAC -------------------------------

ROUNS:          LDA     RAH
                AND     #$E0           ; the guard, round and sticky bits
                BRA     ROUNM1
ROUNM:          LDA     RAH
ROUNM1:         CLC
                ADC     #$80
                BCC     PAKSP
                TAX                    ; zero: exactly a half, round to even
                INC     RDL
                BNE     TSTEVN
                INC     RDH
                BNE     TSTEVN
                INC     RBL            ; a carry out of DX: even already
                BNE     PAKSP
                LDA     #$80           ; out of BL too: STC / RCR BL
                STA     RBL
                INC     FACEXP
                BNE     PAKSP
                JMP     OVFLS
TSTEVN:         TXA
                BNE     PAKSP
                LDA     RDL
                AND     #$FE
                STA     RDL
PAKSP:          LDA     RDL
                STA     FACLO
                LDA     RDH
                STA     FACLO+1
                LDA     RBL
                AND     #$7F
                BIT     FACSGN
                BPL     :+
                ORA     #$80
:               STA     FACM1
                RTS

; --- $ROUND: round a double and pack its sign ----------------------------------

ROUND:          CLC                    ; $80 to the overflow byte's word
                LDA     FACX+7
                ADC     #$80
                STA     FACX+7
                LDA     DFACL
                ADC     #0
                STA     DFACL
                BCC     RDD20
                LDX     #0             ; carry up through the mantissa's words
@up:            INC     DFACL+1,X
                BNE     RDD20
                INX
                CPX     #6
                BNE     @up
                INC     FACEXP         ; out of the top: 1.0 at the next exponent
                BEQ     RDD30
                LDA     #$80
                STA     FACM1
RDD20:          LDA     FACX+7         ; exactly a half: make it even
                BNE     ROUNX
                LDA     DFACL
                AND     #$FE
                STA     DFACL
ROUNX:          LDA     FACM1
                AND     #$7F
                BIT     FACSGN
                BPL     :+
                ORA     #$80
:               STA     FACM1
                RTS
RDD30:          LDA     #1             ; BX is an address in the 8086: signed
                STA     RBH
                JMP     OVFLS

; --- $SIGD: count the zero digits at the right of the FAC -------------------

; For each zero digit found at the right of the bracketed FAC, CL goes up by
; one. Keeps the FAC.
SIGD:           LDA     RCH
                PHA
                LDA     RCL
                PHA
                JSR     VPSHF
                PLA
                STA     RCL
                PHA
                JSR     GETYP          ; already an integer? then no .5
                BCC     SI31
                LDA     FACEXP
                SEC
                SBC     #$B8
                BPL     SI20
                BRA     SI32
SI31:           LDA     FACEXP
                SEC
                SBC     #$98
                BPL     SI20
SI32:           JSR     VADDH
                JSR     VINT
SI20:           LDX     #ZLO           ; Z = INT(FAC+.5)
                JSR     VMVMF
                PLA
                STA     RCL
SI30:           LDA     RCL
                PHA
                LDX     #ZLO           ; Z1 = Z
                LDY     #Z1LO
                JSR     VMOVM
                LDX     #ZLO           ; FAC = Z
                JSR     VMVFM
                JSR     DIV10
                JSR     VINT           ; FAC = INT(Z/10)
                LDX     #ZLO
                JSR     VMVMF          ; Z = INT(Z/10)
                JSR     MUL10          ; FAC = INT(Z/10)*10
                LDX     #Z1LO
                JSR     VCMPM          ; = Z1?
                TAX
                PLA
                STA     RCL
                TXA
                BNE     SI40
                INC     RCL
                BRA     SI30
SI40:           JSR     VPOPF          ; the FAC back
                PLA
                STA     RCH
                RTS

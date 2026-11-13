; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; MATH2.ASM, translated: the resident part (hw/oodos.md, "The plan, phase
; 7": single and integer arithmetic with their conversions). In the 8086's
; order:
;
;   $FS $CSD $FD $CDS $CSI $FI $CINC   forcing a value's type (FINFC)
;   $FLT                               integer to single
;   $FMULS                             single multiply
;   $INT $QINT                         INT, and the integer part
;   $NORMS                             single normalisation
;   $ROUNS $ROUNM PAKSP                single rounding and packing
;
; The rest is block P1's (p1/): $FIDIG $FINEX and the input forcing, $FMULD,
; number output ($FOTAN $FOTED $FOTCV $FOTNV $FOUT $SIGD), $DINT, $LOG,
; $NORMD, $POLY, $RND and $ROUND. Left out: the PRINT USING code ($PUF,
; $PUFOT, $PUI), 7b's.
;
; The 8086 multiplies with MUL and divides with DIV; the 6502 shifts and
; adds. Each routine gives the 8086's result bit for bit: where the 8086
; keeps only part of a product, so does this, and the reference model
; (mbf.py) checks it.

.include "gwbasic.inc"

.export FRCSNG, CSD, FRCDBL, CDS, CSI, FRCINT, CINC, VALINT, FLT
.export FMULS, INT, QINT, NORMS, ROUNS, ROUNM, PAKSP
.import GETYP, TMERR, OVERR, ZERO, SIGNS, AEXPS, OVFLS

.code

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

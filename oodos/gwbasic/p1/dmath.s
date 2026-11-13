; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; MATH1.ASM and MATH2.ASM's double precision, in block P1 (hw/oodos.md, "The
; plan, phase 7"):
;
;   MDPTEN MDP10 $MUL10 $DIV10   times a power of ten, 10 or .1
;   $MOVAC $MOVFC $MOVFA $XCGAF  constants to the ARG or the FAC; ARG to FAC
;   $FMULD $SETDB                double multiply (MATH2)
;   DDIV                         double divide
;   $FADDD $FSUBD $SHRA          double add and subtract
;   $NORMD $ROUND $ROUNX         normalise, round and pack (MATH2)
;   $DADDH $VADDH                add .5
;   $DCMPM $DCMPA                double compare
;   $DINT $FTDNT DINTA ADD1D     INT of a double (MATH2)
;
; The resident core reaches them through its far call stubs (res/mathfar.s:
; DADD DSUB DMULT DDIV DCOMP, VINT's and VCMPM's). Where the 8086 points BX
; at a double in memory, this names one in zero page by X.

.include "gwbasic.inc"

;@entry FADDD
;@entry FSUBD
;@entry FMULD
;@entry DDIVFA
;@entry DCMPA
;@entry DCMPM
;@entry DINT
;@entry DADDH
.export FADDD, FSUBD, FMULD, DDIVFA, DCMPA, DCMPM, DINT, DADDH
.export MDPTEN, MUL10, DIV10, MOVAC, MOVFC, MOVFA, ROUND, ROUNX, NORMD, FTDNT
.export VADDH, DP00, DP01, DP06, DP09, DPM01
.import GETYP, FRCDBL, CSD, OVFLS, DIV0S, DZERO, ZERO, AEXPS, FMULS, FADDH
.import ZCMPCK, ZEREXP, CPM05, CPM06, CPM10

.code

; --- MDPTEN: the FAC times 10^DX, DX signed ----------------------------------------

; In double precision, then back to the FAC's own type (a single, or an
; integer made single). MATH1.ASM 1181.
MDPTEN:         JSR     GETYP
                PHP
                BCS     FIN20
                JSR     FRCDBL
FIN20:          LDA     RDH
                BMI     FIN30
                LDA     FACEXP         ; positive: zero stays zero
                BEQ     FIN80
                LDA     RDH            ; and 10^39 up is Overflow
                BNE     @ovf
                LDA     RDL
                CMP     #39
                BCC     FIN40
@ovf:           PLP
                BCS     FIN25
                JSR     CSD
FIN25:          LDA     #1             ; the 8086's BX is the text pointer here:
                STA     RBH            ; not zero, so a signed infinity
                JMP     OVFLS
FIN30:          LDA     RDH            ; negative: one division, if >= -38
                CMP     #$FF
                BNE     @two
                LDA     RDL
                CMP     #<-38
                BCS     FIN40
@two:           CLC                    ; two, if >= -76
                LDA     RDL
                ADC     #38
                STA     RDL
                LDA     RDH
                ADC     #0
                STA     RDH
                CMP     #$FF
                BNE     FIN80
                LDA     RDL
                CMP     #<-38
                BCC     FIN80
                JSR     MDP10
                LDA     #<-38
                STA     RDL
                LDA     #>-38
                STA     RDH
FIN40:          JSR     MDP10
FIN45:          PLP
                BCS     FIN50
                JSR     CSD
FIN50:          RTS
FIN80:          JSR     ZERO           ; underflow
                BRA     FIN45

; MDP10: multiply by 10^DX, or divide by 10^-DX. MATH1.ASM 1234.
MDP10:          LDA     RDH
                PHP
                BPL     POSEXP
                SEC
                LDA     #0
                SBC     RDL
                STA     RDL
POSEXP:         STZ     PTR+1          ; PTR = $DP00 + DX * 8
                LDA     RDL
                ASL     A
                ROL     PTR+1
                ASL     A
                ROL     PTR+1
                ASL     A
                ROL     PTR+1
                CLC
                ADC     #<DP00
                STA     PTR
                LDA     PTR+1
                ADC     #>DP00
                STA     PTR+1
                JSR     MOVAC
                PLP
                BMI     DBLDIV
                JMP     FMULD
DBLDIV:         JMP     DDIVFA


; --- $DIV10, $MUL10, $MOVAC, $MOVFC (MATH1.ASM 1541-1660) ----------------------

; $DIV10, $MUL10: the FAC times .1 or 10, in its own precision.
DIV10:          JSR     GETYP
                LDA     #<DPM01
                STA     PTR
                LDA     #>DPM01
                STA     PTR+1
                BCC     ML10
                BRA     ML05
MUL10:          JSR     GETYP
                LDA     #<DP01
                STA     PTR
                LDA     #>DP01
                STA     PTR+1
                BCC     ML10
ML05:           JSR     MOVAC
                JMP     FMULD
ML10:                                  ; $MLSP: the constant, rounded to single
                LDA     FACLO
                PHA
                LDA     FACLO+1
                PHA
                LDA     FACM1
                PHA
                LDA     FACEXP
                PHA
                LDA     #VT_DBL
                STA     VALTYP
                JSR     MOVFC
                JSR     CSD
                PLA
                STA     RBH
                PLA
                STA     RBL
                PLA
                STA     RDH
                PLA
                STA     RDL
                JMP     FMULS

; $MOVAC, $MOVFC: a double constant at PTR to the ARG, or to the FAC.
MOVAC:          LDX     #0
                LDY     #0
@m:             LDA     (PTR),Y
                STA     ARGLO,X
                INX
                INY
                CPY     #8
                BNE     @m
                RTS
MOVFC:          LDX     #0
                LDY     #0
@m:             LDA     (PTR),Y
                STA     DFACL,X
                INX
                INY
                CPY     #8
                BNE     @m
                RTS

; $MOVFA: the ARG to the FAC (MATH1.ASM 1691). $XCGAF: the FAC and the ARG
; exchanged, all 8 bytes (MATH1.ASM 1619).
MOVFA:          LDX     #7
@m:             LDA     ARGLO,X
                STA     DFACL,X
                DEX
                BPL     @m
                RTS
XCGAF:          LDX     #7
@x:             LDA     DFACL,X
                LDY     ARGLO,X
                STA     ARGLO,X
                STY     DFACL,X
                DEX
                BPL     @x
                RTS

; --- $FMULD: FAC = FAC * ARG, double (MATH2.ASM 322) -------------------------

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

; --- DDIV: FAC = FAC / ARG, double (MATH1.ASM 3441) ----------------------------

; The 8086 divides by 16-bit digits (Knuth's algorithm D). This divides a
; bit at a time; both give the exact 64-bit quotient of the mantissas, with a
; sticky bit for a remainder, rounded to even. The remainder is in FACX, the
; divisor's copy in ZLO and the quotient in DBUFF.
DDIVFA:         LDA     FACM1
                EOR     ARGM1
                STA     FACSGN
                STA     T6
                LDA     #1             ; BX is the caller's pointer: never zero
                STA     RBH
                LDA     ARGEXP
                BNE     @1
                JMP     DIV0S          ; DDIV0
@1:             LDA     FACEXP
                BNE     @2
                JMP     DZERO          ; EXIT1
@2:             LDA     FACEXP         ; the exponent, unbiased, through V
                EOR     #$80
                STA     T7
                LDA     ARGEXP
                EOR     #$80
                STA     T5
                LDA     T7
                SEC
                SBC     T5
                BVC     @3
                BPL     @z             ; DOVCKJ
                JMP     OVFLS
@z:             JMP     DZERO
@3:             EOR     #$80
                STA     T7
                LDX     #6
@copy:          LDA     DFACL,X        ; the dividend's mantissa, implied bit on
                STA     FACX,X
                LDA     ARGLO,X        ; the divisor's
                STA     ZLO,X
                DEX
                BPL     @copy
                LDA     FACX+6
                ORA     #$80
                STA     FACX+6
                LDA     ZLO+6
                ORA     #$80
                STA     ZLO+6
                STZ     FACX+7
                LDA     #64
                STA     T4
@bit:           LDA     FACX+7         ; remainder >= divisor?
                BNE     @ge
                LDX     #6
@cmp:           LDA     FACX,X
                CMP     ZLO,X
                BCC     @lt
                BNE     @ge
                DEX
                BPL     @cmp
@ge:            LDX     #0
                LDY     #7
                SEC
@sub:           LDA     FACX,X
                SBC     ZLO,X
                STA     FACX,X
                INX
                DEY
                BNE     @sub
                STZ     FACX+7
                SEC
                BRA     @q
@lt:            CLC
@q:             LDX     #0
                LDY     #8
@rol:           ROL     DBUFF,X
                INX
                DEY
                BNE     @rol
                DEC     T4
                BEQ     @done
                ASL     FACX           ; remainder times 2
                LDX     #1
                LDY     #7
@dbl:           ROL     FACX,X
                INX
                DEY
                BNE     @dbl
                BRA     @bit
@done:          LDX     #7             ; a sticky bit if the remainder isn't 0
@rem:           LDA     FACX,X
                BNE     @sticky
                DEX
                BPL     @rem
                BRA     DNRMCHK
@sticky:        LDA     DBUFF
                ORA     #1
                STA     DBUFF
DNRMCHK:        LDA     DBUFF+7
                BMI     DINCEX
                ASL     DBUFF          ; normalise
                LDX     #1
                LDY     #7
@n:             ROL     DBUFF,X
                INX
                DEY
                BNE     @n
                LDA     T7
                BNE     DDRND
                JMP     DZERO
DINCEX:         INC     T7
                BNE     DDRND
                LDA     DBUFF+3        ; BX: the quotient's second word
                STA     RBH
                JMP     OVFLS
DDRND:          LDA     DBUFF          ; the extended bits
                CMP     #$80
                BCC     DDSV
                BNE     FPRNDUP
                LDA     DBUFF+1        ; exactly a half: round to even
                LSR     A
                BCC     DDSV
FPRNDUP:        LDX     #1
@up:            INC     DBUFF,X
                BNE     DDSV
                INX
                CPX     #8
                BNE     @up
                INC     T7             ; all ones became zero: the next exponent
                BNE     DDSV
                STZ     RBH            ; BX is zero now
                JMP     OVFLS
DDSV:           LDX     #6
@st:            LDA     DBUFF+1,X
                STA     DFACL,X
                DEX
                BPL     @st
                LDA     FACM1
                AND     #$7F
                BIT     T6
                BPL     :+
                ORA     #$80
:               STA     FACM1
                LDA     T7
                STA     FACEXP
                RTS

; --- $ROUND, $ROUNX: round a double and pack its sign (MATH2.ASM 1739) -------

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


; --- $FADDD, $FSUBD: FAC = ARG + FAC, or ARG - FAC (MATH1.ASM 3131) ------------

; The larger exponent's number goes in the FAC (the two exchanged, $XCGAF),
; the other, in the ARG, shifts right ($SHRA) until the points line up, its
; bits falling into the ARG's overflow byte. Then the 7 bytes and the
; overflow byte are added or subtracted. 57 bits apart or more, the larger
; is the answer. FACSGN keeps the answer's sign. The 8086 works in 16-bit
; words from the overflow byte up; this works in bytes, the same sum.
FADDX1:         JSR     MOVFA          ; the FAC is zero: the ARG is the answer
FADDX2:         RTS
FSUBD:          LDA     FACEXP
                BEQ     FADDX1
                LDA     FACM1
                EOR     #$80
                STA     FACM1
FADDD:          STZ     FACX+7         ; the overflow bytes
                STZ     ARGX
                LDA     ARGEXP
                BEQ     FADDX2
                LDA     FACEXP
                BEQ     FADDX1
                LDA     FACM1          ; AL, and FAC+1: the FAC's sign
                STA     RAL
                STA     FACSGN
                LDA     ARGM1          ; BL
                STA     RBL
                LDA     FACM1          ; the hidden 1s
                ORA     #$80
                STA     FACM1
                LDA     ARGM1
                ORA     #$80
                STA     ARGM1
                LDA     FACEXP         ; CL = the FAC's exponent less the ARG's
                SEC
                SBC     ARGEXP
                BEQ     FDD25
                BCS     FDD20
                EOR     #$FF           ; the ARG is larger: NEG CL
                INC     A
                PHA
                LDA     RAL            ; XCHG AL,BL; its sign is the answer's
                LDX     RBL
                STA     RBL
                STX     RAL
                STX     FACSGN
                JSR     XCGAF          ; the exponents too
                PLA
FDD20:          CMP     #57
                BCS     FDD95
                JSR     SHRA
FDD25:          LDA     RAL            ; the signs: add or subtract?
                EOR     RBL
                BMI     FDD50
                CLC
                LDX     #0
                LDY     #8
@add:           LDA     FACX+7,X
                ADC     ARGX,X
                STA     FACX+7,X
                INX
                DEY
                BNE     @add
                BCC     FDD40
FDD35:          INC     FACEXP         ; a carry out: one bit right, the carry in
                BEQ     FDD90
                LDX     #7
@ror:           ROR     FACX+7,X
                DEX
                BPL     @ror
FDD40:          JMP     ROUND
FDD50:          SEC                    ; subtract
                LDX     #0
                LDY     #8
@sub:           LDA     FACX+7,X
                SBC     ARGX,X
                STA     FACX+7,X
                INX
                DEY
                BNE     @sub
                BCS     FDD80
                LDA     FACSGN         ; the FAC was the smaller: negate the
                EOR     #$FF           ; 8 bytes and the sign. (The 8086's way
                STA     FACSGN         ; back to FDD35, a carry out of all of
                SEC                    ; them, can't happen: the difference
                LDX     #0             ; isn't zero.)
                LDY     #8
@neg:           LDA     #0
                SBC     FACX+7,X
                STA     FACX+7,X
                INX
                DEY
                BNE     @neg
FDD80:          JMP     NORMD
FDD90:          LDA     #1             ; BX is an address in the 8086: signed
                STA     RBH
                JMP     OVFLS
FDD95:          JMP     ROUNX

; $SHRA (MATH1.ASM 1583): the ARG, 7 bytes over its overflow byte, right by A
; bits. A whole byte at a time keeps only the overflow byte's ST bit ($20);
; a bit at a time, a bit that passes bit 4 of the overflow byte sets ST.
SHRA:           STA     T0
SHRA10:         LDA     T0
                CMP     #8
                BCC     SHRA30
                SBC     #8
                STA     T0
                LDA     ARGX           ; a byte right
                AND     #$20
                STA     T1
                LDX     #0
@mv:            LDA     ARGLO,X
                STA     ARGX,X
                INX
                CPX     #7
                BNE     @mv
                STZ     ARGM1
                LDA     ARGX
                ORA     T1
                STA     ARGX
                BRA     SHRA10
SHRA30:         LDA     T0
                BEQ     SHRA40
                LSR     ARGM1          ; a bit right
                LDX     #5
@ror:           ROR     ARGLO,X
                DEX
                BPL     @ror
                ROR     ARGX
                LDA     ARGX
                AND     #$10
                BEQ     :+
                LDA     ARGX           ; through ST: SHST
                ORA     #$20
                STA     ARGX
:               DEC     T0
                BRA     SHRA30
SHRA40:         RTS

; --- $NORMD: normalise a double, then round (MATH2.ASM 1064) -------------------

; Shift the 7 bytes and the overflow byte left until the top bit is set: a
; byte at a time while the top byte is zero, else a bit. At most 57 bits
; (DL): more, or the exponent run out, is zero.
NORMD:          LDA     #57
                STA     T0             ; DL
NORD30:         LDA     FACM1
                BMI     NORD40         ; normalised
                BEQ     NORD60
                ASL     FACX+7         ; a bit left
                LDX     #0
                LDY     #7
@rol:           ROL     DFACL,X
                INX
                DEY
                BNE     @rol
                DEC     FACEXP
                BEQ     NORD50
                DEC     T0
                BEQ     NORD50
                BRA     NORD30
NORD60:         LDA     FACEXP         ; the top byte is zero: a byte left
                SEC
                SBC     #8
                STA     FACEXP
                BCC     NORD50
                BEQ     NORD50
                LDA     T0
                SEC
                SBC     #8
                STA     T0
                BCC     NORD50
                BEQ     NORD50
                LDX     #6
@mv:            LDA     FACX+7,X
                STA     DFACL,X
                DEX
                BPL     @mv
                STZ     FACX+7
                BRA     NORD30
NORD40:         JMP     ROUND
NORD50:         JMP     DZERO

; --- $DADDH, $VADDH: add .5 (MATH1.ASM 1526) -------------------------------------

VADDH:          JSR     GETYP
                BCS     DADDH
                JMP     FADDH
DADDH:          LDA     #<DHALF
                STA     PTR
                LDA     #>DHALF
                STA     PTR+1
                JSR     MOVAC
                JMP     FADDD

; --- $DCMPM, $DCMPA: double compare (MATH1.ASM 1860) ----------------------------

; The FAC against the double at X (the 8086: BX at its sign byte), or the
; ARG. As $FCOMP: A = 1 if the FAC is greater, 0 if equal, $FF (and C set)
; if less; Z and N follow A. The signs the same, the 8 bytes compare from
; the exponent down, the other way round if negative.
DCMPA:          LDX     #ARGLO
DCMPM:          STX     T0
                LDA     7,X            ; ZCMPBX: BH its exponent, BL its sign
                STA     RBH
                LDA     6,X
                STA     RBL
                JSR     ZCMPCK
                BCC     :+
                JMP     ZEREXP
:               LDA     FACM1
                EOR     RBL
                BPL     DC10
                JMP     CPM05          ; the signs differ
DC10:           LDA     #7
                STA     T1
@c:             CLC
                ADC     T0
                TAX
                LDA     0,X            ; its byte
                LDX     T1
                CMP     DFACL,X        ; less the FAC's
                BNE     @ne
                DEC     T1
                LDA     T1
                BPL     @c
                LDA     #0             ; equal
                CLC
                RTS
@ne:            LDA     #0             ; C: it is the larger in magnitude
                ROR     A
                EOR     FACM1          ; and the smaller, if negative
                BMI     @lt
                JMP     CPM06
@lt:            JMP     CPM10

; --- $DINT, $FTDNT: INT of a double (MATH2.ASM 855) -----------------------------

; As $INT: the mantissa shifted right until the point is at its end, a
; negative number less one before and plus one after, then normalised.
; $FTDNT is $FOTCV's entry, with A the shift and C clear: it leaves the
; integer right-adjusted in the 7 bytes, not normalised. A zero exponent is
; left alone (the mantissa may be anything).
DINT:           LDA     FACEXP
                SEC
                SBC     #$B8
                BCS     DNT20          ; no fraction bits
                EOR     #$FF           ; NEG CL
                INC     A
                SEC                    ; normalise after
FTDNT:          PHP
                STA     T0
                LDA     FACEXP
                BNE     DINTNZ
                PLP
                RTS
DINTNZ:         LDA     FACM1          ; the sign, for $NORMD
                STA     FACSGN
                ORA     #$80           ; the hidden 1
                STA     FACM1
                LDA     #$B8           ; the exponent after the shift
                STA     FACEXP
                BIT     FACSGN
                BPL     DNT10
                JSR     DINTA
DNT10:          LDA     T0             ; $SHRD: the 7 bytes and the overflow
                CMP     #8             ; byte right by CL: whole bytes first
                BCC     DNT12          ; (what falls out isn't kept)
                SBC     #8
                STA     T0
                LDX     #0
@mv:            LDA     DFACL,X
                STA     FACX+7,X
                INX
                CPX     #7
                BNE     @mv
                STZ     FACM1
                BRA     DNT10
DNT12:          TAY
                BEQ     DNT14
@bit:           LSR     FACM1
                LDX     #5
@ror:           ROR     DFACL,X
                DEX
                BPL     @ror
                ROR     FACX+7
                DEY
                BNE     @bit
DNT14:          BIT     FACSGN
                BPL     DNT15
                JSR     ADD1D
DNT15:          STZ     FACX+7         ; zero the overflow byte
                PLP
                BCC     DNT20          ; $FOTCV's call: done
                JMP     NORMD
DNT20:          RTS

; DINTA: the mantissa less 1 (its top bit keeps it off the exponent).
; ADD1D: plus 1.
DINTA:          LDX     #0
@d:             LDA     DFACL,X
                DEC     DFACL,X
                CMP     #0
                BNE     @r
                INX
                BRA     @d
@r:             RTS
ADD1D:          LDX     #0
@i:             INC     DFACL,X
                BNE     @r
                INX
                BRA     @i
@r:             RTS

.rodata

; $DHALF: .5D00 (MATH1.ASM 612).
DHALF:          .byte   $00,$00,$00,$00,$00,$00,$00,$80

; Double precision powers of ten, 10^-1 and 10^0 to 10^38 (MATH1.ASM's
; CON86, octal there).
DPM01:          .byte   $CD,$CC,$CC,$CC,$CC,$CC,$4C,$7D
DP00:           .byte   $00,$00,$00,$00,$00,$00,$00,$81
DP01:           .byte   $00,$00,$00,$00,$00,$00,$20,$84
                .byte   $00,$00,$00,$00,$00,$00,$48,$87
                .byte   $00,$00,$00,$00,$00,$00,$7A,$8A
                .byte   $00,$00,$00,$00,$00,$40,$1C,$8E
                .byte   $00,$00,$00,$00,$00,$50,$43,$91
DP06:           .byte   $00,$00,$00,$00,$00,$24,$74,$94
                .byte   $00,$00,$00,$00,$80,$96,$18,$98
                .byte   $00,$00,$00,$00,$20,$BC,$3E,$9B
DP09:           .byte   $00,$00,$00,$00,$28,$6B,$6E,$9E
                .byte   $00,$00,$00,$00,$F9,$02,$15,$A2
                .byte   $00,$00,$00,$40,$B7,$43,$3A,$A5
                .byte   $00,$00,$00,$10,$A5,$D4,$68,$A8
                .byte   $00,$00,$00,$2A,$E7,$84,$11,$AC
                .byte   $00,$00,$80,$F4,$20,$E6,$35,$AF
                .byte   $00,$00,$A0,$31,$A9,$5F,$63,$B2
                .byte   $00,$00,$04,$BF,$C9,$1B,$0E,$B6
                .byte   $00,$00,$C5,$2E,$BC,$A2,$31,$B9
                .byte   $00,$40,$76,$3A,$6B,$0B,$5E,$BC
                .byte   $00,$E8,$89,$04,$23,$C7,$0A,$C0
                .byte   $00,$62,$AC,$C5,$EB,$78,$2D,$C3
                .byte   $80,$7A,$17,$B7,$26,$D7,$58,$C6
                .byte   $90,$AC,$6E,$32,$78,$86,$07,$CA
                .byte   $B4,$57,$0A,$3F,$16,$68,$29,$CD
                .byte   $A1,$ED,$CC,$CE,$1B,$C2,$53,$D0
                .byte   $85,$14,$40,$61,$51,$59,$04,$D4
                .byte   $A6,$19,$90,$B9,$A5,$6F,$25,$D7
                .byte   $0F,$20,$F4,$27,$8F,$CB,$4E,$DA
                .byte   $0A,$94,$F8,$78,$39,$3F,$01,$DE
                .byte   $0C,$B9,$36,$D7,$07,$8F,$21,$E1
                .byte   $4F,$67,$04,$CD,$C9,$F2,$49,$E4
                .byte   $23,$81,$45,$40,$7C,$6F,$7C,$E7
                .byte   $B6,$70,$2B,$A8,$AD,$C5,$1D,$EB
                .byte   $E3,$4C,$36,$12,$19,$37,$45,$EE
                .byte   $1C,$E0,$C3,$56,$DF,$84,$76,$F1
                .byte   $11,$6C,$3A,$96,$0B,$13,$1A,$F5
                .byte   $16,$07,$C9,$7B,$CE,$97,$40,$F8
                .byte   $DB,$48,$BB,$1A,$C2,$BD,$70,$FB
                .byte   $89,$0D,$B5,$50,$99,$76,$16,$FF

; Derived from Microsoft GW-BASIC (1983), MIT licence, see LICENSE.
;
; MATH1.ASM, translated: the parts the spike measures (README.md). In the
; 8086's order:
;
;   $FOTCI                      an integer's digits
;   $FOTZS                      leading zero suppression
;   CON86                       the constants $FOUT needs (math1d below)
;   $OVFLS $DIV0S DOINF TRAPER  Overflow and Division by zero
;   $FOFMT                      fixed or E format
;   UTILS                       zero, sign, type, compare, move, negate, .5,
;                               times and divided by 10
;   MDPTEN MDP10                times a power of ten (from $FINE)
;   F4                          the interpreter's entries: integer
;                               arithmetic, compare and conversion
;   $AEXPS $SEXPS               the exponents of a product
;   DDIV                        double divide (FAC = FAC / ARG)
;   $FADDS $FSUBS               single add and subtract
;   SDIV $FDIVS                 single divide
;
; Left out: $FOUTO and $FOUTH (OCT$, HEX$), the PRINT USING routines, SIN
; COS TAN ATN, $FINE's input half, $FIN, $FPWR, $SQR, $EXP, $FADDD (double
; add), the double compares, the variable search inner loop (LOPFND) and
; RND.
;
; Where the 8086 points BX or DI at a number, this passes a zero page
; address in X, or a memory address in PTR (gwbasic.inc).

.include "gwbasic.inc"

.export FOTCI, FCI4, FOTZS, OVFLS, DIV0S, FOFMT, STROUT, DZERO, ZERO, SIGNS
.export SIGNAL, GETYP, VADDH, FADDH, DIV10, MUL10, MOVAC, MOVFC, MOVFR, MOVRF
.export FCOMP, COMPM, VABS, VNEG, NEG, INEG, VMOVM, VMVMF, VMVFM, VCMPM
.export VPSHF, VPOPF, VINT, MDPTEN, FADD, FSUB, FMULT, FDIV, CONSIH, ISUB
.export IADD, IMULT, IDIV, PUSHF, CHKSTR, INEG2, VMOVFM, VMOVE, VSIGN, CONIA
.export MAKINT, VALSNG, SGN, FDIVT, LINPRT, LINOUT, FIXER, ICOMP, IMOD, AEXPS
.export DDIVFA, FADDS, FSUBS, FDIVS, POPR, DMULT, DDIV, VFAC, VMOVAF
.export FOTB, DPM01, DP00, DP01, DP06, FOSTB, FOITB, FINCH
.import FOTED, FOTAN, FOTCV, FOTNV, SIGD, FOUT2, FRCDBL, FRCINT, CSD, CSI
.import FMULD, FMULS, NORMS, ROUNS, INT, FLT, CHRGTR, SPUSH, SPOP
.import TMERR, OVERR, DV0ERR, OUTDO, CRFIN, STROUI, DADDH, DINT, DCMPM, FBUFF

.segment "MATH1"

; --- $FOTCI: the 5 digits of the integer in FACLO ----------------------------

; CX holds the point and comma counts for $FOTED, FBPTR the output position.
; $FCI4 is the entry for $FOTCV's last 4 digits, with RBP at the power of
; ten and T0 the count.
FOTCI:          LDA     #5
                STA     T0
                LDA     #<FOITB
                STA     RBP
                LDA     #>FOITB
                STA     RBP+1
FCI4:
FCI10:          JSR     FOTED
                LDA     (RBP)          ; DX = the power of ten
                STA     RDL
                LDY     #1
                LDA     (RBP),Y
                STA     RDH
                INCW    RBP
                INCW    RBP
                LDX     #'0'-1
FCI20:          INX                    ; subtract it until it borrows
                SEC
                LDA     FACLO
                SBC     RDL
                STA     FACLO
                LDA     FACLO+1
                SBC     RDH
                STA     FACLO+1
                BCS     FCI20
                CLC                    ; once too many
                LDA     FACLO
                ADC     RDL
                STA     FACLO
                LDA     FACLO+1
                ADC     RDH
                STA     FACLO+1
                TXA
                STA     (FBPTR)
                INCW    FBPTR
                DEC     T0
                BNE     FCI10
                JSR     FOTED
                LDA     #0
                STA     (FBPTR)
                RTS

; --- $FOTZS: leading zero suppression ------------------------------------------

; Over the digits $FOTCI left in FBUFF: leading zeros become spaces (or *),
; and the sign moves up to the first digit. The 8086 scans with CHRGTR and
; BX; this lends CHRGTR the buffer through TXTPTR and puts it back.
FOTZS:          LDA     TXTPTR
                PHA
                LDA     TXTPTR+1
                PHA
                LDA     #<(FBUFF+1)
                STA     TXTPTR
                LDA     #>(FBUFF+1)
                STA     TXTPTR+1
                LDA     (TXTPTR)       ; the sign
                STA     RCH
                LDA     #' '
                STA     RCL
                LDA     FMTAL          ; fill with "*"?
                AND     #$20
                BEQ     FZ10
                LDA     #'*'
                STA     RCL
                LDA     RCH
                CMP     #' '
                BNE     FZ10
                LDA     FMTAL
                AND     #$04
                BNE     FZ10
                LDA     RCL
                STA     RCH
FZ10:           LDA     RCL
                STA     (TXTPTR)
                JSR     CHRGTR
                BEQ     FZ50           ; the end of the number
                LDX     #9             ; one of $FINCH's characters?
FZ20:           CMP     FINCH,X
                BEQ     FZ30
                DEX
                BPL     FZ20
                BRA     FZ60
FZ30:           TXA
                ASL     A
                TAX
                JMP     (FZ45,X)
FZ50:           DECW    TXTPTR         ; a leading "0" after all
                LDA     #'0'
                STA     (TXTPTR)
FZ60:           LDA     FMTAL          ; a floating "$"?
                AND     #$10
                BEQ     FZ70
                DECW    TXTPTR
                LDA     #'$'
                STA     (TXTPTR)
FZ70:           LDA     FMTAL          ; a trailing sign?
                AND     #$04
                BNE     FZ90
                DECW    TXTPTR
                LDA     RCH
                STA     (TXTPTR)
                STZ     RCH
FZ90:           LDA     TXTPTR
                STA     FBPTR
                LDA     TXTPTR+1
                STA     FBPTR+1
                PLA
                STA     TXTPTR+1
                PLA
                STA     TXTPTR
                LDA     RCH
                RTS

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
DV015:          JSR     STROUT
                LDA     #13
                JSR     OUTDO
                LDA     #10
                JSR     OUTDO
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

; --- $FOFMT: fixed or E format ---------------------------------------------------

; If the number is as exact in fixed format as in scientific, use fixed.
; FBPTR is the output position.
FOFMT:          LDA     FBPTR
                PHA
                LDA     FBPTR+1
                PHA
                JSR     GETYP
                LDA     #8             ; single: 7 digits
                BCC     FFM10
                LDA     #17            ; double: 16
FFM10:          STA     RCH
                STA     RCL
                PHA
                JSR     FOTNV          ; the digits into the integer part
                STA     RAL            ; and the exponent that needs
                BEQ     FFM11
                BPL     FFM15
FFM11:          PLA
                STA     RCL
                STA     RCH
                JSR     GETYP
                BCC     FFM16
                LDA     RAL
                CLC
                ADC     #16
                BPL     FFM18
                BRA     FFM17
FFM15:          PLA
                STA     RCL
                STA     RCH
                BRA     FFM20
FFM16:          LDA     RAL
                CLC
                ADC     #7
                BPL     FFM18
FFM17:          LDA     RAL
                PHA
                JSR     SIGD           ; CL up by the zeros on the right
                PLA
                STA     RAL
                CLC
                ADC     RCL
                BEQ     FFM20          ; JLE: E format if too small
                BVS     @v
                BMI     FFM20
                BRA     @ok
@v:             BPL     FFM20
@ok:            LDA     RCH
                CLC
                ADC     RAL
                STA     RCH
                BRA     FFM19
FFM18:          LDA     RAL
                CLC
                ADC     RCH
                STA     RAL
                INC     RCH
                LDA     RCH
                CMP     RAL
                BCS     @f
                LDA     #3             ; "E" format
                STA     RCH
                BRA     FFM30
@f:             LDA     RAL
                STA     RCH
FFM19:          INC     RCH            ; CH = the digits + 2
                LDA     #2             ; fixed
                STA     RAL
                BRA     FFM30
FFM20:          LDA     RAL
                CLC
                ADC     RCH
                STA     RAL
                LDA     #3             ; "E" format
                STA     RCH
FFM30:          LDA     RAL            ; AL is 2 too big
                SEC
                SBC     #2
                STA     RAL
                PLA
                STA     FBPTR+1
                PLA
                STA     FBPTR
                LDA     RAL
                PHA                    ; the exponent, and its flags
                STZ     RCL            ; no commas
                JSR     FOTAN          ; the point and leading zeros
                PHP
                LDA     #'0'
                STA     (FBPTR)
                PLP
                BNE     FFM40
                INCW    FBPTR
FFM40:          JSR     FOTCV          ; the digits
FFM50:          DECW    FBPTR          ; drop trailing zeros
                LDA     (FBPTR)
                CMP     #'0'
                BEQ     FFM50
                CMP     #'.'           ; and the point, if it's last
                BEQ     FFM60
                INCW    FBPTR
FFM60:          PLA
                BEQ     FFM100         ; no exponent
                STA     RAL            ; $PUEXP
                JSR     GETYP
                LDA     #'E'
                BCC     FFM70
                LDA     #'D'
FFM70:          STA     (FBPTR)
                INCW    FBPTR
                LDA     #'+'
                STA     (FBPTR)
                LDA     RAL
                BPL     FFM80
                LDA     #'-'
                STA     (FBPTR)
                LDA     #0
                SEC
                SBC     RAL
FFM80:          LDX     #'0'-1         ; two digits
FFM90:          INX
                SEC
                SBC     #10
                BCS     FFM90
                ADC     #'0'+10
                PHA
                INCW    FBPTR
                TXA
                STA     (FBPTR)
                INCW    FBPTR
                PLA
                STA     (FBPTR)
                INCW    FBPTR
FFM100:         LDA     #0
                STA     (FBPTR)
                LDA     #<(FBUFF+1)
                STA     FBPTR
                LDA     #>(FBUFF+1)
                STA     FBPTR+1
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

; $VADDH, $FADDH: add .5.
VADDH:          JSR     GETYP
                BCC     FADDH
                JMP     DADDH
FADDH:          STZ     RDL            ; BXDX = .5
                STZ     RDH
                STZ     RBL
                LDA     #$80
                STA     RBH
                JMP     FADDS

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
                JMP     DCMPM
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
                JMP     DINT
VIN10:          JMP     INT
VIN00:          RTS

; --- MDPTEN: the FAC times 10^DX, DX signed ----------------------------------------

; In double precision, then back to the FAC's own type (a single, or an
; integer made single).
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
FIN25:          JMP     OVFLS
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

; MDP10: multiply by 10^DX, or divide by 10^-DX.
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

; --- F4: the interpreter's entries ------------------------------------------------

; The 8086 swaps BX and CX before each: the interpreter's left operand comes
; in CX:DX and the text pointer in BX. Here the operand is already in BXDX
; and the text pointer is TXTPTR, so these are the routines themselves.
FADD            = FADDS
FSUB            = FSUBS
FDIV            = FDIVS
FMULT           = FMULS
DMULT           = FMULD
DDIV            = DDIVFA

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
IAD10:          LDA     #<FADDS
                LDX     #>FADDS
; Float both and apply the single routine at X:A, as IAD10 and IM10 do.
FLOAT2:         STA     PTR2
                STX     PTR2+1
                LDA     RBL            ; the right one
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
                JMP     (PTR2)

; IMULT: BX = BX * DX, or the single product if it doesn't fit.
IMULT:          JSR     IMUL16
                BCS     IM10
                LDA     T0
                STA     RBL
                LDA     T1
                STA     RBH
                JMP     MAKINT
IM10:           LDA     #<FMULS
                LDX     #>FMULS
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

; LINPRT: print BX as a number (a line number, for TRON). LINOUT leaves it in
; FBUFF.
LINPRT:         JSR     LINOUT
                JMP     STROUI
LINOUT:         JSR     MAKINT
                LDA     #0
                STA     TEMP3          ; FOUINI
                LDA     #<(FBUFF+1)
                STA     FBPTR
                LDA     #>(FBUFF+1)
                STA     FBPTR+1
                LDA     #' '
                STA     (FBPTR)
                INCW    FBPTR
                LDA     #'0'
                STA     (FBPTR)
                JMP     FOUT2

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

; --- DDIV: FAC = FAC / ARG, double ----------------------------------------------

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

; The messages, and the constants (CON86) $FOUT needs.
OVMSG:          .byte   "Overflow", 0
DIV0M:          .byte   "Division by zero", 0
FZ45:           .word   FZ50, FZ50, FZ60, FZ60, FZ60, FZ60, FZ50, FZ60, FZ10, FZ10

.segment "MATH1D"

; $FOTB: for each exponent, the power of ten that brings the number near
; 10^6..10^7.
FOTB:           .byte   38,38,38,38,38,38,38,38
                .byte   38,38,38,38,38,38,38,38
                .byte   38,38,38,38,38,38,38,38
                .byte   38,37,37,37,36,36,36,35
                .byte   35,35,34,34,34,34,33,33
                .byte   33,32,32,32,31,31,31,31
                .byte   30,30,30,29,29,29,29,28
                .byte   28,28,27,27,27,26,26,26
                .byte   25,25,25,25,24,24,24,23
                .byte   23,23,23,22,22,22,22,21
                .byte   21,21,20,20,20,19,19,19
                .byte   19,18,18,18,17,17,17,16
                .byte   16,16,16,15,15,15,14,14
                .byte   14,13,13,13,13,12,12,12
                .byte   11,11,11,10,10,10,10,09
                .byte   09,09,08,08,08,07,07,07
                .byte   06,06,06,06,05,05,05,04
                .byte   04,04,03,03,03,03,02,02
                .byte   02,01,01,01,00,00,00,00
                .byte   <-01,<-01,<-01,<-02,<-02,<-02,<-03,<-03
                .byte   <-03,<-03,<-04,<-04,<-04,<-05,<-05,<-05
                .byte   <-06,<-06,<-06,<-06,<-07,<-07,<-07,<-08
                .byte   <-08,<-08,<-09,<-09,<-09,<-09,<-10,<-10
                .byte   <-10,<-11,<-11,<-11,<-12,<-12,<-12,<-12
                .byte   <-13,<-13,<-13,<-14,<-14,<-14,<-15,<-15
                .byte   <-15,<-15,<-16,<-16,<-16,<-17,<-17,<-17
                .byte   <-18,<-18,<-18,<-18,<-19,<-19,<-19,<-20
                .byte   <-20,<-20,<-21,<-21,<-21,<-21,<-22,<-22
                .byte   <-22,<-23,<-23,<-23,<-24,<-24,<-24,<-25
                .byte   <-25,<-25,<-25,<-26,<-26,<-26,<-27,<-27
                .byte   <-27,<-28,<-28,<-28,<-28,<-29,<-29,<-29
                .byte   <-30,<-30,<-30,<-31,<-31,<-31,<-31,<-32

; Double precision powers of ten, 10^-1 and 10^0 to 10^38 (octal, as the
; original).
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
                .byte   $00,$00,$00,$00,$28,$6B,$6E,$9E
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

; $FINCH: the characters $FOTZS stops at.
FINCH:          .byte   ".", "e", "E", "%", "#", "!", "D", "d", ",", "0"

; $FOSTB then $FOITB, which must follow it: powers of ten for $FOTCV's
; first 3 digits (24-bit) and $FOTCI's (16-bit).
FOSTB:          .byte   $40,$42,$0F    ; 1000000
                .byte   $A0,$86,$01    ; 100000
                .byte   $10,$27,$00    ; 10000
FOITB:          .word   10000, 1000, 100, 10, 1

; The infinities: a double, then a single, positive then negative.
INFPD:          .byte   $FF,$FF,$FF,$FF,$FF,$FF,$7F,$FF
INFMD:          .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF

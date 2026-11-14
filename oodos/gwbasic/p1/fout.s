; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Number output, from MATH1.ASM and MATH2.ASM, in block P1 (hw/oodos.md,
; "The plan, phase 7"):
;
;   $FOTCI $FCI4          an integer's digits (MATH1)
;   $FOUTO $FOUTH         OCT$ and HEX$'s digits (MATH1)
;   $FOTZS                leading zero suppression (MATH1)
;   $FOFMT                fixed or E format (MATH1)
;   $FOTAN $FOTED         the decimal point and commas (MATH2)
;   $FOTCV DSUBI DADDI    the digits of a single or a double (MATH2)
;   $FOTNV                bracketing the number (MATH2)
;   $FOUT $FOUT2 S        free-format output (MATH2)
;   $SIGD                 trailing zeros (MATH2)
;   LINPRT LINOUT         a line number (MATH1's F4)
;
; and PRINT USING's number (7b step d), for the field p3/biprtu.s's PRINUS
; scans:
;
;   PUFOUT $PUFOT         the sign, then an integer or a float (MATH2)
;   $PUI $PUIZ            an integer, and the zeros after the point (MATH2)
;   $PUF DPCOM            a single or a double in fixed format (MATH2)
;   $PUFE                 a single or a double in E format (MATH1)
;   $FOTZ $FOTZC          zeros, and zeros with the point and commas (MATH1)
;   $PUFXE                the fix-up: zeros suppressed, the number fitted
;                         to its field, or a "%" before it (MATH1)
;
; With the tables they need: $FOTB, $FODTB, $FOSTB, $FOITB, $FINCH, HIDBL,
; $NUMB, and dmath.s's powers of ten.

.include "gwbasic.inc"

;@entry FOUT
;@entry FOUT2
;@entry LINOUT
;@entry LINPRT
;@entry FOUTO
;@entry FOUTH
;@entry PUFOUT
.export FOUT, FOUT2, LINOUT, LINPRT, FOUTO, FOUTH, PUFOUT
.export FOTCI, FCI4, FOTZS, FOFMT, FOTAN, FOTED, FOTCV, FOTNV, SIGD, FINCH
.export FOTB, FOSTB, FOITB, FODTB
.import GETYP, SIGNS, VNEG, MAKINT, STROUI, CHRGTR, FBUFF, FCOMP, COMPM, QINT
.import INT, FADDH, VINT, VMOVM, VMVMF, VMVFM, VCMPM, VPSHF, VPOPF, VMOVAF
.import MDPTEN, MUL10, DIV10, MOVAC, MOVFA, FMULD, DINT, DCMPA, DCMPM, DADDH
.import FTDNT, VADDH, DP06, DP09, DP00, CSI

.code

; --- $FOTCI: the 5 digits of the integer in FACLO (MATH1.ASM 74) ------------

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

; --- $FOTZS: leading zero suppression (MATH1.ASM 257) --------------------------

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

; --- $FOUTO, $FOUTH: OCT$ and HEX$'s digits (MATH1.ASM 96) ----------------

; The integer in FACLO as 6 octal digits (the first from one bit) or 4 hex,
; at FBUFF; FBPTR returns at the first digit kept, leading zeros dropped
; but the last.
FOUTO:          LDA     #1             ; CL: the first digit's bits
                LDX     #3             ; CH: the others'
                LDY     #6             ; SI: the digits
                BRA     FTH10
FOUTH:          LDA     #4
                LDX     #4
                LDY     #4
FTH10:          STA     RCL
                STX     RCH
                STY     T0
                STY     T1             ; for the zero suppression
                LDA     #<FBUFF
                STA     FBPTR
                LDA     #>FBUFF
                STA     FBPTR+1
                LDA     FACLO          ; DX = the integer
                STA     RDL
                LDA     FACLO+1
                STA     RDH
FTH20:          LDA     #0             ; its top CL bits
                LDX     RCL
@sh:            ASL     RDL
                ROL     RDH
                ROL     A
                DEX
                BNE     @sh
                TAX
                LDA     NUMB,X
                STA     (FBPTR)
                INCW    FBPTR
                LDA     RCH
                STA     RCL
                DEC     T0
                BNE     FTH20
                LDA     #0
                STA     (FBPTR)
                LDA     #<FBUFF
                STA     FBPTR
                LDA     #>FBUFF
                STA     FBPTR+1
                LDX     T1             ; can't drop all the zeros
                DEX
FTH40:          LDA     (FBPTR)
                CMP     #'0'
                BNE     FTH50
                INCW    FBPTR
                DEX
                BNE     FTH40
FTH50:          RTS

; --- $FOFMT: fixed or E format (MATH1.ASM 1335) ----------------------------------

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
PUEXP:          STA     RAL            ; $PUEXP (PRINT USING's $PUFE's too)
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
                LDA     FBPTR          ; XCHG BX,CX: the end in CX ($PUFE's)
                STA     RCL
                LDA     FBPTR+1
                STA     RCH
                LDA     #<(FBUFF+1)
                STA     FBPTR
                LDA     #>(FBUFF+1)
                STA     FBPTR+1
                RTS

; --- $FOTAN, $FOTED: the decimal point and commas (MATH2.ASM 510) --------------

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

; --- $FOTCV: the digits of a single or a double (MATH2.ASM 556) --------------

; After $FOTNV the FAC holds the digits as its integer part: round it to an
; integer (unless it is one), then a double's first 9 digits by 7-byte powers
; of ten ($FODTB), and a single's (or the double's rest) 3 by 24-bit powers
; of ten and the last 4 by $FCI4's 16-bit ones.
FOTCV:          JSR     GETYP
                BCS     FCVD
                JMP     FCV40
FCVD:           LDA     RCL            ; a double: save CX and the buffer
                PHA
                LDA     RCH
                PHA
                LDA     FBPTR
                PHA
                LDA     FBPTR+1
                PHA
                JSR     VMOVAF         ; the number to the ARG
                JSR     DINT
                JSR     DCMPA          ; already an integer?
                PHP
                JSR     MOVFA          ; the number back
                PLP
                BEQ     FCV05
                JSR     DADDH          ; no: add .5
FCV05:          LDA     #$B8           ; CL = -(the exponent - $B8)
                SEC
                SBC     FACEXP
                CLC                    ; $FTDNT: right-adjusted, not normalised
                JSR     FTDNT
                PLA
                STA     FBPTR+1
                PLA
                STA     FBPTR
                PLA
                STA     RCH
                PLA
                STA     RCL
                LDA     #<FODTB
                STA     RBP
                LDA     #>FODTB
                STA     RBP+1
                LDA     #9             ; 9 digits by double powers of ten
                STA     T3
FCV10:          JSR     FOTED
                LDA     #'0'-1
                STA     T1
FCV20:          INC     T1             ; DSUBI until it borrows
                JSR     DSUBI
                BCS     FCV20
                JSR     DADDI          ; one too many: add it back
                CLC                    ; and on to the next power of ten
                LDA     RBP
                ADC     #7
                STA     RBP
                BCC     :+
                INC     RBP+1
:               LDA     T1
                STA     (FBPTR)
                INCW    FBPTR
                DEC     T3
                BNE     FCV10
                LDA     DFACL          ; the rest, under 10^7, as a single's
                STA     FACLO          ; 24 bits
                LDA     DFACL+1
                STA     FACLO+1
                LDA     DFACL+2
                STA     FACM1
                LDA     DFACL+3
                STA     FACEXP
                JMP     FCV50

; DSUBI, DADDI: the 7-byte integer at RBP from, or to, DFACL. DSUBI's C is
; clear if it borrowed.
DSUBI:          LDA     #7
                STA     T2
                LDX     #0
                LDY     #0
                SEC
@s:             LDA     DFACL,X
                SBC     (RBP),Y
                STA     DFACL,X
                INX
                INY
                DEC     T2
                BNE     @s
                RTS
DADDI:          LDA     #7
                STA     T2
                LDX     #0
                LDY     #0
                CLC
@a:             LDA     DFACL,X
                ADC     (RBP),Y
                STA     DFACL,X
                INX
                INY
                DEC     T2
                BNE     @a
                RTS

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
FCV50:          LDA     #3             ; 3 digits
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

; --- $FOTNV: bracket the number (MATH2.ASM 716) --------------------------------

; Multiply the FAC by powers of ten until its 7 significant digits are its
; integer part (into 10^6..10^7); a double then times 10^9, and once by .1
; if past HIDBL, for its 16. Returns the decimal exponent in A, with N and Z
; set by it; keeps CX and FBPTR.
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
                BCC     FNV30
                JSR     MUL10
                DEC     FOEXP
FNV30:          JSR     GETYP          ; a double: 9 more digits
                BCC     FNV40
                LDA     #<DP09
                STA     PTR
                LDA     #>DP09
                STA     PTR+1
                JSR     MOVAC
                JSR     FMULD
                LDA     FOEXP
                SEC
                SBC     #9
                STA     FOEXP
                LDX     #7             ; too big to add .5 to?
@h:             LDA     HIDBL,X
                STA     DBUFF,X
                DEX
                BPL     @h
                LDX     #DBUFF
                JSR     DCMPM
                BEQ     FNV40
                BCS     FNV40
                JSR     DIV10          ; yes: a tenth, and the exponent up
                INC     FOEXP
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

; --- $FOUT: free-format output (MATH2.ASM 789) --------------------------------

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

; --- $SIGD: count the zero digits at the right of the FAC (MATH2.ASM 1827) ---

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

; --- PUFOUT, $PUFOT: PRINT USING's number (MATH2.ASM 1551) ------------------------

; The number in the FAC as its field says: A the format's flags, RCH the
; places before the point and RCL those after it (the point included):
;
;   bit 7  PRINT USING (set by PRINUS; nothing here tests it)
;       6  commas between the integer part's groups of three
;       5  the leading spaces filled with "*"
;       4  a floating "$"
;       3  "+" for a positive number's sign, not a space
;       2  the sign after the number
;       0  E format (bit 6 then ignored)
;
; FMTAL keeps the flags and FMTAH the comma count (3, or 0 without commas):
; the 8086's $FMTAX. FMTCX keeps CX. Returns FBPTR at the text, zero-ended,
; in FBUFF. FBUFF+1 is the sign's place, the digits from FBUFF+2.
PUFOUT:
PUFOT:          STA     FMTAL
                AND     #$40           ; commas?
                BEQ     PFO05          ; no: AH = 0 (and A is)
                LDA     #3
PFO05:          STA     FMTAH
                LDA     RCL
                STA     FMTCX
                LDA     RCH
                STA     FMTCX+1
                LDA     #<(FBUFF+1)
                STA     FBPTR
                LDA     #>(FBUFF+1)
                STA     FBPTR+1
                LDA     #' '           ; the sign's place
                STA     (FBPTR)
                LDA     FMTAL
                AND     #$08           ; "+" wanted?
                BEQ     PFO10
                LDA     #'+'
                STA     (FBPTR)
PFO10:          JSR     SIGNS
                BPL     PFO20
                LDA     #'-'
                STA     (FBPTR)
                JSR     VNEG           ; the magnitude from here on
PFO20:          INCW    FBPTR
                LDA     #'0'           ; in case the number is zero
                STA     (FBPTR)
                LDA     FMTCX          ; MOV CX,$FMTCX
                STA     RCL
                LDA     FMTCX+1
                STA     RCH
                JSR     GETYP
                BMI     PFO40          ; an integer
                JMP     PUF
PFO40:          JMP     PUI

; --- $PUI: an integer (MATH2.ASM 1615) -----------------------------------------------

; $FOTCI's 5 digits with the point after them (CH 6) and its commas (CL the
; count, FMTAH), any leading zeros the field wants first; in E format, as a
; single by $PUFE. $PUIZ: the point dropped if none is wanted (A, the places
; after it, 0), or zeros after it; FMTCX popped back; then $PUFXE.
PUI:            LDA     FMTAH          ; CL: $FOTCI's commas
                STA     RCL
                LDA     #6             ; CH: at most 5 digits before the point
                STA     RCH
                LDA     FMTCX          ; DX = $FMTCX
                STA     RDL
                LDA     FMTCX+1
                STA     RDH
                LDA     FMTAL
                LSR     A              ; E format?
                BCC     PI10
                LDA     FBPTR          ; PUSH BX, for $PUFE
                PHA
                LDA     FBPTR+1
                PHA
                JSR     CSI            ; (the 6502's $CSI keeps DX itself)
                JMP     PUFE
PI10:           LDA     RDH            ; more than 5 places before the point:
                SEC                    ; zeros for the rest
                SBC     #5
                BMI     PI20
                JSR     FOTZ
PI20:           LDA     RDH            ; PUSH DX
                PHA
                LDA     RDL
                PHA
                JSR     FOTCI
                PLA                    ; POP AX: the places after the point
                PHA                    ; PUSH AX
PUIZ:           CMP     #0             ; none: the point dropped
                BNE     PI30
                DECW    FBPTR
                LDA     #0
PI30:           DEC     A              ; zeros after the point?
                BMI     PI40
                JSR     FOTZ
                LDA     #0             ; the end again
                STA     (FBPTR)
PI40:           PLA                    ; POP $FMTCX
                STA     FMTCX
                PLA
                STA     FMTCX+1
                JMP     PUFXE

; --- $PUF: a single or a double (MATH2.ASM 1262) ------------------------------------

; FBPTR at FBUFF+2. E format is $PUFE's. Fixed: 10^16 and more is $FOUT's
; free format with a "%" before it. Else $FOTNV brackets the digits into the
; integer part, A the exponent that undoes it: positive or zero, the digits
; are all before the point (zeros first if the field wants them, the
; exponent's zeros after them, by $FOTZC, with the commas); negative, the
; number is divided by ten until the digits after the point are as many as
; the field has, so that $FOTCV's rounding rounds at the field's last digit,
; and the zeros before and after them are the field's. $PUFXE fits it.
PUF:            LDA     FBPTR          ; PUSH BX
                PHA
                LDA     FBPTR+1
                PHA
                LDA     FMTAL
                LSR     A              ; E format?
                BCC     PUF10
                JMP     PUFE
PUF10:          LDA     #<(DP00+16*8)  ; $DP16: 10^16
                STA     PTR
                LDA     #>(DP00+16*8)
                STA     PTR+1
                JSR     PUCMP          ; less: C set
                BCS     PUF30
                PLA                    ; POP BX
                STA     FBPTR+1
                PLA
                STA     FBPTR
                JSR     FOUT           ; free format, FBPTR at FBUFF+1
                DECW    FBPTR
                LDA     #'%'           ; and the overflow mark before it
                STA     (FBPTR)
                RTS
PUF30:          JSR     GETYP          ; CH: the printable digits
                LDA     #16            ; a double's 16
                BCS     PUF40
                LDA     #7             ; a single's 7
PUF40:          STA     RCH
                JSR     SIGNS          ; zero: A = 0, and no $FOTNV
                BEQ     PUF50
                JSR     FOTNV          ; A: the exponent, N if negative
PUF50:          TAX
                PLA                    ; POP BX
                STA     FBPTR+1
                PLA
                STA     FBPTR
                TXA
                BMI     PUF80          ; digits after the point
                STA     RDL            ; DL: the exponent
                CLC
                ADC     RCH            ; the places the digits need, less the
                SEC                    ; field's: zeros if negative
                SBC     FMTCX+1
                BPL     PUF60
                EOR     #$FF           ; NEG
                INC     A
                JSR     FOTZ
PUF60:          STZ     RCL            ; no zeros made
                JSR     DPCOM
                LDA     FMTCX+1        ; PUSH $FMTCX
                PHA
                LDA     FMTCX
                PHA
                LDA     RDH            ; PUSH DX
                PHA
                LDA     RDL
                PHA
                JSR     FOTCV          ; the printable digits
                PLA                    ; POP DX
                STA     RDL
                PLA
                STA     RDH
                PLA                    ; POP $FMTCX
                STA     FMTCX
                PLA
                STA     FMTCX+1
                PHA                    ; PUSH $FMTCX
                LDA     FMTCX
                PHA
                LDA     RDL            ; the exponent's zeros before the point,
                BEQ     PUF70          ; with its commas, then the point
                JSR     FOTZC
                JSR     FOTED
PUF70:          PLA                    ; POP $FMTCX
                STA     FMTCX
                PLA
                STA     FMTCX+1
                PHA                    ; PUSH $FMTCX
                LDA     FMTCX          ; AL: the places after the point
                PHA
                JMP     PUIZ
PUF80:          STA     RDL            ; DL: the exponent, negative
                LDA     FMTCX          ; the digits wanted after the point
                BEQ     PUF90          ; (the point counted in FMTCX)
                DEC     A
PUF90:          STA     RDH            ; DH
                CLC
                ADC     RDL            ; less those there are
                STA     RCL            ; CL: minus the digits to shift off
                BMI     PUF100
                LDA     #0             ; all the digits wanted: none shifted
                STA     RCL
                BRA     PUF110
PUF100:         PHA                    ; PUSH AX, CX, DX (BX: $DIV10 keeps FBPTR)
                LDA     RCL
                PHA
                LDA     RCH
                PHA
                LDA     RDL
                PHA
                LDA     RDH
                PHA
                JSR     DIV10          ; a digit shifted off
                PLA                    ; POP DX, CX, AX
                STA     RDH
                PLA
                STA     RDL
                PLA
                STA     RCH
                PLA
                STA     RCL
                PLA
                INC     A
                BMI     PUF100
                ; (MOV AH,CL: AH isn't used after)
PUF110:         LDA     RDL            ; the exponent, less the zeros made,
                SEC                    ; plus the digits: negative, no digits
                SBC     RCL            ; before the point
                CLC
                ADC     RCH
                BPL     PUF120
                LDA     FMTCX+1        ; the field's zeros before the point
                JSR     FOTZ
                LDA     #'.'
                STA     (FBPTR)
                LDA     FBPTR          ; for $FOTED and $PUFXE
                STA     DPADR
                LDA     FBPTR+1
                STA     DPADR+1
                INCW    FBPTR
                STZ     RCL            ; no commas
                LDA     RDH            ; zeros after the point, before the
                SEC                    ; digits $FOTCV puts in
                SBC     RCH
                JSR     FOTZ
                STZ     RCL            ; XOR CX,CX: no point, no commas
                STZ     RCH
                LDA     RDH            ; PUSH DX
                PHA
                LDA     RDL
                PHA
                LDA     FMTCX+1        ; PUSH $FMTCX
                PHA
                LDA     FMTCX
                PHA
                BRA     PUF140
PUF120:         LDA     RDH            ; PUSH DX
                PHA
                LDA     RDL
                PHA
                LDA     FMTCX+1        ; PUSH $FMTCX
                PHA
                LDA     FMTCX
                PHA
                LDA     FMTCX+1        ; zeros before the digits: the places
                SEC                    ; wanted less (the printable digits,
                SBC     RCH            ; plus the exponent, less the zeros
                SEC                    ; made)
                SBC     RDL
                CLC
                ADC     RCL
                BMI     PUF130
                JSR     FOTZ
PUF130:         JSR     DPCOM
PUF140:         LDA     FMTCX+1        ; PUSH $FMTCX
                PHA
                LDA     FMTCX
                PHA
                JSR     FOTCV          ; the digits, with the point and commas
                PLA                    ; POP $FMTCX
                STA     FMTCX
                PLA
                STA     FMTCX+1
                PLA                    ; POP AX: the places after the point
                STA     RAL
                PLA
                STA     RAH
                PLA                    ; POP DX
                STA     RDL
                PLA
                STA     RDH
                LDA     FMTCX          ; no places after the point: back to
                BNE     PUF150         ; the point, which goes
                LDA     DPADR
                STA     FBPTR
                LDA     DPADR+1
                STA     FBPTR+1
                JMP     PUFXE
PUF150:         LDA     RAL            ; trailing zeros: the places after the
                CLC                    ; point (and the point), less the
                ADC     RDL            ; digits there are
                DEC     A
                BMI     PUF160
                JSR     FOTZ
PUF160:         JMP     PUFXE

; PUCMP: $MOVBF and $VCOMP, or $MOVBS and $COMPM: the FAC against the power
; of ten at PTR (a double of dmath.s's table), by the FAC's type: a single
; against its high 4 bytes, a double against the 8 copied to DBUFF, there
; being no code segment. As $FCOMP: C set if the FAC is less, N with it.
PUCMP:          JSR     GETYP
                BCS     @dbl
                CLC
                LDA     PTR
                ADC     #4
                STA     PTR
                BCC     :+
                INC     PTR+1
:               JMP     COMPM
@dbl:           LDY     #7
                LDX     #7
@m:             LDA     (PTR),Y
                STA     DBUFF,X
                DEY
                DEX
                BPL     @m
                LDX     #DBUFF
                JMP     DCMPM

; --- DPCOM: the point's and the commas' counts (MATH2.ASM 1500) -------------------

; CH = the printable digits (CH), plus the exponent (DL), less the zeros
; made (CL), plus one for the sign: $FOTED's count to the point. CL = that
; count mod 3 (3 for 0) to the first comma, or 0 without commas.
DPCOM:          LDA     RCH
                CLC
                ADC     RDL
                SEC
                SBC     RCL
                INC     A
                STA     RCH
DPC10:          SEC                    ; less 3 while greater than 0, signed
                SBC     #3
                BEQ     @le            ; (JG)
                BVS     @v
                BPL     DPC10
                BRA     @le
@v:             BMI     DPC10
@le:            CLC
                ADC     #3
                STA     RCL
                LDA     FMTAL
                AND     #$40           ; commas?
                BNE     DPC20
                STA     RCL            ; no: the count 0
DPC20:          RTS

; --- $FOTZ, $FOTZC: zeros (MATH1.ASM 324) ----------------------------------------

; $FOTZ: A zeros at FBPTR (none for 0); A returns 0. $FOTZC: A zeros (one
; at least), $FOTED's point and commas between them, its counts in CX.
FOTZ:           TAX
                BRA     FTZ15
FTZ10:          LDA     #'0'
                STA     (FBPTR)
                INCW    FBPTR
                DEX
FTZ15:          BNE     FTZ10
                TXA
                RTS
FOTZC:          TAX
                BRA     FTZC20
FTZC10:         JSR     FOTED          ; (keeps X)
FTZC20:         LDA     #'0'
                STA     (FBPTR)
                INCW    FBPTR
                DEX
                BNE     FTZC10
                RTS

; --- $PUFE: E format (MATH1.ASM 357) -------------------------------------------------

; A single or a double (an integer comes as a single), FBPTR on the 6502's
; stack. $FOTNV brackets the digits (7 or 16) into the integer part; if the
; field wants fewer, the number is divided by ten until the rest are after
; the point, rounded there and made an integer; if the rounding made it a
; digit longer (as big as the power of ten it was under), the exponent goes
; up and it is divided once more. $FOTCV then puts the digits in with the
; point after the field's places before it (and the sign's), zeros follow if
; the field wants more digits than there are, and $PUEXP the exponent. The
; 8086 keeps the exponent, the digit counts and the zero flag on its stack;
; here they are PFEEXP, PFEDIG, PFEZER, PFECMP and PFEFMT.
PUFE:           JSR     GETYP
                LDA     #7             ; AH: a single's 7 printable digits
                BCC     PFE10
                LDA     #16            ; a double's 16
PFE10:          STA     RAH
                JSR     SIGNS
                STA     RAL            ; AL: 0 if the number is zero
                PLA                    ; POP BX
                STA     FBPTR+1
                PLA
                STA     FBPTR
                SEC                    ; CF: the number is zero ($FOTNV's
                LDA     RAL            ; CF is clear)
                BEQ     PFE20
                LDA     RAH            ; PUSH AX
                PHA
                JSR     FOTNV          ; AL: the exponent (FBPTR kept)
                STA     RAL
                PLA                    ; POP DX: AH, the digits
                STA     RAH
                CLC
PFE20:          ROR     PFEZER         ; PUSHF, PUSH AX: bit 7 the zero flag
                LDA     RAL
                STA     PFEEXP
                LDA     RAH
                STA     PFEDIG
                LDA     FMTCX          ; DX = $FMTCX
                STA     RDL
                LDA     FMTCX+1
                STA     RDH
                LDA     RDL            ; DL: the digits after the point, the
                BEQ     PFE30          ; point not counted
                DEC     RDL
PFE30:          LDA     RDH            ; DH: the digits wanted in all
                CLC
                ADC     RDL
                LDX     RDH            ; (OR DH,DH; PUSHF, before the ADD)
                STA     RDH
                CPX     #0             ; places before the point: one of them
                BEQ     PFE40          ; is the sign's, unless it trails
                LDA     FMTAL
                AND     #$04
                BNE     PFE40
                DEC     RDH
PFE40:          LDA     RDH            ; AH: the digits wanted less those there
                SEC                    ; are; negative, too many
                SBC     PFEDIG
                STA     RAH
                STA     PFECMP         ; PUSH AX
                BMI     PFE45
                JMP     PFE65
PFE45:          LDA     RAH            ; (PUSH BX, PUSH AX: FBPTR kept)
PFE50:          PHA                    ; divided by ten for each one too many
                JSR     DIV10
                PLA
                INC     A
                BNE     PFE50
                JSR     VADDH          ; rounded there
                JSR     VINT           ; and an integer
                LDA     PFECMP         ; POP AX, PUSH AX: the divisions,
                ASL     A              ; negative, times 8 (the table's 8 bytes
                ASL     A              ; an entry) and sign-extended (CBW)
                ASL     A
                LDX     #0
                ORA     #0
                BPL     :+
                DEX
:               STA     T0
                STX     T1
                JSR     GETYP          ; under 10^(16 less the divisions), or
                LDA     #<(DP00+16*8)  ; 10^(7 less) for a single
                LDX     #>(DP00+16*8)
                BCS     PFE55
                LDA     #<(DP00+7*8)
                LDX     #>(DP00+7*8)
PFE55:          CLC
                ADC     T0
                STA     PTR
                TXA
                ADC     T1
                STA     PTR+1
                JSR     PUCMP          ; less: no carry into another digit
PFE57:          BMI     PFE60
                INC     PFEEXP         ; POP CX, INC CL, PUSH CX: the exponent up
                JSR     DIV10          ; and a digit more after the point
PFE60:          LDA     PFECMP         ; AH: the divisions, negative
                STA     RAH
                BRA     PFE66
PFE65:          STZ     RAH            ; no divisions
PFE66:          LDA     RAH            ; NEG AH
                EOR     #$FF
                INC     A
                CLC                    ; plus the places before the point,
                ADC     FMTCX+1        ; plus one ($FOTED counts down first)
                INC     A
                STA     RAH
                LDA     FMTCX+1        ; less one for the sign's place, unless
                BEQ     PFE70          ; there are none or the sign trails
                LDA     FMTAL
                AND     #$04
                BNE     PFE70
                DEC     RAH
PFE70:          LDA     RAH            ; CH: the point's place
                STA     RCH
                STZ     RCL            ; no commas in E format
                LDA     FMTCX          ; PUSH $FMTCX
                STA     PFEFMT
                LDA     FMTCX+1
                STA     PFEFMT+1
                LDA     RCH
                STA     FMTCX+1
                JSR     FOTCV
                LDA     PFECMP         ; trailing zeros if more were wanted
                BEQ     PFE80          ; (JLE)
                BMI     PFE80
                JSR     FOTZC
                JSR     FOTED          ; and the point, if it is next
PFE80:          LDA     PFEFMT         ; POP AX, MOV $FMTCX,AX
                STA     FMTCX
                LDA     PFEFMT+1
                STA     FMTCX+1
                LDA     FMTCX          ; no point wanted: one at the end goes
                BNE     PFE100
                DECW    FBPTR
                LDA     (FBPTR)
                CMP     #'.'
                BEQ     PFE90
                INCW    FBPTR
PFE90:          LDA     FBPTR
                STA     DPADR
                LDA     FBPTR+1
                STA     DPADR+1
PFE100:         LDA     PFEEXP         ; POP AX; POPF
                BIT     PFEZER         ; zero: its exponent is 0
                BMI     PFE110
                CLC                    ; the exponent plus the digits, less
                ADC     PFEDIG         ; the places before the point
                SEC
                SBC     FMTCX+1
                LDX     FMTCX+1        ; plus one for the sign's place, unless
                BEQ     PFE110         ; there are none or the sign trails
                TAX
                LDA     FMTAL
                AND     #$04
                BNE     @t
                INX
@t:             TXA
PFE110:         JSR     PUEXP          ; E or D and the exponent; the end in CX
                LDA     RCL            ; MOV BX,CX
                STA     FBPTR
                LDA     RCH
                STA     FBPTR+1
                ; falls into $PUFXE

; --- $PUFXE: the fix-up (MATH1.ASM 145) ------------------------------------------------

; With FBPTR at the number's end: $FOTZS suppresses the leading zeros, puts
; in the "$" and the sign (or returns a trailing sign, put in here). Then
; from FBUFF+1 the field is fitted: while there are more places before the
; point than the field has, a leading space or "*" is dropped, or a leading
; "0" before the point if a digit follows it (the signs and "$" before it
; moved up over it); if none can go, a "%" before the number. The 8086 walks
; the buffer with BX and $CHRGT (CHRGTR) and keeps the signs on its stack;
; here TXTPTR is lent to CHRGTR, as $FOTZS lends it, and the signs go on
; the 6502's stack above a 0. Returns FBPTR at the text.
PUFXE:          LDA     FBPTR          ; PUSH BX
                PHA
                LDA     FBPTR+1
                PHA
                JSR     FOTZS          ; A: a trailing sign, or 0
                PLA                    ; POP BX
                STA     FBPTR+1
                PLA
                STA     FBPTR
                LDA     RCH
                BEQ     PFX10
                STA     (FBPTR)        ; the trailing sign
                INCW    FBPTR
PFX10:          LDA     #0             ; the end
                STA     (FBPTR)
                LDA     TXTPTR+1       ; the text pointer kept
                PHA
                LDA     TXTPTR
                PHA
                LDA     #<FBUFF        ; BX: TXTPTR, from FBUFF
                STA     TXTPTR
                LDA     #>FBUFF
                STA     TXTPTR+1
PFX20:          INCW    TXTPTR
PFX30:          SEC                    ; the point's address, less here, less
                LDA     DPADR          ; the places before the point: 0 when
                SBC     TXTPTR         ; the number fits
                STA     T0
                LDA     DPADR+1
                SBC     TXTPTR+1
                STA     T1
                SEC
                LDA     T0
                SBC     FMTCX+1
                STA     T0
                LDA     T1
                SBC     #0
                ORA     T0
                BEQ     PFX90
                LDA     (TXTPTR)       ; a leading space or "*" goes
                CMP     #' '
                BEQ     PFX20
                CMP     #'*'
                BEQ     PFX20
                DECW    TXTPTR         ; CHRGTR goes on first
                LDA     TXTPTR+1       ; PUSH BX: the "%"'s place, if need be
                PHA
                LDA     TXTPTR
                PHA
                LDA     #0             ; the end of the signs (the 8086's AH 1)
PFX40:          PHA
                JSR     CHRGTR         ; (the first time, the same character)
                CMP     #'-'           ; a sign or "$": kept
                BEQ     PFX40
                CMP     #'+'
                BEQ     PFX40
                CMP     #'$'
                BEQ     PFX40
                CMP     #'0'           ; a "0" can go if a digit follows
                BNE     PFX70          ; the point after it
                INCW    TXTPTR
                JSR     CHRGTR
                BCC     PFX70          ; not a digit: no hope
                DECW    TXTPTR         ; at the point
                BRA     PFX60
PFX50:          TAX                    ; the signs moved up over the "0"
                DECW    TXTPTR
                TXA
                STA     (TXTPTR)
PFX60:          PLA
                BNE     PFX50
                PLA                    ; ADD SP,2: the "%"'s place dropped
                PLA
                BRA     PFX30          ; does it fit now?
PFX70:          PLA                    ; the signs dropped
                BNE     PFX70
                PLA                    ; POP BX: the "%" before the number
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                LDA     #'%'
                STA     (TXTPTR)
PFX90:          LDA     TXTPTR         ; BX: the text's start, in FBPTR
                STA     FBPTR
                LDA     TXTPTR+1
                STA     FBPTR+1
                PLA                    ; the text pointer back
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                RTS

; LINPRT (MATH1.ASM 2715): print BX as a number (a line number, for TRON).
; LINOUT leaves it in
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

.rodata

; $FOTZS's jump table.
FZ45:           .word   FZ50, FZ50, FZ60, FZ60, FZ60, FZ60, FZ50, FZ60, FZ10, FZ10

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

; $FINCH: the characters $FOTZS stops at.
FINCH:          .byte   ".", "e", "E", "%", "#", "!", "D", "d", ",", "0"

; $FOSTB then $FOITB, which must follow it: powers of ten for $FOTCV's
; first 3 digits (24-bit) and $FOTCI's (16-bit).
FOSTB:          .byte   $40,$42,$0F    ; 1000000
                .byte   $A0,$86,$01    ; 100000
                .byte   $10,$27,$00    ; 10000
FOITB:          .word   10000, 1000, 100, 10, 1

; $FODTB: 10^15 down to 10^7, 7-byte integers, for a double's first 9
; digits (MATH1.ASM 681).
FODTB:          .byte   $00,$80,$C6,$A4,$7E,$8D,$03    ; 1D15
                .byte   $00,$40,$7A,$10,$F3,$5A,$00    ; 1D14
                .byte   $00,$A0,$72,$4E,$18,$09,$00    ; 1D13
                .byte   $00,$10,$A5,$D4,$E8,$00,$00    ; 1D12
                .byte   $00,$E8,$76,$48,$17,$00,$00    ; 1D11
                .byte   $00,$E4,$0B,$54,$02,$00,$00    ; 1D10
                .byte   $00,$CA,$9A,$3B,$00,$00,$00    ; 1D9
                .byte   $00,$E1,$F5,$05,$00,$00,$00    ; 1D8
                .byte   $80,$96,$98,$00,$00,$00,$00    ; 1D7

; HIDBL (MATH2.ASM 776): 9999999999999999, the largest double .5 can be
; added to without overflow.
HIDBL:          .byte   $FD,$FF,$03,$BF,$C9,$1B,$0E,$B6

; $NUMB: OCT$ and HEX$'s digits.
NUMB:           .byte   "0123456789ABCDEF"

.segment "BRAM"

; $PUFE's: what the 8086 keeps on its stack.
PFEZER:         .res    1              ; bit 7: the number is zero (the pushed CF)
PFEEXP:         .res    1              ; the exponent (the pushed AL)
PFEDIG:         .res    1              ; the printable digits, 7 or 16 (the pushed AH)
PFECMP:         .res    1              ; the digits wanted less those there are
PFEFMT:         .res    2              ; $FMTCX kept round $FOTCV

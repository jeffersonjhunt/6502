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
; With the tables they need: $FOTB, $FODTB, $FOSTB, $FOITB, $FINCH, HIDBL,
; $NUMB. The PRINT USING routines ($PUFOT and the rest) are 7b's.

.include "gwbasic.inc"

;@entry FOUT
;@entry FOUT2
;@entry LINOUT
;@entry LINPRT
;@entry FOUTO
;@entry FOUTH
.export FOUT, FOUT2, LINOUT, LINPRT, FOUTO, FOUTH
.export FOTCI, FCI4, FOTZS, FOFMT, FOTAN, FOTED, FOTCV, FOTNV, SIGD, FINCH
.export FOTB, FOSTB, FOITB, FODTB
.import GETYP, SIGNS, VNEG, MAKINT, STROUI, CHRGTR, FBUFF, FCOMP, COMPM, QINT
.import INT, FADDH, VINT, VMOVM, VMVMF, VMVFM, VCMPM, VPSHF, VPOPF, VMOVAF
.import MDPTEN, MUL10, DIV10, MOVAC, MOVFA, FMULD, DINT, DCMPA, DCMPM, DADDH
.import FTDNT, VADDH, DP06, DP09

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
PUEXP:          STA     RAL            ; $PUEXP (PRINT USING's too, 7b)
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

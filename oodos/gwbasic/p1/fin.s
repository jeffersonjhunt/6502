; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Number input, in block P1 (hw/oodos.md, "The plan, phase 7"): the text at
; TXTPTR to the FAC, for VAL, INPUT and READ, and for digits typed where
; they weren't tokenised. From MATH1.ASM and MATH2.ASM:
;
;   $FIN $FINDB           the number, its own type or double (MATH1)
;   $FIDIG                a digit more (MATH2)
;   $FINEX                the exponent (MATH2)
;   $FINE                 times 10^(exponent - digits past the point) (MATH1)
;   $FIND $FINS           "#" and "!" (MATH2)
;   $CHKEL                "E" of ELSE or EQV, not an exponent (MATH1)
;   $CONI2                -32768 back to an integer (MATH1)
;
; and GWMAIN.ASM's $FLGOC (FINOVC), which $FIN returns through: OVCSTR says
; whether an overflow was printed, for INPUT, and the overflow mode goes back
; to normal. The 8086 keeps DI, CX, SI and DX through these; here they are
; FINDI, FINCX, FINSI and FINDX in zero page (gwbasic.inc).

.include "gwbasic.inc"

;@entry FIN
;@entry FINDBL
.export FIN, FINDBL, OVCSTR
.import CHRGTR, GETYP, ZERO, CSI, CDS, FLT, FRCSNG, FRCDBL, VNEG, FCOMP
.import MUL10, FADDS, FADDD, PUSHF, POPR, VMOVAF, MDPTEN, OCTCNS, FINCH

.code

; --- $FIN, $FINDB (MATH1.ASM 2059) ------------------------------------------------

; TXTPTR at the number's first character; it returns past the number. The
; FAC starts as an integer (or, from $FINDB, a double) and goes to single
; and double as the digits need, or as "!", "#", "%", "E" or "D" say.
FIN:            LDA     #0
                BRA     FN20
FINDBL:         LDA     #VT_DBL
                STA     VALTYP
                LDA     #1
FN20:           STA     FINNEG         ; for now: 1 if forced double
                LDA     #1             ; overflow printed once
                STA     FLGOV
                PUSHRET FINOVC         ; and back through FINOVC
                STZ     FINDI          ; digits past the point
                STZ     FINDI+1
                LDA     #$FF           ; no point yet
                STA     FINCX
                STA     FINCX+1
                STZ     FINSI
                JSR     ZERO
                LDA     FINNEG
                BNE     FN30
                LDA     #VT_INT
                STA     VALTYP
FN30:           LDA     (TXTPTR)
                CMP     #'&'
                BNE     FN40
                JMP     OCTCNS         ; &H or &O
FN40:           STZ     FINNEG
                CMP     #'-'
                BNE     @1
                DEC     FINNEG         ; negative: negated once formed
                BRA     FN50
@1:             CMP     #'+'
                BEQ     FN50
                DECW    TXTPTR
FN50:           JSR     CHRGTR
                BCC     FN60
FN55:           JSR     FIDIG
                BRA     FN50
FN60:           STZ     FINDX          ; the exponent is zero
                STZ     FINDX+1
                STZ     FINSI
                LDX     #7             ; one of . e E % # ! D d?
FN70:           CMP     FINCH,X
                BEQ     FN80
                DEX
                BPL     FN70
                BRA     FN96           ; no: the end of the number
FN80:           TXA
                ASL     A
                TAX
                JMP     (FN95,X)
FN92:           SEC                    ; "D": double
FN94:           JSR     FINEX
FN96:           JSR     FINE
                BRA     FINF
FN100:          INCW    FINCX          ; "."
                LDA     FINCX
                ORA     FINCX+1
                BNE     FN96           ; a second one ends it
                JSR     GETYP
                BPL     FN50
                JSR     CSI            ; an integer becomes single
                BRA     FN50
FN200:          JSR     CHKEL          ; "E": ELSE or EQV?
                BEQ     FN96
                CLC                    ; no: single
                BRA     FN94
FN400:          INCW    TXTPTR         ; "%": eaten
                BRA     FN96
FN500:          JSR     FIND           ; "#"
                BRA     FINF
FN600:          JSR     FINS           ; "!"
FINF:           LDA     FINNEG
                BEQ     FN990
                JSR     VNEG
                LDA     VALTYP
                CMP     #VT_SNG
                BNE     FN990
                JSR     CONI2
FN990:          RTS

; FINOVC (GWMAIN.ASM 3535, $FLGOC): whether an overflow was printed to
; OVCSTR, and the overflow mode back to normal ($CLROV). Keeps A and the
; flags.
FINOVC:         PHP
                PHA
                LDA     FLGOV
                STA     OVCSTR
                STZ     FLGOV
                PLA
                PLP
                RTS

; --- $FIDIG: a digit more (MATH2.ASM 21) ----------------------------------------

; A the digit's character, C set (CHRGTR's). DI counts the digits past the
; point: DI + CX + 1, CX being $FFFF before the point and 0 after. An
; integer goes single when it can't take another digit; a single goes double
; at 10^6, where another digit would lose accuracy.
FIDIG:          PHA
                LDA     FINDI
                ADC     FINCX
                STA     FINDI
                LDA     FINDI+1
                ADC     FINCX+1
                STA     FINDI+1
                PLA
                SEC
                SBC     #'0'
                STA     FINDIG
                JSR     GETYP
                BPL     FI05
                LDA     FACLO          ; an integer: under 3277?
                CMP     #<3277
                LDA     FACLO+1
                SBC     #>3277
                BCS     FFI10
                LDA     FACLO          ; BX * 10
                STA     T0
                LDA     FACLO+1
                STA     T1
                ASL     T0
                ROL     T1
                ASL     T0
                ROL     T1
                CLC
                LDA     T0
                ADC     FACLO
                STA     T0
                LDA     T1
                ADC     FACLO+1
                STA     T1
                ASL     T0
                ROL     T1
                CLC                    ; and the digit
                LDA     T0
                ADC     FINDIG
                STA     T0
                LDA     T1
                ADC     #0
                BMI     FFI10          ; 32768 or 32769
                STA     FACLO+1
                LDA     T0
                STA     FACLO
                RTS
FI05:           BCC     FFFI20         ; single
                BRA     FI40           ; double
FFI10:          JSR     CSI            ; too big: single first
                BRA     FI30
FFFI20:         STZ     RDL            ; under 10^6?
                LDA     #$24
                STA     RDH
                LDA     #$74
                STA     RBL
                LDA     #$94
                STA     RBH
                JSR     FCOMP
                BPL     FI35           ; no: double
FI30:           JSR     MUL10          ; times 10
                JSR     PUSHF
                LDA     FINDIG         ; plus the digit
                STA     RDL
                STZ     RDH
                JSR     FLT
                JSR     POPR
                JMP     FADDS
FI35:           JSR     CDS
FI40:           JSR     MUL10
                JSR     VMOVAF
                LDA     FINDIG
                STA     RDL
                STZ     RDH
                JSR     FLT
                JSR     CDS
                JMP     FADDD

; --- $FINEX: the exponent (MATH2.ASM 100) ----------------------------------------

; The FAC first forced to double (C set: "D") or single (C clear: "E"),
; unless it is double already. Then the exponent's digits to DX, its sign to
; SI ($FF negative); past 3275 it becomes 32767, for an overflow.
FINEX:          PHP
                LDA     VALTYP
                CMP     #VT_DBL
                BEQ     @d
                PLP
                BCC     @s
                JSR     FRCDBL
                BRA     EXB
@s:             JSR     FRCSNG
                BRA     EXB
@d:             PLP
EXB:            STZ     FINSI
                STZ     FINDX
                STZ     FINDX+1
                JSR     CHRGTR
                BCS     FX20           ; no sign: positive
                CMP     #'-'
                BNE     FX00
                DEC     FINSI
                BRA     FX10
FX00:           CMP     #'+'
                BEQ     FX10
                RTS                    ; something else: TXTPTR at it
FX10:           JSR     CHRGTR
                BCS     FX20
                RTS
FX20:           PHA
                LDA     FINDX          ; DX < 3276?
                CMP     #<3276
                LDA     FINDX+1
                SBC     #>3276
                BCC     FX30
                PLA
                LDA     #<32767        ; to be sure of overflow
                STA     FINDX
                LDA     #>32767
                STA     FINDX+1
                BRA     FX10
FX30:           LDA     FINDX          ; DX * 10
                STA     T0
                LDA     FINDX+1
                STA     T1
                ASL     FINDX
                ROL     FINDX+1
                ASL     FINDX
                ROL     FINDX+1
                CLC
                LDA     FINDX
                ADC     T0
                STA     FINDX
                LDA     FINDX+1
                ADC     T1
                STA     FINDX+1
                ASL     FINDX
                ROL     FINDX+1
                PLA                    ; plus the digit
                SEC
                SBC     #'0'
                CLC
                ADC     FINDX
                STA     FINDX
                BCC     FX10
                INC     FINDX+1
                BRA     FX10

; --- $FINE: the exponent applied (MATH1.ASM 1165) --------------------------------

; DX = the exponent, negated if SI says so, less the digits past the point;
; then MDPTEN. If the subtraction overflows (an exponent near -32768 with
; digits past the point) the 8086 jumps into MDPTEN's underflow with the
; stack wrong; the port takes it as underflow, as meant: -32768.
FINE:           LDA     FINDX
                STA     RDL
                LDA     FINDX+1
                STA     RDH
                LDA     FINSI
                BEQ     FIN05
                SEC                    ; NEG DX
                LDA     #0
                SBC     RDL
                STA     RDL
                LDA     #0
                SBC     RDH
                STA     RDH
FIN05:          SEC                    ; less DI
                LDA     RDL
                SBC     FINDI
                STA     RDL
                LDA     RDH
                SBC     FINDI+1
                STA     RDH
                BVC     @1
                STZ     RDL
                LDA     #$80
                STA     RDH
@1:             LDA     RDL
                ORA     RDH
                BNE     :+
                RTS                    ; complete as it is
:               JMP     MDPTEN

; --- $FIND, $FINS: "#" and "!" (MATH2.ASM 165) -----------------------------------

FIND:           JSR     FRCDBL
                BRA     FC20
FINS:           JSR     FRCSNG
FC20:           STZ     FINSI          ; a forced number: no exponent
                STZ     FINDX
                STZ     FINDX+1
                JSR     FINE
                INCW    TXTPTR         ; past the "#" or "!"
                RTS

; --- $CHKEL: ELSE or EQV after all? (MATH1.ASM 2984) ----------------------------

; Z set if the "E" at TXTPTR begins one: the next character is L or Q.
; TXTPTR is kept (in FINDX, zero again after).
CHKEL:          LDA     TXTPTR
                STA     FINDX
                LDA     TXTPTR+1
                STA     FINDX+1
                JSR     CHRGTR
                CMP     #'l'
                BEQ     CK10
                CMP     #'L'
                BEQ     CK10
                CMP     #'q'
                BEQ     CK10
                CMP     #'Q'
CK10:           PHP
                LDA     FINDX
                STA     TXTPTR
                LDA     FINDX+1
                STA     TXTPTR+1
                STZ     FINDX
                STZ     FINDX+1
                PLP
                RTS

; --- $CONI2: -32768 back to an integer (MATH1.ASM 1882) --------------------------

CONI2:          STZ     RDL            ; the single -32768
                STZ     RDH
                LDA     #$80
                STA     RBL
                LDA     #$90
                STA     RBH
                JSR     FCOMP
                BNE     CON10
                LDA     #VT_INT
                STA     VALTYP
                STZ     FACLO
                LDA     #$80
                STA     FACLO+1
CON10:          RTS

.rodata

; FN95: $FIN's dispatch, by $FINCH's characters . e E % # ! D d.
FN95:           .word   FN100, FN200, FN200, FN400, FN500, FN600, FN92, FN92

.segment "BRAM"

OVCSTR:         .res    1              ; an overflow was printed in $FIN

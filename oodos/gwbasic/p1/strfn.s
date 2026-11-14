; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; BISTRS.ASM's string functions that need P1's number conversions, in block
; P1 (hw/oodos.md, "The plan, phase 7"), with GWMAIN.ASM's FRQINT:
;
;   STR$ (STR) OCT$ (STRO) HEX$ (STRH)   a number to text, the text a string
;   VAL                                   a string's number, by $FINDB
;   FRQINT                                OCT$ and HEX$'s -32768..65535, and
;                                         an entry for P3's FRMQNT (CLEAR's)
;
; FUNDSP reaches them through the resident core's F_ stubs (res/bistrs.s),
; back through LABBCK, which puts the text pointer back: so they may move
; TXTPTR, as the 8086's do BX. They call the resident string core by JSR.

.include "gwbasic.inc"

;@entry STR
;@entry STRO
;@entry STRH
;@entry VAL
;@entry FRQINT
.export STR, STRO, STRH, VAL, FRQINT
.import FOUT, FOUTO, FOUTH, FINDBL, STRLIT, FREFAC, STRCPY, PUTNEW, LEN1, SNGFLT
.import CHRGTR, GETYP, SIGNS, FRCSNG, FRCINT, FADD

.code

; STRO$, STRH$, STR$ (BISTRS.ASM 104): the text $FOUT (or $FOUTO, $FOUTH)
; leaves in FBUFF as a literal, copied into the string space.
STRO:           JSR     FRQINT
                JSR     FOUTO
                BRA     STR1
STRH:           JSR     FRQINT
                JSR     FOUTH
                BRA     STR1
STR:            JSR     FOUT
STR1:           LDA     FBPTR          ; the 8086's BX at it: TXTPTR here
                STA     TXTPTR
                LDA     FBPTR+1
                STA     TXTPTR+1
                JSR     STRLIT
                JSR     FREFAC
                JSR     STRCPY
                JMP     PUTNEW         ; FINBCK

; VAL (BISTRS.ASM 792): $FINDB on the string's text, a zero put past it
; for the while (the byte there kept: it may be the next string's).
VAL:            JSR     LEN1           ; BX: the descriptor, A the length
                BNE     :+
                JMP     SNGFLT         ; "": 0
:               STA     RDL
                LDY     #1             ; S1: the data
                LDA     (RBL),Y
                STA     S1
                INY
                LDA     (RBL),Y
                STA     S1+1
                CLC                    ; S2: the byte past it
                LDA     S1
                ADC     RDL
                STA     S2
                LDA     S1+1
                ADC     #0
                STA     S2+1
                LDA     (S2)
                PHA
                LDA     S2
                PHA
                LDA     S2+1
                PHA
                LDA     #0
                STA     (S2)
                SEC                    ; the text pointer before it, so
                LDA     S1             ; CHRGTR skips leading spaces
                SBC     #1
                STA     TXTPTR
                LDA     S1+1
                SBC     #0
                STA     TXTPTR+1
                JSR     CHRGTR
                JSR     FINDBL
                PLA                    ; the byte back
                STA     S2+1
                PLA
                STA     S2
                PLA
                STA     (S2)
                RTS

; FRQINT (GWMAIN.ASM 3084): the FAC to an integer, 32768..65535 taken as
; that less 65536.
FRQINT:         JSR     GETYP
                BMI     @int           ; an integer already
                LDA     FACEXP
                CMP     #$90           ; 32768 to 65535?
                BNE     @int
                JSR     SIGNS
                BMI     @int           ; negative: only -32768 will do
                JSR     FRCSNG
                STZ     RDL            ; BXDX = -65536
                STZ     RDH
                LDA     #$80
                STA     RBL
                LDA     #$91
                STA     RBH
                JSR     FADD
@int:           JMP     FRCINT

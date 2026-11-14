; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; BIPRTU.ASM, PRINT USING's driver, in block P3 (hw/oodos.md, "The plan,
; phase 7"), reached from P1's PRINT and LPRINT (p1/gio86.s JPRINU):
;
;   PRINUS    PRINT USING f$; list: f$ scanned for its fields, each value of
;             the list printed in the next field, the text between fields
;             printed as it is; f$ used again from its start while values
;             are left
;   PLSPRT    a "+" that turned out to be no field's, printed
;
; The fields: "!" a string's first character, "&" all of it, "\ \" its
; first 2 + n characters (n the spaces between), "#" a digit's place, "." the
; point, "+" a sign before or after, "-" a minus after, "**" leading "*"s,
; "$$" a floating "$", "**$" both, "," commas every three digits, "^^^^"
; E format; "_" prints the next character as it is. A number is formatted
; by P1's PUFOUT (p1/fout.s): a nested far call.
;
; The 8086 keeps the scan in BX, CX and DX and swaps BX with the text
; pointer: here the scan has its own zero page (USP, USCH, USCL, USDL, USDH:
; the 8086's BX, CH, CL, DL, DH), which nothing else touches, so the 8086's
; pushes of CX and DX round FRMEVL have no counterpart, and the text pointer
; is TXTPTR throughout. What it keeps on its stack across the list is
; P3's RAM: the format's descriptor (USDSC), and the flags it pushes, the
; list ended (Z, USEND) and a line's end wanted (C, USCR).
;
; The far call rule: PRINUS ends by jumping to FINPRT, resident code whose
; RTS returns through the far call to P1's PRINT; its errors (FCERR, SNERR,
; FRMEVL's) reset the 6502's stack.

.include "gwbasic.inc"

;@entry PRINUS
.export PRINUS
.import FRMCHK, FRMEVL, CHKSTR, SYNCHR, CHRGTR, FCERR, SNERR, OUTDO, CRDO
.import DSOUT, STRPRT, FRETM2, FINPRT, LEFTUS

CSTRNG          = '\'                  ; the "\ \" field's ends
CURNCY          = '$'                  ; the floating currency sign

.code

; --- PRINUS (BIPRTU.ASM 44) ------------------------------------------------------------

; TXTPTR at USING: the format, a string, and a ";".
PRINUS:         JSR     FRMCHK
                JSR     CHKSTR
                JSR     SYNCHR
                .byte   ';'
                LDA     FACLO          ; BX: the format's descriptor
                STA     RBL
                LDA     FACLO+1
                STA     RBH
                BRA     INIUS          ; not USFLG's test, the first time
; REUSST (BIPRTU.ASM 51): the format again, if a value went out last time.
REUSST:         LDA     USFLG
                BNE     :+
                JMP     FCERR          ; FCERR3's way: Illegal function call
:               LDA     USDSC          ; POP DX: the descriptor
                STA     RBL
                LDA     USDSC+1
                STA     RBH
; INIUS (BIPRTU.ASM 56)
INIUS:          LDA     RBL            ; PUSH BX: the descriptor kept
                STA     USDSC
                LDA     RBH
                STA     USDSC+1
                STZ     USFLG          ; no value printed yet
                STZ     USEND          ; PUSHF: the list not ended (Z clear,
                STZ     USCR           ; C clear)
                LDA     (RBL)          ; CH: the format's length; none is
                BNE     :+             ; Illegal function call
                JMP     FCERR
:               STA     USCH
                LDY     #1             ; BX: its characters
                LDA     (RBL),Y
                STA     USP
                INY
                LDA     (RBL),Y
                STA     USP+1
                BRA     PRCCHR

; BGSTRF (BIPRTU.ASM 78): "\": a string field if spaces and a "\" follow,
; 2 wide and one more a space; else the "\" is printed.
BGSTRF:         LDA     USCH           ; DL: the count, kept
                STA     USDL
                LDA     USP            ; PUSH BX: the place, kept
                PHA
                LDA     USP+1
                PHA
                LDA     #2
                STA     USCL
LPSTRF:         LDA     (USP)
                INCW    USP
                CMP     #CSTRNG        ; the field's end
                BNE     :+
                JMP     ISSTRF
:               CMP     #' '           ; a space widens it
                BNE     NOSTRF
                INC     USCL
                DEC     USCH
                BNE     LPSTRF
; NOSTRF (BIPRTU.ASM 98): not a field: the place and count back, the "\"
; printed.
NOSTRF:         PLA                    ; POP BX
                STA     USP+1
                PLA
                STA     USP
                LDA     USDL
                STA     USCH
                LDA     #CSTRNG
; NEWUCH (BIPRTU.ASM 104): a character no field's, printed, after a "+"
; before it.
NEWUCH:         JSR     PLSPRT
                JSR     OUTDO
; PRCCHR (BIPRTU.ASM 111): the next character. DL and DH 0, so no "+" is
; printed twice.
PRCCHR:         LDA     #0
                STA     USDL
                STA     USDH
; PLSFIN (BIPRTU.ASM 114): DH the "+" flag (A), any "+" before printed.
PLSFIN:         JSR     PLSPRT
                STA     USDH
                LDA     (USP)
                INCW    USP
                CMP     #'!'           ; one character of a string
                BNE     :+
                JMP     SMSTRF
:               CMP     #'#'           ; a number
                BEQ     NUMNUM
                CMP     #'&'           ; all of a string
                BNE     :+
                JMP     VARSTR
:               DEC     USCH           ; the rest take two characters at
                BNE     :+             ; least: this one the format's last,
                JMP     REUSIN         ; printed
:               CMP     #'+'           ; a "+": a number's sign, maybe
                BNE     @np
                LDA     #8
                BRA     PLSFIN
@np:            DECW    USP            ; the character again
                LDA     (USP)
                INCW    USP
                CMP     #'.'           ; a number with no places before the
                BEQ     DOTNUM         ; point
                CMP     #'_'           ; a literal character
                BNE     :+
                JMP     LITCHR
:               CMP     #CSTRNG        ; a "\ \" field?
                JEQ     BGSTRF
                CMP     (USP)          ; "$$" and "**" are two the same
                BNE     NEWUCH
                CMP     #CURNCY
                BEQ     DOLRNM
                CMP     #'*'
                BNE     NEWUCH
                INCW    USP            ; past "**": "**$"?
                LDA     USCH
                CMP     #2             ; (room for the "$")
                BCC     NOTSPC
                LDA     (USP)
                CMP     #CURNCY
                BEQ     ASTDOL
; NOTSPC (BIPRTU.ASM 151): "**", the "*" fill.
NOTSPC:         LDA     #32
                BRA     SPCNUM
ASTDOL:         DEC     USCH           ; "**$": the "$" counted, one place
                INC     USDL           ; more
                LDA     #32            ; and (the 8086's skip of DOLRNM's
                BRA     DOLRN2         ; XOR) the "*" fill with the "$"
; DOLRNM (BIPRTU.ASM 159): "$$", the floating "$".
DOLRNM:         LDA     #0
DOLRN2:         CLC
                ADC     #16
                INCW    USP            ; past the two
; SPCNUM (BIPRTU.ASM 162): two characters are one place more.
SPCNUM:         INC     USDL
                CLC
                ADC     USDH           ; the flags, with "+" if it came first
                STA     USDH
; NUMNUM (BIPRTU.ASM 167): a place before the point.
NUMNUM:         INC     USDL
                STZ     USCL           ; none after it yet
                DEC     USCH
                JEQ     ENDNUS         ; the format's end
                LDA     (USP)
                INCW    USP
                CMP     #'.'           ; the point
                BEQ     AFTDOT
                CMP     #'#'           ; another place
                BEQ     NUMNUM
                CMP     #','           ; commas
                BNE     FINNUM
                LDA     USDH
                ORA     #64
                STA     USDH
                BRA     NUMNUM
; DOTNUM (BIPRTU.ASM 193): "." a field's only if a "#" follows.
DOTNUM:         LDA     (USP)
                CMP     #'#'
                BEQ     :+
                LDA     #'.'           ; else printed
                JMP     NEWUCH
:               LDA     #1             ; CL: the point
                STA     USCL
                INCW    USP
; AFTDOT (BIPRTU.ASM 202): a place after the point.
AFTDOT:         INC     USCL
                DEC     USCH
                BEQ     ENDNUS
                LDA     (USP)
                INCW    USP
                CMP     #'#'
                BEQ     AFTDOT
; FINNUM (BIPRTU.ASM 216): "^^^^" after the places is E format. The 8086
; pushes DX and NOTSCI's address, keeps BX in DX, and returns to NOTSCI at
; the first character that isn't; here a branch, BX kept in USSAV.
FINNUM:         LDX     USP
                STX     USSAV
                LDX     USP+1
                STX     USSAV+1
                CMP     #'^'
                BNE     NOTSCI
                CMP     (USP)
                BNE     NOTSCI
                INCW    USP
                CMP     (USP)
                BNE     NOTSCI
                INCW    USP
                CMP     (USP)
                BNE     NOTSCI
                INCW    USP
                LDA     USCH           ; four characters left for it?
                SEC
                SBC     #4
                BCC     NOTSCI
                STA     USCH           ; MOV CH,AL
                INC     USDH           ; E format
                INCW    USP
                BRA     ENDNUS
; NOTSCI (BIPRTU.ASM 243)
NOTSCI:         LDA     USSAV          ; BX back
                STA     USP
                LDA     USSAV+1
                STA     USP+1
; ENDNUS (BIPRTU.ASM 245): a leading "+" is a place; without one, a "-" or
; "+" after the field is its sign.
ENDNUS:         DECW    USP
                LDA     USDH
                INC     USDL
                AND     #8
                BNE     ENDNUM
                DEC     USDL
                LDA     USCH           ; the format's end?
                BEQ     ENDNUM
                LDA     (USP)
                SEC
                SBC     #'-'           ; a trailing minus
                BEQ     SGNTRL
                CMP     #<('+'-'-')    ; a trailing plus
                BNE     ENDNUM
                LDA     #8             ; "+" for a positive sign
SGNTRL:         CLC
                ADC     #4             ; the sign after
                CLC
                ADC     USDH
                STA     USDH
                DEC     USCH           ; the sign counted
; ENDNUM (BIPRTU.ASM 270): a value for the field, unless the list ended.
ENDNUM:         LDA     USEND          ; POPF
                BEQ     :+
                JMP     FLDFIN
:               JSR     FRMEVL         ; the value
                LDA     USDL           ; at most 24 digits
                CLC
                ADC     USCL
                CMP     #25
                BCC     :+
                JMP     FCERR
:               LDA     USDL           ; CX: the places before and after the
                STA     RCH            ; point
                LDA     USCL
                STA     RCL
                LDA     USDH           ; AL: the flags, with the USING bit
                ORA     #128
                FJSR    PUFOUT         ; the number in FBUFF, FBPTR at it
                JSR     DSOUT          ; printed
; FNSTRF (BIPRTU.ASM 311): after a field's value, ";" or "," and another
; value, or the end of the list (and of the line, C).
FNSTRF:         DECW    TXTPTR         ; the terminator
                JSR     CHRGTR
                SEC                    ; a line's end wanted
                BEQ     CRDNUS         ; the statement's end: the list's
                STA     USFLG          ; a value printed (A isn't 0)
                CMP     #';'
                BEQ     SEMUSN
                CMP     #','
                BEQ     SEMUSN
                JMP     SNERR
; SEMUSN (BIPRTU.ASM 327)
SEMUSN:         JSR     CHRGTR         ; Z: no value after it
; CRDNUS (BIPRTU.ASM 328): PUSHF: Z, the list ended, and C. Then BX again
; from the descriptor and the characters left (FRMEVL may have moved the
; format's characters in a collection).
CRDNUS:         STZ     USEND          ; (STZ keeps the flags)
                STZ     USCR
                BNE     @z
                DEC     USEND
@z:             BCC     @c
                DEC     USCR
@c:             LDA     USDSC
                STA     RBL
                LDA     USDSC+1
                STA     RBH
                LDA     (RBL)          ; the length, less the characters left
                SEC
                SBC     USCH
                LDY     #1             ; plus the characters' address
                CLC
                ADC     (RBL),Y
                STA     USP
                INY
                LDA     (RBL),Y
                ADC     #0
                STA     USP+1
; CHKUSI (BIPRTU.ASM 353)
CHKUSI:         LDA     USCH           ; characters left: scan on
                BEQ     FINUSI
                JMP     PRCCHR
; REUSIN (BIPRTU.ASM 358): the format's last character, printed.
REUSIN:         JSR     PLSPRT
                JSR     OUTDO
; FINUSI (BIPRTU.ASM 360): the format again if the list goes on.
FINUSI:         LDA     USEND          ; POPF
                BNE     FLDFIN
                JMP     REUSST
; FLDFIN (BIPRTU.ASM 366): the line's end if no ";" or "," ended the list,
; the format's temporary freed, PTRFIL back to the console.
FLDFIN:         LDA     USCR
                BEQ     :+
                JSR     CRDO
:               LDA     USDSC          ; XTHL: BX the descriptor
                STA     RBL
                LDA     USDSC+1
                STA     RBH
                JSR     FRETM2
                JMP     FINPRT         ; a tail call: its RTS is PRINT's

; LITCHR (BIPRTU.ASM 384): "_": the next character printed.
LITCHR:         JSR     PLSPRT
                DEC     USCH
                LDA     (USP)
                INCW    USP
                JSR     OUTDO
                JMP     CHKUSI

; VARSTR (BIPRTU.ASM 395): "&", the string's length (CL 0).
VARSTR:         STZ     USCL
                BRA     ISSTR1
; SMSTRF (BIPRTU.ASM 403): "!", one character.
SMSTRF:         LDA     #1
                STA     USCL
                BRA     ISSTR1
; ISSTRF (BIPRTU.ASM 405): "\ \", BGSTRF's place dropped.
ISSTRF:         PLA                    ; POP AX
                PLA
; ISSTR1 (BIPRTU.ASM 407): a string for the field, unless the list ended; CL
; of it (LEFTUS's LEFT$), or all of it for "&", then spaces to CL.
ISSTR1:         DEC     USCH
                JSR     PLSPRT
                LDA     USEND          ; POPF
                BEQ     :+
                JMP     FLDFIN
:               JSR     FRMEVL
                JSR     CHKSTR
                LDA     FACLO          ; BX: its descriptor
                STA     RBL
                LDA     FACLO+1
                STA     RBH
                LDA     USCL           ; CH: the width; CL 0 for LEFT$
                STA     RCH
                STZ     RCL
                PHA                    ; PUSH AX: the width, for the spaces
                CMP     #0
                BEQ     :+             ; "&": all of it
                JSR     LEFTUS
:               JSR     STRPRT
                LDA     FACLO          ; BX: the string printed
                STA     RBL
                LDA     FACLO+1
                STA     RBH
                PLA                    ; "&": no spaces
                BNE     :+
                JMP     FNSTRF
:               SEC                    ; the width less its length, in X
                SBC     (RBL)
                TAX
                INX                    ; (one more: the loop counts down first)
; UPRTSP (BIPRTU.ASM 438)
UPRTSP:         DEX
                BNE     :+
                JMP     FNSTRF
:               LDA     #' '
                JSR     OUTDO          ; (keeps X)
                BRA     UPRTSP

; --- PLSPRT (BIPRTU.ASM 458) ------------------------------------------------------------

; A "+" (DH not 0: the only flag that can be on here) printed. Keeps A.
PLSPRT:         PHA
                LDA     USDH
                BEQ     :+
                LDA     #'+'
                JSR     OUTDO
:               PLA
                RTS

.segment "BRAM"

USDSC:          .res    2              ; the format's descriptor (the 8086's stack)
USFLG:          .res    1              ; GWDATA.ASM's USFLG: a value printed this pass
USEND:          .res    1              ; the list ended (the pushed ZF): $FF
USCR:           .res    1              ; and with no ";" or "," (the pushed CF): $FF
USSAV:          .res    2              ; FINNUM's BX, kept (the 8086's DX)

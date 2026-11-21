; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; MACLNG.ASM, the macro language's driver, translated, in block P3 (hw/oodos.md,
; "The plan, phase 7": 7e2, U4; the owner's answer 1: DRAW's parsing in P3):
;
;   MACLNG    A/X a command table (p3/draw.s's DRWTAB): the string formula
;             at TXTPTR evaluated, and its commands run, each its routine
;             called with DX its number (1 if none) and C set if one was
;             given; X's strings in turn
;   MCLXEQ    X name; or X with VARPTR$: that string run, then the rest
;   FETCHR    the next character, spaces passed over, lower case made upper
;             (C clear at the end); FETCHZ the same, the end Illegal
;             function call; DECFET one back
;   VALSCN    a number: digits, signs, =name; or VARPTR$ (VALSC3 its first
;             character in A, VALSC2 the digits); VARGET =name;
;   SCNVAR    a name to ";" (or VARPTR$'s 3 bytes): its value in the FAC
;
; What the 8086 pushes for each string (its address and length) goes on the
; 6502's stack, 3 bytes a level, an address of 0 the end, as the source's
; dummy entry; MCLXEQ checks for room as the source's GETSTK (the BASIC stack
; and the 6502's). VALSCN counts its "-" signs (each the source's NEGD,
; negating once more) and negates at the end. MCLOUT keeps the first string's
; address, for DRAW's P (p3/draw.s: its queue kept below it, OO-1). PLAY (7f)
; takes its own copy, this file assembled again into P7 (p7/maclng.s); MCLC2,
; the command's character x 2, is exported for PLAY's notes (PLYNOT).

.include "gwbasic.inc"

.export MACLNG, FETCHR, FETCHZ, DECFET, VALSCN, VALSC3, VALSC2, NEGD, MCLOUT, MCLC2
.import FRMEVL, FRESTR, GETBCD, FRCINT, FCERR, PTRGET, RETVAR, GETSTK, ISLET2, BUF

.code

; --- MACLNG (MACLNG.ASM 32) -------------------------------------------------------------------

MACLNG:         STA     MCLTAB
                STX     MCLTAB + 1
                STZ     MCLOUT         ; no string yet
                STZ     MCLOUT + 1
                JSR     FRMEVL         ; the string
                LDA     #0             ; the end: an address of 0
                PHA
                PHA
                PHA
MCLNEW:         JSR     FRESTR
                JSR     GETBCD         ; DH its length, CX its address
                LDA     RCL
                ORA     RCH
                BEQ     MCLOOP         ; no address: nothing pushed
                LDA     RDH
                BEQ     MCLOOP         ;   nor a null string
                LDA     MCLOUT         ; (OO-1) the first string's address
                ORA     MCLOUT + 1
                BNE     :+
                LDA     RCL
                STA     MCLOUT
                LDA     RCH
                STA     MCLOUT + 1
:               LDA     RCH
                PHA
                LDA     RCL
                PHA
                LDA     RDH
                PHA
MCLOOP:         PLA                    ; a string: its length,
                STA     MCLLEN
                PLA                    ;   its address
                STA     MCLPTR
                PLA
                STA     MCLPTR + 1
                ORA     MCLPTR
                BNE     MCLSCN
                RTS                    ; the end
MCLSCN:         JSR     FETCHR         ; a command
                BCC     MCLOOP         ; the string's end: the one before
                ASL     A              ; the character x 2 (bit 7 off)
                STA     MCLC2
                LDA     MCLTAB
                STA     PTR
                LDA     MCLTAB + 1
                STA     PTR + 1
MSCNLP:         LDA     (PTR)          ; the table's
                ASL     A
                BNE     :+
                JMP     FCERR          ; its end: no such command
:               CMP     MCLC2
                BEQ     MISCMD
                CLC
                LDA     PTR
                ADC     #3
                STA     PTR
                BCC     MSCNLP
                INC     PTR + 1
                BRA     MSCNLP
MISCMD:         LDY     #1             ; its routine
                LDA     (PTR),Y
                STA     MCLJMP
                INY
                LDA     (PTR),Y
                STA     MCLJMP + 1
                LDA     (PTR)
                BMI     :+
                CLC                    ; no number: C clear
                BRA     MNOARG
:               JSR     FETCHR         ; a number? 1 if none
                LDX     #1
                STX     RDL
                STZ     RDH
                BCC     VSNAR0         ; the end: none
                JSR     ISLET2
                BCC     VSNARG         ; a letter: the next command
                JSR     VALSC3
                SEC                    ; given
                BRA     MNOARG
VSNARG:         JSR     DECFET
VSNAR0:         CLC
MNOARG:         JSR     MCLGO          ; the command, back to the next
                JMP     MCLSCN
MCLGO:          JMP     (MCLJMP)

; FETCHR (MACLNG.ASM 117): A the next character, C set; C clear at the end.
FETCHR:         LDA     MCLLEN
                BEQ     @end
                DEC     MCLLEN
                LDA     MCLPTR
                STA     PTR
                LDA     MCLPTR + 1
                STA     PTR + 1
                INC     MCLPTR
                BNE     :+
                INC     MCLPTR + 1
:               LDA     (PTR)
                CMP     #' '           ; spaces passed over
                BEQ     FETCHR
                CMP     #96            ; lower case made upper
                BCC     :+
                SBC     #32
:               SEC
                RTS
@end:           CLC
                RTS
; FETCHZ: the end Illegal function call.
FETCHZ:         JSR     FETCHR
                BCC     MCLFC
                RTS
MCLFC:          JMP     FCERR
; DECFET: one character back.
DECFET:         INC     MCLLEN
                LDA     MCLPTR
                BNE     :+
                DEC     MCLPTR + 1
:               DEC     MCLPTR
                RTS

; --- VALSCN (MACLNG.ASM 144) ------------------------------------------------------------------

; VALSCN: DX a number. VALSC3: A its first character.
VALSCN:         JSR     FETCHZ
VALSC3:         STZ     MCLNEG
@sign:          CMP     #'='           ; =name;
                BNE     :+
                JSR     VARGET
                BRA     VALNEG
:               CMP     #'+'           ; "+" passed over
                BEQ     @next
                CMP     #'-'           ; "-": negated (once more) at the end
                BNE     VALSC2
                LDA     MCLNEG
                EOR     #1
                STA     MCLNEG
@next:          JSR     FETCHZ
                BRA     @sign
VALSC2:         STZ     RDL            ; the digits
                STZ     RDH
NUMLOP:         CMP     #','           ; a comma: back over it, done
                BEQ     VALBCK
                CMP     #';'           ; a ";" ends it
                BEQ     VALNEG
                CMP     #'9' + 1       ; not a digit: back over it, done
                BCS     VALBCK
                CMP     #'0'
                BCC     VALBCK
                SBC     #'0'           ; DX = DX x 10 + the digit, overflow
                PHA                    ; Illegal function call
                LDA     RDL
                STA     T0
                LDA     RDH
                STA     T1
                ASL     RDL            ; x 2
                ROL     RDH
                BCS     MCLFC
                ASL     RDL            ; x 4
                ROL     RDH
                BCS     MCLFC
                CLC                    ; x 5
                LDA     RDL
                ADC     T0
                STA     RDL
                LDA     RDH
                ADC     T1
                STA     RDH
                BCS     MCLFC
                ASL     RDL            ; x 10
                ROL     RDH
                BCS     MCLFC
                PLA
                CLC
                ADC     RDL
                STA     RDL
                BCC     :+
                INC     RDH
                JEQ     MCLFC
                JSR     FETCHR
                BCS     NUMLOP
                BRA     VALNEG           ; the string's end
VALBCK:         JSR     DECFET
VALNEG:           LDA     MCLNEG
                BEQ     :+
NEGD:           SEC                    ; NEGD: DX negated
                LDA     #0
                SBC     RDL
                STA     RDL
                LDA     #0
                SBC     RDH
                STA     RDH
:               RTS

; VARGET: =name; an integer in DX.
VARGET:         JSR     SCNVAR
                JSR     FRCINT
                LDA     RBL
                STA     RDL
                LDA     RBH
                STA     RDH
                RTS

; SCNVAR (MACLNG.ASM 185): a name to ";", or VARPTR$'s string (its type,
; under 9, and the address: 3 bytes), its value in the FAC (ISVAR's,
; PTRGET's from BUF: TXTPTR kept).
SCNVAR:         LDA     MCLPTR
                STA     PTR
                LDA     MCLPTR + 1
                STA     PTR + 1
                LDA     (PTR)
                CMP     #9
                BCS     SCNVR2
                LDA     MCLLEN
                SEC
                SBC     #3
                BCC     SCNVR2
                STA     MCLLEN
                LDA     (PTR)          ; VARPTR$'s: the type, the address
                STA     VALTYP
                LDY     #1
                LDA     (PTR),Y
                TAX
                INY
                LDA     (PTR),Y
                STX     PTR
                STA     PTR + 1
                CLC
                LDA     MCLPTR
                ADC     #3
                STA     MCLPTR
                BCC     :+
                INC     MCLPTR + 1
:               JMP     RETVAR
SCNVR2:         JSR     FETCHZ         ; a letter first
                LDX     #0
                PHA
                JSR     ISLET2
                PLA
                JCS     MCLFC
SCNVLP:         STA     BUF,X          ; to BUF, to the ";"
                INX
                CMP     #';'
                BEQ     SCNV2
                CPX     #40            ; 40 at most
                JCS     MCLFC
                JSR     FETCHZ
                BRA     SCNVLP
SCNV2:          LDA     TXTPTR+1       ; ISVAR on BUF
                PHA
                LDA     TXTPTR
                PHA
                LDA     #<BUF
                STA     TXTPTR
                LDA     #>BUF
                STA     TXTPTR + 1
                JSR     PTRGET
                PLA
                STA     TXTPTR
                PLA
                STA     TXTPTR + 1
                JMP     RETVAR

; MCLXEQ (MACLNG.ASM 236): the X command: the string's value, the place in
; this one pushed (the return to MCLSCN dropped first), room checked
; (GETSTK), then the string run as a new one.
MCLXEQ:         JSR     SCNVAR
                PLA                    ; MCLSCN's return
                PLA
                LDA     MCLPTR + 1
                PHA
                LDA     MCLPTR
                PHA
                LDA     MCLLEN
                PHA
                LDA     #2
                JSR     GETSTK
                JMP     MCLNEW
.export MCLXEQ

.segment "BRAM"
MCLTAB:         .res    2              ; the command table
MCLPTR:         .res    2              ; the string's next character
MCLLEN:         .res    1              ;   and those left
MCLC2:          .res    1              ; the command's character x 2
MCLJMP:         .res    2              ; its routine
MCLNEG:         .res    1              ; VALSCN: the "-" signs' count, odd: negated
MCLOUT:         .res    2              ; the first string's address (OO-1: DRAW's P)

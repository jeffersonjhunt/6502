; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; BISTRS.ASM's STRING$ and MID$ as a statement, in block P3 (hw/oodos.md,
; "The plan, phase 7"), each reached by its resident stub (res/far3.s):
;
;   STRNGP    STRING$(n, code) and STRING$(n, string$): n of one character,
;             through the resident SPACE$'s code (SPACE2)
;   LHSMIDP   MID$(a$, n[, m]) = b$: b$'s characters into a$ from its n-th,
;             at most m of them and never past a$'s end; a$ copied into the
;             string space first if it is a literal in the program
;
; What the 8086 keeps on its stack across a formula (STRING$'s count, MID$'s
; descriptor and numbers) goes on the BASIC stack: the formula may reach P3
; itself. The far call rule: STRING$ ends in SPACE2, a tail call into
; resident code that returns to EVAL's caller (the 8086's FINBCK pops the
; return into STRING$ for the same end); MID$ returns by RTS.

.include "gwbasic.inc"

;@entry STRNGP
;@entry LHSMIDP
.export STRNGP, LHSMIDP
.import CHRGTR, SYNCHR, GETBYT, CONINT, FRMEVL, FRMEQL, GETYP, ASC2, SPACE2
.import PTRGET, CHKSTR, STRCPY, DSCTMP, FRESTR, MIDRST, FCERR
.import SPUSH, SPOP, SPUSHA, SPOPA

.code

; --- STRING$ (BISTRS.ASM 659) ------------------------------------------------------------

; From EVAL, TXTPTR at STRING$: "(", the count (GETBYT: 0..255), ",", a
; formula, ")". A string's first character (ASC2: a null string is Illegal
; function call), or a number 0..255 (CONINT).
STRNGP:         JSR     CHRGTR
                JSR     SYNCHR
                .byte   '('
                JSR     GETBYT         ; the length
                LDA     RDL
                JSR     SPUSHA
                JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL         ; the character
                JSR     SYNCHR
                .byte   ')'
                JSR     GETYP
                BEQ     STRSTR         ; a string
                JSR     CONINT         ; its code
                BRA     CALSPA
; STRSTR (BISTRS.ASM 679)
STRSTR:         JSR     ASC2
; CALSPA (BISTRS.ASM 680): the count back in DL, the character in A.
CALSPA:         PHA
                JSR     SPOPA
                STA     RDL
                PLA
                JMP     SPACE2

; --- LHSMID (BISTRS.ASM 974) -------------------------------------------------------------

; From ISMID, TXTPTR at the "(" after MID$. a$'s descriptor (PTRGET's PTR)
; on the BASIC stack. If a$'s text is above STREND (the string space) or at
; or below TXTTAB (a buffer: a FIELDed string's, 7c), it stays; else it is a
; literal in the program, copied (STRCPY) and the copy's descriptor put in
; a$'s (VMOVE, 3 bytes). Then n (GETBYT; 0 is Illegal function call), m
; (MIDRST: 255 if there is none; and ")"), "=" and b$, whose temporary is
; freed (FRESTR: its text stays until something is allocated).
LHSMIDP:        JSR     SYNCHR
                .byte   '('
                JSR     PTRGET         ; a string variable
                JSR     CHKSTR
                LDX     #PTR           ; its descriptor
                LDA     #2
                JSR     SPUSH
                LDY     #1             ; DX: its text
                LDA     (PTR),Y
                STA     RDL
                INY
                LDA     (PTR),Y
                STA     RDH
                LDA     STREND         ; in the string space already?
                CMP     RDL
                LDA     STREND+1
                SBC     RDH
                BCC     NCPMID
                LDA     TXTTAB         ; a FIELDed string's?
                CMP     RDL
                LDA     TXTTAB+1
                SBC     RDH
                BCS     NCPMID
; NCP1 (BISTRS.ASM 994): the literal copied into the string space.
NCP1:           LDA     PTR
                STA     RBL
                LDA     PTR+1
                STA     RBH
                JSR     STRCPY         ; DX: DSCTMP
                LDA     (SSP)          ; the descriptor, still on the stack
                STA     PTR
                LDY     #1
                LDA     (SSP),Y
                STA     PTR+1
                LDY     #2             ; VMOVE: the copy's descriptor into it
@m:             LDA     DSCTMP,Y
                STA     (PTR),Y
                DEY
                BPL     @m
; NCPMID (BISTRS.ASM 1000)
NCPMID:         JSR     SYNCHR
                .byte   ','
                JSR     GETBYT         ; n, the offset
                LDA     RDL            ; not zero (the 8086's OR AL,AL)
                BNE     :+
                JMP     FCERR
:               JSR     SPUSHA
                LDA     (TXTPTR)       ; the current character
                JSR     MIDRST         ; DL: m, or 255; and ")"
                LDA     RDL
                JSR     SPUSHA
                JSR     FRMEQL         ; "=" and b$
                JSR     FRESTR         ; BX: its descriptor, freed
                LDA     RBL            ; DX: b$'s descriptor
                STA     RDL
                LDA     RBH
                STA     RDH
                JSR     SPOPA          ; CL: m
                STA     RCL
                JSR     SPOPA          ; CH: n
                STA     RCH
                LDX     #RBL           ; BX: a$'s descriptor
                LDA     #2
                JSR     SPOP
                LDA     RCL            ; m 0: nothing
                BNE     :+
                RTS
:               LDA     (RBL)          ; a$'s length less n
                SEC
                SBC     RCH
                BCS     :+
                JMP     FCERR          ; n past a$'s end
:               INC     A              ; the characters from n on
                CMP     RCL            ; more than m: m
                BCC     BIGLEN
                LDA     RCL
; BIGLEN (BISTRS.ASM 1040): BX at a$'s n-th character, CH the count there.
BIGLEN:         PHA
                LDA     RCH            ; CX: the offset, n - 1
                DEC     A
                LDY     #1
                CLC
                ADC     (RBL),Y
                TAX
                INY
                LDA     (RBL),Y
                ADC     #0
                STX     RBL
                STA     RBH
                PLA
                STA     RCH            ; B: a$'s count
                LDA     (RDL)          ; C: b$'s length
                STA     RCL
                LDY     #1             ; DX: b$'s text
                LDA     (RDL),Y
                TAX
                INY
                LDA     (RDL),Y
                STX     RDL
                STA     RDH
                LDA     RCL            ; b$ null: done
                BNE     MIDSLP
                RTS
; MID$LP (BISTRS.ASM 1072): to the end of b$ or of a$'s count.
MIDSLP:         LDA     (RDL)
                STA     (RBL)
                INCW    RDL
                INCW    RBL
                DEC     RCL
                BNE     :+
                RTS
:               DEC     RCH
                BNE     MIDSLP
                RTS

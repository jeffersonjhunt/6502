; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM, translated: the parts the spike measures (README.md):
;
;   NEWSTT GONE        the statement fetcher and dispatch
;   CHRGTR CHRCON      the next character, and embedded constants
;   CONFC1             an embedded constant to the FAC
;   FNDLIN LOOP        find a line
;   LINGET             read a line number
;   GOTO               with the line number turned into a pointer (CONCH2)
;   DATA REM ELSES     skip a statement or the line
;   LET                (numbers; a string's copy is a stub)
;   IFS                IF ... THEN ... ELSE
;
; Statements dispatch as in the 8086: NEWSTT is pushed as the return
; address, then the statement's (from STMDSP, less one) for CHRGTR's RTS to
; go to, so a statement starts with its first character in A and CHRGTR's
; flags: C set for a digit, Z for the end (":" or zero).

.include "gwbasic.inc"

.export NEWSTT, GONE, GONE2, CHRGTR, CHRGT2, CHRCON, CONFC1, FNDLIN, LINGET
.export GOTO, DATA, REM, ELSES, LET, IFS, CONCH2, CONFAC
.import STMDSP, NUMCON, OUTDO, LINPRT, SNERR, ERROR, PRGEND, NEWSTX, ISMID
.import GOTRP, CHKINT, INEG2, SYNCHR, FRMEVL, PTRGET, DOCNVF, VMOVE, VSIGN
.import LETSTR

ERRUS           = 8                    ; Undefined line number

.segment "GWMAIN"

; --- NEWSTT --------------------------------------------------------------------

GORET:          RTS
GONCHR:         JSR     CHRCON
                BRA     GONE0
GISMD:          JSR     NEWSTX         ; an extended statement, or MID$ =
                JMP     ISMID
GONLET:         JMP     LET

NEWSTT:         LDA     ONGSBF         ; an event's GOSUB?
                BEQ     NEWST1
                JSR     GOTRP
NEWST1:         TSX                    ; SAVSTK: both stacks
                STX     SAVSTK
                LDA     SSP
                STA     SAVSTK+1
                LDA     SSP+1
                STA     SAVSTK+2
                LDA     TXTPTR
                STA     SAVTXT
                LDA     TXTPTR+1
                STA     SAVTXT+1
                JSR     CHKINT         ; Ctrl-C?
GONGET:         LDA     (TXTPTR)       ; what ended the last statement
                CMP     #':'
                BEQ     GONE
                JMP     GONZER
GONE:           INCW    TXTPTR         ; CHRGET's work, inline
                LDA     (TXTPTR)
                CMP     #':'
                BCC     GONCHR
GONE0:          TAX
                PUSHRET NEWSTT
                TXA
GONE3:          BEQ     GORET          ; a terminator: try again
                CMP     #':'
                BEQ     GORET
GONE2:          SEC                    ; a statement token, or LET
                SBC     #END_TK
                BCC     GONLET
                CMP     #NUMCMD
                BCS     GISMD
                ASL     A
                TAX
                LDA     STMDSP+1,X
                PHA
                LDA     STMDSP,X
                PHA
                ; falls into CHRGTR, whose RTS starts the statement

; --- CHRGTR ----------------------------------------------------------------------

; The next character: spaces skipped, C set for a digit, Z for ":" or zero.
; An embedded constant is read into CONLO and returned as its token, with
; TXTPTR left at NUMCON so the next CHRGTR comes back to the real text.
CHRGTR:         INCW    TXTPTR
CHRGT2:         LDA     (TXTPTR)
                CMP     #':'
                BCC     CHRCON
                CLC
                RTS
CHRCON:         CMP     #' '
                BEQ     CHRGTR
                BCC     LOWCHR
                CMP     #'0'           ; C for a digit
                INC     A              ; and Z clear
                DEC     A
                RTS
NOTCON:         CMP     #9             ; tab and line feed are eaten
                BCC     NOTLFT
                JMP     CHRGTR
NOTLFT:         CMP     #'0'
                INC     A
                DEC     A
                RTS
LOWCHR:         ORA     #0             ; zero: the end of the line
                BNE     :+
                RTS
:               CMP     #OCTCON
                BCC     NOTCON
                CMP     #CONCON        ; re-scanning a constant?
                BNE     NTRSCC
                LDA     CONSAV
                CLC
                RTS
NTRSCC:         CMP     #CONCN2        ; past one?
                BEQ     CONSCN
NTRSC2:         PHA                    ; the token, to return
                INCW    TXTPTR
                STA     CONSAV
                SEC
                SBC     #INTCON
                BCS     MAKTKN         ; a 2, 4 or 8 byte number
                SEC
                SBC     #<(ONECON-INTCON)
                BCS     ONEI           ; 0-9 in the token itself
                CMP     #<(IN2CON-ONECON)
                BNE     FRCINC         ; 2 bytes
                LDA     (TXTPTR)       ; 1 byte
                INCW    TXTPTR
ONEI:           LDX     TXTPTR
                STX     CONTXT
                LDX     TXTPTR+1
                STX     CONTXT+1
                LDX     #0
ONEI2:          STA     CONLO
                STX     CONLO+1
                LDA     #VT_INT
                STA     CONTYP
                LDA     #<NUMCON
                STA     TXTPTR
                LDA     #>NUMCON
                STA     TXTPTR+1
                PLA                    ; the token; C and Z clear
                CLC
                RTS
FRCINC:         LDY     #1
                LDA     (TXTPTR),Y
                TAX
                LDA     (TXTPTR)
                PHA
                INCW    TXTPTR
                INCW    TXTPTR
                LDA     TXTPTR
                STA     CONTXT
                LDA     TXTPTR+1
                STA     CONTXT+1
                PLA
                BRA     ONEI2

CONFAC:         JSR     CONFC1         ; the constant to the FAC
CONSCN:         LDA     CONTXT         ; and on past it
                STA     TXTPTR
                LDA     CONTXT+1
                STA     TXTPTR+1
                JMP     CHRGT2

MAKTKN:         INC     A              ; VALTYP: 0, 1, 3 -> 2, 4, 8
                ASL     A
                STA     CONTYP
                LDX     #0
@move:          LDA     (TXTPTR)
                STA     CONLO,X
                INCW    TXTPTR
                INX
                CPX     CONTYP
                BNE     @move
FININ1:         LDA     TXTPTR
                STA     CONTXT
                LDA     TXTPTR+1
                STA     CONTXT+1
                PLA
                LDX     #<NUMCON
                STX     TXTPTR
                LDX     #>NUMCON
                STX     TXTPTR+1
                CLC
                ORA     #0
                RTS

; GONZER, GONE4: the end of a line; the next line's number to CURLIN.
GOPRGD:         JMP     PRGEND         ; the end of the program
GOSNER:         JMP     SNERR
GONZER:         CMP     #0             ; the end of a line must be a zero
                BNE     GOSNER
                INCW    TXTPTR
GONE4:          LDA     (TXTPTR)       ; the link: zero at the end of the program
                INCW    TXTPTR
                ORA     (TXTPTR)
                BEQ     GOPRGD
                INCW    TXTPTR         ; the line number to CURLIN
                LDA     (TXTPTR)
                STA     CURLIN
                INCW    TXTPTR
                LDA     (TXTPTR)
                STA     CURLIN+1
                LDA     TRCFLG         ; TRON: "[line]"
                BNE     :+
                JMP     GONE
:
                LDA     #'['
                JSR     OUTDO
                LDA     CURLIN
                STA     RBL
                LDA     CURLIN+1
                STA     RBH
                JSR     LINPRT
                LDA     #']'
                JSR     OUTDO
                JMP     GONE


; CONFC1: the saved constant to the FAC. A line number, or a line pointer's
; line number, is floated as unsigned.
CONFC1:         LDA     CONSAV
                CMP     #LINCON+1
                BCS     NTLINE
                CMP     #PTRCON
                BCC     NTLINE
                BNE     @lin
                CLC                    ; a pointer: its line's number
                LDA     CONLO
                ADC     #3
                STA     PTR
                LDA     CONLO+1
                ADC     #0
                STA     PTR+1
                LDA     (PTR)
                STA     RBL
                LDY     #1
                LDA     (PTR),Y
                STA     RBH
                BRA     FLTLIN
@lin:           LDA     CONLO
                STA     RBL
                LDA     CONLO+1
                STA     RBH
FLTLIN:         JMP     INEG2
NTLINE:         LDA     CONTYP
                STA     VALTYP
                CMP     #VT_DBL
                BEQ     CONFDB
                LDX     #3
@m:             LDA     CONLO,X
                STA     FACLO,X
                DEX
                BPL     @m
                RTS
CONFDB:         LDX     #7
@m:             LDA     CONLO,X
                STA     DFACL,X
                DEX
                BPL     @m
                RTS

; --- FNDLIN ---------------------------------------------------------------------

; Find line DX from the start (FNDLIN) or from the line at PTR2 (LOOP).
;   Z set, C clear: not found, nothing beyond; CX at the end's two zeros.
;   Z and C set: found; CX at its link, PTR2 at the next line's.
;   Z and C clear: not found; CX at the first line beyond it.
FNDLIN:         LDA     TXTTAB
                STA     PTR2
                LDA     TXTTAB+1
                STA     PTR2+1
LOOP:           LDA     PTR2
                STA     RCL
                LDA     PTR2+1
                STA     RCH
                LDY     #0
                LDA     (PTR2),Y
                INY
                ORA     (PTR2),Y
                BNE     @line
                CLC
                RTS
@line:          LDY     #3             ; its number against DX
                LDA     (PTR2),Y
                CMP     RDH
                BNE     @ne
                DEY
                LDA     (PTR2),Y
                CMP     RDL
                BNE     @ne
                JSR     @link
                SEC
                LDA     #0
                RTS
@ne:            PHP
                JSR     @link
                PLP
                BCC     LOOP           ; still below it
                LDA     #1
                CLC
                RTS
@link:          LDY     #0             ; PTR2 = the link
                LDA     (PTR2),Y
                TAX
                INY
                LDA     (PTR2),Y
                STA     PTR2+1
                STX     PTR2
                RTS

; --- LINGET --------------------------------------------------------------------

; A line number from the text to DX: an embedded one, or digits (0-65529);
; returns at the character after it.
LINGET:         DECW    TXTPTR
LINGT2:         JSR     CHRGTR
                CMP     #LINCON
                BEQ     LINGT3
                CMP     #PTRCON
LINGT3:         PHP
                LDA     CONLO
                STA     RDL
                LDA     CONLO+1
                STA     RDH
                PLP
                BNE     :+
                JMP     CHRGTR
:               STZ     CONSAV         ; no constant: GOTO2 mustn't make a pointer
                STZ     RDL
                STZ     RDH
                DECW    TXTPTR
MORLIN:         JSR     CHRGTR
                BCS     :+
                RTS
:               TAX
                LDA     #<6552         ; too big already? leave the digit unread
                CMP     RDL
                LDA     #>6552
                SBC     RDH
                BCC     POPHSR
                LDA     RDL            ; DX = DX * 10 + the digit
                STA     T0
                LDA     RDH
                STA     T1
                ASL     RDL
                ROL     RDH
                ASL     RDL
                ROL     RDH
                CLC
                LDA     RDL
                ADC     T0
                STA     RDL
                LDA     RDH
                ADC     T1
                STA     RDH
                ASL     RDL
                ROL     RDH
                TXA
                SEC
                SBC     #'0'
                CLC
                ADC     RDL
                STA     RDL
                BCC     MORLIN
                INC     RDH
                BRA     MORLIN
POPHSR:         TXA
                SEC
                RTS

; --- GOTO ------------------------------------------------------------------------

; Find the line and continue there. The first time, the line number in the
; text becomes a pointer to the line (CONCH2), so the next GOTO from here
; doesn't search.
GOTO:           JSR     LINGET
GOTO2:          LDA     CONSAV
                CMP     #PTRCON
                BNE     @num
                LDA     RDL            ; a pointer: there already
                STA     TXTPTR
                LDA     RDH
                STA     TXTPTR+1
                RTS
@num:           CMP     #LINCON
                BEQ     :+
                JMP     SNERR
:               LDA     CONTXT+1       ; the text after the constant
                PHA
                LDA     CONTXT
                PHA
                JSR     REM            ; to the end of this line
                INCW    TXTPTR         ; the next line's link
                LDA     TXTPTR
                STA     PTR2
                LDA     TXTPTR+1
                STA     PTR2+1
                LDA     CURLIN         ; ahead of here: search from here
                CMP     RDL
                LDA     CURLIN+1
                SBC     RDH
                BCS     @start
                JSR     LOOP
                BCS     @found
@start:         JSR     FNDLIN
                BCC     USERR
@found:         LDA     RCL            ; CX at the zero before the line
                BNE     :+
                DEC     RCH
:               DEC     RCL
                LDA     #PTRCON
                STA     PTRFLG
                PLA
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                LDA     #PTRCON
                JSR     CONCH2         ; the line number becomes CX
                LDA     RCL
                STA     TXTPTR
                LDA     RCH
                STA     TXTPTR+1
                RTS
USERR:          LDA     #ERRUS
                JMP     ERROR

; CONCH2: overwrite the 3 bytes before TXTPTR with the token in A and CX.
CONCH2:         PHA
                SEC
                LDA     TXTPTR
                SBC     #3
                STA     PTR
                LDA     TXTPTR+1
                SBC     #0
                STA     PTR+1
                PLA
                STA     (PTR)
                LDY     #1
                LDA     RCL
                STA     (PTR),Y
                INY
                LDA     RCH
                STA     (PTR),Y
                RTS

; --- DATA, REM ------------------------------------------------------------------

; Skip to the end of the statement (DATA: ":" or zero, outside quotes) or of
; the line (REM, and an executed ELSE). DATA counts the IFs it passes in DH,
; for FALSIF. Keeps DX otherwise.
DATA:           LDA     #':'
                STA     RCL
                BRA     REMZER
ELSES:
REM:            STZ     RCL
REMZER:         STZ     RCH
EXCHQT:         LDA     RCL            ; a quote swaps the second terminator
                LDX     RCH
                STA     RCH
                STX     RCL
REMER:          DECW    TXTPTR
REMER1:         JSR     CHRGTR
                CMP     #0
                BEQ     @ret
                CMP     RCH
                BEQ     @ret
                INCW    TXTPTR
                CMP     #'"'
                BEQ     EXCHQT
                INC     A              ; $FF: a function token follows
                BEQ     REMER1
                CMP     #IF_TK+1
                BNE     REMER
                LDA     RCH            ; an IF, counted in DATA only
                BEQ     REMER
                INC     RDH
                BRA     REMER
@ret:           RTS

; --- LET ---------------------------------------------------------------------------

LET:            JSR     PTRGET         ; PTR: the variable, VALTYP its type
                JSR     SYNCHR
                .byte   EQULTK
                LDA     PTR
                STA     TEMP
                LDA     PTR+1
                STA     TEMP+1
REDINP:         LDA     PTR+1
                PHA
                LDA     PTR
                PHA
                LDA     VALTYP
                PHA
                JSR     FRMEVL
                PLA                    ; the variable's type
LETCN2:         STA     RCH
                PLA
                STA     PTR
                PLA
                STA     PTR+1
                LDA     RCH
INPCOM:         CMP     VALTYP
                BEQ     LETCN5
                JSR     DOCNVF         ; the value to the variable's type
LETCN4:         LDA     VALTYP
LETCN5:         CMP     #VT_STR
                BNE     COPNUM
                JMP     LETSTR
COPNUM:         JSR     VMOVE
                LDA     PTR            ; DX: the variable, for FOR
                STA     RDL
                LDA     PTR+1
                STA     RDH
                RTS

; --- IF ... THEN ... ELSE -----------------------------------------------------------

IFS:            JSR     FRMEVL
                LDA     (TXTPTR)
                CMP     #','
                BNE     :+
                JSR     CHRGTR
:               CMP     #GOTO_TK
                BEQ     OKGOTO
                JSR     SYNCHR
                .byte   THEN_TK
                DECW    TXTPTR
OKGOTO:         JSR     VSIGN
                BEQ     FALSIF
DOCOND:         JSR     CHRGTR         ; "THEN 100", "THEN statement"
                BNE     :+
                RTS
:               CMP     #LINCON
                BNE     :+
                JMP     GOTO
:               CMP     #PTRCON
                BEQ     :+
                JMP     GONE2
:               LDA     CONLO
                STA     TXTPTR
                LDA     CONLO+1
                STA     TXTPTR+1
                RTS
FALSIF:         LDA     #1             ; the ELSEs to see
                STA     RDH
SKPMRF:         JSR     DATA
                CMP     #0
                BNE     :+
                RTS
:               JSR     CHRGTR
                CMP     #ELSE_TK
                BNE     SKPMRF
                DEC     RDH
                BNE     SKPMRF
                BRA     DOCOND

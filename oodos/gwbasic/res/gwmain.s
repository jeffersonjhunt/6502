; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM, translated: the resident part of the interpreter's frame.
;
;   ERROR SNERR ...    the errors' entry: the stacks reset, the message (P2's
;                      ERRPRT), then READY; the xxERR labels the code jumps to
;   READY MAIN         "Ok", a line read (the OEM's PINLIN), then run at once
;                      (direct) or crunched into the program (P2's EDENT2);
;                      FINI clears and goes round again
;   BAKSP              back over spaces after a number
;   PRGEND             the end of the program, or of a direct line
;   RUN                RUN and RUN line (RUN "file" is 7c's)
;   NEWSTT GONE        the statement fetcher and dispatch
;   CHRGTR CHRCON      the next character, and embedded constants
;   CONFC1             an embedded constant to the FAC
;   FNDLIN LOOP        find a line
;   LINGET             read a line number
;   GOTO               with the line number turned into a pointer (CONCH2)
;   DATA REM ELSES     skip a statement or the line
;   LET                (a string's copy: LETSTR, res/bistrs.s)
;   IFS                IF ... THEN ... ELSE
;   DEFSTR DEFINT      DEFSTR DEFINT DEFSNG DEFDBL: DEFTBL, for PTRGET
;   DEFREA DEFDBL
;   INTIDX             a subscript: a formula, a positive integer
;
; Statements dispatch as in the 8086: NEWSTT is pushed as the return
; address, then the statement's (from STMDSP, less one) for CHRGTR's RTS to
; go to, so a statement starts with its first character in A and CHRGTR's
; flags: C set for a digit, Z for the end (":" or zero).
;
; The two stacks. SAVSTK holds both at a statement's start: the 6502's S,
; then the BASIC stack's SSP. An error (ERESET), a ^C (the OEM's ITSCTC)
; and RUN, NEW and CLEAR (STKINI, res/bimisc.s) set them back, which drops
; any far call's frame: so the main loop is never left inside a block. A
; block that would end at READY or MAIN (LIST, ERREDT) returns instead, and
; its resident caller goes on.

.include "gwbasic.inc"

.export NEWSTT, GONE, GONE2, CHRGTR, CHRGT2, CHRCON, CONFC1, FNDLIN, LINGET
.export GOTO, DATA, REM, ELSES, LET, IFS, CONCH2, CONFAC, LOOP
.export DEFSTR, DEFINT, DEFREA, DEFDBL, INTIDX, INTID2
.export ERROR, SNERR, DV0ERR, NFERR, UFERR, REERR, OVERR, MOERR, TMERR, FCERR
.export USERR, ERESET, READY, MAIN, FINI, BAKSP, PRGEND, RUN, ENDPRG, FTSITE
.export DOT, OLDLIN, OLDTXT, ERRTXT, ONEFLG, INITFG, REDDY, LIST, LLIST, DELETE
.import STMDSP, NUMCON, OUTDO, NEWSTX, ISMID, LRUN
.import GOTRP, CHKINT, INEG2, SYNCHR, FRMEVL, PTRGET, DOCNVF, VMOVE, VSIGN
.import LETSTR, ISLET2, GETIN2, DEFTBL, SPOP, STKERR, CRDONZ, FINLPT
.import STROUT, PINLIN, ENDCON, RUNC, CLEARC

ERRNF           = 1                    ; GWDATA.ASM's error numbers
ERRSN           = 2
ERRFC           = 5
ERROV           = 6
ERRUS           = 8
ERRDV0          = 11
ERRTM           = 13
ERRUF           = 18
ERRNR           = 19
ERRRE           = 20
ERRMO           = 22

.code

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
                FJSR    LINPRT
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

; --- DEFSTR, DEFINT, DEFSNG, DEFDBL (GWMAIN.ASM 1872) -------------------------------

; DEFxxx letter[-letter][,...]: those letters' default type, in DEFTBL.
DEFSTR:         LDA     #VT_STR
                BRA     DEFCON
DEFINT:         LDA     #VT_INT
                BRA     DEFCON
DEFREA:         LDA     #VT_SNG
                BRA     DEFCON
DEFDBL:         LDA     #VT_DBL
DEFCON:         STA     RDL            ; DL: the type
DEFCN1:         LDA     (TXTPTR)       ; ISLET: a letter, or Syntax error
                JSR     ISLET2
                BCS     DEFSN
                SEC
                SBC     #'A'
                STA     RCL            ; CL: the first
                STA     RCH            ; CH: the last, so far
                JSR     CHRGTR
                CMP     #MINUTK        ; a range?
                BNE     NOTRNG
                JSR     CHRGTR
                LDA     (TXTPTR)
                JSR     ISLET2
                BCS     DEFSN
                SEC
                SBC     #'A'
                STA     RCH
                JSR     CHRGTR
NOTRNG:         LDA     RCH            ; backwards is nonsense
                SEC
                SBC     RCL
                BCC     DEFSN
                TAY                    ; Y+1 letters from CL
                LDX     RCL
                LDA     RDL
LPDCHG:         STA     DEFTBL,X
                INX
                DEY
                BPL     LPDCHG
                LDA     (TXTPTR)       ; a comma: another
                CMP     #','
                BEQ     :+
                RTS
:               JSR     CHRGTR
                BRA     DEFCN1
DEFSN:            JMP     SNERR

; --- INTIDX (GWMAIN.ASM 1929) --------------------------------------------------------

; A formula past the character at TXTPTR, as an integer in DX: not negative,
; or Illegal function call. TXTPTR returns at the character after it.
INTIDX:         JSR     CHRGTR
INTID2:         JSR     GETIN2
                BPL     :+
                JMP     FCERR
:               RTS

; --- The errors (GWMAIN.ASM 423) ---------------------------------------------------------

; Each xxERR loads its number, as the 8086's MOV DL / skip chain does.
SNERR:          LDA     #ERRSN
                BRA     ERROR
DV0ERR:         LDA     #ERRDV0
                BRA     ERROR
NFERR:          LDA     #ERRNF
                BRA     ERROR
UFERR:          LDA     #ERRUF
                BRA     ERROR
REERR:          LDA     #ERRRE
                BRA     ERROR
OVERR:          LDA     #ERROV
                BRA     ERROR
MOERR:          LDA     #ERRMO
                BRA     ERROR
TMERR:          LDA     #ERRTM
                BRA     ERROR
FCERR:          LDA     #ERRFC         ; GWMAIN.ASM 1938 $FCERR

; ERROR (GWMAIN.ASM 455): error A (the 8086's DL). Before start-up is done
; (INITFG), P2's CMDERR says so and ends. Then ERL's line and the line for
; EDIT and LIST, the stacks back as the statement began, the line and text
; for CONT, and the message: "Syntax error in 20", on a line of its own.
; ON ERROR's trap (NTMDCN's) is 7b's: the error is always printed.
ERROR:
.ifdef FLAT
                LDX     ERRHOOK+1      ; the math harness takes it?
                BEQ     :+
                JMP     (ERRHOOK)
:
.endif
                STA     RDL
                LDA     INITFG
                BNE     INITOK
                FJSR    CMDERR         ; never returns
INITOK:         LDA     CURLIN         ; ERRLIN, for ERL
                STA     ERRLIN
                LDA     CURLIN+1
                STA     ERRLIN+1
                AND     CURLIN
                CMP     #$FF
                BEQ     ERRESM         ; a direct line: DOT stays
                LDA     CURLIN
                STA     DOT
                LDA     CURLIN+1
                STA     DOT+1
ERRESM:         LDA     #<(ERRMOR-1)   ; CX: where STKERR returns to
                STA     RCL
                LDA     #>(ERRMOR-1)
                STA     RCH
; ERESET: the stacks as SAVSTK has them, then STKERR, which returns to CX.
ERESET:         LDX     SAVSTK
                TXS
                LDA     SAVSTK+1
                STA     SSP
                LDA     SAVSTK+2
                STA     SSP+1
                JMP     STKERR
ERRMOR:         LDX     #RCL           ; the FNDFOR stopper STKERR pushed
                LDA     #2
                JSR     SPOP
                LDA     RDL
                STA     ERRFLG         ; for READY: a Syntax error edits its line
                LDA     SAVTXT         ; RESUME's (7b)
                STA     ERRTXT
                LDA     SAVTXT+1
                STA     ERRTXT+1
                LDA     ERRLIN
                AND     ERRLIN+1
                CMP     #$FF
                BEQ     NOTRAP         ; direct: no CONT
                LDA     ERRLIN
                STA     OLDLIN
                LDA     ERRLIN+1
                STA     OLDLIN+1
                LDA     SAVTXT
                STA     OLDTXT
                LDA     SAVTXT+1
                STA     OLDTXT+1
NOTRAP:         STZ     ONEFLG
                JSR     CRDONZ
                LDA     RDL
                FJSR    ERRPRT         ; the message, " in " ERRLIN, P2's
                ; and into READY

; --- READY, MAIN (GWMAIN.ASM 586) ----------------------------------------------------------

; READY: "Ok". FTSITE's far call to P1's FARTST, which does nothing, is the
; project's own: test_gwbasic_core times it, the built FARCALL's crossing.
; (The 8086's STPRDY pops the line ENDCON or LIST left on its stack; here
; nothing is left there, so STPRDY is READY.)
READY:
FTSITE:         FJSR    FARTST
                JSR     FINLPT         ; LPT1 to its left margin
                JSR     CRDONZ
                LDA     #<REDDY
                STA     PTR
                LDA     #>REDDY
                STA     PTR+1
                JSR     STROUT
REPINI:         LDA     ERRFLG         ; a Syntax error: its line to edit
                CMP     #ERRSN
                BNE     MAIN
                FJSR    ERREDT

; MAIN: a line from the console. A blank one is read again; one starting
; with a number goes into the program; any other runs at once, from KBUF,
; as the end of a program would (CRUNCH leaves three zeros after it). AUTO
; (7b) is left out: its line number and "*".
MAIN:           LDA     #$FF           ; CURLIN: direct
                STA     CURLIN
                STA     CURLIN+1
                LDA     #<ENDPRG
                STA     SAVTXT
                LDA     #>ENDPRG
                STA     SAVTXT+1
NTAUTO:         JSR     PINLIN         ; TXTPTR at BUF-1
                BCC     NTSTOP
                BRA     MAIN
NTSTOP:         JSR     CHRGTR
                INC     A              ; zero? (C kept)
                DEC     A
                BEQ     MAIN
                PHP                    ; C: a digit first
                JSR     LINGET
                BCC     MAINBX
                JMP     SNERR          ; a digit left over: the number is too big
MAINBX:         JSR     BAKSP
NTASTC:         LDA     RDL            ; a line number: one space after it is dropped
                ORA     RDH
                BEQ     EDENT
                LDA     (TXTPTR)
                CMP     #' '
                BNE     EDENT
                INCW    TXTPTR
EDENT:          LDA     RDH            ; the line number, kept
                PHA
                LDA     RDL
                PHA
                FJSR    CRUNCH         ; to KBUF; TXTPTR at KBUF-1, KCNT its length
                PLA
                STA     RDL
                PLA
                STA     RDH
                LDA     TXTPTR
                STA     SAVTXT
                LDA     TXTPTR+1
                STA     SAVTXT+1
                PLP
                BCS     :+
                JMP     GONE           ; DIRDO: direct, from the keyboard
:               FJSR    EDENT2         ; into the program; it ends at FINI
; FINI: after the program has changed, the variables cleared, and MAIN.
FINI:           JSR     RUNC
                JMP     MAIN

; BAKSP (GWMAIN.ASM 1412): TXTPTR back over the spaces, tabs and line feeds
; before it.
BAKSP:          DECW    TXTPTR
                LDA     (TXTPTR)
                CMP     #' '
                BEQ     BAKSP
                CMP     #9
                BEQ     BAKSP
                CMP     #10
                BEQ     BAKSP
                INCW    TXTPTR
                RTS

; --- PRGEND (GWMAIN.ASM 384) ----------------------------------------------------------

; The end of the program or of a direct line: ENDCON, with no "Break". In ON
; ERROR's routine (ONEFLG), No RESUME.
PRGEND:         LDA     CURLIN
                AND     CURLIN+1
                CMP     #$FF
                BEQ     ENDCNJ
                LDA     ONEFLG
                BEQ     ENDCNJ
                LDA     #ERRNR
                JMP     ERROR
ENDCNJ:         JMP     ENDCON

; --- RUN (GWMAIN.ASM 2018) -------------------------------------------------------------

; RUN: from the start (RUNC, which returns to NEWSTT at TXTTAB-1); RUN line:
; cleared, then GOTO it with NEWSTT to return to; RUN "file": LRUN, 7c's.
RUN:            BNE     :+
                JMP     RUNC
:               CMP     #LINCON
                BEQ     CONRUN
                CMP     #PTRCON
                BEQ     CONRUN
                JMP     LRUN
CONRUN:         JSR     CLEARC         ; TXTPTR kept
                PUSHRET NEWSTT         ; RUNC2
                JMP     GOTO

; --- LIST, LLIST, DELETE: P2's statements ----------------------------------------------

; LIST: P2's LISTP lists; then, as GW-BASIC's LIST, NEWSTT's return dropped
; and READY. LLIST: the same to LPT1 (PTRFIL; READY's FINLPT puts it back).
; DELETE: P2's DELETP, which ends at FINI.
LLIST:          PHA
                LDA     #$FF
                STA     PTRFIL
                STA     PTRFIL+1
                PLA
LIST:           FJSR    LISTP
                PLA
                PLA
                JMP     READY
DELETE:         FJSR    DELETP

.rodata
; GWDATA.ASM 1543
REDDY:          .byte   "Ok", $FF, 13, 10, 0

.segment "RAM"
; GWDATA.ASM's
ENDPRG:         .res    5              ; the fake end of a program SAVTXT points at in direct mode
DOT:            .res    2              ; the current line, for LIST . and EDIT
OLDLIN:         .res    2              ; CONT's line
OLDTXT:         .res    2              ; and text; 0: nothing to continue
ERRTXT:         .res    2              ; RESUME's (7b)
ONEFLG:         .res    1              ; in ON ERROR's routine (7b)
INITFG:         .res    1              ; start-up done: errors are BASIC's
.ifdef FLAT
.export ERRHOOK
ERRHOOK:        .res    2              ; the flat harness's error entry, or 0 (test/harness.s)
.endif

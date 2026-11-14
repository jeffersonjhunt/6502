; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM, translated: the resident part of the interpreter's frame.
;
;   ERROR SNERR ...    the errors' entry: the stacks reset, the message (P2's
;                      ERRPRT), then READY, or ON ERROR's trap (NTMDCN); the
;                      xxERR labels the code jumps to
;   READY MAIN         "Ok", a line read (the OEM's PINLIN), then run at once
;                      (direct) or crunched into the program (P2's EDENT2);
;                      FINI clears and goes round again
;   BAKSP              back over spaces after a number
;   PRGEND             the end of the program, or of a direct line
;   RUN                RUN and RUN line (RUN "file" is 7c's)
;   NEWSTT GONE        the statement fetcher and dispatch
;   TRPOLL GOSUB2      OO-1: the event traps' poll, NEWSTT's while a trap is
;                      on (7b step e); an event's GOSUB
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
;   FNDFOR             a FOR entry on the BASIC stack (step I2's, as the rest)
;   FOR                FOR, its NEXT found (NXTSCN) and run once (NEXT86.ASM's
;                      NEXTS, res/next86.s)
;   NXTSCN WNDSCN      the matching NEXT, or WEND (WHILE: res/fiveo.s)
;   GOSUB RETURN       GOSUB, and RETURN [line]
;   PRINT LPRINT ...   P1's statements' stubs: PRINT, LPRINT, INPUT, LINE
;                      INPUT, READ
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

.export NEWSTT, GONE, GONE2, CHRGTR, CHRGT2, CHRCON, CONFC1, FNDLIN, LINGET, LINGT2
.export GOTO, GOTO2, DATA, REM, ELSES, LET, IFS, CONCH2, CONFAC, LOOP
.export DEFSTR, DEFINT, DEFREA, DEFDBL, INTIDX, INTID2
.export ERROR, SNERR, DV0ERR, NFERR, UFERR, REERR, OVERR, MOERR, TMERR, FCERR
.export USERR, ERESET, ERRESM, READY, MAIN, FINI, BAKSP, PRGEND, RUN, ENDPRG, FTSITE
.export DOT, OLDLIN, OLDTXT, ERRTXT, ONEFLG, INITFG, REDDY, LIST, LLIST, DELETE
.export EDIT, EDTMAI, LSTRDY
.export FOR, FNDFOR, FORADV, FORSSP, NXTSCN, WNDSCN, GOSUB, RETURN, NXTLIN, FVALSV
.export NEWST1, GOSUB2, TRPON, TRPTST, CURLIN ; (the zero page ones for the label file: the tests)
.export LETCN2, INPCOM, LETCN4, DATSNE, DATLIN, PRINT, LPRINT, INPUT, LINE, READ, INPRET
.import STMDSP, NUMCON, OUTDO, NEWSTX, ISMID, LRUN
.import CHKINT, INEG2, SYNCHR, FRMEVL, PTRGET, DOCNVF, VMOVE, VSIGN
.import LETSTR, ISLET2, GETIN2, DEFTBL, SPOP, STKERR, CRDONZ, FINLPT
.import STROUT, PINLIN, ENDCON, RUNC, CLEARC
.import SUBFLG, GETSTK, FRCINT, FRCSNG, FRMCHK, GETYP, SIGNS, NEXTS, STPEND

ERRNF           = 1                    ; GWDATA.ASM's error numbers
ERRSN           = 2
ERRRG           = 3
ERRFC           = 5
ERROV           = 6
ERRUS           = 8
ERRDV0          = 11
ERRTM           = 13
ERRUF           = 18
ERRNR           = 19
ERRRE           = 20
ERRMO           = 22
ERRFN           = 26
ERRWH           = 29
FORSIZ          = 20                   ; the BASIC stack's entries (below)
GOSSIZ          = 8
WHLSIZ          = 8

.code

; --- NEWSTT --------------------------------------------------------------------

GORET:          RTS
GONCHR:         JSR     CHRCON
                BRA     GONE0
GISMD:          JSR     NEWSTX         ; an extended statement, or MID$ =
                JMP     ISMID
GONLET:         JMP     LET

; TRPOLL: OO-1, the event traps' poll (hw/oodos.md, "The plan, phase 7": 7b).
; The 8086's sources raise a trap's request from an interrupt, and NEWSTT
; tests ONGSBF each statement. Here NEWSTT tests TRPON alone (gwbasic.inc:
; nonzero while any trap is on, which P3 keeps), and only then comes here:
; the sources polled, then ONGSBF's test (which can be nonzero only while a
; trap is on) and GOTRP (P3). GOTRP returns C set for a trap taken (the far
; call rule: the 8086's GOTRP jumps to GOSUB2 itself), and the poll goes on
; into GOSUB2; else back to NEWST1. The sources' polls are 7d-7f's; until
; then TRPTST, the test-only trigger, stands in their place (p3/trptst.s),
; and goes when they come.
TRPOLL:         LDA     TRPTST         ; test only: a trigger (7d-7f: the sources' polls)
                BEQ     TRPOL1
                FJSR    TRPTSP
TRPOL1:         LDA     ONGSBF         ; NEWSTT's own test: an event's GOSUB?
                BEQ     NEWST1
                FJSR    GOTRP          ; possible ON GOSUB
                BCC     NEWST1         ; none taken
; GOSUB2 (GWMAIN.ASM 2073): the entry for an event's GOSUB, DX the line's
; pointer (its link), CX the trap's flag address (P3's, which RETURN hands
; back to P3: RTNTRP), and TXTPTR the text where NEWSTT was; then the line
; run from its start (GONE4), SAVTXT just before it.
GOSUB2:         JSR     GOSPSH         ; the entry: text, CURLIN, CX, GOSUB
                LDA     RDL            ; HL: the subroutine's text
                STA     TXTPTR
                SEC                    ; SAVTXT one before it (NEWSTT's INX)
                SBC     #1
                STA     SAVTXT
                LDA     RDH
                STA     TXTPTR+1
                SBC     #0
                STA     SAVTXT+1
                JMP     GONE4          ; continue with the subroutine

NEWSTT:         LDA     TRPON          ; OO-1: any trap on? (5 cycles, none)
                BNE     TRPOLL
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

; --- The BASIC stack's entries (GWMAIN.ASM 300, 1430, 2035; FIVEO.ASM 46) -------------
;
; FOR, GOSUB and WHILE leave an entry on the BASIC stack (the 8086's own
; stack: the spike's advice, so nothing walks page 1). Each is laid out as
; the 8086's, which keeps each an even length: a pad byte, then the token
; (the high byte of the 8086's word), then the rest, from the low address
; (SSP) up. FNDFOR and FNDWND walk them from SSP; the zero word STKINI puts
; at the bottom stops the walk, as anything not a FOR or a WHILE does.
;
;   FOR    +1 FOR_TK, +2 the loop variable, +4 the text of its NEXT, +6 the
;          step's sign (2 for a zero step), +7 VALTYP-3 (minus: an integer
;          loop), +8 the step (an integer's at +12, +8 unused), +12 the
;          limit (an integer's at +14), +16 the FOR's line, +18 the text at
;          the FOR statement's end: 20 bytes (the 8086's FORSZC is 19, the
;          pad not counted)
;   GOSUB  +1 GOSUB_TK, +2 an event's flag (0: a GOSUB; ON ... GOSUB is
;          7b's), +4 the line, +6 the text after its line number: 8 bytes
;   WHILE  +1 WHILE_TK, +2 the text after its WEND, +4 the text after the
;          WHILE, +6 its line: 8 bytes

; FNDFOR (GWMAIN.ASM 306): the FOR entry for the variable at DX (0: the
; first FOR), from SSP, past any WHILE. C set: found, FORPTR at the entry,
; DX its variable. C clear: not found, FORPTR at the entry that stopped the
; walk and A its token. (The 8086 says it in Z; A may be zero here.) LOOPER
; goes on from the entry at FORPTR.
FNDFOR:         LDA     SSP
                STA     FORPTR
                LDA     SSP+1
                STA     FORPTR+1
LOOPER:         LDY     #1             ; the entry's token
                LDA     (FORPTR),Y
                CMP     #WHILE_TK
                BNE     STKSRC
                LDA     #WHLSIZ        ; a WHILE: past it
LOOPNX:         JSR     FORADV
                BRA     LOOPER
STKSRC:         CMP     #FOR_TK        ; anything else ends the walk
                BEQ     :+
                CLC
                RTS
:               LDA     RDL            ; NEXT alone matches any
                ORA     RDH
                BNE     @var
                INY                    ; DX = the entry's variable
                LDA     (FORPTR),Y
                STA     RDL
                INY
                LDA     (FORPTR),Y
                STA     RDH
                SEC
                RTS
@var:           INY                    ; POPGOF: this variable's?
                LDA     (FORPTR),Y
                CMP     RDL
                BNE     @no
                INY
                LDA     (FORPTR),Y
                CMP     RDH
                BNE     @no
                SEC
                RTS
@no:            LDA     #FORSIZ
                BRA     LOOPNX
; FORADV: FORPTR up by A.
FORADV:         CLC
                ADC     FORPTR
                STA     FORPTR
                BCC     :+
                INC     FORPTR+1
:               RTS
; FORSSP: SSP, and SAVSTK's, at FORPTR: the stack cut there (the 8086's
; MOV SP,BX / MOV SAVSTK,BX).
FORSSP:         LDA     FORPTR
                STA     SSP
                STA     SAVSTK+1
                LDA     FORPTR+1
                STA     SSP+1
                STA     SAVSTK+2
                RTS

; --- FOR (GWMAIN.ASM 1445) -----------------------------------------------------------

; FOR v = start TO limit [STEP step]: the start to FVALSV; any FOR entry of
; the same variable ending at this same place dropped, with everything
; above it; the entry made; then the matching NEXT found (NXTSCN) and run
; once with the start (NEXTS), so a loop that runs no times goes on past
; its NEXT. The loop is integer or single; a string or a double is Type
; mismatch. NEWSTT's return stays on the 6502's stack (the 8086 pops it and
; makes a stand-in for NEXT): NEXT returns to it.
FOR:            LDA     #$40           ; SUBFLG 100 (octal): no subscripts
                STA     SUBFLG
                JSR     PTRGET
                JSR     SYNCHR
                .byte   EQULTK
                LDA     PTR            ; the variable (the 8086's TEMP)
                STA     FORVAR
                LDA     PTR+1
                STA     FORVAR+1
                LDA     VALTYP         ; its type
                PHA
                JSR     FRMEVL         ; the start
                PLA
                JSR     DOCNVF         ; as the loop's type
                LDX     #3             ; MOVMF: to FVALSV, for NEXT
@sv:            LDA     FACLO,X
                STA     FVALSV,X
                DEX
                BPL     @sv
                LDA     TXTPTR+1       ; the text pointer kept
                PHA
                LDA     TXTPTR
                PHA
                JSR     DATA           ; the end of the statement: ENDFOR
                LDA     TXTPTR
                STA     ENDFOR
                LDA     TXTPTR+1
                STA     ENDFOR+1
                LDA     SSP
                STA     FORPTR
                LDA     SSP+1
                STA     FORPTR+1
LPFORM:         LDA     FORVAR         ; DX: the variable
                STA     RDL
                LDA     FORVAR+1
                STA     RDH
                JSR     LOOPER
                BCC     NOTOL          ; none: nothing dropped
                LDY     #18            ; ending where this one ends?
                LDA     (FORPTR),Y
                CMP     ENDFOR
                BNE     @no
                INY
                LDA     (FORPTR),Y
                CMP     ENDFOR+1
                BEQ     @cut
@no:            LDA     #FORSIZ        ; no: look on past it
                JSR     FORADV
                BRA     LPFORM
@cut:           LDA     #FORSIZ        ; it and everything above it go
                JSR     FORADV
                JSR     FORSSP
NOTOL:          PLA
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                LDA     #8             ; 16 bytes free, as the 8086 asks
                JSR     GETSTK
                SEC                    ; the entry's room
                LDA     SSP
                SBC     #FORSIZ
                STA     SSP
                STA     FORPTR
                LDA     SSP+1
                SBC     #0
                STA     SSP+1
                STA     FORPTR+1
                LDY     #18            ; the FOR's end, and its line
                LDA     ENDFOR
                STA     (FORPTR),Y
                INY
                LDA     ENDFOR+1
                STA     (FORPTR),Y
                LDY     #16
                LDA     CURLIN
                STA     (FORPTR),Y
                INY
                LDA     CURLIN+1
                STA     (FORPTR),Y
                JSR     SYNCHR
                .byte   TO_TK
                JSR     GETYP          ; a string or a double: Type mismatch
                BNE     :+
                JMP     TMERR
:               BCC     :+
                JMP     TMERR
:               STA     FORFLG         ; minus: an integer loop
                JSR     FRMEVL         ; the limit
                BIT     FORFLG
                BPL     SNGFOR
                JSR     FRCINT         ; an integer loop: the limit in BX
                LDY     #14
                LDA     RBL
                STA     (FORPTR),Y
                INY
                LDA     RBH
                STA     (FORPTR),Y
                LDA     #1             ; the step: 1, or GETINT's
                STA     RDL
                STZ     RDH
                LDA     (TXTPTR)
                CMP     #STEP_TK
                BNE     :+
                JSR     CHRGTR
                JSR     GETIN2
:               LDY     #12
                LDA     RDL
                STA     (FORPTR),Y
                INY
                LDA     RDH
                STA     (FORPTR),Y
                ORA     RDL            ; ISIGN: its sign
                BEQ     STPSGN
                LDA     RDH
                BMI     :+
                LDA     #1
                BRA     STPSGN
:               LDA     #$FF
                BRA     STPSGN
SNGFOR:         JSR     FRCSNG         ; a single loop: the limit
                LDY     #12
                JSR     FACFOR
                LDY     #11            ; the step: 1.0, or the formula's
@one:           LDA     ONE-8,Y
                STA     (FORPTR),Y
                DEY
                CPY     #8
                BCS     @one
                LDA     (TXTPTR)
                CMP     #STEP_TK
                BEQ     :+
                LDA     #1
                BRA     STPSGN
:               JSR     FRMCHK
                JSR     FRCSNG
                LDY     #8
                JSR     FACFOR
                JSR     SIGNS
; A zero step's sign is made 2, so the loop never ends, as ANSI asks: NEXT
; ends it when the sign of (value - limit) less the step's sign is zero.
STPSGN:         CMP     #0
                BNE     NT0STP
                LDA     #2
NT0STP:         LDY     #6
                STA     (FORPTR),Y
                JSR     GETYP          ; the integer flag
                INY
                STA     (FORPTR),Y
                DECW    TXTPTR         ; the FOR must end here
                JSR     CHRGTR
                BEQ     :+
                JMP     SNERR
:               JSR     NXTSCN         ; its NEXT
                JSR     CHRGTR         ; NEXT's first character
                PHP
                LDY     #4             ; the entry's NEXT text and variable
                LDA     TXTPTR
                STA     (FORPTR),Y
                INY
                LDA     TXTPTR+1
                STA     (FORPTR),Y
                LDA     NXTLIN         ; NEXT's line, current
                STA     CURLIN
                LDA     NXTLIN+1
                STA     CURLIN+1
                LDY     #2
                LDA     FORVAR
                STA     (FORPTR),Y
                INY
                LDA     FORVAR+1
                STA     (FORPTR),Y
                LDY     #1
                LDA     #FOR_TK        ; and the token: the entry is whole
                STA     (FORPTR),Y
                PLP
                JMP     NEXTS          ; NEXT, with the start (NXTFLG 0)
; FACFOR: the FAC's single to the entry at FORPTR+Y.
FACFOR:         LDX     #0
@m:             LDA     FACLO,X
                STA     (FORPTR),Y
                INY
                INX
                CPX     #4
                BNE     @m
                RTS

; --- NXTSCN, WNDSCN (GWMAIN.ASM 3418) -------------------------------------------------

; The matching NEXT (NXTSCN) or WEND (WNDSCN), from TXTPTR: each statement's
; first token looked at (after ":", THEN, ELSE and a line's start), strings
; skipped, and REM and DATA skipped whole, as their colons could look like
; statements' ends. FOR and NEXT (WHILE and WEND) are counted. NXTLIN
; follows the lines. Returns TXTPTR at the matching token (for a NEXT that
; names several variables, at the comma before the matching one); at the
; program's end, FOR Without NEXT (WHILE without WEND) in the line where it
; started. The 8086 keeps the count and the error in CX: FORCNT and SCNERR.
WNDSCN:         LDA     #ERRWH
                BRA     SCNCNT
NXTSCN:         LDA     #ERRFN
SCNCNT:         STA     SCNERR
                STZ     FORCNT
                LDA     CURLIN         ; NEXT on the same line
                STA     NXTLIN
                LDA     CURLIN+1
                STA     NXTLIN+1
FORINC:         INC     FORCNT
FNLOP:          DECW    TXTPTR
SCANWF:         JSR     CHRGTR
                BEQ     FORTRM
                CMP     #'"'           ; a string may hold any byte
                BNE     NTQTSC
QTLOPF:         JSR     CHRGTR
                CMP     #0
                BEQ     FORTRM         ; only a zero ends it, not ":"
                CMP     #'"'
                BNE     QTLOPF
NTQTSC:         CMP     #ELSE_TK       ; a statement after ELSE or THEN
                BEQ     FNNWST
                CMP     #THEN_TK
                BNE     SCANWF
FORTRM:         CMP     #0             ; ":": the next statement
                BNE     FNNWST
                INCW    TXTPTR         ; the next line's link: none, the error
                LDA     (TXTPTR)
                INCW    TXTPTR
                ORA     (TXTPTR)
                BNE     :+
                LDA     SCNERR
                JMP     ERROR
:               INCW    TXTPTR         ; its number to NXTLIN
                LDA     (TXTPTR)
                STA     NXTLIN
                INCW    TXTPTR
                LDA     (TXTPTR)
                STA     NXTLIN+1
FNNWST:         JSR     CHRGTR         ; the statement's first token
                CMP     #REM_TK
                BNE     NTREMR
                JSR     REM
                BRA     FORTRM
NTREMR:         CMP     #DATA_TK
                BNE     NTDATR
                JSR     DATA
                BRA     FORTRM
NTDATR:         LDX     SCNERR         ; FOR and NEXT, or WHILE and WEND?
                CPX     #ERRFN
                BEQ     NXTLOK
                CMP     #WHILE_TK
                BEQ     FORINC
                CMP     #WEND_TK
                BNE     FNLOP
                DEC     FORCNT
                BNE     FNLOP
                RTS
NXTLOK:         CMP     #FOR_TK
                JEQ     FORINC
                CMP     #NEXT_TK
                JNE     FNLOP
DECNXT:         DEC     FORCNT
                BNE     :+
                RTS                    ; at the NEXT, or the comma
; The variables a NEXT names, each one a FOR's: PTRGET steps over each,
; with CURLIN NEXT's line for its errors.
:               JSR     CHRGTR
                BEQ     FORTRM         ; NEXT alone
                LDA     CURLIN+1
                PHA
                LDA     CURLIN
                PHA
                LDA     NXTLIN
                STA     CURLIN
                LDA     NXTLIN+1
                STA     CURLIN+1
                JSR     PTRGET
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     TRMNXT         ; its end (A the terminator)
                JSR     SYNCHR
                .byte   ','
                DECW    TXTPTR
                PLA
                STA     CURLIN
                PLA
                STA     CURLIN+1
                BRA     DECNXT
TRMNXT:         PLX                    ; CURLIN back, A kept
                STX     CURLIN
                PLX
                STX     CURLIN+1
                JMP     FORTRM

; --- GOSUB, RETURN (GWMAIN.ASM 2049, 2154) -------------------------------------------

; GOSUB line: the entry (CURLIN, and the text after the line number, for
; RETURN; no event's flag, CX 0), then GOTO's search. GOSPSH makes the entry
; from CX and TXTPTR, for GOSUB and for an event's GOSUB (GOSUB2, above).
GOSUB:          LDA     #3
                JSR     GETSTK
                JSR     LINGET
                STZ     RCL            ; not an event's flag (ON event GOSUB)
                STZ     RCH
                JSR     GOSPSH
                JMP     GOTO2
GOSPSH:         SEC                    ; the entry, by the 8086's pushes
                LDA     SSP
                SBC     #GOSSIZ
                STA     SSP
                STA     PTR
                LDA     SSP+1
                SBC     #0
                STA     SSP+1
                STA     PTR+1
                LDY     #1
                LDA     #GOSUB_TK
                STA     (PTR),Y
                INY                    ; CX: the event's flag, or 0
                LDA     RCL
                STA     (PTR),Y
                INY
                LDA     RCH
                STA     (PTR),Y
                INY
                LDA     CURLIN
                STA     (PTR),Y
                INY
                LDA     CURLIN+1
                STA     (PTR),Y
                INY
                LDA     TXTPTR
                STA     (PTR),Y
                INY
                LDA     TXTPTR+1
                STA     (PTR),Y
                RTS

; RETURN [line]: every FOR and WHILE above the GOSUB's entry dropped (FNDFOR
; with a variable no entry has), then its line and text back, and the rest
; of the GOSUB statement skipped; RETURN line goes there instead, from here.
; With no GOSUB entry, RETURN without GOSUB, the stack cut at what stopped
; the walk. (The 8086 leaves SAVSTK just past the GOSUB's token: so here.)
; An event's GOSUB entry (GOSUB2's, its trap's flag address not 0) has P3
; take the stop off its trap (RTNTRP: RSTTRP, if the trap is on).
RETURN:         LDA     TXTPTR
                STA     RTNTXT
                LDA     TXTPTR+1
                STA     RTNTXT+1
                LDA     #$FF           ; a variable pointer no entry has
                STA     RDH
                JSR     FNDFOR
                CMP     #GOSUB_TK
                BEQ     RTRNOK
                JSR     FORSSP
                LDA     #ERRRG
                JMP     ERROR
RTRNOK:         LDA     #2             ; past the token
                JSR     FORADV
                JSR     FORSSP
                LDA     (FORPTR)       ; the trap's flag address, if an event's
                LDY     #1
                ORA     (FORPTR),Y
                BEQ     NOTEVT         ; not ON event GOSUB's
                FJSR    RTNTRP         ; its stop taken off (P3: RSTTRP)
NOTEVT:         LDY     #2             ; the GOSUB's line in CX,
                LDA     (FORPTR),Y     ; its text in DX
                STA     RCL
                INY
                LDA     (FORPTR),Y
                STA     RCH
                INY
                LDA     (FORPTR),Y
                STA     RDL
                INY
                LDA     (FORPTR),Y
                STA     RDH
                LDA     #GOSSIZ - 2    ; the entry popped
                JSR     FORADV
                LDA     FORPTR
                STA     SSP
                LDA     FORPTR+1
                STA     SSP+1
                LDA     RTNTXT         ; RETURN line?
                STA     TXTPTR
                LDA     RTNTXT+1
                STA     TXTPTR+1
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     :+
                JMP     GOTO
:               LDA     RCL            ; CURLIN back
                STA     CURLIN
                LDA     RCH
                STA     CURLIN+1
                LDA     RDL            ; and the text: the GOSUB's rest skipped
                STA     TXTPTR
                LDA     RDH
                STA     TXTPTR+1
                JMP     DATA

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
; DATSNE (GWMAIN.ASM 419): READ's Syntax error, in the DATA's line.
DATSNE:         LDA     DATLIN
                STA     CURLIN
                LDA     DATLIN+1
                STA     CURLIN+1
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
; for CONT; then ON ERROR's trap (NTMDCN), or the message: "Syntax error
; in 20", on a line of its own. ERRESM is the way in for an error forced
; again (ON ERROR GOTO 0 in the handler, p3/gwmain.s RESTRP): ERRLIN and DOT
; as they were.
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
                LDA     SAVTXT         ; RESUME's
                STA     ERRTXT
                LDA     SAVTXT+1
                STA     ERRTXT+1
                LDA     ERRLIN
                AND     ERRLIN+1
                INC     A              ; the 8086's AL: zero for a direct line,
                TAY                    ; kept in Y for NTMDCN
                BEQ     NTMDCN         ; direct: no CONT
                LDA     ERRLIN
                STA     OLDLIN
                LDA     ERRLIN+1
                STA     OLDLIN+1
                LDA     SAVTXT
                STA     OLDTXT
                LDA     SAVTXT+1
                STA     OLDTXT+1
; NTMDCN (GWMAIN.ASM 501): ON ERROR's trap. With ONELIN 0, no trap. Else
; AL AND ONEFLG, as the 8086 has it: AL is zero for a direct line, so a
; direct line's error is always trapped, even in the handler; in a program
; line it is the line number's two bytes ANDed, plus one (not zero), so the
; handler's own error (ONEFLG $FF) is printed. Trapped: ONEFLG decremented,
; and the line ONELIN points at run (its link at TXTPTR, as GONE4 wants it),
; with both stacks as the statement began (ERESET), RESUME's text in ERRTXT.
NTMDCN:         LDA     ONELIN
                ORA     ONELIN+1
                BEQ     NOTRAP         ; no trapping
                TYA
                AND     ONEFLG
                BNE     NOTRAP         ; in the handler already: the message
                DEC     ONEFLG
                LDA     ONELIN
                STA     TXTPTR
                LDA     ONELIN+1
                STA     TXTPTR+1
                JMP     GONE4
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
; as the end of a program would (CRUNCH leaves three zeros after it). In
; AUTO (AUTFLG), P3's MAINA offers the line's number first, and AUTAST
; skips the "*" offered after it (p3/gwmain.s). (PINLIN never returns C set
; on OODOS: a ^C breaks inside it, and CNTCCN ends AUTO, so the 8086's
; AUTFLG reset on that way back isn't needed.)
MAIN:           LDA     #$FF           ; CURLIN: direct
                STA     CURLIN
                STA     CURLIN+1
                LDA     #<ENDPRG
                STA     SAVTXT
                LDA     #>ENDPRG
                STA     SAVTXT+1
                LDA     AUTFLG         ; in an AUTO command?
                BEQ     NTAUTO
                FJSR    MAINA          ; its line number into BUF, and "*"
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
                LDA     AUTFLG         ; AUTO: its "*" skipped
                BEQ     NTASTC
                FJSR    AUTAST
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
LLIST:          PHP                    ; CHRGTR's flags kept, as the 8086's
                PHA                    ; PUSH, MOV, POP keeps them (Z: the
                LDA     #$FF           ; statement's end)
                STA     PTRFIL
                STA     PTRFIL+1
                PLA
                PLP
LIST:           FJSR    LISTP
LSTRDY:         PLA
                PLA
                JMP     READY
DELETE:         FJSR    DELETP

; EDIT (7b step c): P2's EDITP puts the line in BUF to edit (SCNEDT.ASM's
; EDIT and EREDIT); then NEWSTT's return dropped and MAIN, as EREDIT's POP
; BX and JMP MAIN. AUTO's stub (res/far3.s) ends here too.
EDIT:           FJSR    EDITP
EDTMAI:         PLA
                PLA
                JMP     MAIN

; --- PRINT, LPRINT, INPUT, LINE INPUT, READ: P1's statements -----------------------

; Each a far call to P1 (p1/gio86.s, p1/gwmain.s), back to NEWSTT. INPUT
; and LINE INPUT return C set for a ^C (the 8086's JMP STPEND): STPEND pops
; NEWSTT's return, so it is called from here, with nothing of the far call
; left on the 6502's stack. (OODOS's function 10 gives the ^C as a
; character, and SINLIN's ITSCTC breaks at once, so 7a's SINLIN doesn't
; return that way.) LINE alone (graphics, 7e) is Advanced Feature, in P1.
PRINT:          FJSR    PRINTP
                RTS
LPRINT:         FJSR    LPRNTP
                RTS
READ:           FJSR    READP
                RTS
INPUT:          FJSR    INPUTP
                BRA     INPRET
LINE:           FJSR    LINEP
INPRET:         BCC     :+
                JMP     STPEND
:               RTS

.rodata
; GWDATA.ASM 1543
REDDY:          .byte   "Ok", $FF, 13, 10, 0
ONE:            .byte   0, 0, 0, $81   ; FOR's step, 1.0 (the 8086's CX $8100, DX 0)

.segment "RAM"
; GWDATA.ASM's
FORVAR:         .res    2              ; FOR's loop variable (the 8086's TEMP)
FVALSV:         .res    4              ; FOR's start, for NEXTS
ENDFOR:         .res    2              ; the FOR (WHILE) statement's end
NXTLIN:         .res    2              ; NXTSCN's line
FORFLG:         .res    1              ; FOR: VALTYP-3, minus for an integer loop
FORCNT:         .res    1              ; NXTSCN's count of FORs (the 8086's CH)
SCNERR:         .res    1              ; and its error (CL)
RTNTXT:         .res    2              ; RETURN's text (the 8086's TEMP)
DATLIN:         .res    2              ; the line of the DATA READ is in
ENDPRG:         .res    5              ; the fake end of a program SAVTXT points at in direct mode
DOT:            .res    2              ; the current line, for LIST . and EDIT
OLDLIN:         .res    2              ; CONT's line
OLDTXT:         .res    2              ; and text; 0: nothing to continue
ERRTXT:         .res    2              ; RESUME's: the erring statement's text (SAVTXT)
ONEFLG:         .res    1              ; in ON ERROR's routine: nonzero (NTMDCN decrements it)
INITFG:         .res    1              ; start-up done: errors are BASIC's
.ifdef FLAT
.export ERRHOOK
ERRHOOK:        .res    2              ; the flat harness's error entry, or 0 (test/harness.s)
.endif

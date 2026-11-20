; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM, translated: the home block's part of the interpreter's frame
; (hw/oodos.md, "The plan, phase 7": 7c's home block). The statement loop
; (NEWSTT, CHRGTR, GONE) is resident; these are the statements and the
; main loop it reaches only with the home block in W3, which no banked
; block calls:
;
;   FNDFOR             a FOR entry on the BASIC stack
;   FOR                FOR, its NEXT found (NXTSCN) and run once (NEXTS,
;                      p8/next86.s)
;   NXTSCN WNDSCN      the matching NEXT, or WEND (WHILE: p8/fiveo.s)
;   RETURN             RETURN [line] (GOSUB is resident: GOSUB2 shares it)
;   DEFSTR DEFINT      DEFSTR DEFINT DEFSNG DEFDBL: DEFTBL, for PTRGET
;   DEFREA DEFDBL
;   READY MAIN         "Ok" (READY's far call, FTSITE, resident), a line
;                      read, then run at once or crunched into the program;
;                      FINI clears and goes round again
;   LIST LLIST ...     P2's statements' stubs: LIST, LLIST, DELETE, EDIT
;   PRINT LPRINT ...   P1's statements' stubs: PRINT, LPRINT, INPUT, LINE
;                      INPUT, READ
;   CONT TON TOFF      CONT, TRON, TROFF (BIMISC.ASM's; 7e3's S1)
;
; Resident code reaches READY, GONE4 and FINI only through res/home.s, which
; sets the 6502's stack back to its base and far calls the home block, so W3
; holds the home block whenever this code runs.

.include "gwbasic.inc"

.export FOR, FNDFOR, FORADV, FORSSP, NXTSCN, WNDSCN, RETURN
.export DEFSTR, DEFINT, DEFREA, DEFDBL, READY, MAIN, FINIH
.export LIST, LLIST, DELETE, EDIT, EDTMAI, LSTRDY
.export PRINT, LPRINT, INPUT, LINE, READ, INPRET, CONT, TON, TOFF
.import BAKSP, CHRGTR, CRDONZ, DATA, DEFTBL, DOCNVF, ENDFOR, ENDPRG, ERROR, FINLPT
.import FORCNT, FORFLG, FORVAR, FRCINT, FRCSNG, FRMCHK, FRMEVL, FVALSV, GETIN2, GETSTK
.import GETYP, GONE, GOTO, ISLET2, LINGET, NXTLIN, ONE, PINLIN, PTRGET, REDDY, REM
.import RTNTXT, RUNC, SCNERR, SIGNS, SNERR, STPEND, STROUT, SUBFLG, SYNCHR, TMERR
.import FTSITE, NEXTS, ISFLIO, NEWSTT, SCRMOD, BUF, OLDTXT, OLDLIN

ERRSN           = 2                    ; GWDATA.ASM's error numbers
ERRRG           = 3
ERRFN           = 26
ERRWH           = 29
ERRCN           = 17                   ; Can't continue
FORSIZ          = 20                   ; the BASIC stack's entries (the FOR section)
GOSSIZ          = 8
WHLSIZ          = 8

.code

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


; --- READY, MAIN (GWMAIN.ASM 586) ----------------------------------------------------------

; READY: "Ok". FTSITE's far call to P1's FARTST, which does nothing, is the
; project's own: test_gwbasic_core times it, the built FARCALL's crossing
; (FTSITE is resident, res/gwmain.s, so its site is the resident core's).
; (The 8086's STPRDY pops the line ENDCON or LIST left on its stack; here
; nothing is left there, so STPRDY is READY.)
READY:          JSR     FTSITE
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
; AUTFLG reset on that way back isn't needed.) While PTRFIL is a file (7c:
; LOAD or MERGE of a text file, file 0), the line is the file's, PINLIN's
; DSKCHI: P4 reads it whole into BUF (DSKCHP, one far call a line), and at
; the file's end, if the LOAD runs it, RUNC and NEWSTT, as INCHR's NOTCHN
; does (a CHAIN's end, CHNFLG, CHNRET and NEWSTT, as its LDREOF does); a
; direct statement in it is Direct statement in file (DIRDO), a
; line number too big isn't Syntax error.
MAIN:           STZ     RUNPND         ; (OO-1: an error on the way, no run)
                LDA     #$FF           ; CURLIN: direct
                STA     CURLIN
                STA     CURLIN+1
                LDA     #<ENDPRG
                STA     SAVTXT
                LDA     #>ENDPRG
                STA     SAVTXT+1
                LDA     AUTFLG         ; in an AUTO command?
                BEQ     NTAUTO
                FJSR    MAINA          ; its line number into BUF, and "*"
NTAUTO:         JSR     ISFLIO         ; PINLIN's DSKCHI: a line of a file
                BEQ     @kbd           ; LOAD or MERGE reads as text
                FJSR    DSKCHP         ; TXTPTR at BUF-1; C set: its end, run
                BCC     NTSTOP
                LDA     CHNFLG         ; a CHAIN's end: LDREOF's CHNRET (P4's;
                BEQ     :+             ; TXTPTR where NEWSTT starts)
                FJSR    CHNRET
                JMP     NEWSTT
:               LDX     #0             ; OO-1: the file's last line, with no CR,
@w:             LDA     BUF,X          ; goes in first, as LOAD's does (the IBM
                BEQ     @run           ; manual's p. 306, RUN filespec loads it);
                INX                    ; FINIH then runs it. Only blanks and
                CMP     #' '           ; tabs: nothing to put in, the run at
                BEQ     @w             ; once (review round 1, F4)
                CMP     #9
                BEQ     @w
                DEC     RUNPND
                BRA     NTSTOP
@run:           JSR     RUNC           ; NOTCHN's RUNC and NEWSTT (INCHR's)
                JMP     NEWSTT
@kbd:           JSR     PINLIN         ; TXTPTR at BUF-1
                BCC     NTSTOP
                BRA     MAIN
NTSTOP:         JSR     CHRGTR
                INC     A              ; zero? (C kept)
                DEC     A
                BEQ     MAIN
                PHP                    ; C: a digit first
                JSR     LINGET
                BCC     MAINBX
                JSR     ISFLIO         ; a digit left over: the number is too
                BNE     MAINBX         ; big, unless the line is a file's
                JMP     SNERR
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
                JSR     ISFLIO         ; DIRDO: direct, from the keyboard; from
                BEQ     @dir           ; a file, Direct statement in file
                LDA     #ERRFDR        ; (ERFDR)
                JMP     ERROR
@dir:           JMP     GONE
:               FJSR    EDENT2         ; into the program; it ends at FINI
; FINI: after the program has changed, the variables cleared, and MAIN.
FINIH:          JSR     RUNC           ; FINI (resident FINI, res/home.s, comes here)
                BIT     RUNPND         ; OO-1: a text file's last line put in by
                BPL     :+             ; a run (MAIN): now NEWSTT, as INCHR's
                JMP     NEWSTT         ; NOTCHN (RUNC done)
:               JMP     MAIN

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
; return that way.) LINE alone, the graphics' (7e1), is P5's GLINEP in
; screen mode (no hop through P1), and in console mode P1's LINEP, Advanced
; Feature as it was.
PRINT:          FJSR    PRINTP
                RTS
LPRINT:         FJSR    LPRNTP
                RTS
READ:           FJSR    READP
                RTS
INPUT:          FJSR    INPUTP
                BRA     INPRET
LINE:           CMP     #INPUT_TK
                BEQ     :+
                BIT     SCRMOD
                BPL     :+
                FJSR    GLINEP
                RTS
:               FJSR    LINEP
INPRET:         BCC     :+
                JMP     STPEND
:               RTS

; --- CONT, TRON, TROFF (BIMISC.ASM 639) ----------------------------------------------

; CONT: on from OLDTXT (0: Can't continue), at OLDLIN. OO-1 (7e3's S1): the
; home block's, as HOMTAB alone dispatches them; the resident SCRTCH clears
; TRCFLG itself.
CONT:           LDA     OLDTXT
                ORA     OLDTXT+1
                BNE     :+
                LDA     #ERRCN         ; RESERR
                JMP     ERROR
:               LDA     OLDTXT
                STA     TXTPTR
                LDA     OLDTXT+1
                STA     TXTPTR+1
                LDA     OLDLIN
                STA     CURLIN
                LDA     OLDLIN+1
                STA     CURLIN+1
                RTS
TON:            LDA     #$FF
                BRA     TONOFF
TOFF:           LDA     #0
TONOFF:         STA     TRCFLG
                RTS

.segment "BRAM"
RUNPND:         .res    1              ; OO-1: bit 7, run once the line is in (MAIN, FINIH)

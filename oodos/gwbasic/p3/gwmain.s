; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM's ON, RESUME and ERROR statements, in block P3 (hw/oodos.md,
; "The plan, phase 7"), each reached by its resident stub (res/far3.s):
;
;   ONGOTOP   ON ERROR GOTO line (and GOTO 0, which in the handler forces
;             the error again: RESTRP), ON n GOTO and ON n GOSUB (NTONGS,
;             NTONG1, ISGOSU, LOOPON); ON event GOSUB is step e's (ONGOTP)
;   RESUMEP   RESUME, RESUME 0, RESUME NEXT, RESUME line
;   ERRORSP   ERROR n
;
; and step a2's:
;
;   OPTIONP   OPTION BASE 0 or 1
;   RANDOMP   RANDOMIZE n, and RANDOMIZE alone, which asks for the seed
;   FRMQNT    a formula as 0..65535 in DX (CLEAR's GETMPM, p3/bimisc.s)
;
; The far call rule (translate-7b-common.md): ON's chosen GOTO or GOSUB is
; started by JMP GONE2, a tail call, as the 8086's is: the statement's RTS
; returns through FARCALL to the stub, whose RTS goes to NEWSTT. RESUME's
; JMP DATA is one too. RESTRP's JMP ERRESM and the errors reset both stacks.
; RANDOMIZE returns C set for a ^C at its prompt (the 8086's JMP STPEND),
; which the stub takes to STPEND, as INPUT's is.

.include "gwbasic.inc"

;@entry ONGOTOP
;@entry RESUMEP
;@entry ERRORSP
;@entry OPTIONP
;@entry RANDOMP
.export ONGOTOP, RESUMEP, ERRORSP, OPTIONP, RANDOMP, FRMQNT
.import CHRGTR, SYNCHR, LINGET, LINGT2, FNDLIN, GOTO2, GONE2, DATA, GETBYT
.import ERROR, ERRESM, USERR, REERR, FCERR, ADVERR, ONEFLG, ERRTXT, ONGOTP
.import SNERR, DDERR, OPTVAL, FRMEVL, FRCINT, STROUT, QINLIN, RNDX, RNDMN2

.code

; --- ON (GWMAIN.ASM 2357) --------------------------------------------------------------

; ONGOTO: A the character after ON, as CHRGTR left it. ON ERROR GOTO line:
; the line's link to ONELIN (0 for GOTO 0). FNDLIN keeps TXTPTR, so it is
; the 8086's FNDLN1 (which saves BX round FNDLIN) as it stands. C at RESTRP
; is the 8086's: set, a line was found; clear, GOTO 0.
ONGOTOP:        CMP     #ERROR_TK      ; ON ERROR?
                BNE     NTOERR
                JSR     CHRGTR
                JSR     SYNCHR
                .byte   GOTO_TK
                JSR     LINGET
                LDA     RDL            ; line 0: the trap off
                ORA     RDH
                CLC                    ; (the 8086's OR clears C)
                BEQ     RESTRP
                JSR     FNDLIN         ; FNDLN1: C set, found
                BCS     :+
                JMP     USERR
:               LDA     RCL            ; DX: the line's link (C kept)
                STA     RDL
                LDA     RCH
                STA     RDH
; RESTRP (GWMAIN.ASM 2373)
RESTRP:         LDA     RDL
                STA     ONELIN
                LDA     RDH
                STA     ONELIN+1
                BCC     :+
                RTS                    ; a line: done
:               LDA     ONEFLG         ; GOTO 0 in the handler?
                BNE     :+
                RTS
:               LDA     ERRFLG         ; yes: the error happens after all
                STA     RDL
                JMP     ERRESM
; NTOERR (GWMAIN.ASM 2384): ON event GOSUB (ONGSB0 to RSTRAP) is step e's;
; for now ONGOTP says "not an event" for everything (p3/todo_a1.s).
NTOERR:         JSR     ONGOTP         ; C set: not an event
                BCS     NTONGS
                JMP     ADVERR
; NTONGS (GWMAIN.ASM 2443): the index, 0-255, or Illegal function call.
NTONGS:         JSR     GETBYT         ; DL
; NTONG1 (GWMAIN.ASM 2447): GOSUB, or GOTO; CH the token, CL the count.
NTONG1:         LDA     (TXTPTR)
                STA     RCH
                CMP     #GOSUB_TK
                BEQ     ISGOSU
                JSR     SYNCHR
                .byte   GOTO_TK
                DECW    TXTPTR
ISGOSU:         LDA     RDL
                STA     RCL
; LOOPON (GWMAIN.ASM 2456): CL-1 line numbers skipped, then the statement
; started at the comma before its line number (or, for the first, where
; the 8086 leaves it). A list too short: on to the next statement.
LOOPON:         DEC     RCL
                BNE     :+
                LDA     RCH            ; done: GOTO or GOSUB, a tail call
                JMP     GONE2
:               JSR     LINGT2         ; past a line number
                CMP     #','
                BEQ     LOOPON
                RTS

; --- RESUME (GWMAIN.ASM 2471) ------------------------------------------------------------

; RESUME: only in the handler (ONEFLG), else RESUME without error. ERR
; becomes 0. RESUME and RESUME 0: the erring statement again (TXTPTR at
; ERRTXT, NEWSTT's). RESUME NEXT: the statement after it (ERRTXT's, skipped
; by DATA). RESUME line: GOTO2's. ONEFLG cleared on each way out, after
; GOTO2 (so its Undefined line number isn't trapped); a statement not ended
; is left to NEWSTT's Syntax error.
RESUMEP:        LDA     ONEFLG
                BNE     :+
                JMP     REERR
:               STZ     ERRFLG
                LDA     (TXTPTR)       ; the current character
                CMP     #NEXT_TK
                BEQ     RESNXT
                JSR     LINGET
                BEQ     :+
                RTS                    ; not ended
:               LDA     RDL            ; RESUME 0, or plain?
                ORA     RDH
                BEQ     RES0
                JSR     GOTO2
                STZ     ONEFLG
                RTS
; RESNXT (GWMAIN.ASM 2495)
RESNXT:         JSR     CHRGTR         ; must end here
                BEQ     RESTXT         ; Z: RESUME NEXT
                RTS
; RES0 (GWMAIN.ASM 2500)
RES0:           STZ     ONEFLG
                LDA     #1             ; Z clear: RESUME
; RESTXT (GWMAIN.ASM 2503): the 8086's moves keep Z; PHP and PLP keep it
; here.
RESTXT:         PHP
                LDA     ERRTXT
                STA     TXTPTR
                LDA     ERRTXT+1
                STA     TXTPTR+1
                LDA     ERRLIN         ; CURLIN: the error's
                STA     CURLIN
                LDA     ERRLIN+1
                STA     CURLIN+1
                PLP
                BEQ     :+
                RTS                    ; RESUME: to NEWSTT
:               LDA     (TXTPTR)       ; ":", or the zero before a line
                BNE     NOTBGL
                CLC                    ; past the line's link and number
                LDA     TXTPTR
                ADC     #4
                STA     TXTPTR
                BCC     NOTBGL
                INC     TXTPTR+1
; NOTBGL (GWMAIN.ASM 2517)
NOTBGL:         INCW    TXTPTR         ; the statement's start
                STZ     ONEFLG
                JMP     DATA           ; past it: a tail call

; --- ERROR (GWMAIN.ASM 2528) ----------------------------------------------------------------

; ERRORS: ERROR n, 1-255: error n (0, or more than 255: Illegal function
; call); a statement not ended is left to NEWSTT's Syntax error.
ERRORSP:        JSR     GETBYT
                BEQ     :+
                RTS
:               LDA     RDL
                BNE     GOERR
                JMP     FCERR
GOERR:          JMP     ERROR          ; GWMAIN.ASM 2535 GOERR

; --- OPTION BASE (GWMAIN.ASM 3327) --------------------------------------------------------

; OPTION BASE n: the letters BASE (not a reserved word, so in the text as
; they were typed), then the digit as a character (CRUNCH keeps a digit after
; a name as it is): 0 or 1, else Syntax error. The base it has already: nothing
; more. Another: Duplicate Definition if OPTION BASE has set one before, or if
; there is an array (ARYTAB not STREND); else OPTVAL, and OPTFLG set.
OPTIONP:        JSR     SYNCHR
                .byte   'B'
                JSR     SYNCHR
                .byte   'A'
                JSR     SYNCHR
                .byte   'S'
                JSR     SYNCHR
                .byte   'E'
                LDA     (TXTPTR)       ; the base
                SEC
                SBC     #'0'
                BCS     :+
                JMP     SNERR
:               CMP     #2             ; only 0 and 1
                BCC     :+
                JMP     SNERR
:               STA     RCH
                LDA     OPTVAL
                CMP     RCH
                BEQ     OLDOPT         ; the base it has
                LDA     OPTFLG         ; OPTION BASE seen before?
                BEQ     :+
                JMP     DDERR
:               LDA     ARYTAB         ; any arrays yet?
                CMP     STREND
                BNE     @dd
                LDA     ARYTAB+1
                CMP     STREND+1
                BEQ     :+
@dd:            JMP     DDERR
:               LDA     RCH
                STA     OPTVAL
                INC     A              ; nonzero
                STA     OPTFLG
OLDOPT:         JMP     CHRGTR         ; GWMAIN.ASM 3363: the terminator

; --- RANDOMIZE (GWMAIN.ASM 3383) ----------------------------------------------------------

; RANDOMIZE n: n as an integer (-32768..32767, else Overflow) in RNDX+1 and
; RNDX+2, then RNDMN2 (RND(1)'s step). RANDOMIZE alone: "Random number seed
; (-32768 to 32767)? " and a line typed (QINLIN), read by $FIN; anything
; after the number asks again. The text pointer is kept on the 6502's stack,
; as the 8086 keeps BX on its own; C set back for a ^C (QINLIN's C: OODOS's
; ^C breaks inside it, res/oem.s, so it comes back clear).
RANDOMP:        BEQ     INPRG          ; no argument: ask
                JSR     FRMEVL
                LDA     TXTPTR+1
                PHA
                LDA     TXTPTR
                PHA
                JSR     FRCINT         ; normal integers
                BRA     STRNDS
; INPRG (GWMAIN.ASM 3389)
INPRG:          LDA     TXTPTR+1
                PHA
                LDA     TXTPTR
                PHA
; INPRAG (GWMAIN.ASM 3390)
INPRAG:         LDA     #<RANMES
                STA     PTR
                LDA     #>RANMES
                STA     PTR+1
                JSR     STROUT
                JSR     QINLIN         ; TXTPTR at BUFMIN
                BCC     :+
                PLA                    ; ^C: the text pointer back, STPEND
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                SEC
                RTS
:               INCW    TXTPTR         ; past BUFMIN to BUF
                FJSR    FIN            ; a number
                LDA     (TXTPTR)       ; the terminator: only the end
                BNE     INPRAG
                JSR     FRCINT
; STRNDS (GWMAIN.ASM 3405)
STRNDS:         LDA     RBL            ; the seed: FRCINT's BX
                STA     RNDX+1
                LDA     RBH
                STA     RNDX+2
                JSR     RNDMN2
                PLA                    ; the text pointer back
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                CLC
                RTS

; --- FRMQNT (GWMAIN.ASM 3076) -------------------------------------------------------------

; A formula, then FRQINT (P1's: 32768..65535 taken as such), the value in
; DX: FRCINT's BX, which the 8086 exchanges into DX.
FRMQNT:         JSR     FRMEVL
                FJSR    FRQINT
                LDA     RBL
                STA     RDL
                LDA     RBH
                STA     RDH
                RTS

.rodata
RANMES:         .byte   "Random number seed (-32768 to 32767)", 0 ; GWMAIN.ASM 3412

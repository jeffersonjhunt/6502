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
; The far call rule (translate-7b-common.md): ON's chosen GOTO or GOSUB is
; started by JMP GONE2, a tail call, as the 8086's is: the statement's RTS
; returns through FARCALL to the stub, whose RTS goes to NEWSTT. RESUME's
; JMP DATA is one too. RESTRP's JMP ERRESM and the errors reset both stacks.

.include "gwbasic.inc"

;@entry ONGOTOP
;@entry RESUMEP
;@entry ERRORSP
.export ONGOTOP, RESUMEP, ERRORSP
.import CHRGTR, SYNCHR, LINGET, LINGT2, FNDLIN, GOTO2, GONE2, DATA, GETBYT
.import ERROR, ERRESM, USERR, REERR, FCERR, ADVERR, ONEFLG, ERRTXT, ONGOTP

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

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM's ON, RESUME and ERROR statements, in block P3 (hw/oodos.md,
; "The plan, phase 7"), each reached by its resident stub (res/far3.s):
;
;   ONGOTOP   ON ERROR GOTO line (and GOTO 0, which in the handler forces
;             the error again: RESTRP), ON n GOTO and ON n GOSUB (NTONGS,
;             NTONG1, ISGOSU, LOOPON); ON event GOSUB (step e's: NTOERR,
;             ONGSB0, RSTRAP, with p3/gwsts.s's ONGOTP and SETGSB)
;   RESUMEP   RESUME, RESUME 0, RESUME NEXT, RESUME line
;   ERRORSP   ERROR n
;
; and step a2's:
;
;   OPTIONP   OPTION BASE 0 or 1
;   RANDOMP   RANDOMIZE n, and RANDOMIZE alone, which asks for the seed
;   FRMQNT    a formula as 0..65535 in DX (CLEAR's GETMPM, p3/bimisc.s)
;
; and step c's:
;
;   AUTOP     AUTO [n][, [i]]: AUTLIN, AUTINC and AUTFLG set for MAIN
;   MAINA     MAIN's AUTO part, before a line is read: the line number
;             offered, with "*" if the line exists
;   AUTAST    MAINBX's AUTO part: the "*" after the number skipped
;   RESEQP    RENUM [new][, [old][, inc]] (SCCLIN and SCCPTR, P2's, turn
;             the line numbers in the text into pointers and back)
;
; and step e's:
;
;   RTNTRP    RETURN's part for an event's GOSUB entry (GWMAIN.ASM 2172):
;             the stop taken off its trap (RSTTRP), if the trap is on; the
;             resident RETURN calls it, since the flag is in P3's TRPTBL
;
; The far call rule (translate-7b-common.md): ON's chosen GOTO or GOSUB is
; started by JMP GONE2, a tail call, as the 8086's is: the statement's RTS
; returns through FARCALL to the stub, whose RTS goes to NEWSTT. RESUME's
; JMP DATA is one too. RESTRP's JMP ERRESM and the errors reset both stacks.
; RANDOMIZE returns C set for a ^C at its prompt (the 8086's JMP STPEND),
; which the stub takes to STPEND, as INPUT's is. AUTO and RENUM return, and
; their stubs drop NEWSTT's return and go to MAIN and READY, as the 8086's
; POP CX / JMP MAIN and SCCALL's STPRDY do (a jump from here would leave
; the far call's frame on the 6502's stack).

.include "gwbasic.inc"

;@entry ONGOTOP
;@entry RESUMEP
;@entry ERRORSP
;@entry OPTIONP
;@entry RANDOMP
;@entry AUTOP
;@entry MAINA
;@entry AUTAST
;@entry RESEQP
;@entry RTNTRP
.export ONGOTOP, RESUMEP, ERRORSP, OPTIONP, RANDOMP, FRMQNT
.export AUTOP, MAINA, AUTAST, RESEQP, RTNTRP
.import CHRGTR, SYNCHR, LINGET, LINGT2, FNDLIN, GOTO2, GONE2, DATA, GETBYT
.import BUF, PRELOD
.import ERROR, ERRESM, USERR, REERR, FCERR, ONEFLG, ERRTXT, ONGOTP, SETGSB, RSTTRP
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
; NTOERR (GWMAIN.ASM 2384): ON event GOSUB line (ONGOTP, p3/gwsts.s: C
; clear, CH the event's id, CL the line numbers it takes, 1): each line's
; link set as its event's GOSUB line (SETGSB), line 0 clearing it; a line not
; there is Undefined line number, more lines than CL Syntax error. The 8086
; keeps CX and its count on its stack round LINGET and FNDLN1: ONGCX and
; ONGCNT here. Not an event (C set): ON's numeric form, NTONGS.
NTOERR:         JSR     ONGOTP         ; ON event GOSUB?
                BCS     NTONGS         ; not an event trap
                LDA     RCL            ; save the event's id (CH) and the
                STA     ONGCX          ; lines it may have (CL)
                LDA     RCH
                STA     ONGCX+1
                JSR     CHRGTR         ; skip over the event's last token
                JSR     SYNCHR
                .byte   GOSUB_TK       ; ON event GOSUB nn
                LDA     #0             ; AL: the line number count
; ONGSB0 (GWMAIN.ASM 2394)
ONGSB0:         CMP     ONGCX          ; within the range of traps allowed?
                BCC     :+
                JMP     SNERR
:               STA     ONGCNT
                JSR     LINGET
                LDA     RDL            ; line 0?
                ORA     RDH
                BEQ     RSTRAP         ; yes: just reset the trap's line
                JSR     FNDLIN         ; FNDLN1 (TXTPTR kept): C set, found
                LDA     RCL            ; DX: the line's link (C kept)
                STA     RDL
                LDA     RCH
                STA     RDH
                BCS     RSTRAP
                JMP     USERR          ; the line isn't there
; RSTRAP (GWMAIN.ASM 2415)
RSTRAP:         LDA     ONGCNT         ; the event: CH + the count
                CLC
                ADC     ONGCX+1
                JSR     SETGSB         ; its GOSUB line, DX
                DECW    TXTPTR
                JSR     CHRGTR
                BNE     :+
                RTS                    ; the statement's end
:               JSR     SYNCHR
                .byte   ','
                LDA     ONGCNT         ; the next
                INC     A
                BRA     ONGSB0
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

; --- RETURN's event part (GWMAIN.ASM 2172) ----------------------------------------------

; RTNTRP: from the resident RETURN (res/gwmain.s RTRNOK), FORPTR at the
; GOSUB entry's flag address, which isn't 0 (an ON event GOSUB's: GOSUB2).
; The 8086's POP BX, and if the trap is on (F_TRPO), RSTTRP: the stop GOTRP
; put on it taken off, so a request kept meanwhile is taken now.
F_TRPO          = T_ON                 ; GWMAIN.ASM 2172: after a trap's GOSUB, the trap
F_TRPS          = T_STOP               ; is stopped; RETURN clears the stop
RTNTRP:         LDA     (FORPTR)       ; BX: the trap's flag
                STA     RBL
                LDY     #1
                LDA     (FORPTR),Y
                STA     RBH
                LDA     (RBL)          ; is the trap on?
                AND     #F_TRPO
                BEQ     :+
                JMP     RSTTRP         ; yes: reset its stop (a tail call)
:               RTS

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

; --- AUTO (GWMAIN.ASM 2546) ------------------------------------------------------------------

; AUTO: the first line 10 unless given (LINSPC: n, or "." for DOT), the
; increment 10 unless given after a comma; a comma alone, AUTINC's old one.
; An increment of 0 is Illegal function call; anything after it, Syntax
; error. Then AUTINC, AUTFLG (the increment's two bytes ORed, nonzero, as
; the 8086's AL) and AUTLIN, and back: the stub drops NEWSTT's return and
; goes to MAIN. Z on entry is CHRGTR's: the statement's end. The first line
; is kept in AUFST (P3's), where the 8086 keeps it on its stack.
AUTOP:          PHP                    ; (the flags kept round the moves)
                LDX     #10            ; DX = 10
                STX     RDL
                STZ     RDH
                STX     AUFST          ; PUSH DX: the first line, 10
                STZ     AUFST+1
                PLP
                BEQ     SNGAUT         ; AUTO alone: 10, 10
                FJSR    LINSPC         ; the first line (Z: the end)
                PHP
                LDA     RDL            ; XTHL: it kept, and BX = 10
                STA     AUFST
                LDA     RDH
                STA     AUFST+1
                LDA     #10
                STA     RBL
                STZ     RBH
                PLP
                BEQ     SNGAU1         ; AUTO n: an increment of 10
                JSR     SYNCHR         ; a comma must follow
                .byte   ','
                PHP
                LDA     AUTINC         ; DX: the old increment
                STA     RDL
                LDA     AUTINC+1
                STA     RDH
                PLP
                BEQ     SNGAUT         ; AUTO n,: the old one
                JSR     LINGET         ; the increment
                BEQ     SNGAUT
                JMP     SNERR          ; should have ended
; SNGAUT (GWMAIN.ASM 2563): BX = DX
SNGAUT:         LDA     RDL
                STA     RBL
                LDA     RDH
                STA     RBH
; SNGAU1 (GWMAIN.ASM 2564)
SNGAU1:         LDA     RBH            ; an increment of 0: Illegal function call
                ORA     RBL
                BNE     :+
                JMP     FCERR
:               LDX     RBL
                STX     AUTINC
                LDX     RBH
                STX     AUTINC+1
                STA     AUTFLG         ; AUTO from MAIN on
                LDA     AUFST          ; POP BX: AUTLIN, the first line
                STA     AUTLIN
                LDA     AUFST+1
                STA     AUTLIN+1
                RTS

; MAINA (GWMAIN.ASM 609, MAIN's AUTO part): with AUTFLG set, AUTLIN
; offered as the line's start, then "*" if that line exists (FNDLIN's C)
; or a space, and AUTFLG becomes that character, for AUTAST. OO-1: GW-BASIC
; prints the number (LINPRT) and the character (OUTDO) on the screen, and
; its screen editor reads them back with the line. On OODOS's console the
; line comes from function 10, so they go in BUF as its preload instead
; (PRELOD, as ERREDT's line does): function 10 shows them at once and they
; begin the line read, as the screen's do; and they can be rubbed out,
; which on the PC is moving the cursor over them. LINOUT gives the number's
; digits (its blank left out, as LINPRT's isn't shown either).
MAINA:          LDA     AUTLIN
                STA     RBL
                LDA     AUTLIN+1
                STA     RBH
                FJSR    LINOUT         ; at FBPTR
                LDX     #0
                LDY     #0
@n:             LDA     (FBPTR),Y
                BEQ     @f
                CMP     #' '
                BEQ     :+
                STA     BUF,X
                INX
:               INY
                BRA     @n
@f:             PHX
                LDA     AUTLIN
                STA     RDL
                LDA     AUTLIN+1
                STA     RDH
                JSR     FNDLIN         ; does it exist?
                PLX
                LDA     #'*'           ; C set: it does
                BCS     AUTELN
                LDA     #' '
; AUTELN (GWMAIN.ASM 621)
AUTELN:         STA     BUF,X          ; OUTDO's character, into BUF
                STZ     BUF+1,X
                STA     AUTFLG         ; whether there is a "*"
                LDA     #1
                STA     PRELOD
                RTS

; AUTAST (GWMAIN.ASM 644, MAINBX's AUTO part, the project's name): A
; AUTFLG (nonzero: the resident MAIN tests it). With a "*" offered and the
; line read having it after the number, it is skipped (INXHRT).
AUTAST:         CMP     #'*'
                BNE     @r             ; AUTO did not append an asterisk
                CMP     (TXTPTR)
                BNE     @r
                INCW    TXTPTR
@r:             RTS

; --- RENUM (GWMAIN.ASM 3108 RESEQ) ----------------------------------------------------------

; RESEQP: RENUM [new][, [old][, inc]]: the lines from old on (0, all of
; them, if not given) numbered from new (10) by inc (10). "." for new or
; old is DOT (LINSPC). Illegal function call for an increment of 0, for a
; new number at or below a line kept (the first line numbered new or more
; would come before the first line renumbered), and for a last number past
; 65529; all checked before anything changes. Then SCCLIN turns each line
; number in the text into a pointer to its line (saying "Undefined line"
; for one not there, which keeps its number), the lines get their new
; numbers, and SCCPTR turns the pointers back into the lines' numbers, the
; new ones (SCCALL). The 8086 keeps new, the increment and the first line
; on its stack; here they are RSNN, RSINC and RSPTR (P3's), and new is CX,
; old DX, while the arguments are read.
RESEQP:         PHP                    ; Z: CHRGTR's, the statement's end
                LDX     #10            ; CX = 10: new, and the increment
                STX     RCL
                STX     RSINC
                STZ     RCH
                STZ     RSINC+1
                STZ     RDL            ; DX = 0: old, all the lines
                STZ     RDH
                PLP
                BEQ     RESNN          ; RENUM alone: 10 by 10
                CMP     #','
                BEQ     EATCOM         ; no new: 10
                FJSR    LINSPC         ; new
                PHP
                LDA     RDL
                STA     RCL
                LDA     RDH
                STA     RCH
                STZ     RDL            ; DX back: 0
                STZ     RDH
                PLP
                BEQ     RESNN
; EATCOM (GWMAIN.ASM 3122)
EATCOM:         JSR     SYNCHR
                .byte   ','
                FJSR    LINSPC         ; old
                BEQ     RESNN
                JSR     SYNCHR         ; (the old increment dropped)
                .byte   ','
                LDA     RDH            ; old kept
                PHA
                LDA     RDL
                PHA
                JSR     LINGET         ; the increment
                BEQ     :+
                JMP     SNERR          ; should have ended
:               LDA     RDL            ; 0: Illegal function call
                ORA     RDH
                BNE     :+
                JMP     FCERR
:               LDA     RDL
                STA     RSINC
                LDA     RDH
                STA     RSINC+1
                PLA
                STA     RDL
                PLA
                STA     RDH
; RESNN (GWMAIN.ASM 3141): CX new, DX old.
RESNN:          LDA     RCL
                STA     RSNN
                LDA     RCH
                STA     RSNN+1
                JSR     FNDLIN         ; old's line, or the one after it
                LDA     RCL
                STA     RSPTR
                LDA     RCH
                STA     RSPTR+1
                LDA     RSNN           ; new's
                STA     RDL
                LDA     RSNN+1
                STA     RDH
                JSR     FNDLIN
                LDA     RCL            ; new's at or after old's, or Illegal
                CMP     RSPTR          ; function call: the program can't be
                LDA     RCH            ; renumbered on top of itself
                SBC     RSPTR+1
                BCS     :+
                JMP     FCERR
:               LDA     RSPTR          ; BX: the first line to renumber
                STA     PTR
                LDA     RSPTR+1
                STA     PTR+1
                LDA     RSNN           ; DX: the line numbers counted up
                STA     RDL
                LDA     RSNN+1
                STA     RDH
; NXTRSL (GWMAIN.ASM 3175): the last new number found, before anything
; changes. Each line's link; at the end's zero link, done; and none added
; for the last line (the one whose next is the end).
NXTRSL:         LDY     #1             ; the link
                LDA     (PTR)
                TAX
                ORA     (PTR),Y
                BEQ     RESSD1         ; zero: done
                LDA     (PTR),Y
                STA     PTR+1
                STX     PTR
                LDA     (PTR)          ; the next line's link
                ORA     (PTR),Y
                BEQ     RESSD1
; NXTRSC (GWMAIN.ASM 3164)
NXTRSC:         CLC                    ; one increment more
                LDA     RDL
                ADC     RSINC
                STA     RDL
                LDA     RDH
                ADC     RSINC+1
                STA     RDH
                BCC     :+
                JMP     FCERR          ; the increment was too large
:               LDA     #<65529        ; past 65529: the same
                CMP     RDL
                LDA     #>65529
                SBC     RDH
                BCS     NXTRSL
                JMP     FCERR
; RESSD1 (GWMAIN.ASM 3191): the numbers to pointers, then the lines
; numbered.
RESSD1:         FJSR    SCCLIN
                LDA     RSPTR
                STA     PTR
                LDA     RSPTR+1
                STA     PTR+1
                LDA     RSNN
                STA     RDL
                LDA     RSNN+1
                STA     RDH
; RESNX1 (GWMAIN.ASM 3197)
RESNX1:         LDY     #1             ; the link: zero at the end
                LDA     (PTR)
                TAX
                ORA     (PTR),Y
                BEQ     SCCALL
                LDA     (PTR),Y
                PHA
                INY                    ; the new number
                LDA     RDL
                STA     (PTR),Y
                INY
                LDA     RDH
                STA     (PTR),Y
                CLC                    ; and the next
                LDA     RDL
                ADC     RSINC
                STA     RDL
                LDA     RDH
                ADC     RSINC+1
                STA     RDH
                PLA                    ; on to the next line
                STA     PTR+1
                STX     PTR
                BRA     RESNX1
; SCCALL (GWMAIN.ASM 3214): the pointers back to numbers; the stub goes to
; READY (STPRDY).
SCCALL:         FJSR    SCCPTR
                RTS

.rodata
RANMES:         .byte   "Random number seed (-32768 to 32767)", 0 ; GWMAIN.ASM 3412

.segment "BRAM"
ONGCX:          .res    2              ; ON event GOSUB: CL, CH (the 8086 keeps CX on its stack)
ONGCNT:         .res    1              ; and the line numbers so far (its AX)
RSNN:           .res    2              ; RENUM: the first new number (the 8086's NN)
RSINC:          .res    2              ; its increment (INC)
RSPTR:          .res    2              ; the first line renumbered (old's)
AUFST:          .res    2              ; AUTO: its first line, till the rest is read

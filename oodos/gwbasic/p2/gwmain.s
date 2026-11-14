; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM's program entry and errors, in block P2 (hw/oodos.md, "The
; plan, phase 7"):
;
;   CRUNCH         a line typed (BUF) to tokens (KBUF): the reserved words
;                  (p2/ibmres.s), numbers through P1's $FIN as embedded
;                  constants, line numbers after GOTO and the rest, &H and
;                  &O, "?" for PRINT, ' for :REM', DATA's and REM's text
;                  and strings kept as typed
;   EDENT2         a numbered line put in the program, replacing one with
;                  its number, or deleting it if empty; then CHEAD and FINI
;   LINKER CHEAD   the links put right, from the start or a line
;   SCNLIN LINSPC  a line range (LIST, DELETE); a line number or "."
;   SCCPTR         the line pointers in the text back to line numbers
;   SCCLIN         and line numbers to pointers, for RENUM (p3/gwmain.s
;                  RESEQP): "Undefined line 100 in 20" for one not there
;   ERRPRT ERRFIN  an error's message and " in 20" (and BIMISC.ASM's
;   BRKMSG INPRT   "Break"); MATH1.ASM's INPRT
;   ERREDT         after a Syntax error, its line to edit (SCNEDT.ASM)
;   EDITP          EDIT n, the same way (SCNEDT.ASM's EDIT, reached by
;                  res/gwmain.s's stub)
;
; CRUNCH's text pointer is TXTPTR (the 8086's BX), as CHRGTR, LINGET and
; $FIN want it; its deposit pointer and count are KDP and KCNT (DX, CX), and
; what the 8086 pushes while it tries a word is in KSRC, KRP and KCH.

.include "gwbasic.inc"

;@entry CRUNCH
;@entry EDENT2
;@entry LINKER
;@entry ERRPRT
;@entry BRKMSG
;@entry ERREDT
;@entry SCNLIN
;@entry LINSPC
;@entry SCCLIN
;@entry SCCPTR
;@entry EDITP
.export CRUNCH, EDENT2, LINKER, CHEAD, ERRPRT, ERRFIN, BRKMSG, ERREDT, SCNLIN
.export SCCLIN, EDITP
.export LINSPC, SCCPTR, DEPTR, INPRT, NOTRFN, RENCRN, KLOOP
.import CHRGTR, CHRGT2, LINGET, FNDLIN, BAKSP, MAKUPL, MAKUPS, ISLET2, OCTCNS
.import STROUT, OUTDO, CRDO, SYNCHR, ERROR, SNERR, USERR, BLTU, CONCH2
.import CRDONZ, FCERR
.import BUF, KBUF, MEMSIZ, DOT, FINI, SYSTME
.import ALPTAB, SPCTAB, LINRES, CRUNCX, TSTANM, BUFLN2
.import ERRTAB, INTXT, BRKTXT, PRELOD

ERRSN           = 2                    ; GWDATA.ASM's error numbers
ERRLBO          = 23                   ; Line buffer overflow
ERRUE           = 21                   ; Unprintable error
NONDSK          = 30                   ; the last of the errors before the disk's
DSKERR          = 49                   ; less one than the first disk error
LSTERR          = 75                   ; one past the last
DSKER1          = DSKERR - NONDSK
ERRUE1          = ERRUE + DSKER1

.code

; --- CRUNCH (GWMAIN.ASM 948) -------------------------------------------------------------

; TXTPTR at the line's first character. Returns TXTPTR at KBUF-1 (as GONE
; wants it), and KCNT the line's length in the program: its link, number,
; tokens and zero. Three zeros end KBUF: a line's end, and a zero link so a
; direct line ends as a program does. Line buffer overflow past KBFLEN.
CRUNCH:         STZ     DONUM          ; numbers, not line numbers
                STZ     DORES          ; crunching allowed
                LDA     #<(KBFLEN-3)
                STA     KCNT
                LDA     #>(KBFLEN-3)
                STA     KCNT+1
                LDA     #<KBUF
                STA     KDP
                LDA     #>KBUF
                STA     KDP+1
KLOOP:          LDA     (TXTPTR)
                BNE     NCRDON
CRDONE:         SEC                    ; KCNT = KBFLEN+2 - KCNT: the length plus 5
                LDA     #<(KBFLEN+2)
                SBC     KCNT
                STA     KCNT
                LDA     #>(KBFLEN+2)
                SBC     KCNT+1
                STA     KCNT+1
                LDA     #<(KBUF-1)
                STA     TXTPTR
                LDA     #>(KBUF-1)
                STA     TXTPTR+1
                LDA     #0
                LDY     #2
@z:             STA     (KDP),Y
                DEY
                BPL     @z
                RTS
NCRDON:         CMP     #'"'
                BNE     :+
                JMP     STRNG
:               CMP     #' '
                BEQ     STUFFH
                LDX     DORES          ; in DATA: as typed
                BNE     STUFFH
                JMP     NTDATA
STUFFH:         INCW    TXTPTR
                PHA
                JSR     KRNSAV
                PLA
                SEC
                SBC     #':'
                BEQ     COLIS          ; a colon: crunching again
                CMP     #DATCO
                BNE     NODATT
                LDA     #1             ; DATA: no crunching, line numbers allowed
COLIS:          STA     DORES
                STA     DONUM
NODATT:         SEC
                SBC     #REMCO
                BNE     KLOOP
                STA     KTRM           ; REM: the rest as typed (terminator 0)
STR1:           LDA     (TXTPTR)
                BEQ     CRDONE
                CMP     KTRM
                BEQ     STUFFH         ; the closing quote
                BRA     STRNG2
STRNG:          STA     KTRM           ; a string: to its closing quote
STRNG2:         INCW    TXTPTR
                JSR     KRNSAV
                BRA     STR1

; NTDATA: not a string, a space, or DATA's. A character with bit 7 set is
; dropped; "?" is PRINT; a letter starts a reserved word or a name.
NTDATA:         INCW    TXTPTR
                ORA     #0
                BPL     :+
                JMP     KLOOP
:               DECW    TXTPTR         ; (DECW takes A)
                LDA     (TXTPTR)
                CMP     #'?'
                BNE     :+
                LDA     #PRINT_TK
                JMP     NOTFN2
:               JSR     MAKUPL
                JSR     ISLET2
                BCC     :+
                JMP     TSTNUM
:               LDA     TXTPTR         ; GO TO and GO SUB with any spaces
                STA     KSRC
                LDA     TXTPTR+1
                STA     KSRC+1
                LDA     #<GOSTEX
                LDX     #>GOSTEX
                JSR     CHKRES
                BNE     NOTGOS
                JSR     CHRGTR
                LDA     #<TOTEX
                LDX     #>TOTEX
                JSR     CHKRES
                BNE     :+
                LDA     #GOTO_TK
                BRA     GPUTRS
:
                LDA     #<SUBTEX
                LDX     #>SUBTEX
                JSR     CHKRES
                BNE     NOTGOS
                LDA     #GOSUB_TK
GPUTRS:         JMP     NOTFN2
; CHKRES: the text at TXTPTR against the word at A/X, to its zero: Z set if
; it is there (TXTPTR past it), Z clear if not (TXTPTR where it differs).
CHKRES:         STA     KRP
                STX     KRP+1
@l:             LDA     (KRP)
                BNE     :+
                RTS
:               STA     KCH
                JSR     MAKUPL
                CMP     KCH
                BEQ     :+
                RTS
:               INCW    TXTPTR
                INCW    KRP
                BRA     @l

; NOTGOS: the letter's table (ALPTAB), each word tried from the character
; after the first.
NOTGOS:         LDA     KSRC
                STA     TXTPTR
                LDA     KSRC+1
                STA     TXTPTR+1
                JSR     MAKUPL
                SEC
                SBC     #'A'
                ASL     A
                TAX
                LDA     ALPTAB,X
                STA     KRP
                LDA     ALPTAB+1,X
                STA     KRP+1
                INCW    TXTPTR
TRYAGA:         LDA     TXTPTR
                STA     KSRC
                LDA     TXTPTR+1
                STA     KSRC+1
LOPPSI:         JSR     MAKUPL
                STA     KCH
                LDA     (KRP)
                AND     #$7F
                BNE     :+
                JMP     NOTRES         ; the end of this letter's words
:               INCW    TXTPTR
                CMP     KCH
                BNE     LOPSKP
                LDA     (KRP)
                INCW    KRP
                ORA     #0
                BPL     LOPPSI         ; more of the word
                LDA     KCH            ; TAB( and SPC(: anything may follow
                CMP     #'('
                BEQ     ISRESW
                LDA     (KRP)          ; and FN and USR
                CMP     #FN_TK
                BEQ     ISRESW
                CMP     #USR_TK
                BEQ     ISRESW
                JSR     MAKUPL         ; a letter, digit or "." after it: a name
                CMP     #'.'
                BEQ     NOTRES
                JSR     TSTANM
                BCC     NOTRES
ISRESW:         LDA     (KRP)          ; the token
                BPL     :+
                JMP     NOTFNT         ; a statement or operator
:               ORA     #$80           ; a function: $FF and its token
                PHA
                LDA     #$FF
; RENCRN: A, then the byte on the 6502's stack; numbers again (CRUNCX's way
; in too).
RENCRN:         JSR     KRNSAV
                STZ     DONUM
                PLA
                JSR     KRNSAV
                JMP     KLOOP
LOPSKP:         LDA     KSRC           ; the next word
                STA     TXTPTR
                LDA     KSRC+1
                STA     TXTPTR+1
@sk:            LDA     (KRP)
                INCW    KRP
                ORA     #0
                BPL     @sk
                INCW    KRP            ; past the token
                BRA     TRYAGA
NOTRES:         JMP     CRUNCX         ; the extended words (p2/ibmres.s)

; NOTFNT: a statement's or operator's token (TXTPTR back one, as STUFFH
; steps over it). NOTFN2: A is to be stored. After the words of LINRES, a
; number is a line number; ELSE has a colon before it; WHILE a "+" after
; it, so its formula doesn't start with a constant; ' is :REM' and the rest
; as typed.
NOTFNT:         PHA                    ; (DECW takes A)
                DECW    TXTPTR
                PLA
NOTFN2:         PHA
                STA     KCH
                LDX     #0
NOTFN3:         LDA     LINRES,X
                BEQ     NOTFN4
                INX
                CMP     KCH
                BNE     NOTFN3
                LDA     #1             ; NOTRS2: line numbers
                BRA     NOTRS6
NOTFN4:         LDA     #0
NOTRS6:         STA     DONUM
                PLA
                CMP     #ELSE_TK
                BNE     :+
                PHA
                JSR     KRNSVC
                PLA
:               CMP     #WHILE_TK
                BNE     CKSNGQ
                JSR     KRNSAV
                LDA     #PLUSTK
CKSNGQ:         CMP     #SNGQTK
                BEQ     :+
                JMP     NTSNGT
:               PHA
                JSR     KRNSVC
                LDA     #REM_TK
                JSR     KRNSAV
                PLA
                STZ     KTRM           ; to the end of the line
                JMP     STRNG2

; TSTNUM: a digit or ".": a line number (DONUM 1; a "." is EDIT's and
; LIST's line), a number by $FIN (DONUM 0), or part of a name (DONUM $FF).
TSTNUM:         LDA     (TXTPTR)
                CMP     #'.'
                BEQ     NUMTRY
                CMP     #'9'+1
                BCS     SRCSPC
                CMP     #'0'
                BCC     SRCSPC
NUMTRY:         LDX     DONUM
                BPL     :+
                JMP     STUFFH
:               BEQ     FLTGET
                CMP     #'.'
                BNE     :+
                JMP     STUFFH
:               LDA     #LINCON
                JSR     KRNSAV
                JSR     LINGET
                JSR     BAKSP
SAVINT:         LDA     RDL            ; DX, low then high
                STA     RBL
                LDA     RDH
                STA     RBH
SAVI:           LDA     RBL            ; BX, low then high
                JSR     KRNSAV
                LDA     RBH
POPSTF:         JSR     KRNSAV
                JMP     KLOOP
; FLTGET: the number to the FAC by $FIN; 0-9 as ONECON+n, 10-255 as IN2CON
; and a byte, a larger integer INTCON and two, a single SNGCON and four, a
; double DBLCON and eight.
FLTGET:         FJSR    FIN
                JSR     BAKSP
                LDA     VALTYP
                CMP     #VT_INT
                BNE     NTINTG
                LDA     FACLO+1
                BNE     NTINTG2
                LDA     FACLO
                STA     RBH
                LDX     #IN2CON
                STX     RBL
                CMP     #10
                BCS     SAVI
                CLC
                ADC     #ONECON
                BRA     POPSTF
NTINTG2:        LDA     #VT_INT
NTINTG:         PHA
                LSR     A              ; 2, 4, 8: INTCON, SNGCON, DBLCON
                CLC
                ADC     #INTCON - 1
                JSR     KRNSAV
                LDX     #FACLO
                PLA
                TAY
                CMP     #VT_DBL
                BNE     MOVCON
                LDX     #DFACL
MOVCON:         LDA     0,X
                JSR     KRNSAV
                INX
                DEY
                BNE     MOVCON
                JMP     KLOOP

; SRCSPC: a special character's token (SPCTAB), which ends a name; or the
; character itself (a control character as a space, but tab and line feed).
SRCSPC:         LDX     #0
SRCSP2:         LDA     SPCTAB,X
                AND     #$7F
                BEQ     NOTRS5
                INX
                CMP     (TXTPTR)
                BEQ     :+
                INX
                BRA     SRCSP2
:               LDA     SPCTAB,X
                BRA     NOTRS1
NOTRS5:         LDA     (TXTPTR)
                CMP     #' '
                BCS     NOTRS1
                CMP     #9
                BEQ     NOTRS1
                CMP     #10
                BEQ     NOTRS1
                LDA     #' '
NOTRS1:         PHA
                LDA     DONUM          ; after a name: numbers again
                INC     A
                BEQ     :+
                DEC     A
:               JMP     NOTRS6

; NTSNGT: "&": &H or &O and its value (OCTCNS), as an integer; anything
; else as it is.
NTSNGT:         CMP     #'&'
                BEQ     :+
                JMP     STUFFH
:               LDA     TXTPTR
                PHA
                LDA     TXTPTR+1
                PHA
                JSR     CHRGTR
                TAX
                PLA
                STA     TXTPTR+1
                PLA
                STA     TXTPTR
                TXA
                JSR     MAKUPS
                CMP     #'H'
                BEQ     :+
                LDA     #OCTCON
                BRA     WUZOCT
:               LDA     #HEXCON
WUZOCT:         JSR     KRNSAV
                JSR     OCTCNS
                JMP     SAVINT

; KRNSAV: A into KBUF; KRNSVC: a colon. Line buffer overflow when it's
; full. X and Y kept.
KRNSVC:         LDA     #':'
KRNSAV:         STA     (KDP)
                INCW    KDP
                LDA     KCNT
                BNE     :+
                DEC     KCNT+1
:               DEC     KCNT
                LDA     KCNT
                ORA     KCNT+1
                BEQ     LBOERR
                RTS
LBOERR:         LDA     #ERRLBO
                JMP     ERROR

; NOTRFN (GWMAIN.ASM 1373): a name, from its first letter, in capitals, to
; the first character that isn't a letter, digit or "."; DONUM $FF.
NOTRFN:         LDA     KSRC
                STA     TXTPTR
                LDA     KSRC+1
                STA     TXTPTR+1
                DECW    TXTPTR
                LDA     #$FF
                STA     DONUM
                JSR     MAKUPL
KRNVAR:         JSR     KRNSAV
                INCW    TXTPTR
                JSR     MAKUPL
                JSR     ISLET2
                BCC     KRNVAR
                CMP     #'9'+1
                BCS     JKLOOP
                CMP     #'0'
                BCS     KRNVAR
                CMP     #'.'
                BEQ     KRNVAR
JKLOOP:         JMP     KLOOP

; --- EDENT2 (GWMAIN.ASM 665) -------------------------------------------------------------

; A numbered line: DX its number, KCNT its length (CRUNCH's), TXTPTR at
; KBUF-1. The line with that number is taken out, if there is one; a line
; with nothing after the number deletes it (Undefined line number if there
; is none), else it goes in its place. The strings are dropped, so REASON
; doesn't count them; then CHEAD and FINI (resident: RUNC, MAIN). Out of
; memory from BLTU fixes the links itself (OMERR). (PROCHK and CHAIN's
; CHNFLG are 7c's.)
;
; AUTO's part (7b step c): every numbered line sets AUTO's next line,
; AUTLIN, to its number plus AUTINC, as the 8086 does whether AUTO is on or
; not; past 65529 (or a carry), AUTO ends (AUTRES). In AUTO, an empty line
; (the number offered and nothing typed) neither deletes a line nor gives
; Undefined line number: it goes straight to FINI.
EDENT2:         JSR     CHRGTR         ; the line empty? (KTRM 0)
                STA     KTRM
                LDA     RDL
                STA     DOT
                LDA     RDH
                STA     DOT+1
                CLC                    ; BX = AUTINC + DX
                LDA     AUTINC
                ADC     RDL
                STA     RBL
                LDA     AUTINC+1
                ADC     RDH
                STA     RBH
                BCS     AUTRES         ; the pathetic case: a carry
                LDA     RBL            ; 65530 or more: too big, AUTO ends
                CMP     #<65530
                LDA     RBH
                SBC     #>65530
                BCS     AUTRES
                LDA     RBL
                STA     AUTLIN
                LDA     RBH
                STA     AUTLIN+1
                BRA     AUTGOD
; AUTRES (GWMAIN.ASM 694)
AUTRES:         STZ     AUTFLG
AUTGOD:         JSR     FNDLIN         ; RCL: the line, or where it goes; PTR2 the next
                BCS     LEXIST
                LDA     KTRM
                BNE     LXISTS
                LDA     AUTFLG         ; not AUTO: nothing to delete
                BNE     AUTFIN
                JMP     USERR
LXISTS:         CLC                    ; in, nothing to take out
                BRA     INONLY
LEXIST:         LDA     KTRM           ; the line there: AUTO doesn't erase it for
                BNE     ERAINS         ; an empty line
                LDA     AUTFLG
                BEQ     ERAINS
AUTFIN:         LDA     RCL            ; PUSH CX, JMP FINI: the links from the line
                STA     KSRC
                LDA     RCH
                STA     KSRC+1
                JMP     FINI2
ERAINS:         SEC                    ; out, and in if not empty
INONLY:         PHP                    ; C: take the old one out
                LDA     RCH            ; the place
                PHA
                LDA     RCL
                PHA
                LDA     PTR2+1         ; and the next line
                PHA
                LDA     PTR2
                PHA
                JSR     DEPTR          ; line pointers back to numbers first
                PLA
                STA     RBL
                PLA
                STA     RBH
                PLA
                STA     RCL
                PLA
                STA     RCH
                PLP
                BCC     NODEL
                JSR     DEL            ; CX the line, BX the next
NODEL:          LDA     RCL            ; DX: the place, for CHEAD too
                STA     KSRC
                LDA     RCH
                STA     KSRC+1
                LDA     KTRM
                JEQ     FINI2          ; empty: deleted, done
                LDA     MEMSIZ         ; the strings dropped
                STA     FRETOP
                LDA     MEMSIZ+1
                STA     FRETOP+1
LEVFRE:         STZ     ONELIN         ; no ON ERROR, so Out of memory isn't trapped
                STZ     ONELIN+1
                LDA     KSRC           ; DX: the place
                STA     RDL
                LDA     KSRC+1
                STA     RDH
                LDA     VARTAB         ; CX: the old VARTAB
                STA     RCL
                LDA     VARTAB+1
                STA     RCH
                CLC                    ; BX: the new
                LDA     VARTAB
                ADC     KCNT
                STA     RBL
                LDA     VARTAB+1
                ADC     KCNT+1
                STA     RBH
                PHA
                LDA     RBL
                PHA
                JSR     BLTU
                PLA
                STA     VARTAB
                PLA
                STA     VARTAB+1
                LDA     KSRC           ; the line: a link that isn't zero, its number,
                STA     PTR            ; then its tokens and zero from KBUF
                LDA     KSRC+1
                STA     PTR+1
                LDY     #1
                STA     (PTR),Y
                INY
                LDA     DOT
                STA     (PTR),Y
                INY
                LDA     DOT+1
                STA     (PTR),Y
                SEC                    ; KCNT - 4 bytes
                LDA     KCNT
                SBC     #4
                STA     KCNT
                BCS     :+
                DEC     KCNT+1
:               CLC
                LDA     PTR
                ADC     #4
                STA     PTR
                BCC     :+
                INC     PTR+1
:               LDA     #<KBUF
                STA     PTR2
                LDA     #>KBUF
                STA     PTR2+1
MLOOPR:         LDA     (PTR2)
                STA     (PTR)
                INCW    PTR
                INCW    PTR2
                LDA     KCNT
                BNE     :+
                DEC     KCNT+1
:               DEC     KCNT
                LDA     KCNT
                ORA     KCNT+1
                BNE     MLOOPR
FINI2:          LDA     KSRC           ; FINI: the links from the place on
                STA     RDL
                LDA     KSRC+1
                STA     RDH
                JSR     CHEAD
                JMP     FINI

; DEL (GWLIST.ASM 816): the line at CX taken out, BX the next: the rest of
; the program moved down; VARTAB, ARYTAB and STREND where it ends (the
; variables go, as a replaced line may make BLTU collect).
DEL:            LDA     RCL
                STA     PTR
                LDA     RCH
                STA     PTR+1
                LDA     RBL
                STA     PTR2
                LDA     RBH
                STA     PTR2+1
MLOOP:          LDA     (PTR2)
                STA     (PTR)
                INCW    PTR
                INCW    PTR2
                LDA     PTR2
                CMP     VARTAB
                BNE     MLOOP
                LDA     PTR2+1
                CMP     VARTAB+1
                BNE     MLOOP
                LDA     PTR
                STA     VARTAB
                STA     ARYTAB
                STA     STREND
                LDA     PTR+1
                STA     VARTAB+1
                STA     ARYTAB+1
                STA     STREND+1
                RTS
.export DEL

; --- LINKER, CHEAD (GWMAIN.ASM 799) ------------------------------------------------------

; LINKER: from the start; CHEAD: from the line at DX. Each line's end found
; by its zero (stepping over embedded constants, which may hold one), and
; its link made to point past it, to the zero link at the end.
LINKER:         LDA     TXTTAB
                STA     RDL
                LDA     TXTTAB+1
                STA     RDH
CHEAD:          LDA     RDL
                STA     TXTPTR
                LDA     RDH
                STA     TXTPTR+1
                LDA     (TXTPTR)
                LDY     #1
                ORA     (TXTPTR),Y
                BNE     :+
                RTS
:               CLC                    ; to the byte before the text
                LDA     TXTPTR
                ADC     #3
                STA     TXTPTR
                BCC     CZLOOP
                INC     TXTPTR+1
CZLOOP:         INCW    TXTPTR
                LDA     (TXTPTR)
CZLOO2:         BEQ     CZLIN
                CMP     #DBLCON+1
                BCS     CZLOOP
                CMP     #11
                BCC     CZLOOP
                JSR     CHRGT2         ; the constant
                JSR     CHRGTR         ; and past it
                ORA     #0
                BRA     CZLOO2
CZLIN:          INCW    TXTPTR         ; the next line
                LDA     RDL
                STA     PTR
                LDA     RDH
                STA     PTR+1
                LDA     TXTPTR
                STA     (PTR)
                STA     RDL
                LDY     #1
                LDA     TXTPTR+1
                STA     (PTR),Y
                STA     RDH
                BRA     CHEAD

; --- SCNLIN, LINSPC (GWMAIN.ASM 842, 1950) -----------------------------------------------

; SCNLIN: a range, at A (CHRGTR's) and TXTPTR: n, n-, -n, n-m or none (a
; comma may end it: LIST's file, 7c). Returns RCL the first line in it (as
; FNDLIN finds it), LSTMAX its last number, TEMP the text pointer.
SCNLIN:         STZ     KSRC           ; the first: 0, if none
                STZ     KSRC+1
                BEQ     ALLLST
                CMP     #','
                BEQ     ALLLST
                JSR     LINSPC         ; the first
                PHP
                PHA
                LDA     RDL
                STA     KSRC
                LDA     RDH
                STA     KSRC+1
                PLA
                PLP
                BEQ     SNGLIN         ; n alone: the last is the first
                CMP     #','
                BEQ     SNGLIN
                JSR     SYNCHR
                .byte   MINUTK
                BEQ     ALLLST
                CMP     #','
ALLLST:         PHP                    ; to the end, unless a last is given
                LDA     #<65530
                STA     RDL
                LDA     #>65530
                STA     RDH
                PLP
                BEQ     SNGLIN
                JSR     LINSPC
                BEQ     SNGLIN
                CMP     #','
                BEQ     SNGLIN
                JMP     SNERR
SNGLIN:         LDA     TXTPTR
                STA     TEMP
                LDA     TXTPTR+1
                STA     TEMP+1
                LDA     RDL            ; the last
                STA     LSTMAX
                LDA     RDH
                STA     LSTMAX+1
FNDLN1:         LDA     KSRC           ; and the first's line
                STA     RDL
                LDA     KSRC+1
                STA     RDH
                JMP     FNDLIN

; LINSPC: a line number, or "." (DOT, the last line typed, listed or in an
; error).
LINSPC:         LDA     (TXTPTR)
                CMP     #'.'
                BEQ     :+
                JMP     LINGET
:               LDA     DOT
                STA     RDL
                LDA     DOT+1
                STA     RDH
                JMP     CHRGTR

; --- DEPTR, SCCPTR (GWMAIN.ASM 3316, 3221) -----------------------------------------------

; DEPTR: if GOTO and the rest have turned line numbers into pointers
; (PTRFLG), back to numbers, before the program moves. SCCPTR: each PTRCON
; becomes LINCON and its line's number. SCCLIN (7b step c), the other way,
; for RENUM: PTRFLG set, and each LINCON becomes PTRCON and a pointer to
; the zero before its line; one to a line not there is left as it is, with
; "Undefined line 100 in 20" (CRDONZ first; " in" the scanned line's
; number, SCNLN). The one special case: ON ERROR GOTO 0's 0 stays a
; LINCON, so RENUM doesn't change it. (The 8086 enters SCCLIN by skipping
; SCCPTR's XOR AL,AL, so AL is nonzero: here PTRCON, as GOTO2 sets it.)
DEPTR:          LDA     PTRFLG
                BNE     SCCPTR
                RTS
SCCLIN:         LDA     #PTRCON        ; nonzero: numbers to pointers
                BRA     SCCPT1
SCCPTR:         LDA     #0             ; pointers to numbers
SCCPT1:         STA     PTRFLG
SCNPGM:         LDA     TXTTAB
                STA     TXTPTR
                LDA     TXTTAB+1
                STA     TXTPTR+1
                DECW    TXTPTR
SCNPLN:         INCW    TXTPTR         ; the link
                LDA     (TXTPTR)
                INCW    TXTPTR
                ORA     (TXTPTR)
                BNE     :+
                RTS
:               INCW    TXTPTR         ; the line number (the 8086's DX)
                LDA     (TXTPTR)
                STA     SCNLN
                INCW    TXTPTR
                LDA     (TXTPTR)
                STA     SCNLN+1
SCNEXT:         JSR     CHRGTR
SCNEX2:         CMP     #0
                BEQ     SCNPLN
                LDX     PTRFLG         ; which way? (A kept)
                BEQ     SCNPT2         ; pointers to numbers
                CMP     #ERROR_TK      ; ERROR GOTO?
                BNE     NTERRG
                JSR     CHRGTR
                CMP     #GOTO_TK
                BNE     SCNEX2
                JSR     CHRGTR
                CMP     #LINCON
                BNE     SCNEX2
                JSR     CONDX          ; LINGT3: DX the line, then past it
                LDA     RDL            ; not line 0: a pointer
                ORA     RDH
                BNE     CHGPTR
                DECW    TXTPTR         ; SCNEX3: line 0 left a number
                BRA     SCNEXT
; CONDX: LINGT3's way (GWMAIN.ASM 1972): DX = CONLO, the constant CHRGTR has
; just read, before CHRGTR goes past it (and may read the next constant into
; CONLO: "GOTO 30 40"; review U2). Z and C as CHRGTR leaves them.
CONDX:          LDA     CONLO
                STA     RDL
                LDA     CONLO+1
                STA     RDH
                JMP     CHRGTR
; NTERRG (GWMAIN.ASM 3258)
NTERRG:         CMP     #LINCON
                BNE     SCNEXT
                JSR     CONDX          ; LINGT3: DX the line, then past it
; CHGPTR (GWMAIN.ASM 3262): TXTPTR, just past the constant, kept in KSRC.
CHGPTR:         LDA     TXTPTR
                STA     KSRC
                LDA     TXTPTR+1
                STA     KSRC+1
                JSR     FNDLIN         ; DX: the line (CONDX's)         ; C set: found, CX at it
                LDA     RCL            ; CX - 1: the zero before it (C kept)
                BNE     :+
                DEC     RCH
:               DEC     RCL
                LDA     #PTRCON
                BCS     MAKPTR
                JSR     CRDONZ         ; "Undefined line 100 in 20"
                LDA     #<LINM
                STA     PTR
                LDA     #>LINM
                STA     PTR+1
                JSR     STROUT
                LDA     RDL
                STA     RBL
                LDA     RDH
                STA     RBH
                FJSR    LINPRT
                LDA     SCNLN
                STA     RBL
                LDA     SCNLN+1
                STA     RBH
                JSR     INPRT
                BRA     SCNPOP
SCNPT2:         CMP     #PTRCON
                BNE     JSCNXT
                JSR     CONDX          ; LINGT3: DX the pointer, then past it
                LDA     TXTPTR
                STA     KSRC
                LDA     TXTPTR+1
                STA     KSRC+1
                CLC                    ; CX: its line's number
                LDA     RDL
                ADC     #3
                STA     PTR
                LDA     RDH
                ADC     #0
                STA     PTR+1
                LDA     (PTR)
                STA     RCL
                LDY     #1
                LDA     (PTR),Y
                STA     RCH
                LDA     #LINCON
MAKPTR:         LDX     CONTXT         ; CONCHG: at the constant's end, A the token
                STX     TXTPTR
                LDX     CONTXT+1
                STX     TXTPTR+1
                JSR     CONCH2
SCNPOP:         LDA     KSRC           ; on from the character after it
                STA     TXTPTR
                LDA     KSRC+1
                STA     TXTPTR+1
                DECW    TXTPTR
JSCNXT:         JMP     SCNEXT

; --- ERRPRT, ERRFIN (GWMAIN.ASM 511) -----------------------------------------------------

; ERRPRT: error A's message from ERRTAB (past the last, or one of its "?",
; Unprintable error; the disk errors, 50 on, follow the others in it), and
; ERRLIN. ERRFIN: the message at PTR, and line RBL; " in" and the line
; unless direct ($FFFF); start-up's own line 65534 ends GW-BASIC (SYSTME).
; Then the "not typed" mark, $FF, and back: the caller goes to READY.
ERRPRT:         CMP     #LSTERR
                BCS     UPERR
                CMP     #DSKERR+1
                BCS     NTDER2
                CMP     #NONDSK+1
                BCC     NTDERR
UPERR:          LDA     #ERRUE1
NTDER2:         SEC
                SBC     #DSKER1
NTDERR:         STA     KCH
                LDA     #<ERRTAB
                STA     PTR
                LDA     #>ERRTAB
                STA     PTR+1
LEPSKP:         LDA     (PTR)          ; past KCH messages
                INCW    PTR
                ORA     #0
                BNE     LEPSKP
                DEC     KCH
                BNE     LEPSKP
                LDA     ERRLIN
                STA     RBL
                LDA     ERRLIN+1
                STA     RBH
ERRFIN:         LDA     (PTR)
                CMP     #'?'           ; a gap in the table
                BNE     ERRFN1
                LDA     #ERRUE1
                BRA     NTDER2
ERRFN1:         JSR     STROUT
                LDA     RBL
                CMP     #<65534
                BNE     :+
                LDA     RBH
                CMP     #>65534
                BNE     :+
                JSR     CRDO
                JMP     SYSTME
:               LDA     RBL
                AND     RBH
                CMP     #$FF
                BEQ     :+
                JSR     INPRT
:               LDA     #$FF
                JMP     OUTDO

; BRKMSG (BIMISC.ASM 611, ENDCON's): "Break" and CURLIN.
BRKMSG:         LDA     #<BRKTXT
                STA     PTR
                LDA     #>BRKTXT
                STA     PTR+1
                LDA     CURLIN
                STA     RBL
                LDA     CURLIN+1
                STA     RBH
                BRA     ERRFIN

; INPRT (MATH1.ASM 2711): " in " and BX, by P1's LINPRT.
INPRT:          LDA     #<INTXT
                STA     PTR
                LDA     #>INTXT
                STA     PTR+1
                JSR     STROUT
                FJSR    LINPRT
                RTS

; --- ERREDT (SCNEDT.ASM 392) -------------------------------------------------------------

; After a Syntax error in a program line: that line to edit, as GW-BASIC's
; EDIT gives it (EREDIT). GW-BASIC lists it and leaves the cursor in it;
; here the line, as LIST shows it, is put in BUF (PRELOD) for the next line
; read, which function 10 shows and lets be edited as if just typed. The
; caller goes on to MAIN.
ERREDT:         STZ     ERRFLG
                LDA     ERRLIN         ; DX: the line
                STA     RDL
                LDA     ERRLIN+1
                STA     RDH
ERRED2:         LDA     RDL            ; direct, or 0: nothing
                AND     RDH
                CMP     #$FF
                BEQ     ERRED3
                LDA     RDL
                ORA     RDH
                BNE     EREDIT
ERRED3:         RTS

; EDITP (SCNEDT.ASM 321 EDIT): EDIT n or EDIT ., which must end the
; statement (else Illegal function call); then EREDIT, as for ERREDT: the
; line into BUF to edit, or Undefined line number. The source doesn't check
; for a program: EDIT in one ends it, and MAIN offers the line. EREDIT
; returns; the resident stub drops NEWSTT's return and goes to MAIN, as
; EREDIT's POP BX and JMP MAIN do.
EDITP:          JSR     LINSPC         ; DX
                BEQ     EREDIT
                JMP     FCERR          ; EFCERR
EREDIT:
                LDA     RDL
                STA     DOT
                LDA     RDH
                STA     DOT+1
                JSR     FNDLIN
                BCS     :+
                JMP     USERR
:               LDA     RCL            ; the line's number, as LINPRT prints it
                STA     PTR
                LDA     RCH
                STA     PTR+1
                CLC                    ; the text, at the line + 4, kept
                LDA     RCL            ; (LINOUT takes CX)
                ADC     #4
                STA     KSRC
                LDA     RCH
                ADC     #0
                STA     KSRC+1
                LDY     #2
                LDA     (PTR),Y
                STA     RBL
                INY
                LDA     (PTR),Y
                STA     RBH
                FJSR    LINOUT         ; at FBPTR
                LDX     #0
                LDY     #0
@n:             LDA     (FBPTR),Y
                BEQ     @sp
                CMP     #' '
                BEQ     :+
                STA     BUF,X
                INX
:               INY
                BRA     @n
@sp:            LDA     KSRC
                STA     TXTPTR
                LDA     KSRC+1
                STA     TXTPTR+1
                LDA     (TXTPTR)
                CMP     #9             ; a space unless it starts with a tab
                BEQ     :+
                LDA     #' '
                STA     BUF,X
                INX
:               TXA                    ; BUFLIN from there
                CLC
                ADC     #<BUF
                STA     KDP
                LDA     #>BUF
                ADC     #0
                STA     KDP+1
                STX     KCNT
                SEC
                LDA     #BUFLEN
                SBC     KCNT
                STA     KCNT
                JSR     BUFLN2
                LDA     #1
                STA     PRELOD
                RTS

.rodata
GOSTEX:         .byte   "GO ", 0       ; CHKRES's words
TOTEX:          .byte   "TO", 0
SUBTEX:         .byte   "UB", 0
LINM:           .byte   "Undefined line ", 0 ; GWMAIN.ASM 3286

.segment "BRAM"
SCNLN:          .res    2              ; SCNPLN: the line being scanned (the 8086's DX)

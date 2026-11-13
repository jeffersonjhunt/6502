; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM's LINE INPUT, INPUT and READ, translated, in block P1
; (hw/oodos.md, "The plan, phase 7"):
;
;   LINEP           LINE INPUT [;]["prompt";]v$: the line as it was typed
;   INPUTP          INPUT [;]["prompt";|,]v[,v...]: a line typed, checked
;                   whole first (pass 1: each value's type and the count,
;                   nothing assigned), then assigned (pass 2); a bad line is
;                   "?Redo from start" and the statement again
;   READP           READ v[,v...]: from DATA, DATPTR on; a value READ can't
;                   take is Syntax error in the DATA's line
;   QTINP           the prompt
;   SCNVAL          a value from the data: a string (quoted, or to a comma,
;                   or to a colon in DATA) or a number ($FIN)
;
; The resident core's INPUT, LINE and READ (res/gwmain.s) far call these.
; The 8086 holds two text pointers, the variable list's and the data's (the
; line typed, or the DATA statements), swapping them with XTHL; every
; routine reads at TXTPTR, so here XTHL swaps TXTPTR with OTHPTR. What the
; 8086 keeps on its stack is in this block's RAM: the list's start
; (VLSTRT; pass 2's data starts at BUFMIN), the variable (INPVAR), the
; value's type (INPTYP). A return with C set is INPUT's ^C (STPEND, the
; resident caller's). INPUT # and LINE INPUT # (the files) are 7c's, and the
; graphics LINE 7e's: Advanced Feature.

.include "gwbasic.inc"

;@entry INPUTP
;@entry LINEP
;@entry READP
.export INPUTP, LINEP, READP
.import CHRGTR, SYNCHR, SNERR, ERROR, ADVERR, SCNSEM, SINLIN, STRLTI, STRLT2
.import STRLT3, STRPRT, PTRGET, PTRGTN, CHKSTR, INPCOM, GETYP, FREFAC, FIN
.import FINDBL, OVCSTR, FLGSCN, ISFLIO, DATA, DATPTR, DATLIN, DATSNE, RESFIN
.import OUTDO, STROUT, FINPRT, BUFMIN, SUBFLG

ERROD           = 4                    ; Out of DATA

.code

; --- LINE INPUT (GWMAIN.ASM 2636) ------------------------------------------------

LINEP:          CMP     #INPUT_TK      ; LINE alone: GLINE, the graphics' (7e)
                BEQ     :+
                JMP     ADVERR
:               JSR     SYNCHR
                .byte   INPUT_TK
                CMP     #'#'           ; DLINE, a file's (7c)
                BNE     :+
                JMP     ADVERR
:               JSR     SCNSEM
                JSR     QTINP
                JSR     PTRGET         ; the string to set
                JSR     CHKSTR
                LDA     PTR
                STA     INPVAR
                LDA     PTR+1
                STA     INPVAR+1
                LDA     TXTPTR         ; the text pointer, kept
                STA     OTHPTR
                LDA     TXTPTR+1
                STA     OTHPTR+1
                JSR     SINLIN         ; TXTPTR at BUFMIN
                BCS     XTHLC          ; ^C: STPEND
RELINE:         STZ     RCH            ; the line a literal, to its zero
                JSR     STRLT3
                JSR     XTHL           ; the text pointer back
                LDA     INPVAR         ; LETCN2: the assignment
                STA     PTR
                LDA     INPVAR+1
                STA     PTR+1
                LDA     #VT_STR
                JSR     INPCOM
                CLC
                RTS
XTHLC:          JSR     XTHL
                SEC
                RTS

; --- INPUT (GWMAIN.ASM 2713) -----------------------------------------------------

INPUTP:         CMP     #'#'           ; FILSTI, a file's (7c)
                BNE     :+
                JMP     ADVERR
:               JSR     SCNSEM
                JSR     QTINP
NOTQTI:         LDA     TXTPTR         ; the variable list
                STA     VLSTRT
                LDA     TXTPTR+1
                STA     VLSTRT+1
GETAGN:         LDA     INPQM          ; "? " unless the prompt had a comma
                BEQ     SUPPRS
                LDA     #'?'
                JSR     OUTDO
                LDA     #' '
                JSR     OUTDO
SUPPRS:         JSR     SINLIN         ; TXTPTR at BUFMIN
                BCC     :+
                LDA     VLSTRT         ; ^C: STPEND
                STA     TXTPTR
                LDA     VLSTRT+1
                STA     TXTPTR+1
                RTS
; Pass 1, as ANSI asks: no value assigned before the types and the count are
; checked. The list is scanned without its subscripts (SUBFLG $80; PTRGTN
; makes no variable), each value scanned (SCNVAL with INPPAS 0).
:               STZ     FLGINP         ; INPUT (SCNVAL's strings end at no ":")
                LDA     #','           ; a comma before the data
                STA     (TXTPTR)
                LDA     TXTPTR         ; the data's pointer, pass 1's
                STA     OTHPTR
                LDA     TXTPTR+1
                STA     OTHPTR+1
                LDA     VLSTRT
                STA     TXTPTR
                LDA     VLSTRT+1
                STA     TXTPTR+1
                DECW    TXTPTR
VARLOP:         LDA     #$80           ; no subscripts: back at the "("
                STA     SUBFLG
                JSR     CHRGTR
                JSR     PTRGTN
                DECW    TXTPTR         ; the terminator rescanned (DECW uses A)
                LDY     #1
                LDA     (TXTPTR),Y
                CMP     #'['
                BEQ     SCNBKT
                CMP     #'('
                BNE     ENDSCN
SCNBKT:         INCW    TXTPTR         ; the subscripts skipped, by their brackets
                STZ     PARCNT
SCNOPN:         INC     PARCNT
SCNCON:         JSR     CHRGTR
                BNE     :+
                JMP     SNERR          ; the statement ends inside them
:               CMP     #'"'
                BEQ     SCNSTR
                CMP     #'('
                BEQ     SCNOPN
                CMP     #'['           ; (a "[" isn't counted: the 8086's)
                BEQ     SCNCON
                CMP     #']'
                BEQ     LEFPRN
                CMP     #')'
                BNE     SCNCON
LEFPRN:         DEC     PARCNT
                BNE     SCNCON
ENDSCN:         JSR     CHRGTR         ; the variable's end: the list's, or a comma
                BEQ     OKVLST
                CMP     #','
                BEQ     OKVLST
                JMP     SNERR
SCNSTR:         INCW    TXTPTR         ; a string in a subscript, to its quote
                LDA     (TXTPTR)
                BNE     :+
                JMP     SNERR
:               CMP     #'"'
                BNE     SCNSTR
                BRA     SCNCON
OKVLST:         JSR     XTHL           ; the data: a comma, then the value
                LDA     (TXTPTR)
                CMP     #','
                BNE     INPBAK
                LDA     #1
                STA     OVCSTR
                STA     FLGSCN         ; $FIN's overflows print, and come back
                JSR     SCNVAL
                DEC     FLGSCN
                LDA     OVCSTR         ; an overflow printed: again
                DEC     A
                BNE     INPBAK
                JSR     GETYP          ; a string's temporary freed
                BNE     :+
                JSR     FREFAC
:               DECW    TXTPTR         ; past spaces after it
                JSR     CHRGTR
                JSR     XTHL
                LDA     (TXTPTR)       ; more variables?
                CMP     #','
                JEQ     VARLOP
                LDA     OTHPTR         ; no: the data must end too
                STA     TXTPTR
                LDA     OTHPTR+1
                STA     TXTPTR+1
                DECW    TXTPTR
                JSR     CHRGTR
                BNE     RDOINP
                LDA     #<BUFMIN       ; pass 2, from the data's start
                STA     TXTPTR
                LDA     #>BUFMIN
                STA     TXTPTR+1
INPCN3:         LDA     #','
                STA     (TXTPTR)
INPCON:         STZ     FLGINP         ; the list in TXTPTR, the data in OTHPTR
                LDA     TXTPTR
                STA     OTHPTR
                LDA     TXTPTR+1
                STA     OTHPTR+1
                LDA     VLSTRT
                STA     TXTPTR
                LDA     VLSTRT+1
                STA     TXTPTR+1
                BRA     LOPDAT

; INPBAK, RDOIN2, RDOINP (GWMAIN.ASM 2687): the data badly formed: "?Redo
; from start", and back to NEWSTT at the statement's start, to run INPUT
; again. TRMNOK: a READ's value badly formed: Syntax error in its DATA.
TRMNOK:         LDA     FLGINP
                BEQ     RDOINP
                JMP     DATSNE
INPBAK:
RDOIN2:
RDOINP:         LDA     #<TRYAGN
                STA     PTR
                LDA     #>TRYAGN
                STA     PTR+1
                JSR     STROUT
                LDA     SAVTXT
                STA     TXTPTR
                LDA     SAVTXT+1
                STA     TXTPTR+1
                CLC
                RTS

; --- READ (GWMAIN.ASM 2866) ------------------------------------------------------

; READ: the list in TXTPTR, DATPTR's data in OTHPTR; FLGINP 1. Both READ and
; INPUT's pass 2 go on at LOPDAT, the data pointer always at a terminator
; (",", ":" or a line's end) before each value.
READP:          LDA     TXTPTR
                STA     OTHPTR
                LDA     TXTPTR+1
                STA     OTHPTR+1
                LDA     #1
                STA     FLGINP
                LDA     DATPTR
                STA     TXTPTR
                LDA     DATPTR+1
                STA     TXTPTR+1
                JSR     XTHL
                BRA     LOPDAT
LOPDT2:         JSR     SYNCHR
                .byte   ','
LOPDAT:         JSR     PTRGET         ; the next variable
                JSR     XTHL           ; to the data
                LDA     PTR
                STA     INPVAR
                LDA     PTR+1
                STA     INPVAR+1
                LDA     (TXTPTR)       ; a comma: a value follows
                CMP     #','
                BEQ     DATBK
                LDA     FLGINP         ; READ: the next DATA statement
                BEQ     DATBK
                JMP     DATLOP
DATBK:          LDA     #1             ; pass 2: SCNVAL assigns
                STA     INPPAS
                JSR     SCNVL1
STRDN2:         DECW    TXTPTR         ; the value must end at a terminator
                JSR     CHRGTR
                BEQ     TRMOK
                CMP     #','
                BNE     TRMNOK
TRMOK:          JSR     XTHL           ; to the list: more?
                DECW    TXTPTR
                JSR     CHRGTR
                BNE     LOPDT2
                LDA     FLGINP         ; the end: READ keeps its place in DATPTR
                BEQ     FINPRG
                LDA     OTHPTR
                LDX     OTHPTR+1
                JSR     RESFIN
                CLC
                RTS
FINPRG:         JSR     FINPRT
                CLC
                RTS

; DATLOP, DATFND, NOWLIN (GWMAIN.ASM 3036): the next DATA statement, by
; DATA's own skip; each line's number to DATLIN, for DATSNE. None left: Out
; of DATA.
DATLOP:         JSR     DATA
DATFND:         CMP     #0
                BNE     NOWLIN
                INCW    TXTPTR         ; the next line's link
                LDA     (TXTPTR)
                INCW    TXTPTR
                ORA     (TXTPTR)
                BNE     :+
                LDA     #ERROD
                JMP     ERROR
:               INCW    TXTPTR
                LDA     (TXTPTR)
                STA     DATLIN
                INCW    TXTPTR
                LDA     (TXTPTR)
                STA     DATLIN+1
NOWLIN:         JSR     CHRGTR         ; the statement: DATA?
                CMP     #DATA_TK
                BNE     DATLOP
                BRA     DATBK

; --- SCNVAL (GWMAIN.ASM 2916) ----------------------------------------------------

; A value at TXTPTR+1 for the variable PTRGET (or PTRGTN) last found,
; VALTYP its type. A string: quoted, to its quote; else to a comma, or, in
; DATA, a colon, its trailing spaces dropped (STRLT2 with CH ","). Unquoted
; it may be all digits: only the variable's type decides. A number: $FIN
; ($FINDB for a double). DOASIG: in pass 2 (INPPAS not 0), assigned to
; INPVAR as LET assigns (INPCOM). A file's value (FILIND) is 7c's.
SCNVAL:         STZ     INPPAS         ; pass 1: scanned only
SCNVL1:         JSR     ISFLIO
                BEQ     :+
                JMP     ADVERR
:               JSR     GETYP
                STA     INPTYP
                BNE     NUMINS
                JSR     CHRGTR         ; a string
                STA     RDH            ; DH and CH: quoted, the quote
                STA     RCH
                CMP     #'"'
                BEQ     NOWGET
                LDA     FLGINP         ; else a comma, and in DATA a colon
                STA     RDH
                BEQ     NCOLST
                LDA     #':'
                STA     RDH
NCOLST:         LDA     #','
                STA     RCH
                DECW    TXTPTR         ; its first character is in it
NOWGET:         JSR     STRLT2
                BRA     DOASIG
NUMINS:         JSR     CHRGTR
                LDA     INPTYP
                CMP     #VT_DBL - 3
                BCS     :+
                JSR     FIN
                BRA     DOASIG
:               JSR     FINDBL
DOASIG:         LDA     INPPAS
                BNE     :+
                RTS                    ; pass 1
:               LDA     INPVAR
                STA     PTR
                LDA     INPVAR+1
                STA     PTR+1
                LDA     INPTYP         ; the variable's VALTYP
                CLC
                ADC     #3
                JMP     INPCOM

; --- QTINP (GWMAIN.ASM 2721) -----------------------------------------------------

; A prompt: a string, then ";" ("? " after it) or "," (no "? "). INPQM is
; the 8086's TEMPA+1: nonzero for "? ".
QTINP:          LDX     #$FF
                STX     INPQM
                CMP     #'"'
                BEQ     :+
                RTS
:               JSR     STRLTI
                CMP     #','
                BNE     NTICMA
                STZ     INPQM
                JSR     CHRGTR
                BRA     INPCMA
NTICMA:         JSR     SYNCHR
                .byte   ';'
INPCMA:         JMP     STRPRT

; XTHL: TXTPTR and OTHPTR swapped.
XTHL:           LDA     TXTPTR
                LDX     OTHPTR
                STA     OTHPTR
                STX     TXTPTR
                LDA     TXTPTR+1
                LDX     OTHPTR+1
                STA     OTHPTR+1
                STX     TXTPTR+1
                RTS

.rodata
TRYAGN:         .byte   "?Redo from start", 13, 10, 0

.segment "BRAM"
; GWDATA.ASM's, and what the 8086 keeps on its stack
OTHPTR:         .res    2              ; the other text pointer (XTHL's)
VLSTRT:         .res    2              ; INPUT's variable list
INPVAR:         .res    2              ; the variable a value goes to
INPTYP:         .res    1              ; its VALTYP-3
FLGINP:         .res    1              ; 0 INPUT, else READ
INPPAS:         .res    1              ; 0 pass 1, else pass 2
INPQM:          .res    1              ; "? " after the prompt (the 8086's TEMPA+1)
PARCNT:         .res    1              ; the brackets open (the 8086's CH)

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; DSKCOM.ASM's reading of sequential files, translated, in block P4, the file
; layer (hw/oodos.md, "The plan, phase 7": 7c):
;
;   FILIND          INPUT #'s value (P1's SCNVAL, for a file): an item read
;                   into BUF and made the value, as INPUT makes one typed
;   DLINE           LINE INPUT #f, v$: a line read into v$
;   INPDOR          the item's reading (the two's), STRCHR a byte kept
;   FIXINPP         INPUT$(n[, [#]f]): n bytes from the file, or typed
;
; A whole item comes back to P1 in one far call (V1: the device interface
; takes runs): its bytes go into BUF here, then STRLT2 (resident) or $FIN
; (P1's, a far call back) makes the value, as the 8086's QUIT2B does. The
; 8086 keeps the data pointer (BUFMIN's comma, for INPUT #; the text, for
; LINE INPUT #) on its stack round the item; here it is INPTXT, and BX, the
; place in BUF, is RBL/RBH; DH and DL, the item's two terminators, RDH and
; RDL; CH, the room left, RCH.

.include "gwbasic.inc"

;@entry FILIND
;@entry DLINE
.export FILIND, DLINE, FIXINPP
.import GETYP, CHRGTR, SYNCHR, PTRGET, CHKSTR, STRLT2, INPCOM, FINPRT, GETBYT
.import STRINI, PUTNEW, FCERR, BUF, INCHR, INDSKC, BAKCHR, FILINP, FILSCN, FILSET
.import SETPTR, INCHRI, DERFNO, DERBFM, DERRPE

.code

; --- FILIND, DLINE (DSKCOM.ASM 120) -------------------------------------------------------

; FILIND: the variable's value from PTRFIL's file. A number ends at a space
; or a comma (DX: CON1, DH "," DL " "), a string at a comma (both ","): back
; to P1's DOASIG, which assigns it.
FILIND:         LDA     #' '
                STA     RDL
                LDA     #','
                STA     RDH
                JSR     GETYP
                BNE     INPDOR         ; a number
                LDA     RDH            ; a string: both terminators ","
                STA     RDL
                BRA     INPDOR

; DLINE (133): LINE INPUT #f, v$: the file (FILINP, past its comma), the
; string variable, and the line, to a CR (DX 0: no terminator else), into
; it as LET puts it (LETCON); then the console again (FINPRT).
DLINE:          JSR     FILINP
                JSR     PTRGET
                JSR     CHKSTR
                LDA     PTR            ; PUSH DX: the variable
                STA     DLVAR
                LDA     PTR+1
                STA     DLVAR+1
                STZ     RDH
                STZ     RDL
                JSR     INPDOR
                LDA     DLVAR          ; LETCON: the assignment
                STA     PTR
                LDA     DLVAR+1
                STA     PTR+1
                LDA     #VT_STR
                JSR     INPCOM
                JMP     FINPRT

; INPDOR (145): an item from PTRFIL's file. Leading spaces are skipped
; (LINE INPUT's kept). A quoted item (a string's, DL ",") runs to its quote,
; CRs, LFs and commas in it kept. Else it runs to a terminator, a CR (its LF
; eaten), or the file's end; an LF is kept in a string read by LINE INPUT
; (dropped in an unquoted string, and in a number, with a CR after an LF);
; a zero byte is never kept. 255 bytes end it at once. After a quote or a
; space, the spaces after it and one comma, or a CR and its LF, are skipped
; (a byte that isn't is backed up). Then the item, in BUF to a zero, made
; the value: $FIN's for a number (DL " "), else a literal (STRLT2: CH DH, DH
; 0), and TXTPTR back where it was.
INPDOR:         LDA     TXTPTR         ; PUSH BX: the data pointer
                STA     INPTXT
                LDA     TXTPTR+1
                STA     INPTXT+1
NOTNWT:         JSR     INCHR
                CMP     #' '           ; spaces first skipped
                BNE     NOTSPC
                LDX     RDH            ; but not LINE INPUT's (DH 0)
                BNE     NOTNWT
NOTSPC:         CMP     #'"'
                BNE     NOTQTE
                LDX     RDL            ; a quote begins a string's item
                CPX     #','
                BNE     NOTQTE
                STA     RDH            ; terminators: the quote alone
                STA     RDL
                JSR     INDSKC         ; past it
                BCS     QUITSI
NOTQTE:         LDX     #<BUF          ; BX: the item's bytes
                STX     RBL
                LDX     #>BUF
                STX     RBH
                LDX     #255           ; CH: room for 255
                STX     RCH
LOPCRS:         LDX     RDH            ; quoted: CR and LF kept (the 8086 keeps
                CPX     #'"'           ; the byte in CL round the test)
                BEQ     NOTQTL
                CMP     #13            ; a CR ends it (its LF eaten)
                BEQ     ICASLF
                CMP     #10
                BNE     NOTQTL
LPISLF:         LDX     RDL            ; an LF: kept unless an unquoted string's
                CPX     #','
                BEQ     :+
                JSR     STRCHR
:               JSR     INDSKC
                BCS     QUITSI
                CMP     #10            ; (5/14/82: LFs one after another)
                BEQ     LPISLF
                CMP     #13
                BNE     NOTQTL
                LDX     RDL            ; a CR after LFs: a number's, an
                CPX     #' '           ; unquoted string's, is dropped
                BEQ     LPCRGT
                CPX     #','
                BEQ     LPCRGT
NOTQTL:         CMP     #0             ; a zero: never kept (STRLT2's end)
                BEQ     LPCRGT
                CMP     RDH
                BEQ     QUITSI
                CMP     RDL
                BEQ     QUITSI
                JSR     STRCHR
LPCRGT:         JSR     INDSKC
                BCC     LOPCRS
QUITSI:         CMP     #'"'           ; after a quote: spaces, then a comma
                BEQ     MORSPC         ; or a CR LF, skipped
                CMP     #' '           ; after a space too
                BNE     NOSKCR
MORSPC:         JSR     INDSKC
                BCS     NOSKCR
                CMP     #' '
                BEQ     MORSPC
                CMP     #','
                BEQ     NOSKCR
                CMP     #13
                BNE     BAKUPT
ICASLF:         JSR     INDSKC
                BCS     NOSKCR
                CMP     #10
                BEQ     NOSKCR
BAKUPT:         JSR     BAKCHR         ; not part of it: backed up
NOSKCR:
QUIT2B:         LDA     #0             ; the item's end
                STA     (RBL)
                LDA     #<(BUF-1)      ; the item at BUF
                STA     TXTPTR
                LDA     #>(BUF-1)
                STA     TXTPTR+1
                LDA     RDL            ; a number?
                CMP     #' '
                BEQ     NUMIMK
                LDA     RDH            ; CH: DH ("," drops trailing spaces)
                STA     RCH
                STZ     RDH
                JSR     STRLT2
                BRA     INPDRX
NUMIMK:         JSR     GETYP          ; the variable's type: C set for a double
                PHP
                JSR     CHRGTR         ; the first byte
                PLP
                BCS     :+
                FJSR    FIN            ; single or integer
                BRA     INPDRX
:               FJSR    FINDBL         ; double
INPDRX:         LDA     INPTXT         ; POP BX: the data pointer back
                STA     TXTPTR
                LDA     INPTXT+1
                STA     TXTPTR+1
                RTS

; STRCHR (277): A kept in BUF at BX, unless it is zero. The 255th ends the
; item (the caller's return dropped: QUIT2B).
STRCHR:         CMP     #0
                BNE     :+
                RTS
:               STA     (RBL)
                INCW    RBL
                DEC     RCH
                BEQ     :+
                RTS
:               PLA
                PLA
                BRA     QUIT2B

; --- FIXINP (DSKCOM.ASM 777) --------------------------------------------------------------

; INPUT$(n[, [#]f]), from EVAL at INPUT: n bytes (1-255: 0 is Illegal
; function call) from file f, which mustn't be for output (Bad file mode;
; not open: DERFNO), at its end Input past end; or, with no file, n keys
; typed, not echoed (INCHRI), every byte as it comes. PTRFIL is kept round
; it (PRINT #2, INPUT$(3, #1) prints to file 2): FIXPTR, the 8086's stack.
; FIXTTY: nonzero reads keys; FIXCNT, CL, the count left; PTR2 the place.
FIXINPP:        JSR     CHRGTR
                JSR     SYNCHR
                .byte   '$'
                JSR     SYNCHR
                .byte   '('
                LDA     PTRFIL         ; PTRFIL kept, then 0
                STA     FIXPTR
                LDA     PTRFIL+1
                STA     FIXPTR+1
                LDA     #0
                TAX
                JSR     SETPTR
                JSR     GETBYT         ; the count
                STA     FIXCNT
                LDA     #$FF           ; the keyboard
                STA     FIXTTY
                LDA     (TXTPTR)
                CMP     #','
                BNE     REDTTY
                JSR     CHRGTR
                JSR     FILSCN
                BNE     :+
                JMP     DERFNO
:               CMP     #MD_SQO
                BNE     :+
                JMP     DERBFM
:               JSR     FILSET
                STZ     FIXTTY         ; a file
REDTTY:         JSR     SYNCHR
                .byte   ')'
                LDA     FIXCNT
                BNE     :+
                JMP     FCERR
:               JSR     STRINI         ; room for them: DX
                LDA     RDL
                STA     PTR2
                LDA     RDH
                STA     PTR2+1
FIXLOP:         LDA     FIXTTY
                BEQ     DSKCHR
                JSR     INCHRI
PUTCHR:         STA     (PTR2)
                INCW    PTR2
                DEC     FIXCNT
                BNE     FIXLOP
                LDA     FIXPTR         ; PTRFIL back (SAVKEY, the second byte of
                LDX     FIXPTR+1       ; a two-byte key, is 7d's)
                JSR     SETPTR
                JMP     PUTNEW
DSKCHR:         JSR     INDSKC
                BCC     PUTCHR
                JMP     DERRPE

.segment "BRAM"
INPTXT:         .res    2              ; INPDOR's data pointer
DLVAR:          .res    2              ; DLINE's variable
FIXPTR:         .res    2              ; INPUT$'s PTRFIL, kept
FIXCNT:         .res    1
FIXTTY:         .res    1

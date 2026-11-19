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
;   LOADP LOADP2    LOAD name[,R] and RUN name[,R] (LRUN3 to NOTRN1; then
;                   NOTINI to BINLOD, MAINGO): the home block's stubs go on
;                   (p8/far4.s), as the far call rule needs; CHAIN's way in,
;                   CHNENT, is LOADP's ",R" (p4/fiveo.s CHAINP)
;   MERGEP          MERGE name (MERGE, OKGETM, OKGET2)
;   SAVEP           SAVE name[,A|,P] (SAVE, GOODSV, ENDOFS)
;   FIELDP          FIELD [#]n, w AS v$, ...: strings in a random file's FIELD
;                   buffer
;   (LSETP, RSETP   LSET and RSET: P3's since the IBM spec's fix-up, p3/dskcom.s)
;   (CVI to MKD$, FUNDXP: P3's since the fix-up's review round 1, p3/dskcom.s)
;
; (DSKCOM.ASM's LOAD is LOADP here: the resident LOAD is GW-BASIC's loader,
; res/loader.s, whose name the tests read.)
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
;@entry LOADP
;@entry LOADP2
;@entry MERGEP
;@entry SAVEP
;@entry FIELDP
;@entry LSETR
;@entry RSETR
;@entry FUNDXR
.export FILIND, DLINE, FIXINPP, LOADP, LOADP2, MERGEP, SAVEP, FIELDP, LSETR, RSETR, FUNDXR
.export CHNENT, OKGETM, PRGFLI
.import GETDAT, DERFOV, GTBYTC, FRMEQL, GETSPA, DOCNVF, VMOVE, VMOVFM
.import GETYP, CHRGTR, SYNCHR, PTRGET, CHKSTR, STRLT2, INPCOM, FINPRT, GETBYT
.import STRINI, PUTNEW, FCERR, BUF, INCHR, INDSKC, BAKCHR, FILINP, FILSCN, FILSET
.import SETPTR, INCHRI, DERFNO, DERBFM, DERRPE
.import PRGFIL, SCDASC, FSTLOD, BINSAV, PRGFIN, RUNFLG, PROSAV, PDECOD, SNERR, FRMEVL, FRESTR

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
; eaten), or the file's end; an LF is kept in a string read by LINE INPUT,
; and ends a number or an unquoted string (OO-1: the IBM manual's pp.
; 133-134; the source drops it and goes on, LPISLF's 5/14/82 fix); a zero
; byte is never kept. 255 bytes end it at once. After a quote or a
; space, the spaces after it and one comma, or a CR and its LF, are skipped
; (a byte that isn't is backed up). Then the item, in BUF to a zero, made
; the value: $FIN's for a number (DL " "), else a literal (STRLT2: CH DH, DH
; 0), and TXTPTR back where it was.
INPDOR:         LDA     TXTPTR         ; PUSH BX: the data pointer
                STA     INPTXT
                LDA     TXTPTR+1
                STA     INPTXT+1
NOTNWT:         JSR     INCHR
                LDX     RDH            ; not LINE INPUT's (DH 0)
                BEQ     NOTSPC
                CMP     #' '           ; spaces first skipped (OO-1, review round
                BEQ     NOTNWT         ; 1, F1: and CRs and LFs, the IBM manual's
                CMP     #13            ; pp. 133-134)
                BEQ     NOTNWT
                CMP     #10
                BEQ     NOTNWT
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
LPISLF:         LDX     RDL            ; an LF: it ends INPUT #'s item (OO-1, the
                BNE     NOSKCR         ; IBM manual's pp. 133-134); LINE INPUT's keeps it
                JSR     STRCHR
                JSR     INDSKC
                BCS     QUITSI
                CMP     #10            ; (5/14/82: LFs one after another)
                BEQ     LPISLF         ; (a CR after them kept, as the source's)
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

; --- LOAD, RUN name (DSKCOM.ASM 290) -----------------------------------------------------

; LOADP: LRUN3's first part, X its flag (0 LOAD: the files closed, not run;
; 1 RUN: closed, run; the 8086's stack): file 0 opened for input (PRGFLI);
; ",R" makes the flag $81 (CHNENT's 201O: the files left open, run), and
; nothing else may follow. NOTRN1: RUNFLG the flag, NLONLY the flag or 1
; (file 0 not closed; with $81, none). Then the stub's SCRTCH. CHNENT
; (DSKCOM.ASM 332): CHAIN's way in, file 0 already open (p4/fiveo.s), the
; ",R" case's flag; then CHAIN's stub's SCRTCH, as LOAD's.
LOADP:          STX     RUNFLG
                JSR     PRGFLI
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     NOTRN1         ; NOTRNL: the flag as it was
                JSR     SYNCHR
                .byte   ','
                JSR     SYNCHR
                .byte   'R'
                BEQ     :+
                JMP     SNERR          ; that must be the end
:
CHNENT:         LDA     #$81
                STA     RUNFLG
NOTRN1:         LDA     RUNFLG
                ORA     #1
                STA     NLONLY
                RTS

; PRGFLI (290): file 0 opened for input.
PRGFLI:         LDA     #MD_SQI
                JMP     PRGFIL

; LOADP2: from NOTINI on, after the stub's SCRTCH: the first byte. None: X
; 0, MAIN (which meets the end at once). 254: a protected program (PROFLG);
; then, as 255, BINLOD. Else MAINGO: the byte backed up, the file text
; (SCDASC), X 0: MAIN reads its lines as typed (DSKCHP).
; BINLOD (381): the program to TXTTAB (FSTLOD), VARTAB at its end, decoded
; if protected (PROLOD, PDECOD), the links made (LINKER, P2's), VARTAB past
; the end's zero link; X 1, A RUNFLG: the stub's RUNC, FINPRT and READY or
; NEWSTT, or for a CHAIN (CHNFLG) CHNRET (the stub's). OO-1 (W2): the end
; is found by the links, as LINKER finds it, among the bytes read: twelve
; zeros put after them (in the 86 bytes FSTLOD leaves below FRETOP: no
; string is there after SCRTCH) end any line and the chain there, however a
; constant runs on; if the chain's end isn't below them, the program didn't
; fit: X 2, the stub's OUTLOD (SCRTCH, Out of memory). The 8086 says so
; from the read alone.
LOADP2:         JSR     INDSKC
                BCS     LDMAIN
                CMP     #254
                BNE     NTPROL
                STA     PROFLG
                BRA     BINLOD
NTPROL:         INC     A              ; 255: binary
                BEQ     BINLOD
MAINGO:         DEC     A
                JSR     BAKCHR
                JSR     SCDASC
LDMAIN:         LDX     #0
                RTS
BINLOD:         LDA     TXTTAB
                STA     RBL
                LDA     TXTTAB+1
                STA     RBH
                JSR     FSTLOD
                LDA     RBL
                STA     VARTAB
                LDA     RBH
                STA     VARTAB+1
                LDA     PROFLG
                BEQ     :+
                JSR     PDECOD         ; PROLOD
:               LDA     #0             ; OO-1: the guard
                LDY     #11
@g:             STA     (RBL),Y
                DEY
                BPL     @g
                FJSR    LINKER         ; TXTPTR at the end's zero link
                LDA     TXTPTR
                CMP     RBL
                LDA     TXTPTR+1
                SBC     RBH
                BCC     :+
                LDX     #2             ; not below them: OUTLOD
                RTS
:               CLC
                LDA     TXTPTR
                ADC     #2
                STA     VARTAB
                LDA     TXTPTR+1
                ADC     #0
                STA     VARTAB+1
                LDA     RUNFLG
                LDX     #1
                RTS

; --- MERGE (DSKCOM.ASM 432) ---------------------------------------------------------------

; MERGEP: the name, which must end the statement (else file 0 closed,
; PRGFIN, and Syntax error); file 0 opened for input; OKGETM: not run, file
; 0 not closed (NLONLY 1); OKGET2: the first byte. A tokenised file (255)
; or a protected one (254) is Bad file mode; else it is backed up and the
; file made text (MAINGO). The stub drops NEWSTT's return and goes to MAIN,
; which reads the lines as typed, into the program as it is.
MERGEP:         JSR     PRGFLI
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     OKGETM
                JSR     PRGFIN
                JMP     SNERR
OKGETM:         STZ     RUNFLG
                LDA     #1
                STA     NLONLY
OKGET2:         JSR     INDSKC
                BCS     @x
                INC     A
                BEQ     @bfm
                INC     A
                BEQ     @bfm
                DEC     A
                DEC     A              ; MAINGO's
                JSR     BAKCHR
                JMP     SCDASC
@bfm:           JMP     DERBFM
@x:             RTS

; --- SAVE (DSKCOM.ASM 477) ----------------------------------------------------------------

; SAVEP: the name skipped (FRMEVL, its string freed); then the end, or ",A"
; or ",P" and the end (GOODSV); file 0 opened for output (PRGFL2: the old
; file gone, a new one made) by the name, then TXTPTR at the end, or at the
; "A" or "P" (ENDOFS). ",P": PROSAV; ",A": the file text (SCDASC), and C
; set back: the stub skips the "A" and goes to LIST, which lists to file 0;
; else BINSAV. Both of those close file 0 and return C clear. SVNAM and
; SVEND are the 8086's stack.
SAVEP:          LDA     TXTPTR         ; the name's place, for the rescan
                STA     SVNAM
                LDA     TXTPTR+1
                STA     SVNAM+1
                JSR     FRMEVL
                JSR     FRESTR
                DECW    TXTPTR
                JSR     CHRGTR
                BNE     :+
                LDX     #0             ; no option
                BRA     ENDOFS
:               JSR     SYNCHR
                .byte   ','
                CMP     #'A'
                BEQ     GOODSV
                CMP     #'P'
                BEQ     GOODSV
SVSN:           JMP     SNERR
GOODSV:         PHA                    ; "A" or "P"
                LDA     TXTPTR
                STA     SVEND
                LDA     TXTPTR+1
                STA     SVEND+1
                JSR     CHRGTR
                BNE     SVSN
                LDA     SVEND          ; at the "A" or "P"
                STA     TXTPTR
                LDA     SVEND+1
                STA     TXTPTR+1
                PLX
ENDOFS:         STX     SVOPT
                LDA     TXTPTR
                STA     SVEND
                LDA     TXTPTR+1
                STA     SVEND+1
                LDA     SVNAM
                STA     TXTPTR
                LDA     SVNAM+1
                STA     TXTPTR+1
                LDA     #MD_SQO        ; PRGFL2
                JSR     PRGFIL
                LDA     SVEND
                STA     TXTPTR
                LDA     SVEND+1
                STA     TXTPTR+1
                LDA     SVOPT
                CMP     #'P'
                BNE     :+
                JSR     PROSAV
                CLC
                RTS
:               CMP     #'A'
                BNE     :+
                JSR     SCDASC
                SEC
                RTS
:               JSR     BINSAV
                CLC
                RTS

; --- FIXINP (DSKCOM.ASM 777) --------------------------------------------------------------

; INPUT$(n[, [#]f]), from EVAL at INPUT: n bytes (1-255: 0 is Illegal
; function call) from file f, which mustn't be for output (Input past end;
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
:               CMP     #MD_SQO        ; for output: Input past end (OO-1, the
                BNE     :+             ; IBM manual's A-11; the source's Bad file mode)
                JMP     DERRPE
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

; --- FIELD (DSKCOM.ASM 541) ---------------------------------------------------------------

; FIELDP: FIELD [#]n, w AS v$[, w AS v$ ...]. The file must be open
; (DERFNO) and random (Bad file mode). Each clause makes v$ (a string
; variable or element: Type mismatch if not) w bytes (0-255) of the FIELD
; buffer, the next after the last clause's; their total past the record's
; size (FD_SIZ, TEMPA) is FIELD overflow. FLDTOT is TEMPB, the total; FLDDX
; the 8086's DX, the place in the buffer; FLDWID its CL, the width.
FIELDP:         JSR     FILSCN         ; FDBP; A the mode, Z not open
                BNE     :+
                JMP     DERFNO
:               CMP     #MD_RND        ; SUB AL,MD_RND
                BEQ     :+
                JMP     DERBFM
:               LDY     #FD_SIZ        ; TEMPA: the most
                LDA     (FDBP),Y
                STA     FLDMAX
                INY
                LDA     (FDBP),Y
                STA     FLDMAX+1
                STZ     FLDTOT         ; TEMPB: none yet
                STZ     FLDTOT+1
                JSR     GETDAT         ; DX: the FIELD buffer (FD_DAT)
                STA     FLDDX
                STX     FLDDX+1
LOPFLD:         LDA     (TXTPTR)       ; more "AS"s? a comma starts one
                CMP     #','
                BEQ     :+
                RTS                    ; back to NEWSTT
:               JSR     GTBYTC         ; the width
                STA     FLDWID
                JSR     SYNCHR
                .byte   'A'
                JSR     SYNCHR
                .byte   'S'
                JSR     PTRGET         ; PTR: the descriptor
                JSR     CHKSTR         ; FRCSTR
                CLC                    ; the total, in range?
                LDA     FLDTOT
                ADC     FLDWID
                STA     FLDTOT
                BCC     :+
                INC     FLDTOT+1
:               LDA     FLDMAX
                CMP     FLDTOT
                LDA     FLDMAX+1
                SBC     FLDTOT+1
                BCS     :+
                JMP     DERFOV
:               LDA     FLDWID         ; the length, then the place
                STA     (PTR)
                LDY     #1
                LDA     FLDDX
                STA     (PTR),Y
                INY
                LDA     FLDDX+1
                STA     (PTR),Y
                CLC                    ; the next's place
                LDA     FLDDX
                ADC     FLDWID
                STA     FLDDX
                BCC     LOPFLD
                INC     FLDDX+1
                BRA     LOPFLD

; --- LSET, RSET: P3's (p3/dskcom.s) -------------------------------------------------------

; OO-1 (the IBM spec's fix-up, the owner's answer 2: P4's relief): LSET and
; RSET moved to block P3 whole; they need nothing of P4's, so each is still
; one far call from the home block's stub. Their two jump table slots here
; are kept, retired (LSETR, RSETR: an RTS), so P4's later entries keep their
; places and the blocks that call them are unchanged.
LSETR:
RSETR:
FUNDXR:         RTS                    ; (and FUNDXP's: CVI to MKD$ are P3's too,
                                       ; review round 1's second move)

.segment "BRAM"
FLDMAX:         .res    2              ; FIELD's TEMPA: the record's size
FLDTOT:         .res    2              ; and TEMPB: the widths so far
FLDDX:          .res    2              ; and its DX: the place in the buffer
FLDWID:         .res    1              ; and its CL: a clause's width
INPTXT:         .res    2              ; INPDOR's data pointer
DLVAR:          .res    2              ; DLINE's variable
FIXPTR:         .res    2              ; INPUT$'s PTRFIL, kept
FIXCNT:         .res    1
FIXTTY:         .res    1
SVNAM:          .res    2              ; SAVEP's name's place, its end's, and its option
SVEND:          .res    2
SVOPT:          .res    1

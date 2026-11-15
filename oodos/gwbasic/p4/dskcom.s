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
;                   (p8/far4.s), as the far call rule needs
;   MERGEP          MERGE name (MERGE, OKGETM, OKGET2)
;   SAVEP           SAVE name[,A|,P] (SAVE, GOODSV, ENDOFS)
;   FIELDP          FIELD [#]n, w AS v$, ...: strings in a random file's FIELD
;                   buffer
;   LSETP RSETP     LSET, RSET v$ = s$: s$ into v$'s room, left or right
;                   (BLKFIL, MAKDSC)
;   CVI CVS CVD     the functions from FUNDSX (FUNDXP, from the resident
;   MKI MKS MKD     EVALNF): a string's bytes as a number, and a number's
;                   as a string (MKI$, MKS$, MKD$)
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
;@entry LSETP
;@entry RSETP
;@entry FUNDXP
.export FILIND, DLINE, FIXINPP, LOADP, LOADP2, MERGEP, SAVEP, FIELDP, LSETP, RSETP, FUNDXP
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

; --- LOAD, RUN name (DSKCOM.ASM 290) -----------------------------------------------------

; LOADP: LRUN3's first part, X its flag (0 LOAD: the files closed, not run;
; 1 RUN: closed, run; the 8086's stack): file 0 opened for input (PRGFLI);
; ",R" makes the flag $81 (CHNENT's 201O: the files left open, run), and
; nothing else may follow. NOTRN1: RUNFLG the flag, NLONLY the flag or 1
; (file 0 not closed; with $81, none). Then the stub's SCRTCH.
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
:               LDA     #$81
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
; NEWSTT. (CHAIN's CHNRET, CHNFLG, is a later step's.) OO-1 (W2): the end
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

; --- LSET, RSET (DSKCOM.ASM 621) ----------------------------------------------------------

; LSETP, RSETP: LSET v$ = s$, RSET v$ = s$. s$'s bytes go into v$'s, as many
; as v$ has room for, the rest of it spaces: after them (LSET) or before
; (RSET). A null v$ is left as it is. v$'s bytes are changed where they
; are: a FIELD buffer (below TXTTAB in the 8080's map; here above MEMSIZ, as
; the 8086's) or the string space; but a v$ whose bytes are a literal in the
; program gets a copy of its length first (GETSPA), and if that could
; collect the strings, a temporary for s$ is made first (MAKDSC), if s$ was
; one: the flags FRESTR leaves (the 8086's PUSHF, "zero if was temp"; they
; are zero only for a null temporary at FRETOP, so MAKDSC almost never makes
; it). LRFLG bit 7 is the 8086's carry, LSET; LRVAR v$'s descriptor, LREXP
; s$'s, LRP FRESTR's flags; LRCL v$'s length (CL), LRCH s$'s (CH), then
; the bytes to copy and CL the spaces; DX (RDL) the place in v$.
RSETP:          CLC
                BRA     LSETA
LSETP:          SEC
LSETA:          ROR     LRFLG          ; PUSHF
                JSR     PTRGET
                JSR     CHKSTR         ; a string variable
                LDA     PTR            ; PUSH DX
                STA     LRVAR
                LDA     PTR+1
                STA     LRVAR+1
                JSR     FRMEQL         ; "=" and s$
                JSR     FRESTR         ; a string, its temporary freed: BX
                PHP
                PLA
                STA     LRP
                LDA     RBL
                STA     LREXP
                LDA     RBH
                STA     LREXP+1
                LDA     (RBL)          ; CH: s$'s length
                STA     LRCH
                LDA     LRVAR          ; CL: v$'s
                STA     RBL
                LDA     LRVAR+1
                STA     RBH
                LDA     (RBL)
                STA     LRCL
                BNE     :+
                RTS                    ; RETCUR: null, not changed
:               LDY     #1             ; DX: its bytes
                LDA     (RBL),Y
                STA     RDL
                INY
                LDA     (RBL),Y
                STA     RDH
                LDA     TXTTAB         ; at or below TXTTAB: a buffer
                CMP     RDL
                LDA     TXTTAB+1
                SBC     RDH
                BCS     OLDSTR
                LDA     VARTAB         ; above VARTAB: the strings'
                CMP     RDL
                LDA     VARTAB+1
                SBC     RDH
                BCC     OLDSTR
                CLC                    ; a literal: a copy. Would GETSPA collect?
                LDA     STREND
                ADC     LRCL
                STA     RBL
                LDA     STREND+1
                ADC     #0
                STA     RBH
                LDA     FRETOP
                CMP     RBL
                LDA     FRETOP+1
                SBC     RBH
                JCC     MAKDSC
MADESC:         LDA     LRCL           ; room for v$'s length
                JSR     GETSPA
                LDA     RDL            ; PUSH DX round it
                PHA
                LDA     RDH
                PHA
                JSR     FRESTR         ; s$'s temporary freed, if any
                PLA
                STA     RDH
                PLA
                STA     RDL
                LDA     LRVAR          ; v$ at the copy
                STA     RBL
                LDA     LRVAR+1
                STA     RBH
                LDY     #1
                LDA     RDL
                STA     (RBL),Y
                INY
                LDA     RDH
                STA     (RBL),Y
OLDSTR:         LDA     LRVAR          ; DX: v$'s bytes
                STA     RBL
                LDA     LRVAR+1
                STA     RBH
                LDY     #1
                LDA     (RBL),Y
                STA     RDL
                INY
                LDA     (RBL),Y
                STA     RDH
                LDA     LREXP          ; PTR2: s$'s
                STA     RBL
                LDA     LREXP+1
                STA     RBH
                LDY     #1
                LDA     (RBL),Y
                STA     PTR2
                INY
                LDA     (RBL),Y
                STA     PTR2+1
                LDA     LRCL           ; room for all of s$?
                CMP     LRCH
                BCS     FILDOK
                STA     LRCH           ; no: as many as there is
FILDOK:         SEC                    ; CL: the spaces
                SBC     LRCH
                STA     LRCL
                BIT     LRFLG
                BMI     :+
                JSR     BLKFIL         ; RSET: before
:               LDY     #0             ; COPLOP: CH bytes
@c:             CPY     LRCH
                BEQ     LRSTDN
                LDA     (PTR2),Y
                STA     (RDL),Y
                INY
                BRA     @c
LRSTDN:         TYA                    ; DX past them
                CLC
                ADC     RDL
                STA     RDL
                BCC     :+
                INC     RDH
:               BIT     LRFLG
                BPL     :+
                JSR     BLKFIL         ; LSET: after
:               RTS

; BLKFIL: CL spaces at DX, DX past them.
BLKFIL:         LDY     #0
@b:             CPY     LRCL
                BEQ     @x
                LDA     #' '
                STA     (RDL),Y
                INY
                BRA     @b
@x:             TYA
                CLC
                ADC     RDL
                STA     RDL
                BCC     :+
                INC     RDH
:               RTS

; MAKDSC: GETSPA may collect: if s$ was a temporary (LRP's Z), a temporary
; is made for it (STRINI of its length, PUTNEW) before MADESC goes on.
MAKDSC:         LDA     LRP
                AND     #$02           ; the Z flag
                BEQ     MAKDS1         ; not a temporary
                LDA     LRCH
                JSR     STRINI
                JSR     PUTNEW
MAKDS1:         JMP     MADESC

; --- CVI, CVS, CVD, MKI$, MKS$, MKD$ (DSKCOM.ASM 79) ----------------------------------------

; FUNDXP (the project's own): EVALNF's dispatch (res/ibmres.s), X the
; function's place in FUNDSX (2 x (its token - $81)), the argument in the
; FAC; back to EVALNF's LABBCK.
FUNDXP:         JMP     (FUNDSX,X)

; CVI CVS CVD: the string's first 2, 4 or 8 bytes are the value, an
; integer, a single or a double, as MKI$ and the rest make them (its bytes
; as they are in memory: the 8086's, an integer's low byte first, MBF's
; singles and doubles); a shorter string is Illegal function call. AL is
; one less than the bytes, then VALTYP.
CVI:            LDA     #1
                BRA     CVX
CVS:            LDA     #3
                BRA     CVX
CVD:            LDA     #7
CVX:            PHA
                JSR     FRESTR         ; BX: the string's descriptor
                PLA
                CMP     (RBL)          ; longer than AL?
                BCC     :+
                JMP     FCERR
:               INC     A
                STA     VALTYP
                LDY     #1             ; its bytes
                LDA     (RBL),Y
                STA     PTR
                INY
                LDA     (RBL),Y
                STA     PTR+1
                JMP     VMOVFM         ; to the FAC

; MKI MKS MKD: MKI$, MKS$, MKD$: the FAC made an integer, a single or a
; double (DOCNVF: Overflow, Type mismatch), and its 2, 4 or 8 bytes a new
; string's (STRINI; VMOVMF, here VMOVE to PTR), its result (FINBCK).
MKI:            LDA     #2
                BRA     MKX
MKS:            LDA     #4
                BRA     MKX
MKD:            LDA     #8
MKX:            PHA
                JSR     DOCNVF
                PLA
                JSR     STRINI         ; DX: its room (DSCPTR's)
                LDA     RDL
                STA     PTR
                LDA     RDH
                STA     PTR+1
                JSR     VMOVE
                JMP     PUTNEW         ; FINBCK (res/bistrs.s: PUTNEW, as LABBCK pops the text pointer)

.rodata
; FUNDSX (IBMRES.ASM 598): the $FD functions, CVI to MKD$ (OO-1: KTN, JIS,
; KPOS and KLEN, Kanji's, are dropped).
FUNDSX:         .word   CVI, CVS, CVD, MKI, MKS, MKD

.segment "BRAM"
FLDMAX:         .res    2              ; FIELD's TEMPA: the record's size
FLDTOT:         .res    2              ; and TEMPB: the widths so far
FLDDX:          .res    2              ; and its DX: the place in the buffer
FLDWID:         .res    1              ; and its CL: a clause's width
LRFLG:          .res    1              ; LSET (bit 7) or RSET
LRVAR:          .res    2              ; LSET's and RSET's v$ (its descriptor)
LREXP:          .res    2              ; and s$
LRP:            .res    1              ; and FRESTR's flags
LRCL:           .res    1              ; v$'s length (CL), then the spaces
LRCH:           .res    1              ; s$'s (CH), then the bytes to copy
INPTXT:         .res    2              ; INPDOR's data pointer
DLVAR:          .res    2              ; DLINE's variable
FIXPTR:         .res    2              ; INPUT$'s PTRFIL, kept
FIXCNT:         .res    1
FIXTTY:         .res    1
SVNAM:          .res    2              ; SAVEP's name's place, its end's, and its option
SVEND:          .res    2
SVOPT:          .res    1

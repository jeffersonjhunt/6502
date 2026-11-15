; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIO86.ASM's file statements, functions and file data blocks, translated, in
; block P4, the file layer (hw/oodos.md, "The plan, phase 7": 7c):
;
;   OPENP           OPEN mode, [#]n, name[, len] and OPEN name [FOR mode] AS
;                   [#]n [LEN = len] (PORLEN, PNRLEN, PRLENC: the length)
;   CLOSEP          CLOSE [[#]n[, [#]n ...]]: the files named, or all
;   EOF LOC LOF     the functions, from FILFNP (the resident FILFUN's)
;   POFNUM PRFNUM   [#]n, #n, n: a file number (GETNZB)
;   FILINP FILGET   PRINT #, WRITE #, INPUT #'s file: PTRFIL at its FDB
;   FILSET FILSCN   (FILSCN: the number, its FDB and its mode)
;   GETPTR          VARPTR(#n)'s: file n's FDB (its F_MODE's address)
;   OPNFIL          a file opened (DSKOPN, p4/giodsk.s)
;   CLSALP CLSFIL   every file closed, or one
;   INCHR INCHRE    the next byte from PTRFIL's file (INCHR: Input past end at
;   INDSKC INCHSI   its end; the others C set), BAKCHR to back one up
;   NAMSCN PARDEV   a file's name, drive and type to FILNAM
;   INIFDB FDBPTR   an FDB made, or found by its number; FFREE frees one
;   FALLOC XFRSTR   and a random file's FIELD buffer taken from the string
;   UPDSTD SKPNAM   space, or given back, the strings moved (FIVEO.ASM's
;                   SKPNAM)
;   DPUTG           GET and PUT [#]n[, record] of a random file (GWSTS.ASM's
;                   PUT and GET, p4/gwsts.s, come here)
;   PTRDSP SIDSP    the device's function for an FDB (the disk's DSKDSP)
;   PRGFIL NAMBAS   file 0 opened for LOAD, SAVE and MERGE, ".BAS" the type
;   NULOPN          if it has none (7c's LOAD step); NULOPM in a mode (LIST's
;   NULOPM          file, p2/gwlist.s)
;   SCDASC SCDBIN   PTRFIL's file made text, or binary
;   FSTLOD INPBLK   a tokenised program read (the disk's DFSTLD)
;   BINSAV BINPSV   and one written (OUTBLK: the disk's DEVBOT, each byte
;   OUTBLK DEVBOT   out)
;
; and the project's own (the OO-1's, beside them):
;
;   FILFNP          FILFUN's dispatch (res/far4.s): EOF, LOC, LOF, INPUT$
;   FLUSHP FLUSHR   OUTDO's run (RUNBUF, res/oem.s) to PTRFIL's file, the 8086's
;                   OUTDO and PTRDSP for each byte
;   FINPRP          FINPRT's part (res/oem.s): the run sent, FREFDB's FDB freed
;   SETPTR          PTRFIL set, the run for the old one sent first, and a
;                   file's column (FILPOS) from its F_POS
;   INCHRI          the console's next key, unechoed (GIOKYB.ASM's: the OEM's)
;   GETDAT          a random file's FIELD buffer's address (FDDAT)
;   PRGFIN          DSKCOM.ASM's: output to the console, file 0 closed, TXTPTR
;                   from TEMP (here for INCHR's NOTCHN, the first to need it)
;
; The FDBs are a table in this block's RAM (FDBTAB), one for each file number
; 0 to NMFILE: OO-1 (V1, route (c)), where the 8086 allocates them in the
; string space as a chain (FALLOC, FFREE, XFRSTR). So a file's FCB and
; record buffer are reached only while P4 is in W3, and the BDOS reads and
; writes a record straight into the buffer at its address here, through the
; W3 block its entry saved. FDBP is the 8086's SI; the pseudo-registers its
; other registers, as elsewhere. The 8086's file numbers may be any of
; 1-255 (its FDBs only take memory); here a number past NMFILE is Bad file
; number, as GW-BASIC's own /F: makes it later (OO-1). The only device is
; the disk: a name with a device other than a drive is Bad file name, as the
; 8086's PARDEV says of a device its table hasn't (the others are later).
;
; A random file's FIELD buffer (the 8086's FD_DAT, the FDB's last field) is
; in the TPA, as the plan has it (the string functions read a FIELDed string
; there): FALLOC takes it from the top of the string space when the file is
; opened, as the 8086's FALLOC takes the whole FDB, and FFREE gives it back,
; moving the strings (XFRSTR). So it costs no byte until a random file is
; open, and then its LEN. Its address is FDDAT's, by the file's number
; (OO-1: the FDB's own size is kept).
;
; The far call rule: every routine here returns by RTS, or ends in an error
; or a tail call to resident code that returns (FINPRT, MAKINT, PUTNEW).

.include "gwbasic.inc"

;@entry OPENP
;@entry CLOSEP
;@entry CLSALP
;@entry FILFNP
;@entry FILGET
;@entry FILINP
;@entry FINPRP
;@entry FLUSHP
;@entry NAMSCN
;@entry NULOPM
;@entry GETPTR
.export NAMSCN, NULOPM, GETPTR, SKPNAM
.export OPENP, CLOSEP, CLSALP, FILFNP, FILGET, FILINP, FINPRP, FLUSHP
.export FILSET, FILSCN, FDBPTR, INCHR, INDSKC, BAKCHR, SETPTR, FLUSHR, INIFDB, FFREE
.export INCHRI, SIDSP, FILNAM, FILMOD, OPNNUM, FDBTAB
.export PRGFIL, SCDASC, FSTLOD, BINSAV, BINPSV, DEVBOT, PRGFIN, CLSFIL, RUNFLG, LDRUN, PTRDSP
.export DERBFN, DERFNF, DERBFM, DERFNO, DERFAO, DERIOE, DERDFL, DERRPE, DERIFN, DERTMF, DERDNA
.export DERFAE, DERRAD, DERFOV, DERBRN, DPUTG, GETDAT
.import FRMEVL, FRESTR, SYNCHR, CHRGTR, GETBYT, CONINT, INTID2, FINPRT, MAKINT
.import GARBA2, MEMSIZ
.import ERROR, FCERR, SNERR, ADVERR, CHSNS, RUNBUF, FILOPN, FREFDB
.import DSKOPN, DSKDSP, FIXINPP
.import STROUT, REDDY, GTMPRT, SAVFRE

ERRBFN          = 52                   ; GWDATA.ASM's disk errors
ERRFNF          = 53
ERRBFM          = 54
ERRFNO          = ERRBFN               ; OO-1: the 1983 source maps it to ERRBFM (the owner's, 2026-11-14)
ERRFAO          = 55
ERRIOE          = 57
ERRDFL          = 61
ERRRPE          = 62
ERRNMF          = 64
ERRTMF          = 67
ERRDNA          = 68
ERRFAE          = 58
ERRRAD          = 74
ERRFOV          = 50                   ; FIELD overflow
ERRBRN          = 63                   ; Bad record number
ERROM           = 7                    ; Out of memory
FNAML           = 11                   ; MSDOSU's: a name and type's bytes

.code

; --- OPEN (GIO86.ASM 62) ------------------------------------------------------------------

; OPEN: a formula first, the mode or the name. Followed by a comma it is the
; old form, its mode the string's first letter (O, I, A or R, in either
; case), then [#]n and the name, and a random file's length. Else the new:
; the name, then FOR INPUT, OUTPUT or APPEND (no FOR: random), AS [#]n and
; a random file's LEN = len. Then OPNFIL, and output back to the console.
; The file number waits in OPNNUM (the 8086's stack), CX's length in RCX.
OPENP:          JSR     FRMEVL
                LDA     (TXTPTR)       ; its terminator
                CMP     #','
                BNE     NOTNSO
                JSR     FRESTR         ; BX at its descriptor
                LDA     (RBL)          ; a null string: Bad file mode
                BEQ     ERBFM0
                LDY     #1
                LDA     (RBL),Y
                STA     PTR
                INY
                LDA     (RBL),Y
                STA     PTR+1
                LDA     (PTR)          ; the mode's letter, upper case
                AND     #$DF
                LDX     #MD_SQO
                CMP     #'O'
                BEQ     HAVMOD
                LDX     #MD_SQI
                CMP     #'I'
                BEQ     HAVMOD
                LDX     #MD_APP
                CMP     #'A'
                BEQ     HAVMOD
                LDX     #MD_RND
                CMP     #'R'
                BNE     ERBFM0
HAVMOD:         STX     FILMOD
                JSR     SYNCHR
                .byte   ','
HAVMD1:         JSR     POFNUM         ; the file number
                STA     OPNNUM
                JSR     SYNCHR
                .byte   ','
                JSR     NAMSCN
                JSR     PORLEN
                BRA     OPEN1
ERBFM0:         JMP     DERBFM

NOTNSO:         JSR     NAMSC1         ; the name, already evaluated
                LDX     #MD_RND        ; no FOR: random
                LDA     (TXTPTR)
                CMP     #FOR_TK
                BNE     GOTMOD
                JSR     CHRGTR
                LDX     #MD_SQI
                CMP     #INPUT_TK
                BEQ     GOTMD1
                CMP     #'A'
                BNE     NTAPP
                JSR     SYNCHR
                .byte   'A'
                JSR     SYNCHR
                .byte   'P'
                JSR     SYNCHR
                .byte   'P'
                JSR     SYNCHR
                .byte   'E'
                JSR     SYNCHR
                .byte   'N'
                JSR     SYNCHR
                .byte   'D'
                LDX     #MD_APP
                BRA     GOTMOD
NTAPP:          JSR     SYNCHR         ; it must be OUTPUT
                .byte   'O'
                JSR     SYNCHR
                .byte   'U'
                JSR     SYNCHR
                .byte   'T'
                JSR     SYNCHR
                .byte   'P'
                JSR     SYNCHR
                .byte   'U'
                JSR     SYNCHR
                .byte   'T'
                LDX     #MD_SQO
                BRA     GOTMOD
GOTMD1:         STX     FILMOD         ; (the 8086's DH, kept over CHRGTR)
                JSR     CHRGTR
                BRA     GOTMD2
GOTMOD:         STX     FILMOD
GOTMD2:         JSR     SYNCHR
                .byte   'A'
                JSR     SYNCHR
                .byte   'S'
                JSR     POFNUM
                STA     OPNNUM
                JSR     PNRLEN
OPEN1:          DECW    TXTPTR
                JSR     CHRGTR
                BNE     SNERR1         ; the statement must end
                LDA     OPNNUM
                JSR     OPNFIL
                JMP     FINPRT         ; PTRFIL back to the console

; PORLEN (GIO86.ASM 164): the old form's length, after a comma. PNRLEN
; (174): the new form's, LEN = len. Either: CX the length (1-32767; 0 is
; Illegal function call), or 0 if there is none. PRLENC (191): Z clear if a
; length is expected: a random file, the statement not ended.
PORLEN:         JSR     PRLENC
                BEQ     PRLENX
                JSR     SYNCHR
                .byte   ','
                BRA     VARECS
PNRLEN:         JSR     PRLENC
                BEQ     PRLENX
                JSR     SYNCHR
                .byte   $FF            ; LEN is a function's two-byte token
                JSR     SYNCHR
                .byte   LEN_FN
                JSR     SYNCHR
                .byte   EQULTK
VARECS:         JSR     INTID2         ; DX: 0..32767
                LDA     RDL
                STA     RCL
                LDA     RDH
                STA     RCH
                ORA     RCL
                BEQ     FCERR1
PRLENX:         RTS
PRLENC:         STZ     RCL
                STZ     RCH
                LDA     FILMOD
                CMP     #MD_RND
                BNE     NOLEN
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     NOLEN
                LDA     #1
                STA     RCL
NOLEN:          LDA     RCL            ; Z: no length
                ORA     RCH
                RTS
FCERR1:         JMP     FCERR
SNERR1:         JMP     SNERR

; --- CLOSE (GIO86.ASM 214) ----------------------------------------------------------------

; CLOSE alone closes every file; else each number in the list, with or
; without "#" (one that isn't open is passed over, as CLSFIL does).
CLOSEP:         BNE     CLOS1
                JMP     CLSALP
CLOS1:          CMP     #'#'
                BNE     NOLBS
                JSR     CHRGTR
NOLBS:          JSR     GETBYT
                JSR     CLSFIL
                DECW    TXTPTR
                JSR     CHRGTR
                CMP     #','
                BNE     RET6
                JSR     CHRGTR
                BRA     CLOS1
RET6:           RTS

; --- EOF, LOC, LOF (GIO86.ASM 596) --------------------------------------------------------

; FILFNP (the project's own): the resident FILFUN's, X the function as FINGO
; has it (2 x its token's offset), or 0 for INPUT$ (FIXINP's).
FILFNP:         CPX     #0
                BNE     :+
                JMP     FIXINPP
:               CPX     #2 * (LOC_FN - ONEFUN)
                BEQ     LOC
                BCS     LOF

; EOF(n): -1 at the file's end, else 0. A byte backed up isn't the end; an
; end found (F_ORCT 0) is; else the device's EOF (DSKEOF).
EOF:            JSR     FACFPT
                BEQ     ERFNO4
                STZ     RBL            ; BX = 0
                STZ     RBH
                LDY     #F_FLGS
                LDA     (FDBP),Y
                AND     #FL_BKC
                BNE     GFUNX
                DEC     RBL            ; BX = -1
                DEC     RBH
                LDY     #F_ORCT
                LDA     (FDBP),Y
                BEQ     GFUNX
                LDX     #G_EOF
                JSR     SIDSP
                BRA     GFUNX
ERFNO4:         JMP     DERFNO

; LOC(n): the device's LOC (DSKLOC: the records read or written), as an
; integer. LOF(n): its LOF, which makes the FAC itself (DSKLOF).
LOC:            LDX     #G_LOC
GENFUN:         JSR     FACDSP
GFUNX:          JMP     MAKINT
LOF:            LDX     #G_LOF
                JMP     FACDSP

; --- POFNUM, PRFNUM, GETNZB (GIO86.ASM 670) -----------------------------------------------

; POFNUM: [#]n; PRFNUM: #n; GETNZB: n. A the number, 1-255: 0 is Bad file
; number, and so (OO-1) is one past NMFILE, the FDB table's last.
POFNUM:         LDA     (TXTPTR)
                CMP     #'#'
                BNE     GETNZB
PRFNUM:         JSR     SYNCHR
                .byte   '#'
GETNZB:         JSR     GETBYT
                CMP     #0
                BEQ     ERBFN1
                CMP     #NMFILE + 1    ; OO-1
                BCS     ERBFN1
RET9:           RTS
ERBFN1:         JMP     DERBFN

; --- FILINP, FILGET, FILSET, FILSCN (GIO86.ASM 698) ---------------------------------------

; FILINP: "#n," for input (INPUT #, LINE INPUT #); FILGET: for the mode in
; CL (MD_SQO: PRINT #, WRITE #), A the character at TXTPTR. Not "#": nothing.
; The file must be open (else DERFNO) in that mode, or random (else Bad
; file mode); then past the comma, and PTRFIL at its FDB (FILSET).
FILINP:         LDX     #MD_SQI
                STX     RCL
FILGET:         CMP     #'#'
                BNE     RET9
                LDA     RCL            ; PUSH CX: the mode wanted
                PHA
                JSR     FILSCN
                BEQ     ERFNO3
                STA     T0             ; the file's mode
                PLA
                CMP     T0
                BEQ     GDFILM
                LDA     T0
                CMP     #MD_RND        ; a random file takes them all
                BNE     ERBFM3
GDFILM:         JSR     SYNCHR
                .byte   ','
; FILSET (712): PTRFIL at the FDB FILSCN found (the 8086's CX: FDBP).
FILSET:         LDA     FDBP
                LDX     FDBP+1
                JMP     SETPTR
ERBFM3:         JMP     DERBFM
ERFNO3:         JMP     DERFNO

; FILSCN (726): [#]n; FILIDX: its number in A; FILID2: the same. DL the
; number, FDBP its FDB; A the file's mode, Z set if it isn't open (then no
; FDB is in use for it).
FILSCN:         JSR     POFNUM
FILIDX:         STA     RDL
FILID2:         JSR     FDBPTR
                BEQ     NTOPEN
                LDY     #F_MODE
                LDA     (FDBP),Y       ; (not zero: open)
NTOPEN:         RTS

; GETPTR (740): VARPTR(#n), A the number: PTR at file n's FDB, its F_MODE
; (OO-1: in P4's FDB table, FDB0 on, FDBLEN apart); Bad file number if the
; file isn't open.
GETPTR:         JSR     FDBPTR
                BEQ     ERFNO3
                CLC
                LDA     FDBP
                ADC     #F_MODE
                STA     PTR
                LDA     FDBP+1
                ADC     #0
                STA     PTR+1
                RTS

; --- OPNFIL, CLSALL, CLSFIL (GIO86.ASM 830) -----------------------------------------------

; OPNFIL: file A opened, CX its length (0 the default), FILMOD its mode,
; FILNAM its drive, name and type: File already open if it is; else the
; device's open (TBLDSP's G_OPN: the disk's DSKOPN, the only device).
; PTRFIL returns at its FDB.
OPNFIL:         STA     OPNNUM         ; BX: the number
                JSR     FDBPTR
                BNE     ERFAO1
                LDA     FILDEV         ; the device: a disk (CDEVID's 0..n)
                JMP     DSKOPN
ERFAO1:         JMP     DERFAO

; PRGFIL (GIO86.ASM 790): file 0 opened for LOAD, SAVE or MERGE, in mode A
; (the 8086's DH): the name (NAMSCN), TEMP past it (PRGFIN puts TXTPTR back
; there), ".BAS" if it has no type (NAMBAS); NULOPN: file 0, no length,
; OPNFIL. PTRFIL returns at its FDB, so every input and output is the file's.
PRGFIL:         STA     FILMOD
                JSR     NAMSCN         ; C set: a "." in it
                PHP
                LDA     TXTPTR
                STA     TEMP
                LDA     TXTPTR+1
                STA     TEMP+1
                PLP
                BCS     NULOPN
                JSR     NAMBAS
                BRA     NULOPN         ; PRGFIX
; NULOPM (815): file 0 opened in mode A, the name NAMSCN found (LIST's file).
NULOPM:         STA     FILMOD
NULOPN:         STZ     RCL
                STZ     RCH
                LDA     #0
                JMP     OPNFIL

; NAMBAS (GIO86.ASM 806): a disk file's blank type made BAS. (OO-1: every
; FILDEV is a drive's: no other device yet.)
NAMBAS:         LDA     FILNM+8
                CMP     #' '
                BNE     NAMBAX
                LDA     #'B'
                STA     FILNM+8
                LDA     #'A'
                STA     FILNM+9
                LDA     #'S'
                STA     FILNM+10
NAMBAX:         RTS

; PRGFIN (DSKCOM.ASM 422): output to the console (FINPRT, which closes file
; 0 unless a load is under way), file 0 closed (CLSFIL), TXTPTR from TEMP.
PRGFIN:         JSR     FINPRT
                LDA     #0
                JSR     CLSFIL
                JMP     GTMPRT

; CLSALP (862): every file closed. OO-1: each number of the table in turn
; (CLSFIL passes over one not open), where the 8086 walks its chain.
CLSALP:         STZ     CLSNUM
CLSAL1:         LDA     CLSNUM
                CMP     #NMFILE + 1
                BCS     CLSALX
                JSR     CLSFIL
                INC     CLSNUM
                BRA     CLSAL1
CLSALX:         RTS

; CLSFIL (880): file A closed: not while NLONLY says not (bit 7: none;
; bit 0: not file 0); nothing if it isn't open. FREFDB holds the FDB while
; the device closes it (DSKCLS), so an error there still frees it (FINPRT);
; then FFREE. OO-1: OUTDO's run is sent first, so a file is never closed
; with its last bytes still in RUNBUF.
CLSFIL:         JSR     FLUSHR         ; (OO-1; A kept)
                TAX                    ; the number
                LDA     NLONLY
                BMI     RET22
                LSR     A
                BCC     CLSFL1
                CPX     #0
                BEQ     RET22
CLSFL1:         TXA
                JSR     FDBPTR
                BEQ     RET22
                LDA     FDBP
                STA     FREFDB
                LDA     FDBP+1
                STA     FREFDB+1
                LDX     #G_CLS
                JSR     SIDSP
                STZ     FREFDB
                STZ     FREFDB+1
                JMP     FFREE
RET22:          RTS

; --- INCHR, INCHRE, INCHSI (GIO86.ASM 912) ------------------------------------------------

; INCHR: the next byte from PTRFIL's file in A. At its end Input past end;
; but file 0's end is a load's (FL0EOF): a binary one's, C set back (a
; BLOAD's, a later step's); a text one's (LDREOF) is a CHAIN's end if one
; is under way (CHNFLG: CHNRET, which MAIN far calls on LDRUN, as for a run:
; OO-1, the far call rule), else it ends the load (NOTCHN): every file
; but the others NLONLY keeps may be closed, file 0 closed (PRGFIN); then
; not run, "Ok" and a CR, which ends the line being read; or run: the
; 8086's RUNC and NEWSTT are the home block's (OO-1: the far call rule),
; so LDRUN says so and the line ends with a CR, which DSKCHP (p4/scnedt.s)
; returns to MAIN as C set.
INCHR:          JSR     INCHRE
                BCC     INCHRX
                LDY     #F_NUM
                LDA     (FDBP),Y
                BEQ     FL0EOF
                JMP     DERRPE
FL0EOF:         LDY     #F_CODE
                LDA     (FDBP),Y
                CMP     #FC_BIN
                BNE     LDREOF
                SEC
                RTS
LDREOF:         LDA     CHNFLG         ; a CHAIN: its end, CHNRET (OO-1: by
                BEQ     NOTCHN         ; MAIN, which far calls it, as the run)
                LDA     #$80
                STA     LDRUN
                BRA     NOTCH2
NOTCHN:         LDA     NLONLY
                AND     #$80
                STA     NLONLY
                JSR     PRGFIN
                LDA     RUNFLG
                BEQ     NORUNC
                LDA     #$80           ; OO-1: run, by MAIN
                STA     LDRUN
                BRA     NOTCH2
NORUNC:         LDA     #<REDDY        ; "Ok"
                STA     PTR
                LDA     #>REDDY
                STA     PTR+1
                JSR     STROUT
NOTCH2:         LDA     #13
INCHRX:         RTS

; INDSKC, INCHRE: the next byte from PTRFIL's file, C set at its end.
; INCHSI: FDBP's. LPT1 can't be read (Bad file mode); the keyboard's is
; INCHRI. A file for output can't (Bad file mode); one whose end has been
; found is at its end again; a byte backed up comes first; else the
; device's (DSKSIN). At the end F_ORCT and F_BREM are 0, A as it came.
INDSKC:
INCHRE:         LDA     PTRFIL
                STA     FDBP
                LDA     PTRFIL+1
                STA     FDBP+1
INCHSI:         LDA     FDBP           ; $FFFF: LPT1
                AND     FDBP+1
                INC     A
                BEQ     ERBFM6
                LDA     FDBP           ; 0: the keyboard
                ORA     FDBP+1
                BNE     INGFDB
                JMP     INCHRI
INGFDB:         LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_SQO
                BEQ     ERBFM6
                LDY     #F_ORCT
                LDA     (FDBP),Y
                BEQ     INCEOF
                LDY     #F_FLGS
                LDA     (FDBP),Y
                BMI     GETBKC         ; FL_BKC
                LDX     #G_SIN
                JSR     SIDSP
                BCS     INCEOF
                RTS
INCEOF:         PHA
                LDA     #0
                LDY     #F_ORCT
                STA     (FDBP),Y
                INY                    ; F_BREM
                STA     (FDBP),Y
                PLA
                SEC
                RTS
GETBKC:         AND     #$FF - FL_BKC
                STA     (FDBP),Y
                LDY     #F_BAKC
                LDA     (FDBP),Y
                CLC
                RTS
ERBFM6:         JMP     DERBFM

; BAKCHR (1074): A backed up into PTRFIL's file; BCHRSI: FDBP's. The next
; INCHSI gives it.
BAKCHR:         PHA
                LDA     PTRFIL
                STA     FDBP
                LDA     PTRFIL+1
                STA     FDBP+1
                PLA
BCHRSI:         LDY     #F_BAKC
                STA     (FDBP),Y
                LDY     #F_FLGS
                LDA     (FDBP),Y
                ORA     #FL_BKC
                STA     (FDBP),Y
                RTS

; INCHRI (GIOKYB.ASM's, the OEM's: the project's own): the next key typed,
; not echoed, waited for (CHSNS: the keys polled, ^C breaks).
INCHRI:         JSR     CHSNS
                BEQ     INCHRI
                RTS

; --- BINSAV, DEVBOT, FSTLOD, SCDASC (GIO86.ASM 1095) ---------------------------------------

; BINSAV: a tokenised SAVE: the line pointers back to numbers (SCCPTR, P2's),
; not of a protected program (PROCHK, inline: Illegal function call),
; binary (SCDBIN), 255 first; BINPSV: A first (PROSAV's 254), then the text,
; TXTTAB to VARTAB, by the device's block output (OUTBLK: the disk's
; DEVBOT); then PRGFIN (file 0 closed, TXTPTR back). P4 sends each byte to
; the device itself, as the 8086's OUTDO (FILOU3) does, not through the
; resident OUTDO's run.
BINSAV:         FJSR    SCCPTR
                LDA     PROFLG
                BEQ     :+
                JMP     FCERR
:               JSR     SCDBIN
                LDA     #255
BINPSV:         LDX     #G_SOT
                JSR     PTRDSP
                SEC                    ; CX: the bytes to save
                LDA     VARTAB
                SBC     TXTTAB
                STA     RCL
                LDA     VARTAB+1
                SBC     TXTTAB+1
                STA     RCH
                BCC     BINSVX         ; none (JBE)
                ORA     RCL
                BEQ     BINSVX
                LDA     TXTTAB
                STA     RBL
                LDA     TXTTAB+1
                STA     RBH
                JSR     OUTBLK
BINSVX:         JMP     PRGFIN

; DEVBOT (1144): CX bytes from BX to PTRFIL's file, each by the device's
; byte output; BX past them. OUTBLK (1164): PTRFIL's block output.
DEVBOT:         LDA     RCL
                ORA     RCH
                BEQ     RET27
                LDA     (RBL)
                LDX     #G_SOT
                JSR     PTRDSP
                INCW    RBL
                LDA     RCL
                BNE     :+
                DEC     RCH
:               DEC     RCL
                BRA     DEVBOT
RET27:          RTS
OUTBLK:         LDX     #G_BOT
                JMP     PTRDSP

; FSTLOD (1190): a tokenised program to BX (TXTTAB), its first byte (255 or
; 254) read: CX the most it may take, FRETOP - 86 - BX; binary (SCDBIN);
; the block read (INPBLK: the disk's DFSTLD). BX returns past it. OO-1:
; whether it fitted is BINLOD's (p4/dskcom.s), by the program's links, not
; the read's C (the 8086's OUTLOD when the file is longer than CX): CP/M's
; last record runs past the program's end.
FSTLOD:         SEC
                LDA     FRETOP
                SBC     #86
                STA     RCL
                LDA     FRETOP+1
                SBC     #0
                STA     RCH
                SEC
                LDA     RCL
                SBC     RBL
                STA     RCL
                LDA     RCH
                SBC     RBH
                STA     RCH
                BCS     :+
                STZ     RCL            ; (OO-1: no room at all is none)
                STZ     RCH
:               JSR     SCDBIN
INPBLK:         LDX     #G_BIN
                JMP     PTRDSP

; SCDASC (1709), SCDBIN (1719): PTRFIL's file text, or binary (F_CODE).
SCDASC:         LDA     #FC_ASC
                BRA     SCDCOD
SCDBIN:         LDA     #FC_BIN
SCDCOD:         PHA
                LDA     PTRFIL
                STA     FDBP
                LDA     PTRFIL+1
                STA     FDBP+1
                PLA
                LDY     #F_CODE
                STA     (FDBP),Y
                RTS

; --- FACDSP, FILDSP, PTRDSP, SIDSP (GIO86.ASM 1343) ---------------------------------------

; FACDSP: the device's function X for file FAC (0 is Bad file number);
; FILDSP: for file A, which must be open. PTRDSP: for PTRFIL's FDB; SIDSP:
; for FDBP's. The function's own registers come back (A, BX). OO-1: the
; disk is the only device (its DSKDSP, p4/giodsk.s): an FDB of another
; (F_DEV negative) is Device Unavailable, as TBLDSP's empty entry is; the
; console's and LPT1's pseudo FDBs (0, $FFFF) are the OEM's OUTDO's, and
; never come here.
FACDSP:         STX     GFCODE         ; the 8086's AH
                JSR     CONINT
                CMP     #0
                BEQ     ERBFN2
FILDSP:         JSR     FDBPTR
                BEQ     ERFNO2
                LDX     GFCODE
                BRA     SIDSP
ERBFN2:         JMP     DERBFN
ERFNO2:         JMP     DERFNO
PTRDSP:         PHA
                LDA     PTRFIL
                STA     FDBP
                LDA     PTRFIL+1
                STA     FDBP+1
                PLA
SIDSP:          PHA
                LDY     #F_DEV
                LDA     (FDBP),Y
                BMI     ERDNA2
                PLA
                JMP     (DSKDSP,X)
ERDNA2:         JMP     DERDNA

; --- NAMSCN, PARDEV (GIO86.ASM 1486) ------------------------------------------------------

; NAMSCN: a file's name, a string formula; NAMSC1: the one just evaluated.
; FILDEV its drive (0 the current one, 1 A:), FILNM its name and type, 8
; and 3, padded with spaces: a "." starts the type, which C returns set
; for. "*", "?" or a control character is Bad file name; so is a name with
; only a type, or a "." in the type. A name of more than 8 with no "."
; runs on into the type, and what passes 11 is dropped (the 8086's). OO-1:
; the name goes upper case, as CP/M's FCBs take names. TXTPTR is kept. X
; counts the bytes put (the 8086's DX is FNAML less it).
NAMSCN:         JSR     FRMEVL
NAMSC1:         JSR     FRESTR         ; BX: the descriptor
                LDA     (RBL)
                STA     RCL            ; CX: its length
                LDY     #1
                LDA     (RBL),Y
                STA     PTR2           ; SI: its text
                INY
                LDA     (RBL),Y
                STA     PTR2+1
                JSR     PARDEV
                STA     FILDEV
                CLC                    ; "." not scanned
                PHP
                LDX     #0             ; DI: FILNM (the disk's: no other device)
SCAN1:          LDA     RCL
                BEQ     FILSPC         ; the end
                DEC     RCL
                LDA     (PTR2)
                INCW    PTR2
                CMP     #'*'           ; no wild cards
                BEQ     ERIFN1
                CMP     #'?'
                BEQ     ERIFN1
                CMP     #' '
                BCS     SCNAM2
ERIFN1:         JMP     DERIFN
SCNAM2:         CMP     #'.'
                BEQ     FILLNM
                JSR     UPCASE         ; OO-1
                STA     FILNM,X
                INX
                CPX     #FNAML
                BNE     SCAN1
SCNAM3:         PLP
                RTS
FILLNM:         PLP                    ; "." scanned
                SEC
                PHP
FILNM0:         CPX     #0             ; a type alone
                BEQ     ERIFN1
                CPX     #FNAML - 3 + 1 ; a second "."
                BCS     ERIFN1
                CPX     #FNAML - 3     ; the type next
                BEQ     SCAN1
                LDA     #' '
                STA     FILNM,X
                INX
                BRA     FILNM0
FILSPC:         LDA     #' '           ; a short name padded
                STA     FILNM,X
                INX
                CPX     #FNAML
                BNE     FILSPC
                BRA     SCNAM3

; PARDEV (1577): a device at the name's start, CX bytes at SI: A its number
; (0 none), CX and SI past its ":". A ":" first is Bad file name; one letter
; is a drive, OO-1 A: to P: (1-16, CP/M's; the 8086's A: to Y:); a longer
; name is a device of the table, which (OO-1) has none yet: Bad file name,
; as for a name the 8086's table hasn't.
PARDEV:         LDY     #0             ; the ":"'s place
@scan:          CPY     RCL
                BEQ     NODVNM
                LDA     (PTR2),Y
                CMP     #':'
                BEQ     DEVNM
                INY
                BRA     @scan
NODVNM:         LDA     #0             ; no device
                RTS
DEVNM:          CPY     #0             ; the device's name: Y bytes
                BEQ     ERIFN2
                CPY     #1
                BNE     ERIFN2         ; DEVSRC: no device but the disk's (OO-1)
DSKNAM:         LDA     (PTR2)
                JSR     UPCASE
                SEC
                SBC     #'A' - 1       ; A is 1
                BCC     ERIFN2
                CMP     #16 + 1        ; OO-1: past P:
                BCS     ERIFN2
                PHA                    ; DEVRET: past "x:"
                CLC
                LDA     PTR2
                ADC     #2
                STA     PTR2
                BCC     :+
                INC     PTR2+1
:               DEC     RCL
                DEC     RCL
                PLA
                RTS
ERIFN2:         JMP     DERIFN

UPCASE:         CMP     #'a'
                BCC     UPRET
                CMP     #'z' + 1
                BCS     UPRET
                AND     #$FF - ' '
UPRET:          RTS

; --- INIFDB, FDBPTR, FALLOC, FFREE (GIO86.ASM 1683) ---------------------------------------

; INIFDB: an FDB for file OPNNUM, of device A, if FILMOD is one of the modes
; in X (else Bad file mode); RDL its width, RDH its column. Its fields set,
; F_ORCT 1 (not at the end); PTRFIL and FDBP at it. OO-1: FALLOC's FDB is
; the table's for the number (GETNZB keeps it in range), and FILOPN
; counts it.
INIFDB:         STA     T0             ; the device
                TXA
                AND     FILMOD
                BEQ     ERBFM4
                JSR     FALLOC
                LDY     #F_NUM
                LDA     OPNNUM
                STA     (FDBP),Y
                LDY     #F_DEV
                LDA     T0
                STA     (FDBP),Y
                LDY     #F_FLGS
                LDA     #0
                STA     (FDBP),Y
                LDY     #F_CODE
                LDA     #FC_ASC
                STA     (FDBP),Y
                LDY     #F_MODE
                LDA     FILMOD
                STA     (FDBP),Y
                LDY     #F_WID
                LDA     RDL
                STA     (FDBP),Y
                LDY     #F_POS
                LDA     RDH
                STA     (FDBP),Y
                LDY     #F_ORCT
                LDA     #1
                STA     (FDBP),Y
                INC     FILOPN
                LDA     FDBP           ; PTRFIL at it
                LDX     FDBP+1
                JMP     SETPTR
ERBFM4:         JMP     DERBFM

; FACFPT: FDBPTR for file FAC. FDBPTR: file A's FDB in FDBP, Z clear if it
; is open; Z set if not (a number past the table's too). A is kept.
FACFPT:         JSR     CONINT
FDBPTR:         CMP     #NMFILE + 1
                BCS     @none
                PHA
                ASL     A
                TAX
                LDA     FDBTAB,X
                STA     FDBP
                LDA     FDBTAB+1,X
                STA     FDBP+1
                LDY     #F_MODE
                LDA     (FDBP),Y
                TAX
                PLA
                CPX     #0
                RTS
@none:          LDX     #0
                RTS

; FALLOC (GIO86.ASM 1765): OO-1, file OPNNUM's FDB of the table in FDBP
; (the 8086 makes room for the whole FDB below the strings); F0OPN (OO-1) is
; set while file 0 has its FDB, so FINPRT knows to close it. Then a random
; file's FIELD buffer, CX bytes (DSKOPN's length; 0 for any other file: no
; buffer), as the 8086 takes its FDB: room in the string space (FRETOP -
; STREND more than CX), after a collection if there isn't, else Out of
; memory; the string space moved down CX (XFRSTR, SI at FILTAB), and the
; buffer is the new FILTAB (OO-1: FILTAB is always MEMSIZ + 2, as start-up
; and CLEAR leave it, so it has no byte of its own). FALDL is the 8086's DL.
FALLOC:         LDA     OPNNUM
                BNE     :+
                DEC     A              ; file 0: F0OPN for FINPRT (OO-1)
                STA     F0OPN
                INC     A
:               ASL     A
                TAX
                LDA     FDBTAB,X
                STA     FDBP
                LDA     FDBTAB+1,X
                STA     FDBP+1
                LDA     RCL            ; a FIELD buffer?
                ORA     RCH
                BEQ     FALCX
                LDA     #1             ; DL: the first attempt
                STA     FALDL
FALC1:          SEC                    ; BX: FRETOP - STREND
                LDA     FRETOP
                SBC     STREND
                STA     RBL
                LDA     FRETOP+1
                SBC     STREND+1
                STA     RBH
                LDA     RCL            ; CX below it: room
                CMP     RBL
                LDA     RCH
                SBC     RBH
                BCC     FALCOK
                DEC     FALDL
                BEQ     FAPAS1         ; the first attempt: collect
                LDA     #ERROM
                JMP     ERROR
FAPAS1:         LDA     RCL            ; PUSH AX round the collection
                PHA
                LDA     RCH
                PHA
                JSR     GARBA2
                PLA
                STA     RCH
                PLA
                STA     RCL
                BRA     FALC1
FALCOK:         JSR     FILTAB         ; SI: FILTAB
                STA     RSI
                STX     RSI+1
                SEC                    ; DX: -CX, down
                LDA     #0
                SBC     RCL
                STA     RDL
                LDA     #0
                SBC     RCH
                STA     RDH
                JSR     XFRSTR
                LDA     OPNNUM         ; the buffer: FILTAB now
                ASL     A
                TAY
                JSR     FILTAB
                STA     FDDAT,Y
                TXA
                STA     FDDAT+1,Y
FALCX:          RTS
; FILTAB (OO-1): A/X the 8086's FILTAB, the lowest FIELD buffer's address
; or, with none, the string space's top: MEMSIZ + 2 (VAL's zero between).
FILTAB:         CLC
                LDA     MEMSIZ
                ADC     #2
                PHA
                LDA     MEMSIZ+1
                ADC     #0
                TAX
                PLA
                RTS

; FFREE (GIO86.ASM 1814): FDBP's FDB freed (OO-1: marked free, F0OPN clear
; for file 0), and a random file's FIELD buffer given back: AX its size
; (FD_SIZ); each FIELD buffer below it moves up with the strings, so its
; address goes up by AX (FFNDL, STILOK: the 8086's chain's FDBs below it);
; then XFRSTR, SI the buffer, DX AX, which makes each string FIELDed into it
; null. FFIDX is its number's place in FDDAT.
FFREE:          LDY     #F_NUM
                LDA     (FDBP),Y
                ASL     A
                TAX
                LDA     FDDAT,X
                ORA     FDDAT+1,X
                BEQ     FFREE2         ; no FIELD buffer
                LDA     FDDAT,X        ; SI: the buffer
                STA     RSI
                LDA     FDDAT+1,X
                STA     RSI+1
                STZ     FDDAT,X
                STZ     FDDAT+1,X
                LDY     #FD_SIZ        ; AX (DX): its size
                LDA     (FDBP),Y
                STA     RDL
                INY
                LDA     (FDBP),Y
                STA     RDH
                LDX     #0
FFNDL:          LDA     FDDAT,X        ; each other buffer below it
                ORA     FDDAT+1,X
                BEQ     FFNXT
                LDA     FDDAT,X
                CMP     RSI
                LDA     FDDAT+1,X
                SBC     RSI+1
                BCS     FFNXT
                CLC                    ; STILOK: up by AX
                LDA     FDDAT,X
                ADC     RDL
                STA     FDDAT,X
                LDA     FDDAT+1,X
                ADC     RDH
                STA     FDDAT+1,X
FFNXT:          INX
                INX
                CPX     #2 * (NMFILE + 1)
                BNE     FFNDL
                JSR     XFRSTR         ; FFOUND
FFREE2:         LDY     #F_MODE
                LDA     #0
                STA     (FDBP),Y
                DEC     FILOPN
                LDY     #F_NUM         ; file 0: F0OPN clear
                LDA     (FDBP),Y
                BNE     :+
                STZ     F0OPN
:               RTS

; XFRSTR (GIO86.ASM 1858): the string space moved by DX bytes (negative:
; down), from FRETOP + 1 to SI - 1 (SI: FILTAB, to make room below it; a
; freed FIELD buffer's address, to close it up: what is below it moves up);
; then every string variable's and string array element's descriptor
; (UPDSTD): data that moved moves with it; data in the freed buffer (above
; the block's top, up to its top's new place) is a FIELDed string, made null.
; Then FRETOP and MEMSIZ by DX, and with MEMSIZ FILTAB (OO-1: MEMSIZ + 2).
; OO-1: PTRFIL (an FDB in P4's RAM) never moves. SAVFRE (CHAIN's) moves by
; DX too. RSI is the 8086's SI less one (the block's top), RDI its DI (that
; top's new place), RCX its count; PTR and PTR2 the move's places; BX the
; descriptor walk's, the 8086's.
XFRSTR:         DECW    RSI            ; SI: the block's top
                CLC                    ; DI: where it goes
                LDA     RSI
                ADC     RDL
                STA     RDI
                LDA     RSI+1
                ADC     RDH
                STA     RDI+1
                SEC                    ; CX: SI - FRETOP bytes
                LDA     RSI
                SBC     FRETOP
                STA     RCL
                LDA     RSI+1
                SBC     FRETOP+1
                STA     RCH
                ORA     RCL
                JEQ     XFRSTX         ; the string space empty
                LDA     RDH
                BPL     XFRSUP         ; up: from the top down
                SEC                    ; down: from the bottom up, SI - CX + 1
                LDA     RSI            ; to DI - CX + 1
                SBC     RCL
                STA     PTR
                LDA     RSI+1
                SBC     RCH
                STA     PTR+1
                INCW    PTR
                SEC
                LDA     RDI
                SBC     RCL
                STA     PTR2
                LDA     RDI+1
                SBC     RCH
                STA     PTR2+1
                INCW    PTR2
@dn:            LDA     (PTR)
                STA     (PTR2)
                INCW    PTR
                INCW    PTR2
                JSR     XFRCNT
                BNE     @dn
                BRA     XFRVAR
XFRSUP:         LDA     RSI
                STA     PTR
                LDA     RSI+1
                STA     PTR+1
                LDA     RDI
                STA     PTR2
                LDA     RDI+1
                STA     PTR2+1
@up:            LDA     (PTR)
                STA     (PTR2)
                DECW    PTR
                DECW    PTR2
                JSR     XFRCNT
                BNE     @up
XFRVAR:         LDA     VARTAB         ; the simple variables
                STA     RBL
                LDA     VARTAB+1
                STA     RBH
CSVAR:          LDA     RBL
                CMP     ARYTAB
                BNE     :+
                LDA     RBH
                CMP     ARYTAB+1
                BEQ     CAYVAR
:               JSR     SKPNAM         ; a string?
                BNE     CSKPVA
                JSR     UPDSTD
                BRA     CSVAR
CSKPVA:         CLC                    ; past its value, VALTYP bytes
                ADC     RBL
                STA     RBL
                BCC     CSVAR
                INC     RBH
                BRA     CSVAR
CAYVA2:         LDA     ARYEND         ; BX: the next array
                STA     RBL
                LDA     ARYEND+1
                STA     RBH
CAYVAR:         LDA     RBL            ; the arrays, to STREND
                CMP     STREND
                BNE     :+
                LDA     RBH
                CMP     STREND+1
                BEQ     XFRSTX
:               JSR     SKPNAM
                PHP                    ; a string array?
                CLC                    ; AX: past its length, the array's end
                LDA     (RBL)
                ADC     #2
                PHA
                LDY     #1
                LDA     (RBL),Y
                ADC     #0
                TAX
                PLA
                CLC
                ADC     RBL
                STA     ARYEND
                TXA
                ADC     RBH
                STA     ARYEND+1
                INCW    RBL
                INCW    RBL
                PLP
                BNE     CAYVA2         ; not strings
                LDA     (RBL)          ; past its dimensions, 2 bytes each, and their count
                ASL     A
                BCC     :+
                INC     RBH
:               SEC
                ADC     RBL
                STA     RBL
                BCC     CAYSTR
                INC     RBH
CAYSTR:         LDA     RBL            ; each element
                CMP     ARYEND
                BNE     :+
                LDA     RBH
                CMP     ARYEND+1
                BEQ     CAYVAR
:               JSR     UPDSTD
                BRA     CAYSTR
XFRSTX:         CLC                    ; FRETOP and MEMSIZ by DX (XFSTX1)
                LDA     FRETOP
                ADC     RDL
                STA     FRETOP
                LDA     FRETOP+1
                ADC     RDH
                STA     FRETOP+1
                CLC
                LDA     MEMSIZ
                ADC     RDL
                STA     MEMSIZ
                LDA     MEMSIZ+1
                ADC     RDH
                STA     MEMSIZ+1
                CLC                    ; and SAVFRE, CHAIN's (p4/fiveo.s)
                LDA     SAVFRE
                ADC     RDL
                STA     SAVFRE
                LDA     SAVFRE+1
                ADC     RDH
                STA     SAVFRE+1
                RTS
; (The project's own: CX one less, Z at 0.)
XFRCNT:         LDA     RCL
                BNE     :+
                DEC     RCH
:               DEC     RCL
                LDA     RCL
                ORA     RCH
                RTS

; UPDSTD (GIO86.ASM 1950): the descriptor at BX, BX past it. Its data (CX)
; at or below FRETOP: not moved; at or below SI: moved, its address by DX;
; at or below DI (in the freed buffer): null, and moved; above: not moved.
UPDSTD:         LDY     #1
                LDA     (RBL),Y
                STA     RCL
                INY
                LDA     (RBL),Y
                STA     RCH
                LDA     FRETOP         ; CX <= FRETOP: below the strings
                CMP     RCL
                LDA     FRETOP+1
                SBC     RCH
                BCS     UPDSTX
                LDA     RSI            ; CX <= SI: moved
                CMP     RCL
                LDA     RSI+1
                SBC     RCH
                BCS     UPDSTU
                LDA     RDI            ; CX > DI: not in the freed buffer
                CMP     RCL
                LDA     RDI+1
                SBC     RCH
                BCC     UPDSTX
                LDA     #0             ; a FIELDed string: null
                STA     (RBL)
UPDSTU:         LDY     #1
                CLC
                LDA     RCL
                ADC     RDL
                STA     (RBL),Y
                INY
                LDA     RCH
                ADC     RDH
                STA     (RBL),Y
UPDSTX:         CLC                    ; BX past it
                LDA     RBL
                ADC     #3
                STA     RBL
                BCC     :+
                INC     RBH
:               RTS

; SKPNAM (FIVEO.ASM 701): BX at a variable: A its VALTYP, BX past its name
; (the 8086's IADAHL: the type, two letters, the count of the rest, the
; rest), Z set if it is a string.
SKPNAM:         LDA     (RBL)
                PHA
                LDY     #3
                LDA     (RBL),Y
                CLC
                ADC     #4
                ADC     RBL
                STA     RBL
                BCC     :+
                INC     RBH
:               PLA
                CMP     #VT_STR
                RTS

; --- GET and PUT (GIO86.ASM 627) ----------------------------------------------------------

; DPUTG: GET or PUT [#]n[, record], X 0 GET, 1 PUT (the 8086's CX, PGTFLG
; here). The file must be open (DERFNO) and random (Bad file mode); with a
; record (ADRGET's 0..65535, P1's: a far call), A is 2 GET or 3 PUT, else
; 0 or 1, for the device's random function (G_RND: the disk's DSKRND). FDBP
; is kept round the record's formula, as the 8086's PUSH SI (a function in
; it, LOF say, finds an FDB of its own).
DPUTG:          STX     PGTFLG
                JSR     POFNUM
                JSR     FDBPTR
                BEQ     ERFNO5
                LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_RND
                BNE     ERBFM1
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     RELRND         ; the statement's end: the next record
                JSR     SYNCHR
                .byte   ','
                LDA     FDBP
                PHA
                LDA     FDBP+1
                PHA
                FJSR    ADRGET         ; DX: the record
                PLA
                STA     FDBP+1
                PLA
                STA     FDBP
                LDA     PGTFLG
                CLC
                ADC     #2
                BRA     RELRN1
RELRND:         LDA     PGTFLG
RELRN1:         LDX     #G_RND
                JMP     SIDSP
ERFNO5:         JMP     DERFNO
ERBFM1:         JMP     DERBFM

; GETDAT (the project's own): A/X FDBP's FIELD buffer, the 8086's FD_DAT
; (FDDAT, by its number). Y used.
GETDAT:         LDY     #F_NUM
                LDA     (FDBP),Y
                ASL     A
                TAY
                LDA     FDDAT,Y
                LDX     FDDAT+1,Y
                RTS

; --- OUTDO's run, FINPRT's part, SETPTR (the project's own) -------------------------------

; FLUSHP: OUTDO's run full (res/oem.s). FLUSHR: the bytes OUTDO left in
; RUNBUF sent to PTRFIL's file, each as the 8086's OUTDO sends one (G_SOT,
; PTRDSP: DSKSOT). RUNLEN is 0 before the first, so an error on the way
; (Disk full) leaves nothing for FINPRT to send again. FDBP and A are
; kept; X and Y used.
FLUSHP:
FLUSHR:         PHA
                LDA     RUNLEN
                BEQ     @x
                STA     FLCNT
                STZ     RUNLEN
                STZ     FLIDX
                LDA     FDBP
                PHA
                LDA     FDBP+1
                PHA
@l:             LDX     FLIDX
                LDA     RUNBUF,X
                LDX     #G_SOT
                JSR     PTRDSP
                INC     FLIDX
                DEC     FLCNT
                BNE     @l
                PLA
                STA     FDBP+1
                PLA
                STA     FDBP
@x:             PLA
                RTS

; FINPRP: FINPRT's part (GIO86.ASM 2124, res/oem.s), when OUTDO's run waits,
; FREFDB is set or file 0 is open: the run sent, FREFDB's FDB freed, and
; file 0 closed (CLSFIL: not while a load is under way).
FINPRP:         JSR     FLUSHR
                LDA     FREFDB+1
                BEQ     NFRFDB
                STA     FDBP+1
                LDA     FREFDB
                STA     FDBP
                JSR     FFREE
NFRFDB:         STZ     FREFDB
                STZ     FREFDB+1
                LDA     #0
                JMP     CLSFIL

; SETPTR: PTRFIL = A/X (the 8086's MOV PTRFIL), OUTDO's run for the file it
; was first sent; for a file, FDBP at its FDB and FILPOS its column.
SETPTR:         PHA
                PHX
                JSR     FLUSHR
                PLX
                PLA
                STA     PTRFIL
                STX     PTRFIL+1
                CPX     #0             ; the console
                BEQ     @r
                CPX     #$FF           ; LPT1
                BEQ     @r
                STA     FDBP
                STX     FDBP+1
                LDY     #F_POS
                LDA     (FDBP),Y
                STA     FILPOS
@r:             RTS

; --- The errors (GWMAIN.ASM 397, DERMAK) ----------------------------------------------------

DERBFN:         LDA     #ERRBFN
                BRA     DERGO
DERFNF:         LDA     #ERRFNF
                BRA     DERGO
DERBFM:         LDA     #ERRBFM
                BRA     DERGO
DERFNO:         LDA     #ERRFNO
                BRA     DERGO
DERFAO:         LDA     #ERRFAO
                BRA     DERGO
DERIOE:         LDA     #ERRIOE
                BRA     DERGO
DERDFL:         LDA     #ERRDFL
                BRA     DERGO
DERRPE:         LDA     #ERRRPE
                BRA     DERGO
DERIFN:         LDA     #ERRNMF
                BRA     DERGO
DERTMF:         LDA     #ERRTMF
                BRA     DERGO
DERFAE:         LDA     #ERRFAE
                BRA     DERGO
DERRAD:         LDA     #ERRRAD
                BRA     DERGO
DERFOV:         LDA     #ERRFOV
                BRA     DERGO
DERBRN:         LDA     #ERRBRN
                BRA     DERGO
DERDNA:         LDA     #ERRDNA
DERGO:          JMP     ERROR

.rodata
; FDBTAB: each file number's FDB, 0 to NMFILE.
FDBTAB:
.repeat NMFILE + 1, i
                .word   FDB0 + i * FDBLEN
.endrepeat

.segment "BRAM"
; GWDATA.ASM's, and what the 8086 keeps on its stack
FILMOD:         .res    1              ; the mode an OPEN asks for
FILNAM:                                ; the name an OPEN gives: its drive, name and type
FILDEV:         .res    1              ; 0 the current drive, 1 A:
FILNM:          .res    FNAML
                .res    36 - 1 - FNAML ; the rest of an FCB: FILFCB's (KILL, NAME, FILES; F_PARSE fills 0-23)
RUNFLG:         .res    1              ; a load's: 0 not run, 1 run, $81 run with the files kept
LDRUN:          .res    1              ; OO-1: bit 7, a text load's end runs it (MAIN's NEWSTT)
OPNNUM:         .res    1              ; the number of the file being opened (the 8086's BX)
CLSNUM:         .res    1              ; CLSALP's number
GFCODE:         .res    1              ; FACDSP's function (the 8086's AH)
FLCNT:          .res    1              ; FLUSHR's count and place
FLIDX:          .res    1
FALDL:          .res    1              ; FALLOC's attempt (the 8086's DL)
ARYEND:         .res    2              ; XFRSTR's array's end (the 8086's AX)
PGTFLG:         .res    1              ; DPUTG's GET or PUT (GWDATA.ASM's PGTFLG; the 8086's CX)
; FDDAT: each file number's FIELD buffer in the TPA (the 8086's FD_DAT), 0
; for none (not random, or not open).
FDDAT:          .res    2 * (NMFILE + 1)
; The FDBs (V1, route (c)): the table's, each FDBLEN bytes, its record's
; buffer at DATOFS, where the BDOS reads and writes it.
FDB0:           .res    (NMFILE + 1) * FDBLEN

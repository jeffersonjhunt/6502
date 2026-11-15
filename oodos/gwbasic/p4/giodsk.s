; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIODSK.ASM's disk device, translated, in block P4, the file layer
; (hw/oodos.md, "The plan, phase 7": 7c): the disk's function table and its
; sequential files, on OODOS's FCB functions (the 8086's MS-DOS FCB calls,
; MSDOSU's C_*: CP/M's numbers, oodos.inc's names):
;
;   DSKDSP          the disk's functions, by GIO86U's G_* (SIDSP's table)
;   DSKEOF DSKLOC   EOF, LOC and LOF of a disk file
;   DSKLOF
;   DSKGPS DSKGWD   its column, its width (255), the comma width (nothing)
;   DSKSCW DSKGCW
;   DSKOPN          a disk file opened: its FDB, its FCB; OUTPUT made anew,
;                   INPUT found and its first record read, APPEND opened at
;                   its end (or made); File already open (CHKFOP) for an
;                   OUTPUT or APPEND of a file open already
;   DSKCLS          closed: an output file's ^Z and last record first
;   DSKSIN DSKINP   a byte in: ^Z or the last record's end is the end
;   DSKSOT FILOU4   a byte out, a record written when the buffer is full
;   READS WRITES    a record read or written ($READS, $WRITS), at the
;                   random record CP/M's R0-R2 holds, which ACCFIL moves on
;   RESETP          RESET: every file closed, the drives reset
;   FILESP          FILES: the directory's names that match, or every one
;   KILLP NAMEP     KILL, NAME: a file deleted, renamed (the BDOS's 19, 23)
;   NAMFIL FILFCB   their names: a string, made an FCB by function 152
;   DFSTLD          a LOAD's block input: whole records straight to the
;                   workspace by function 83's DM_LOAD (W2), the last part
;                   through the buffer
;   PROSAV          SAVE ,P: the program saved encoded (PENCOD), then decoded
;   PENCOD PDECOD   (PDECOD, which LOAD's PROLOD is)
;
; The FDB is P4's (p4/gio86.s, gwbasic.inc): FDBP, the 8086's SI, points at
; it. Each record goes straight to or from the buffer in it: SETBUF makes its
; CPU address in W3, where P4 is, the DMA address (V1, route (c)); the BDOS
; reaches it through the windows its entry saved, W3 among them.
;
; OO-1, where CP/M isn't MS-DOS:
;   - CP/M files are whole 128-byte records, and CP/M's FCB has no byte size:
;     LOF is the records times 128 (function 35), and APPEND finds the end by
;     the last record's ^Z (the 8086's, for a size that is a whole record);
;   - an FCB's drive 0 (the current one) is made the drive itself once the
;     file is open, as MS-DOS's open does (CHKFOP compares the drives);
;   - CP/M's codes for 33 and 34 are those ACCFIL maps (it maps CP/M 2's
;     already: 3 cannot close the extent, 5 the directory full);
;   - random files are X1's (later 7c): an OPEN for random is Advanced
;     Feature until then, and the random parts here are kept, unreached.

.include "gwbasic.inc"

;@entry RESETP
;@entry FILESP
;@entry KILLP
;@entry NAMEP
.export DSKDSP, DSKOPN, RESETP, PROSAV, PDECOD, FILESP, KILLP, NAMEP
.import FREFDB, FILNAM, FILMOD, INIFDB, CLSALP, FDBTAB
.import DEVBOT, BINPSV, GTMPRT, CHRGTR
.import DERTMF, DERFNF, DERBFM, DERFAO, DERIOE, DERDFL, FCERR, ADVERR, FRCDBL
.import DERIFN, DERFAE, DERRAD, FRMEVL, FRESTR, SYNCHR, OUTDO, CRDO, CRDONZ, POLKEY, LINLEN, BUF

ASCCTZ          = 26                   ; ^Z, a text file's end
FNAML           = 11                   ; MSDOSU's: a name and type's bytes

.code

; --- DSKEOF, DSKLOC, DSKLOF (GIODSK.ASM 49) ----------------------------------------------

; DSKEOF: BX -1 at the file's end, else 0: Bad file mode for an output file.
; The end: F_ORCT 0, or the next byte a ^Z (the buffer refilled first if
; it is used up). A random file is never at its end.
DSKEOF:         LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_SQO
                BEQ     ER_BFM
ORNCHK:         LDY     #F_ORCT
                LDA     (FDBP),Y
                BEQ     WASEOF
                LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_RND
                BEQ     NOTEOF
                LDY     #F_BREM
                LDA     (FDBP),Y
                BNE     CHKCTZ
                JSR     READS
                BRA     ORNCHK
CHKCTZ:         EOR     #$FF           ; DATPSC - F_BREM: the next byte's place
                SEC
                ADC     #DATOFS + DATPSC
                TAY
                LDA     (FDBP),Y
                CMP     #ASCCTZ
                BNE     NOTEOF
WASEOF:         LDA     #$FF
                STA     RBL
                STA     RBH
                RTS
NOTEOF:         STZ     RBL
                STZ     RBH
                RTS
ER_BFM:         JMP     DERBFM

; DSKLOC: BX the records read or written (F_CLOC), or a random file's last
; record (FD_LOG).
DSKLOC:         LDY     #F_MODE
                LDA     (FDBP),Y
                LDY     #F_CLOC
                CMP     #MD_RND
                BNE     LOC1
                LDY     #FD_LOG
LOC1:           LDA     (FDBP),Y
                STA     RBL
                INY
                LDA     (FDBP),Y
                STA     RBH
                RTS

; DSKLOF: the file's size in bytes, a double in the FAC. OO-1: CP/M's size
; is in records (function 35, to R0-R2, which are kept round it: they are
; the file's place), so the size is that times 128, made a single (the 24
; bits are exact there), then a double (the 8086's $NORMD of FCB_FS).
DSKLOF:         LDY     #FCB_RN + 2
@k:             LDA     (FDBP),Y
                STA     LOFRN - FCB_RN,Y
                DEY
                CPY     #FCB_RN
                BCS     @k
                LDY     #F_SIZE
                JSR     FCBDOS
                LDY     #FCB_RN        ; the records, 24 bits, to FACLO..FACM1
                LDA     (FDBP),Y
                STA     FACLO
                INY
                LDA     (FDBP),Y
                STA     FACLO+1
                INY
                LDA     (FDBP),Y
                STA     FACM1
                LDY     #FCB_RN + 2
@b:             LDA     LOFRN - FCB_RN,Y
                STA     (FDBP),Y
                DEY
                CPY     #FCB_RN
                BCS     @b
                LDA     #VT_SNG
                STA     VALTYP
                STZ     FACEXP         ; zero records: zero
                LDA     FACLO
                ORA     FACLO+1
                ORA     FACM1
                BEQ     @dbl
                LDA     #$80 + 24 + 7  ; 24 bits, times 128
                STA     FACEXP
@n:             BIT     FACM1          ; normalised: the top bit set
                BMI     @sgn
                DEC     FACEXP
                ASL     FACLO
                ROL     FACLO+1
                ROL     FACM1
                BRA     @n
@sgn:           LDA     FACM1          ; positive: the top bit is the sign's
                AND     #$7F
                STA     FACM1
@dbl:           JMP     FRCDBL

; DSKGPS: A the column. DSKGWD: A the width, 255 for a disk file. DSKSCW,
; DSKGCW: a disk file's comma width, nothing to set or get.
DSKGPS:         LDY     #F_POS
                LDA     (FDBP),Y
                RTS
DSKGWD:         LDA     #255
DSKSCW:
DSKGCW:         RTS

; --- DSKOPN (GIODSK.ASM 184) --------------------------------------------------------------

; DSKOPN: file OPNNUM opened on disk A (FILDEV: 0 the current drive, 1 A:),
; FILMOD its mode, FILNAM its name, CX a random file's length (0 the
; default, a record). The FDB (INIFDB: every mode, width 255, column 0) is
; FREFDB's until the file is open, so an error first frees it (FINPRT). Its
; FCB gets the drive, name and type, the rest zero. OUTPUT: no other file
; open of that name (CHKFOP), the old one deleted, a new one made (Too many
; files if the directory is full). INPUT, APPEND, random: opened; if it
; isn't there, APPEND and random make it, INPUT is File not found. Then:
; random, its buffer and fields zero; APPEND, at the end (APPFIN); INPUT,
; its first record read. FDBP returns at the FDB.
DSKOPN:         STA     ODEV           ; the device, till INIFDB
                LDA     FILMOD
                CMP     #MD_RND
                BNE     DSKOP2
                JMP     ADVERR         ; OO-1: X1's (later 7c): CX 0 is DATPSC there
DSKOP2:         LDA     RCL            ; PUSH CX: the length asked for
                STA     OLEN
                LDA     RCH
                STA     OLEN+1
                LDA     #255           ; DL: the width
                STA     RDL
                STZ     RDH            ; DH: the column
                LDX     #$FF           ; every mode is a disk's
                LDA     ODEV
                JSR     INIFDB
                LDA     FDBP
                STA     FREFDB
                LDA     FDBP+1
                STA     FREFDB+1
                LDY     #FCB_DV        ; the drive, the name and the type
                LDX     #0
@n:             LDA     FILNAM,X
                STA     (FDBP),Y
                INY
                INX
                CPX     #12
                BNE     @n
                LDA     #0             ; EX, S1, S2, RC (OO-1: MS-DOS's EX alone)
@x:             STA     (FDBP),Y
                INY
                CPY     #FCB_EX + 4
                BNE     @x
                LDY     #FCB_NR        ; CR and R0-R2
@r:             STA     (FDBP),Y
                INY
                CPY     #FCB_RN + 3
                BNE     @r
                LDY     #FD_SIZ
                LDA     OLEN
                STA     (FDBP),Y
                INY
                LDA     OLEN+1
                STA     (FDBP),Y
                JSR     SETBUF
                LDA     FILMOD
                CMP     #MD_APP
                BNE     NTOAPP
                JSR     CHKFOP
NTOAPP:         CMP     #MD_SQO
                BNE     OPNFIL
                JSR     CHKFOP
                LDY     #F_DELETE
                JSR     FCBDOS
MAKFIL:         LDY     #F_MAKE
                JSR     FCBDOS
                INC     A              ; $FF: the directory is full
                BNE     OPNSET
                JMP     DERTMF
OPNFIL:         LDY     #F_OPEN
                JSR     FCBDOS
                INC     A
                BNE     OPNSET
                LDA     FILMOD
                CMP     #MD_APP        ; APPEND: made (the 8086's AL = MD_SQO
                BEQ     MAKFIL         ; here goes nowhere: FILMOD stays APPEND)
NTAPNF:         CMP     #MD_RND
                BNE     ER_FNF
                BRA     MAKFIL
ER_FNF:         JMP     DERFNF
; OPNSET: open. (FCB_RC's 128: OO-1, CP/M's records are 128 bytes.) OO-1:
; the drive filled in, as MS-DOS's open does. F_CLOC, F_ORCT, F_BREM zero.
OPNSET:         LDY     #FCB_DV
                LDA     (FDBP),Y
                BNE     @drv
                LDY     #DRV_GET
                JSR     BDOS
                INC     A
                LDY     #FCB_DV
                STA     (FDBP),Y
@drv:           LDA     #0
                LDY     #F_CLOC
@z:             STA     (FDBP),Y
                INY
                CPY     #F_CLOC + 4
                BNE     @z
                LDA     FILMOD
                CMP     #MD_RND
                BEQ     RNDFIN
                CMP     #MD_APP
                BEQ     APPFIN
                CMP     #MD_SQI
                BNE     OPNFIN
                JSR     READS          ; the first record
OPNFIN:         STZ     FREFDB         ; open: FINPRT leaves it
                STZ     FREFDB+1
                RTS
; RNDFIN: FD_PHY to the FDB's end zero (X1's, unreached till then).
RNDFIN:         LDA     #0
                LDY     #FD_PHY
@z:             STA     (FDBP),Y
                INY
                CPY     #FDBLEN
                BNE     @z
                BRA     OPNFIN

; APPFIN: APPEND at the file's end: an empty file is OUTPUT's. Else the
; last record read, the place set back to it, its bytes counted to the
; first ^Z (or its end: then the place on past it, the count 0), and the
; file is an output file with that many in its buffer. OO-1: the size is
; CP/M's records (function 35's R0-R2), a whole number, so the last is the
; size less one (the 8086's BAKURN for a size that is a multiple of 128).
APPFIN:         LDY     #F_SIZE
                JSR     FCBDOS
                LDY     #FCB_RN
                LDA     (FDBP),Y
                INY
                ORA     (FDBP),Y
                INY
                ORA     (FDBP),Y
                BNE     NTZRF1
                LDY     #F_MODE
                LDA     #MD_SQO
                STA     (FDBP),Y
                BRA     OPNFIN
NTZRF1:         JSR     BAKURN         ; (a whole record: OO-1)
NOMTRC:         JSR     READS
                JSR     BAKURN
                STZ     APPCNT         ; DX: the bytes before the end
REDEOF:         JSR     DSKINP
                BCS     SETSQM
                CMP     #ASCCTZ
                BEQ     SETSQO
                INC     APPCNT
                BRA     REDEOF
SETSQM:         STZ     APPCNT         ; the record's end: past it
                JSR     BAKURN
SETSQO:         LDY     #F_MODE
                LDA     #MD_SQO
                STA     (FDBP),Y
                LDY     #F_CLOC
                LDA     #0
                STA     (FDBP),Y
                INY
                STA     (FDBP),Y
                INY                    ; F_ORCT: the bytes in the buffer
                LDA     APPCNT
                STA     (FDBP),Y
                LDY     #F_POS
                LDA     #0
                STA     (FDBP),Y
                BRA     OPNFIN

; BAKURN: the random record back one.
BAKURN:         LDY     #FCB_RN
                LDA     (FDBP),Y
                SEC
                SBC     #1
                STA     (FDBP),Y
                INY
                LDA     (FDBP),Y
                SBC     #0
                STA     (FDBP),Y
                BCS     BAKRET
                INY
                LDA     (FDBP),Y
                DEC     A
                STA     (FDBP),Y
BAKRET:         RTS

; --- DSKCLS (GIODSK.ASM 330) --------------------------------------------------------------

; DSKCLS: an output file's ^Z put after its last byte, and its record
; written; then the file closed.
DSKCLS:         LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_SQO
                BNE     NOFORC
                LDA     #ASCCTZ
                JSR     FILOU4
                LDY     #F_ORCT
                LDA     (FDBP),Y
                BEQ     NOFORC
                JSR     WRITES
NOFORC:         JSR     SETBUF
                LDY     #F_CLOSE
                JMP     FCBDOS

; --- DSKSIN, DSKINP (GIODSK.ASM 351) ------------------------------------------------------

; DSKSIN: the next byte in A, C set at the end: a ^Z is the end of a text
; file (not of a binary one).
DSKSIN:         JSR     DSKINP
                BCS     DSYEOF
                CMP     #ASCCTZ
                BNE     DSNEOF
                PHA
                LDY     #F_CODE
                LDA     (FDBP),Y
                CMP     #FC_BIN
                PLA
                BCS     DSNEOF         ; (FC_BIN, the highest: binary)
DSYEOF:         SEC
                RTS
DSNEOF:         CLC
                RTS

; DSKINP: the next byte of the file in A, C set at its end (a ^Z then). An
; output file has none (Input past end, INCHR's); the buffer's next, or the
; next record read into it first.
DSKINP:         LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_RND
                BNE     SINP1
                JMP     ADVERR         ; SINP50: a random file's (X1, later 7c)
SINP1:          LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_SQO
                BEQ     FILLS1
                LDY     #F_BREM
                LDA     (FDBP),Y
                BEQ     FILLSQ
                DEC     A              ; one less left
                STA     (FDBP),Y
                INC     A
                EOR     #$FF           ; BX = F_ORCT - F_BREM
                SEC
                LDY     #F_ORCT
                ADC     (FDBP),Y
                CLC
                ADC     #DATOFS
                TAY
                LDA     (FDBP),Y
                CLC
                RTS
FILLSQ:         LDY     #F_ORCT
                LDA     (FDBP),Y
                BEQ     FILLS1
                JSR     READS
                BNE     SINP1
FILLS1:         LDA     #ASCCTZ
                SEC
                RTS

; --- DSKSOT (GIODSK.ASM 422) --------------------------------------------------------------

; DSKSOT: A out. An input file takes none (an echo, Extra ignored: dropped);
; a random file's is FIELD's (X1, later 7c). FILOU4: into the buffer, the
; record written first if it is full; then the column: a CR to 0, a
; printable byte on one. A kept (X and Y used: the 8086 keeps them all).
DSKSOT:         PHA
                LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_SQI
                BEQ     FILOUX
                CMP     #MD_RND
                BNE     FILOU5
                PLA
                JMP     ADVERR         ; (X1, later 7c)
FILOU4:         PHA
FILOU5:         LDY     #F_ORCT
                LDA     (FDBP),Y
                CMP     #DATPSC
                BNE     SOUT2
                JSR     WRITES
SOUT2:          LDY     #F_ORCT
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
                CLC
                ADC     #DATOFS - 1
                TAY
                PLA
                STA     (FDBP),Y
SOUTPS:         LDY     #F_POS
                CMP     #13
                BNE     SOUT3
                PHA
                LDA     #0
                STA     (FDBP),Y
                PLA
                RTS
SOUT3:          CMP     #' '
                BCC     FILOX1
                PHA
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
FILOUX:         PLA
FILOX1:         RTS

; --- READS, WRITES, SETBUF, ACCFIL (GIODSK.ASM 599) ---------------------------------------

; READS ($READS): the next record into the buffer (zeroed first), F_CLOC
; one on. F_ORCT and F_BREM 128, or 0 at the end (any code but 0): Z set
; then.
READS:          LDY     #F_CLOC
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
                BNE     :+
                INY
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
:               LDA     #0
                LDY     #DATOFS
@z:             STA     (FDBP),Y
                INY
                CPY     #DATOFS + DATPSC
                BNE     @z
                JSR     SETBUF
                LDY     #F_READRAND
                JSR     ACCFIL
                TAX
                LDA     #0             ; the end: none
                CPX     #0
                BNE     READ1
                LDA     #DATPSC
READ1:          LDY     #F_ORCT
                STA     (FDBP),Y
                INY                    ; F_BREM
                STA     (FDBP),Y
                CMP     #0
                RTS

; WRITES ($WRITS): the buffer written as the next record, F_ORCT 0, F_CLOC
; one on. ACCFIL's 1 is Device I/O Error; 2 Disk full (the file made input,
; so CLOSE doesn't try again); $FF Too many files. A kept.
WRITES:         PHA
                LDY     #F_ORCT
                LDA     #0
                STA     (FDBP),Y
                JSR     SETBUF
                LDY     #F_WRITERAND
                JSR     ACCFIL
                CMP     #255
                BEQ     ER_TMF
                DEC     A
                BEQ     ER_IOE
                DEC     A
                BNE     WRITE1
                LDY     #F_MODE
                LDA     #MD_SQI
                STA     (FDBP),Y
                JMP     DERDFL
WRITE1:         LDY     #F_CLOC
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
                BNE     :+
                INY
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
:               PLA
                RTS
ER_TMF:         JMP     DERTMF
ER_IOE:         JMP     DERIOE

; SETBUF: the DMA address the buffer's, at its CPU address in W3 (V1).
SETBUF:         CLC
                LDA     FDBP
                ADC     #DATOFS
                PHA
                LDA     FDBP+1
                ADC     #0
                TAX
                PLA
                LDY     #F_DMAOFF
                JMP     BDOS

; ACCFIL: function Y (33 or 34) on the FCB, its random record one on. 34's
; codes mapped: 0 none; 5 Too many files (the directory full); 3 (the
; extent not closed) 1; any other 2 (the disk full). 33's: 3 is 0 (MS-DOS's
; part record; CP/M's 3 is CP/M 2's extent, mapped so too). A the code.
ACCFIL:         STY     ACCFN
                JSR     FCBDOS
                PHA
                LDY     #FCB_RN
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
                BNE     ACCFL1
                INY
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
                BNE     ACCFL1
                INY                    ; R2
                LDA     (FDBP),Y
                INC     A
                STA     (FDBP),Y
ACCFL1:         PLA
                LDY     ACCFN
                CPY     #F_WRITERAND
                BNE     ACCFL2
                CMP     #0
                BEQ     ACCRET
                CMP     #5
                BEQ     ER_TMF
                CMP     #3
                BNE     @full
                LDA     #1
                RTS
@full:          LDA     #2
                RTS
ACCFL2:         CMP     #3
                BNE     ACCRET
                LDA     #0
ACCRET:         RTS

; FCBDOS (the project's own): function Y on FDBP's FCB (the 8086's LEA DX,
; F_FCB[SI] and CALLOS). A the result.
FCBDOS:         CLC
                LDA     FDBP
                ADC     #F_FCB
                PHA
                LDA     FDBP+1
                ADC     #0
                TAX
                PLA
                JMP     BDOS

; --- DFSTLD (GIODSK.ASM 783) --------------------------------------------------------------

; DFSTLD: CX bytes at most from PTRFIL's file (file 0, its first byte read)
; to BX; BX returns past the last, C set if the file ended first. OO-1
; (W2, hw/oodos.md 7c): the 8086 reads the bytes from the second on by one
; random block read of 1-byte records; CP/M's records are 128 bytes, so:
;   1. the bytes the buffer has left of the first record, as many as CX
;      takes (BUFCPY);
;   2. whole records by function 83's DM_LOAD, from the next (R0-R2),
;      straight to the workspace (its physical address, function 73's), as
;      many as fit in CX whole: each lies below BX + CX;
;   3. if CX has room for part of one more, that record read into the
;      buffer (READS), and only the bytes CX takes copied.
; So nothing is written at or past BX + CX, however the file ends. A
; DM_LOAD that fails is Device I/O error.
DFSTLD:         LDA     PTRFIL
                STA     FDBP
                LDA     PTRFIL+1
                STA     FDBP+1
                JSR     BUFCPY
                LDA     RCL            ; the whole records: CX / 128
                ASL     A
                LDA     RCH
                ROL     A
                STA     LDBLK+6
                LDA     #0
                ROL     A
                STA     LDBLK+7
                ORA     LDBLK+6
                JEQ     DFSTL3
                LDA     #DM_LOAD
                STA     LDBLK
                CLC                    ; the FCB
                LDA     FDBP
                ADC     #F_FCB
                STA     LDBLK+1
                LDA     FDBP+1
                ADC     #0
                STA     LDBLK+2
                LDY     #FCB_RN        ; the first, the next record
                LDX     #0
@rn:            LDA     (FDBP),Y
                STA     LDBLK+3,X
                INY
                INX
                CPX     #3
                BNE     @rn
                LDA     RBL            ; the destination: BX's physical address
                STA     LDPHY
                LDA     RBH
                STA     LDPHY+1
                LDA     #<LDPHY
                LDX     #>LDPHY
                LDY     #O_PHYSADDR
                JSR     BDOS
                CMP     #0
                JNE     DFSTER
                LDA     LDPHY+2        ; paragraph x 16 + offset
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     LDPHY
                STA     LDBLK+8
                LDA     LDPHY+2
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                STA     T0
                LDA     LDPHY+3
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     T0
                STA     LDBLK+9
                LDA     LDPHY+3
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                STA     LDBLK+10
                LDA     #<LDBLK
                LDX     #>LDBLK
                LDY     #O_DMA
                JSR     BDOS
                STA     LDRES          ; 0 all, 1 the file ended first
                CMP     #2
                BCS     DFSTER
                LDY     #FCB_RN        ; the next record: the first + those loaded
                CLC
                LDA     LDBLK+3
                ADC     LDBLK+6
                STA     (FDBP),Y
                INY
                LDA     LDBLK+4
                ADC     LDBLK+7
                STA     (FDBP),Y
                INY
                LDA     LDBLK+5
                ADC     #0
                STA     (FDBP),Y
                LDA     LDBLK+6        ; BX on, CX less, by 128 x those loaded
                LSR     LDBLK+7
                ROR     A
                STA     T1             ; (x 128: the high byte, and the low's bit 7)
                LDA     #0
                ROR     A
                STA     T0
                CLC
                LDA     RBL
                ADC     T0
                STA     RBL
                LDA     RBH
                ADC     T1
                STA     RBH
                SEC
                LDA     RCL
                SBC     T0
                STA     RCL
                LDA     RCH
                SBC     T1
                STA     RCH
                LDA     LDRES
                BNE     DFSTEF         ; the file ended
DFSTL3:         LDA     RCL            ; room for part of one more?
                ORA     RCH
                BEQ     DFSTNE
                JSR     READS
                BEQ     DFSTEF         ; none: the end
                JSR     BUFCPY
DFSTNE:         CLC
                RTS
DFSTEF:         SEC
                RTS
DFSTER:         JMP     DERIOE

; BUFCPY (the project's own): the bytes left in FDBP's buffer (F_BREM) to
; BX, as many as CX takes: BX on, CX and F_BREM less.
BUFCPY:         LDY     #F_BREM
                LDA     (FDBP),Y
                BEQ     @x
                LDA     RCL
                ORA     RCH
                BEQ     @x
                LDA     (FDBP),Y       ; DATOFS + F_ORCT - F_BREM: the next byte
                EOR     #$FF
                SEC
                LDY     #F_ORCT
                ADC     (FDBP),Y
                CLC
                ADC     #DATOFS
                TAY
                LDA     (FDBP),Y
                STA     (RBL)
                INCW    RBL
                LDA     RCL
                BNE     :+
                DEC     RCH
:               DEC     RCL
                LDY     #F_BREM
                LDA     (FDBP),Y
                DEC     A
                STA     (FDBP),Y
                BRA     BUFCPY
@x:             RTS

; --- PROSAV, PENCOD, PDECOD (GIODSK.ASM 814) ----------------------------------------------

; PROSAV: SAVE name,P: past the "P", TEMP there; the line pointers back to
; numbers (SCCPTR, P2's), the program encoded (PENCOD), saved with 254 first
; (BINPSV, which closes file 0), decoded again (PDECOD); TXTPTR from TEMP.
PROSAV:         JSR     CHRGTR
                LDA     TXTPTR
                STA     TEMP
                LDA     TXTPTR+1
                STA     TEMP+1
                FJSR    SCCPTR
                JSR     PENCOD
                LDA     #254
                JSR     BINPSV
                JSR     PDECOD
                JMP     GTMPRT

; PENCOD: each byte of the text, TXTTAB to VARTAB, less CH, XORed with
; PRKEY1's byte CL and PRKEY2's byte CH, plus CL; CL counts 11 down to 1 and
; again (N1), CH 13 down to 1 (N2), each byte. PDECOD (PROLOD) undoes it:
; less CL, XOR CH's, XOR CL's, plus CH. PTR is the 8086's DX.
N1              = 11
N2              = 13
PENCOD:         JSR     PRINIT
ENCDBL:         JSR     PREND
                BEQ     PRRET
                LDA     (PTR)
                SEC
                SBC     PRCH
                LDX     PRCL
                EOR     PRKEY1-1,X
                LDX     PRCH
                EOR     PRKEY2-1,X
                CLC
                ADC     PRCL
                STA     (PTR)
                JSR     PRNEXT
                BRA     ENCDBL
PDECOD:         JSR     PRINIT
DECDBL:         JSR     PREND
                BEQ     PRRET
                LDA     (PTR)
                SEC
                SBC     PRCL
                LDX     PRCH
                EOR     PRKEY2-1,X
                LDX     PRCL
                EOR     PRKEY1-1,X
                CLC
                ADC     PRCH
                STA     (PTR)
                JSR     PRNEXT
                BRA     DECDBL
PRRET:          RTS
; (The 8086's loop's parts, the project's own: both counters set, PTR at
; TXTTAB; Z set at VARTAB; PTR on and the counters down, each back to its
; start at 0.)
PRINIT:         LDA     #N1
                STA     PRCL
                LDA     #N2
                STA     PRCH
                LDA     TXTTAB
                STA     PTR
                LDA     TXTTAB+1
                STA     PTR+1
                RTS
PREND:          LDA     PTR
                CMP     VARTAB
                BNE     :+
                LDA     PTR+1
                CMP     VARTAB+1
:               RTS
PRNEXT:         INCW    PTR
                DEC     PRCL
                BNE     :+
                LDA     #N1
                STA     PRCL
:               DEC     PRCH
                BNE     :+
                LDA     #N2
                STA     PRCH
:               RTS

; --- CHKFOP (GIODSK.ASM 714) --------------------------------------------------------------

; CHKFOP: File already open if another FDB has this one's drive, name and
; type (upper case alike); its drive 0 made the current one first. A kept.
; OO-1: the table's FDBs in use (F_MODE not 0), where the 8086 walks its
; chain; PTR is the 8086's DI.
CHKFOP:         PHA
                LDY     #FCB_DV
                LDA     (FDBP),Y
                BNE     NTCRDV
                LDY     #DRV_GET
                JSR     BDOS
                INC     A
                LDY     #FCB_DV
                STA     (FDBP),Y
NTCRDV:         STZ     CHKN
CHKNFL:         LDA     CHKN
                CMP     #NMFILE + 1
                BCS     CHKFLX
                ASL     A
                TAX
                LDA     FDBTAB,X
                STA     PTR
                LDA     FDBTAB+1,X
                STA     PTR+1
                LDY     #F_MODE
                LDA     (PTR),Y
                BEQ     IGNTFL         ; (free: no FDB in the chain)
                LDA     PTR
                CMP     FDBP
                BNE     :+
                LDA     PTR+1
                CMP     FDBP+1
                BEQ     IGNTFL         ; itself
:               LDY     #F_FCB
CMPLOP:         LDA     (FDBP),Y
                JSR     MAKUPC
                STA     T0
                LDA     (PTR),Y
                JSR     MAKUPC
                CMP     T0
                BNE     IGNTFL         ; NTSAME
                INY
                CPY     #F_FCB + 12
                BNE     CMPLOP
                JMP     DERFAO
IGNTFL:         INC     CHKN
                BRA     CHKNFL
CHKFLX:         PLA
                RTS

MAKUPC:         CMP     #'a'
                BCC     NOTLC
                CMP     #'z' + 1
                BCS     NOTLC
                AND     #$FF - ' '
NOTLC:          RTS

; --- FILES, KILL, NAME (GIODSK.ASM 931) --------------------------------------------------

; FILESP: FILES [name] (GIODSK.ASM 949): the names in the directory that
; match the name, or with none every file of the current drive ("*.*"),
; with "d:" alone every file of that drive ("d:*.*"); "*" and "?" are wild
; cards. Each is printed as the 8086 prints MS-DOS's: its 8 characters,
; "." (a blank if the type is blank) and its 3, then a blank, or a new line
; when the next would end past the width; File not found if none matches.
; The directory's records come to DIRBUF (the 8086's FILNA2, its DTA), the
; DMA address (OO-1: CP/M's search gives a 128-byte record, the entry at
; DIRBUF + 32 A, its name and type at +1, the attributes in their bit 7,
; which is taken off). OO-1: PTRWID and PTRGPS are P1's; FILES's output is
; the console's (a statement starts with PTRFIL 0), so its width and column
; are theirs, LINLEN and TTYPOS. Z from CHRGTR: nothing follows.
FILESP:         BEQ     NOARG          ; JE NOARG
                JSR     NAMFIL         ; PTR2 the name, RCL its length
                LDA     RCL
                CMP     #2
                BNE     GOTNAM         ; not "d:"
                LDY     #1
                LDA     (PTR2),Y
                CMP     #':'
                BNE     GOTNAM
                LDA     (PTR2)         ; "d:" to FILNA2
                STA     FILNA2
                LDA     #':'
                STA     FILNA2+1
                LDX     #2
                BRA     ALFILS         ; "*.*" after it
NOARG:          LDX     #0             ; SI: FILNA2
                STZ     RCL            ; XOR CX,CX
ALFILS:         LDA     #'*'           ; "*.*"
                STA     FILNA2,X
                STA     FILNA2+2,X
                LDA     #'.'
                STA     FILNA2+1,X
                CLC                    ; ADD CX,3
                LDA     RCL
                ADC     #3
                STA     RCL
                LDA     #<FILNA2
                STA     PTR2
                LDA     #>FILNA2
                STA     PTR2+1
GOTNAM:         JSR     FILFCB         ; FILNAM: the search's FCB
                JSR     SETDIR         ; C_BUFF: the records to DIRBUF
                JSR     SEARCH         ; C_SEAR: the first
                CMP     #$FF
                BNE     FILNXT
                JMP     DERFNF
FILNXT:         JSR     POLKEY         ; ^C, ^S between the names
                ASL     A              ; the entry's name: DIRBUF + 32 A + 1
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                INC     A
                STA     FIDX
                LDA     #FNAML         ; CX: the name's characters
                STA     FCNT
MORNAM:         LDX     FIDX
                LDA     DIRBUF,X
                AND     #$7F           ; OO-1: CP/M's attribute bits off
                JSR     OUTDO
                INC     FIDX
                LDA     FCNT
                CMP     #4
                BNE     NOTEXT         ; not at the type
                LDX     FIDX
                LDA     DIRBUF,X       ; the type's first: blank, a blank
                AND     #$7F
                CMP     #' '
                BEQ     PRISPA
                LDA     #'.'
PRISPA:         JSR     OUTDO
NOTEXT:         DEC     FCNT           ; LOOP MORNAM
                BNE     MORNAM
                LDA     TTYPOS         ; PTRGPS (OO-1: the console's, above)
                CLC
                ADC     #14            ; past the next name
                CMP     LINLEN         ; PTRWID's width
                BCS     NWFILN
                LDA     #' '
                JSR     OUTDO
                BRA     NEXTFL
NWFILN:         JSR     CRDO
NEXTFL:         LDY     #F_SNEXT       ; C_SEAR+1: the next
                JSR     BDOS
                CMP     #$FF
                BNE     FILNXT
                JMP     CRDONZ         ; a new line, unless at the margin

; KILLP: KILL name (GIODSK.ASM 1022): the files of that name deleted (its
; "*" and "?" match, as the BDOS's delete takes them); File not found if
; there is none, File already open if it is open (CHKFOP, FILNAM taken for
; an FDB's FCB, as the 8086's SI at FILNAM - F_FCB). The 8086 opens and
; closes it to see it is there. OO-1: CP/M's open refuses a "?" (through
; the error mode, which would end GW-BASIC), so a search for it (17, its
; record to DIRBUF) answers the same.
KILLP:          JSR     NAMFIL
                JSR     FILFCB
                JSR     SETDIR
                JSR     SEARCH         ; OO-1: for C_OPEN, C_CLOS
                CMP     #$FF
                BNE     KILL2
                JMP     DERFNF
KILL2:          JSR     FCBFDB         ; "pretend we are FDB"
                JSR     CHKFOP
                LDA     #<FILNAM       ; C_DELE
                LDX     #>FILNAM
                LDY     #F_DELETE
                JMP     BDOS           ; (its answer not tested, as the 8086's)

; NAMEP: NAME old AS new (GIODSK.ASM 1048): the old name found (File not
; found) and not open (CHKFOP: File already open), its drive and name to
; FILNA2, the rename's FCB; "AS"; the new name, on the same drive (none
; named: the old one's), else Rename across disks; it to FILNA2 + 17, then
; the rename (23): File already exists if the new name is there.
; OO-1: CP/M's rename gives File Exists (and "?" in either name) through the
; error mode, which would end GW-BASIC, where MS-DOS's returns $FF; so a
; "?" in either name is Bad file name, and the new name is searched for
; first, on the old one's drive (found: File already exists). The rename's
; $FF (no old name: the 8086's File already exists) is then not reached.
NAMEP:          JSR     NAMFIL
                JSR     FILFCB         ; FILNAM: the old name
                JSR     SETDIR
                JSR     SEARCH
                CMP     #$FF
                BEQ     NA_FNF
                JSR     FCBFDB
                JSR     CHKFOP         ; (its drive 0 made the current one)
                LDX     #FNAML         ; the drive and name to FILNA2
@o:             LDA     FILNAM,X
                STA     FILNA2,X
                DEX
                BPL     @o
                JSR     SYNCHR         ; "AS"
                .byte   'A'
                JSR     SYNCHR
                .byte   'S'
                JSR     NAMFIL
                JSR     FILFCB         ; FILNAM: the new name
                LDA     FILNAM         ; its drive: none, or the old one's
                BEQ     SAMDRV
                CMP     FILNA2
                BEQ     SAMDRV
                JMP     DERRAD         ; Rename across disks
SAMDRV:         LDX     #FNAML - 1     ; the new name and type to FILNA2 + 17
@n:             LDA     FILNAM+1,X
                STA     FILNA2+17,X
                DEX
                BPL     @n
                LDX     #FNAML         ; OO-1: "?" in either: Bad file name
@q:             LDA     FILNA2,X
                CMP     #'?'
                BEQ     NA_NMF
                LDA     FILNA2+16,X
                CMP     #'?'
                BEQ     NA_NMF
                DEX
                BNE     @q
                LDA     FILNA2         ; OO-1: the new name there already?
                STA     FILNAM
                JSR     SEARCH
                CMP     #$FF
                BNE     NA_FAE
                LDA     #<FILNA2       ; C_RENA, the FCB with both names
                LDX     #>FILNA2
                LDY     #F_RENAME
                JSR     BDOS
                INC     A              ; $FF: the 8086's File already exists
                BEQ     NA_FAE
                RTS
NA_FNF:         JMP     DERFNF
NA_FAE:         JMP     DERFAE
NA_NMF:         JMP     DERIFN

; NAMFIL (GIODSK.ASM 1106): a file name for NAME, KILL or FILES, a string
; formula (Type mismatch if not), its temporary freed: PTR2 its text, RCL
; its length (the 8086's SI and CX); a null one is Bad file name. TXTPTR
; past it.
NAMFIL:         JSR     FRMEVL
                JSR     FRESTR         ; BX: the descriptor
                LDA     (RBL)
                BEQ     ER_NMF         ; JCXZ ER_NMF
                STA     RCL
                LDY     #1
                LDA     (RBL),Y
                STA     PTR2
                INY
                LDA     (RBL),Y
                STA     PTR2+1
                RTS
ER_NMF:         JMP     DERIFN         ; DERNMF, "Bad file name"

; FILFCB (GIODSK.ASM 1129): the name at PTR2, RCL bytes ("d:name.ext"),
; made an unopened FCB in FILNAM. It is copied to BUF first, as the 8086
; copies it: a name of more than 8 characters before any ".", ":" or "*"
; gets a "." after its 8th, so the rest is the type; then a zero after it,
; and F_PARSE (152, the 8086's C_PARS: MS-DOS's 41) fills FILNAM's bytes
; 0-23 from it: the drive, the name and type in capitals, "*" made "?"s.
; The 8086 returns AL from its parse (bad, wild, or neither), which none of
; its callers tests. OO-1: a name 152 refuses (a name of more than 8, a
; type of more than 3, a bad drive: MS-DOS's parse cuts the first two
; short) leaves the FCB as it was, so it is Bad file name, as OPEN's names
; are; and the name is taken to 127 bytes at most (152 reads 128), so BUF
; always has room.
FILFCB:         LDA     RCL
                CMP     #127
                BCC     :+
                LDA     #127           ; OO-1
                STA     RCL
:               LDX     #0             ; DI: BUF
STRTNM:         STZ     FCNT           ; AH: the name's characters
NAMORE:         LDA     RCL            ; anything left?
                BEQ     TRMSTR
                LDA     (PTR2)         ; MOVSB
                INCW    PTR2
                STA     BUF,X
                INX
                DEC     RCL
                CMP     #':'           ; a device: the name starts again
                BEQ     STRTNM
                CMP     #'.'           ; the type next
                BEQ     FINNAM
                CMP     #'*'           ; a wild card ends the name too
                BEQ     FINNAM
                LDY     FCNT
                CPY     #8
                BCS     LNGNAM         ; past 8: the rest is the type
                INC     FCNT
                BRA     NAMORE
LNGNAM:         STA     BUF,X          ; the 9th after a "."
                LDA     #'.'
                STA     BUF-1,X
                INX
FINNAM:         LDA     RCL            ; REP MOVSB: the rest
                BEQ     TRMSTR
                LDA     (PTR2)
                INCW    PTR2
                STA     BUF,X
                INX
                DEC     RCL
                BRA     FINNAM
TRMSTR:         STZ     BUF,X          ; the terminator
                LDA     #<PARSBK       ; C_PARS: BUF to FILNAM
                LDX     #>PARSBK
                LDY     #F_PARSE
                JSR     BDOS
                CMP     #$FF           ; OO-1: refused, Bad file name
                BNE     FLFCBX
                CPX     #$FF
                BNE     FLFCBX
                JMP     DERIFN
FLFCBX:         RTS

; The project's own, for the three: SETDIR, the DMA address DIRBUF's (P4's
; own RAM, in W3: V1); SEARCH, the first entry that matches FILNAM (17), A
; its place in DIRBUF's record or $FF; FCBFDB, FDBP at FILNAM less F_FCB,
; so CHKFOP takes FILNAM for an FDB's FCB.
SETDIR:         LDA     #<DIRBUF
                LDX     #>DIRBUF
                LDY     #F_DMAOFF
                JMP     BDOS
SEARCH:         LDA     #<FILNAM
                LDX     #>FILNAM
                LDY     #F_SFIRST
                JMP     BDOS
FCBFDB:         LDA     #<(FILNAM - F_FCB)
                STA     FDBP
                LDA     #>(FILNAM - F_FCB)
                STA     FDBP+1
                RTS

; --- RESET (GIODSK.ASM 1179) --------------------------------------------------------------

; RESET (nothing may follow it): every file closed, the disks reset
; (function 13), the current drive as it was (25, then 14).
RESETP:         BNE     RESETX
                JSR     CLSALP
                LDY     #DRV_GET
                JSR     BDOS
                PHA
                LDY     #DRV_ALLRESET
                JSR     BDOS
                PLA
                LDY     #DRV_SET
                JSR     BDOS
RESETX:         RTS

.rodata
; FILFCB's parse block (function 152): the string BUF, the FCB FILNAM.
PARSBK:         .word   BUF, FILNAM
; DSKDSP (GIODSK.ASM 49): the disk's functions, by G_*. Random records
; (DSKRND: X1) are a later step's, Advanced Feature till then.
DSKDSP:         .word   DSKEOF         ; G_EOF
                .word   DSKLOC         ; G_LOC
                .word   DSKLOF         ; G_LOF
                .word   DSKCLS         ; G_CLS
                .word   FCERR          ; G_SWD: a disk's width can't be set
                .word   ADVERR         ; G_RND: DSKRND (X1, later 7c)
                .word   DSKOPN         ; G_OPN
                .word   DSKSIN         ; G_SIN
                .word   DSKSOT         ; G_SOT
                .word   DSKGPS         ; G_GPS
                .word   DSKGWD         ; G_GWD
                .word   DSKSCW         ; G_SCW
                .word   DSKGCW         ; G_GCW
                .word   DFSTLD         ; G_BIN
                .word   DEVBOT         ; G_BOT

; PRKEY1, PRKEY2: PENCOD's and PDECOD's keys, the bytes the 8086 takes of
; MATH1.ASM's $EXPCN (+1 to +11) and $LOGP (+1 to +13), so a protected
; program is encoded as GW-BASIC encodes it.
PRKEY1:         .byte   $7C, $88, $59, $74, $E0, $97, $26, $77, $C4, $1D, $1E
PRKEY2:         .byte   $9A, $F7, $19, $83, $24, $63, $43, $83, $75, $CD, $8D, $84, $A9

.segment "BRAM"
ODEV:           .res    1              ; DSKOPN's device (the 8086's AL, kept till INIFDB)
OLEN:           .res    2              ; and the length asked for (its stack)
APPCNT:         .res    1              ; APPFIN's count (DX)
ACCFN:          .res    1              ; ACCFIL's function (the 8086's AH)
CHKN:           .res    1              ; CHKFOP's number
LOFRN:          .res    3              ; DSKLOF's R0-R2, kept
LDBLK:          .res    11             ; DFSTLD's DM_LOAD block
LDPHY:          .res    4              ; and function 73's
LDRES:          .res    1              ; DM_LOAD's A
PRCL:           .res    1              ; PENCOD's and PDECOD's CL and CH
PRCH:           .res    1
FILNA2:         .res    33             ; NAME's rename FCB (both names), FILES's "d:*.*" (GWDATA.ASM's FILNA2)
DIRBUF:         .res    128            ; OO-1: the search's directory record (FILNA2 the 8086's DTA)
FIDX:           .res    1              ; FILES's place in DIRBUF (the 8086's SI)
FCNT:           .res    1              ; FILES's count (CX), FILFCB's name count (AH)

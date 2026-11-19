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
;   SINP50 FOVCHK   a random file's byte in, from its FIELD buffer
;   DSKSOT FILOU4   a byte out, a record written when the buffer is full (a
;                   random file's into its FIELD buffer)
;   DSKRND          GET and PUT: a random file's record, through the 128-byte
;   GETSUB PUTSUB   records it spans, each read (and written) in the buffer
;   CMPFBC CMPFBP   WRITE #'s padding of a random file's record (P3's WRTFIN)
;   READS WRITES    a record read or written ($READS, $WRITS), at the
;                   random record CP/M's R0-R2 holds, which ACCFIL moves on
;   RESETP          RESET: every file closed, the drives reset
;   FILESP          FILES: the directory's names that match, or every one
;   KILLP NAMEP     KILL, NAME: a file deleted, renamed (the BDOS's 19, 23)
;   NAMFIL FILFCB   their names: a string, made an FCB by function 152
;   DFSTLD          a LOAD's and a BLOAD's block input, to a physical
;                   address: whole records straight there by function 83's
;                   DM_LOAD (W2), the rest from the buffer by DM_COPY
;   BUFPHY BXFER    (7c's tail) the buffer's physical address; a piece
;   P73             between it and LDBLK+8-10, by DM_COPY; function 73
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
;   - a random file's record n of LEN bytes is bytes (n - 1) x LEN on, read
;     and written through the 128-byte records it spans, one at a time, in
;     the FDB's own buffer (hw/oodos.md 7c), as the 8086 reads and writes its
;     sectors; a record only partly changed is read first. Past the file's
;     end (X1), function 33 gives 1 (unwritten data) or 4 (an unwritten
;     extent): READS has zeroed the buffer, and takes any code but 0 for the
;     end, so the record is zeros, as the 8086's is past MS-DOS's end;
;   - LEN past /S: (MAXREC, res/far4.s: at most 1,024, W4; 128 while the
;     command line's switches aren't read) is Illegal function call, as
;     GW-BASIC gives it; the 1983 source takes any LEN (its FDBs are dynamic).

.include "gwbasic.inc"

;@entry RESETP
;@entry FILESP
;@entry KILLP
;@entry NAMEP
;@entry CMPFBP
.export DSKDSP, DSKOPN, RESETP, PROSAV, PDECOD, FILESP, KILLP, NAMEP, CMPFBP
.export BUFPHY, BXFER, P73, WRITES, LDBLK
.import FREFDB, FILNAM, FILMOD, INIFDB, CLSALP, FDBTAB, GETDAT, FLUSHR, DERFOV, DERBRN
.import MAXREC, ERROR
.import DEVBOT, BINPSV, GTMPRT, CHRGTR
.import DERTMF, DERFNF, DERBFM, DERFAO, DERIOE, DERDFL, FCERR, ADVERR, FRCDBL
.import DERIFN, DERFAE, DERRAD, FRMEVL, FRESTR, SYNCHR, OUTDO, CRDO, CRDONZ, POLKEY, LINLEN, BUF

ASCCTZ          = 26                   ; ^Z, a text file's end
FNAML           = 11                   ; MSDOSU's: a name and type's bytes
ERRDWP          = 70                   ; GWDATA.ASM's: Disk write protected
ERRDNR          = 71                   ; Disk not Ready
ERRDME          = 72                   ; Disk media error
ERRPAE          = 75                   ; Path/File access error (OO-1: p4/oem.s's)
ERRFAX          = 58                   ; File already exists
ERRNMX          = 64                   ; Bad file name

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
; Function 35 reads the directory, where MS-DOS's FCB_FS is the open FCB's:
; so the file is closed first (function 16), which puts the extent being
; written in the directory and, on OODOS, leaves the FCB open.
DSKLOF:         LDY     #F_CLOSE
                JSR     FCBDOS
                LDY     #FCB_RN + 2
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
; default, a record; past MAXREC, OO-1: Illegal function call), which is its
; FIELD buffer's (INIFDB's FALLOC). The FDB (INIFDB: every mode, width 255, column 0) is
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
                BNE     DSKOP2         ; not random
                LDA     RCL            ; a length asked for?
                ORA     RCH
                BNE     DSKOP1
                LDA     #DATPSC        ; else a record's bytes
                STA     RCL
DSKOP1:         LDA     MAXREC         ; OO-1 (W4): more than /S: is Illegal
                CMP     RCL            ; function call
                LDA     MAXREC+1
                SBC     RCH
                BCS     DSKOP2
                JMP     FCERR
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
                JSR     SYSCHK
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
; SYSCHK (the project's own, W7, the owner's 2026-11-15): a SYS file of
; FILNAM's name isn't there for OPEN (and LOAD, SAVE, CHAIN, MERGE), as
; MS-DOS's FCB open passes over a system file and FILES, KILL and NAME do
; here: INPUT is File not found; OUTPUT, APPEND and random, which would
; delete it or make a second entry of its name, Path/File access error. A
; search (17, to DIRBUF; SETBUF's DMA address is set after it) for the name,
; in error mode $FF, so a physical error says nothing here: the open, the
; delete or the make that follows meets it as before, its function named.
SYSCHK:         STZ     FILNAM + 12    ; EX, S1, S2: the first extent's entry
                STZ     FILNAM + 13
                STZ     FILNAM + 14
                JSR     SETDIR
                LDA     #$FF           ; error mode: return, quietly
                LDY     #F_ERRMODE
                JSR     BDOS
                LDA     #<FILNAM
                LDX     #>FILNAM
                LDY     #F_SFIRST
                JSR     BDOS
                PHA
                LDA     #$FE           ; GW-BASIC's (p2/gwinit.s)
                LDY     #F_ERRMODE
                JSR     BDOS
                PLA
                CMP     #$FF           ; none, or a physical error
                BEQ     @r
                ASL     A              ; DIRBUF + 32 A + 10: t2'
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                TAX
                LDA     DIRBUF + 10,X
                BPL     @r
                LDA     FILMOD
                CMP     #MD_SQI
                BNE     @pae
                JMP     DERFNF
@pae:           LDA     #ERRPAE
                JMP     ERROR
@r:             RTS
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
; RNDFIN: FD_PHY to the FDB's end zero (the FIELD buffer isn't: the 8086's
; count stops at FD_DAT, so it holds what the string space left there).
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
                JMP     SINP50         ; a random file's, from its FIELD buffer
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

; SINP50 (GIODSK.ASM 403): a random file's next byte, its FIELD buffer's at
; FD_OPS (FOVCHK: FIELD overflow past its end), C clear. FOVCHK (410): the
; place one on, unless it is at FD_SIZ already: FIELD overflow.
SINP50:         JSR     FOVCHK
                JSR     FLDGET
                CLC
                RTS
FOVCHK:         LDY     #FD_OPS        ; BX: the place
                LDA     (FDBP),Y
                LDY     #FD_SIZ
                CMP     (FDBP),Y
                BNE     @on
                LDY     #FD_OPS + 1
                LDA     (FDBP),Y
                LDY     #FD_SIZ + 1
                CMP     (FDBP),Y
                BEQ     ER_FOV
@on:            LDY     #FD_OPS        ; one on
                LDA     (FDBP),Y
                CLC
                ADC     #1
                STA     (FDBP),Y
                INY
                LDA     (FDBP),Y
                ADC     #0
                STA     (FDBP),Y
                RTS
ER_FOV:         JMP     DERFOV

; FLDGET, FLDPUT (the project's own): the byte at FD_DAT - 1 + FD_OPS (the
; 8086's FD_DAT-1[BX+SI], FOVCHK's place) read to A, or written from A.
; FD_DAT is the FIELD buffer, in the TPA (GETDAT); PTR is kept, as the 8086
; keeps BX (OUTDO's caller may be walking a string by it). X and Y used.
FLDPUT:         PHA
                JSR     FLDSET
                PLA
                STA     (PTR)
                BRA     FLDRST
FLDGET:         JSR     FLDSET
                LDA     (PTR)
FLDRST:         PHA
                LDA     FLDSAV
                STA     PTR
                LDA     FLDSAV+1
                STA     PTR+1
                PLA
                RTS
FLDSET:         LDA     PTR
                STA     FLDSAV
                LDA     PTR+1
                STA     FLDSAV+1
                JSR     GETDAT
                CLC
                LDY     #FD_OPS
                ADC     (FDBP),Y
                STA     PTR
                TXA
                INY
                ADC     (FDBP),Y
                STA     PTR+1
                DECW    PTR
                RTS

; --- DSKSOT (GIODSK.ASM 422) --------------------------------------------------------------

; DSKSOT: A out. An input file takes none (an echo, Extra ignored: dropped);
; a random file's goes into its FIELD buffer, at FD_OPS (FOVCHK: FIELD
; overflow past its end), and its column moves. FILOU4: into the buffer, the
; record written first if it is full; then the column: a CR to 0, a
; printable byte on one. A kept (X and Y used: the 8086 keeps them all).
DSKSOT:         PHA
                LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_SQI
                BEQ     FILOUX
                CMP     #MD_RND
                BNE     FILOU5
                JSR     FOVCHK         ; serial output to a random file
                PLA
                JSR     FLDPUT
                BRA     SOUTPS
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

; --- DSKRND, GETSUB, PUTSUB (GIODSK.ASM 462) ---------------------------------------------

PGFLAG          = 1                    ; DSKRND's function: on, PUT; off, GET
RELFLG          = 2                    ; on, a record given; off, the next
DIRFLG          = 4                    ; PGSUB1's: on, write; off, read

; DSKRND: GET or PUT, A the function (0 GET the next record, 1 PUT it, 2 GET
; record DX, from 1, 3 PUT it), FDBP the file's FDB; RNDAL is the 8086's
; AL. The next is FD_LOG + 1; a record given must be 1..65535 (OO-1: the
; IBM manual's p. 113 and A-4 take records past 32767, which the source's
; JLE refuses; 0 is Bad record number). FD_LOG is it, FD_OPS 0. Its first
; byte is (record - 1) x FD_SIZ: DX the 128-byte record, BX the place in it
; (a record reaching past 128-byte record 65535, 8 MB, the port's reach, is
; Bad record number, OO-1: the source's Illegal function call; LEN 128 is
; the record itself). Then the record is moved a 128-byte record at a time,
; CX bytes each (what is left of it, or of the record), between its FIELD
; buffer (LBUFF) and the FDB's buffer (PBUFF): GET reads each (GETSUB) and
; copies; PUT reads one it only partly fills, copies into it, and writes it
; (PUTSUB). RECRD is the 128-byte record, DX the bytes left, BX the place.
ER_BRN:         JMP     DERBRN         ; Bad record number
DSKRND:         STA     RNDAL
                AND     #RELFLG
                BNE     RAND1
                LDY     #FD_LOG        ; DX: the next, FD_LOG + 1
                LDA     (FDBP),Y
                CLC
                ADC     #1
                STA     RDL
                INY
                LDA     (FDBP),Y
                ADC     #0
                STA     RDH
                BRA     RAND2
RAND1:          LDA     RDH            ; 0: Bad record number (OO-1: past 32767
                ORA     RDL            ; too, the source's JLE, is taken: the IBM
                BEQ     ER_BRN         ; manual's p. 113, A-4)
RAND2:          LDY     #FD_LOG        ; the next logical
                LDA     RDL
                STA     (FDBP),Y
                INY
                LDA     RDH
                STA     (FDBP),Y
                DECW    RDL            ; DX: the current, from 0
                LDY     #FD_OPS        ; the output place 0
                LDA     #0
                STA     (FDBP),Y
                INY
                STA     (FDBP),Y
                LDY     #FD_SIZ        ; BX: the record's length
                LDA     (FDBP),Y
                STA     RBL
                INY
                LDA     (FDBP),Y
                STA     RBH
                BNE     RANDM          ; logical = physical?
                LDA     RBL
                CMP     #DATPSC
                BNE     RANDM
RAND3:          STZ     RBL            ; BX: the place 0
                BRA     RAND4
RANDM:          JSR     RNDMUL         ; the byte's place, x 2 (for / 128)
                ASL     RNDPRD
                ROL     RNDPRD+1
                ROL     RNDPRD+2
                ROL     RNDPRD+3
                LDA     RNDPRD+3       ; DH: past the reach (OO-1: Bad record
                BNE     ER_BRN         ; number; the source's Illegal function call)
                LDA     RNDPRD+2       ; DX: the 128-byte record
                STA     RDH
                LDA     RNDPRD+1
                STA     RDL
                LDA     RNDPRD         ; BX: the place in it
                LSR     A
                STA     RBL
                STZ     RBH
                LDY     #FD_SIZ        ; OO-1: its last byte's record past 65,535
                CLC                    ; too (P + size - 1, over 128, added)
                ADC     (FDBP),Y
                TAX
                INY
                LDA     #0
                ADC     (FDBP),Y
                CPX     #1             ; less one
                SBC     #0
                DEX
                CPX     #$80
                ROL     A
                CLC
                ADC     RDL
                LDA     RDH
                ADC     #0
                BCC     RAND4
                JMP     DERBRN
RAND4:          LDA     RDL            ; RECRD: the record
                STA     RECRD
                LDA     RDH
                STA     RECRD+1
                JSR     GETDAT         ; LBUFF: the FIELD buffer
                STA     LBUFF
                STX     LBUFF+1
                LDY     #FD_SIZ        ; DX: the bytes to move, its length
                LDA     (FDBP),Y
                STA     RDL
                INY
                LDA     (FDBP),Y
                STA     RDH
NXTOPD:         CLC                    ; PBUFF: the FDB's buffer + BX
                LDA     FDBP
                ADC     #DATOFS
                STA     PBUFF
                LDA     FDBP+1
                ADC     #0
                STA     PBUFF+1
                CLC
                LDA     PBUFF
                ADC     RBL
                STA     PBUFF
                BCC     :+
                INC     PBUFF+1
:               SEC                    ; CX: the bytes left in it
                LDA     #DATPSC
                SBC     RBL
                STA     RCL
                STZ     RCH
                LDA     RDH            ; the smaller of that and DX
                BNE     DATMOF
                LDA     RCL
                CMP     RDL
                BCC     DATMOF
                LDA     RDL
                STA     RCL
DATMOF:         LDA     RNDAL
                AND     #PGFLAG
                BEQ     FIVDRD         ; GET
                LDA     RCL            ; a whole record: no need to read it
                CMP     #DATPSC
                BCS     NOFVRD
                JSR     GETSUB
NOFVRD:         LDA     LBUFF          ; the FIELD buffer's bytes into it
                STA     PTR
                LDA     LBUFF+1
                STA     PTR+1
                LDA     PBUFF
                STA     PTR2
                LDA     PBUFF+1
                STA     PTR2+1
                JSR     RNDCPY
                JSR     PUTSUB         ; written
                BRA     NXFVBF
FIVDRD:         JSR     GETSUB         ; read, and its bytes to the FIELD buffer
                LDA     PBUFF
                STA     PTR
                LDA     PBUFF+1
                STA     PTR+1
                LDA     LBUFF
                STA     PTR2
                LDA     LBUFF+1
                STA     PTR2+1
                JSR     RNDCPY
NXFVBF:         INCW    RECRD          ; the next record
                CLC                    ; LBUFF on, DX less, by CX
                LDA     LBUFF
                ADC     RCL
                STA     LBUFF
                BCC     :+
                INC     LBUFF+1
:               SEC
                LDA     RDL
                SBC     RCL
                STA     RDL
                LDA     RDH
                SBC     #0
                STA     RDH
                STZ     RBL            ; from its start
                ORA     RDL
                JNE     NXTOPD         ; more to move
                RTS

; RNDCPY (the project's own: the 8086's REP MOVSW and MOVSB): CX (at most
; 128) bytes from PTR to PTR2.
RNDCPY:         LDY     #0
@c:             CPY     RCL
                BEQ     @x
                LDA     (PTR),Y
                STA     (PTR2),Y
                INY
                BRA     @c
@x:             RTS

; RNDMUL (the project's own: the 8086's MUL): RNDPRD (4 bytes) = BX x DX,
; unsigned, by shifts and adds.
RNDMUL:         STZ     RNDPRD+2
                STZ     RNDPRD+3
                LDA     RDL
                STA     RNDPRD
                LDA     RDH
                STA     RNDPRD+1
                LDX     #16
@l:             LDA     RNDPRD         ; the multiplier's low bit
                LSR     A
                BCC     @s
                CLC
                LDA     RNDPRD+2
                ADC     RBL
                STA     RNDPRD+2
                LDA     RNDPRD+3
                ADC     RBH
                STA     RNDPRD+3
@s:             ROR     RNDPRD+3
                ROR     RNDPRD+2
                ROR     RNDPRD+1
                ROR     RNDPRD
                DEX
                BNE     @l
                RTS

; PUTSUB: record RECRD written from the buffer; GETSUB: read into it
; (PGSUB1). The record the buffer holds is FD_PHY's (less one), which
; nothing sets after RNDFIN zeroes it, as in the 8086: so a read is always
; made (but for record 65535). F_CLOC the record (READS and WRITES step it),
; F_ORCT and F_BREM 128 (READS sets them by its read), CP/M's R0-R1 the
; record and R2 0; then WRITES, or READS: a record past the file's end comes
; back zeros, F_ORCT 0 (X1). RNDAL's DIRFLG is set or cleared, as the
; 8086's AL.
PUTSUB:         LDA     RNDAL
                ORA     #DIRFLG
                BRA     PGSUB1
GETSUB:         LDA     RNDAL
                AND     #$FF - DIRFLG
PGSUB1:         STA     RNDAL
                CLC                    ; BX: the record + 1
                LDA     RECRD
                ADC     #1
                TAX
                LDA     RECRD+1
                ADC     #0
                LDY     #FD_PHY + 1    ; in the buffer?
                CMP     (FDBP),Y
                BNE     NTREDS
                TXA
                DEY
                CMP     (FDBP),Y
                BNE     NTREDS
                LDA     RNDAL          ; read: there already
                AND     #DIRFLG
                BEQ     PGRET
NTREDS:         LDY     #F_CLOC        ; CURLOC: the record
                LDA     RECRD
                STA     (FDBP),Y
                INY
                LDA     RECRD+1
                STA     (FDBP),Y
                LDA     #DATPSC
                LDY     #F_ORCT
                STA     (FDBP),Y
                INY                    ; F_BREM
                STA     (FDBP),Y
                LDY     #FCB_RN        ; the random record
                LDA     RECRD
                STA     (FDBP),Y
                INY
                LDA     RECRD+1
                STA     (FDBP),Y
                INY
                LDA     #0
                STA     (FDBP),Y
                LDA     RNDAL
                AND     #DIRFLG
                BEQ     GET1
                JMP     WRITES
GET1:           JMP     READS
PGRET:          RTS

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
                JMP     PBDOS

; PBDOS (the project's own, the OEM's): JSR BDOS, then a physical error made
; GW-BASIC's. OO-1: GWINIT sets error mode $FE (function 45), so the BDOS
; says a physical error (its two lines) and returns A = $FF with X its code,
; where the default mode would end GW-BASIC. A result code's $FF has X 0 (or
; X $FF: a block refused) and comes back as it is. The codes are made errors
; as the 8086's DSKERR makes MS-DOS's critical errors (GIO86.ASM 2165): Read
; Only, its write protect, Disk write protected (70); Invalid Drive (a drive
; with no image), its drive not ready, Disk not Ready (71); Disk I/O, and the
; codes OODOS doesn't give, its others, Disk media error (72). MS-DOS raises
; no critical error for the rest but answers the FCB call; here: File R/O
; and Password Error, access refused, Path/File access error (75); File
; Exists, File already exists (58); ? in Filename, Bad file name (64). A and
; X are kept; the flags aren't (A is tested after it, as after JSR BDOS).
PBDOS:          JSR     BDOS
                CMP     #$FF
                BNE     @r
                CPX     #PHYEND - PHYTAB + 1
                BCS     @r             ; X past 9: not a physical error
                CPX     #0
                BEQ     @r             ; X 0: a result code's $FF
                LDA     PHYTAB - 1,X
                JMP     ERROR
@r:             RTS

; --- DFSTLD (GIODSK.ASM 783) --------------------------------------------------------------

; DFSTLD: CX bytes at most from PTRFIL's file (file 0, its first bytes
; read) to the physical address at LDBLK+8-10 (7c's tail: FSTLOD gives its
; BX's, by function 73; BLOAD its run's, P5's DSRUN), which moves on past
; them, as BX does; C set if the file ended first. OO-1 (W2, hw/oodos.md
; 7c): the 8086 reads the bytes from the second on by one random block read
; of 1-byte records; CP/M's records are 128 bytes, so:
;   1. the bytes the buffer has left of the first record, as many as CX
;      takes (BUFCPY: by DM_COPY from the buffer's physical address);
;   2. whole records by function 83's DM_LOAD, from the next (R0-R2),
;      straight to their place, as many as fit in CX whole;
;   3. if CX has room for part of one more, that record read into the
;      buffer (READS), and only the bytes CX takes copied (BUFCPY).
; So nothing is written at or past the start + CX, however the file ends.
; A DM_LOAD that fails is Device I/O error.
DFSTLD:         LDA     PTRFIL
                STA     FDBP
                LDA     PTRFIL+1
                STA     FDBP+1
                JSR     BUFPHY
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
                BEQ     DFSTL3
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
                LDA     #<LDBLK
                LDX     #>LDBLK
                LDY     #O_DMA
                JSR     PBDOS
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
                LDA     LDBLK+6        ; on by 128 x those loaded
                LSR     LDBLK+7
                ROR     A
                STA     T1             ; (x 128: the high byte, and the low's bit 7)
                LDA     #0
                ROR     A
                STA     T0
                JSR     ADVN
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

; BUFCPY (the project's own): the bytes left in FDBP's buffer (F_BREM), as
; many as CX takes, to LDBLK+8-10 by DM_COPY (BXFER): it, BX and CX on, and
; F_BREM less.
BUFCPY:         LDY     #F_BREM
                LDA     (FDBP),Y
                BEQ     @x
                STA     T0             ; T0: F_BREM, or CX if less
                STZ     T1
                LDA     RCH
                BNE     @n
                LDA     RCL
                BEQ     @x             ; CX 0
                CMP     T0
                BCS     @n
                STA     T0
@n:             LDA     (FDBP),Y       ; F_ORCT - F_BREM: the next byte's place
                EOR     #$FF
                SEC
                LDY     #F_ORCT
                ADC     (FDBP),Y
                PHA
                LDY     #F_BREM        ; F_BREM less
                LDA     (FDBP),Y
                SEC
                SBC     T0
                STA     (FDBP),Y
                PLA
                LDX     #0             ; from the buffer
                JMP     BXFER
@x:             RTS

; BUFPHY (7c's tail): BBUF, FDBP's buffer's physical address (function 73,
; while this block is in W3).
BUFPHY:         CLC
                LDA     FDBP
                ADC     #DATOFS
                PHA
                LDA     FDBP+1
                ADC     #0
                TAX
                PLA
                LDY     #BBUF - LDBLK - 8
; P73: the CPU address A/X's physical address (function 73's paragraph x
; 16 + offset), at LDBLK+8-10 + Y. A refusal is Device I/O error (there is
; none for the workspace or this block).
P73:            STA     LDPHY
                STX     LDPHY+1
                PHY
                LDA     #<LDPHY
                LDX     #>LDPHY
                LDY     #O_PHYSADDR
                JSR     BDOS
                PLY
                CMP     #0
                BNE     DFSTER
                LDA     LDPHY+2        ; the paragraph x 16
                STA     T0
                LDA     LDPHY+3
                STA     T1
                STZ     T2
                LDX     #4
@x16:           ASL     T0
                ROL     T1
                ROL     T2
                DEX
                BNE     @x16
                LDA     T0             ; + the offset (0-15)
                ORA     LDPHY
                STA     LDBLK+8,Y
                LDA     T1
                STA     LDBLK+9,Y
                LDA     T2
                STA     LDBLK+10,Y
                RTS

; BXFER (7c's tail): T0 bytes (1-128) by function 83's DM_COPY between the
; buffer's byte A (BBUF + A) and LDBLK+8-10: X 0 from the buffer (DFSTLD's),
; 3 to it (BSAVE's): the buffer's address goes to BDM+1+X. Then ADVN.
BXFER:          CLC
                ADC     BBUF
                STA     BDM+1,X
                LDA     BBUF+1
                ADC     #0
                STA     BDM+2,X
                LDA     BBUF+2
                ADC     #0
                STA     BDM+3,X
                TXA                    ; the other side: LDBLK+8-10
                EOR     #3
                TAX
                LDY     #0
@d:             LDA     LDBLK+8,Y
                STA     BDM+1,X
                INX
                INY
                CPY     #3
                BNE     @d
@go:            LDA     T0
                STA     BDM+7
                STZ     BDM+8
                LDA     #DM_COPY
                STA     BDM
                LDA     #<BDM
                LDX     #>BDM
                LDY     #O_DMA
                JSR     BDOS
; ADVN (7c's tail): LDBLK+8-10 and BX on by T1:T0, CX less.
ADVN:           CLC
                LDA     LDBLK+8
                ADC     T0
                STA     LDBLK+8
                LDA     LDBLK+9
                ADC     T1
                STA     LDBLK+9
                BCC     :+
                INC     LDBLK+10
:               CLC
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
                RTS

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
; OO-1: the table's FDBs in use (F_MODE not 0) and a disk's (F_DEV not
; negative), where the 8086 walks its chain; PTR is the 8086's DI; the
; bytes compared without CP/M's attribute bits.
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
                LDY     #F_DEV         ; OO-1: a device's FDB (LPT1:, COM1:, ...)
                LDA     (PTR),Y        ; may keep a closed file's FCB from
                BMI     IGNTFL         ; before; the 8086's has none (W2)
                LDA     PTR
                CMP     FDBP
                BNE     :+
                LDA     PTR+1
                CMP     FDBP+1
                BEQ     IGNTFL         ; itself
:               LDY     #F_FCB
CMPLOP:         LDA     (FDBP),Y       ; OO-1: CP/M's attribute bits (the
                AND     #$7F           ; high bits F_OPEN leaves in the name
                JSR     MAKUPC         ; and type) off, as CP/M matches names (W3)
                STA     T0
                LDA     (PTR),Y
                AND     #$7F
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

; --- CMPFBC (GIODSK.ASM 823) -------------------------------------------------------------

; CMPFBP (the project's own, P3's WRTFIN's part, FIVEO.ASM 769): OUTDO's run
; to PTRFIL's file first (FLUSHR: FD_OPS then counts what WRITE # sent).
; Then, as WRTFIN tests it: not a file (the console, or LPT1: OO-1, the
; 8086 reads F_DEV wherever PTRFIL is), not a disk's (F_DEV negative), or
; not random: C clear. Else CMPFBC: BX its record's size (GETFSZ: FD_SIZ),
; DX its place (GETFPS: FD_OPS); then BX less DX less 2 (CRLFSQ, the CR and
; LF still to come), C set if that isn't negative (the 8086's carry from
; its ADD): the spaces that pad the record.
CMPFBP:         JSR     FLUSHR
                LDA     PTRFIL+1       ; an FDB in P4's RAM?
                CMP     #>BLK_BASE
                BCC     NTRNDW
                CMP     #$FF
                BEQ     NTRNDW
                STA     FDBP+1
                LDA     PTRFIL
                STA     FDBP
                LDY     #F_DEV         ; a disk's
                LDA     (FDBP),Y
                BMI     NTRNDW
                LDY     #F_MODE        ; random
                LDA     (FDBP),Y
                CMP     #MD_RND
                BNE     NTRNDW
                JSR     CMPFBC
                SEC                    ; BX - DX - 2
                LDA     RBL
                SBC     RDL
                STA     RBL
                LDA     RBH
                SBC     RDH
                STA     RBH
                SEC
                LDA     RBL
                SBC     #2
                STA     RBL
                LDA     RBH
                SBC     #0
                STA     RBH
                RTS                    ; C: not negative
NTRNDW:         CLC
                RTS
; CMPFBC: BX FD_SIZ (GETFSZ), DX FD_OPS (GETFPS), FDBP's.
CMPFBC:         LDY     #FD_OPS        ; GETFPS
                LDA     (FDBP),Y
                STA     RDL
                INY
                LDA     (FDBP),Y
                STA     RDH
                LDY     #FD_SIZ        ; GETFSZ
                LDA     (FDBP),Y
                STA     RBL
                INY
                LDA     (FDBP),Y
                STA     RBH
                RTS

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
                JSR     SKPSYS
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
                JSR     PBDOS
                JSR     SKPSYS
                CMP     #$FF
                BNE     FILNXT
                JMP     CRDONZ         ; a new line, unless at the margin

; SKPSYS (the project's own): A the search's answer (the entry's place 0-3,
; or $FF none); a system file's entry passed over, the search gone on to the
; next. OO-1: CP/M's SYS attribute (t2', the high bit of the type's second
; byte) hides a file, as MS-DOS's FCB search leaves a hidden or system file
; out and CP/M's DIR doesn't list one (the owner's, 2026-11-14): so FILES
; doesn't list it, and a pattern only it matches is File not found; KILL
; and NAME don't find it either (W7, 2026-11-15).
SKPSYS:         CMP     #$FF
                BEQ     @r
                PHA
                ASL     A              ; DIRBUF + 32 A + 10: t2'
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                TAX
                LDA     DIRBUF + 10,X
                BMI     @sys
                PLA
@r:             RTS
@sys:           PLA
                LDY     #F_SNEXT
                JSR     PBDOS
                BRA     SKPSYS

; KILLP: KILL name (GIODSK.ASM 1022): the files of that name deleted (its
; "*" and "?" match, as the BDOS's delete takes them); File not found if
; there is none, File already open if it is open (CHKFOP, FILNAM taken for
; an FDB's FCB, as the 8086's SI at FILNAM - F_FCB). The 8086 opens and
; closes it to see it is there. OO-1: CP/M's open refuses a "?" (through
; the error mode, which would end GW-BASIC), so a search for it (17, its
; record to DIRBUF) answers the same. A SYS file isn't there (W7, as
; FILES's SKPSYS: MS-DOS's FCB search and delete pass over a system file):
; each other match is deleted by its own name, the search begun again after
; each, since CP/M's delete of the pattern would take the SYS files too.
KILLP:          JSR     NAMFIL
                JSR     FILFCB
                JSR     SETDIR
                JSR     SEARCH         ; OO-1: for C_OPEN, C_CLOS
                JSR     SKPSYS         ; (W7: a SYS file isn't there)
                CMP     #$FF
                BNE     KILL2
                JMP     DERFNF
KILL2:          PHA                    ; (the entry's place)
                JSR     FCBFDB         ; "pretend we are FDB"
                JSR     CHKFOP
                PLA
KILL3:          ASL     A              ; OO-1 (W7): each match but a SYS file,
                ASL     A              ; deleted by its own name (FILNA2), as
                ASL     A              ; MS-DOS's FCB delete passes over a
                ASL     A              ; system file; CP/M's delete of the
                ASL     A              ; pattern would take them all
                TAX
                LDY     #0
@n:             LDA     DIRBUF + 1,X   ; the entry's name and type, without
                AND     #$7F           ; its attribute bits (f5': XFCBs only)
                STA     FILNA2 + 1,Y
                INX
                INY
                CPY     #FNAML
                BNE     @n
                LDA     FILNAM         ; its drive (CHKFOP's: not 0)
                STA     FILNA2
                STZ     FILNA2 + 12    ; EX, S1, S2, RC
                STZ     FILNA2 + 13
                STZ     FILNA2 + 14
                STZ     FILNA2 + 15
                LDA     #<FILNA2       ; C_DELE
                LDX     #>FILNA2
                LDY     #F_DELETE
                JSR     PBDOS          ; (its answer not tested, as the 8086's;
                INC     A              ; but none deleted ends the loop)
                BEQ     @x
                JSR     SEARCH         ; the next: the search again, from the
                JSR     SKPSYS         ; first (a delete ends a search)
                CMP     #$FF
                BNE     KILL3
@x:             RTS

; NAMEP: NAME old AS new (GIODSK.ASM 1048): the old name found (File not
; found) and not open (CHKFOP: File already open), its drive and name to
; FILNA2, the rename's FCB; "AS"; the new name, on the same drive (none
; named: the old one's), else Rename across disks; it to FILNA2 + 17, then
; the rename (23): File already exists if the new name is there. A SYS
; file as the old name isn't there (W7); as the new one it is (no second
; entry of a name is made).
; OO-1: CP/M's rename gives File Exists (and "?" in either name) through the
; error mode, which would end GW-BASIC, where MS-DOS's returns $FF; so a
; "?" in either name is Bad file name, and the new name is searched for
; first, on the old one's drive (found: File already exists). The rename's
; $FF (no old name: the 8086's File already exists) is then not reached.
NAMEP:          JSR     NAMFIL
                JSR     FILFCB         ; FILNAM: the old name
                JSR     SETDIR
                JSR     SEARCH
                JSR     SKPSYS         ; (W7: a SYS file isn't there)
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
                JSR     PBDOS
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
; always has room. Where MS-DOS's parse takes a name that 152 would refuse,
; the copy makes it one 152 takes alike (OO-1, 2026-11-15): a control
; character, "+", "/" or '"' ends the name (W8: MS-DOS's terminators that
; aren't 152's); after a "*" the rest of its field is dropped, to the next
; of 152's delimiters (W6: MS-DOS's parse passes over it); and the type
; the 8th character's "." makes keeps 3 characters, the rest dropped as
; after a "*" (W5: MS-DOS's parse cuts it to 3).
FILFCB:         LDA     RCL
                CMP     #127
                BCC     :+
                LDA     #127           ; OO-1
                STA     RCL
:               LDX     #0             ; DI: BUF
STRTNM:         STZ     FCNT           ; AH: the name's characters
                STZ     FTYPE          ; OO-1: in the name
                STZ     FSKIP
NAMORE:         LDA     RCL            ; anything left?
                BEQ     @t
                LDA     (PTR2)         ; MOVSB
                INCW    PTR2
                DEC     RCL
                CMP     #' '           ; OO-1 (W8): MS-DOS's terminators
                BCC     @t             ; that 152 doesn't have end it
                CMP     #'+'
                BEQ     @t
                CMP     #'/'
                BEQ     @t
                CMP     #'"'
                BNE     @ok
@t:             JMP     TRMSTR
@ok:            BIT     FTYPE          ; OO-1 (W5): the type made below, past
                BPL     @sk            ; its 3rd: the rest dropped
                LDY     FTCNT
                CPY     #3
                BCC     @tc
                ROR     FSKIP          ; (C set: bit 7)
                BRA     @sk
@tc:            INC     FTCNT
@sk:            BIT     FSKIP          ; OO-1 (W6): dropped, to a delimiter
                BPL     NOSKIP
                LDY     #SKPEND_LEN - 1
@d:             CMP     SKPEND,Y
                BEQ     @e
                DEY
                BPL     @d
                BRA     NAMORE
@e:             STZ     FSKIP
NOSKIP:         STA     BUF,X
                INX
                LDY     FTYPE
                BNE     INTYPE
                CMP     #':'           ; a device: the name starts again
                BEQ     STRTNM
                CMP     #'.'           ; the type next
                BEQ     TYPNXT
                CMP     #'*'           ; a wild card ends the name too
                BEQ     SKIPON
                LDY     FCNT
                CPY     #8
                BCS     LNGNAM         ; past 8: the rest is the type
                INC     FCNT
                BRA     NAMORE
LNGNAM:         STA     BUF,X          ; the 9th after a "."
                LDA     #'.'
                STA     BUF-1,X
                INX
                LDA     #1             ; OO-1 (W5): the type made, its 1st
                STA     FTCNT
                LDA     #$80
                STA     FTYPE
                BRA     NAMORE
TYPNXT:         INC     FTYPE          ; a type given: as it is (152's rules)
                JMP     NAMORE
INTYPE:         CMP     #'*'
                BEQ     SKIPON
                JMP     NAMORE
SKIPON:         LDA     #$80
                STA     FSKIP
                JMP     NAMORE
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
                JMP     PBDOS
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
                JSR     PBDOS
                PLA
                LDY     #DRV_SET
                JSR     BDOS
RESETX:         RTS

.rodata
; PHYTAB: the error for each physical error's code, 1-9 (PBDOS).
PHYTAB:         .byte   ERRDME, ERRDWP, ERRPAE, ERRDNR ; Disk I/O, Read Only, File R/O, Invalid Drive
                .byte   ERRDME, ERRDME, ERRPAE ; (5, 6), Password Error
                .byte   ERRFAX, ERRNMX ; File Exists, ? in Filename
PHYEND:
; FILFCB's parse block (function 152): the string BUF, the FCB FILNAM.
PARSBK:         .word   BUF, FILNAM
; FILFCB's (OO-1, W6): 152's delimiters that end what a "*" drops (its 0,
; TAB and CR end the name before).
SKPEND:         .byte   ' ', ';', '=', '<', '>', '.', ':', ',', '|', '[', ']'
SKPEND_LEN      = * - SKPEND
; DSKDSP (GIODSK.ASM 49): the disk's functions, by G_*.
DSKDSP:         .word   DSKEOF         ; G_EOF
                .word   DSKLOC         ; G_LOC
                .word   DSKLOF         ; G_LOF
                .word   DSKCLS         ; G_CLS
                .word   FCERR          ; G_SWD: a disk's width can't be set
                .word   DSKRND         ; G_RND
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
BBUF:           .res    3              ; BUFPHY: FDBP's buffer's physical address (7c's tail)
BDM:            .res    9              ; BXFER's DM_COPY block
PRCL:           .res    1              ; PENCOD's and PDECOD's CL and CH
PRCH:           .res    1
FILNA2:         .res    33             ; NAME's rename FCB (both names), FILES's "d:*.*" (GWDATA.ASM's FILNA2)
DIRBUF:         .res    128            ; OO-1: the search's directory record (FILNA2 the 8086's DTA)
FIDX:           .res    1              ; FILES's place in DIRBUF (the 8086's SI)
FCNT:           .res    1              ; FILES's count (CX), FILFCB's name count (AH)
FTYPE:          .res    1              ; FILFCB's (OO-1): 0 in the name, 1 a type given, $80 one made
FTCNT:          .res    1              ; FILFCB's: the made type's characters
FSKIP:          .res    1              ; FILFCB's: bit 7, dropping to a delimiter
FLDSAV:         .res    2              ; FLDGET's and FLDPUT's PTR, kept
RNDAL:          .res    1              ; DSKRND's function (the 8086's AL)
RNDPRD:         .res    4              ; RNDMUL's product (the 8086's DX:AX)
RECRD:          .res    2              ; GWDATA.ASM's: the 128-byte record
LBUFF:          .res    2              ; and the place in the FIELD buffer
PBUFF:          .res    2              ; and in the FDB's buffer

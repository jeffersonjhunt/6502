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
.export DSKDSP, DSKOPN, RESETP
.import FREFDB, FILNAM, FILMOD, INIFDB, CLSALP, FDBTAB
.import DERTMF, DERFNF, DERBFM, DERFAO, DERIOE, DERDFL, FCERR, ADVERR, FRCDBL

ASCCTZ          = 26                   ; ^Z, a text file's end

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
; DSKDSP (GIODSK.ASM 49): the disk's functions, by G_*. Those of later
; steps are Advanced Feature till then: random records (DSKRND: X1), the
; block input of a LOAD (DFSTLD) and the block output of a BSAVE (DEVBOT).
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
                .word   ADVERR         ; G_BIN: DFSTLD (LOAD, later 7c)
                .word   ADVERR         ; G_BOT: DEVBOT (BSAVE, later 7c)

.segment "BRAM"
ODEV:           .res    1              ; DSKOPN's device (the 8086's AL, kept till INIFDB)
OLEN:           .res    2              ; and the length asked for (its stack)
APPCNT:         .res    1              ; APPFIN's count (DX)
ACCFN:          .res    1              ; ACCFIL's function (the 8086's AH)
CHKN:           .res    1              ; CHKFOP's number
LOFRN:          .res    3              ; DSKLOF's R0-R2, kept

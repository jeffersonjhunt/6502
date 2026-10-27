; OODOS's BDOS, its file functions (hw/oodos.md, "The BDOS, phase 3"), block
; 60, with bdos.s and bdosdisk.s. Written from the CP/M 3 manuals'
; descriptions of the functions: the registers are hw/oodos.md's, a byte
; result in A with X 0; an error (bdos.s's DERR) as the error mode says.
;
;   13 DRV_ALLRESET  every buffer written back and dropped, every drive logged
;                    out and read-write; A: current, the DMA address $0280
;   14 DRV_SET       A = the drive (0-15) logged in and made current
;   15 F_OPEN        the FCB at A/X: its extent; not in the user but in user
;                    0 with the SYS attribute (t2'): opened read-only from
;                    there, f8' set. RC as the extent has it; with f6', CR
;                    the last record's byte count. A = the directory code
;                    (the entry's place in its record, 0-3), $FF none
;   16 F_CLOSE       a written extent put in the directory (its blocks and
;                    RC merged), stamped, the drive's buffers written back
;   17 F_SFIRST      the first entry the FCB matches ('?' any byte; '?' in
;                    EX any extent; '?' as the drive every entry, labels,
;                    SFCBs, XFCBs and free ones): its record to the DMA
;                    address, A its directory code, $FF none
;   18 F_SNEXT       the next, as 17 asked
;   19 F_DELETE      every extent of each file the FCB matches ('?' too), and
;                    their XFCBs; with f5' the XFCBs only. A read-only one:
;                    none are deleted (File R/O). A = 0, $FF none
;   20 F_READ        the record CR of the extent to the DMA address, CR + 1;
;                    the next extent opened after 128. A = 0, 1 the end of
;                    the file (or a record never written)
;   21 F_WRITE       the DMA address's record to CR, CR + 1, a block taken
;                    if it needs one; the next extent opened, or made, after
;                    128. A = 0, 1 no directory space, 2 the disk full
;   22 F_MAKE        a new entry for the FCB's extent (no '?'; not if it is
;                    there already: File Exists), stamped as the label asks.
;                    A = the directory code, $FF the directory full
;   23 F_RENAME      every extent of the FCB's file (and its XFCB) given the
;                    name at +16, its attributes kept. Neither name with '?',
;                    the new one not there (File Exists). A = the code, $FF
;   24 DRV_LOGINVEC  A/X = the logged-in drives, bit 0 A:
;   26 F_DMAOFF      the DMA address = A/X
;   27 DRV_ALLOCVEC  A/X = $FFFF: the vector is in the system bank (46 has it)
;   28 DRV_SETRO     the current drive read-only until it is reset
;   29 DRV_ROVEC     A/X = the read-only drives
;   30 F_ATTRIB      f1'-f4' and t1'-t3' from the FCB into every extent; with
;                    f6' the byte count (CR) into S1 too. A = the code, $FF
;   31 DRV_DPB       A/X = the current drive's DPB, in common memory
;   32 F_USERNUM     A = $FF: A = the user; else the user = A & 15
;   33 F_READRAND    the record R0-R2 (CR set to it, not moved on). A = 0, 1
;                    never written, 3 the extent can't be closed, 4 an extent
;                    never written, 6 past 32 MB
;   34 F_WRITERAND   as 33, written: the extent made if need be. A = 0, 2
;                    the disk full, 3, 5 no directory space, 6
;   35 F_SIZE        R0-R2 = the file's size in records: past its last. A = 0,
;                    $FF none
;   36 F_RANDREC     R0-R2 = the record CR of the extent is
;   37 DRV_RESET     the drives in A/X's bits written back, dropped, logged
;                    out and read-write
;   40 F_WRITEZF     as 34: every block a write takes is zeroed anyway
;   42, 43           A = 0: there is nothing to lock
;   45 F_ERRMODE     the error mode = A
;   46 DRV_SPACE     drive A's free records, 3 bytes to the DMA address
;   48 DRV_FLUSH     every changed buffer written; A = $FF: and dropped
;   98 DRV_FREE      the blocks of files written but not closed freed
;   99 F_TRUNCATE    the file (no '?') ends with record R0-R2, which must be
;                    in it and in an extent it has: later extents deleted,
;                    blocks past it freed. A = 0, $FF
;
; The FCB is read into F (33 bytes, 36 for 33-36, 40 and 99) and written
; back as the function changed it. An extent's state is the FCB's, as CP/M's:
; EX and S2 the extent, its block pointers and RC (as Open sets it), S2's
; bit 7 set while it hasn't been written. A write needs the drive writable
; (Read Only) and the file too (File R/O: t1', or opened from user 0).

.setcpu "65C02"
.include "bdos.inc"
.importzp DBCB, AP, AMASK, MK

DEF_DMA         = $0280                ; the base page's buffer

.zeropage
NEXT:           .res    2              ; POSITION's extent
PCREATE:        .res    1              ; and whether it may make it
RRCR:           .res    1              ; a random record's place in its extent
WPY:            .res    1              ; WRITEREC: the pointer's place in F
ZN:             .res    1              ; ZEROBLK's sectors to go
FOUND:          .res    1              ; a search's first match: 1 found
FIRST:          .res    2              ; and its entry
TGL:            .res    1              ; F_TRUNC: R's extent's group
TGH:            .res    1
GRP:            .res    1              ; and whether an entry has it
ETMP:           .res    1              ; EXT11's

.segment "BDOS"

; ---------------------------------------------------------------------------
; Drives

F_RESET:        LDA     #0
                JSR     FLUSHD
                LDA     #0
                JSR     PURGED
                STZ     LOGVEC
                STZ     LOGVEC + 1
                STZ     ROVEC
                STZ     ROVEC + 1
                STZ     SSON
                STZ     SCB_DISK
                LDA     #<DEF_DMA
                STA     SCB_DMA
                LDA     #>DEF_DMA
                STA     SCB_DMA + 1
                JMP     RET0

F_SELECT:       CMP     #16
                BCC     @drive
                STA     EDRV
                LDA     #ER_SELECT
                JMP     DERR
@drive:         JSR     SELECT
                LDA     CDRV
                STA     SCB_DISK
                JMP     RET0

F_LOGIN:        LDA     LOGVEC
                LDX     LOGVEC + 1
                JMP     RETAX

F_DMA:          LDA     PA
                STA     SCB_DMA
                LDA     PX
                STA     SCB_DMA + 1
                JMP     RET0

F_ALLOC:        LDA     #$FF
                TAX
                JMP     RETAX

F_SETRO:        LDA     SCB_DISK
                JSR     DRVBIT
                ORA     ROVEC,X
                STA     ROVEC,X
                JMP     RET0

F_ROVEC:        LDA     ROVEC
                LDX     ROVEC + 1
                JMP     RETAX

F_DPB:          LDA     SCB_DISK
                JSR     SELECT
                LDA     DPBP
                LDX     DPBP + 1
                JMP     RETAX

F_USER:         CMP     #$FF
                BNE     @set
                LDA     SCB_USER
                JMP     RETA
@set:           AND     #$0F
                STA     SCB_USER
                JMP     RET0

F_DRESET:       LDA     PA             ; T2/T3 the drives, T4 each
                STA     T2
                LDA     PX
                STA     T3
                STZ     T4
@drive:         LDA     T4
                JSR     DRVBIT
                AND     T2,X
                BEQ     @next
                LDA     T4
                INC     A
                JSR     FLUSHD
                LDA     T4
                INC     A
                JSR     PURGED
                LDA     T4
                JSR     DRVBIT
                EOR     #$FF
                PHA
                AND     LOGVEC,X
                STA     LOGVEC,X
                PLA
                AND     ROVEC,X
                STA     ROVEC,X
@next:          INC     T4
                LDA     T4
                CMP     #16
                BNE     @drive
                STZ     SSON
                JMP     RET0

F_LOCK:         JMP     RET0

F_ERRMODE:      STA     SCB_ERRMODE
                JMP     RET0

F_FLUSH:        LDA     #0
                JSR     FLUSHD
                LDA     PA
                CMP     #$FF
                BNE     @kept
                LDA     #0
                JSR     PURGED
@kept:          JMP     RET0

; Free records: (DSM + 1 - the blocks in use) x the records a block, from
; the current vector, whose bits past DSM are never set.
F_FREE:         CMP     #16
                BCC     @drive
                STA     EDRV
                LDA     #ER_SELECT
                JMP     DERR
@drive:         JSR     SELECT
                LDA     ALVP
                STA     AP
                LDA     ALVP + 1
                STA     AP + 1
                STZ     SZ             ; the bits set
                STZ     SZ + 1
                STZ     SZ + 2
                LDX     #2             ; 512 bytes: every vector's whole half
                LDY     #0
@byte:          LDA     (AP),Y
                BEQ     @none
@bit:           ASL     A
                BCC     @zero
                INC     SZ
                BNE     @zero
                INC     SZ + 1
@zero:          ORA     #0
                BNE     @bit
@none:          INY
                BNE     @byte
                INC     AP + 1
                DEX
                BNE     @byte
                SEC                    ; DSM + 1 - SZ
                LDA     DSM
                SBC     SZ
                STA     SZ
                LDA     DSM + 1
                SBC     SZ + 1
                STA     SZ + 1
                INC     SZ
                BNE     @records
                INC     SZ + 1
@records:       LDX     BSH
@shift:         ASL     SZ
                ROL     SZ + 1
                ROL     SZ + 2
                DEX
                BNE     @shift
                LDA     #<SZ
                STA     BW1
                LDA     #>SZ
                STA     BW1 + 1
                LDA     SCB_DMA
                STA     BW2
                LDA     SCB_DMA + 1
                STA     BW2 + 1
                LDA     #3
                JSR     POUT
                JMP     RET0

; Each logged-in drive's current vector made its permanent one again.
F_FREEBLK:      STZ     T4
@drive:         LDA     T4
                JSR     DRVBIT
                AND     LOGVEC,X
                BEQ     @next
                LDA     T4
                JSR     DSLOT
                BCS     @next
                STA     CSLOT
                JSR     LOADCD
                LDA     ALVP           ; T0/T1 the current, AP the permanent
                STA     T0
                STA     AP
                LDA     ALVP + 1
                STA     T1
                INC     A
                INC     A
                STA     AP + 1
                LDX     #2
                LDY     #0
@copy:          LDA     (AP),Y
                STA     (T0),Y
                INY
                BNE     @copy
                INC     T1
                INC     AP + 1
                DEX
                BNE     @copy
@next:          INC     T4
                LDA     T4
                CMP     #16
                BNE     @drive
                JMP     RET0

; ---------------------------------------------------------------------------
; Opening, making, closing

F_OPEN:         LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     NOWILD
                LDA     FC + FCB_F8     ; f8' is the BDOS's to set
                AND     #$7F
                STA     FC + FCB_F8
                LDA     SCB_USER
                LDX     #0
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCC     @found
                LDA     SCB_USER       ; not here: a SYS file in user 0?
                BEQ     @none
                STZ     SPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@sys:           BCS     @none
                LDY     #FCB_T2
                LDA     (DPTR),Y
                BMI     @user0
                JSR     FINDNXT
                BRA     @sys
@user0:         LDA     FC + FCB_F8
                ORA     #$80
                STA     FC + FCB_F8
@found:         JSR     OPENEXT
                BIT     FC + FCB_F6     ; f6': the byte count into CR
                BPL     @access
                LDY     #FCB_S1
                LDA     (DPTR),Y
                STA     FC + FCB_CR
@access:        LDA     MODE
                AND     #MODE_ACCESS
                BEQ     @done
                LDA     #0
                JSR     STAMP
@done:          JSR     FCBOUT
                LDA     DIDX
                AND     #3
                JMP     RETA
@none:          LDA     #$FF
                JMP     RETA

F_MAKE:         LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     NOWILD
                JSR     RODRV
                LDA     FC + FCB_F7
                AND     #$7F
                STA     FC + FCB_F7
                LDA     FC + FCB_F8
                AND     #$7F
                STA     FC + FCB_F8
                LDA     SCB_USER
                LDX     #0
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCS     @new
                LDA     #ER_EXISTS
                JMP     DERR
@new:           LDA     SCB_USER
                JSR     MAKEENT
                BCC     @made
                LDA     #$FF
                JMP     RETA
@made:          JSR     FEMPTY
                JSR     FCBOUT
                LDA     DIDX
                AND     #3
                JMP     RETA

F_CLOSE:        LDA     #FCB_SEQ
                JSR     FCBSEL
                LDA     FC + FCB_S2
                BMI     @clean
                JSR     CLOSEX
                BCS     @failed
                LDA     CDRV
                INC     A
                JSR     FLUSHD
                JSR     FCBOUT
                LDA     DIDX
                AND     #3
                JMP     RETA
@clean:         LDA     CDRV           ; nothing to put in the directory
                INC     A
                JSR     FLUSHD
                JMP     RET0
@failed:        LDA     #$FF
                JMP     RETA

; F's extent, if written, into the directory: its entry found (made if it
; isn't there), the block pointers merged (one where the other has 0), EX
; and RC the later, its blocks permanent, the update stamped. Carry set if
; it can't be: two different blocks in one place, or no entry to be had.
CLOSEX:         LDA     FC + FCB_S2
                BPL     @written
                CLC                    ; not written: nothing to do
                RTS
@written:       JSR     RODRV
                JSR     EXTUSER
                LDX     #0
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCC     @found
                JSR     EXTUSER
                JSR     MAKEENT
                BCC     @found
                RTS                    ; no entry to be had
@found:         STZ     MK
@pointer:       LDY     MK
                JSR     EPTR           ; BLK the entry's
                LDA     MK
                JSR     FPTR           ; T0/T1 F's, Y its place in F
                LDA     T0
                ORA     T1
                BNE     @fhas
                LDA     BLK            ; F has none: the entry's
                STA     FC,Y
                BIT     BIG
                BPL     @next
                LDA     BLK + 1
                STA     FC + 1,Y
                BRA     @next
@fhas:          LDA     BLK
                ORA     BLK + 1
                BNE     @both
                LDA     MK             ; the entry has none: F's
                JSR     EPLACE
                LDA     T0
                STA     (DPTR),Y
                BIT     BIG
                BPL     @next
                INY
                LDA     T1
                STA     (DPTR),Y
                BRA     @next
@both:          LDA     BLK
                CMP     T0
                BNE     @failed
                LDA     BLK + 1
                CMP     T1
                BNE     @failed
@next:          INC     MK
                JSR     NPTRS
                CMP     MK
                BNE     @pointer
                JSR     FEXT           ; T0/T1 F's extent, T2/T3 the entry's
                JSR     DEXT
                LDA     T2
                CMP     T0
                LDA     T3
                SBC     T1
                BCC     @later         ; the entry's is before F's: F's EX and RC
                LDA     T0
                CMP     T2
                BNE     @put           ; F's is before: the entry's stand
                LDA     T1
                CMP     T3
                BNE     @put
                LDY     #FCB_RC        ; the same: the larger RC
                LDA     (DPTR),Y
                CMP     FC + FCB_RC
                BCS     @put
                LDA     FC + FCB_RC
                STA     (DPTR),Y
                BRA     @put
@later:         LDY     #FCB_EX
                LDA     FC + FCB_EX
                AND     #$1F
                STA     (DPTR),Y
                LDY     #FCB_S2
                LDA     FC + FCB_S2
                AND     #$3F
                STA     (DPTR),Y
                LDY     #FCB_RC
                LDA     FC + FCB_RC
                STA     (DPTR),Y
@put:           JSR     DIRPUT
                JSR     MARKE
                LDA     MODE
                AND     #MODE_UPDATE
                BEQ     @closed
                LDA     #4
                JSR     STAMP
@closed:        LDA     FC + FCB_S2
                ORA     #S2_CLEAN
                STA     FC + FCB_S2
                CLC
                RTS
@failed:        SEC
                RTS

; F from the entry at DPTR, for F's extent: the block pointers, the
; attributes (f1'-f4', t1'-t3'; f7' clear, f8' as it is), RC: 128 if the
; entry goes past F's extent, 0 if it doesn't reach it, else its own. S2's
; bit 7 set: not written.
OPENEXT:        LDY     #FCB_D
@pointers:      LDA     (DPTR),Y
                STA     FC,Y
                INY
                CPY     #FCB_CR
                BNE     @pointers
                LDY     #FCB_NAME
@attribute:     CPY     #FCB_F5
                BCC     @take
                CPY     #FCB_T1
                BCC     @skip
@take:          LDA     (DPTR),Y
                AND     #$80
                STA     T4
                LDA     FC,Y
                AND     #$7F
                ORA     T4
                STA     FC,Y
@skip:          INY
                CPY     #FCB_EX
                BNE     @attribute
                LDA     FC + FCB_F7
                AND     #$7F
                STA     FC + FCB_F7
                JSR     FEXT
                JSR     DEXT
                LDY     #FCB_RC
                LDA     (DPTR),Y
                TAX
                LDA     T0             ; F's against the entry's
                CMP     T2
                LDA     T1
                SBC     T3
                BCC     @before
                LDA     T0
                CMP     T2
                BNE     @after
                LDA     T1
                CMP     T3
                BEQ     @rc
@after:         LDX     #0
                BRA     @rc
@before:        LDX     #128
@rc:            STX     FC + FCB_RC
                LDA     FC + FCB_S2
                ORA     #S2_CLEAN
                STA     FC + FCB_S2
                RTS

; F a new, empty extent: no blocks, RC 0, not written.
FEMPTY:         STZ     FC + FCB_RC
                LDX     #FCB_D
@zero:          STZ     FC,X
                INX
                CPX     #FCB_CR
                BNE     @zero
                LDA     FC + FCB_S2
                AND     #$3F
                ORA     #S2_CLEAN
                STA     FC + FCB_S2
                RTS

; F to extent NEXT: the one it has closed first (3 if it can't be), then
; found, or made if PCREATE (A) says (5 if the directory is full); carry
; clear, or set with A the error. Not found and not made: 4, F at NEXT
; empty.
POSITION:       STA     PCREATE
                JSR     CLOSEX
                BCC     @closed
                LDA     #3
                RTS
@closed:        LDA     NEXT
                AND     #$1F
                STA     FC + FCB_EX
                LDA     NEXT + 1       ; S2 = NEXT / 32
                ASL     A
                ASL     A
                ASL     A
                STA     T0
                LDA     NEXT
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     T0
                STA     FC + FCB_S2
                JSR     FEMPTY
                JSR     EXTUSER
                LDX     #0
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCC     @open
                LDA     PCREATE
                BEQ     @none
                JSR     EXTUSER
                JSR     MAKEENT
                BCS     @full
@open:          JSR     OPENEXT
                CLC
                RTS
@none:          LDA     #4
                SEC
                RTS
@full:          LDA     #5
                SEC
                RTS

; NEXT = F's extent + 1; carry set past the last (2047: 32 MB).
NEXTEXT:        JSR     FEXT
                CLC
                LDA     T0
                ADC     #1
                STA     NEXT
                LDA     T1
                ADC     #0
                STA     NEXT + 1
                CMP     #>2048
                RTS

; T0/T1 = F's extent, S2 x 32 + EX; DEXT: T2/T3 the entry at DPTR's.
FEXT:           LDA     FC + FCB_S2
                AND     #$3F
                STA     T1
                LDA     FC + FCB_EX
                JSR     EXT11
                STA     T0
                RTS
DEXT:           LDY     #FCB_S2
                LDA     (DPTR),Y
                AND     #$3F
                PHA
                LDY     #FCB_EX
                LDA     (DPTR),Y
                PLY
                STY     T3
                JSR     EXT11T3
                STA     T2
                RTS
; A = EX's 5 bits ORed with T1's low 3 x 32; T1 = T1 / 8 (EXT11T3: T3).
EXT11:          AND     #$1F
                PHA
                LDA     T1
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     ETMP
                PLA
                ORA     ETMP
                LSR     T1
                LSR     T1
                LSR     T1
                RTS
EXT11T3:        AND     #$1F
                PHA
                LDA     T3
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     ETMP
                PLA
                ORA     ETMP
                LSR     T3
                LSR     T3
                LSR     T3
                RTS

; A = the user F's extents are looked for in: 0 if it was opened from
; there (f8'), else the current one.
EXTUSER:        LDA     FC + FCB_F8
                BMI     @zero
                LDA     SCB_USER
                RTS
@zero:          LDA     #0
                RTS

; The drive must be writable: Read Only.
RODRV:          LDA     CDRV
                JSR     DRVBIT
                AND     ROVEC,X
                BNE     @ro
                RTS
@ro:            LDA     #ER_RODISK
                JMP     DERR

; The drive and the file must be writable: Read Only, File R/O (t1', or
; opened from user 0).
WCHECK:         JSR     RODRV
                LDA     FC + FCB_T1
                ORA     FC + FCB_F8
                BMI     @ro
                RTS
@ro:            LDA     #ER_ROFILE
                JMP     DERR

; No '?' in F's name: ? in Filename.
NOWILD:         JSR     ISWILD
                BCS     @wild
                RTS
@wild:          LDA     #ER_WILD
                JMP     DERR

; ---------------------------------------------------------------------------
; Records

; A = the number of F's pointer for record CR of its extent: (EX & EXM)
; x the blocks an extent + CR / the records a block.
FPIDX:          LDA     #7             ; the blocks a 128-record extent: 2^(7 - BSH)
                SEC
                SBC     BSH
                TAX
                LDA     FC + FCB_EX
                AND     EXM
@extent:        DEX
                BMI     @record
                ASL     A
                BRA     @extent
@record:        STA     T4
                LDA     FC + FCB_CR
                LDX     BSH
@shift:         LSR     A
                DEX
                BNE     @shift
                CLC
                ADC     T4
                RTS

; T0/T1 = F's pointer A, Y its place in F.
FPTR:           BIT     BIG
                BMI     @word
                CLC
                ADC     #FCB_D
                TAY
                LDA     FC,Y
                STA     T0
                STZ     T1
                RTS
@word:          ASL     A
                CLC
                ADC     #FCB_D
                TAY
                LDA     FC,Y
                STA     T0
                LDA     FC + 1,Y
                STA     T1
                RTS

; Y = the place of the entry's pointer A in the entry.
EPLACE:         BIT     BIG
                BPL     @byte
                ASL     A
@byte:          CLC
                ADC     #FCB_D
                TAY
                RTS

; A = the pointers in an entry: 16 bytes or 8 words.
NPTRS:          LDA     #16
                BIT     BIG
                BPL     @done
                LDA     #8
@done:          RTS

; LBA = the sector of F's record CR in block BLK, T4 which 128 bytes of it.
RECSEC:         LDA     BLK
                STA     LBA
                LDA     BLK + 1
                STA     LBA + 1
                LDX     BSH            ; a block is 2^(BSH - 2) sectors
                DEX
                DEX
@block:         ASL     LBA
                ROL     LBA + 1
                DEX
                BNE     @block
                LDA     FC + FCB_CR
                AND     BLM
                PHA
                LSR     A
                LSR     A
                CLC
                ADC     LBA
                STA     LBA
                BCC     @off
                INC     LBA + 1
@off:           CLC
                LDA     LBA
                ADC     OFFS
                STA     LBA
                LDA     LBA + 1
                ADC     OFFS + 1
                STA     LBA + 1
                PLA
                AND     #3
                STA     T4
                RTS

; RECP = the record in GETBUF's buffer: BPTR + T4 x 128.
RECPTR:         LDA     T4
                LSR     A
                TAX
                LDA     #0
                ROR     A
                STA     RECP
                TXA
                CLC
                ADC     BPTR + 1
                STA     RECP + 1
                RTS

; Record CR of F's extent to the DMA address; carry set if it has no block.
READREC:        JSR     FPIDX
                JSR     FPTR
                LDA     T0
                STA     BLK
                LDA     T1
                STA     BLK + 1
                ORA     BLK
                BNE     @block
                SEC
                RTS
@block:         JSR     RECSEC
                LDA     #0
                JSR     GETBUF
                JSR     RECPTR
                LDA     RECP
                STA     BW1
                LDA     RECP + 1
                STA     BW1 + 1
                LDA     SCB_DMA
                STA     BW2
                LDA     SCB_DMA + 1
                STA     BW2 + 1
                LDA     #128
                JSR     POUT
                CLC
                RTS

; The DMA address's record to record CR of F's extent: a block taken (the
; first free, zeroed) if it has none; RC at least CR + 1; F written. Carry
; set if the drive is full.
WRITEREC:       JSR     FPIDX
                JSR     FPTR
                STY     WPY
                LDA     T0
                STA     BLK
                LDA     T1
                STA     BLK + 1
                ORA     BLK
                BNE     @block
                JSR     ALVFREE
                BCC     @new
                RTS
@new:           LDX     #1             ; the current vector only, until it is closed
                JSR     ALVSET
                LDY     WPY
                LDA     BLK
                STA     FC,Y
                BIT     BIG
                BPL     @zero
                LDA     BLK + 1
                STA     FC + 1,Y
@zero:          JSR     ZEROBLK
@block:         JSR     RECSEC
                LDA     #0
                JSR     GETBUF
                JSR     RECPTR
                LDA     SCB_DMA
                STA     BW1
                LDA     SCB_DMA + 1
                STA     BW1 + 1
                LDA     RECP
                STA     BW2
                LDA     RECP + 1
                STA     BW2 + 1
                LDA     #128
                JSR     PIN
                LDX     LASTB
                LDA     #1
                STA     BCB_DIRTY,X
                LDX     FC + FCB_CR
                INX
                CPX     FC + FCB_RC
                BCC     @written
                STX     FC + FCB_RC
@written:       LDA     FC + FCB_S2
                AND     #$7F
                STA     FC + FCB_S2
                CLC
                RTS

; Block BLK's sectors, each a new buffer of zeros, changed.
ZEROBLK:        LDA     BLM            ; (BLM + 1) / 4 sectors
                LSR     A
                LSR     A
                INC     A
                STA     ZN
                LDA     FC + FCB_CR     ; RECSEC for the block's first record
                PHA
                STZ     FC + FCB_CR
                JSR     RECSEC
                PLA
                STA     FC + FCB_CR
@sector:        LDA     #1
                JSR     GETBUF
                LDA     #1
                STA     BCB_DIRTY,X
                INC     LBA
                BNE     @next
                INC     LBA + 1
@next:          DEC     ZN
                BNE     @sector
                RTS

F_SREAD:        LDA     #FCB_SEQ
                JSR     FCBSEL
                LDA     FC + FCB_CR
                BPL     @record
                JSR     NEXTEXT        ; past the extent's end: the next one
                BCS     @end
                LDA     #0
                JSR     POSITION
                STZ     FC + FCB_CR
                BCS     @end
@record:        LDA     FC + FCB_CR
                CMP     FC + FCB_RC
                BCS     @end
                JSR     READREC
                BCS     @end
                INC     FC + FCB_CR
                JSR     FCBOUT
                JMP     RET0
@end:           JSR     FCBOUT
                LDA     #1
                JMP     RETA

F_SWRITE:       LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     WCHECK
                LDA     FC + FCB_CR
                BPL     @record
                JSR     NEXTEXT
                BCS     @full          ; past 32 MB
                LDA     #1
                JSR     POSITION
                STZ     FC + FCB_CR
                BCC     @record
                JSR     FCBOUT
                LDA     #1
                JMP     RETA
@record:        JSR     WRITEREC
                BCS     @full
                INC     FC + FCB_CR
                JSR     FCBOUT
                JMP     RET0
@full:          JSR     FCBOUT
                LDA     #2
                JMP     RETA

; NEXT = R0-R2's extent, RRCR its record there; carry set past 32 MB (R2
; over 3).
RANDPOS:        LDA     FC + FCB_R + 2
                CMP     #4
                BCS     @far
                LDA     FC + FCB_R
                AND     #$7F
                STA     RRCR
                LDA     FC + FCB_R      ; NEXT = R / 128
                ASL     A
                LDA     FC + FCB_R + 1
                ROL     A
                STA     NEXT
                LDA     FC + FCB_R + 2
                ROL     A
                STA     NEXT + 1
                CLC
@far:           RTS

; Z set if NEXT is F's extent.
SAMEEXT:        JSR     FEXT
                LDA     NEXT
                CMP     T0
                BNE     @done
                LDA     NEXT + 1
                CMP     T1
@done:          RTS

F_RREAD:        LDA     #FCB_RAND
                JSR     FCBSEL
                JSR     RANDPOS
                BCS     @far
                JSR     SAMEEXT
                BEQ     @here
                LDA     #0
                JSR     POSITION
                BCC     @here
                PHA                    ; 3 or 4
                LDA     RRCR
                STA     FC + FCB_CR
                JSR     FCBOUT
                PLA
                JMP     RETA
@here:          LDA     RRCR
                STA     FC + FCB_CR
                CMP     FC + FCB_RC
                BCS     @unwritten
                JSR     READREC
                BCS     @unwritten
                JSR     FCBOUT
                JMP     RET0
@unwritten:     JSR     FCBOUT
                LDA     #1
                JMP     RETA
@far:           LDA     #6
                JMP     RETA

F_RWRITEZ:
F_RWRITE:       LDA     #FCB_RAND
                JSR     FCBSEL
                JSR     WCHECK
                JSR     RANDPOS
                BCS     @far
                JSR     SAMEEXT
                BEQ     @here
                LDA     #1
                JSR     POSITION
                BCC     @here
                PHA                    ; 3 or 5
                JSR     FCBOUT
                PLA
                JMP     RETA
@here:          LDA     RRCR
                STA     FC + FCB_CR
                JSR     WRITEREC
                BCS     @full
                JSR     FCBOUT
                JMP     RET0
@full:          JSR     FCBOUT
                LDA     #2
                JMP     RETA
@far:           LDA     #6
                JMP     RETA

; RR = extent T0/T1's record A: T0/T1 x 128 + A.
RECOF:          PHA
                LDA     T1
                LSR     A
                STA     RR + 2
                LDA     T0
                ROR     A
                STA     RR + 1
                LDA     #0
                ROR     A
                STA     RR
                PLA
                CLC
                ADC     RR
                STA     RR
                BCC     @done
                INC     RR + 1
                BNE     @done
                INC     RR + 2
@done:          RTS

F_SETRAND:      LDA     #FCB_RAND
                JSR     FCBIN
                JSR     FEXT
                LDA     FC + FCB_CR
                JSR     RECOF
                LDX     #2
@r:             LDA     RR,X
                STA     FC + FCB_R,X
                DEX
                BPL     @r
                JSR     FCBOUT
                JMP     RET0

; SZ = the larger of SZ and the entry at DPTR's size: its extent x 128 +
; RC.
ESIZE:          JSR     DEXT
                LDA     T2
                STA     T0
                LDA     T3
                STA     T1
                LDY     #FCB_RC
                LDA     (DPTR),Y
                JSR     RECOF
                LDA     SZ             ; SZ < RR: RR
                CMP     RR
                LDA     SZ + 1
                SBC     RR + 1
                LDA     SZ + 2
                SBC     RR + 2
                BCS     @done
                LDX     #2
@take:          LDA     RR,X
                STA     SZ,X
                DEX
                BPL     @take
@done:          RTS

F_SIZE:         LDA     #FCB_RAND
                JSR     FCBSEL
                JSR     NOWILD
                LDA     SCB_USER
                LDX     #SM_ANYEX
                JSR     SETPAT
                STZ     SZ
                STZ     SZ + 1
                STZ     SZ + 2
                STZ     FOUND
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @all
                LDA     #1
                STA     FOUND
                JSR     ESIZE
                JSR     FINDNXT
                BRA     @entry
@all:           LDA     FOUND
                BNE     @size
                LDA     #$FF
                JMP     RETA
@size:          LDX     #2
@r:             LDA     SZ,X
                STA     FC + FCB_R,X
                DEX
                BPL     @r
                JSR     FCBOUT
                JMP     RET0

; ---------------------------------------------------------------------------
; The directory

; Each entry F's pattern (SETPAT's, done) matches, FOUND 1 if any and FIRST
; the first; File R/O for a read-only file's (not an XFCB's), if A says so.
FINDALL:        STA     T5
                STZ     FOUND
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                LDA     FOUND
                BNE     @more
                INC     FOUND
                LDA     DIDX
                STA     FIRST
                LDA     DIDX + 1
                STA     FIRST + 1
@more:          LDA     T5
                BEQ     @next
                LDA     (DPTR)
                CMP     #16
                BCS     @next          ; an XFCB
                LDY     #FCB_T1
                LDA     (DPTR),Y
                BPL     @next
                LDA     #ER_ROFILE
                JMP     DERR
@next:          JSR     FINDNXT
                BRA     @entry
@done:          RTS

F_SFIRST:       LDA     #FCB_SEQ
                JSR     FCBIN
                STZ     SSON
                LDA     FC + FCB_DR
                CMP     #'?'
                BNE     @named
                LDA     SCB_DISK       ; '?': every entry of the current drive
                JSR     SELECT
                LDX     #SM_ALL
                BRA     @pattern
@named:         JSR     FCBDRV
                JSR     SELECT
                LDX     #0
@pattern:       LDA     SCB_USER
                JSR     SETPAT
                LDX     #13            ; for 18
@save:          LDA     SPAT,X
                STA     SSAVE,X
                DEX
                BPL     @save
                LDA     SMODE
                STA     SSMODE
                LDA     CDRV
                STA     SSDRV
                LDA     #1
                STA     SSON
                STZ     DIDX
                STZ     DIDX + 1
SEARCH:         JSR     FIND
                BCS     @none
                CLC
                LDA     DIDX
                ADC     #1
                STA     SSIDX
                LDA     DIDX + 1
                ADC     #0
                STA     SSIDX + 1
                JSR     DIRREC
                LDA     DIDX
                AND     #3
                JMP     RETA
@none:          STZ     SSON
                LDA     #$FF
                JMP     RETA

F_SNEXT:        LDA     SSON
                BEQ     @none
                LDA     SSDRV
                JSR     SELECT
                LDX     #13
@restore:       LDA     SSAVE,X
                STA     SPAT,X
                DEX
                BPL     @restore
                LDA     SSMODE
                STA     SMODE
                JSR     PATDERIV
                LDA     SSIDX
                STA     DIDX
                LDA     SSIDX + 1
                STA     DIDX + 1
                BRA     SEARCH
@none:          LDA     #$FF
                JMP     RETA

F_DELETE:       LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     RODRV
                LDA     SCB_USER
                LDX     #SM_XFCB | SM_ANYEX
                JSR     SETPAT
                LDA     #1             ; none read-only, or none deleted
                JSR     FINDALL
                LDA     FOUND
                BNE     @delete
                LDA     #$FF
                JMP     RETA
@delete:        STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                LDA     (DPTR)
                CMP     #16
                BCS     @gone          ; an XFCB: always
                BIT     FC + FCB_F5     ; f5': the XFCBs only
                BMI     @next
                JSR     FREEE
@gone:          LDA     #DIR_FREE
                STA     (DPTR)
                JSR     DIRPUT
@next:          JSR     FINDNXT
                BRA     @entry
@done:          JMP     RET0

F_RENAME:       LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     NOWILD
                LDX     #FCB_NEW + FCB_NAME
@wild:          LDA     FC,X
                AND     #$7F
                CMP     #'?'
                BNE     @char
                LDA     #ER_WILD
                JMP     DERR
@char:          INX
                CPX     #FCB_NEW + FCB_EX
                BNE     @wild
                JSR     RODRV
                LDA     SCB_USER       ; the new name mustn't be there
                LDX     #SM_ANYEX
                JSR     SETPAT
                LDX     #FCB_NAME
@new:           LDA     FC + FCB_NEW,X
                AND     #$7F
                STA     SPAT,X
                INX
                CPX     #FCB_EX
                BNE     @new
                JSR     PATDERIV
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCS     @free
                LDA     #ER_EXISTS
                JMP     DERR
@free:          LDA     SCB_USER
                LDX     #SM_XFCB | SM_ANYEX
                JSR     SETPAT
                LDA     #1
                JSR     FINDALL
                LDA     FOUND
                BNE     @rename
                LDA     #$FF
                JMP     RETA
@rename:        STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                LDY     #FCB_NAME
@name:          LDA     (DPTR),Y       ; the new name, the old attributes
                AND     #$80
                STA     T4
                LDA     FC + FCB_NEW,Y
                AND     #$7F
                ORA     T4
                STA     (DPTR),Y
                INY
                CPY     #FCB_EX
                BNE     @name
                JSR     DIRPUT
                JSR     FINDNXT
                BRA     @entry
@done:          LDA     FIRST
                AND     #3
                JMP     RETA

F_ATTRIB:       LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     NOWILD
                JSR     RODRV
                LDA     SCB_USER
                LDX     #SM_ANYEX
                JSR     SETPAT
                LDA     #0
                JSR     FINDALL
                LDA     FOUND
                BNE     @set
                LDA     #$FF
                JMP     RETA
@set:           STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                LDY     #FCB_NAME
@name:          LDA     #0             ; f5'-f8': none on the disk
                CPY     #FCB_F5
                BCC     @attribute
                CPY     #FCB_T1
                BCC     @put
@attribute:     LDA     FC,Y
                AND     #$80
@put:           STA     T4
                LDA     (DPTR),Y
                AND     #$7F
                ORA     T4
                STA     (DPTR),Y
                INY
                CPY     #FCB_EX
                BNE     @name
                BIT     FC + FCB_F6     ; f6': the byte count
                BPL     @written
                LDY     #FCB_S1
                LDA     FC + FCB_CR
                STA     (DPTR),Y
@written:       JSR     DIRPUT
                JSR     FINDNXT
                BRA     @entry
@done:          LDA     FIRST
                AND     #3
                JMP     RETA

; Truncate: R0-R2 the new last record. Every extent checked first (none
; read-only, the file larger than R, an entry for R's extent); then those
; past R's extent's group deleted, and the one with it ending at R.
F_TRUNC:        LDA     #FCB_RAND
                JSR     FCBSEL
                JSR     NOWILD
                JSR     RODRV
                JSR     RANDPOS
                BCS     @refused
                LDA     SCB_USER
                LDX     #SM_ANYEX
                JSR     SETPAT
                STZ     SZ
                STZ     SZ + 1
                STZ     SZ + 2
                STZ     GRP            ; an entry of R's group seen
                JSR     TGROUP
                STZ     FOUND
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@check:         BCS     @checked
                LDA     #1
                STA     FOUND
                LDY     #FCB_T1
                LDA     (DPTR),Y
                BPL     @rw
                LDA     #ER_ROFILE
                JMP     DERR
@rw:            JSR     ESIZE
                JSR     EGROUP         ; Z set: R's group
                BNE     @next
                LDA     #1
                STA     GRP
@next:          JSR     FINDNXT
                BRA     @check
@checked:       LDA     FOUND
                BEQ     @refused
                LDA     GRP
                BEQ     @refused
                LDA     FC + FCB_R      ; R < SZ
                CMP     SZ
                LDA     FC + FCB_R + 1
                SBC     SZ + 1
                LDA     FC + FCB_R + 2
                SBC     SZ + 2
                BCS     @refused
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                JSR     EGROUP
                BEQ     @ends
                BCC     @kept          ; before R's group
                JSR     FREEE          ; after it: gone
                LDA     #DIR_FREE
                STA     (DPTR)
                JSR     DIRPUT
                BRA     @kept
@ends:          JSR     TCUT
@kept:          JSR     FINDNXT
                BRA     @entry
@done:          JMP     RET0
@refused:       LDA     #$FF
                JMP     RETA

; TGL/TGH = R's extent's group: NEXT with its EXM bits clear.
TGROUP:         LDA     EXM
                EOR     #$FF
                AND     NEXT
                STA     TGL
                LDA     NEXT + 1
                STA     TGH
                RTS

; The entry at DPTR's group against R's (TG): Z set the same, else carry
; set if after it.
EGROUP:         JSR     DEXT
                LDA     EXM
                EOR     #$FF
                AND     T2
                STA     T2
                LDA     T3
                CMP     TGH
                BNE     @done
                LDA     T2
                CMP     TGL
@done:          RTS

; The entry at DPTR (of R's group) ends at R: EX and S2 R's extent, RC its
; record + 1, the pointers after R's block freed and cleared; the update
; stamped.
TCUT:           LDY     #FCB_EX
                LDA     NEXT
                AND     #$1F
                STA     (DPTR),Y
                LDA     NEXT + 1       ; S2 = NEXT / 32
                ASL     A
                ASL     A
                ASL     A
                STA     T0
                LDA     NEXT
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     T0
                LDY     #FCB_S2
                STA     (DPTR),Y
                LDY     #FCB_RC
                LDA     RRCR
                INC     A
                STA     (DPTR),Y
                LDA     NEXT           ; R's pointer, as FPIDX works it out
                STA     FC + FCB_EX
                LDA     RRCR
                STA     FC + FCB_CR
                JSR     FPIDX
                STA     MK
@pointer:       INC     MK
                JSR     NPTRS
                CMP     MK
                BEQ     @put
                LDY     MK
                JSR     EPTR
                LDA     BLK
                ORA     BLK + 1
                BEQ     @pointer
                LDA     DSM
                CMP     BLK
                LDA     DSM + 1
                SBC     BLK + 1
                BCC     @clear         ; past DSM: no block to free
                JSR     ALVCLR
@clear:         LDA     MK
                JSR     EPLACE
                LDA     #0
                STA     (DPTR),Y
                BIT     BIG
                BPL     @pointer
                INY
                STA     (DPTR),Y
                BRA     @pointer
@put:           JSR     DIRPUT
                LDA     MODE
                AND     #MODE_UPDATE
                BEQ     @done
                LDA     #4
                JMP     STAMP
@done:          RTS


; OODOS's BDOS, its file functions (hw/oodos.md, "The BDOS, phase 3"), block
; 60, with bdos.s and bdosdisk.s. Written from the CP/M 3 manuals'
; descriptions of the functions: the registers are hw/oodos.md's, a byte
; result in A with X 0; an error (bdos.s's DERR) as the error mode says.
;
;   13 DRV_ALLRESET  the units read again (RESCAN: a drive whose image was
;                    swapped has its buffers dropped), every buffer written
;                    back and dropped, every drive logged out and read-write;
;                    A: current, the DMA address $0280. A buffer that can't
;                    be written is dropped too, and the first such error
;                    said after (Disk I/O, Read Only)
;   14 DRV_SET       A = the drive (0-15) logged in and made current
;   15 F_OPEN        the FCB at A/X: extent EX (its S2 taken as 0); not in
;                    the user but in user 0 with the SYS attribute (t2'):
;                    opened read-only from there, f8' set. RC as the extent
;                    has it; CR $FF: CR the last record's byte count (S1).
;                    A protected file (bdosxtra.s) without its password:
;                    Password Error in read mode, f7' set (read-only) in
;                    write mode. A = 0, $FF none
;   16 F_CLOSE       the drive's changed buffers written back, then a written
;                    extent put in the directory (its blocks and RC merged).
;                    A = 0, $FF. A buffer that won't write fails only the
;                    close of a file with a block there (Disk I/O)
;   17 F_SFIRST      the first entry the FCB matches ('?' any byte; '?' in
;                    EX any extent, else S2 taken as 0; '?' as the drive
;                    every entry, labels, SFCBs, XFCBs and free ones): its
;                    record to the DMA address, A its directory code (its
;                    place in the record, 0-3), $FF none
;   18 F_SNEXT       the next, as 17 asked
;   19 F_DELETE      every extent of each file the FCB matches ('?' too), and
;                    their XFCBs; with f5' the XFCBs only (their files'
;                    slots' mode 0). A read-only one: none are deleted (File
;                    R/O); nor a protected one without its password
;                    (Password Error). A = 0, $FF none
;   20 F_READ        the record CR of the extent to the DMA address, CR + 1;
;                    the next extent opened after 128. A = 0, 1 the end of
;                    the file (or a record never written). 20, 21, 33, 34
;                    and 40 do the multi-sector count's records (44): MULTI
;   21 F_WRITE       the DMA address's record to CR, CR + 1, a block taken
;                    if it needs one; the next extent opened, or made, after
;                    128. A = 0, 1 no directory space, 2 the disk full. A
;                    write (21, 34, 40) clears the archive attribute (t3')
;                    of its extent's entry; the first since the open stamps
;                    the update, unless a make of extent 0 has (WROTE)
;   22 F_MAKE        a new entry for the FCB's extent EX (its S2 taken as 0;
;                    no '?'; not if the file is there, any extent: File
;                    Exists), every attribute clear in it and in the FCB;
;                    extent 0's stamped as the label asks; with f6' and
;                    passwords on, its XFCB too (the DMA buffer's password
;                    and mode). A = 0, $FF the directory full
;   23 F_RENAME      every extent of the FCB's file (and its XFCB) given the
;                    name at +16, its attributes kept. Neither name with '?',
;                    the new one not there (File Exists), the old one's
;                    password given if it's protected. A = 0, $FF
;   24 DRV_LOGINVEC  A/X = the logged-in drives, bit 0 A:
;   26 F_DMAOFF      the DMA address = A/X
;   27 DRV_ALLOCVEC  A/X = $FFFF: the vector is in the system bank (46 has it)
;   28 DRV_SETRO     the current drive read-only until it is reset
;   29 DRV_ROVEC     A/X = the read-only drives
;   30 F_ATTRIB      f1'-f4' and t1'-t3' from the FCB into every extent; with
;                    f6' the byte count (CR) into S1 too. A = 0, $FF
;   31 DRV_DPB       A/X = the current drive's DPB, in common memory
;   32 F_USERNUM     A = $FF: A = the user; else the user = A & 15
;   33 F_READRAND    the record R0-R2 (CR set to it, not moved on). A = 0, 1
;                    never written, 3 the extent can't be closed, 4 an extent
;                    never written, 6 past 32 MB
;   34 F_WRITERAND   as 33, written: the extent made if need be. A = 0, 2
;                    the disk full, 3, 5 no directory space, 6
;   35 F_SIZE        R0-R2 = the file's size in records: past its last. A = 0,
;                    $FF none (R0-R2 0)
;   36 F_RANDREC     R0-R2 = the record CR of the extent is
;   37 DRV_RESET     as 13, for the drives in A/X's bits: written back,
;                    dropped, logged out and read-write
;   40 F_WRITEZF     as 34: every block a write takes is zeroed anyway
;   41               A = $FF, X 0: MP/M's test and write, as CP/M 3 answers it
;   38, 39, 42, 43   A = 0: there is nothing to lock (MP/M's drive access
;                    and free, and record locks, answered as CP/M 3 does)
;   45 F_ERRMODE     the error mode = A
;   46 DRV_SPACE     drive A's free records, 3 bytes to the DMA address
;   48 DRV_FLUSH     a swapped unit's drive's buffers dropped (CHANGED), then
;                    every changed buffer written, past a failure, the one
;                    that failed kept; A = $FF: and all dropped. Then the
;                    first failure said (Disk I/O, Read Only)
;   98 DRV_FREE      the blocks of files written but not closed freed, and
;                    the buffers of those blocks whose write failed dropped
;   99 F_TRUNCATE    the file (no '?') ends with record R0-R2, which must be
;                    in it where it has data (an extent its entry has, below
;                    RC there, in a block): later extents deleted, blocks
;                    past it freed, the archive attribute (t3') cleared in
;                    the entry that ends at R, the update stamped. A = 0,
;                    $FF
;
; The FCB is read into F (33 bytes, 36 for 33-36, 40 and 99) and written
; back as the function changed it. An extent's state is the FCB's, as CP/M's:
; EX and S2 the extent, its block pointers and RC (as Open sets it), S2's
; bit 7 set while it hasn't been written; S1's bit 7 (S1_STAMPED) once this
; open's update is stamped (CP/M 3 reserves S1 for the system). A write needs
; the drive writable
; (Read Only) and the file too (File R/O: t1', or opened from user 0, or
; f7'). Password Error is checked after every other error (hw/oodos.md).

.setcpu "65C02"
.include "bdos.inc"
.importzp DBCB, AP, AMASK, MK, RERR

DEF_DMA         = $0280                ; the base page's buffer

.zeropage
NEXT:           .res    2              ; POSITION's extent
PCREATE:        .res    1              ; and whether it may make it
RRCR:           .res    1              ; a random record's place in its extent
WPY:            .res    1              ; WRITEREC: the pointer's place in F
ZN:             .res    1              ; ZEROBLK's sectors to go
FOUND:          .res    1              ; a search's match: 1 found
TGL:            .res    1              ; F_TRUNC: R's extent's group
TGH:            .res    1
GRP:            .res    1              ; and whether an entry has it
ETMP:           .res    1              ; EXT11's
RDMA:           .res    2              ; READREC's and WRITEREC's record: the DMA address + 128 x k
MBODY:          .res    2              ; MULTI's record function
MLEFT:          .res    1              ; its records to go
MDONE:          .res    1              ; and done
MON:            .res    1              ; 1 once one is done: a physical error keeps them (MERR)
MRAND:          .res    1              ; bit 6: random, R0-R2 back to RSAVE at the end; 7: a read
MKPW:           .res    1              ; F_MAKE: bit 7, f6' with passwords on: an XFCB too
RSAVE:          .res    3
DSAVE:          .res    2              ; F_OPEN's entry, while its XFCB is looked for (OPENPW) and the last extent (LASTS1)

.segment "BDOS"

; ---------------------------------------------------------------------------
; Drives

F_RESET:        JSR     RESCAN         ; a swapped unit's drive: its buffers dropped
                STZ     RERR
                LDA     #0
                JSR     RFLUSH
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
                JMP     RDONE

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

F_DRESET:       JSR     RESCAN         ; a swapped unit's drive: its buffers dropped
                STZ     RERR
                LDA     PA             ; T2/T3 the drives, T4 each
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
                JSR     RFLUSH
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
                JMP     RDONE

F_LOCK:         JMP     RET0           ; 38, 39, 42, 43

; 41, MP/M's test and write record: not supported, A = $FF with X 0, as
; CP/M 3 answers it (its H 0).
F_TESTW:        LDA     #$FF
                JMP     RETA

F_ERRMODE:      STA     SCB_ERRMODE
                JMP     RET0

F_FLUSH:        JSR     CHANGED        ; a swapped unit's drive (a program's SELDSK): dropped
                LDA     #0             ; every buffer tried, a failed one kept
                JSR     FLUSHD
                LDA     PA
                CMP     #$FF
                BNE     @kept
                LDA     #0             ; $FF: all dropped all the same
                JSR     PURGED
@kept:          JMP     RDONE          ; then the first failure said

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

; Each logged-in drive's current vector made its permanent one again, and
; the marked buffers (a write that failed) of the blocks that frees dropped
; (PURGEM): 98 is the warm boot's (the CCP's), so what wrote them has ended
; without a close, and those blocks are free now. A marked buffer of a block
; the directory has is kept for a later 48 or close to write.
F_FREEBLK:      JSR     PURGEM
                STZ     T4
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
                STZ     FC + FCB_S2     ; the program gives bytes 0-12: module 0
                JSR     GENSET          ; the update not stamped: the first write's
                LDA     FC + FCB_F8     ; f8' and f7' are the BDOS's to set
                AND     #$7F
                STA     FC + FCB_F8
                LDA     FC + FCB_F7
                AND     #$7F
                STA     FC + FCB_F7
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
@found:         JSR     OPENPW
                JSR     OPENEXT
                LDA     FC + FCB_CR     ; CR $FF: the file's byte count into CR,
                CMP     #$FF           ; before the stamp: a failed look writes nothing
                BNE     @access
                JSR     LASTS1
@access:        LDA     MODE           ; the access, in extent 0's slot only
                AND     #MODE_ACCESS
                BEQ     @done
                LDA     FC + FCB_S2
                AND     #$3F
                ORA     FC + FCB_EX
                BNE     @done
                LDA     CDRV           ; not on a read-only drive
                JSR     DRVBIT
                AND     ROVEC,X
                BNE     @done
                LDA     #0
                JSR     STAMP
@done:          JSR     FCBOUT
                JMP     RET0
@none:          LDA     #$FF
                JMP     RETA

; CR = S1, the byte count, of the entry of F's file with the highest extent
; (S2, then EX), in the user it was opened from: cpmtools keeps the count in
; the last extent's entry only. Every entry of the file is looked at, as
; Compute File Size looks (F_SIZE). DIDX as it was, for the access stamp
; (DSAVE, free once OPENPW is done); SPAT, DPTR and T0-T3 changed.
LASTS1:         LDA     DIDX
                STA     DSAVE
                LDA     DIDX + 1
                STA     DSAVE + 1
                JSR     EXTUSER
                LDX     #SM_ANYEX
                JSR     SETPAT
                STZ     T0             ; T1/T0: the highest extent so far
                STZ     T1
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                JSR     DEXT           ; T3/T2: this entry's
                LDA     T2
                CMP     T0
                LDA     T3
                SBC     T1
                BCC     @next          ; below the highest
                LDA     T2
                STA     T0
                LDA     T3
                STA     T1
                LDY     #FCB_S1
                LDA     (DPTR),Y
                STA     FC + FCB_CR
@next:          JSR     FINDNXT
                BRA     @entry
@done:          LDA     DSAVE
                STA     DIDX
                LDA     DSAVE + 1
                STA     DIDX + 1
                RTS

; The file at DPTR (DIDX) opened from user EXTUSER: protected (PWFILE)
; without its password given, Password Error in read mode, f7' (read-only)
; in write mode. DIDX and DPTR as they were.
OPENPW:         BIT     MODE
                BPL     @done          ; passwords off
                LDA     DIDX
                STA     DSAVE
                LDA     DIDX + 1
                STA     DSAVE + 1
                JSR     EXTUSER
                JSR     PWFILE
                BCC     @back
                ASL     A              ; carry: read, N: write
                BCC     @write
                LDA     #ER_PASSWORD
                JMP     DERR
@write:         BPL     @back
                LDA     FC + FCB_F7
                ORA     #$80
                STA     FC + FCB_F7
@back:          LDA     DSAVE
                STA     DIDX
                LDA     DSAVE + 1
                STA     DIDX + 1
                JMP     DIRENT
@done:          RTS

; Password Error unless user A's file F names is unprotected or its password
; given (PWFILE); then FIND's pattern F's name in any extent of user A again.
PWNEED:         PHA
                JSR     PWFILE
                BCC     @given
                LDA     #ER_PASSWORD
                JMP     DERR
@given:         PLA
                LDX     #SM_ANYEX
                JMP     SETPAT

F_MAKE:         LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     NOWILD
                JSR     RODRV
                STZ     FC + FCB_S2     ; the program gives bytes 0-12: module 0
                LDA     FC + FCB_F6     ; f6' with passwords on: an XFCB too
                AND     MODE
                STA     MKPW
                LDX     #FCB_NAME       ; every attribute 0, in the entry and F
@attribute:     LDA     FC,X
                AND     #$7F
                STA     FC,X
                INX
                CPX     #FCB_EX
                BNE     @attribute
                LDA     SCB_USER        ; the file in any extent: File Exists
                LDX     #SM_ANYEX
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCS     @new
                LDA     #ER_EXISTS
                JMP     DERR
@new:           BIT     MKPW
                BPL     @room
                JSR     MKROOM         ; and its XFCB's
                BCS     @full
@room:          LDA     FC + FCB_EX     ; extent 0: stamped (MAKEENT)
                AND     #$1F
                CMP     #1
                LDA     SCB_USER
                BCS     @make
                ORA     #$80
@make:          JSR     MAKEENT
                BCC     @made
@full:          LDA     #$FF
                JMP     RETA
@made:          BIT     MKPW
                BPL     @empty
                JSR     MKXFCB         ; the DMA buffer's password and mode
@empty:         JSR     FEMPTY
                JSR     GENSET          ; extent 0's update is make's (the Guide,
                LDA     FC + FCB_EX     ; 21 and 34: a write stamps it only if
                AND     #$1F            ; make or a write hasn't), a later
                BNE     @out            ; extent's the first write's
                LDA     FC + FCB_S1
                ORA     #S1_STAMPED
                STA     FC + FCB_S1
@out:           JSR     FCBOUT
                JMP     RET0

F_CLOSE:        LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     GENCHK
                LDA     FC + FCB_S2
                BMI     @clean
                JSR     CLOSEX         ; the drive's buffers written back first
                BCS     @failed
                JSR     FCBOUT
                JMP     RET0
@clean:         LDA     CDRV           ; nothing to put in the directory, so a
                INC     A              ; buffer that won't write is no failure
                JSR     FLUSHD         ; of this close's
                JSR     GENCHK         ; the disk found changed as they were: 10
                JMP     RET0
@failed:        LDA     #$FF
                JMP     RETA

; Carry set if a marked buffer of drive CDRV (a write that failed) holds a
; sector of a block F points at: an entry put in for F would point at a
; sector never written. Another file's marked buffer is no matter to F's
; close: its entry doesn't point there, or did already. T2/T3 the block.
FMARKED:        LDX     #NBCB - 1
@buffer:        LDA     BCB_DIRTY,X
                CMP     #2
                BNE     @next
                LDA     BCB_DRV,X
                DEC     A
                CMP     CDRV
                BNE     @next
                JSR     BCBBLK         ; its block, T2/T3
                BCS     @next          ; before the directory: no file's
                PHX
                JSR     NPTRS
                STA     T4             ; F's pointers to go
@ptr:           DEC     T4
                BMI     @none
                LDA     T4
                JSR     FPTR           ; T0/T1
                LDA     T0
                ORA     T1
                BEQ     @ptr           ; no block there
                LDA     T0
                CMP     T2
                BNE     @ptr
                LDA     T1
                CMP     T3
                BNE     @ptr
                PLX
                SEC
                RTS
@none:          PLX
@next:          DEX
                BPL     @buffer
                CLC
                RTS

; F's extent, if written, into the directory: the drive's changed buffers
; written back first (the file's data on the disk before the entry that
; points at it); its entry found (made if it isn't there), the block
; pointers merged (one where the other has 0), EX and RC the later, its
; blocks permanent. No stamp: the update is the first write's (WROTE), as
; CP/M 3 makes it. Carry set, F and the entry as they were, if it can't be: two
; different blocks in one place (every pointer is checked before any is
; merged), or no entry to be had.
CLOSEX:         LDA     FC + FCB_S2
                BPL     @written
                CLC                    ; not written: nothing to do
                RTS
@written:       JSR     RODRV
                LDA     CDRV           ; the drive's buffers written back, then
                INC     A              ; before the directory is touched: a
                JSR     FLUSHD         ; unit found read-only Read Only, a
                JSR     GENCHK         ; sector of F's blocks not written
                JSR     RODRV          ; Disk I/O; O:'s or P:'s disk found
                JSR     FMARKED        ; changed (FLUSHD reads it first) 10
                BCC     @flushed
                LDA     CDRV
                STA     EDRV
                LDA     #ER_IO
                JMP     DERR
@flushed:       JSR     EXTUSER
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
@found:         STZ     MK             ; every pointer checked first
@check:         LDY     MK
                JSR     EPTR           ; BLK the entry's
                LDA     MK
                JSR     FPTR           ; T0/T1 F's
                LDA     T0
                ORA     T1
                BEQ     @agree
                LDA     BLK
                ORA     BLK + 1
                BEQ     @agree
                LDA     BLK
                CMP     T0
                BNE     @conflict
                LDA     BLK + 1
                CMP     T1
                BNE     @conflict
@agree:         INC     MK
                JSR     NPTRS
                CMP     MK
                BNE     @check
                BRA     @merge
@conflict:      SEC                    ; two blocks in one place: nothing changed
                RTS
@merge:         STZ     MK             ; then merged
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
                BNE     @next          ; both: the same (checked)
                LDA     MK             ; the entry has none: F's
                JSR     EPLACE
                LDA     T0
                STA     (DPTR),Y
                BIT     BIG
                BPL     @next
                INY
                LDA     T1
                STA     (DPTR),Y
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
                LDA     FC + FCB_S2
                ORA     #S2_CLEAN
                STA     FC + FCB_S2
                CLC
                RTS

; F from the entry at DPTR, for F's extent: the block pointers, the
; attributes (f1'-f4', t1'-t3'; f7' and f8' as they are), RC: 128 if the
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
                AND     #$3F
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
; empty and marked S2_NOENT (5 too).
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
                BRA     @noent
@full:          LDA     #5
@noent:         PHA                    ; a write here makes it (HAVEENT, SAMEEXT)
                LDA     FC + FCB_S2
                ORA     #S2_NOENT
                STA     FC + FCB_S2
                PLA
                SEC
                RTS

; F's extent's entry made now if a read found none (S2_NOENT), so a write
; that needs one and can't have it fails at once, as CP/M 3's do; carry set
; if the directory is full.
HAVEENT:        BIT     FC + FCB_S2
                BVC     @have
                JSR     EXTUSER
                JSR     MAKEENT
                BCS     @full
                LDA     FC + FCB_S2
                AND     #<~S2_NOENT
                STA     FC + FCB_S2
@have:          CLC
@full:          RTS

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
; opened from user 0, or without a write-protected file's password: f7').
WCHECK:         JSR     RODRV
                LDA     FC + FCB_T1
                ORA     FC + FCB_F8
                ORA     FC + FCB_F7
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

; Record CR of F's extent to RDMA; carry set if it has no block.
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
                LDA     RDMA
                STA     BW2
                LDA     RDMA + 1
                STA     BW2 + 1
                LDA     #128
                JSR     DOUT
                CLC
                RTS

; RDMA's record to record CR of F's extent: a block taken (the
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
                LDA     RDMA
                STA     BW1
                LDA     RDMA + 1
                STA     BW1 + 1
                LDA     RECP
                STA     BW2
                LDA     RECP + 1
                STA     BW2 + 1
                LDA     #128
                JSR     DIN
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
                JSR     WROTE
                CLC
                RTS

; F's extent written (the Guide, 2.3.4 and functions 21, 34): the archive
; attribute (t3') cleared in its entry, if F has it (Open and each extent's
; OPENEXT copy it from the entry, so F's says what the entry has); and the
; update stamped, in the file's extent-zero entry (STAMPU), if this open's
; isn't yet (S1_STAMPED: by make of extent 0, or an earlier write).
WROTE:          BIT     FC + FCB_T3
                BPL     @stamp
                LDA     FC + FCB_T3
                AND     #$7F
                STA     FC + FCB_T3
                JSR     EXTUSER
                LDX     #0
                JSR     SETPAT
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCS     @stamp         ; none: a write has its entry
                LDY     #FCB_T3
                LDA     (DPTR),Y
                AND     #$7F
                STA     (DPTR),Y
                JSR     DIRPUT
@stamp:         BIT     FC + FCB_S1
                BMI     @done
                LDA     FC + FCB_S1
                ORA     #S1_STAMPED
                STA     FC + FCB_S1
                JSR     EXTUSER
                SEC
                JMP     STAMPU
@done:          RTS

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

; The multi-sector count (44, the SCB's): a read or write of N records is N
; calls (the Guide, 2.3.10), each record at the DMA address + 128 x k, F
; read once and written back once. MULTI runs the record function at A/X
; (A = 0, or its error, F changed) to the first that isn't 0: A that, X the
; records done before it (the Guide's H); a physical error (DERR) returns
; as it does, F written back as the records done left it (MERR). MRAND's
; bit 6: R0-R2 moved on a record each time, and put back at the end. Carry
; set: a read, the records to the program's memory. All N records' memory
; is checked first (MRANGE): memory refused, A = X = $FF and nothing done.
MULTI:          STA     MBODY
                STX     MBODY + 1
                ROR     MRAND          ; the carry to bit 7, random to bit 6
                LDA     SCB_DMA
                STA     RDMA
                LDA     SCB_DMA + 1
                STA     RDMA + 1
                LDA     SCB_MULTI
                BNE     @count
                INC     A              ; 0, never set: 1
@count:         STA     MLEFT
                JSR     MRANGE
                BCC     @ok
                JMP     ABORT
@ok:            STZ     MDONE
@record:        JSR     @call
                TAY
                BNE     @stop
                INC     MDONE
                DEC     MLEFT
                BEQ     @stop
                LDA     #1
                STA     MON
                CLC
                LDA     RDMA
                ADC     #128
                STA     RDMA
                BCC     @rand
                INC     RDMA + 1
@rand:          BIT     MRAND
                BVC     @record
                INC     FC + FCB_R
                BNE     @record
                INC     FC + FCB_R + 1
                BNE     @record
                INC     FC + FCB_R + 2
                BRA     @record
@call:          JMP     (MBODY)
@stop:          STZ     MON
                PHY
                JSR     MBACK
                PLA
                BEQ     @done
                LDX     MDONE
                JMP     RETAX
@done:          JMP     RET0

; F back to the program, R0-R2 the first record's if random.
MBACK:          BIT     MRAND
                BVC     @out
                LDX     #2
@r:             LDA     RSAVE,X
                STA     FC + FCB_R,X
                DEX
                BPL     @r
@out:           JMP     FCBOUT

; Carry set if the MLEFT records at the DMA address aren't all the
; program's memory: past $FFFF, in the I/O hole, on flash (NB_ROM), or a
; read's in a read-only window (PCOPY's rule, the BIOS's PHYSOF), a page at
; a time. With a DMA segment (51), past $FFFF too, then the physical range
; segment x 16 + the DMA address on, which must be memory a segment may
; reach (bdosmem.s's SEGOK). Uses A, X, Y, T1.
MRANGE:         LDA     MLEFT          ; 128 x N - 1: (N - 1) / 2 and $7F, or $FF
                DEC     A
                LSR     A
                STA     T1
                LDA     #0
                ROR     A
                ORA     #$7F
                CLC
                ADC     SCB_DMA
                LDA     T1
                ADC     SCB_DMA + 1
                BCS     @no            ; past $FFFF
                STA     T1             ; the last record's last page
                LDA     DMASEG
                AND     DMASEG + 1
                CMP     #$FF
                BEQ     @cpu
                LDX     #RDMA          ; MULTI's copy of the DMA address
                JSR     SEGADR
                LDA     MLEFT          ; 128 x N - 1 again, A/Y
                DEC     A
                LSR     A
                TAY
                LDA     #0
                ROR     A
                ORA     #$7F
                JMP     SEGOK
@cpu:           LDY     SCB_DMA + 1
@page:          TYA
                CMP     #>CODE_WINDOW
                BCS     @mem
                CMP     #$C0
                BCS     @no            ; the I/O hole
@mem:           ROL     A              ; the window: the page's top 2 bits
                ROL     A
                ROL     A
                AND     #3
                TAX
                LDA     ST_WIN,X
                BIT     #NB_ROM
                BNE     @no
                BIT     MRAND
                BPL     @next
                AND     #NB_RO
                BNE     @no
@next:          CPY     T1
                BEQ     @yes
                INY
                BRA     @page
@yes:           CLC
                RTS
@no:            SEC
                RTS

; DERR's, before a physical error returns: a multi-sector call's records
; done kept in the program's FCB.
MERR:           LDA     MON
                BEQ     @none
                STZ     MON
                JMP     MBACK
@none:          RTS

; MRAND random (MULTI moves it to bit 6), R0-R2 kept to put back.
RSTART:         LDA     #$80
                STA     MRAND
                LDX     #2
@r:             LDA     FC + FCB_R,X
                STA     RSAVE,X
                DEX
                BPL     @r
                RTS

F_SREAD:        LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     GENCHK
                STZ     MRAND
                LDA     #<SREAD1
                LDX     #>SREAD1
                SEC                    ; a read
                JMP     MULTI
SREAD1:         LDA     FC + FCB_CR
                BPL     @record
                JSR     NEXTEXT        ; past the extent's end: the next one
                BCS     @end
                LDA     #0
                JSR     POSITION
                BCC     @moved
                CMP     #3             ; 3: F still at its extent, CR as it was
                BEQ     @end
                STZ     FC + FCB_CR     ; 4: at the next, which has no entry
                BRA     @end
@moved:         STZ     FC + FCB_CR
@record:        LDA     FC + FCB_CR
                CMP     FC + FCB_RC
                BCS     @end
                JSR     READREC
                BCS     @end
                INC     FC + FCB_CR
                LDA     #0
                RTS
@end:           LDA     #1
                RTS

F_SWRITE:       LDA     #FCB_SEQ
                JSR     FCBSEL
                JSR     GENCHK
                JSR     WCHECK
                STZ     MRAND
                LDA     #<SWRITE1
                LDX     #>SWRITE1
                CLC                    ; a write
                JMP     MULTI
SWRITE1:        LDA     FC + FCB_CR
                BPL     @record
                JSR     NEXTEXT
                BCS     @full          ; past 32 MB
                LDA     #1
                JSR     POSITION
                BCC     @moved
                CMP     #3             ; 3: F still at its extent, CR as it was
                BEQ     @nodir
                STZ     FC + FCB_CR     ; 5: at the next, no entry to be had
                BRA     @nodir
@moved:         STZ     FC + FCB_CR
@record:        JSR     HAVEENT
                BCS     @nodir
                JSR     WRITEREC
                BCS     @full
                INC     FC + FCB_CR
                LDA     #0
                RTS
@full:          LDA     #2
                RTS
@nodir:         LDA     #1
                RTS

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

; Z set if NEXT is F's extent and F has its entry (not S2_NOENT: POSITION
; looks again, or makes it).
SAMEEXT:        BIT     FC + FCB_S2
                BVC     @ext
                LDA     #1             ; Z clear
                RTS
@ext:           JSR     FEXT
                LDA     NEXT
                CMP     T0
                BNE     @done
                LDA     NEXT + 1
                CMP     T1
@done:          RTS

F_RREAD:        LDA     #FCB_RAND
                JSR     FCBSEL
                JSR     GENCHK
                JSR     RSTART
                LDA     #<RREAD1
                LDX     #>RREAD1
                SEC                    ; a read
                JMP     MULTI
RREAD1:         JSR     RANDPOS
                BCS     @far
                JSR     SAMEEXT
                BEQ     @here
                LDA     #0
                JSR     POSITION
                BCC     @here
                PHA                    ; 3 or 4
                LDA     RRCR
                STA     FC + FCB_CR
                PLA
                RTS
@here:          LDA     RRCR
                STA     FC + FCB_CR
                CMP     FC + FCB_RC
                BCS     @unwritten
                JSR     READREC
                BCS     @unwritten
                LDA     #0
                RTS
@unwritten:     LDA     #1
                RTS
@far:           LDA     #6
                RTS

F_RWRITEZ:
F_RWRITE:       LDA     #FCB_RAND
                JSR     FCBSEL
                JSR     GENCHK
                JSR     WCHECK
                JSR     RSTART
                LDA     #<RWRITE1
                LDX     #>RWRITE1
                CLC                    ; a write
                JMP     MULTI
RWRITE1:        JSR     RANDPOS
                BCS     @far
                JSR     SAMEEXT
                BEQ     @here
                LDA     #1
                JSR     POSITION
                BCC     @here
                PHA                    ; 3 or 5
                LDA     RRCR           ; CR the record's, as a random read leaves it
                STA     FC + FCB_CR
                PLA
                RTS
@here:          LDA     RRCR
                STA     FC + FCB_CR
                JSR     WRITEREC
                BCS     @full
                LDA     #0
                RTS
@full:          LDA     #2
                RTS
@far:           LDA     #6
                RTS

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
@all:           LDX     #2             ; 0 if there's none, as CP/M 2.2's
@r:             LDA     SZ,X
                STA     FC + FCB_R,X
                DEX
                BPL     @r
                LDA     FOUND
                BNE     @size
                JSR     FCBOUT
                LDA     #$FF
                JMP     RETA
@size:          JSR     FCBOUT
                JMP     RET0

; ---------------------------------------------------------------------------
; The directory

; Each entry F's pattern (SETPAT's, done) matches, FOUND 1 if any; File R/O
; for a read-only file's (not an XFCB's), if A says so.
FINDALL:        STA     T5
                STZ     FOUND
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                LDA     #1
                STA     FOUND
                LDA     T5
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
@pattern:       LDA     FC + FCB_EX     ; S2 is the program's only with '?' in EX
                CMP     #'?'
                BEQ     @user
                STZ     FC + FCB_S2
@user:          LDA     SCB_USER
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
@delete:        JSR     PWALL          ; nor protected without its password
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                LDA     (DPTR)
                CMP     #16
                BCS     @gone          ; an XFCB: always
                BIT     FC + FCB_F5     ; f5': the XFCBs only
                BMI     @next
                LDA     #DIR_FREE      ; the entry gone on the disk, then its
                STA     (DPTR)         ; blocks freed (its pointers are kept)
                JSR     DIRPUT
                JSR     FREEE
                BRA     @next
@gone:          BIT     FC + FCB_F5     ; the XFCB only: its file's slot's mode 0
                BPL     @xgone
                JSR     XCLEAR
@xgone:         LDA     #DIR_FREE
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
@rename:        JSR     PWALL
                STZ     DIDX
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
@done:          JMP     RET0

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
@set:           LDA     SCB_USER
                JSR     PWNEED
                STZ     DIDX
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
@done:          JMP     RET0

; Truncate: R0-R2 the new last record. Every extent checked first (none
; read-only, the file larger than R, R where an entry of its group has data:
; TDATA); then those past R's extent's group deleted, and the one with it
; ending at R.
F_TRUNC:        LDA     #FCB_RAND
                JSR     FCBSEL
                JSR     NOWILD
                JSR     RODRV
                JSR     RANDPOS
                BCC     @r
                JMP     @refused
@r:             LDA     SCB_USER
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
                JSR     TDATA          ; and R where it has data
                BCS     @next
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
                LDA     SCB_USER       ; then the password, last
                JSR     PWNEED
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
@entry:         BCS     @done
                JSR     EGROUP
                BEQ     @ends
                BCC     @kept          ; before R's group
                LDA     #DIR_FREE      ; after it: gone on the disk, then its
                STA     (DPTR)         ; blocks freed
                JSR     DIRPUT
                JSR     FREEE
                BRA     @kept
@ends:          JSR     TCUT
@kept:          JSR     FINDNXT
                BRA     @entry
@done:          LDA     SCB_USER       ; the update, in extent 0's slot
                SEC
                JSR     STAMPU
                JMP     RET0
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

; Carry clear if R is where the entry at DPTR (of R's group) has data: its
; extent at or after R's, R below its RC if R's extent is the entry's, and
; R's block pointer not 0 (the Guide: R in a region where data exists).
; F's EX and CR R's (FPIDX's) after.
TDATA:          JSR     DEXT           ; T2/T3 the entry's extent
                LDA     T2
                CMP     NEXT
                BNE     @other
                LDA     T3
                CMP     NEXT + 1
                BNE     @other
                LDY     #FCB_RC        ; R's extent is the entry's: R below RC
                LDA     RRCR
                CMP     (DPTR),Y
                BCS     @no
                BRA     @block
@other:         LDA     T2             ; before R's: no data there
                CMP     NEXT
                LDA     T3
                SBC     NEXT + 1
                BCC     @no
@block:         LDA     NEXT
                STA     FC + FCB_EX
                LDA     RRCR
                STA     FC + FCB_CR
                JSR     FPIDX
                TAY
                JSR     EPTR
                LDA     BLK
                ORA     BLK + 1
                BEQ     @no
                CLC
                RTS
@no:            SEC
                RTS

; The entry at DPTR (of R's group) ends at R: EX and S2 R's extent, RC its
; record + 1, the pointers after R's block cleared; the entry written, then
; their blocks freed, from F's pointers, where the entry's are kept (truncate
; writes no FCB back). F_TRUNC stamps the update in the extent-zero entry
; when they're all done.
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
                LDY     #FCB_D         ; the pointers, to free once it's written
@keep:          LDA     (DPTR),Y
                STA     FC,Y
                INY
                CPY     #FCB_CR
                BNE     @keep
                LDA     NEXT           ; R's pointer, as FPIDX works it out
                STA     FC + FCB_EX
                LDA     RRCR
                STA     FC + FCB_CR
                JSR     FPIDX
                PHA
                STA     MK
@clear:         INC     MK
                JSR     NPTRS
                CMP     MK
                BEQ     @put
                LDA     MK
                JSR     EPLACE
                LDA     #0
                STA     (DPTR),Y
                BIT     BIG
                BPL     @clear
                INY
                STA     (DPTR),Y
                BRA     @clear
@put:           LDY     #FCB_T3        ; changed: not archived (ours, as a write)
                LDA     (DPTR),Y
                AND     #$7F
                STA     (DPTR),Y
                JSR     DIRPUT
                PLA
                STA     MK
@free:          INC     MK
                JSR     NPTRS
                CMP     MK
                BEQ     @done
                LDA     MK
                JSR     FPTR           ; T0/T1 the pointer as it was
                LDA     T0
                STA     BLK
                LDA     T1
                STA     BLK + 1
                ORA     BLK
                BEQ     @free
                LDA     DSM
                CMP     BLK
                LDA     DSM + 1
                SBC     BLK + 1
                BCC     @free          ; past DSM: no block to free
                JSR     ALVCLR
                BRA     @free
@done:          RTS


; ---------------------------------------------------------------------------
; Media change (6c): an FCB opened or made on a removable drive (O:, P:)
; carries in S1's bits 0-6 the count of the drive's changes seen then
; (DGEN, bdosdisk.s's MEDIACHK); bit 7 stays S1_STAMPED. Another count now
; is the Guide's error 10, returned in A: the disk under it was changed.

; F's S1: the drive's count, or 0 on a drive that isn't removable.
GENSET:         LDA     CSLOT
                SEC
                SBC     #RSLOT
                BCC     @fixed
                TAX
                LDA     DGEN,X
                STA     FC + FCB_S1
                RTS
@fixed:         STZ     FC + FCB_S1
                RTS

; F opened on this disk: on; if not, the function ends here: A = 10.
GENCHK:         LDA     CSLOT
                SEC
                SBC     #RSLOT
                BCC     @same
                TAX
                LDA     FC + FCB_S1
                AND     #$7F
                CMP     DGEN,X
                BEQ     @same
                LDA     #RC_MEDIA
                JMP     RETA
@same:          RTS

; ---------------------------------------------------------------------------
; Function 83's DM_LOAD (hw/oolib.md, L4): the file's part, block 60's, as
; it positions by the extents and reads through the buffers, which move W2.
; Video.s's DM_LOADX (block 61) has read the block and checked the count
; (1-512) and the destination's 128 x count bytes: LFCB, LREC, LCNT, LDST.
;
; The FCB is read whole (36 bytes) and its drive selected, as 33's; the
; drive's changed buffers are written back first, so the load sees every
; write. Then a step at a time, from record LREC, each positioned as 33
; positions (RANDPOS, SAMEEXT, POSITION): on an SD unit of OODOS's own
; formats or M:, from a record that starts a sector, the whole sectors of
; its block that the extent has and the count wants, at most 8 (4 KB) a
; step, go straight to the destination by the BIOS's RDRUN (one READ on
; channel 0, or DMAGO for M:); anything else, a record at a time through a
; buffer (GETBUF) and MOVE to the physical destination, as 33 with a DMA
; segment does, the DMA segment itself untouched. apple-do drives (O:, P:,
; an apple-do unit) always go the second way: their sectors are in the
; track buffer. A = 0; 1 at a record past the extent's RC, a block never
; written, or an extent never written (as 33's 1 and 4), the step's records
; done; 3 or 6 as 33 gives them; a physical error as 33's (DERR). An FCB
; never opened has no blocks: 1, nothing loaded (the owner's decision: the
; FCB has no mark of being open the BDOS could trust). +6-7 of the block
; get the records loaded; the FCB is written back as a random read of the
; last record loaded leaves it (R0-R2 and CR that record's, F on its
; extent), or of the first if none was.
.import LFCB, LREC, LCNT, LDST, LDONE  ; video.s's
.import RDRUN, DRVAPL                  ; bios.s's
.importzp SRCP
.export LOADGO

.segment "SYSBSS"
LDIR:           .res    1              ; bit 7: the drive's whole sectors may go straight

.segment "BDOS"

LOADGO:         LDA     LFCB
                STA     PP
                LDA     LFCB + 1
                STA     PP + 1
                LDA     #FCB_RAND
                JSR     FCBSEL
                JSR     GENCHK
                LDA     CDRV
                STA     EDRV
                INC     A
                JSR     FLUSHD         ; the drive's changed buffers written first
                BCC     @flushed
                LDA     RERR
                JMP     DERR
@flushed:       STZ     LDONE
                STZ     LDONE + 1
                STZ     LDIR
                LDA     CDRV
                CMP     #DRV_M
                BEQ     @direct
                CMP     #SD_DRIVES
                BCS     LDSTEP
                TAX
                LDA     DRVAPL,X
                BMI     LDSTEP         ; apple-do: through its track buffer
@direct:        DEC     LDIR
LDSTEP:         LDA     LCNT
                ORA     LCNT + 1
                BNE     @more
                LDA     #0
                JMP     LDEND
@more:          LDX     #2
@r:             LDA     LREC,X
                STA     FC + FCB_R,X
                DEX
                BPL     @r
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
                PLA
                CMP     #4
                BEQ     @eof0
                JMP     LDEND          ; 3: as 33
@far:           LDA     #6
                JMP     LDEND
@eof0:          JMP     LDEOF          ; (for the branches below)
@buf0:          JMP     LDBUF
@here:          LDA     RRCR
                STA     FC + FCB_CR
                CMP     FC + FCB_RC
                BCS     @eof0
                JSR     FPIDX
                JSR     FPTR
                LDA     T0
                STA     BLK
                LDA     T1
                STA     BLK + 1
                ORA     BLK
                BEQ     @eof0         ; a block never written
                BIT     LDIR
                BPL     @buf0
                LDA     FC + FCB_CR
                AND     #3
                BNE     @buf0        ; not a sector's first record
                SEC                    ; T2 = the records here: the extent's, the block's, the count's
                LDA     FC + FCB_RC
                SBC     FC + FCB_CR
                STA     T2
                LDA     FC + FCB_CR
                AND     BLM
                STA     T3
                SEC
                LDA     BLM
                SBC     T3
                INC     A              ; the block's records from CR on
                CMP     T2
                BCS     @blk
                STA     T2
@blk:           LDA     LCNT + 1
                BNE     @cnt
                LDA     LCNT
                CMP     T2
                BCS     @cnt
                STA     T2
@cnt:           LSR     T2             ; whole sectors, 8 at most
                LSR     T2
                BEQ     @buf0
                LDA     T2
                CMP     #9
                BCC     @run
                LDA     #8
                STA     T2
@run:           JSR     RECSEC         ; LBA: CR's sector (T4 0)
                LDA     CDRV           ; the BIOS's drive, as DISKOP selects it
                INC     A
                CMP     BIOSDRV
                BEQ     @sel
                STA     BIOSDRV
                DEC     A
                LDX     #1
                JSR     B_SELDSK
                STX     T3
                ORA     T3
                BNE     @sel
                STZ     BIOSDRV
                BRA     @ioerr
@sel:           LDA     LBA
                STA     BW1
                LDA     LBA + 1
                STA     BW1 + 1
                LDX     #2
@d:             LDA     LDST,X
                STA     SRCP,X
                DEX
                BPL     @d
                LDA     #BIOS_BLOCK    ; block 61's RDRUN, called directly
                STA     NB_WIN2
                LDA     T2
                JSR     RDRUN
                CMP     #0
                BNE     @ioerr
                LDA     T2             ; 4 records a sector
                ASL     A
                ASL     A
                JSR     LDADV
                JMP     LDSTEP
@ioerr:         LDA     #ER_IO
                JMP     DERR

; One record through a buffer to the destination.
LDBUF:         JSR     RECSEC
                LDA     #0
                JSR     GETBUF
                JSR     RECPTR
                LDA     RECP
                STA     BW1
                LDA     RECP + 1
                STA     BW1 + 1
                LDA     LDST
                STA     BW2
                LDA     LDST + 1
                STA     BW2 + 1
                LDA     LDST + 2       ; a physical bank: $80 + A16-A19
                ORA     #$80
                TAX
                LDA     #BANK_SYS
                JSR     B_XMOVE
                LDA     #128
                LDX     #0
                JSR     B_MOVE
                LDA     #1
                JSR     LDADV
                JMP     LDSTEP

LDEOF:          LDA     #1
LDEND:          PHA
                LDA     LDONE
                ORA     LDONE + 1
                BEQ     @out           ; none: R0-R2 the first's, as the step left them
                SEC                    ; R0-R2 the last loaded, CR its place in its extent
                LDA     LREC
                SBC     #1
                STA     FC + FCB_R
                LDA     LREC + 1
                SBC     #0
                STA     FC + FCB_R + 1
                LDA     LREC + 2
                SBC     #0
                STA     FC + FCB_R + 2
                JSR     RANDPOS        ; and F at that record's extent, as 33 leaves it: a step
                JSR     SAMEEXT        ; past a full extent's end left F on the next, empty one
                BEQ     @cr            ; (review F4)
                LDA     #0
                JSR     POSITION       ; (its extent was found before: it is there)
@cr:            LDA     RRCR
                STA     FC + FCB_CR
@out:           JSR     FCBOUT
                CLC                    ; the block's +6-7: the records loaded
                LDA     PA
                ADC     #6
                STA     PP
                LDA     PX
                ADC     #0
                STA     PP + 1
                LDA     LDONE
                JSR     PPUT
                LDA     LDONE + 1
                JSR     PPUT
                PLA
                JMP     RETA

; A records done (1-32): the record, the count, the records loaded and the
; destination (A x 128 bytes) moved on.
LDADV:          STA     T3
                CLC
                ADC     LREC
                STA     LREC
                BCC     @cnt
                INC     LREC + 1
                BNE     @cnt
                INC     LREC + 2
@cnt:           SEC
                LDA     LCNT
                SBC     T3
                STA     LCNT
                BCS     @done
                DEC     LCNT + 1
@done:          CLC
                LDA     LDONE
                ADC     T3
                STA     LDONE
                BCC     @dst
                INC     LDONE + 1
@dst:           LDA     T3             ; A x 128: (A / 2) x 256 + (A & 1) x 128
                LSR     A
                TAY
                LDA     #0
                ROR     A
                CLC
                ADC     LDST
                STA     LDST
                TYA
                ADC     LDST + 1
                STA     LDST + 1
                BCC     @end
                INC     LDST + 2
@end:           RTS

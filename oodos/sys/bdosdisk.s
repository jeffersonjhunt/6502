; OODOS's BDOS, its disks (hw/oodos.md, "The BDOS, phase 3"): the drives and
; their login, the sector buffers (deblocking), the directory and its hash,
; the allocation vectors and the date stamps. Block 60, with bdos.s and
; bdosfile.s. Written from the CP/M 3 manuals' descriptions.
;
; Drives. A function works on one drive at a time, CDRV, through CD: its
; DPB's fields and its high-water mark, kept per drive's slot in DTAB (A-G
; are slots 0-6, M: 7). SELECT logs a drive in on its first select after a
; reset (13, 37): SELDSK as a first select (the BIOS reads the card's units
; again, and the drives whose unit changed are logged out: CHANGED), the DPB
; from common memory, then the whole directory read once to make the
; allocation vector and the hash, and the label's mode byte kept for the
; stamps. A read-only unit (the units table's flag) makes the drive
; read-only. Every OODOS format has 512-byte sectors, 16 a track.
;
; Buffers. 32 sectors of 512 bytes in block 28 (BCB_BLOCK), mapped into W2
; while the BDOS uses one: each buffer's drive, sector, changed flag and
; when it was last used are in the system block. GETBUF finds a sector or
; takes the least recently used buffer (an empty one first), writing it back
; first if it changed. Directory changes are written at once (DIRPUT); a
; data sector when its buffer is taken, by 48, 13 or 37, at the warm boot
; (the CCP's 48), and before a close or an extent change writes the entry
; that points at it (CLOSEX). A data write that fails keeps the buffer
; changed, marked (BCB_DIRTY 2), so a close whose entry would point at its
; sector fails again rather than point at a sector never written (another
; file's close doesn't); GETBUF takes a marked buffer only when all 32 are,
; and tries its write again (still failing: Disk I/O). A read-only unit's
; failure drops the buffer and makes the drive read-only; so does a
; directory write's (DIRPUT), the directory in memory kept as it is on the
; disk. 48 writes every buffer it can, then says the first failure; 13 and
; 37 drop what they can't write, as they log the drive out, and say so
; after; 98, the warm boot's, drops the marked ones whose blocks it frees
; (an unclosed file's new blocks: the program that wrote them has ended),
; and keeps one of a block the directory has (a closed file's record
; written again in place) for the next 48 or close to write.
;
; The hash (ours, in memory only): 4 bytes a directory entry, in blocks 24-
; 27, 8 KB a slot (HBLK, HBASE): the entry's first byte (the user, $E5,
; $20, $21), a 16-bit hash of its name (each byte's low 7 bits into a word
; turned left a bit each time), and its extent (EX's 5 bits and S2's low 3).
; FIND compares those before it reads an entry, then the entry itself in
; full, so a hash that matches the wrong name doesn't. Files are looked for
; up to the high-water mark (HWM); free entries and every entry (17 with '?'
; in the drive byte) to the directory's end.
;
; Allocation vectors: CP/M 3's two bits a block, as two vectors of 512
; bytes a slot in the system block (ALV_BASE): the current (every block in
; use, bit 7 of byte 0 block 0, as AL0 has it) and the permanent (the blocks
; the directory has). A write takes a block in the current one only; close
; puts its entry's blocks in both; delete and truncate free them in both,
; once the entry is written;
; function 98 makes the current the permanent again, freeing what unclosed
; files had. A newly taken block is zeroed (its sectors new buffers, zeros,
; changed), so a record never written in it reads as zeros.
;
; Stamps: an entry's SFCB is entry i | 3, its 10 bytes from 1 + (i & 3) x 10:
; creation (or access), update, the password mode and a reserved byte.
; CP/M 3 keeps a file's stamps in its extent-zero entry's slot only: a made
; entry's slot is cleared, and Make of extent 0 stamps the creation (or
; access) and the update as the label's mode says (INITSTAMP); CLOSEX and
; truncate stamp the update in the extent-zero entry's slot whichever extent
; they changed (STAMPU), and Open of extent 0 the access if the mode asks.
; Later extents' slots stay 0. The time is BIOS TIME's in the SCB. A
; directory without SFCBs gets none. A made entry's slot is cleared even
; when the label asks for no stamps (3b1: 102 reads it).

.setcpu "65C02"
.include "bdos.inc"
.import DRVRO, DRVCHG

DPH_DPB         = 12                   ; the DPH's DPB pointer

.zeropage
GDRV:           .res    1              ; GETBUF's drive + 1
FDRV:           .res    1              ; FLUSHD's and PURGED's
DBCB:           .res    1              ; DIRENT's buffer
DOPW:           .res    1              ; DISKOP: 0 read, 1 write
DOPX:           .res    1              ; its buffer
VIDX:           .res    1              ; VICTIM's
VBEST:          .res    2
VD:             .res    2
AP:             .res    2              ; ALVBIT's byte of the vector
AMASK:          .res    1              ; and its bit
MK:             .res    1              ; MARKE's and FREEE's pointer
NBYTES:         .res    2              ; ALVFREE's bytes to go
MKU:            .res    1              ; MAKEENT's user, bit 7: Make's extent 0
STW:            .res    1              ; STAMP's stamp: 0 or 4
STOFS:          .res    1              ; the slot's offset in its SFCB
SDID:           .res    2              ; the entry SFCBAT came from
UDID:           .res    2              ; the entry STAMPU came from
LOGL:           .res    1              ; LOGIN: a label seen
CHG:            .res    1              ; CHANGED's drives to go
WBM:            .res    1              ; WBACK's mode: bit 7 drop, bit 6 return
RERR:           .res    1              ; RFLUSH's first error, 0 none
REDRV:          .res    1              ; and its drive
PBX:            .res    1              ; PURGEM's buffer
.exportzp DBCB, AP, AMASK, MK, RERR

.segment "BDOS"

; A = drive (0-15) to its slot, carry clear; carry set if it has none.
DSLOT:          CMP     #SD_DRIVES
                BCC     @done
                CMP     #DRV_M
                BNE     @none
                LDA     #NSLOT - 1
                CLC
@done:          RTS
@none:          SEC
                RTS

; Drive A the one the function works on, logged in if it isn't. Not a
; drive: Invalid Drive.
SELECT:         STA     CDRV
                STA     EDRV
                JSR     DSLOT
                BCS     @bad
                STA     CSLOT
                JSR     CHANGED        ; a rescan since (a program's SELDSK)
                LDA     CDRV
                JSR     DRVBIT
                AND     LOGVEC,X
                BNE     LOADCD
                JMP     LOGIN
@bad:           LDA     #ER_SELECT
                JMP     DERR

; The card's units read again (a first select, of A:), then CHANGED: 13 and
; 37 run it before they write anything back, so a drive whose image was
; swapped since has its old buffers dropped, not written to the new one.
RESCAN:         LDA     #0
                TAX                    ; X 0: a first select
                JSR     B_SELDSK
                STZ     BIOSDRV        ; the BIOS has A: (or none): DISKOP selects again
                JMP     CHANGED

; The drives whose unit changed at a rescan of the units (the BIOS's
; DRVCHG) logged out, read-write, their buffers dropped, not written: the
; card's unit isn't what they came from. A drive not logged in has nothing
; to drop; one whose unit is as it was is left alone.
CHANGED:        LDA     DRVCHG
                BEQ     @done
                STA     CHG
                STZ     DRVCHG
                LDX     #0             ; X the drive
@drive:         LSR     CHG
                BCC     @next
                PHX
                TXA
                JSR     DRVBIT
                AND     LOGVEC,X
                BEQ     @out
                PLX
                PHX
                TXA
                INC     A
                JSR     PURGED
                PLX
                PHX
                TXA
                JSR     DRVBIT
                EOR     #$FF
                PHA
                AND     LOGVEC,X
                STA     LOGVEC,X
                PLA
                AND     ROVEC,X
                STA     ROVEC,X
@out:           PLX
@next:          INX
                LDA     CHG
                BNE     @drive
@done:          RTS

; CD from the slot's DTAB entry, and where its hash and ALV are (PLACES).
LOADCD:         LDA     CSLOT
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                TAX
                LDY     #0
@copy:          LDA     DTAB,X
                STA     CD,Y
                INX
                INY
                CPY     #CD_LEN
                BNE     @copy
PLACES:         LDA     CSLOT          ; HBLK 24 + slot / 2, HBASE $80 or $A0
                LSR     A
                TAX
                LDA     #$80
                BCC     @even
                LDA     #$A0
@even:          STA     HBASE
                TXA
                CLC
                ADC     #HASH_BLOCK
                STA     HBLK
                LDA     CSLOT          ; ALVP: ALV_BASE + slot x $400
                ASL     A
                ASL     A
                CLC
                ADC     #>ALV_BASE
                STA     ALVP + 1
                STZ     ALVP
                RTS
.assert <ALV_BASE = 0, error, "ALV_BASE is a page's start"

SAVECD:         LDA     CSLOT
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                TAX
                LDY     #0
@copy:          LDA     CD,Y
                STA     DTAB,X
                INX
                INY
                CPY     #CD_LEN
                BNE     @copy
                RTS

; The first select since a reset: the DPB, then the directory read for the
; allocation vector, the hash, the high-water mark and the label's mode.
LOGIN:          JSR     PLACES
                LDA     CDRV
                INC     A
                JSR     PURGED         ; nothing of the drive's from before is kept
                LDA     CDRV
                LDX     #0             ; a first select: the BIOS reads the units again
                JSR     B_SELDSK
                STA     T0
                STX     T1
                ORA     T1
                BNE     @dph
                LDA     #ER_SELECT
                JMP     DERR
@dph:           JSR     CHANGED        ; the units read again: others changed?
                LDA     CDRV
                INC     A
                STA     BIOSDRV
                LDA     #BIOS_BLOCK    ; the DPH is the BIOS's
                STA     NB_WIN2
                LDY     #DPH_DPB
                LDA     (T0),Y
                STA     DPBP
                INY
                LDA     (T0),Y
                STA     DPBP + 1
                LDY     #2             ; the DPB, in common memory: BSH, BLM, EXM, DSM, DRM
@dpb:           LDA     (DPBP),Y
                STA     CD - 2,Y
                INY
                CPY     #9
                BNE     @dpb
                LDA     (DPBP),Y       ; AL0 and AL1
                STA     T2
                INY
                LDA     (DPBP),Y
                STA     T3
                LDY     #13            ; OFF: tracks of 16 sectors
                LDA     (DPBP),Y
                STA     OFFS
                INY
                LDA     (DPBP),Y
                STA     OFFS + 1
                LDX     #4
@off:           ASL     OFFS
                ROL     OFFS + 1
                DEX
                BNE     @off
                LDA     DSM + 1
                BEQ     @small
                LDA     #$80
@small:         STA     BIG
                STZ     MODE
                STZ     HWM
                STZ     HWM + 1
                STZ     LOGL
                LDA     EXM
                EOR     #$1F
                AND     #$1F
                STA     EXMASK
                LDA     ALVP + 1       ; both vectors empty, then the directory's blocks
                STA     T1
                STZ     T0
                LDX     #4
                LDA     #0
                TAY
@clear:         STA     (T0),Y
                INY
                BNE     @clear
                INC     T1
                DEX
                BNE     @clear
                LDA     T2
                STA     (ALVP)
                LDY     #1
                LDA     T3
                STA     (ALVP),Y
                LDA     ALVP + 1
                CLC
                ADC     #2
                STA     T1
                LDA     T2
                STA     (T0)
                LDA     T3
                STA     (T0),Y
                STZ     DIDX
                STZ     DIDX + 1
                JSR     HPSET
@entry:         JSR     DIRENT
                JSR     HASHE
                LDA     H0
                CMP     #16
                BCS     @other
                JSR     MARKE          ; a file's blocks
                BRA     @used
@other:         CMP     #32
                BCC     @used          ; an XFCB
                CMP     #DIR_LABEL
                BNE     @hash
                LDA     LOGL
                BNE     @used          ; the first label's mode
                INC     LOGL
                LDY     #FCB_EX
                LDA     (DPTR),Y
                STA     MODE
@used:          CLC
                LDA     DIDX
                ADC     #1
                STA     HWM
                LDA     DIDX + 1
                ADC     #0
                STA     HWM + 1
@hash:          LDA     HBLK
                STA     NB_WIN2
                LDY     #0
                LDA     H0
                STA     (HP),Y
                INY
                LDA     HV
                STA     (HP),Y
                INY
                LDA     HV + 1
                STA     (HP),Y
                INY
                LDA     H3
                STA     (HP),Y
                CLC
                LDA     HP
                ADC     #4
                STA     HP
                BCC     @next
                INC     HP + 1
@next:          LDA     DIDX           ; the last entry, DRM?
                CMP     DRM
                BNE     @more
                LDA     DIDX + 1
                CMP     DRM + 1
                BEQ     @logged
@more:          INC     DIDX
                BNE     @entry
                INC     DIDX + 1
                BRA     @entry
@logged:        LDA     CDRV
                JSR     DRVBIT
                ORA     LOGVEC,X
                STA     LOGVEC,X
                LDA     CSLOT          ; a read-only unit: a read-only drive
                CMP     #SD_DRIVES
                BCS     @save
                TAX
                LDA     DRVRO,X
                AND     #2
                BEQ     @save
                LDA     CDRV
                JSR     DRVBIT
                ORA     ROVEC,X
                STA     ROVEC,X
@save:          JMP     SAVECD

; ---------------------------------------------------------------------------
; Buffers

; Sector LBA of drive CDRV in a buffer: A = 0 read from the disk if it isn't
; in one; 1 a new sector, zeroed and not read. X its buffer, BPTR its
; address in W2, W2 on the buffers. Keeps T0-T5.
GETBUF:         STA     GBNEW
                LDA     CDRV
                INC     A
                STA     GDRV
                LDX     LASTB          ; the last one first
                JSR     SAME
                BEQ     @hit
                LDX     #NBCB - 1
@scan:          JSR     SAME
                BEQ     @hit
                DEX
                BPL     @scan
                JSR     VICTIM
                LDA     BCB_DIRTY,X
                BEQ     @take
                JSR     WBACKX
@take:          LDA     GDRV
                STA     BCB_DRV,X
                LDA     LBA
                STA     BCB_LO,X
                LDA     LBA + 1
                STA     BCB_HI,X
                STZ     BCB_DIRTY,X
                LDA     GBNEW
                BNE     @zero
                JSR     RDBCB
                BRA     @used
@hit:           LDA     GBNEW
                BEQ     @used
@zero:          JSR     BUFAT
                JSR     ZEROB
@used:          STX     LASTB
                INC     AGE
                BNE     @age
                INC     AGE + 1
@age:           LDA     AGE
                STA     BCB_AGEL,X
                LDA     AGE + 1
                STA     BCB_AGEH,X
; BPTR at buffer X, W2 on the buffers.
BUFAT:          STZ     BPTR
                TXA
                ASL     A
                ORA     #$80
                STA     BPTR + 1
                LDA     #BCB_BLOCK
                STA     NB_WIN2
                RTS

; Z set if buffer X holds GDRV's sector LBA.
SAME:           LDA     BCB_DRV,X
                CMP     GDRV
                BNE     @done
                LDA     BCB_LO,X
                CMP     LBA
                BNE     @done
                LDA     BCB_HI,X
                CMP     LBA + 1
@done:          RTS

; X = an empty buffer, or the least recently used (AGE less its use the
; most) but a marked one (a write that failed); buffer 0 if all are marked.
VICTIM:         LDX     #NBCB - 1
@empty:         LDA     BCB_DRV,X
                BEQ     @done
                DEX
                BPL     @empty
                STZ     VBEST
                STZ     VBEST + 1
                STZ     VIDX
                LDX     #NBCB - 1
@lru:           SEC
                LDA     AGE
                SBC     BCB_AGEL,X
                STA     VD
                LDA     AGE + 1
                SBC     BCB_AGEH,X
                STA     VD + 1
                CMP     VBEST + 1
                BCC     @next
                BNE     @take
                LDA     VD
                CMP     VBEST
                BCC     @next
@take:          LDA     BCB_DIRTY,X    ; one whose write failed: only if all are
                CMP     #2
                BEQ     @next
                LDA     VD
                STA     VBEST
                LDA     VD + 1
                STA     VBEST + 1
                STX     VIDX
@next:          DEX
                BPL     @lru
                LDX     VIDX
@done:          RTS

; The 512 bytes at BPTR (W2 on the buffers) zeroed.
ZEROB:          LDA     #0
                TAY
@lo:            STA     (BPTR),Y
                INY
                BNE     @lo
                INC     BPTR + 1
@hi:            STA     (BPTR),Y
                INY
                BNE     @hi
                DEC     BPTR + 1
                RTS

; Buffer X written to its sector, X kept, carry clear. A failure is an
; error on its drive: for a read-only unit Read Only, the drive made so, the
; buffer dropped; else Disk I/O, the buffer kept changed and marked
; (BCB_DIRTY 2), so what needs it written fails again until it is. WBACK
; with WBM: bit 7 drop it on Disk I/O too (DIRPUT, RFLUSH), bit 6 no error
; raised: carry set, A its code, EDRV the drive (RFLUSH).
WBACKX:         STZ     WBM
WBACK:          LDA     #1
                JSR     DISKOP
                BEQ     @done
                PHA
                LDA     BCB_DRV,X
                DEC     A
                STA     EDRV
                PLA
                CMP     #2
                BEQ     @ro
                BIT     WBM
                BMI     @drop
                LDA     #2             ; kept, marked
                STA     BCB_DIRTY,X
                LDA     #ER_IO
                BRA     @error
@ro:            PHX
                LDA     EDRV
                JSR     DRVBIT
                ORA     ROVEC,X
                STA     ROVEC,X
                PLX
                LDA     #ER_RODISK
                BRA     @dropit
@drop:          LDA     #ER_IO
@dropit:        STZ     BCB_DRV,X
                STZ     BCB_DIRTY,X
@error:         BIT     WBM
                BVS     @back
                JMP     DERR
@back:          SEC
                RTS
@done:          STZ     BCB_DIRTY,X
                CLC
                RTS

; Buffer X read from its sector, X kept. A failure empties it: Disk I/O.
RDBCB:          LDA     #0
                JSR     DISKOP
                BEQ     @done
                LDA     BCB_DRV,X
                DEC     A
                STA     EDRV
                STZ     BCB_DRV,X
                LDA     #ER_IO
                JMP     DERR
@done:          RTS

; Buffer X to or from its sector through the BIOS (A = 0 READ, 1 WRITE):
; its drive selected if the BIOS has another, the track and sector, bank 0
; and its address, W2 on the buffers for the BIOS to find it there. A = the
; BIOS's result, Z set for 0; X kept.
DISKOP:         STA     DOPW
                STX     DOPX
                LDA     BCB_DRV,X
                CMP     BIOSDRV
                BEQ     @selected
                STA     BIOSDRV
                DEC     A
                LDX     #1             ; selected before: no units read
                JSR     B_SELDSK
                STX     VD
                ORA     VD
                BNE     @selected
                STZ     BIOSDRV        ; no such drive now: the disk's error
                LDX     DOPX
                LDA     #1
                RTS
@selected:      LDX     DOPX
                LDA     BCB_HI,X       ; the track: the sector / 16
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                PHA
                LDA     BCB_HI,X
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     VD
                LDA     BCB_LO,X
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     VD
                PLX
                JSR     B_SETTRK
                LDX     DOPX
                LDA     BCB_LO,X
                AND     #SPT_PHYS - 1
                LDX     #0
                JSR     B_SETSEC
                LDA     #BANK_SYS
                JSR     B_SETBNK
                LDA     DOPX
                ASL     A
                ORA     #$80
                TAX
                LDA     #0
                JSR     B_SETDMA
                LDA     #BCB_BLOCK
                STA     NB_WIN2
                LDA     DOPW
                BNE     @write
                JSR     B_READ
                BRA     @done
@write:         LDA     #0             ; CP/M's deblocking code: a plain write
                JSR     B_WRITE
@done:          LDX     DOPX
                CMP     #0
                RTS

; Every changed buffer of drive A - 1 (A = 0: of every drive) written back,
; carrying on past a failure: one that can't be is kept, marked (a read-only
; unit's dropped, as WBACK has it), and the first failure's code is in RERR
; and its drive in REDRV (0 none); carry set if there was one. No error is
; raised: 48 says it after (RDONE), CLOSEX decides what fails its close.
FLUSHD:         STZ     RERR
                LDX     #$40
                BRA     WFLUSH

; The same for 13 and 37, which log the drive out: one that can't be written
; is dropped, and RERR (the caller's 0 before) keeps the first failure.
RFLUSH:         LDX     #$C0
WFLUSH:         STX     WBM
                STA     FDRV
                LDX     #NBCB - 1
@buffer:        LDA     BCB_DIRTY,X
                BEQ     @next
                LDA     FDRV
                BEQ     @write
                CMP     BCB_DRV,X
                BNE     @next
@write:         JSR     WBACK
                BCC     @next
                LDY     RERR
                BNE     @next
                STA     RERR
                LDA     EDRV
                STA     REDRV
@next:          DEX
                BPL     @buffer
                LDA     RERR
                CMP     #1
                RTS

; 98's, at the warm boot: the marked buffers (a write that failed) dropped
; whose blocks 98 frees, those not in their drive's permanent vector (a
; file's written but not closed). One in a block the directory has (a record
; of a closed file written again in place) is kept, marked: the next 48 or
; close tries it again, and 13 and 37 drop it. A sector before the data area
; is in no block, and is kept; one of a drive not logged in is dropped.
PURGEM:         LDX     #NBCB - 1
@buffer:        LDA     BCB_DIRTY,X
                CMP     #2
                BNE     @next
                STX     PBX
                JSR     PERMB
                LDX     PBX
                BCS     @next
                STZ     BCB_DRV,X
                STZ     BCB_DIRTY,X
@next:          DEX
                BPL     @buffer
                RTS

; Carry set if buffer PBX's sector is in a block of its drive's permanent
; vector, or before the data area. CD its drive's.
PERMB:          LDA     BCB_DRV,X
                DEC     A
                JSR     DRVBIT
                AND     LOGVEC,X
                BEQ     @no            ; not logged in: no vector
                LDX     PBX
                LDA     BCB_DRV,X
                DEC     A
                JSR     DSLOT
                BCS     @no
                STA     CSLOT
                JSR     LOADCD
                LDX     PBX
                JSR     BCBBLK
                BCS     @yes           ; no block
                LDA     T2
                STA     BLK
                LDA     T3
                STA     BLK + 1
                JSR     ALVBIT
                INC     AP + 1         ; the permanent vector's byte
                INC     AP + 1
                LDA     (AP)
                AND     AMASK
                BEQ     @no
@yes:           SEC
                RTS
@no:            CLC
                RTS

; T2/T3 = buffer X's block on drive CD, (its sector - OFFS) / 2^(BSH - 2),
; carry clear; carry set if the sector is before the data area. X kept.
BCBBLK:         SEC
                LDA     BCB_LO,X
                SBC     OFFS
                STA     T2
                LDA     BCB_HI,X
                SBC     OFFS + 1
                STA     T3
                BCC     @none
                LDY     BSH
                DEY
                DEY
@shift:         LSR     T3
                ROR     T2
                DEY
                BNE     @shift
                CLC
                RTS
@none:          SEC
                RTS

; 13's and 37's end: 0, or RFLUSH's first failure as the error.
RDONE:          LDA     RERR
                BNE     @error
                JMP     RET0
@error:         LDA     REDRV
                STA     EDRV
                LDA     RERR
                JMP     DERR

; Every buffer of drive A - 1 (0: of every drive) emptied, not written.
PURGED:         STA     FDRV
                LDX     #NBCB - 1
@buffer:        LDA     FDRV
                BEQ     @drop
                CMP     BCB_DRV,X
                BNE     @next
@drop:          STZ     BCB_DRV,X
                STZ     BCB_DIRTY,X
@next:          DEX
                BPL     @buffer
                RTS

; ---------------------------------------------------------------------------
; The directory

; Entry DIDX of drive CDRV in its buffer: DPTR at it, W2 on the buffers,
; DBCB the buffer. Keeps T0-T5.
DIRENT:         LDA     DIDX + 1       ; the sector: OFFS + DIDX / 16
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     LBA
                LDA     DIDX
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     LBA
                CLC
                ADC     OFFS
                STA     LBA
                LDA     OFFS + 1
                ADC     #0
                STA     LBA + 1
                LDA     #0
                JSR     GETBUF
                STX     DBCB
                LDA     DIDX           ; DPTR: BPTR + (DIDX & 15) x 32
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     DPTR
                LDA     DIDX
                AND     #8
                CMP     #8
                LDA     BPTR + 1
                ADC     #0
                STA     DPTR + 1
                RTS

; The entry at DPTR changed: its sector written now, its hash entry made
; again. W2 on the buffers after.
DIRPUT:         LDX     DBCB
                LDA     #1
                STA     BCB_DIRTY,X
                LDA     #$80           ; a failure drops it: memory as the disk is
                STA     WBM
                JSR     WBACK
HASHPUT:        JSR     HASHE
                JSR     HPSET
                LDA     HBLK
                STA     NB_WIN2
                LDY     #0
                LDA     H0
                STA     (HP),Y
                INY
                LDA     HV
                STA     (HP),Y
                INY
                LDA     HV + 1
                STA     (HP),Y
                INY
                LDA     H3
                STA     (HP),Y
                LDA     #BCB_BLOCK
                STA     NB_WIN2
                RTS

; HP at DIDX's hash entry: HBASE + DIDX x 4.
HPSET:          LDA     DIDX
                ASL     A
                STA     HP
                LDA     DIDX + 1
                ROL     A
                ASL     HP
                ROL     A
                CLC
                ADC     HBASE
                STA     HP + 1
                RTS

; The hash entry of the entry at DPTR (W2 on the buffers): H0, HV, H3.
HASHE:          LDA     (DPTR)
                STA     H0
                CMP     #32
                BCS     @none          ; a label, an SFCB, free: its first byte only
                LDA     DPTR
                STA     NP
                LDA     DPTR + 1
                STA     NP + 1
                JSR     NHASH
                LDY     #FCB_S2
                LDA     (DPTR),Y
                AND     #7
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     H3
                LDY     #FCB_EX
                LDA     (DPTR),Y
                AND     #$1F
                ORA     H3
                STA     H3
                RTS
@none:          STZ     HV
                STZ     HV + 1
                STZ     H3
                RTS

; HV = the hash of the 11 name bytes at NP + 1, their low 7 bits.
NHASH:          STZ     HV
                STZ     HV + 1
                LDY     #FCB_NAME
@byte:          ASL     HV
                ROL     HV + 1
                BCC     @in
                INC     HV
@in:            LDA     (NP),Y
                AND     #$7F
                EOR     HV
                STA     HV
                INY
                CPY     #FCB_EX
                BNE     @byte
                RTS

; FIND's pattern from F: A the user, X the mode (SM_*); the name's low 7
; bits, EX and S2. PATDERIV adds '?' in the name or EX to the mode and
; works out what FIND compares the hash with.
SETPAT:         STA     SPAT
                STX     SMODE
                LDX     #FCB_NAME
@name:          LDA     FC,X
                AND     #$7F
                STA     SPAT,X
                INX
                CPX     #FCB_EX
                BNE     @name
                LDA     FC + FCB_EX
                STA     SPAT + 12
                LDA     FC + FCB_S2
                AND     #$3F
                STA     SPAT + 13
PATDERIV:       LDX     #FCB_NAME
@wild:          LDA     SPAT,X
                CMP     #'?'
                BEQ     @anyname
                INX
                CPX     #FCB_EX
                BNE     @wild
                BRA     @ex
@anyname:       LDA     SMODE
                ORA     #SM_ANYNAME
                STA     SMODE
@ex:            LDA     SPAT + 12
                CMP     #'?'
                BNE     @hash
                LDA     SMODE
                ORA     #SM_ANYEX
                STA     SMODE
@hash:          LDA     #<SPAT
                STA     NP
                LDA     #>SPAT
                STA     NP + 1
                JSR     NHASH
                LDA     HV
                STA     PHASH
                LDA     HV + 1
                STA     PHASH + 1
                LDA     SPAT + 13
                AND     #7
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     PEX3
                LDA     SPAT + 12
                AND     #$1F
                ORA     PEX3
                STA     PEX3
                LDA     EXMASK
                ORA     #$E0
                STA     MASK3
                RTS

; From entry DIDX (FINDNXT: the one after it), the first SPAT and SMODE
; match: carry clear, DIDX at it, DPTR its bytes (W2 on the buffers), DBCB
; its buffer. Carry set if none. Keeps T0-T5.
FINDNXT:        INC     DIDX
                BNE     FIND
                INC     DIDX + 1
FIND:           LDA     SMODE          ; how far: every entry, or files to HWM
                AND     #SM_ALL | SM_FREE
                BEQ     @hwm
                CLC
                LDA     DRM
                ADC     #1
                STA     LIM
                LDA     DRM + 1
                ADC     #0
                STA     LIM + 1
                BRA     @start
@hwm:           LDA     HWM
                STA     LIM
                LDA     HWM + 1
                STA     LIM + 1
@start:         JSR     HPSET
@loop:          LDA     DIDX
                CMP     LIM
                LDA     DIDX + 1
                SBC     LIM + 1
                BCC     @in
                RTS                    ; none: carry set
@in:            LDA     SMODE
                BMI     @candidate     ; SM_ALL
                LDX     HBLK
                STX     NB_WIN2
                AND     #SM_FREE
                BEQ     @user
                LDA     (HP)
                CMP     #DIR_FREE
                BEQ     @candidate
                BRA     @next
@user:          LDA     SMODE
                AND     #SM_XFCB
                BEQ     @exact
                LDA     (HP)
                AND     #$EF           ; user + 16 is the user's XFCB
                BRA     @isuser
@exact:         LDA     (HP)
@isuser:        CMP     SPAT
                BNE     @next
                LDA     SMODE
                AND     #SM_ANYNAME
                BNE     @extent
                LDY     #1
                LDA     (HP),Y
                CMP     PHASH
                BNE     @next
                INY
                LDA     (HP),Y
                CMP     PHASH + 1
                BNE     @next
@extent:        LDA     SMODE
                AND     #SM_ANYEX
                BNE     @candidate
                LDY     #3
                LDA     (HP),Y
                EOR     PEX3
                AND     MASK3
                BNE     @next
@candidate:     JSR     DIRENT
                LDA     SMODE
                AND     #SM_ALL | SM_FREE
                BNE     @found
                JSR     MATCH
                BEQ     @found
@next:          INC     DIDX
                BNE     @hp
                INC     DIDX + 1
@hp:            CLC
                LDA     HP
                ADC     #4
                STA     HP
                BCC     @loop
                INC     HP + 1
                BRA     @loop
@found:         CLC
                RTS

; Z set if the entry at DPTR is SPAT's, in full: the user (or its XFCB),
; each name byte but a '?', the extent's group (EX but its EXM bits, and
; S2) unless any extent.
MATCH:          LDA     (DPTR)
                TAX
                LDA     SMODE
                AND     #SM_XFCB
                BEQ     @user
                TXA
                AND     #$EF
                TAX
@user:          CPX     SPAT
                BNE     @done
                LDY     #FCB_NAME
@name:          LDA     SPAT,Y
                CMP     #'?'
                BEQ     @any
                EOR     (DPTR),Y
                AND     #$7F
                BNE     @done
@any:           INY
                CPY     #FCB_EX
                BNE     @name
                LDA     SMODE
                AND     #SM_ANYEX
                BNE     @yes
                LDA     (DPTR),Y       ; Y = FCB_EX
                EOR     SPAT + 12
                AND     EXMASK
                BNE     @done
                LDY     #FCB_S2
                LDA     (DPTR),Y
                CMP     SPAT + 13
@done:          RTS
@yes:           LDA     #0
                RTS

; The first free entry: as FIND.
FINDFREE:       LDA     #SM_FREE
                STA     SMODE
                STZ     DIDX
                STZ     DIDX + 1
                JMP     FIND

; A new entry for F's name and extent, of user A (bit 7 set: Make's extent
; 0, stamped), in the first free one: no blocks, RC 0, the attributes but
; f5'-f8', its SFCB slot made (INITSTAMP). DIDX, DPTR at it, carry clear;
; carry set if the directory is full.
MAKEENT:        STA     MKU
                JSR     FINDFREE
                BCC     @free
                RTS
@free:          LDA     MKU
                AND     #$0F
                STA     (DPTR)
                LDY     #FCB_NAME
@name:          LDA     FC,Y
                CPY     #FCB_F5
                BCC     @keep
                CPY     #FCB_T1
                BCS     @keep
                AND     #$7F           ; f5'-f8' are for the call, not the disk
@keep:          STA     (DPTR),Y
                INY
                CPY     #FCB_EX
                BNE     @name
                LDA     FC + FCB_EX
                AND     #$1F
                STA     (DPTR),Y
                INY
                LDA     #0
                STA     (DPTR),Y       ; S1
                INY
                LDA     FC + FCB_S2
                AND     #$3F
                STA     (DPTR),Y
                LDA     #0
                INY
@zero:          STA     (DPTR),Y       ; RC and the blocks
                INY
                CPY     #ENTRY_LEN
                BNE     @zero
                JSR     DIRPUT
                JSR     HWMUP
                JSR     INITSTAMP
                JSR     DIRENT
                CLC
                RTS

; Entry DIDX now used: past the high-water mark, the mark moves (and CD is
; saved). Keeps DIDX, DPTR and T0-T5.
HWMUP:          LDA     DIDX
                CMP     HWM
                LDA     DIDX + 1
                SBC     HWM + 1
                BCC     @done
                CLC
                LDA     DIDX
                ADC     #1
                STA     HWM
                LDA     DIDX + 1
                ADC     #0
                STA     HWM + 1
                JMP     SAVECD
@done:          RTS

; The 128-byte directory record holding the entry at DPTR, to the DMA
; address (17, 18).
DIRREC:         LDA     DPTR
                AND     #$80
                STA     BW1
                LDA     DPTR + 1
                STA     BW1 + 1
                LDA     SCB_DMA
                STA     BW2
                LDA     SCB_DMA + 1
                STA     BW2 + 1
                LDA     #128
                JMP     POUT

; ---------------------------------------------------------------------------
; Stamps

; The time now into stamp A (0 the first, creation or access; 4 the update)
; of entry DIDX's SFCB slot, if it has one. DIDX kept; DPTR moves.
STAMP:          STA     STW
                JSR     SFCBAT
                BCS     @none
                JSR     NOW
                LDA     STOFS
                CLC
                ADC     STW
                TAY
                JSR     PUTNOW
                JMP     SFCBPUT
@none:          RTS

; A made entry's SFCB slot, if it has one: 0 (no stamp made, no password:
; the Guide, 2.3.8 and 102), whatever the label asks; and for Make's extent
; 0 (MKU bit 7) the first stamp the time if the mode asks for creation or
; access stamps, the update if it asks for those.
INITSTAMP:      JSR     SFCBAT
                BCS     @none
                LDY     STOFS
                LDX     #SFCB_SLOT
                LDA     #0
@clear:         STA     (DPTR),Y
                INY
                DEX
                BNE     @clear
                BIT     MKU
                BPL     @put
                LDA     MODE
                AND     #MODE_CREATE | MODE_UPDATE | MODE_ACCESS
                BEQ     @put
                JSR     NOW
                LDA     MODE
                AND     #MODE_CREATE | MODE_ACCESS
                BEQ     @update
                LDY     STOFS
                JSR     PUTNOW
@update:        LDA     MODE
                AND     #MODE_UPDATE
                BEQ     @put
                LDA     STOFS
                CLC
                ADC     #4
                TAY
                JSR     PUTNOW
@put:           JMP     SFCBPUT
@none:          RTS

; The update stamp, if the label asks for them, in the slot of the file's
; extent-zero entry (user A, F's name, EX and S2 0): with carry clear the
; entry at DIDX/DPTR if it is that one, else (or carry set) looked for.
; DIDX as it was; DPTR and SPAT changed.
STAMPU:         TAX
                LDA     MODE
                AND     #MODE_UPDATE
                BEQ     @done
                LDA     DIDX
                STA     UDID
                LDA     DIDX + 1
                STA     UDID + 1
                BCS     @find
                LDY     #FCB_S2
                LDA     (DPTR),Y
                AND     #$3F
                BNE     @find
                LDY     #FCB_EX
                LDA     (DPTR),Y
                AND     EXMASK
                BEQ     @stamp
@find:          TXA
                LDX     #0
                JSR     SETPAT
                STZ     SPAT + 12
                STZ     SPAT + 13
                JSR     PATDERIV
                STZ     DIDX
                STZ     DIDX + 1
                JSR     FIND
                BCS     @back
@stamp:         LDA     #4
                JSR     STAMP
@back:          LDA     UDID
                STA     DIDX
                LDA     UDID + 1
                STA     DIDX + 1
@done:          RTS

; The SCB's time from the clock (BIOS TIME), W2 kept.
NOW:            LDA     #0
                JMP     B_TIME

; The SCB's days, hour and minute to (DPTR),Y on.
PUTNOW:         LDX     #0
@byte:          LDA     SCB_DATE,X
                STA     (DPTR),Y
                INY
                INX
                CPX     #4
                BNE     @byte
                RTS

; DPTR at entry DIDX's SFCB, STOFS its slot, carry clear; carry set, and
; DIDX as it was, if it has none (entry DIDX | 3 isn't one). SFCBPUT writes
; it and puts DIDX back.
SFCBAT:         LDA     DIDX
                STA     SDID
                LDA     DIDX + 1
                STA     SDID + 1
                LDA     DIDX
                AND     #3
                STA     STOFS          ; 1 + slot x 10
                ASL     A
                ASL     A
                ADC     STOFS
                ASL     A
                INC     A
                STA     STOFS
                LDA     DIDX
                ORA     #3
                STA     DIDX
                JSR     DIRENT
                LDA     (DPTR)
                CMP     #DIR_SFCB
                BEQ     @ok
                JSR     SFREST
                SEC
                RTS
@ok:            CLC
                RTS
SFCBPUT:        JSR     DIRPUT
SFREST:         LDA     SDID
                STA     DIDX
                LDA     SDID + 1
                STA     DIDX + 1
                RTS

; ---------------------------------------------------------------------------
; Allocation vectors

; AP at block BLK's byte in the current vector, AMASK its bit.
ALVBIT:         LDA     BLK
                AND     #7
                TAX
                LDA     BITS,X
                STA     AMASK
                LDA     BLK + 1
                STA     AP + 1
                LDA     BLK
                LSR     AP + 1
                ROR     A
                LSR     AP + 1
                ROR     A
                LSR     AP + 1
                ROR     A
                CLC
                ADC     ALVP
                STA     AP
                LDA     AP + 1
                ADC     ALVP + 1
                STA     AP + 1
                RTS
BITS:           .byte   $80, $40, $20, $10, $08, $04, $02, $01

; Block BLK in use: X = 0 in both vectors, 1 in the current one only.
ALVSET:         PHX
                JSR     ALVBIT
                LDA     (AP)
                ORA     AMASK
                STA     (AP)
                PLX
                BNE     @done
                INC     AP + 1
                INC     AP + 1
                LDA     (AP)
                ORA     AMASK
                STA     (AP)
@done:          RTS

; Block BLK free in both vectors.
ALVCLR:         JSR     ALVBIT
                LDA     AMASK
                EOR     #$FF
                STA     AMASK
                LDA     (AP)
                AND     AMASK
                STA     (AP)
                INC     AP + 1
                INC     AP + 1
                LDA     (AP)
                AND     AMASK
                STA     (AP)
                RTS

; BLK = the first free block in the current vector, carry clear; carry set
; if the drive is full.
ALVFREE:        LDA     ALVP
                STA     AP
                LDA     ALVP + 1
                STA     AP + 1
                LDA     DSM + 1        ; NBYTES: the vector's bytes, DSM / 8 + 1
                STA     NBYTES + 1
                LDA     DSM
                LSR     NBYTES + 1
                ROR     A
                LSR     NBYTES + 1
                ROR     A
                LSR     NBYTES + 1
                ROR     A
                CLC
                ADC     #1
                STA     NBYTES
                BCC     @count
                INC     NBYTES + 1
@count:         STZ     BLK
                STZ     BLK + 1
@byte:          LDA     (AP)
                CMP     #$FF
                BNE     @bit
                CLC
                LDA     BLK
                ADC     #8
                STA     BLK
                BCC     @inc
                INC     BLK + 1
@inc:           INC     AP
                BNE     @left
                INC     AP + 1
@left:          LDA     NBYTES
                BNE     @dec
                DEC     NBYTES + 1
@dec:           DEC     NBYTES
                LDA     NBYTES
                ORA     NBYTES + 1
                BNE     @byte
                SEC
                RTS
@bit:           LDX     #0
@find:          ASL     A
                BCC     @got
                INX
                BRA     @find
@got:           TXA
                ORA     BLK
                STA     BLK
                LDA     DSM            ; past DSM, in the last byte: full
                CMP     BLK
                LDA     DSM + 1
                SBC     BLK + 1
                BCC     @full
                CLC
                RTS
@full:          SEC
                RTS

; BLK = the entry at DPTR's pointer Y (0-15, or 0-7 when they are words).
EPTR:           BIT     BIG
                BMI     @word
                TYA
                CLC
                ADC     #FCB_D
                TAY
                LDA     (DPTR),Y
                STA     BLK
                STZ     BLK + 1
                RTS
@word:          TYA
                ASL     A
                ADC     #FCB_D
                TAY
                LDA     (DPTR),Y
                STA     BLK
                INY
                LDA     (DPTR),Y
                STA     BLK + 1
                RTS

; The blocks of the entry at DPTR in both vectors (MARKE), or free in both
; (FREEE): each pointer but 0 and those past DSM.
MARKE:          LDA     #0
                BRA     EBLOCKS
FREEE:          LDA     #1
EBLOCKS:        PHA
                STZ     MK
@pointer:       LDY     MK
                JSR     EPTR
                LDA     BLK
                ORA     BLK + 1
                BEQ     @next
                LDA     DSM
                CMP     BLK
                LDA     DSM + 1
                SBC     BLK + 1
                BCC     @next          ; past the drive's end: not a block
                PLA
                PHA
                BNE     @free
                LDX     #0
                JSR     ALVSET
                BRA     @next
@free:          JSR     ALVCLR
@next:          INC     MK
                LDA     #16            ; 16 byte pointers, or 8 words
                BIT     BIG
                BPL     @count
                LDA     #8
@count:         CMP     MK
                BNE     @pointer
                PLA
                RTS

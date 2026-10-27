; A read-only CP/M 3 file reader for OODOS's SD drives (hw/oodos.md, "The
; system, phase 2"). OOLDR links it to find and read OODOS.SYS, and the
; stand-in CCP in OODOS.SYS to run NAME.COM from A:: one reader for both.
; Written from the CP/M 3 manuals' description of the directory.
;
; It talks to the SD card through its registers, a byte at a time through
; DATA (no DMA), so it works whatever the windows hold, and has no fixed
; addresses. The caller's linker config places CODE and these:
;
;   CRZP   zero page, 4 bytes
;   CRBSS  data, about 1 KB: the sector buffer and the file's entries
;
; A drive: an SD unit (1-7) of 16,384 sectors is oodos-8m, of 65,536
; oodos-32m (oodos/diskdefs): 512-byte sectors, no boot tracks, so the
; directory starts at the unit's LBA 0. Both have over 255 blocks, so an
; entry has 8 block pointers of 2 bytes, and 8 directory blocks.
;
;   CR_INIT  A = the unit. Its size from IDENTIFY's units table picks the
;            DPB: CR_KIND is 0 for oodos-8m, 1 for oodos-32m.
;   CR_FIND  A/X = the name: 11 bytes, 8 + 3, upper case, space padded.
;            Finds the file in user 0 and keeps its entries in extent order;
;            CR_RECORDS is then its length in 128-byte records, and the
;            next CR_READ reads record 0.
;   CR_SEEK  A/X = a record: the next CR_READ reads that one.
;   CR_READ  A/X = a 128-byte buffer (a CPU address): the next record into
;            it. At the end of the file, carry set and A = CRE_EOF.
;
; Each returns carry clear, or carry set and an error in A (CRE_*,
; system.inc; with CRE_SD the card's ERROR is in CR_SDERR). A card that
; stays BUSY about a second, before a command or in one, is CRE_TIMEOUT:
; the command is abandoned (CTRL's RESET; units stay mounted). A, X and Y
; are not kept. D must be clear.
;
; Only entries in user 0 (byte 0 is 0) are looked at, so free and deleted
; ones ($E5), the directory label ($20), SFCBs ($21), XFCBs (16-31) and other
; users' files are skipped. A name's attribute bits (bit 7) are ignored. A
; file may have up to 16 entries (512 KB on oodos-8m, 1 MB on oodos-32m);
; every entry but the last must be full, as CP/M 3 writes them.

.setcpu "65C02"
.include "backplane.inc"
.include "system.inc"

.export CR_INIT, CR_FIND, CR_SEEK, CR_READ
.export CR_KIND, CR_RECORDS, CR_SDERR

MAX_ENTRIES     = 16
DIR_BLOCKS      = 8                    ; both formats: 32 KB of 4 KB, 64 KB of 8 KB
UNITS           = $40                  ; IDENTIFY: 32 bytes a unit from 1, sectors then flags
UNIT_MOUNTED    = $01

.segment "CRZP": zeropage
ptr:            .res    2              ; the caller's name or buffer
src:            .res    2              ; an entry or a record in sbuf

.segment "CRBSS"
CR_KIND:        .res    1
CR_SDERR:       .res    1
CR_RECORDS:     .res    2
unit:           .res    1
dpb:                                   ; from DPBS, by CR_KIND:
rbsh:           .res    1              ;   log2 of the records in a block
spbsh:          .res    1              ;   log2 of the sectors in a block
exm:            .res    1
dirsecs:        .res    1              ;   the directory's sectors
dsm:            .res    2
DPB_LEN         = * - dpb
rec:            .res    2              ; the next record
tmp:            .res    2
lba:            .res    2              ; the sector in sbuf, if valid
valid:          .res    1
dsec:           .res    1              ; CR_FIND's directory sector
tmo:            .res    3              ; a wait's passes left, to about a second
name:           .res    11
last:           .res    1              ; the last entry's index; $FF: none yet
found:          .res    MAX_ENTRIES    ; nonzero: that entry is in blocks
recs:           .res    MAX_ENTRIES * 2 ; the records each entry holds
blocks:         .res    MAX_ENTRIES * 16 ; each entry's 8 block pointers
sbuf:           .res    512

.segment "RODATA"
; rbsh, spbsh, exm, dirsecs, dsm: oodos-8m (4 KB blocks), oodos-32m (8 KB)
DPBS:           .byte   5, 3, 1, 64
                .word   2047
                .byte   6, 4, 3, 128
                .word   4095

.segment "CODE"

CR_INIT:        STA     unit
                STZ     valid
                STZ     rec
                STZ     rec+1
                STZ     CR_RECORDS
                STZ     CR_RECORDS+1
                DEC
                CMP     #7
                BCS     @unit          ; 0 or over 7
                JSR     idle
                BCS     @out
                STZ     SD_CTRL
                LDA     #SD_IDENTIFY
                STA     SD_CMD
                JSR     take
                BCS     @out
                LDA     unit           ; X = (unit - 1) x 32
                DEC
                ASL
                ASL
                ASL
                ASL
                ASL
                TAX
                LDA     sbuf + UNITS + 4,X
                AND     #UNIT_MOUNTED
                BEQ     @unit
                LDA     sbuf + UNITS,X  ; 16,384 is $00004000, 65,536 $00010000
                ORA     sbuf + UNITS + 3,X
                BNE     @size
                LDY     #0
                LDA     sbuf + UNITS + 1,X
                CMP     #$40
                BNE     @big
                LDA     sbuf + UNITS + 2,X
                BEQ     @kind
                BRA     @size
@big:           CMP     #0
                BNE     @size
                LDA     sbuf + UNITS + 2,X
                CMP     #1
                BNE     @size
                LDY     #DPB_LEN
@kind:          TYA
                BEQ     @8m
                LDA     #1
@8m:            STA     CR_KIND
                LDX     #0
@copy:          LDA     DPBS,Y
                STA     dpb,X
                INY
                INX
                CPX     #DPB_LEN
                BNE     @copy
                CLC
@out:           RTS
@unit:          LDA     #CRE_UNIT
                SEC
                RTS
@size:          LDA     #CRE_SIZE
                SEC
                RTS

CR_FIND:        STA     ptr
                STX     ptr+1
                LDY     #10
@name:          LDA     (ptr),Y
                STA     name,Y
                DEY
                BPL     @name
                LDA     #$FF
                STA     last
                LDX     #MAX_ENTRIES - 1
@clear:         STZ     found,X
                DEX
                BPL     @clear
                STZ     dsec
@sector:        LDA     dsec
                LDX     #0
                JSR     get
                BCC     @got
                RTS
@got:           LDA     #<sbuf
                STA     src
                LDA     #>sbuf
                STA     src+1
@entry:         JSR     entry
                BCS     @fail
                CLC
                LDA     src
                ADC     #32
                STA     src
                BCC     @same
                INC     src+1
@same:          CMP     #<(sbuf + 512)  ; past the sector's last entry?
                BNE     @entry
                LDA     src+1
                CMP     #>(sbuf + 512)
                BNE     @entry
                INC     dsec
                LDA     dsec
                CMP     dirsecs
                BNE     @sector

                LDA     last           ; every entry to the last, the rest full
                CMP     #$FF
                BEQ     @none
                LDX     #0
@check:         LDA     found,X
                BEQ     @bad
                CPX     last
                BEQ     @length
                TXA
                ASL
                TAY
                LDA     recs,Y         ; full: (EXM + 1) x 128, a whole number of pages
                BNE     @bad
                LDA     exm
                INC
                LSR
                CMP     recs+1,Y
                BNE     @bad
                INX
                BRA     @check
@length:        LDA     exm            ; CR_RECORDS = last x (EXM + 1) x 128 + the last's
                INC
                LSR
                STA     tmp+1          ; (EXM + 1) x 128, in pages
                LDA     last
                ASL
                TAY
                LDA     recs,Y
                STA     CR_RECORDS
                LDA     recs+1,Y
                LDX     last
                BEQ     @set
@pages:         CLC
                ADC     tmp+1
                DEX
                BNE     @pages
@set:           STA     CR_RECORDS+1
                STZ     rec
                STZ     rec+1
                CLC
                RTS
@none:          LDA     #CRE_NOFILE
                SEC
@fail:          RTS
@bad:           LDA     #CRE_BAD
                SEC
                RTS

; The directory entry at src: kept if it is the file's. Carry set with an
; error if it can't be.
not_it:         CLC
                RTS
too_big:        LDA     #CRE_BIG
                SEC
                RTS
damaged:        LDA     #CRE_BAD
                SEC
                RTS
entry:          LDA     (src)
                BNE     not_it         ; not user 0's
                LDY     #1
@cmp:           LDA     (src),Y
                AND     #$7F
                CMP     name-1,Y
                BNE     not_it
                INY
                CPY     #12
                BNE     @cmp
                LDY     #14            ; S2, then EX: the extent is S2 x 32 + EX
                LDA     (src),Y
                CMP     #2
                BCS     too_big           ; extent 64 or more: past entry 15 either way
                LSR
                ROR
                ROR
                ROR
                STA     tmp+1          ; S2 x 32
                LDY     #12
                LDA     (src),Y
                CMP     #32
                BCS     damaged
                ORA     tmp+1
                PHA
                AND     exm
                STA     tmp+1          ; EX & EXM: the logical extents before the last
                PLA
                LDX     exm            ; the entry: the extent / (EXM + 1)
@shift:         LSR
                PHA
                TXA
                LSR
                TAX
                PLA
                CPX     #0
                BNE     @shift
                CMP     #MAX_ENTRIES
                BCS     too_big
                TAX
                LDA     found,X
                BNE     damaged           ; twice
                INC     found,X
                LDA     last           ; the last so far: none ($FF), or below X
                CMP     #$FF
                BEQ     @last
                CPX     last
                BCC     @rc
@last:          STX     last
@rc:            LDY     #15            ; its records: (EX & EXM) x 128 + RC
                LDA     (src),Y
                BMI     @full
                STA     tmp
                BRA     @recs
@full:          CMP     #$80           ; 128 is the most
                BNE     damaged
                STA     tmp
@recs:          TXA
                ASL
                TAY
                LDA     tmp+1
                LSR                    ; x 128: its low bit to bit 7, the rest a page
                STA     recs+1,Y
                LDA     #0
                ROR
                CLC
                ADC     tmp
                STA     recs,Y
                BCC     @ptrs
                LDA     recs+1,Y
                INC
                STA     recs+1,Y
@ptrs:          TXA                    ; its block pointers, to blocks + X x 16
                ASL
                ASL
                ASL
                ASL
                TAX
                LDY     #16
@ptr:           LDA     (src),Y
                STA     blocks,X
                INX
                INY
                CPY     #32
                BNE     @ptr
                CLC
                RTS

CR_SEEK:        STA     rec
                STX     rec+1
                CLC
                RTS

CR_READ:        STA     ptr
                STX     ptr+1
                LDA     rec
                CMP     CR_RECORDS
                LDA     rec+1
                SBC     CR_RECORDS+1
                BCC     @in
                LDA     #CRE_EOF
                RTS                    ; carry set
@in:            LDA     rec            ; the file's block: rec >> rbsh, under 128
                STA     tmp
                LDA     rec+1
                STA     tmp+1
                LDX     rbsh
@block:         LSR     tmp+1
                ROR     tmp
                DEX
                BNE     @block
                LDA     tmp            ; x 2: entry x 16 + pointer x 2
                ASL
                TAX
                LDA     blocks,X
                STA     tmp
                LDA     blocks+1,X
                STA     tmp+1
                BNE     @high          ; under DIR_BLOCKS: none, or the directory's
                LDA     tmp
                CMP     #DIR_BLOCKS
                BCC     @bad
@high:          LDA     dsm            ; over DSM: off the drive
                CMP     tmp
                LDA     dsm+1
                SBC     tmp+1
                BCC     @bad
                LDX     spbsh          ; its first sector
@lba:           ASL     tmp
                ROL     tmp+1
                DEX
                BNE     @lba
                LDA     #1             ; and the record's sector in it: (rec mod
                LDX     rbsh           ; records a block) / 4
@mask:          ASL
                DEX
                BNE     @mask
                DEC
                AND     rec
                LSR
                LSR
                ORA     tmp
                LDX     tmp+1
                JSR     get
                BCS     @out
                LDA     rec            ; the record in it: sbuf + (rec & 3) x 128
                AND     #3
                LSR
                TAX
                LDA     #0
                ROR
                STA     src
                TXA
                CLC
                ADC     #>sbuf
                TAX
                LDA     src
                CLC
                ADC     #<sbuf
                STA     src
                BCC     @page
                INX
@page:          STX     src+1
                LDY     #127
@copy:          LDA     (src),Y
                STA     (ptr),Y
                DEY
                BPL     @copy
                INC     rec
                BNE     @done
                INC     rec+1
@done:          CLC
@out:           RTS
@bad:           LDA     #CRE_BAD
                SEC
                RTS

; Wait for a command to end, before the next: one written while BUSY is
; ignored. Its ERR is the caller's business, not ours. Carry set with
; CRE_TIMEOUT after about a second.
idle:           JSR     tstart
@wait:          LDA     SD_STATUS
                BIT     #SD_BUSY
                BEQ     @idle
                JSR     tick
                BCC     @wait
                RTS
@idle:          CLC
                RTS

; A wait's count: tick's passes, 4 x 65,536 of about 34 cycles, a second.
tstart:         STZ     tmo
                STZ     tmo+1
                LDA     #4
                STA     tmo+2
                RTS
; One pass of a wait: carry set, the command abandoned and A = CRE_TIMEOUT,
; once they're all gone. Keeps X and Y.
tick:           DEC     tmo
                BNE     @more
                DEC     tmo+1
                BNE     @more
                DEC     tmo+2
                BNE     @more
                LDA     #SD_CTRL_RESET
                STA     SD_CTRL
                STZ     SD_CTRL
                STZ     valid
                LDA     #CRE_TIMEOUT
                SEC
                RTS
@more:          CLC
                RTS

; Sector A/X of the unit into sbuf, unless it's there already. Carry set
; with CRE_SD if the card fails it.
get:            BIT     valid
                BPL     @read
                CMP     lba
                BNE     @read
                CPX     lba+1
                BNE     @read
                CLC
                RTS
@read:          STA     lba
                STX     lba+1
                STZ     valid
                JSR     idle
                BCS     @out
                STZ     SD_CTRL        ; DATA by the CPU, not DRQ0
                LDA     unit
                STA     SD_UNIT
                LDA     lba
                STA     SD_LBA
                LDA     lba+1
                STA     SD_LBA + 1
                STZ     SD_LBA + 2
                STZ     SD_LBA + 3
                LDA     #1
                STA     SD_COUNT
                STZ     SD_COUNT + 1
                LDA     #SD_READ
                STA     SD_CMD
                JSR     take
                BCS     @out
                LDA     #$80
                STA     valid
@out:           RTS

; The command's sector out of DATA into sbuf, then its end. Carry set with
; CRE_SD if it fails, CRE_TIMEOUT if it takes over about a second.
take:           JSR     tstart
@wait:          LDA     SD_STATUS
                BIT     #SD_DATA_READY
                BNE     @read
                BIT     #SD_BUSY
                BEQ     failed
                JSR     tick
                BCC     @wait
                RTS
@read:          LDY     #0
@lo:            LDA     SD_DATA
                STA     sbuf,Y
                INY
                BNE     @lo
@hi:            LDA     SD_DATA
                STA     sbuf + 256,Y
                INY
                BNE     @hi
                JSR     tstart
@end:           LDA     SD_STATUS      ; then the command's end
                BIT     #SD_BUSY
                BEQ     @ended
                JSR     tick
                BCC     @end
                RTS
@ended:         BIT     #SD_ERR
                BNE     failed
                CLC
                RTS
failed:         LDA     SD_ERROR
                STA     CR_SDERR
                STZ     valid
                LDA     #CRE_SD
                SEC
                RTS

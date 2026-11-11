; OODOS's BDOS, its memory functions (hw/oodos.md, "The BDOS, phase 3",
; phase 3c): CP/M-86's 51-58 and OODOS's 73 and 74. Block 60, with the rest
; of the BDOS. Where hw/oodos.md and CP/M-86 are silent the choice is ours,
; and says so there.
;
;   51  F_DMASEG    A/X = the DMA segment, a paragraph: the records of 17,
;                   18, 20, 21, 33, 34 and 40 go to and from physical memory
;                   at segment x 16 + the DMA address. $FFFF (the default,
;                   set by every warm boot) is none: the DMA address is the
;                   program's CPU address. A = X = 0
;   52  F_DMAGET    A/X = 4 bytes it fills: the DMA address (+0-1) and the
;                   segment (+2-3). A = X = 0
;   53  MC_MAX      the MCB at A/X (+0-1 M_BASE, a paragraph; +2-3 M_LENGTH,
;                   paragraphs; +4 M_EXT): the largest run of free blocks, at
;                   most M_LENGTH rounded up to whole blocks, allocated, as
;                   CP/M-86's: its base and length, M_EXT 1 if a free block
;                   is left outside it
;   54  MC_ABSMAX   the same, the run from M_BASE
;   55  MC_ALLOC    M_LENGTH allocated, the lowest run that has it: M_BASE
;   56  MC_ABSALLOC M_LENGTH allocated at M_BASE, all of it free
;   57  MC_FREE     M_EXT $FF: everything freed (58); else M_BASE an
;                   allocation's base: all of it freed; or a later block of
;                   one, M_LENGTH reaching its end: freed from there on, its
;                   head kept
;   58  MC_ALLFREE  every allocation freed
;   73  O_PHYSADDR  A/X = 4 bytes: a CPU address (+0-1) in; out the offset
;                   (+0-1, 0-15) and the paragraph (+2-3) of its physical
;                   address, through the program's windows. A = X = 0
;   74  O_MOUNT     A/X = the drive (+0, 1-6: B:-G:), the flags (+1, bit 0
;                   read-only) and the path (+2, to its NUL, as DRIVES.TXT
;                   has it; empty: unmount). A = 0, or A = $FF and X: 1 the
;                   card or a write-back failed, 4 not B:-G:, $10 the path,
;                   $11 not found, $12 the image's size (16,384, 65,536 or
;                   280 sectors: apple-do, phase 6b), $13 another drive's
;                   file
;
; Memory is handed out in 16 KB blocks, 1,024 paragraphs, from banks 1-5
; (blocks 4-23): a length is rounded up to whole blocks, a base is a block's
; first paragraph. AMAP has a byte a block: 0 free, $81 an allocation's
; first, 1 a later one. The running program owns them all: the warm boot
; (common.s's WARM) frees them, and sets the segment back to $FFFF. The
; MCB's results: A = 0 done, A = $FF and X 0 no memory (or a base or length
; that can't be), A = X = $FF an MCB in the I/O hole (PGET), nothing
; written. Every block of the program's memory is read before any is
; written, so a refused one leaves them all as they were.

.setcpu "65C02"
.include "bdos.inc"
.import SDMNT, UNITSAV
.import RAWF, DRVAPL, MNTGEN           ; bios.s's: function 76

FIRST_BLOCK     = 4                    ; bank 1's first, AMAP's 0
MAP_FIRST       = $81
MAP_MORE        = $01
ER_CARD         = 1                    ; 74's X
ER_DRIVE        = 4
ER_PATH         = $10
ER_NOTFOUND     = $11
ER_SIZE         = $12
ER_SAMEFILE     = $13
MAX_PATH        = 509                  ; the MOUNT sector's room, after the unit and flags, less the NUL
SD_ERR_UNIT     = 6                    ; the card's ERROR: unit not mounted
SD_ERR_NOTFOUND = 8
DOP_UNMOUNT     = 2                    ; bios.s's SDMNT
DOP_MOUNT       = 3
UNIT_LEN        = 32                   ; IDENTIFY's units table (bios.s's UNITSAV)
UNIT_NAME       = 8
UNIT_NAME_LEN   = 24

.zeropage
SG:             .res    3              ; a physical address (DSEG, MRANGE)
SGE:            .res    3              ; and the last of its bytes
SGB:            .res    1
MP:             .res    2              ; 74: the path's next byte, in SECBUF

.segment "SYSBSS"
RBLK:           .res    6              ; 76's block: the drive, track, sector, op, address
RRO:            .res    1              ; 76: the drive's read-only bit
RGEN:           .res    SD_DRIVES      ; 76: each SD drive's mount count at the program's first call
RSEEN:          .res    1              ; and the drives it has: the warm boot clears it
DMASEG:         .res    2              ; 51's, $FFFF none
AMAP:           .res    AMAP_LEN
MBLK:           .res    5              ; the program's MCB or block, as read
MNEED:          .res    1              ; the blocks asked for
MIDX:           .res    1              ; a block, AMAP's index
MRUN:           .res    1              ; a run's length
MBEST:          .res    1
MLEN:           .res    2              ; 74: the path's length so far
MDRV:           .res    1              ; 74's drive
NLEN:           .res    1              ; a name's length, to 3
DOTS:           .res    1              ; bit 7: the name is all dots

.segment "BDOS"

; ---------------------------------------------------------------------------
; The DMA segment

F_DMASEG:       LDA     PA
                STA     DMASEG
                LDA     PX
                STA     DMASEG + 1
                JMP     RET0

F_DMAGET:       LDX     #4
                JSR     PREAD
                LDA     SCB_DMA
                JSR     PPUT
                LDA     SCB_DMA + 1
                JSR     PPUT
                LDA     DMASEG
                JSR     PPUT
                LDA     DMASEG + 1
                JSR     PPUT
                JMP     RET0

; Carry clear, A kept, if there is no DMA segment: the DMA address at zero
; page X is the program's CPU address. With one: the word at X becomes the
; low 16 bits of segment x 16 + it, SG + 2 its bank for the BIOS's XMOVE
; ($80 + A16-A19), carry set; a record there (128 bytes) that a segment may
; not reach (SEGOK), or that runs past $FFFF, ends the function: A = X = $FF.
DSEG:           PHA
                LDA     DMASEG
                AND     DMASEG + 1
                CMP     #$FF
                BNE     @seg
                PLA
                CLC
                RTS
@seg:           LDA     0,X            ; its last byte past $FFFF: refused, as without one
                CLC
                ADC     #127
                LDA     1,X
                ADC     #0
                BCS     @no
                JSR     SEGADR
                LDA     #127
                LDY     #0
                JSR     SEGOK
                BCS     @no
                LDA     SG
                STA     0,X
                LDA     SG + 1
                STA     1,X
                LDA     SG + 2
                ORA     #$80
                STA     SG + 2
                PLA
                SEC
                RTS
@no:            JMP     ABORT

; SG = the segment x 16 + the word at zero page X. Keeps X.
SEGADR:         LDA     DMASEG + 1
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                STA     SG + 2
                LDA     DMASEG + 1
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     SG + 1
                LDA     DMASEG
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     SG + 1
                STA     SG + 1
                LDA     DMASEG
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                CLC
                ADC     0,X
                STA     SG
                LDA     SG + 1
                ADC     1,X
                STA     SG + 1
                BCC     @done
                INC     SG + 2
@done:          RTS

; Carry set if SG to SG + A/Y (at most $7FFF on: 49 may set a multi-sector
; count of 255, three blocks) isn't all memory a segment may reach: past
; $FFFFF, or a block of OODOS's own (BLKOK), each block from the first to
; the last checked. Keeps X.
SEGOK:          CLC
                ADC     SG
                STA     SGE
                TYA
                ADC     SG + 1
                STA     SGE + 1
                LDA     SG + 2
                ADC     #0
                STA     SGE + 2
                CMP     #$10
                BCS     @no            ; past $FFFFF
                PHX
                LDX     #SGE
                JSR     BLKOF
                STA     SGE            ; the last block (SGE's done with)
                LDX     #SG
                JSR     BLKOF          ; the first
@block:         JSR     BLKOK          ; (keeps A)
                BCS     @out
                CMP     SGE
                INC     A
                BCC     @block         ; short of the last
                CLC
@out:           PLX
                RTS
@no:            SEC
                RTS

; Carry set if SG (its A16-A19 in SG + 2's low nibble: DOUT's has $80 in its
; top) to SG + A/Y meets an aperture window that shows OODOS's card memory,
; which no program may write (L4's question 2, revision 2: function 83's
; destinations, and a record read through a DMA segment): slot 0's,
; $80000-$83FFF (card bank 0: the console's units 0-12 and the rest of
; OODOS's first 16 KB), and slots 2 and 3's, $88000-$8FFFF (card banks 2 and
; 3 from the warm boot on, but inside DM_VCOPY, which borrows them). Only
; slot 1's window, $84000-$87FFF, is the program's: the warm boot puts slot 1
; on its first card bank (vmem.inc's VM_PROG_BANK) and OOLIB keeps it on the
; program's. SEGOK has checked the range's end. Keeps X.
OSWIN:          CLC                    ; the end's high byte and A16-A19: SGE + 1, SGE + 2
                ADC     SG
                TYA
                ADC     SG + 1
                STA     SGE + 1
                LDA     SG + 2
                AND     #$0F
                ADC     #0
                STA     SGE + 2
                CMP     #^APER_WIN
                BCC     @clear         ; it ends below $80000
                LDA     SG + 2
                AND     #$0F
                CMP     #^APER_WIN
                BCC     @meets         ; from below $80000 into slot 0's window
                BNE     @clear         ; from bank 9 on
                LDA     SG + 1
                CMP     #>SLOT1_WIN
                BCC     @meets         ; from slot 0's window
                LDA     SGE + 2
                CMP     #^APER_WIN
                BNE     @meets         ; on past bank 8, through slots 2 and 3's
                LDA     SGE + 1
                CMP     #>SLOT1_END
                BCS     @meets         ; into slot 2's
@clear:         CLC
                RTS
@meets:         SEC
                RTS
APER_WIN        = $80000               ; the aperture, bank 8: slot n's window at + n x $4000
SLOT1_WIN       = $84000               ; slot 1's, the program's
SLOT1_END       = $88000               ; past it: slots 2 and 3's
.assert ^APER_WIN = 8 && (APER_WIN & $FFFF) = 0, error, "OSWIN compares the bank alone for the aperture's start"
.assert ^SLOT1_WIN = 8 && <SLOT1_WIN = 0 && ^SLOT1_END = 8 && <SLOT1_END = 0, error, "OSWIN compares slot 1's window by its high bytes in bank 8"

; A = the block (A14-A19) of the physical address at zero page X.
BLKOF:          LDA     1,X
                ROL     A
                ROL     A
                ROL     A
                AND     #3
                STA     SGB
                LDA     2,X
                ASL     A
                ASL     A
                ORA     SGB
                RTS

; Carry set if block A is OODOS's (hw/oodos.md, "Banking"): banks 6-7 (24-
; 31: the hash, the buffers, the system block), C-D (48-55: M:) and 60-63
; (the BDOS, the BIOS, block 62, the ROM BIOS's copy). Ours: any other RAM
; is the program's to name.
BLKOK:          CMP     #60
                BCS     @no
                CMP     #24
                BCC     @yes
                CMP     #32
                BCC     @no
                CMP     #48
                BCC     @yes
                CMP     #56
                BCC     @no
@yes:           CLC
                RTS
@no:            SEC
                RTS

; ---------------------------------------------------------------------------
; Memory: CP/M-86's MCB

; X bytes of the program's at PP (the parameter) into MBLK, then PP back at
; the parameter: an address in the I/O hole ends the function (PGET) before
; anything is written.
PREAD:          STX     MRUN
                LDX     #0
@byte:          JSR     PGET
                STA     MBLK,X
                INX
                CPX     MRUN
                BNE     @byte
PBACK:          LDA     PA
                STA     PP
                LDA     PX
                STA     PP + 1
                RTS

; MBLK's 5 bytes back to the program's MCB.
MCBOUT:         JSR     PBACK
                LDX     #0
@byte:          LDA     MBLK,X
                JSR     PPUT
                INX
                CPX     #5
                BNE     @byte
                JMP     RET0

; MNEED = M_LENGTH in blocks, rounded up: (length - 1) / 1,024 + 1, 1-64.
; Carry set for a length of 0.
NEED:           LDA     MBLK + 2
                ORA     MBLK + 3
                BEQ     @none
                LDA     MBLK + 2
                CMP     #1             ; length - 1's high byte: borrow if the low is 0
                LDA     MBLK + 3
                SBC     #0
                LSR     A
                LSR     A
                INC     A
                STA     MNEED
                CLC
                RTS
@none:          SEC
                RTS

; MIDX = M_BASE's block, AMAP's index; carry set if it isn't a block's
; first paragraph in banks 1-5.
BASE:           LDA     MBLK
                BNE     @no
                LDA     MBLK + 1
                AND     #3
                BNE     @no
                LDA     MBLK + 1
                LSR     A
                LSR     A
                SEC
                SBC     #FIRST_BLOCK
                BCC     @no
                CMP     #AMAP_LEN
                BCS     @no
                STA     MIDX
                CLC
                RTS
@no:            SEC
                RTS

; MRUN = the free blocks from MIDX, to the first taken one or the end.
RUN:            STZ     MRUN
                LDX     MIDX
@block:         CPX     #AMAP_LEN
                BCS     @done
                LDA     AMAP,X
                BNE     @done
                INC     MRUN
                INX
                BRA     @block
@done:          RTS

; MRUN blocks from MIDX allocated (53, 54): MBLK's base and length
; theirs, its M_EXT 1 if a free block is left; then out to the program.
SAYRUN:         JSR     MARK
                LDX     #AMAP_LEN - 1
                LDA     #0
@count:         LDY     AMAP,X
                BNE     @taken
                LDA     #1
@taken:         DEX
                BPL     @count
                STA     MBLK + 4
                JMP     MCBOUT

; MRUN blocks from MIDX allocated: MBLK's base block MIDX, its length MRUN
; blocks.
MARK:           LDX     MIDX
                LDA     #MAP_FIRST
                LDY     MRUN
@block:         STA     AMAP,X
                LDA     #MAP_MORE
                INX
                DEY
                BNE     @block
                STZ     MBLK
                LDA     MIDX
                CLC
                ADC     #FIRST_BLOCK
                ASL     A
                ASL     A
                STA     MBLK + 1
                STZ     MBLK + 2
                LDA     MRUN
                ASL     A
                ASL     A
                STA     MBLK + 3
                RTS

; MRUN = at most MNEED.
CLAMP:          LDA     MRUN
                CMP     MNEED
                BCC     @done
                LDA     MNEED
                STA     MRUN
@done:          RTS

F_MCMAX:        LDX     #5
                JSR     PREAD
                JSR     NEED
                BCS     NOMEM
                STZ     MBEST          ; the longest run, the lowest first
                LDX     #0
@from:          STX     MIDX
                JSR     RUN
                LDA     MRUN
                CMP     MBEST
                BCC     @next
                BEQ     @next
                STA     MBEST
                LDA     MIDX
                STA     T0
@next:          LDX     MIDX
                INX
                CPX     #AMAP_LEN
                BNE     @from
                LDA     MBEST
                BEQ     NOMEM
                STA     MRUN
                LDA     T0
                STA     MIDX
                JSR     CLAMP
                JMP     SAYRUN

F_MCABSMAX:     LDX     #5
                JSR     PREAD
                JSR     BASE
                BCS     NOMEM
                JSR     NEED
                BCS     NOMEM
                JSR     RUN
                LDA     MRUN
                BEQ     NOMEM
                JSR     CLAMP
                JMP     SAYRUN

F_MCALLOC:      LDX     #5
                JSR     PREAD
                JSR     NEED
                BCS     NOMEM
                LDX     #0
@from:          STX     MIDX
                JSR     RUN
                LDA     MRUN
                CMP     MNEED
                BCS     TAKE
                LDX     MIDX
                INX
                CPX     #AMAP_LEN
                BNE     @from
                BRA     NOMEM

NOMEM:          LDA     #$FF
                JMP     RETA

F_MCABSALL:     LDX     #5
                JSR     PREAD
                JSR     BASE
                BCS     NOMEM
                JSR     NEED
                BCS     NOMEM
                JSR     RUN
                LDA     MRUN
                CMP     MNEED
                BCC     NOMEM
                ; on: TAKE

; MNEED blocks from MIDX allocated; the MCB's base and length theirs.
TAKE:           LDA     MNEED
                STA     MRUN
                JSR     MARK
                JMP     MCBOUT

NOMEM2:         BRA     NOMEM

F_MCFREE:       LDX     #5
                JSR     PREAD
                LDA     MBLK + 4
                CMP     #$FF
                BEQ     F_MCALLFREE
                JSR     BASE
                BCS     NOMEM2
                LDX     MIDX
                LDA     AMAP,X
                CMP     #MAP_FIRST
                BEQ     @block         ; an allocation's base: all of it
                CMP     #MAP_MORE
                BNE     NOMEM2         ; a free block
                JSR     NEED           ; a later block: its tail, if M_LENGTH reaches the end
                BCS     NOMEM2
                LDX     MIDX
@end:           INX
                CPX     #AMAP_LEN
                BCS     @ends
                LDA     AMAP,X
                CMP     #MAP_MORE
                BEQ     @end
@ends:          TXA                    ; the tail's blocks
                SEC
                SBC     MIDX
                CMP     MNEED
                BEQ     @tail
                BCS     NOMEM2         ; short of the end
@tail:          LDX     MIDX
@block:         STZ     AMAP,X
                INX
                CPX     #AMAP_LEN
                BCS     @done
                LDA     AMAP,X
                CMP     #MAP_MORE
                BEQ     @block
@done:          JMP     RET0

F_MCALLFREE:    LDX     #AMAP_LEN - 1
@block:         STZ     AMAP,X
                DEX
                BPL     @block
                JMP     RET0

; ---------------------------------------------------------------------------
; 73: a CPU address to its physical one

F_PHYS:         LDX     #4
                JSR     PREAD
                LDA     MBLK + 1
                CMP     #>CODE_WINDOW
                BCS     @memory
                CMP     #$C0
                BCS     @no            ; the I/O hole
@memory:        ROL     A              ; the window: the top 2 bits
                ROL     A
                ROL     A
                AND     #3
                TAX
                LDA     ST_WIN,X
                BIT     #NB_ROM
                BNE     @no            ; flash
                AND     #$3F
                STA     T0             ; the block
                LDA     MBLK + 1
                AND     #$3F
                STA     T1             ; the address in it: T1, MBLK
                LDA     MBLK           ; the paragraph: block x 1,024 + it / 16
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                STA     T2
                LDA     T1
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     T2
                STA     T2
                LDA     T1
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                STA     T3
                LDA     T0
                ASL     A
                ASL     A
                ORA     T3
                STA     T3
                LDA     MBLK
                AND     #$0F
                JSR     PPUT
                LDA     #0
                JSR     PPUT
                LDA     T2
                JSR     PPUT
                LDA     T3
                JSR     PPUT
                JMP     RET0
@no:            JMP     ABORT

; ---------------------------------------------------------------------------
; 74: mount or unmount an SD drive

MERROR:         LDA     #$FF
                JMP     RETAX

F_MOUNT:        JSR     PGET           ; the drive: B:-G:
                STA     MDRV
                DEC     A
                CMP     #SD_DRIVES - 1
                BCC     @drive
                LDX     #ER_DRIVE
                BRA     MERROR
@drive:         LDA     MDRV           ; MOUNT's sector: the unit, the flags, the path
                INC     A
                STA     SECBUF
                JSR     PGET
                AND     #1
                STA     SECBUF + 1
                LDA     #<(SECBUF + 2)
                STA     MP
                LDA     #>(SECBUF + 2)
                STA     MP + 1
                STZ     MLEN
                STZ     MLEN + 1
                JSR     PGET           ; (PGET's flags are X's)
                STA     (MP)
                CMP     #0
                BEQ     @path          ; empty: unmount
                CMP     #'/'
                BNE     @bad           ; from the root
                JSR     PATH
                BCS     @bad
@path:          LDA     MLEN + 1       ; the rest of the sector 0
                ORA     MLEN
                BEQ     @written
@zero:          JSR     MNEXT
                LDA     MP + 1
                CMP     #>(SECBUF + 512)
                BEQ     @written
                LDA     #0
                STA     (MP)
                BRA     @zero
@bad:           LDX     #ER_PATH
                BRA     MERROR
@written:       LDA     MDRV           ; its buffers written back: one that fails refuses it
                INC     A
                JSR     FLUSHD
                BCC     @flushed
                LDX     #ER_CARD
                BRA     MERROR
@flushed:       LDA     MDRV           ; dropped, the drive logged out, read-write
                JSR     DROPD
@card:          LDA     SECBUF + 2
                BEQ     @unmount
                LDA     #DOP_MOUNT
                JSR     CARD
                BEQ     @mounted
                LDX     #ER_NOTFOUND
                LDA     SD_ERROR
                CMP     #SD_ERR_NOTFOUND
                BEQ     @failed
                LDX     #ER_CARD
@failed:        JMP     MERROR
@unmount:       JSR     UNMOUNT
                BEQ     @done
                LDA     SD_ERROR       ; nothing was mounted: done
                CMP     #SD_ERR_UNIT
                BEQ     @done
                LDX     #ER_CARD
                BRA     @failed
@done:          JSR     RESCAN
                JMP     RET0
@mounted:       JSR     RESCAN         ; the units again: the new one's size and name
                LDX     #ER_CARD
                BCS     @wrong         ; not read: the table is the old one's
                LDA     MDRV
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                TAY                    ; its entry
                LDX     #ER_SIZE
                LDA     UNITSAV + 2,Y  ; 280 sectors, $00000118: apple-do (phase 6b)
                ORA     UNITSAV + 3,Y
                BNE     @own
                LDA     UNITSAV,Y
                CMP     #<SD_SECTORS_APPLE
                BNE     @own
                LDA     UNITSAV + 1,Y
                CMP     #>SD_SECTORS_APPLE
                BEQ     @sized
@own:           LDA     UNITSAV,Y      ; 16,384 sectors: $00004000
                ORA     UNITSAV + 3,Y
                BNE     @wrong
                LDA     UNITSAV + 1,Y
                CMP     #>SD_SECTORS_8M
                BNE     @big
                LDA     UNITSAV + 2,Y
                BEQ     @sized
                BRA     @wrong
@big:           CMP     #0             ; 65,536: $00010000
                BNE     @wrong
                LDA     UNITSAV + 2,Y
                CMP     #1
                BNE     @wrong
@sized:         JSR     SAMEFILE
                BCS     @same
                JMP     RET0
@same:          LDX     #ER_SAMEFILE
@wrong:         PHX                    ; the unit unmounted again: no image
                JSR     UNMOUNT
                JSR     RESCAN
                PLX
                JMP     MERROR

; ---------------------------------------------------------------------------
; 76: a DOS 3.3 sector of an Apple disk (6d, ours). A/X a block: the drive
; (0 A: - 15 P:), the track (0-34), DOS 3.3's sector (0-15), 0 read or 1
; write, the address of 256 bytes in the program's memory. A = 0 done, 1 an
; error (the disk's, or a track or sector past the disk), 2 write-protected,
; $FF (X 0) not an Apple disk: O: or P: with the Disk II, or an SD drive
; whose unit is 140 KB. The drive's buffers are written back and dropped
; and it is logged out first, as 37 does, if it was logged in: its next
; CP/M use reads the disk again.

F_SECT:         LDX     #0
@get:           JSR     PGET
                STA     RBLK,X
                INX
                CPX     #6
                BNE     @get
                LDA     RBLK           ; an Apple disk? O:, P: (the Disk II's DPH, at @bios)
                CMP     #DRV_O
                BCS     @disk2
                CMP     #SD_DRIVES
                BCS     @not
                JSR     DRVBIT         ; an SD drive whose unit is 140 KB. The program's first
                STA     RRO            ; 76 call on it, and every write, read the units again;
                AND     RSEEN          ; the first keeps the unit's mount count (MNTGEN, the
                BEQ     @scan          ; BIOS's: nothing clears it), and a write is refused if
                LDA     RBLK + 3       ; it has moved since, however the change was seen (6d's
                BNE     @scan          ; third review). A read of a drive known as one: as it is
                LDX     RBLK
                LDA     DRVAPL,X
                BMI     @apple
@scan:          JSR     RESCAN
                LDX     RBLK
                LDA     RRO            ; (RRO for the moment: the drive's bit)
                AND     RSEEN
                BNE     @seen
                LDA     RRO
                TSB     RSEEN
                LDA     MNTGEN,X
                STA     RGEN,X
                BRA     @known
@seen:          LDA     RBLK + 3
                BEQ     @known
                LDA     MNTGEN,X
                CMP     RGEN,X
                BNE     @not
@known:         LDA     DRVAPL,X
                BMI     @apple
@not:           LDA     #$FF
                JMP     RETA
@error:         LDA     #1
                JMP     RETA
@disk2:         CMP     #DRV_P + 1
                BCS     @not
@apple:         LDA     RBLK + 1       ; past the disk: an error
                CMP     #35
                BCS     @error
                LDA     RBLK + 2
                CMP     #16
                BCS     @error
                LDA     RBLK + 3
                CMP     #2
                BCS     @error
                LDA     RBLK           ; read-only (28, SET [RO]): a write refused, and the
                JSR     DRVBIT         ; drive left read-only
                AND     ROVEC,X
                STA     RRO
                BEQ     @rw
                LDA     RBLK + 3
                BEQ     @rw
                LDA     #2
                JMP     RETA
@rw:            LDA     RBLK           ; logged in: written back, dropped, logged out
                JSR     DRVBIT
                AND     LOGVEC,X
                BEQ     @out
                LDA     RBLK
                INC     A
                JSR     FLUSHD
                BCS     @error         ; data that won't go: not dropped
                LDA     RBLK
                JSR     DROPD
                LDA     RRO
                BEQ     @out
                LDA     RBLK
                JSR     DRVBIT
                ORA     ROVEC,X
                STA     ROVEC,X
@out:           LDA     RBLK + 3       ; a write: the program's 256 bytes into SECBUF first
                BEQ     @bios
                CLC
                JSR     RBW
                LDA     #BANK_PROG
                LDX     #BANK_SYS
                JSR     R256
@bios:          LDA     RBLK
                LDX     #1
                JSR     B_SELDSK
                STX     T0
                ORA     T0
                BNE     @dph
                STZ     BIOSDRV        ; no DPH: O: or P: with no Disk II
                LDA     #$FF
                JMP     RETA
@dph:           LDA     RBLK + 1
                LDX     #0
                JSR     B_SETTRK
                LDA     RBLK + 2
                LDX     #0
                JSR     B_SETSEC
                LDA     #BANK_SYS
                JSR     B_SETBNK
                LDA     #<SECBUF
                LDX     #>SECBUF
                JSR     B_SETDMA
                INC     RAWF
                LDA     RBLK + 3
                BNE     @write
                JSR     B_READ
                BRA     @done
@write:         LDA     #0
                JSR     B_WRITE
@done:          STZ     RAWF
                STZ     BIOSDRV
                CMP     #0
                BNE     @result
                LDX     RBLK + 3       ; a read: SECBUF to the program
                BNE     @result
                SEC
                JSR     RBW
                LDA     #BANK_SYS
                LDX     #BANK_PROG
                JSR     R256
                LDA     #0
@result:        JMP     RETA

; BW1 the program's address and BW2 SECBUF (carry clear), or the other way.
RBW:            LDX     #BW1
                LDY     #BW2
                BCC     @set
                LDX     #BW2
                LDY     #BW1
@set:           LDA     RBLK + 4
                STA     0,X
                LDA     RBLK + 5
                STA     1,X
                LDA     #<SECBUF
                STA     0,Y
                LDA     #>SECBUF
                STA     1,Y
                RTS
; 256 bytes from bank A's BW1 to bank X's BW2; memory MOVE refuses ends the
; function (A = X = $FF), as PIN and POUT.
R256:           JSR     B_XMOVE
                LDA     #0
                LDX     #1
                JSR     B_MOVE
                BCC     @done
                JMP     ABORT
@done:          RTS

; Drive A's buffers dropped, not written; the drive logged out, read-write,
; and a search on it ended (74, 76).
DROPD:          STA     T0
                INC     A
                JSR     PURGED
                LDA     T0
                JSR     DRVBIT
                EOR     #$FF
                PHA
                AND     LOGVEC,X
                STA     LOGVEC,X
                PLA
                AND     ROVEC,X
                STA     ROVEC,X
                LDA     SSDRV
                CMP     T0
                BNE     @done
                STZ     SSON
@done:          RTS

; The card's UNMOUNT of MDRV's unit; A as CARD's.
UNMOUNT:        LDA     #DOP_UNMOUNT
; The BIOS's SDMNT, A its operation, MDRV the drive: A = 0 (Z set) or 1.
; The BIOS's drive is then another: DISKOP selects its own again.
CARD:           LDX     #BIOS_BLOCK
                STX     NB_WIN2
                LDX     MDRV
                JSR     SDMNT
                STZ     BIOSDRV
                CMP     #0
                RTS

; Carry set if another mounted unit's file has the new one's name (the
; units table's 24 bytes, DRIVES.TXT's rule). Y the new one's entry.
SAMEFILE:       STY     T0
                LDX     #0             ; X the other unit's entry
@unit:          CPX     T0
                BEQ     @next
                LDA     UNITSAV + 4,X
                LSR     A              ; bit 0: mounted
                BCC     @next
                STX     T1
                LDY     T0
                LDA     #UNIT_NAME_LEN
                STA     T2
@byte:          LDA     UNITSAV + UNIT_NAME,X
                CMP     UNITSAV + UNIT_NAME,Y
                BNE     @differ
                INX
                INY
                DEC     T2
                BNE     @byte
                SEC                    ; the same
                RTS
@differ:        LDX     T1
@next:          TXA
                CLC
                ADC     #UNIT_LEN
                TAX
                CPX     #SD_DRIVES * UNIT_LEN
                BCC     @unit
                CLC
                RTS

; The path after its first '/', each byte to MP: carry set if DRIVES.TXT's
; rules refuse it (hw/oodos.md, "Drives"): a name empty, '.' or '..', a
; space, tab, '#', control character or DEL, over MAX_PATH bytes.
PATH:           JSR     MNEXT
@name:          STZ     NLEN
                LDA     #$80
                STA     DOTS
@byte:          JSR     PGET
                STA     (MP)
                CMP     #0
                BEQ     ENDNAME        ; the NUL: the last name ends
                CMP     #'#'
                BEQ     @no
                CMP     #' ' + 1
                BCC     @no            ; space, tab, control
                CMP     #$7F
                BEQ     @no
                PHA
                JSR     MNEXT
                PLA
                LDX     MLEN + 1       ; past MAX_PATH bytes?
                CPX     #>(MAX_PATH + 1)
                BCC     @room
                LDX     MLEN
                CPX     #<(MAX_PATH + 1)
                BCS     @no
@room:          CMP     #'/'
                BNE     @char
                JSR     ENDNAME
                BCC     @name
                RTS
@char:          CMP     #'.'
                BEQ     @dot
                STZ     DOTS
@dot:           LDX     NLEN           ; counted to 3: enough to tell . and ..
                CPX     #3
                BCS     @byte
                INC     NLEN
                BRA     @byte
@no:            SEC
                RTS

; Carry set if the name just ended is empty, '.' or '..'.
ENDNAME:        LDA     NLEN
                BEQ     @no
                BIT     DOTS
                BPL     @ok
                CMP     #3
                BCC     @no
@ok:            CLC
                RTS
@no:            SEC
                RTS

; MP on to the next byte, MLEN the bytes so far.
MNEXT:          INC     MP
                BNE     @len
                INC     MP + 1
@len:           INC     MLEN
                BNE     @done
                INC     MLEN + 1
@done:          RTS

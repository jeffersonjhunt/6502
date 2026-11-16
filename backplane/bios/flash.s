; The flash update (hw/design.md, "The flash: ROM A and ROM B" and "ROM A's
; layout"; step B5), from the monitor: C818R, then F (bios.s's menu). It
; writes an Apple IIe ROM image and its character set from the SD card into
; ROM A: image n (0-3) in block n, set n at $10000 + n x $2000 (blocks 4-5).
; It writes nothing else: blocks 6-31 and ROM B are never touched.
;
;   1. It asks for the image, 0-3, and reads /APPLE2En.ROM (16,384 bytes,
;      needed) and /APPLE2En.CHR (4 or 8 KB, if there is one) from the card's
;      root, by the CPU, into RAM blocks 59 and 58 (the staging, through W2;
;      Apple II mode overwrites both anyway). The sizes are the listing's.
;   2. It checks them before anything is erased: the size, and a IIe's $06
;      at the image's $FBB3. A file missing, the wrong size or not a IIe's, or
;      a read that fails, changes nothing in the flash.
;   3. It says what it found and asks before it writes.
;   4. It puts each chip back to reading first (a byte of $FF to each, which
;      programs nothing but ends a program sequence someone left half-sent,
;      then $F0), then erases the image's 16 KB (four sector erases) and the
;      set's 8 KB (two). With no .CHR the set stays erased, so Apple II mode
;      makes one from the VGA font.
;   5. It programs every byte but the commit bytes, a pair at a time: the even
;      byte's sequence and byte to the even chip, the odd byte's to the odd,
;      then each chip polled at an address of its own parity until two reads
;      agree (the toggle bit). The commit bytes are the image's $FBB3, which
;      makes it good, and the set's first 8, which make it more than erased.
;   6. It verifies everything against the staging, then programs the commit
;      bytes, the image's last of all, and verifies again. So an image is good
;      only when it was written whole and verified, and an update stopped part
;      way (a Break, the power) leaves it not good.
;   7. On a mismatch or a chip that doesn't answer it erases the image and set
;      again and says so; either way it ends in the monitor.
;
; However it is entered it runs QUIET (bios.s) and with interrupts off, and
; sets every window itself: W0 zero page and the stack (RAM block 0), W2 the
; staging, W3 the BIOS's copy (RAM block 63), and W1 the flash, read-only to
; verify and writable to program. Every write to the flash goes through FPUT,
; which refuses (at run time, whatever computed it) any W1 value but ROM A
; blocks 0-5 and goes to the monitor; and it never sends a chip erase. On the
; way out W1 and W2 go back to blocks 1 and 2, as Break leaves them. The CPU
; card's word buffer serves only read-only windows, so every read here with
; RO clear (each poll) is a bus cycle.
;
; Zero page $10-$21 and RAM $0280-$029A are its own while it runs; the SD
; card's are sd.s's ($F0-$FB, $0400-$05FF).

.setcpu "65C02"
.include "backplane.inc"
.import WOZMON, ECHO, PRBYTE, GETKEY, QUIET, SDLOAD
.export FLASHUP

FIMG      = $10                        ; the image, 0-3
FBLK      = $11                        ; the W1 value written to: NB_ROM | the block
FA        = $12                        ; $12-$13: FPUT's address, through W1
FP        = $14                        ; $14-$15: the flash, through W1
SP        = $16                        ; $16-$17: the staging, through W2
FE        = $18                        ; a pair's even and odd bytes ($FF: none to program)
FO        = $19
FCL       = $1A                        ; a poll's count
FCH       = $1B
FCNT      = $1C                        ; $1C-$1D: pairs or pages left
FFAIL     = $1E                        ; bit 7: erasing again after a failure
FCHR      = $1F                        ; the set's pages from the card: $10, $20, or 0 none
FPATH     = $0280                      ; "/APPLE2En.ROM", then ".CHR"
FSAVE     = $0290                      ; the set's first 8 bytes, its commit
FWHERE    = $0298                      ; $0298-$029A: a mismatch's address, high first
FMSG      = $20                        ; $20-$21: SAY's message

.macro SAYM msg
                LDA     #<msg
                LDY     #>msg
                JSR     SAY
.endmacro

IMG_STAGE = 59                         ; RAM block 59: the image, at $8000 through W2
SET_STAGE = 58                         ; RAM block 58: the set
STAGE     = $8000
FLASH_CPU = $4000                      ; W1
LAST_BLOCK = IIE_CHARS_BLOCK + (IIE_IMAGES - 1) / 2 ; the last block the update may write: 5
.assert IIE_IMAGES = 4 && IIE_CHARS_BLOCK = IIE_IMAGES && LAST_BLOCK = 5, error, "the update writes ROM A blocks 0-5 alone"

; A chip's command addresses, $5555 and $2AAA in the chip, are flash offsets
; $AAAA and $5554 for the even chip, one more for the odd: through W1, $6AAA
; in block 2 and $5554 in block 1 (ORed with the chip, 0 or 1).
CMD1_BLK  = NB_ROM | 2
CMD1      = FLASH_CPU + ($AAAA - $8000)
CMD2_BLK  = NB_ROM | 1
CMD2      = FLASH_CPU + ($5554 - $4000)
.assert (<CMD1 & 1) = 0 && (<CMD2 & 1) = 0, error, "the even chip's command addresses are even"

.segment "FLASH"

FLASHUP:        SEI
                CLD
                LDX     #$FF
                TXS
                STZ     NB_WIN0        ; zero page and the stack
                LDA     #1
                STA     NB_WIN1
                LDA     #2
                STA     NB_WIN2
                LDA     #NB_RO | 63    ; the BIOS's copy, where this runs
                STA     NB_WIN3
                JSR     QUIET          ; the DMA stopped, the cards quiet
                SAYM    MASK
                JSR     GETKEY
                JSR     ECHO
                SEC
                SBC     #'0'
                CMP     #IIE_IMAGES
                BCC     @image
                JMP     LEAVECR        ; any other key: the monitor
@image:         STA     FIMG
                LDX     #ROMNAME_END - ROMNAME - 1
@name:          LDA     ROMNAME,X
                STA     FPATH,X
                DEX
                BPL     @name
                LDA     FIMG
                ORA     #'0'
                STA     FPATH + 8

                LDA     #IMG_STAGE     ; the image, into RAM block 59
                STA     NB_WIN2
                LDA     #<FPATH
                LDX     #>FPATH
                LDY     #>STAGE
                CLC                    ; not there: said
                JSR     SDLOAD
                BCC     @rom
                JMP     LEAVE          ; said
@rom:           CPX     #>$4000        ; 16,384 bytes, by the listing
                BNE     @notrom
                CMP     #<$4000
                BNE     @notrom
                LDA     STAGE + IIE_ID_AT
                CMP     #IIE_ID
                BEQ     @romok
@notrom:        JSR     CRPATH
                SAYM    MNOTROM
                JMP     LEAVECR
@romok:         JSR     CRPATH
                SAYM    M16

                LDA     #SET_STAGE     ; the set, into RAM block 58, $FF where it has none
                STA     NB_WIN2
                LDX     #$20
                LDY     #0
                STZ     SP
                LDA     #>STAGE
                STA     SP+1
                LDA     #$FF
@fill:          STA     (SP),Y
                INY
                BNE     @fill
                INC     SP+1
                DEX
                BNE     @fill
                LDX     #2             ; .ROM to .CHR
@chrname:       LDA     CHREXT,X
                STA     FPATH + 10,X
                DEX
                BPL     @chrname
                LDA     #<FPATH
                LDX     #>FPATH
                LDY     #>STAGE
                SEC                    ; not there: nothing to say
                JSR     SDLOAD
                BCC     @chr
                CMP     #SD_NOT_FOUND
                BEQ     @nochr
                JMP     LEAVE          ; a failure, said
@nochr:         STZ     FCHR
                SAYM    MNO
                JSR     PATH
                SAYM    MNOCHR
                BRA     @ask
@chr:           CMP     #0
                BNE     @notchr
                CPX     #>$1000        ; 4 KB
                BEQ     @size
                CPX     #>$2000        ; or 8 KB
                BEQ     @size
@notchr:        JSR     CRPATH
                SAYM    MNOTCHR
                JMP     LEAVECR
@size:          STX     FCHR
                JSR     CRPATH
                LDA     FCHR
                CMP     #>$1000
                BNE     @kb8
                SAYM    M4
                BRA     @ask
@kb8:           SAYM    M8

@ask:           SAYM    MASKW
                JSR     GETKEY
                JSR     ECHO
                AND     #$DF           ; y or Y
                CMP     #'Y'
                BEQ     WRITE
                JMP     LEAVECR

; The writing, from here to the monitor.
WRITE:          STZ     FFAIL
                JSR     CHIPS          ; each chip reading
                JSR     ERASE          ; the image's 16 KB and the set's 8 KB
                LDA     #IMG_STAGE     ; the commit bytes out of the staging, $FF there for now
                STA     NB_WIN2
                LDA     #$FF
                STA     STAGE + IIE_ID_AT
                LDA     #SET_STAGE
                STA     NB_WIN2
                LDX     #7
@save:          LDA     STAGE,X
                STA     FSAVE,X
                LDA     #$FF
                STA     STAGE,X
                DEX
                BPL     @save

                LDA     #IMG_STAGE     ; the image, every pair but its commit byte
                STA     NB_WIN2
                JSR     IMGAT
                LDA     #<$2000
                STA     FCNT
                LDA     #>$2000
                STA     FCNT+1
                JSR     PROG
                LDA     #SET_STAGE     ; the set, every pair but its first 4
                STA     NB_WIN2
                JSR     SETAT
                STZ     FCNT
                LDA     #>$1000
                STA     FCNT+1
                JSR     PROG
                JSR     VERIFY

                LDA     #SET_STAGE     ; the commit: the set's first 8 bytes,
                STA     NB_WIN2
                LDX     #7
@restore:       LDA     FSAVE,X
                STA     STAGE,X
                DEX
                BPL     @restore
                JSR     SETAT
                LDA     #4
                STA     FCNT
                STZ     FCNT+1
                JSR     PROG
                LDA     #IMG_STAGE     ; then the image's $FBB3, last of all
                STA     NB_WIN2
                LDA     #IIE_ID
                STA     STAGE + IIE_ID_AT
                JSR     IMGAT
                LDA     #<(FLASH_CPU + IIE_ID_AT - 1)
                STA     FP
                LDA     #>(FLASH_CPU + IIE_ID_AT - 1)
                STA     FP+1
                LDA     #$FF           ; its pair's even byte is written already
                STA     FE
                LDA     #IIE_ID
                STA     FO
                JSR     PAIR
                JSR     VERIFY
                SAYM    MWRITTEN
                JMP     LEAVECR

; Each chip back to reading the array, whatever someone left it in: a byte of
; $FF at the image's first pair (it programs nothing, and ends any sequence
; half-sent, a program's taking it as its data), each polled, then $F0.
CHIPS:          JSR     IMGAT
                LDA     FP
                STA     FA
                LDA     FP+1
                STA     FA+1
                LDA     #$FF
                LDX     FBLK
                JSR     FPUT
                INC     FA
                LDA     #$FF
                LDX     FBLK
                JSR     FPUT
                JSR     POLL           ; the odd byte
                DEC     FA
                JSR     POLL           ; the even
                LDY     #0
                JSR     TOREAD
                LDY     #1
TOREAD:         TYA                    ; $F0 to chip Y
                ORA     #<CMD1
                STA     FA
                LDA     #>CMD1
                STA     FA+1
                LDA     #$F0
                LDX     #CMD1_BLK
                JMP     FPUT

; The image's two 8 KB units and the set's one: a sector erase in each chip,
; then each polled.
ERASE:          JSR     IMGAT
                JSR     ERASEU
                LDA     #>(FLASH_CPU + $2000)
                STA     FP+1
                JSR     ERASEU
                JSR     SETAT
ERASEU:         LDY     #0
                JSR     ESTART
                LDY     #1
                JSR     ESTART
                LDA     FP             ; each chip polled in the unit
                STA     FA
                LDA     FP+1
                STA     FA+1
                JSR     POLL
                INC     FA
                JMP     POLL
ESTART:         LDX     FBLK           ; refused before any command is sent
                JSR     FCHECK
                LDA     #$80
                JSR     UNLOCK
                JSR     KEYS
                TYA
                ORA     FP
                STA     FA
                LDA     FP+1
                STA     FA+1
                LDA     #$30           ; the sector holding FA
                LDX     FBLK
                JMP     FPUT

; FCNT pairs from the staging at $8000 (W2 as the caller set it) to the flash
; at FP (FBLK), in ascending order.
PROG:           STZ     SP
                LDA     #>STAGE
                STA     SP+1
@pair:          LDA     (SP)
                STA     FE
                LDY     #1
                LDA     (SP),Y
                STA     FO
                JSR     PAIR
                CLC
                LDA     SP
                ADC     #2
                STA     SP
                BCC     @flash
                INC     SP+1
@flash:         CLC
                LDA     FP
                ADC     #2
                STA     FP
                BCC     @count
                INC     FP+1
@count:         LDA     FCNT
                BNE     @low
                DEC     FCNT+1
@low:           DEC     FCNT
                LDA     FCNT
                ORA     FCNT+1
                BNE     @pair
                RTS

; One pair: FE to the even byte at FP and FO to the odd one after it, in
; FBLK, a byte of $FF not programmed (erased already). Both chips are started,
; then each polled.
PAIR:           LDX     FBLK           ; refused before any command is sent
                JSR     FCHECK
                LDA     FE
                CMP     #$FF
                BEQ     @odd
                LDY     #0
                LDA     #$A0           ; program
                JSR     UNLOCK
                LDA     FP
                STA     FA
                LDA     FP+1
                STA     FA+1
                LDA     FE
                LDX     FBLK
                JSR     FPUT
@odd:           LDA     FO
                CMP     #$FF
                BEQ     @poll
                LDY     #1
                LDA     #$A0
                JSR     UNLOCK
                LDA     FP
                ORA     #1
                STA     FA
                LDA     FP+1
                STA     FA+1
                LDA     FO
                LDX     FBLK
                JSR     FPUT
@poll:          LDA     FP+1
                STA     FA+1
                LDA     FE
                CMP     #$FF
                BEQ     @podd
                LDA     FP
                STA     FA
                JSR     POLL
@podd:          LDA     FO
                CMP     #$FF
                BEQ     @done
                LDA     FP
                ORA     #1
                STA     FA
                JSR     POLL
@done:          RTS

; Chip Y's command: the unlock pair, then A at its $5555.
UNLOCK:         PHA
                JSR     KEYS
                TYA
                ORA     #<CMD1
                STA     FA
                LDA     #>CMD1
                STA     FA+1
                PLA
                LDX     #CMD1_BLK
                JMP     FPUT
; Chip Y's unlock pair: $AA at its $5555, $55 at its $2AAA.
KEYS:           TYA
                ORA     #<CMD1
                STA     FA
                LDA     #>CMD1
                STA     FA+1
                LDA     #$AA
                LDX     #CMD1_BLK
                JSR     FPUT
                TYA
                ORA     #<CMD2
                STA     FA
                LDA     #>CMD2
                STA     FA+1
                LDA     #$55
                LDX     #CMD2_BLK
                JMP     FPUT

; Every write to the flash: A to FA through W1 = X, which must be ROM A's
; block 0-5, writable (NB_ROM | 0-5); anything else is refused, said, and the
; monitor. FCHECK is the check alone. Both keep A and Y.
FPUT:           JSR     FCHECK
                STX     NB_WIN1
                STA     (FA)
                RTS
FCHECK:         CPX     #NB_ROM
                BCC     REFUSE
                CPX     #NB_ROM + LAST_BLOCK + 1
                BCS     REFUSE
                RTS
REFUSE:         LDA     #1
                STA     NB_WIN1
                PHX
                SAYM    MREFUSED
                PLA
                AND     #$3F           ; the block
                JSR     PRBYTE
                JMP     LEAVECR

; The chip of FA's parity, in FBLK, polled until two reads agree (its toggle
; bit still), with RO clear; 65,536 pairs of reads (about 180 ms at 7.16 MHz,
; far over the parts' longest sector erase) and it doesn't answer.
POLL:           LDX     FBLK
                JSR     FCHECK
                STX     NB_WIN1
                STZ     FCL
                STZ     FCH
@read:          LDA     (FA)
                CMP     (FA)
                BEQ     @done
                DEC     FCL
                BNE     @read
                DEC     FCH
                BNE     @read
                JMP     NOANSWER
@done:          RTS

; The image (64 pages) and the set (32) against the staging, read-only.
VERIFY:         LDA     #IMG_STAGE
                STA     NB_WIN2
                JSR     IMGAT
                LDA     #$40
                JSR     SAME
                LDA     #SET_STAGE
                STA     NB_WIN2
                JSR     SETAT
                LDA     #$20
SAME:           STA     FCNT
                STZ     SP
                LDA     #>STAGE
                STA     SP+1
                LDA     FBLK
                ORA     #NB_RO
                STA     NB_WIN1
                LDY     #0
@byte:          LDA     (FP),Y
                CMP     (SP),Y
                BNE     @differs
                INY
                BNE     @byte
                INC     FP+1
                INC     SP+1
                DEC     FCNT
                BNE     @byte
                RTS
@differs:       STY     FA             ; where: the physical address, $F00000 on
                LDA     FP+1
                STA     FA+1
                JMP     NOTWRITTEN

; FP and FBLK: image FIMG's start, in block FIMG at $4000.
IMGAT:          LDA     FIMG
                ORA     #NB_ROM
                STA     FBLK
                STZ     FP
                LDA     #>FLASH_CPU
                STA     FP+1
                RTS
; FP and FBLK: set FIMG's, in block 4 + FIMG / 2, at $4000 or $6000.
SETAT:          LDA     FIMG
                LSR     A
                CLC
                ADC     #NB_ROM | IIE_CHARS_BLOCK
                STA     FBLK
                STZ     FP
                LDA     FIMG
                AND     #1
                BEQ     @even
                LDA     #>(FLASH_CPU + $2000)
                BRA     @at
@even:          LDA     #>FLASH_CPU
@at:            STA     FP+1
                RTS

; A byte that doesn't verify, at FA through W1 on FBLK: the image and set
; erased again, said with its physical address, and the monitor.
NOTWRITTEN:     LDA     FBLK           ; $F0 + block / 4, then (block & 3) x $40 + the page in W1
                AND     #$3F
                PHA
                LSR     A
                LSR     A
                ORA     #$F0
                STA     FWHERE
                PLA
                AND     #3
                LSR     A
                ROR     A
                ROR     A              ; (block & 3) x $40
                STA     FWHERE + 1
                LDA     FA+1
                SEC
                SBC     #>FLASH_CPU
                ORA     FWHERE + 1
                STA     FWHERE + 1
                LDA     FA
                STA     FWHERE + 2
                JSR     AGAIN
                SAYM    MNOTW
                LDA     FWHERE
                JSR     PRBYTE
                LDA     FWHERE + 1
                JSR     PRBYTE
                LDA     FWHERE + 2
                JSR     PRBYTE
                JMP     LEAVECR

; A chip that doesn't answer its poll: erased again (unless that is what
; didn't answer), said, and the monitor.
NOANSWER:       BIT     FFAIL
                BMI     @say
                JSR     AGAIN
@say:           SAYM    MNOANS
                JMP     LEAVECR

AGAIN:          LDA     #$80
                STA     FFAIL
                JMP     ERASE

; "\r" and the path; the path.
CRPATH:         LDA     #$0D
                JSR     ECHO
PATH:           LDX     #0
@char:          LDA     FPATH,X
                BEQ     @done
                JSR     ECHO
                INX
                BNE     @char
@done:          RTS

; The message at A (low) and Y (high), to its 0; a # in it is the image's
; digit. Keeps X.
SAY:            STA     FMSG
                STY     FMSG+1
                LDY     #0
@char:          LDA     (FMSG),Y
                BEQ     @done
                CMP     #'#'
                BNE     @echo
                LDA     FIMG
                ORA     #'0'
@echo:          JSR     ECHO
                INY
                BNE     @char
@done:          RTS

; To the monitor, the windows as Break leaves them; LEAVECR says "\r" first.
LEAVECR:        LDA     #$0D
                JSR     ECHO
LEAVE:          LDA     #1
                STA     NB_WIN1
                LDA     #2
                STA     NB_WIN2
                LDX     #$FF
                TXS
                JMP     WOZMON

ROMNAME:        .byte   "/APPLE2E0.ROM", 0
ROMNAME_END:
CHREXT:         .byte   "CHR"
MASK:           .byte   $0D, "FLASH UPDATE: WHICH IIE IMAGE, 0-3? ", 0
M16:            .byte   " 16 KB", 0
M4:             .byte   " 4 KB", 0
M8:             .byte   " 8 KB", 0
MNO:            .byte   $0D, "NO ", 0
MNOCHR:         .byte   ": SET # ERASED, ITS CHARACTERS THE VGA FONT'S", 0
MNOTROM:        .byte   " ISN'T A IIE ROM", 0
MNOTCHR:        .byte   " ISN'T 4 OR 8 KB", 0
MASKW:          .byte   $0D, "WRITE FLASH IMAGE # (Y/N)? ", 0
MWRITTEN:       .byte   $0D, "FLASH IMAGE # WRITTEN", 0
MNOTW:          .byte   $0D, "FLASH IMAGE # NOT WRITTEN AT $", 0
MNOANS:         .byte   $0D, "THE FLASH DOESN'T ANSWER", 0
MREFUSED:       .byte   $0D, "THE FLASH UPDATE REFUSED BLOCK $", 0

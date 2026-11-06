; OODOS's BIOS (hw/oodos.md, "BIOS"), block 61: $8000-$BFFF while the BDOS
; or the BIOS runs (W2), with the system block in W0 and the BDOS in W1.
; Written from the CP/M 3 manuals' descriptions of the BIOS; the screen and
; the copies follow the ROM BIOS's (backplane/bios: video.s, input.s).
;
; Every entry is reached through the jump table at $F800 (common.s), CP/M
; 3's order. CP/M 3's registers map to the 6502 as the BDOS's do
; (hw/oodos.md, "Registers"): a byte in C (or E) comes in A, a word in BC
; comes in A (low) and X (high); a byte result comes back in A (CP/M's A),
; a word in A and X (CP/M's HL). MOVE's other two words, DE and HL, are in
; the system's zero page, BW1 and BW2: function 50's +4-5 and +6-7. Nothing
; else is kept; D must be clear (the BDOS stub clears it).
;
;   Entry    In                              Out
;   BOOT     -                               cold boot (block 62's COLD)
;   WBOOT    -                               warm boot (common.s's WARM)
;   CONST    -                               A = $FF a character waits, 0 none
;   CONIN    -                               A = the character
;   CONOUT   A = the character               -
;   LIST     A                               -
;   AUXOUT   A                               -
;   AUXIN    -                               A
;   HOME     -                               - (track 0)
;   SELDSK   A = drive (0-15), X bit 0: 1    A/X = its DPH, 0: no such drive
;            if selected before (0 rereads
;            the card's units)
;   SETTRK   A/X = track                     -
;   SETSEC   A/X = sector (512-byte)         -
;   SETDMA   A/X = the buffer's address      -
;   READ     -                               A = 0, or 1 an error
;   WRITE    A = CP/M's deblocking code      A = 0, 1 an error, 2 read-only
;   LISTST   -                               A = $FF ready, 0 not
;   SECTRN   A/X = sector, BW1 = table       A/X = the sector (no translation)
;   CONOST   -                               A = $FF ready, 0 not
;   AUXIST   -                               A
;   AUXOST   -                               A
;   DEVTBL   -                               A/X = the character device table
;   DEVINI   A = device                      -
;   DRVTBL   -                               A/X = the drive table (16 DPHs)
;   MULTIO   A = count                       A = 0 (READ and WRITE take one)
;   FLUSH    -                               A = 0 (nothing is held back)
;   MOVE     A/X = count, BW1 = source       A/X and BW2 = destination +
;            (CP/M's DE), BW2 = destination  count, BW1 = source + count,
;            (CP/M's HL)                     carry clear; or A = X = $FF,
;                                            carry set, nothing moved (an
;                                            end of $FFFF is a move: the
;                                            carry tells them apart)
;   TIME     A = 0 read the clock into the   carry set if the clock is past
;            SCB, $FF set it from the SCB    2155 (the SCB left as it was)
;   SELMEM   A = bank                        - (the bank MOVE uses)
;   SETBNK   A = bank                        - (the bank of SETDMA's address)
;   XMOVE    A = source bank (CP/M's C),     - (for the next MOVE)
;            X = destination bank (B)
;
; Banks: 0 is the system's view (W0, W1 and W3 on 31, 60 and 62, W2 the
; block its caller had there, CALLW2: the BIOS's own, 61, unless the BDOS
; called with a hash or buffer block in W2), 1 the program's (the windows
; the BDOS stub saved); any other is taken as 0, but that XMOVE's may also
; be $80 + A16-A19 (phase 3c): that side of the next MOVE is then a
; physical address, its low 16 bits MOVE's (the BDOS's DMA segment, function
; 51, moves records so). Cold boot and
; each warm boot (common.s's WARM) set SETBNK's and SELMEM's to 0 and drop a
; pending XMOVE. Addresses in either are turned into physical ones for the
; DMA controller, a window at a time, so a buffer may cross windows mapped
; anywhere. An address in a window's I/O hole ($C000-$C7FF), in a window on
; flash, or written in a read-only window, can't be: READ and WRITE return 1
; and MOVE moves nothing, checked before a byte moves.
;
; Drives: A-G are SD units 1-7 as IDENTIFY's units table has them (16,384
; sectors oodos-8m, 65,536 oodos-32m, 280 apple-do; anything else isn't a
; drive; a first select reads it again, and DRVCHG says which units
; changed), M: the RAM disk in banks C-D. On OODOS's own formats (8 MB, 32
; MB, M:) LBA = track x 16 + sector; an apple-do drive's are below. A
; sector moves by DMA: channel 0 between the card and the buffer, channel 3
; for M: and copies. A buffer that crosses a window goes through SECBUF.
;
; An apple-do drive (hw/oodos.md, "The plan, phase 6": a 140 KB .dsk, the
; SoftCard's CP/M format as cpmtools has it) has 512-byte sectors too, 8 a
; track, 35 tracks: sector h of track t is CP/M's 256-byte sectors 2h and
; 2h + 1 of the track, each the file's sector SKEW[n] of the track (the
; file is in DOS order, 16 of 256 bytes a track, two to an SD sector). A
; READ brings the whole track, 8 SD sectors, into the track buffer (block
; 29) if it hasn't it already, and copies the two halves out; a WRITE
; copies them in and writes the two SD sectors they changed (the skew never
; puts both halves in one), so the card always has what the buffer has. Any failure empties the buffer, and
; so do a rescan of the units and MOUNT or UNMOUNT.
;
; O: and P: (6c) are the Disk II's drives 1 and 2 (disk2.s), when cold boot
; found the card: apple-do too, the same layer, each with two track buffers
; of its own, one for the directory's track (3) and one for any other (block
; 29 $1000 and $2000, block 30 $0000 and $1000), read a whole track at a
; time and written through a sector at a time. A disk that had stopped
; turning (or the card had the other drive) when an access starts may have
; been changed: its buffers are emptied, unless the BDOS checked the disk in
; this call (VOUCH); MEDIA, FORGET and FORGETD are the BDOS's, for its
; directory checksums.
;
; Character devices (DEVTBL): 0 CRT, the video card's 80x30 text and the USB
; keyboard (crt.s); 1 TTY, the serial card (the 6551 for now); 2 LPT, which
; takes and drops. CONIN, CONOUT and the rest go to the devices their SCB
; vector names (bit 15 device 0). Input from a vector with no device that
; has input (none, or LPT alone) is ^Z at once, as CP/M 3 has a device that
; isn't there.

.setcpu "65C02"
.include "sys.inc"
.import WARM, COLD
.import CRTINIT, CRTOUT
.export CONST, CONIN, CONOUT, LIST, AUXOUT, AUXIN, HOME, SELDSK, SETTRK, SETSEC
.export SETDMA, READ, WRITE, LISTST, SECTRN, CONOST, AUXIST, AUXOST, DEVTBL, DEVINI
.export DRVTBL, MULTIO, FLUSH, MOVE, TIME, SELMEM, SETBNK, XMOVE
.export BINIT, MFORMAT, DAYS2DATE, DMAGO, DRVRO, DRVCHG, SDMNT, UNITSAV, MEDIA, FORGET, FORGETD, VOUCH
.exportzp TMP, SPTR                    ; disk2.s's too
.import D2INIT, D2START, D2STOP, D2TRACK, D2WSEC, D2MEDIA, D2FS, D2GOTF, D2BLK
.exportzp TYEAR, TMONTH, TDATE, SRCP, DSTP, DCOUNT
.exportzp DMABANK, CURBANK, XPEND      ; WARM resets them
.export TESC                           ; and TTYOUT's escape state

DPB_LEN         = 17
DPH_LEN         = 25
PHYS            = $80                  ; a copy side's bank: a physical address
SD_WRITE        = $30                  ; CMD
SD_UNMOUNT      = $A1
DOP_UNMOUNT     = 2                    ; SDIO's DOP: 0 READ, 1 WRITE, 2 UNMOUNT, 3 MOUNT
DOP_MOUNT       = 3                    ;   (bit 0 the DMA's direction)
SD_WRITE_PROTECTED = 5                 ; ERROR
NTB             = 5                    ; track buffers: an SD unit's; O:'s and P:'s, data and directory

.zeropage
CURDRV:         .res    1
TRACK:          .res    2
SECTOR:         .res    2
DMAADR:         .res    2
DMABANK:        .res    1
CURBANK:        .res    1              ; SELMEM's
XPEND:          .res    1              ; XMOVE's banks wait for the next MOVE
XSRC:           .res    1
XDST:           .res    1
DOP:            .res    1              ; 0 READ, 1 WRITE (SDIO: 2 UNMOUNT, 3 MOUNT)
LBA:            .res    3
SIDES:          .res    8              ; COPY's source, then destination: bank, address (3)
CNT:            .res    2
SAVED:          .res    10             ; SIDES and CNT, for COPY's second pass
DRY:            .res    1              ; bit 7: COPY's first pass, which only checks
TMO:            .res    1              ; SDIDLE's and SDDATA's outer count
SRCP:           .res    3              ; DMAGO's source, destination and count
DSTP:           .res    3
DCOUNT:         .res    2
DVEC:           .res    1              ; a vector's devices, shifted out
DCHAR:          .res    1
TYEAR:          .res    1              ; years from 2000
TMONTH:         .res    1
TDATE:          .res    1
TDAYS:          .res    2
TMP:            .res    2
SPTR:           .res    2

SIDE_S          = 0
SIDE_D          = 4

.segment "SYSBSS"
DRVRO:          .res    SD_DRIVES      ; bit 1: the unit is read-only
DRVCHG:         .res    1              ; bit n: unit n + 1 changed at a rescan (the BDOS clears it)
DRVAPL:         .res    SD_DRIVES      ; bit 7: the unit is an apple-do drive
TBTAG:          .res    2 * NTB        ; each track buffer's drive + 1 (0: empty) and track
BUFIX:          .res    1              ; CURDRV's buffer: 0 an SD unit's, 1 O:'s, 2 P:'s, 3 and 4 their directory tracks'
TBMASK:         .res    2 * NTB        ; each buffer's file sectors that are good (an SD unit's: all)
VOUCH:          .res    1              ; O: or P: + 1 whose disk the BDOS checked in this call: its buffers stay
RETRY:          .res    1              ; APPLE1's: a partly read track's second try
AHALF:          .res    1              ; APPLEIO's half, 0 or 1
AK:             .res    1              ; its file's sector in the track; TRKLOAD's SD sector
UNITSAV:        .res    SD_DRIVES * 32 ; the units table as the last IDENTIFY gave it
TESC:           .res    1              ; TTYOUT: 0, 1 after ESC, 2 after ESC Y, 3 after its row
TROW:           .res    1              ; ESC Y's row, from 0

.segment "BIOS"

; ---------------------------------------------------------------------------
; Character devices

CONST:          LDA     SCB_CIVEC + 1
ANYIST:         STA     DVEC           ; any device in the vector with input
                LDX     #0
@next:          ASL     DVEC
                BCC     @skip
                JSR     DIST
                BNE     @yes
@skip:          INX
                CPX     #DEV_COUNT
                BNE     @next
                LDA     #0
                RTS
@yes:           LDA     #$FF
                RTS

AUXIST:         LDA     SCB_AIVEC + 1
                BRA     ANYIST

CONIN:          LDA     SCB_CIVEC + 1
ANYIN:          STA     TMP            ; the first device in the vector with input
                AND     #VEC_CRT | VEC_TTY
                BNE     @poll
                LDA     #$1A           ; none has input (LPT alone, or none): ^Z
                RTS
@poll:          LDA     TMP
                STA     DVEC
                LDX     #0
@next:          ASL     DVEC
                BCC     @skip
                JSR     DIST
                BNE     @got
@skip:          INX
                CPX     #DEV_COUNT
                BNE     @next
                BRA     @poll
@got:           JMP     DIN

AUXIN:          LDA     SCB_AIVEC + 1
                BRA     ANYIN

CONOUT:         LDX     SCB_COVEC + 1
ANYOUT:         STX     DVEC           ; A to every device in the vector
                STA     DCHAR
                LDX     #0
@next:          ASL     DVEC
                BCC     @skip
                LDA     DCHAR
                JSR     DOUT
@skip:          INX
                CPX     #DEV_COUNT
                BNE     @next
                RTS

LIST:           LDX     SCB_LOVEC + 1
                BRA     ANYOUT

AUXOUT:         LDX     SCB_AOVEC + 1
                BRA     ANYOUT

CONOST:         LDA     SCB_COVEC + 1
ALLOST:         STA     DVEC           ; every device in the vector ready
                LDX     #0
@next:          ASL     DVEC
                BCC     @skip
                JSR     DOST
                BEQ     @no
@skip:          INX
                CPX     #DEV_COUNT
                BNE     @next
                LDA     #$FF
                RTS
@no:            LDA     #0
                RTS

LISTST:         LDA     SCB_LOVEC + 1
                BRA     ALLOST

AUXOST:         LDA     SCB_AOVEC + 1
                BRA     ALLOST

; Device X's input status, input, output (of A) and output status. Each
; keeps X; the statuses set Z by A.
DIST:           PHX
                TXA
                ASL     A
                TAX
                JSR     @go
                PLX
                CMP     #0
                RTS
@go:            JMP     (ISTTAB,X)
DIN:            PHX
                TAY
                TXA
                ASL     A
                TAX
                JSR     @go
                PLX
                RTS
@go:            JMP     (INTAB,X)
DOUT:           PHX
                TAY
                TXA
                ASL     A
                TAX
                TYA
                JSR     @go
                PLX
                RTS
@go:            JMP     (OUTTAB,X)
DOST:           PHX
                TXA
                ASL     A
                TAX
                JSR     @go
                PLX
                CMP     #0
                RTS
@go:            JMP     (OSTTAB,X)

ISTTAB:         .word   CRTIST, TTYIST, NOIN
INTAB:          .word   CRTIN, TTYIN, LPTIN
OUTTAB:         .word   CRTOUT, TTYOUT, LPTOUT
OSTTAB:         .word   READY, READY, READY

READY:          LDA     #$FF
                RTS
NOIN:           LDA     #0
                RTS
LPTIN:          LDA     #$1A           ; no input: end of file
LPTOUT:         RTS

; The USB keyboard: KEY, cooked, the strobe in bit 7. Lowercase stays.
CRTIST:         LDA     USB_ID
                CMP     #USB_ID_V1
                BNE     NOIN
                LDA     USB_KEY
                BPL     NOIN
                BRA     READY
CRTIN:          JSR     CRTIST
                BEQ     CRTIN
                LDA     USB_KEY
                BIT     USB_KEYCLR     ; any access clears the strobe
                AND     #$7F
                RTS

; The serial line: the 6551 stand-in.
TTYIST:         LDA     ACIA_STATUS
                AND     #$08
                BEQ     NOIN
                BRA     READY
TTYIN:          JSR     TTYIST
                BEQ     TTYIN
                LDA     ACIA_DATA
                RTS
; The screen's VT52 subset (crt.s) goes out on the serial line as ANSI, as a
; PC's terminal program and the WebUI's Terminal read it: ESC A-D, H, J and K
; as ESC [ and the same letter; ESC E (clear) as ESC [H ESC [2J; ESC Y row
; col as ESC [row;colH, counted from 1 there, a row or column past the
; screen's edge its last, as crt.s has it. Other ESC pairs are dropped, as
; the screen drops them. Uses A, X, Y.
TTYOUT:         LDX     TESC
                BNE     @esc
                CMP     #$1B
                BEQ     @begin
                JMP     TTYPUT
@begin:         LDA     #1
                STA     TESC
                RTS
@esc:           CPX     #1
                BNE     @arg
                STZ     TESC
                CMP     #'E'
                BEQ     @clear
                CMP     #'Y'
                BEQ     @move
                LDX     #TTYKEYS_LEN - 1
@find:          CMP     TTYKEYS,X
                BEQ     @csi
                DEX
                BPL     @find
                RTS                    ; not one of ours
@csi:           JMP     CSI
@clear:         LDA     #'H'
                JSR     CSI
                JSR     CSIOPEN
                LDA     #'2'
                JSR     TTYPUT
                LDA     #'J'
                BRA     TTYPUT
@move:          LDA     #2
                STA     TESC
                RTS
@arg:           SEC                    ; ESC Y's row, then its column
                SBC     #32
                CPX     #2
                BNE     @col
                CMP     #TTY_ROWS
                BCC     @row
                LDA     #TTY_ROWS - 1
@row:           STA     TROW
                LDA     #3
                STA     TESC
                RTS
@col:           STZ     TESC
                CMP     #TTY_COLS
                BCC     @at
                LDA     #TTY_COLS - 1
@at:            PHA
                JSR     CSIOPEN
                LDA     TROW
                INC     A
                JSR     TTYDEC
                LDA     #';'
                JSR     TTYPUT
                PLA
                INC     A
                JSR     TTYDEC
                LDA     #'H'
                BRA     TTYPUT

TTYKEYS:        .byte   "ABCDHJK"
TTYKEYS_LEN     = * - TTYKEYS
TTY_ROWS        = 30                   ; crt.s's screen
TTY_COLS        = 80

; ESC [ and then A; CSIOPEN only the ESC [.
CSI:            PHA
                JSR     CSIOPEN
                PLA
                BRA     TTYPUT
CSIOPEN:        LDA     #$1B
                JSR     TTYPUT
                LDA     #'['
                BRA     TTYPUT

; A, 1-99, in decimal.
TTYDEC:         LDX     #'0' - 1       ; the tens
                SEC
@ten:           INX
                SBC     #10
                BCS     @ten
                ADC     #10 + '0'      ; the units (the carry is clear)
                PHA
                CPX     #'0'
                BEQ     @units
                TXA
                JSR     TTYPUT
@units:         PLA
                ; fall through

; The W65C51N's transmit-empty flag is stuck, so wait out the character, as
; the ROM's ECHO does: 3 x 256 x 5 cycles, 3,840, over 10 bits at 19,200.
; Uses A and Y.
TTYPUT:         STA     ACIA_DATA
                LDY     #3
@outer:         LDA     #0
@inner:         DEC     A
                BNE     @inner
                DEY
                BNE     @outer
                RTS

DEVINI:         CMP     #DEV_CRT
                BNE     @tty
                JMP     CRTINIT
@tty:           CMP     #DEV_TTY
                BNE     @done
                LDA     #$1F           ; 8-N-1, 19,200
                STA     ACIA_CTRL
                LDA     #$0B           ; no parity, no echo, no interrupts
                STA     ACIA_CMD
@done:          RTS

DEVTBL:         LDA     #<CHRTBL
                LDX     #>CHRTBL
                RTS

; CP/M 3's character device table: a name of 6, the mode (bit 0 input, 1
; output, 3 serial), the baud rate's code (15: 19,200), then a 0.
CHRTBL:         .byte   "CRT   ", $03, 0
                .byte   "TTY   ", $0B, 15
                .byte   "LPT   ", $02, 0
                .byte   0

; ---------------------------------------------------------------------------
; Drives

HOME:           STZ     TRACK
                STZ     TRACK + 1
                RTS
SETTRK:         STA     TRACK
                STX     TRACK + 1
                RTS
SETSEC:         STA     SECTOR
                STX     SECTOR + 1
                RTS
SETDMA:         STA     DMAADR
                STX     DMAADR + 1
                RTS
SECTRN:         RTS                    ; A/X back as they came
MULTIO:
FLUSH:          LDA     #0
                RTS
SETBNK:         JSR     BANK
                STA     DMABANK
                RTS
SELMEM:         JSR     BANK
                STA     CURBANK
                RTS
XMOVE:          JSR     XBANK          ; C, the source, then B, the destination
                STA     XSRC
                TXA
                JSR     XBANK
                STA     XDST
                LDA     #1
                STA     XPEND
                RTS
XBANK:          CMP     #PHYS + $10    ; XMOVE's: PHYS + A16-A19 stays, a physical address
                BCS     BANK
                CMP     #PHYS
                BCS     BANKED
BANK:           CMP     #BANK_PROG     ; 1 stays 1, the rest are 0
                BEQ     BANKED
                LDA     #BANK_SYS
BANKED:         RTS

DRVTBL:         LDA     #<DRVTAB
                LDX     #>DRVTAB
                RTS

SELDSK:         CMP     #16
                BCS     @none
                PHA
                TXA
                AND     #1
                BNE     @known
                PLA
                PHA
                CMP     #SD_DRIVES
                BCS     @known
                JSR     UNITS          ; a first select: what is mounted now
                BCC     @known
                PLA                    ; no units table: no drive this time
                BRA     @none
@known:         PLA
                ASL     A
                TAY
                LDA     DRVTAB,Y
                ORA     DRVTAB + 1,Y
                BEQ     @none
                TYA
                LSR     A
                STA     CURDRV
                LDA     DRVTAB,Y
                LDX     DRVTAB + 1,Y
                RTS
@none:          LDA     #0
                TAX
                RTS

UNITTAB         = SECBUF + $40         ; IDENTIFY's: 32 bytes a unit, 1-7

; IDENTIFY's units table into DRVTAB, DPHs and DRVRO, carry clear; each
; unit whose entry isn't the last table's (its size, flags or file's name)
; set in DRVCHG, for the BDOS to log its drive out, so a rescan leaves a
; drive whose unit is as it was alone. A card that isn't there or not
; started up, or doesn't answer within about a second, changes nothing,
; carry set; one still busy is reset (CTRL's RESET), as SDIO resets it.
UNITS:          JSR     CARDOK
                BCS     @none
                JSR     SDIDLE         ; a command written while BUSY is ignored
                BCS     @stuck
                STZ     SD_CTRL
                STZ     SD_UNIT
                LDA     #SD_IDENTIFY
                STA     SD_CMD
                JSR     SDDATA
                BCC     @read
@stuck:         JSR     SDRESET        ; abandoned, as SDIO does, so the next one can go
@none:          SEC                    ; failed: the drives as they were
                RTS
@read:          STZ     TBTAG          ; the track buffer: whatever is mounted now
                LDY     #0
@lo:            LDA     SD_DATA
                STA     SECBUF,Y
                INY
                BNE     @lo
@hi:            LDA     SD_DATA
                STA     SECBUF + $100,Y
                INY
                BNE     @hi
                JSR     SDIDLE         ; and IDENTIFY's end, before anything else
                LDA     #1             ; each unit's entry against the last table's:
                STA     SPTR           ; SPTR its bit, SPTR + 1 any byte not the same
                LDY     #0
@entry:         STZ     SPTR + 1
                LDX     #32
@byte:          LDA     UNITTAB,Y
                CMP     UNITSAV,Y
                BEQ     @same
                STA     UNITSAV,Y
                LDA     #1
                STA     SPTR + 1
@same:          INY
                DEX
                BNE     @byte
                LDA     SPTR + 1
                BEQ     @kept
                LDA     SPTR
                ORA     DRVCHG
                STA     DRVCHG
@kept:          ASL     SPTR
                CPY     #SD_DRIVES * 32
                BNE     @entry
                LDX     #2 * SD_DRIVES - 1
@clear:         STZ     DRVTAB,X
                DEX
                BPL     @clear
                LDX     #0             ; X the drive, Y its entry
                LDY     #0
@unit:          STZ     DRVAPL,X
                LDA     UNITTAB + 4,Y
                STA     DRVRO,X
                LSR     A              ; bit 0: mounted
                BCC     @next
                LDA     UNITTAB + 2,Y  ; 280: $00000118, apple-do
                ORA     UNITTAB + 3,Y
                BNE     @own
                LDA     UNITTAB,Y
                CMP     #<SD_SECTORS_APPLE
                BNE     @own
                LDA     UNITTAB + 1,Y
                CMP     #>SD_SECTORS_APPLE
                BNE     @own
                LDA     #$80
                STA     DRVAPL,X
                LDA     #<DPBAPL
                STA     TMP
                LDA     #>DPBAPL
                BRA     @set
@own:           LDA     UNITTAB,Y      ; 16,384: $00004000
                ORA     UNITTAB + 3,Y
                BNE     @next
                LDA     UNITTAB + 1,Y
                CMP     #>SD_SECTORS_8M
                BNE     @big
                LDA     UNITTAB + 2,Y
                BNE     @next
                LDA     #<DPB8M
                STA     TMP
                LDA     #>DPB8M
                BRA     @set
@big:           CMP     #0             ; 65,536: $00010000
                BNE     @next
                LDA     UNITTAB + 2,Y
                CMP     #1
                BNE     @next
                LDA     #<DPB32M
                STA     TMP
                LDA     #>DPB32M
@set:           STA     TMP + 1
                PHY
                TXA                    ; DPHS + X x 25
                ASL     A
                TAY
                LDA     DPHADDR,Y
                STA     DRVTAB,Y
                STA     SPTR
                LDA     DPHADDR + 1,Y
                STA     DRVTAB + 1,Y
                STA     SPTR + 1
                LDY     #DPH_DPB
                LDA     TMP
                STA     (SPTR),Y
                INY
                LDA     TMP + 1
                STA     (SPTR),Y
                PLY
@next:          TYA
                CLC
                ADC     #32
                TAY
                INX
                CPX     #SD_DRIVES
                BEQ     @done
                JMP     @unit
@done:          CLC
                RTS

; Carry clear if the card is there and started up.
CARDOK:         LDA     SD_ID
                CMP     #SD_ID_V1
                BNE     @no
                LDA     SD_STATUS
                AND     #SD_PRESENT | SD_READY
                CMP     #SD_PRESENT | SD_READY
                BNE     @no
                CLC
                RTS
@no:            SEC
                RTS

READ:           STZ     DOP
                BRA     DISKIO
WRITE:          LDA     #1
                STA     DOP
DISKIO:         LDA     CURDRV
                CMP     #DRV_O         ; O: and P:: the Disk II's, apple-do
                BCS     @apple
                CMP     #SD_DRIVES
                BCS     @own
                TAX
                LDA     DRVAPL,X
                BPL     @own
@apple:         JMP     APPLEIO
@own:           LDA     SECTOR + 1     ; LBA = track x 16 + sector, sector 0-15
                BNE     @error
                LDA     SECTOR
                CMP     #SPT_PHYS
                BCS     @error
                LDA     TRACK
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     SECTOR
                STA     LBA
                LDA     TRACK + 1
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                STA     LBA + 1
                LDA     TRACK
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     LBA + 1
                STA     LBA + 1
                LDA     TRACK + 1
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                STA     LBA + 2
                LDA     CURDRV
                CMP     #DRV_M
                BEQ     MDISK
                CMP     #SD_DRIVES
                BCS     @error
                ASL     A
                TAY
                LDA     DRVTAB,Y
                ORA     DRVTAB + 1,Y
                BEQ     @error
                LDA     DOP
                BEQ     @rw
                LDX     CURDRV
                LDA     DRVRO,X
                AND     #2
                BEQ     @rw
                LDA     #2             ; read-only, as CP/M 3's write-protect
                RTS
@error:         LDA     #1
                RTS
@rw:            JSR     CROSSES
                BCS     BOUNCE
                LDA     DOP            ; straight to or from the buffer: the
                BNE     @out           ; destination of a READ, the source of a WRITE
                JSR     DMASIDE2
                BRA     @phys
@out:           JSR     DMASIDE
@phys:          JSR     PHYSOF
                BCS     @error         ; the I/O hole, flash, or read-only
                JMP     SDIO

; READ or WRITE of an SD sector through SECBUF.
BOUNCE:         LDA     DOP
                BEQ     @read
                JSR     DMASIDE        ; the buffer to SECBUF, then SECBUF out
                JSR     TOSECBUF
                JSR     COPY512
                BCS     @error
                JSR     SECPH
                JMP     SDIO
@read:          JSR     SECPH          ; the sector to SECBUF, then on
                JSR     SDIO
                BNE     @done
                JSR     FROMSECBUF
                JSR     DMASIDE2
                JSR     COPY512
                BCS     @error
                LDA     #0
@done:          RTS
@error:         LDA     #1
                RTS

SECPH:          LDA     #<SECBUF_PHYS
                STA     SRCP
                LDA     #>SECBUF_PHYS
                STA     SRCP + 1
                LDA     #^SECBUF_PHYS
                STA     SRCP + 2
                RTS

; M:: 512 bytes between $C0000 + LBA x 512 and the buffer, by channel 3.
MDISK:          LDA     LBA + 1
                ORA     LBA + 2
                BNE     @error         ; 256 sectors
                LDA     DOP
                BEQ     @read
                JSR     DMASIDE        ; the buffer is the source
                LDX     #SIDE_D
                JSR     MSIDE
                BRA     @go
@read:          LDX     #SIDE_S
                JSR     MSIDE
                JSR     DMASIDE2
@go:            JSR     COPY512
                BCS     @error
                LDA     #0
                RTS
@error:         LDA     #1
                RTS

; Copy side X: M:'s sector at LBA.
MSIDE:          LDA     #PHYS
                STA     SIDES,X
                STZ     SIDES + 1,X
                LDA     LBA
                ASL     A
                STA     SIDES + 2,X
                LDA     #^MDISK_BASE
                ADC     #0
                STA     SIDES + 3,X
                RTS

; The SETDMA buffer as copy side S (DMASIDE) or D (DMASIDE2).
DMASIDE:        LDX     #SIDE_S
                BRA     DMAS
DMASIDE2:       LDX     #SIDE_D
DMAS:           LDA     DMABANK
                STA     SIDES,X
                LDA     DMAADR
                STA     SIDES + 1,X
                LDA     DMAADR + 1
                STA     SIDES + 2,X
                RTS
TOSECBUF:       LDX     #SIDE_D
                BRA     SECSIDE
FROMSECBUF:     LDX     #SIDE_S
SECSIDE:        LDA     #PHYS
                STA     SIDES,X
                LDA     #<SECBUF_PHYS
                STA     SIDES + 1,X
                LDA     #>SECBUF_PHYS
                STA     SIDES + 2,X
                LDA     #^SECBUF_PHYS
                STA     SIDES + 3,X
                RTS
COPY512:        STZ     CNT
                LDA     #2
                STA     CNT + 1
                JMP     COPY

; Carry set if the SETDMA buffer crosses a window ($xE01-$xFFF in it).
CROSSES:        LDA     DMAADR + 1
                AND     #$3F
                CMP     #$3E
                BCC     @no
                BNE     @yes
                LDA     DMAADR
                BEQ     @no
@yes:           SEC
                RTS
@no:            CLC
                RTS

; One sector between the SD card (CURDRV's unit, LBA) and physical memory at
; SRCP, by DMA channel 0, as DOP says. A = 0, 1 an error, 2 the card is
; write-protected; Z by A. It waits for the card to be idle first (a command
; written while BUSY is ignored), and gives a command about a second to end
; (an SD card may stay busy hundreds of milliseconds programming a write);
; then the card is reset, its units staying mounted. A command that ends
; without its 512 bytes all moved (channel 0 short of its terminal count) is
; an error too.
SDIO:           JSR     CARDOK
                BCC     @card
                LDA     #1
                RTS
@card:          JSR     SDIDLE
                BCC     @idle
                JSR     SDRESET        ; still busy with something else: give up
                LDA     #1
                RTS
@idle:          LDA     #$01           ; channel 0, programmed stopped, its
                STA     DMA_ABORT      ; terminal count cleared
                STA     DMA_STATUS
                LDX     #2
@addr:          LDA     SRCP,X
                STA     DMA0_ADDR,X
                DEX
                BPL     @addr
                STZ     DMA0_COUNT
                LDA     #>512
                STA     DMA0_COUNT + 1
                LDA     DOP
                AND     #1             ; DOP's bit 0: the direction
                ASL     A
                ASL     A
                ORA     #DMA_DEMAND_IN ; demand; DIR 00 card to memory, 01 memory to card
                STA     DMA0_MODE
                LDA     DMA_CONTROL
                PHA
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
                LDA     #DMA_ENABLE
                STA     DMA0_CTRL
                LDX     CURDRV
                INX
                STX     SD_UNIT
                LDX     #2
@lba:           LDA     LBA,X
                STA     SD_LBA,X
                DEX
                BPL     @lba
                STZ     SD_LBA + 3
                LDA     #1
                STA     SD_COUNT
                STZ     SD_COUNT + 1
                LDA     #SD_CTRL_DMA
                STA     SD_CTRL
                LDX     DOP
                LDA     SDCMDS,X
                STA     SD_CMD
SDWAIT:         JSR     SDIDLE
                BCC     SDEND
                JSR     SDRESET        ; abandon it
SDEND:          LDA     DMA_STATUS     ; channel 0's terminal count, in bit 0
                TAY
                STZ     SD_CTRL
                LDA     #$01
                STA     DMA_ABORT
                PLA
                STA     DMA_CONTROL
                STZ     SD_UNIT
                LDA     SD_STATUS
                BMI     @error         ; still busy: given up
                AND     #SD_ERR
                BEQ     @ended
                LDA     SD_ERROR
                CMP     #SD_WRITE_PROTECTED
                BNE     @error
                LDA     #2
                RTS
@ended:         TYA                    ; no error: were all 512 bytes moved?
                LSR     A
                BCS     @moved
                LDA     DOP            ; UNMOUNT moves none
                CMP     #DOP_UNMOUNT
                BNE     @error
@moved:         LDA     #0
                RTS
@error:         LDA     #1
                RTS

SDCMDS:         .byte   SD_READ, SD_WRITE, SD_UNMOUNT, SD_MOUNT ; by DOP

; The card's MOUNT of SECBUF's sector (A = DOP_MOUNT), or UNMOUNT of unit X
; + 1 (A = DOP_UNMOUNT), for the BDOS's function 74, which calls it
; directly with W2 on this block (no jump table entry: programs reach it
; only by 74). As SDIO: A = 0, or 1 failed (the card's ERROR says why,
; but for no card or a timeout). The BIOS's drive (CURDRV) is then X's.
SDMNT:          STA     DOP
                STX     CURDRV
                STZ     TBTAG          ; what the units hold changes
                JSR     SECPH
                JMP     SDIO

SDRESET:        LDA     #SD_CTRL_RESET ; the command abandoned; units stay mounted
                STA     SD_CTRL
                STZ     SD_CTRL
                RTS

; Carry clear once the card isn't BUSY; set if it still is after about a
; second: 11 cycles a pass, 655,360 passes. Uses A, X, Y.
SDIDLE:         LDA     #10
                STA     TMO
                LDX     #0
                LDY     #0
@wait:          BIT     SD_STATUS      ; N: BUSY
                BPL     @idle
                DEX
                BNE     @wait
                DEY
                BNE     @wait
                DEC     TMO
                BNE     @wait
                SEC
                RTS
@idle:          CLC
                RTS

; Carry clear once the command has a sector for DATA; set if it ended
; instead, or after about a second (17 cycles a pass). Uses A, X, Y.
SDDATA:         LDA     #7
                STA     TMO
                LDX     #0
                LDY     #0
@wait:          LDA     SD_STATUS
                BIT     #SD_DATA_READY
                BNE     @ready
                BIT     #SD_BUSY
                BEQ     @ended
                DEX
                BNE     @wait
                DEY
                BNE     @wait
                DEC     TMO
                BNE     @wait
@ended:         SEC
                RTS
@ready:         CLC
                RTS

; ---------------------------------------------------------------------------
; apple-do drives (the header's "An apple-do drive")

; READ or WRITE (DOP) of CURDRV's TRACK (0-34) and SECTOR (0-7). A = 0, 1
; an error, 2 read-only (the units table's flag, or the card's); Z by A.
APPLEIO:        STZ     BUFIX          ; an SD unit's: straight to the layer
                LDA     CURDRV
                CMP     #DRV_O
                BCC     APPLE1
                SBC     #DRV_O - 1     ; O: 1, P: 2 (the carry set)
                STA     BUFIX
                DEC     A
                TAX
                JSR     D2START        ; motor on; carry set: it wasn't turning
                BCC     @turning
                LDX     CURDRV         ; checked by the BDOS in this call: kept
                INX
                CPX     VOUCH
                BEQ     @turning
                JSR     TBEMPTY2       ; it may be another disk now
@turning:       LDA     TRACK          ; the directory's track: its own buffer
                CMP     #DIR_TRACK
                BNE     @data
                LDA     BUFIX
                CLC
                ADC     #2
                STA     BUFIX
@data:          JSR     APPLE1
                PHA
                JSR     D2STOP         ; off: the card keeps it turning a second
                PLA
                CMP     #0
                RTS

; This drive's track buffer emptied (BUFIX's). Keeps A.
TBEMPTY:        PHA
                LDA     BUFIX
                ASL     A
                TAX
                STZ     TBTAG,X
                PLA
                RTS
; Both of O:'s or P:'s (BUFIX 1 or 2). Keeps A.
TBEMPTY2:       PHA
                LDA     BUFIX
                ASL     A
                TAX
                STZ     TBTAG,X
                STZ     TBTAG + 4,X
                PLA
                RTS

; MEDIA, for the BDOS (bdosdisk.s, its directory checksums; called with W2
; on this block, as SDMNT): X = O: or P:. Carry set if its disk isn't
; turning, so may have been changed: its track buffer is emptied.
MEDIA:          TXA
                SEC
                SBC     #DRV_O - 1
                STA     BUFIX
                DEC     A
                TAX
                JMP     D2MEDIA        ; the buffers kept: the BDOS checks, then forgets

; FORGET, for the BDOS (as MEDIA): X = O: or P:, its track buffer emptied,
; so the next READ is from the disk (its directory checked past the buffer).
FORGET:         TXA
                SEC
                SBC     #DRV_O - 1
                STA     BUFIX
                JMP     TBEMPTY2
; FORGETD: only its directory track's buffer, so a check of the directory
; reads the disk and the data track's buffer stays.
FORGETD:        TXA
                SEC
                SBC     #DRV_O - 3     ; O: 3, P: 4
                STA     BUFIX
                JMP     TBEMPTY

; The layer itself: READ or WRITE of CURDRV's sector, BUFIX its buffer.
APPLE1:         LDA     SECTOR + 1
                ORA     TRACK + 1
                BNE     @bad
                LDA     SECTOR
                CMP     #SPT_APPLE
                BCS     @bad
                LDA     TRACK
                CMP     #APPLE_TRACKS
                BCS     @bad
                LDA     DOP
                BEQ     @load
                LDX     CURDRV
                CPX     #SD_DRIVES     ; O: and P:: the disk's own write-protect
                BCS     @load
                LDA     DRVRO,X
                AND     #2
                BEQ     @load
                LDA     #2             ; read-only, as CP/M 3's write-protect
                RTS
@bad:           LDA     #1             ; no such sector
                RTS
@load:          LDA     #2             ; a READ's halves missing: the track read again once
                STA     RETRY
@again:         JSR     TRKLOAD
                BEQ     @loaded
                RTS                    ; SDIO's error, or the disk's
@loaded:        LDA     DOP            ; a READ: both halves good in the buffer?
                BNE     @ok
                STZ     AHALF
                JSR     HALFOK
                BCS     @missing
                INC     AHALF
                JSR     HALFOK
                BCC     @ok
@missing:       JSR     TBEMPTY        ; not kept as missing: read again, or the next READ's
                DEC     RETRY
                BNE     @again
                LDA     #1
                RTS
@ok:            STZ     AHALF
@half:          JSR     AKSET          ; AK: this half's sector in the file's track
                LDX     #SIDE_S        ; the track buffer's side: the source of a READ
                LDA     DOP
                BEQ     @tb
                LDX     #SIDE_D        ; the destination of a WRITE
@tb:            LDA     #PHYS
                STA     SIDES,X
                STZ     SIDES + 1,X
                JSR     BUFPAGE        ; the buffer's page + AK
                STA     SIDES + 2,X
                LDA     #^TRKBUF_PHYS  ; (the directory tracks' too)
                STA     SIDES + 3,X
                TXA                    ; and the SETDMA buffer the other side,
                EOR     #SIDE_S ^ SIDE_D
                TAX
                JSR     DMAS
                LDA     AHALF          ; this half 256 bytes on
                CLC
                ADC     SIDES + 2,X
                STA     SIDES + 2,X
                STZ     CNT
                LDA     #1
                STA     CNT + 1
                JSR     COPY
                BCS     @fail          ; the I/O hole, flash, read-only
                INC     AHALF
                LDA     AHALF
                CMP     #2
                BNE     @half
                LDA     DOP
                BEQ     @done          ; A = 0
                STZ     AHALF          ; a WRITE: the SD sector of each half out
@out:           JSR     AKSET
                LDX     BUFIX
                BNE     @disk2
                LSR     A              ; the SD sector in the track
                JSR     TRKSEC
                JSR     SDIO
                BNE     @failed
                BRA     @wrote
@disk2:         STA     D2FS           ; the Disk II's sector of the track
                JSR     D2PAGE
                LDA     TRACK
                JSR     D2WSEC
                BNE     @failed
                LDX     AK             ; that sector good in the buffer now
                JSR     MASKBIT
                ORA     TBMASK,Y
                STA     TBMASK,Y
@wrote:
                INC     AHALF
                LDA     AHALF
                CMP     #2
                BNE     @out
                LDA     #0
@done:          RTS
@fail:          LDA     #1
@failed:        JSR     TBEMPTY        ; the buffer may not be the card's now
                CMP     #0
                RTS

; Carry clear if this half's sector (AKSET) is good in BUFIX's buffer.
HALFOK:         JSR     AKSET
                TAX
                JSR     MASKBIT
                AND     TBMASK,Y
                CLC
                BNE     @good
                SEC
@good:          RTS
; X = a file sector (0-15): A its bit in TBMASK's byte Y (BUFIX's).
MASKBIT:        LDA     BUFIX
                ASL     A
                STA     TMP
                TXA
                LSR     A
                LSR     A
                LSR     A
                CLC
                ADC     TMP
                TAY
                TXA
                AND     #7
                TAX
                LDA     #1
@shift:         DEX
                BMI     @bit
                ASL     A
                BRA     @shift
@bit:           RTS

; A = the track buffer's physical page (bits 8-15) for BUFIX's buffer, + AK.
BUFPAGE:        LDX     BUFIX
                LDA     TBPAGE,X
                CLC
                ADC     AK
                RTS
; Y = the Disk II's buffer's page in W1's view ($4000 on), D2BLK its block.
D2PAGE:         LDX     BUFIX
                LDA     TBPAGE,X
                AND     #$3F
                ORA     #$40
                TAY
                LDA     TBBLK,X
                STA     D2BLK
                RTS
; The buffers: an SD unit's, O:'s, P:'s, O:'s directory track's, P:'s.
TBPAGE:         .byte   >TRKBUF_PHYS, >(TRKBUF_PHYS + $1000), >(TRKBUF_PHYS + $2000)
                .byte   >DIRBUF_PHYS, >(DIRBUF_PHYS + $1000)
TBBLK:          .byte   TRKBUF_PHYS >> 14, TRKBUF_PHYS >> 14, TRKBUF_PHYS >> 14
                .byte   DIRBUF_PHYS >> 14, DIRBUF_PHYS >> 14
.assert * - TBBLK = NTB, error, "a block for each track buffer"
.assert ^TRKBUF_PHYS = ^DIRBUF_PHYS, error, "the track buffers' A16-A19 one value (BUFBANK)"

; AK = A = the file's sector of the track for CP/M's sector 2 x SECTOR +
; AHALF, by the skew.
AKSET:          LDA     SECTOR
                ASL     A
                ORA     AHALF
                TAX
                LDA     SKEW,X
                STA     AK
                RTS

; cpmtools's apple-do skew: CP/M's sector n of a track is the .dsk's SKEW[n].
SKEW:           .byte   0, 6, 12, 3, 9, 15, 14, 5, 11, 2, 8, 7, 13, 4, 10, 1

; The track buffer holding CURDRV's TRACK: read from the card, 8 SD sectors,
; unless it holds it already. A = 0, or SDIO's error (the buffer then
; empty); Z by A.
TRKLOAD:        LDA     BUFIX
                ASL     A
                TAY                    ; Y: the buffer's tag
                LDX     CURDRV
                INX
                TXA
                CMP     TBTAG,Y
                BNE     @read
                LDA     TRACK
                CMP     TBTAG + 1,Y
                BNE     @read
                LDA     #0
                RTS
@read:          LDA     #0
                STA     TBTAG,Y
                LDA     BUFIX
                BEQ     @sd
                JSR     D2PAGE         ; O: or P:: the whole track off the disk
                LDA     TRACK
                JSR     D2TRACK
                BNE     @done
                LDA     BUFIX
                ASL     A
                TAY
                LDA     D2GOTF         ; its good sectors
                STA     TBMASK,Y
                LDA     D2GOTF + 1
                STA     TBMASK + 1,Y
                BRA     @tag
@sd:            LDA     DOP
                PHA
                STZ     DOP
                STZ     AK
@sector:        LDA     AK
                JSR     TRKSEC
                JSR     SDIO
                BNE     @fail
                INC     AK
                LDA     AK
                CMP     #SPT_APPLE
                BNE     @sector
                PLA
                STA     DOP
                LDY     #0
                LDA     #$FF           ; an SD unit's: all good
                STA     TBMASK
                STA     TBMASK + 1
@tag:           LDX     CURDRV
                INX
                TXA
                STA     TBTAG,Y
                LDA     TRACK
                STA     TBTAG + 1,Y
                LDA     #0
@done:          RTS
@fail:          TAX
                PLA
                STA     DOP
                TXA
                RTS

; SD sector A (0-7) of TRACK: LBA = TRACK x 8 + A, and SRCP its place in
; the track buffer, A x 512 on.
TRKSEC:         PHA
                STZ     LBA + 1
                STZ     LBA + 2
                LDA     TRACK
                ASL     A
                ROL     LBA + 1
                ASL     A
                ROL     LBA + 1
                ASL     A
                ROL     LBA + 1
                STA     LBA
                PLA
                PHA
                ORA     LBA
                STA     LBA
                STZ     SRCP
                PLA
                ASL     A
                CLC
                ADC     #>TRKBUF_PHYS
                STA     SRCP + 1
                LDA     #^TRKBUF_PHYS
                STA     SRCP + 2
                RTS
.assert <TRKBUF_PHYS = 0, error, "the track buffer starts a page"

; ---------------------------------------------------------------------------
; Copies

MOVE:           STA     CNT
                STX     CNT + 1
                PHA
                PHX
                LDA     CURBANK
                STA     SIDES + SIDE_S
                STA     SIDES + SIDE_D
                LDA     XPEND
                BEQ     @banks
                STZ     XPEND
                LDA     XSRC
                STA     SIDES + SIDE_S
                AND     #$0F           ; a physical bank's A16-A19 (PHYSOF reads it only then)
                STA     SIDES + SIDE_S + 3
                LDA     XDST
                STA     SIDES + SIDE_D
                AND     #$0F
                STA     SIDES + SIDE_D + 3
@banks:         LDA     BW1
                STA     SIDES + SIDE_S + 1
                LDA     BW1 + 1
                STA     SIDES + SIDE_S + 2
                LDA     BW2
                STA     SIDES + SIDE_D + 1
                LDA     BW2 + 1
                STA     SIDES + SIDE_D + 2
                JSR     COPY
                PLX
                PLA
                BCS     @refused       ; nothing moved: BW1 and BW2 as they were
                PHA
                CLC
                ADC     BW1
                STA     BW1
                TXA
                ADC     BW1 + 1
                STA     BW1 + 1
                PLA
                CLC
                ADC     BW2
                STA     BW2
                TXA
                ADC     BW2 + 1
                STA     BW2 + 1
                TAX
                LDA     BW2
                CLC                    ; moved: carry clear, whatever A/X are
                RTS
@refused:       LDA     #$FF           ; carry set (COPY's)
                TAX
                RTS

; CNT bytes from side S to side D (SIDES): a bank of 0 or 1 is a CPU address
; through that bank's windows, PHYS a physical one. In chunks that cross no
; window, each one DMA copy. Carry set, and nothing copied, if a chunk can't
; be (PHYSOF): a first pass only checks them. Uses A, X, Y.
.assert CNT = SIDES + 8, error, "COPY saves SIDES and CNT together"
COPY:           LDX     #9
@save:          LDA     SIDES,X
                STA     SAVED,X
                DEX
                BPL     @save
                LDA     #$80
                STA     DRY
                JSR     CHUNKS
                BCS     @done
                LDX     #9
@back:          LDA     SAVED,X
                STA     SIDES,X
                DEX
                BPL     @back
                STZ     DRY
                JMP     CHUNKS
@done:          RTS

CHUNKS:
@loop:          LDA     CNT
                ORA     CNT + 1
                BEQ     @all
                LDA     CNT
                STA     DCOUNT
                LDA     CNT + 1
                STA     DCOUNT + 1
                LDX     #SIDE_S
                JSR     CLIP
                LDX     #SIDE_D
                JSR     CLIP
                LDX     #SIDE_D
                JSR     PHYSOF
                BCS     @done
                LDX     #2
@dst:           LDA     SRCP,X
                STA     DSTP,X
                DEX
                BPL     @dst
                LDX     #SIDE_S
                JSR     PHYSOF
                BCS     @done
                BIT     DRY
                BMI     @next
                JSR     DMAGO
@next:          LDX     #SIDE_S
                JSR     ADVANCE
                LDX     #SIDE_D
                JSR     ADVANCE
                SEC
                LDA     CNT
                SBC     DCOUNT
                STA     CNT
                LDA     CNT + 1
                SBC     DCOUNT + 1
                STA     CNT + 1
                BRA     @loop
@all:           CLC
@done:          RTS

; DCOUNT = at most what is left of side X's window.
CLIP:           LDA     SIDES,X
                BMI     @done          ; physical: no windows
                LDA     SIDES + 2,X
                AND     #$3F
                STA     TMP + 1
                SEC                    ; TMP = $4000 - the offset in the window
                LDA     #0
                SBC     SIDES + 1,X
                STA     TMP
                LDA     #$40
                SBC     TMP + 1
                STA     TMP + 1
                SEC
                LDA     TMP
                SBC     DCOUNT
                LDA     TMP + 1
                SBC     DCOUNT + 1
                BCS     @done          ; the window holds it all
                LDA     TMP
                STA     DCOUNT
                LDA     TMP + 1
                STA     DCOUNT + 1
@done:          RTS

; Side X += DCOUNT.
ADVANCE:        CLC
                LDA     SIDES + 1,X
                ADC     DCOUNT
                STA     SIDES + 1,X
                LDA     SIDES + 2,X
                ADC     DCOUNT + 1
                STA     SIDES + 2,X
                LDA     SIDES + 3,X
                ADC     #0
                STA     SIDES + 3,X
                RTS

; SRCP = side X's physical address; carry set if it has none the BIOS may
; use: an address in $C000-$C7FF (the I/O hole, in either bank's W3), a
; window on flash (NB_ROM), or the destination in a read-only window.
PHYSOF:         LDA     SIDES + 1,X
                STA     SRCP
                LDA     SIDES,X
                BPL     @window
                LDA     SIDES + 2,X
                STA     SRCP + 1
                LDA     SIDES + 3,X
                STA     SRCP + 2
                CLC
                RTS
@window:        LDA     SIDES + 2,X
                CMP     #>CODE_WINDOW
                BCS     @mem
                CMP     #$C0
                BCS     @no            ; the I/O hole
@mem:           ROL     A              ; the window: the address's top 2 bits
                ROL     A
                ROL     A
                AND     #3
                TAY
                LDA     SIDES,X
                BEQ     @system
                LDA     ST_WIN,Y
                BRA     @block
@system:        LDA     SYSWIN,Y
                CPY     #2
                BNE     @block
                LDA     CALLW2         ; W2: the caller's, through the jump table
@block:         BIT     #NB_ROM
                BNE     @no
                CPX     #SIDE_D
                BNE     @rw
                BIT     #NB_RO
                BNE     @no
@rw:            AND     #$3F           ; the block: A14-A19
                STA     TMP
                LSR     A
                LSR     A
                STA     SRCP + 2
                LDA     TMP
                ROR     A              ; its bottom 2 bits to A14-A15
                ROR     A
                ROR     A
                AND     #$C0
                STA     TMP
                LDA     SIDES + 2,X
                AND     #$3F
                ORA     TMP
                STA     SRCP + 1
                CLC
                RTS
@no:            SEC
                RTS

SYSWIN:         .byte   SYS_BLOCK, BDOS_BLOCK, BIOS_BLOCK, RES_BLOCK

; DCOUNT bytes (0: 65,536) from SRCP to DSTP, physical, by DMA channel 3, a
; block transfer, waited for, as the ROM's DMA3RUN: programmed stopped, the
; master enable on for it and CONTROL put back after; given up after about
; 0.1 s. Uses A, X, Y.
DMAGO:          LDA     #DMA_CH3
                STA     DMA_ABORT
                LDX     #2
@regs:          LDA     SRCP,X
                STA     DMA3_ADDR,X
                LDA     DSTP,X
                STA     DMA3_DEST,X
                DEX
                BPL     @regs
                LDA     DCOUNT
                STA     DMA3_COUNT
                LDA     DCOUNT + 1
                STA     DMA3_COUNT + 1
                LDA     #DMA_M2M
                STA     DMA3_MODE
                LDA     DMA_CONTROL
                PHA
                ORA     #DMA_MASTER
                STA     DMA_CONTROL
                LDA     #DMA_GO
                STA     DMA3_CTRL
                LDX     #0
                LDY     #0
@wait:          LDA     DMA3_CTRL
                LSR     A
                BCC     @done
                DEX
                BNE     @wait
                DEY
                BNE     @wait
                LDA     #DMA_CH3
                STA     DMA_ABORT
@done:          PLA
                STA     DMA_CONTROL
                RTS

; ---------------------------------------------------------------------------
; TIME: the north bridge's clock (hw/design.md, "Real-time clock") and the
; SCB's date (days from 1 January 1978, that day 1), hour, minute and second
; (BCD). The bridge holds 2000-2199 (its century bit: 21xx); OODOS's days go
; to 31 December 2155 (hw/oodos.md, "BIOS"): a clock past it is read as no
; time (carry set, the SCB left as it was), and a date past it, or before
; 2000, isn't set. Leap years are the DS3231's, every fourth, so the two
; always agree (2100 has a 29 February in both); the day of the week is
; worked out from the days, 1 January 1978 a Sunday (1).

DAYS_2000       = 8035                 ; the days before 1 January 2000
LAST_YEAR       = 155                  ; 2155, from 2000

TIME:           CMP     #0
                BEQ     TGET
                JSR     DAYS2DATE
                BCS     @done          ; before 2000 or past 2155: not set
                LDA     SCB_SEC
                STA     NB_SEC
                LDA     SCB_MIN
                STA     NB_MIN
                LDA     SCB_HOUR
                STA     NB_HOUR
                JSR     WEEKDAY
                STA     NB_DAY
                LDA     TDATE
                JSR     BIN2BCD
                STA     NB_DATE
                LDA     TYEAR
                LDX     #0
                CMP     #100
                BCC     @year
                SBC     #100
                LDX     #$80           ; the century bit: 21xx
@year:          STX     TMP + 1        ; BIN2BCD uses TMP
                JSR     BIN2BCD
                STA     NB_YEAR
                LDA     TMONTH
                JSR     BIN2BCD
                ORA     TMP + 1
                STA     NB_MONTH
                LDA     NB_RTCCTL      ; SET, keeping TIE; TICK written 0 stays
                AND     #NB_RTC_TIE
                ORA     #NB_RTC_SET
                STA     NB_RTCCTL
                LDX     #0
                LDY     #0
@wait:          LDA     NB_RTCCTL
                AND     #NB_RTC_BUSY
                BEQ     @done
                DEX
                BNE     @wait
                DEY
                BNE     @wait
@done:          RTS

TGET:           LDA     NB_SEC         ; latches the other six
                PHA
                LDA     NB_MIN
                PHA
                LDA     NB_HOUR
                AND     #$3F
                PHA
                LDA     NB_YEAR
                JSR     BCD2BIN
                STA     TYEAR
                LDA     NB_MONTH
                BPL     @month
                LDA     TYEAR
                CLC
                ADC     #100
                STA     TYEAR
                LDA     NB_MONTH
@month:         AND     #$1F
                JSR     BCD2BIN
                STA     TMONTH
                LDA     NB_DATE
                JSR     BCD2BIN
                STA     TDATE
                LDA     TYEAR
                CMP     #LAST_YEAR + 1
                BCS     @far
                JSR     DATE2DAYS
                PLA
                STA     SCB_HOUR
                PLA
                STA     SCB_MIN
                PLA
                STA     SCB_SEC
                CLC
                RTS
@far:           PLA                    ; past 2155: the SCB's days can't say it
                PLA
                PLA
                SEC
                RTS

; SCB_DATE from TYEAR, TMONTH and TDATE.
DATE2DAYS:      LDA     #<DAYS_2000
                STA     TDAYS
                LDA     #>DAYS_2000
                STA     TDAYS + 1
                LDX     #0             ; the years before
@year:          CPX     TYEAR
                BEQ     @months
                TXA
                JSR     YEARLEN
                JSR     ADDDAYS
                INX
                BRA     @year
@months:        LDX     #1
@month:         CPX     TMONTH
                BCS     @date
                JSR     MONTHLEN
                LDY     #0
                JSR     ADDDAYS
                INX
                BRA     @month
@date:          LDA     TDATE
                LDY     #0
                JSR     ADDDAYS
                LDA     TDAYS
                STA     SCB_DATE
                LDA     TDAYS + 1
                STA     SCB_DATE + 1
                RTS

; TDAYS += A + Y x 256. Keeps X.
ADDDAYS:        CLC
                ADC     TDAYS
                STA     TDAYS
                TYA
                ADC     TDAYS + 1
                STA     TDAYS + 1
                RTS

; Year A (from 2000)'s days: A the low byte, Y the high. Every fourth is a
; leap year.
YEARLEN:        LDY     #>365          ; 366 too
                AND     #3
                BEQ     @leap
                LDA     #<365
                RTS
@leap:          LDA     #<366
                RTS

; Month X (1-12) of TYEAR's days, in A. Keeps X.
MONTHLEN:       CPX     #2
                BNE     @plain
                LDA     TYEAR
                AND     #3
                BNE     @plain
                LDA     #29
                RTS
@plain:         LDA     MDAYS - 1,X
                RTS

MDAYS:          .byte   31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31

; TYEAR, TMONTH and TDATE from SCB_DATE. Carry set if it's before 2000 or
; past 2155.
DAYS2DATE:      SEC
                LDA     SCB_DATE
                SBC     #<DAYS_2000
                STA     TDAYS
                LDA     SCB_DATE + 1
                SBC     #>DAYS_2000
                STA     TDAYS + 1
                BCC     @early
                ORA     TDAYS
                BEQ     @early         ; day 8035 is 31 December 1999
                STZ     TYEAR
@year:          LDA     TYEAR
                JSR     YEARLEN
                JSR     FITS
                BCC     @months
                INC     TYEAR
                BRA     @year
@months:        LDX     #1
@month:         STX     TMONTH
                JSR     MONTHLEN
                LDY     #0
                JSR     FITS
                BCC     @date
                INX
                BRA     @month
@date:          LDA     TDAYS
                STA     TDATE
                LDA     TYEAR
                CMP     #LAST_YEAR + 1 ; carry: past 2155
                RTS
@early:         SEC
                RTS

; If TDAYS > A + Y x 256, take it off and set carry; else carry clear.
FITS:           STA     TMP
                STY     TMP + 1
                SEC
                LDA     TMP
                SBC     TDAYS
                LDA     TMP + 1
                SBC     TDAYS + 1
                BCS     @in            ; the length >= TDAYS: it's in here
                SEC
                LDA     TDAYS
                SBC     TMP
                STA     TDAYS
                LDA     TDAYS + 1
                SBC     TMP + 1
                STA     TDAYS + 1
                SEC
                RTS
@in:            CLC
                RTS

; The day of the week of SCB_DATE: 1 Sunday to 7, in A.
WEEKDAY:        SEC
                LDA     SCB_DATE
                SBC     #1
                STA     TMP
                LDA     SCB_DATE + 1
                SBC     #0
                STA     TMP + 1
                LDX     #13            ; take off 7 x 2^13, ... 7 x 1 where they fit
@step:          LDA     WEEKS_LO,X
                STA     SPTR
                LDA     WEEKS_HI,X
                STA     SPTR + 1
                SEC
                LDA     TMP
                SBC     SPTR
                TAY
                LDA     TMP + 1
                SBC     SPTR + 1
                BCC     @next
                STA     TMP + 1
                STY     TMP
@next:          DEX
                BPL     @step
                LDA     TMP
                INC     A
                RTS
WEEKS_LO:
.repeat 14, k
                .byte   <(7 << k)
.endrepeat
WEEKS_HI:
.repeat 14, k
                .byte   >(7 << k)
.endrepeat

BCD2BIN:        PHA
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                TAY                    ; tens x 10 = tens x 8 + tens x 2
                ASL     A
                ASL     A
                ASL     A
                STA     TMP
                TYA
                ASL     A
                ADC     TMP
                STA     TMP
                PLA
                AND     #$0F
                CLC
                ADC     TMP
                RTS

BIN2BCD:        LDY     #0             ; A (0-99): tens in Y
@tens:          CMP     #10
                BCC     @done
                SBC     #10
                INY
                BRA     @tens
@done:          STA     TMP
                TYA
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     TMP
                RTS

; ---------------------------------------------------------------------------
; Cold boot's part (block 62's COLD calls these)

; The devices, the drives, the banks.
BINIT:          STZ     CURBANK
                STZ     DMABANK
                STZ     XPEND
                LDA     SD_ID          ; the card's interrupts off: polled
                CMP     #SD_ID_V1
                BNE     @devices
                STZ     SD_IEN
@devices:       LDA     #DEV_CRT
                JSR     DEVINI
                LDA     #DEV_TTY
                JSR     DEVINI
                LDA     #DEV_LPT
                JSR     DEVINI
                JSR     UNITS
                LDA     #<DPHM
                STA     DRVTAB + 2 * DRV_M
                LDA     #>DPHM
                STA     DRVTAB + 2 * DRV_M + 1
                LDA     DISK2_ID       ; the Disk II: O: and P:
                CMP     #DISK2_ID_V1
                BNE     @done
                JSR     D2INIT
                LDA     #<DPHO
                STA     DRVTAB + 2 * DRV_O
                LDA     #>DPHO
                STA     DRVTAB + 2 * DRV_O + 1
                LDA     #<DPHP
                STA     DRVTAB + 2 * DRV_P
                LDA     #>DPHP
                STA     DRVTAB + 2 * DRV_P + 1
@done:          RTS

; M:'s directory, as `mkfs.cpm -f oodos-ram -t` makes it with the label's
; mode then set to 0x31 (hw/oodos.md, "Disk format"): 256 entries in 16
; sectors, all $E5 but entry 0, the label UNLABELED with its two stamps the
; SCB's time, and every fourth entry from 3 an SFCB, $21 then $E5s. The rest
; of the disk is left as it is, as mkfs.cpm leaves it.
LABEL_LEN       = 32
MFORMAT:        LDA     #$E5
                LDX     #0
@fill:          STA     SECBUF,X
                STA     SECBUF + $100,X
                INX
                BNE     @fill
                LDA     #$21
                LDX     #3 * 32        ; an SFCB in entries 3, 7, 11 and 15 of the sector
@sfcb:          STA     SECBUF,X
                STA     SECBUF + $100,X
                PHA
                TXA
                CLC
                ADC     #4 * 32
                TAX
                PLA
                BCC     @sfcb
                LDX     #LABEL_LEN - 1
@label:         LDA     MLABEL,X
                STA     SECBUF,X
                DEX
                BPL     @label
                LDX     #3
@stamp:         LDA     SCB_DATE,X     ; days, hour, minute: made and updated
                STA     SECBUF + 24,X
                STA     SECBUF + 28,X
                DEX
                BPL     @stamp
                STZ     LBA            ; sector 0, with the label
                JSR     @put
                LDX     #LABEL_LEN - 1 ; then 15 without
                LDA     #$E5
@unlabel:       STA     SECBUF,X
                DEX
                BPL     @unlabel
@next:          INC     LBA
                JSR     @put
                LDA     LBA
                CMP     #15
                BNE     @next
                RTS
@put:           JSR     FROMSECBUF
                LDX     #SIDE_D
                JSR     MSIDE
                JMP     COPY512

MLABEL:         .byte   $20, "UNLABELED  ", $31  ; a label; creation and update stamps
                .res    11, 0
                .res    8, 0                     ; the stamps, from the SCB

; ---------------------------------------------------------------------------
; The drives: CP/M 3's DPBs, worked out from oodos/diskdefs; a DPH for each
; drive, with no translation and no buffers yet (the BDOS's, phase 3).
;
; SPT is in 128-byte records; PSH 2 and PHM 3 are 512-byte sectors; CKS
; $8000 a drive that isn't changed under the system, so no checksums. The
; DPBs are in common memory, so function 31 can give a program one.

; What the BDOS and the BIOS assume of a DPB, checked as it's assembled.
.macro DPB spt, bsh, exm, dsm, drm, al0, al1, off, cks
.assert spt = 64 || spt = 32, error, "SPT: 16 or 8 sectors of 512 bytes a track (the BDOS's TSH)"
.assert bsh >= 3 && bsh <= 7, error, "BSH 3-7: a block of 2 sectors at least (RECSEC), of an extent at most (FPIDX)"
.assert dsm < 4096, error, "DSM: 4,096 blocks at most, a 512-byte allocation vector"
.assert drm < 2048, error, "DRM: 2,048 entries at most, the hash's 8 KB a drive"
.assert (dsm + 1) << (bsh - 2) <= 65536, error, "the drive's sectors: LBA is 16 bits"
.assert exm = (1 << (bsh - 3 - (dsm > 255))) - 1, error, "EXM: CP/M 3's, from BSH and DSM"
.assert (al0 << 8 | al1) = ($FFFF << (16 - ((drm + 1) * 32 + (128 << bsh) - 1) / (128 << bsh))) & $FFFF, error, "AL0 and AL1: the directory's blocks"
                .word   spt
                .byte   bsh, (1 << bsh) - 1, exm
                .word   dsm, drm
                .byte   al0, al1
                .word   cks            ; CKS: $8000 a drive never changed under the system
                .word   off            ; OFF: the boot tracks
                .byte   2, 3           ; PSH, PHM
.endmacro

.pushseg
.segment "COMMON"
; oodos-8m: 4 KB blocks, 2,048 of them, 1,024 entries in 8 blocks
DPB8M:          DPB     64, 5, 1, 2047, 1023, $FF, $00, 0, $8000
; oodos-32m: 8 KB blocks, 4,096 of them, 2,048 entries in 8 blocks
DPB32M:         DPB     64, 6, 3, 4095, 2047, $FF, $00, 0, $8000
; oodos-ram: 2 KB blocks, 64 of them (so EXM counts 1 KB), 256 entries in 4
DPBM:           DPB     64, 4, 1, 63, 255, $F0, $00, 0, $8000
; apple-do: 8 sectors a track (16 of 256 bytes), 1 KB blocks, 128 of them
; past the 3 boot tracks, 64 entries in 2 blocks
DPBAPL:         DPB     32, 3, 0, 127, 63, $C0, $00, 3, $8000
; O: and P:, the Disk II's: apple-do, and a floppy can be changed: CKS 16, a
; checksum for each of the directory's 16 records (bdosdisk.s)
DPBD2:          DPB     32, 3, 0, 127, 63, $C0, $00, 3, 16
.assert DPB32M - DPB8M = DPB_LEN, error, "a DPB is 17 bytes"
.popseg

.macro DPH dpb
                .word   0              ; XLT: no translation
                .res    9, 0           ; the BDOS's scratch
                .byte   0              ; MF
                .word   dpb
                .word   0, 0           ; CSV, ALV
                .word   0, 0           ; DIRBCB, DTABCB
                .word   0              ; HASH
                .byte   0              ; HBANK
.endmacro

DPHS:
.repeat SD_DRIVES
                DPH     0              ; the DPB from the units table
.endrepeat
DPHM:           DPH     DPBM
DPHO:           DPH     DPBD2
DPHP:           DPH     DPBD2
.assert DPHM - DPHS = SD_DRIVES * DPH_LEN, error, "a DPH is 25 bytes"

DPHADDR:
.repeat SD_DRIVES, i
                .word   DPHS + i * DPH_LEN
.endrepeat

; The drive table: a DPH for each of A-P, 0 for none.
DRVTAB:         .res    32, 0

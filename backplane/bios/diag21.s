; The ROM's diagnostics, flash block 21 (hw/diag.md, plan 2, D3b): stages
; 11-14: storage, read-only; USB, serial and the panel; the interactive tests;
; the summary and the end (diagcon.inc's ENDRUN: the loop, or a key).
;
; Nothing here writes the SD card (the owner's answer 5): its commands are
; READ and IDENTIFY alone. The Disk II is found by its ID and nothing else:
; any access to its switches moves the drive.

.setcpu "65C02"
.macpack longbranch
.include "backplane.inc"
.include "diag.inc"
OOHW_ROM        = 1
OOHW_BLK        = 21
.include "oohw.inc"

.import SKIPS

HZ              = 7159090
SDBUF           = $0400                ; W0's: IDENTIFY's sector
SDCPU           = $0600                ; W0's: LBA 0 by the CPU
SDDMA           = $0800                ; W0's: LBA 0 by channel 0
P3BLOCK         = <(DG_P3ADDR >> 14)   ; RAM block 47
P3OFS           = DG_P3ADDR & $3FFF    ; +$11A5
P3BYTE          = $A7                  ; what P3 reads there
L1MAP           = $3400                ; V8's test card: layer 1's text, after layer 0's

.segment "D21ZP": zeropage
T_REV:          .res    1
T_PAT:          .res    1
T_ANY:          .res    1              ; a test of the stage ran (not all skipped)
T_SEEN:         .res    2              ; U4, SR3: what has been seen
T_ROW:          .res    1
T_COL:          .res    1
                DTVARS
                DSVARS

.segment "D21"
                DG_HEADER 2, START
.assert * = $8025, error, "block 21's header"

START:
                cld
                ldx     #$FF
                txs
                jsr     HW_PROBE
                jsr     STAGE11
                jsr     STAGE12
                jsr     STAGE13
                jsr     SUMMARY
                jmp     ENDRUN

; A stage's start: A the stage; its name from STNAMES (diagcon.inc).
SBEGIN:
                pha
                asl     a
                tax
                lda     STNAMES - 14,x
                sta     DG_T0
                lda     STNAMES - 13,x
                sta     DG_T0 + 1
                pla
                stz     T_ANY
                jmp     CBEG

; The stage's end: PASS, or SKIP if every test was skipped.
SEND:
                lda     T_ANY
                beq     @skip
                lda     #DR_PASS
                jmp     CEND
@skip:          lda     #DR_SKIP
                jmp     CEND

; Carry clear if module X's card is at revision A; else the test skipped,
; with why (no card, or a revision this build doesn't know).
HAS:
                sta     T_REV
                lda     HW_REV,x
                cmp     #HW_NONE
                beq     @none
                cmp     T_REV
                bne     @rev
                clc
                rts
@none:          TS      "NO CARD"
                sec
                rts
@rev:           pha
                lda     #DR_SKIP
                jsr     TEND
                jsr     SIX
                SAY     "   REVISION "
                pla
                jsr     PUTDEC
                SAY     ": NOT KNOWN TO THIS BUILD"
                jsr     CRLF
                sec
                rts

; Wait about T_REV x 2^16 cycles (timer 2).
WAITT:
                jsr     TZERO
@w:             jsr     TTICK
                lda     T_ACC + 2
                cmp     T_REV
                bcc     @w
                rts

; --- Stage 11: storage, read-only --------------------------------------------

STAGE11:
                lda     #11
                jsr     SBEGIN
                jsr     SD1
                bcs     @dk
                jsr     SD2
                jsr     SD3
                jsr     SD4
@dk:            jsr     DK1
                jmp     SEND

; The SD card idle (BUSY clear) within about a second: carry set if not.
SDIDLE:
                jsr     TZERO
@w:             bit     SD_STATUS
                bpl     @ok
                jsr     TTICK
                lda     T_ACC + 2
                cmp     #$6D
                bcc     @w
                sec
                rts
@ok:            clc
                rts

; READ of unit 0 from LBA 0, A sectors, CTRL X (0, or SD_CTRL_DMA). Carry set,
; and no command written, if the card stayed busy (review P2): the caller
; says so through SDSTUCK.
SDREAD:
                pha
                phx
                jsr     SDIDLE
                bcc     @idle
                plx
                pla
                rts
@idle:          stz     SD_UNIT
                stz     SD_LBA
                stz     SD_LBA + 1
                stz     SD_LBA + 2
                stz     SD_LBA + 3
                plx
                pla
                sta     SD_COUNT
                stz     SD_COUNT + 1
                stx     SD_CTRL
                lda     #SD_READ
                sta     SD_CMD
                clc
                rts

; The card stayed busy: its command abandoned (CTRL's RESET, which writes
; nothing and leaves its units mounted) and channel 0 aborted, so neither
; runs on into the boot; the test failed, said (review P2). Called only from
; SD3's and SD4's DMA parts (SD3GO, SD4GO), whose callers keep CONTROL and
; put it back: it selects channel 0 itself before its MODE.
SDSTUCK:
                lda     #SD_CTRL_RESET
                sta     SD_CTRL
                lda     #DMA_CH0
                sta     DMA_ABORT
                lda     #DMA_ON        ; SELECT 0 first: the window is channel 0's
                sta     DMA_CONTROL
                stz     DMA_MODE
                TD      "THE CARD STAYED BUSY"
                rts

SD1:
                lda     #<SN_SD1
                ldx     #>SN_SD1
                jsr     TBEG
                ldx     #7
                lda     #1
                jsr     HAS
                bcs     @r
                inc     T_ANY
                jsr     TZERO          ; PRESENT and READY: a card starting up has a second
@w:             lda     SD_STATUS
                and     #SD_PRESENT | SD_READY
                cmp     #SD_PRESENT | SD_READY
                beq     @ready
                jsr     TTICK
                lda     T_ACC + 2
                cmp     #$6D
                bcc     @w
                lda     SD_STATUS
                and     #SD_PRESENT
                bne     @nready
                jsr     TINFO
                lda     #<SEMPTY
                ldx     #>SEMPTY
                jsr     NOTE9
                sec
@r:             rts
@nready:        TD      "PRESENT, BUT NOT READY WITHIN A SECOND"
                sec
                rts
@ready:         jsr     TPASS
                clc
                rts

; SD2: IDENTIFY (HW_SD_ID, by the CPU through DATA): the type 1-3, the size
; not 0, a FAT volume.
SD2:
                TB      "SD2  IDENTIFY: "
                lda     #<SDBUF
                sta     HW_PTR
                lda     #>SDBUF
                sta     HW_PTR + 1
                jsr     HW_SD_ID
                bcc     @got
                sta     T_REV
                jsr     TDET
                SAY     "IDENTIFY FAILED: "
                lda     T_REV
                jsr     HEX2
                jsr     CRLF
                jmp     SCROFF
@got:           lda     SDBUF + SI_TYPE
                beq     @type
                cmp     #4
                bcs     @type
                asl     a
                tax
                lda     STYPES - 2,x
                ldy     STYPES - 1,x
                phy
                plx
                jsr     PUTS
                SAY     ", "
                ldx     #3
@cp:            lda     SDBUF + SI_SECTORS,x
                sta     T_N,x
                dex
                bpl     @cp
                jsr     DEC32
                SAY     " SECTORS"
                lda     SDBUF + SI_SECTORS
                ora     SDBUF + SI_SECTORS + 1
                ora     SDBUF + SI_SECTORS + 2
                ora     SDBUF + SI_SECTORS + 3
                beq     @size
                lda     SDBUF + SI_FLAGS
                and     #SIF_FAT
                beq     @fat
                SAY     ", FAT"
                jmp     TPASS
@type:          TD      "THE CARD'S TYPE ISN'T 1-3"
                rts
@size:          TD      "THE CARD'S SIZE IS 0"
                rts
@fat:           TD      "NO FAT VOLUME FOUND"
                rts

; SD3: LBA 0 of unit 0, the card itself, read by the CPU through DATA and
; again by channel 0 into W0's block: the two agree. CONTROL is kept, SELECT 0
; (the master on) written before any channel register, and CONTROL put back
; however SD3GO ends.
SD3:
                TB      "SD3  LBA 0 BY THE CPU AND BY CHANNEL 0 AGREE"
                lda     HW_REV + 8
                cmp     #2
                beq     @dma
                TS      "NO DMA CONTROLLER"
                rts
@dma:           lda     DMA_CONTROL
                pha
                lda     #DMA_ON        ; SELECT 0: the window is channel 0's
                sta     DMA_CONTROL
                jsr     SD3GO
                pla
                sta     DMA_CONTROL
                rts
SD3GO:          lda     #1
                ldx     #0
                jsr     SDREAD
                jcs     @stuck
                jsr     TZERO
@data:          lda     SD_STATUS
                bit     #SD_DATA_READY
                bne     @read
                bit     #SD_BUSY
                beq     @fail
                jsr     TTICK
                lda     T_ACC + 2
                cmp     #$6D
                bcc     @data
@fail:          TD      "THE READ ENDED WITH NO SECTOR"
                rts
@read:          ldy     #0
@lo:            lda     SD_DATA
                sta     SDCPU,y
                iny
                bne     @lo
@hi:            lda     SD_DATA
                sta     SDCPU + $100,y
                iny
                bne     @hi
                lda     #>SDDMA        ; channel 0, demand, card to memory: 512 bytes
                jsr     DMA0AT
                stz     DMA_COUNT
                lda     #2
                sta     DMA_COUNT + 1
                lda     #DMA_DEMAND_IN
                sta     DMA_MODE
                lda     #DMA_ENABLE
                sta     DMA_CTRL
                lda     #1
                ldx     #SD_CTRL_DMA
                jsr     SDREAD
                bcs     @stuck
                jsr     SDIDLE
                bcs     @stuck
                stz     SD_CTRL
                lda     DMA_CTRL
                and     #DMA_ENABLE
                bne     @short
                ldy     #0
@cmp:           lda     SDCPU,y
                cmp     SDDMA,y
                bne     @diff
                lda     SDCPU + $100,y
                cmp     SDDMA + $100,y
                bne     @diff
                iny
                bne     @cmp
                jmp     TPASS
@stuck:         jmp     SDSTUCK
@short:         lda     #DMA_CH0
                sta     DMA_ABORT
                TD      "CHANNEL 0 DIDN'T REACH ITS TERMINAL COUNT"
                rts
@diff:          TD      "THE TWO READS DIFFER"
                rts

; Channel 0's ADDR (SELECT 0 is the caller's): W0's page A.
DMA0AT:
                tax
                lda     DG_W0
                jsr     PHYSB
                ldx     #2
@a:             lda     T_PA,x
                sta     DMA_ADDR,x
                dex
                bpl     @a
                rts

; Four tested blocks in a row, none W0's: the first in A, carry clear;
; carry set if there are none.
FOUR:
                lda     #$FF
@start:         jsr     GOODBLK        ; a first block
                bcs     @r
                cmp     DG_W0
                beq     @start
                sta     T_PAT
                sta     T_ROW          ; the last so far
                lda     #3
                sta     T_COL
@next:          lda     T_ROW
                jsr     GOODBLK
                bcs     @r
                cmp     DG_W0
                beq     @again
                dec     a
                cmp     T_ROW
                bne     @again         ; not the next: start again past the first
                inc     a
                sta     T_ROW
                dec     T_COL
                bne     @next
                lda     T_PAT
                clc
@r:             rts
@again:         lda     T_PAT
                bra     @start

; SD4: the read speed: 64 KB of unit 0 by channel 0 into four tested blocks,
; timed by timer 2 from the command to the card idle again: INFO. CONTROL is
; kept, SELECT 0 written before any channel register, and CONTROL put back
; however SD4GO ends.
SD4:
                TB      "SD4  READ SPEED: "
                lda     HW_REV + 8
                cmp     #2
                beq     @dma
                TS      "NO DMA CONTROLLER"
                rts
@dma:           lda     DMA_CONTROL
                pha
                lda     #DMA_ON        ; SELECT 0: the window is channel 0's
                sta     DMA_CONTROL
                jsr     SD4GO
                pla
                sta     DMA_CONTROL
                rts
SD4GO:          jsr     FOUR
                bcc     @blocks
                TS      "NOT FOUR TESTED BLOCKS IN A ROW"
                rts
@blocks:        ldx     #0
                jsr     PHYSB
                ldx     #2
@a:             lda     T_PA,x
                sta     DMA_ADDR,x
                dex
                bpl     @a
                stz     DMA_COUNT      ; 65,536
                stz     DMA_COUNT + 1
                lda     #DMA_DEMAND_IN
                sta     DMA_MODE
                lda     #DMA_MASTER | DMA_SEL0 | $40 ; bursts of 16 bytes, SELECT 0 kept: the CPU between them keeps
                sta     DMA_CONTROL    ;   timer 2's count, its three reads (T2NOW) under 256 cycles apart
                lda     #DMA_ENABLE
                sta     DMA_CTRL
                jsr     SDIDLE
                jcs     SDSTUCK
                stz     SD_UNIT
                stz     SD_LBA
                stz     SD_LBA + 1
                stz     SD_LBA + 2
                stz     SD_LBA + 3
                lda     #128
                sta     SD_COUNT
                stz     SD_COUNT + 1
                lda     #SD_CTRL_DMA
                sta     SD_CTRL
                jsr     TZERO
                lda     #SD_READ
                sta     SD_CMD
@busy:          jsr     TTICK
                bit     SD_STATUS
                bpl     @done
                lda     T_ACC + 3
                cmp     #4             ; about 9 s
                bcc     @busy
                jmp     SDSTUCK
@done:          stz     SD_CTRL
                lda     #DMA_ON
                sta     DMA_CONTROL
                lda     SD_STATUS
                and     #SD_ERR
                bne     @err
                lda     DMA_CTRL
                and     #DMA_ENABLE
                jne     @short
                lda     T_ACC + 3      ; KB a second: 64 x 7,159,090 / cycles, both / 256
                bne     @slow
                lda     #<(64 * HZ / 256)
                sta     HW_NUM
                lda     #>(64 * HZ / 256)
                sta     HW_NUM + 1
                lda     #^(64 * HZ / 256)
                sta     HW_NUM + 2
                stz     HW_NUM + 3
                lda     T_ACC + 1
                ldx     T_ACC + 2
                bne     @div
                cmp     #0
                bne     @div
                lda     #1
@div:           jsr     HW_DIV
                ldx     #3
@cp:            lda     HW_NUM,x
                sta     T_N,x
                dex
                bpl     @cp
                jsr     DEC32
                SAY     " KB A SECOND"
                jmp     TINFO
@slow:          SAY     "UNDER 8 KB A SECOND"
                jmp     TINFO
@err:           TD      "THE READ ENDED IN AN ERROR"
                rts
@short:         lda     #DMA_CH0
                sta     DMA_ABORT
                TD      "CHANNEL 0 DIDN'T REACH ITS TERMINAL COUNT"
                rts

; DK1: the Disk II by its ID alone.
DK1:
                lda     #<SN_DK1
                ldx     #>SN_DK1
                jsr     TBEG
                ldx     #$C
                lda     #1
                jsr     HAS
                bcs     @r
                inc     T_ANY
                jmp     TPASS
@r:             rts

; --- Stage 12: USB and the game port; the serial card and the VIA; the panel --

STAGE12:
                lda     #12
                jsr     SBEGIN
                jsr     U1
                jsr     U2
                jsr     SR1
                jsr     SR2
                jsr     SR4
                jsr     P1
                jsr     P2
                jmp     SEND

U1:
                lda     #<SN_U1
                ldx     #>SN_U1
                jsr     TBEG
                ldx     #$B
                lda     #1
                jsr     HAS
                bcs     @r
                inc     T_ANY
                SAY     ": DEVICES $"
                lda     USB_DEVICES
                jsr     HEX2
                lda     USB_DEVICES
                and     #$01
                beq     @m
                SAY     " KEYBOARD"
@m:             lda     USB_DEVICES
                and     #$02
                beq     @g
                SAY     " MOUSE"
@g:             lda     USB_DEVICES
                and     #$04
                beq     @i
                SAY     " GAMEPAD"
@i:             jmp     TINFO
@r:             rts

; U2: the paddles settle: PTRIG, then PADDLE0-3 read twice, 20 ms apart.
U2:
                lda     #<SN_U2
                ldx     #>SN_U2
                jsr     TBEG
                ldx     #$B
                lda     #1
                jsr     HAS
                bcs     @r
                inc     T_ANY
                lda     USB_PTRIG
                ldx     #3
@first:         lda     USB_PADDLE0,x
                sta     T_N,x
                dex
                bpl     @first
                lda     #3             ; 3 x 65,536 cycles: 27 ms
                sta     T_REV
                jsr     WAITT
                ldx     #0
@say:           jsr     SPACE
                lda     USB_PADDLE0,x
                cmp     T_N,x
                bne     @moved
                jsr     PUTDEC
                inx
                cpx     #4
                bne     @say
                jmp     TPASS
@moved:         stx     T_REV
                jsr     TDET
                SAY     "PADDLE "
                lda     T_REV
                jsr     PUTDEC
                SAY     " MOVED IN 20 MS"
                jsr     CRLF
                jmp     SCROFF
@r:             rts

; SR1: the serial card's ID, $61; the emulator's stand-in 6551 has none.
SR1:
                TB      "SR1  THE SERIAL CARD'S ID"
                lda     HW_REV + 6
                cmp     #1
                beq     @card
                TS      "THE STAND-IN 6551 (NO ID REGISTER)"
                rts
@card:          inc     T_ANY
                SAY     " $61"
                jmp     TPASS

; SR2: the VIA's timers: each across a 1,000-cycle loop less across none.
SR2:
                TB      "SR2  THE VIA'S TIMERS 2 AND 1 COUNT 1000 CYCLES"
                inc     T_ANY
                lda     #$FF
                sta     VIA_T2CL
                sta     VIA_T2CH
                ldx     #VIA_T2CL - VIA
                jsr     @span
                bcs     @bad2
                stz     VIA_ACR        ; timer 1 a one-shot, the core's; the end's put-back stops it
                lda     #$FF
                sta     VIA_T1CL
                sta     VIA_T1CH
                ldx     #VIA_T1CL - VIA
                jsr     @span
                bcs     @bad1
                jmp     TPASS
@bad2:          TD      "TIMER 2 DIDN'T COUNT 1000"
                rts
@bad1:          TD      "TIMER 1 DIDN'T COUNT 1000"
                rts
; The timer at VIA + X: its count across 1,000 cycles less across none,
; within 4; carry set if not.
@span:          jsr     @now
                sta     T_N
                sty     T_N + 1
                jsr     @now
                sta     T_N + 2
                sty     T_N + 3
                jsr     @now
                sta     T_D
                sty     T_D + 1
                phx                    ; PHX, LDX, the loop, NOP, PLX: 1,000 cycles
                ldx     #198
@d:             dex
                bne     @d
                nop
                plx
                jsr     @now
                sta     T_D + 2
                sty     T_D + 3
                sec                    ; across the loop: T_D less T_D + 2
                lda     T_D
                sbc     T_D + 2
                sta     T_D
                lda     T_D + 1
                sbc     T_D + 3
                sta     T_D + 1
                sec                    ; across none: T_N less T_N + 2
                lda     T_N
                sbc     T_N + 2
                sta     T_N
                lda     T_N + 1
                sbc     T_N + 3
                sta     T_N + 1
                sec                    ; the loop's own, less 996: 0-8
                lda     T_D
                sbc     T_N
                sta     T_D
                lda     T_D + 1
                sbc     T_N + 1
                sta     T_D + 1
                sec
                lda     T_D
                sbc     #<(1000 - 4)
                sta     T_D
                lda     T_D + 1
                sbc     #>(1000 - 4)
                bne     @off
                lda     T_D
                cmp     #9
                bcs     @off
                clc
                rts
@off:           sec
                rts
; The timer at VIA + X now: A low, Y high, one moment's.
@now:           lda     VIA + 1,x
                tay
                lda     VIA,x
                pha
                tya
                cmp     VIA + 1,x
                beq     @same
                pla
                bra     @now
@same:          pla
                rts

; SR4: the TL16C552A's COM2 in its internal loopback (MCR bit 4), once the
; card exists (the line's text avoids the word LOOP, the header's for a loop).
SR4:
                TB      "SR4  THE TL16C552A'S COM2 SENDING TO ITSELF"
                lda     HW_REV + 6
                cmp     #1
                beq     @card
                TS      "NO TL16C552A CARD"
                rts
@card:          TS      "NOT IN THIS BUILD"
                rts

; P1: the panel's switches and its HALT/ENABLE and S INST/S BUS CYCLE: INFO.
P1:
                lda     #<SN_P1
                ldx     #>SN_P1
                jsr     TBEG
                ldx     #$E
                lda     #1
                jsr     HAS
                jcs     @r
                inc     T_ANY
                SAY     ": SWITCHES $"
                lda     CONSOLE_SWR + 2
                jsr     HEXDIG
                lda     CONSOLE_SWR + 1
                jsr     HEX2
                lda     CONSOLE_SWR
                jsr     HEX2
                lda     CONSOLE_PANEL
                and     #$01
                beq     @en
                SAY     ", HALT"
                bra     @bc
@en:            SAY     ", ENABLE"
@bc:            lda     CONSOLE_PANEL
                and     #$02
                beq     @si
                SAY     ", S BUS CYCLE"
                jmp     TINFO
@si:            SAY     ", S INST"
                jmp     TINFO
@r:             rts

; P2: a bit walking across the lights, 0.2 s a step: INFO, or (interactive)
; the user's word.
P2:
                lda     #<SN_P2
                ldx     #>SN_P2
                jsr     TBEG
                ldx     #$E
                lda     #1
                jsr     HAS
                bcs     @r
                inc     T_ANY
                jsr     WALK
                lda     DG_OPT
                and     #DO_ASK
                beq     @info
                SAY     ": DID ONE LIGHT WALK ACROSS?"
                jmp     YESNO
@info:          jmp     TINFO
@r:             rts

WALK:
                lda     #1
@step:          sta     T_PAT
                sta     CONSOLE_DISPLAY
                lda     #22            ; 22 x 65,536 cycles: 0.2 s
                sta     T_REV
                jsr     WAITT
                lda     T_PAT
                asl     a
                bne     @step
                lda     DG_STAGE
                sta     CONSOLE_DISPLAY
                rts

; --- Stage 13: the interactive tests ------------------------------------------

STAGE13:
                lda     #13
                jsr     SBEGIN
                lda     DG_OPT
                and     #DO_ASK
                bne     @ask
                lda     #<SNOASK
                ldx     #>SNOASK
                jmp     CSKIP
@ask:           jsr     V8
                jsr     S10
                jsr     U3
                jsr     U4
                jsr     U5
                jsr     SR3
                jsr     P3
                jmp     SEND

; Carry set and the test skipped if A is Esc; else carry clear.
ESCAPED:
                cmp     #$1B
                bne     @no
                TS      "ESC: SKIPPED"
                sec
                rts
@no:            clc
                rts

; V8: the test card on layer 1: colour bars, a grid.
V8:
                TB      "V8   THE TEST CARD: EIGHT COLOUR BARS AND A GRID?"
                lda     HW_REV + 9
                cmp     #2
                beq     @card
                TS      "NO VIDEO CARD"
                rts
@card:          inc     T_ANY
                lda     #<L1MAP
                sta     VIDEO_PADDR
                lda     #>L1MAP
                sta     VIDEO_PADDR + 1
                stz     VIDEO_PADDR + 2
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                stz     VIDEO_PCTL
                stz     T_ROW
@row:           stz     T_COL
@col:           lda     T_COL          ; the bar (X): the column / 10; T_PAT the column in it
                ldx     #0
@ten:           cmp     #10
                bcc     @bar
                sbc     #10
                inx
                bra     @ten
@bar:           sta     T_PAT
                lda     T_ROW          ; the row in its five
@five:          cmp     #5
                bcc     @r5
                sbc     #5
                bra     @five
@r5:            ldy     #' '
                cmp     #0
                bne     @edge
                ldy     #'-'
@edge:          pha
                lda     T_PAT
                bne     @put1
                ldy     #'|'
                pla
                pha
                bne     @put1
                ldy     #'+'
@put1:          pla
                tya
                sta     VIDEO_PDATA
                txa
                asl     a
                asl     a
                asl     a
                asl     a
                ora     #$0F
                sta     VIDEO_PDATA
                inc     T_COL
                lda     T_COL
                cmp     #DG_COLS
                bne     @col
                inc     T_ROW
                lda     T_ROW
                cmp     #DG_ROWS
                bne     @row
                lda     #VIDEO_TEXT80B
                sta     VIDEO_L1_CONFIG
                lda     #<(L1MAP >> 8)
                sta     VIDEO_L1_MAP
                stz     VIDEO_L1_MAP + 1
                lda     #<(DG_FONT >> 8)
                sta     VIDEO_L1_TILE
                stz     VIDEO_L1_TILE + 1
                stz     VIDEO_L1_HSCROLL
                stz     VIDEO_L1_HSCROLL + 1
                stz     VIDEO_L1_VSCROLL
                stz     VIDEO_L1_VSCROLL + 1
                stz     VIDEO_L1_PALOFS
                lda     #$05           ; the display and layer 1 alone
                sta     VIDEO_CTRL
                jsr     YESNO
                lda     #VIDEO_ON_L0
                sta     VIDEO_CTRL
                stz     VIDEO_L1_CONFIG
                rts

; S10: the sound card's own sounds, by command (diagsnd.inc): a music block's
; one note on a SID voice (the A at 440 Hz, a triangle, half a second), then a
; sample effect on the ES5503 (a square wave, looped, at 220 Hz for half a
; second); the sample slot freed after. Revision 3.
S10:
                TB      "S10  A NOTE ON A SID VOICE, A SAMPLE ON THE ES5503"
                lda     HW_REV + $A
                cmp     #3
                beq     @card
                TS      "NO SOUND CARD"
                rts
@card:          inc     T_ANY
                lda     #SOUND_ST_READY
                ldy     #SND_250MS
                jsr     SNDWT
                jcs     @dead
                lda     #<RMLOAD       ; the block into music slot 0, played and freed
                ldx     #>RMLOAD
                jsr     SNDDO
                jcs     @dead
                jne     @dead
                lda     #<MBLOCK
                sta     SN_PTR
                lda     #>MBLOCK
                sta     SN_PTR + 1
                ldy     #MBLEN
                jsr     SNDDATA
                lda     #<RMPLAY
                ldx     #>RMPLAY
                jsr     SNDDO
                bcs     @dead
                bne     @dead
                lda     #60            ; its half second, and a little
                sta     T_REV
                jsr     WAITT
                lda     #<RSLOAD       ; 256 bytes into sample slot 0: a square wave
                ldx     #>RSLOAD
                jsr     SNDDO
                bcs     @dead
                bne     @dead
                ldx     #0
@w:             lda     #$40
                cpx     #$80
                bcc     @lo
                lda     #$C0
@lo:            sta     SOUND_DATA
                inx
                bne     @w
                lda     #<RSDEF        ; effect 0: the sample, looped, held half a second
                ldx     #>RSDEF
                jsr     SNDDO
                bcs     @dead
                bne     @dead
                lda     #<RSPLAY
                ldx     #>RSPLAY
                jsr     SNDDO
                bcs     @dead
                bne     @dead
                lda     #60
                sta     T_REV
                jsr     WAITT
                lda     #<RSFREE
                ldx     #>RSFREE
                jsr     SNDDO
                SAY     "?"
                jmp     YESNO
@dead:          TD      "THE CARD DIDN'T TAKE ITS RECORDS"
                rts

; S10's records (diagsnd.inc): the command, n, the tag, n bytes; and its block
RMLOAD:         .byte   SOUND_MUSIC_LOAD, 4, SND_TAG, 0, MBLEN, 0, 0
RMPLAY:         .byte   SOUND_MUSIC_PLAY, 2, SND_TAG, 0, $02    ; freed when done
RSLOAD:         .byte   SOUND_SAMPLE_LOAD, 13, SND_TAG, 0       ; sample slot 0:
                .byte   0, 1, 0, 0                              ;   256 bytes, unsigned,
                .word   56320                                   ;   one cycle at 220 Hz,
                .byte   0, 0, 0, 0, 1, 0                        ;   looped whole
RSDEF:          .byte   SOUND_EFFECT_DEF, 13, SND_TAG, 0, 1     ; effect 0, a sample:
                .byte   0                                       ;   slot 0,
                .word   0                                       ;   at its own rate,
                .byte   1                                       ;   looped,
                .word   0, 500, 20                              ;   attack, hold, release (ms)
                .byte   0
RSPLAY:         .byte   SOUND_PLAY, 7, SND_TAG, 0, 255, 128, 0  ; effect 0: full, centre,
                .word   0                                       ;   priority 0, no offset,
                .byte   2                                       ;   on the ES5503
RSFREE:         .byte   SOUND_BANK_FREE, 1, SND_TAG, 2          ; every effect and sample
MBLOCK:         .byte   1, $01, 0, 0                            ; version 1, voice 0,
                .dword  1                                       ;   one entry
                .byte   0, $10, $09, $F0, 0, 0, 255, 128        ; a SID voice, a triangle
                .byte   $00, 69                                 ; voice 0: the A at 440 Hz,
                .dword  500000, 500000                          ;   half a second, gated
MBLEN           = * - MBLOCK

; U3: a key on the USB keyboard (USB_KEY polled).
U3:
                TB      "U3   A KEY ON THE USB KEYBOARD (ESC ON THE TERMINAL: SKIP)"
                ldx     #$B
                lda     #1
                jsr     HAS
                bcs     @r
                inc     T_ANY
                bit     USB_KEYCLR
                jsr     TZERO
@w:             lda     USB_KEY
                bmi     @key
                lda     ACIA_STATUS
                and     #$08
                beq     @t
                lda     ACIA_DATA
                and     #$7F
                jsr     ESCAPED
                bcs     @r
@t:             jsr     TTICK
                lda     T_ACC + 3
                cmp     #13
                bcc     @w
                TD      "NO KEY IN 30 S"
@r:             rts
@key:           bit     USB_KEYCLR
                and     #$7F
                sta     T_PAT
                SAY     ": $"
                lda     T_PAT
                jsr     HEX2
                jmp     TPASS

; Wait for every bit of T_N's mask (2 bytes) to be seen in T_SEEN, which the
; routine at A/X ORs what it sees into, polled: carry clear when they are;
; set at Esc on the terminal (the test skipped) or after 30 s (failed).
SEEALL:
                sta     T_D + 2
                stx     T_D + 3
                stz     T_SEEN
                stz     T_SEEN + 1
                jsr     TZERO
@w:             jsr     @poll
                lda     T_SEEN
                and     T_N
                cmp     T_N
                bne     @more
                lda     T_SEEN + 1
                and     T_N + 1
                cmp     T_N + 1
                bne     @more
                clc
                rts
@more:          lda     ACIA_STATUS
                and     #$08
                beq     @t
                lda     ACIA_DATA
                and     #$7F
                jsr     ESCAPED
                bcs     @r
@t:             jsr     TTICK
                lda     T_ACC + 3
                cmp     #13
                bcc     @w
                TD      "NOT EVERYTHING IN 30 S"
                sec
@r:             rts
@poll:          jmp     (T_D + 2)

; U4: the mouse moved and its two buttons pressed; the game port's buttons 0
; and 1 pressed.
U4:
                TB      "U4   MOVE THE MOUSE; PRESS ITS TWO BUTTONS, THEN PADDLE BUTTONS 0 AND 1 (ESC: SKIP)"
                ldx     #$B
                lda     #1
                jsr     HAS
                bcs     @r
                inc     T_ANY
                lda     USB_MX         ; the sums so far dropped
                lda     #$37           ; moved (bit 0), buttons 0-1 (1-2), paddle buttons 0-1 (4-5)
                sta     T_N
                stz     T_N + 1
                lda     #<MOUSE
                ldx     #>MOUSE
                jsr     SEEALL
                bcs     @r
                jmp     TPASS
@r:             rts
MOUSE:          lda     USB_MX         ; latches MX, MY: a move is any of them not 0
                ora     USB_MX + 1
                ora     USB_MY
                ora     USB_MY + 1
                beq     @btn
                lda     #$01
                tsb     T_SEEN
@btn:           lda     USB_MBTN
                and     #$03
                asl     a
                tsb     T_SEEN
                lda     USB_GAME
                and     #$30
                tsb     T_SEEN
                rts

; U5: the speaker toggled by the CPU at 1 kHz (USB_SPKR) for a second.
U5:
                TB      "U5   THE SPEAKER AT 1 KHZ: DID IT SOUND?"
                ldx     #$B
                lda     #1
                jsr     HAS
                bcs     @r
                inc     T_ANY
                lda     #<2000         ; 2,000 toggles, about 3,580 cycles apart
                sta     T_N
                lda     #>2000
                sta     T_N + 1
@tog:           bit     USB_SPKR
                ldy     #3
                ldx     #200
@d:             dex
                bne     @d
                dey
                bne     @d
                lda     T_N
                bne     @lo
                dec     T_N + 1
@lo:            dec     T_N
                lda     T_N
                ora     T_N + 1
                bne     @tog
                jmp     YESNO
@r:             rts

; SR3: each joystick's directions and fire, on both ports (the VIA's ports
; A and B, their DDRs left as inputs; a line pressed reads 0).
SR3:
                TB      "SR3  BOTH JOYSTICKS: EACH WAY AND FIRE (ESC: SKIP)"
                inc     T_ANY
                lda     #$1F
                sta     T_N
                sta     T_N + 1
                lda     #<STICKS
                ldx     #>STICKS
                jsr     SEEALL
                bcs     @r
                jmp     TPASS
@r:             rts
STICKS:         lda     VIA_ORA_NH
                eor     #$FF
                and     #$1F
                tsb     T_SEEN
                lda     VIA_ORB
                eor     #$FF
                and     #$1F
                tsb     T_SEEN + 1
                rts

; P3: the breakpoint: BPADDR P3's address ($BD1A5, RAM block 47, which stage
; 6 passed), a read that halts; RETURN (or CONT), then the read, which holds
; the CPU until CONT; STATUS's HIT, LADDR and LDATA checked; BPCTRL 0 and HIT
; cleared. Break meanwhile is the core's handler, whose put-back turns it off
; (diag.inc's PUTBACKM), as the BIOS's RESET does (LL1).
P3:
                TB      "P3   THE BREAKPOINT: THE CPU HALTS AT A READ OF $BD1A5"
                ldx     #$E
                lda     #1
                jsr     HAS
                bcs     @r
                lda     #P3BLOCK - 1   ; block 47 tested and passed
                jsr     GOODBLK
                bcs     @bad47
                cmp     #P3BLOCK
                beq     @ok47
@bad47:         TS      "RAM BLOCK 47 DIDN'T PASS STAGE 6"
@r:             rts
@ok47:          inc     T_ANY
                lda     #P3BLOCK
                sta     NB_WIN1
                lda     #P3BYTE
                sta     $4000 + P3OFS
                lda     #<DG_P3ADDR
                sta     CONSOLE_BPADDR
                lda     #>DG_P3ADDR
                sta     CONSOLE_BPADDR + 1
                lda     #^DG_P3ADDR
                sta     CONSOLE_BPADDR + 2
                lda     #$02           ; HIT cleared, then the read that halts armed
                sta     CONSOLE_STATUS
                lda     #$05           ; BP_ENABLE | BP_READ
                sta     CONSOLE_BPCTRL
                SAY     ": RETURN, THEN CONT WHEN IT HALTS "
@key:           jsr     KEYWAIT
                bcc     @off
                cmp     #$0D
                bne     @key
                lda     $4000 + P3OFS  ; the CPU halts here until CONT
                lda     CONSOLE_STATUS
                sta     T_PAT
                ldx     #2
@l:             lda     CONSOLE_LADDR,x
                sta     T_N,x
                dex
                bpl     @l
                lda     CONSOLE_LDATA
                sta     T_N + 3
                stz     CONSOLE_BPCTRL
                lda     #$02
                sta     CONSOLE_STATUS
                lda     #1
                sta     NB_WIN1
                lda     T_PAT
                and     #$02
                beq     @nohit
                lda     T_N
                cmp     #<DG_P3ADDR
                bne     @wrong
                lda     T_N + 1
                cmp     #>DG_P3ADDR
                bne     @wrong
                lda     T_N + 2
                cmp     #^DG_P3ADDR
                bne     @wrong
                lda     T_N + 3
                cmp     #P3BYTE
                bne     @wrong
                jmp     TPASS
@off:           stz     CONSOLE_BPCTRL
                lda     #$02
                sta     CONSOLE_STATUS
                lda     #1
                sta     NB_WIN1
                TD      "NO RETURN IN 30 S"
                rts
@nohit:         TD      "STATUS'S HIT ISN'T SET"
                rts
@wrong:         jsr     TDET
                SAY     "LATCHED $"
                lda     T_N + 2
                jsr     HEXDIG
                lda     T_N + 1
                jsr     HEX2
                lda     T_N
                jsr     HEX2
                SAY     " $"
                lda     T_N + 3
                jsr     HEX2
                SAY     ", NOT $BD1A5 $A7"
                jsr     CRLF
                jmp     SCROFF

        .include "diagcon.inc"
        .include "diagtest.inc"
        .include "diagsnd.inc"

; --- Tables and text -------------------------------------------------------------

STYPES:         .word   STSC, STHC, STXC
STSC:           .byte   "SDSC", 0
STHC:           .byte   "SDHC", 0
STXC:           .byte   "SDXC", 0
SN_SD1:         .byte   "SD1  ID $71; PRESENT AND READY", 0
SN_DK1:         .byte   "DK1  THE DISK II: ID $C1", 0
SN_U1:          .byte   "U1   ID $B1", 0
SN_U2:          .byte   "U2   THE PADDLES SETTLE:", 0
SN_P1:          .byte   "P1   ID $E1", 0
SN_P2:          .byte   "P2   A BIT WALKS ACROSS THE LIGHTS", 0
SEMPTY:         .byte   "NO CARD IN THE SLOT: SD2-SD4 NOT RUN", 0
SNOASK:         .byte   "SWITCH 16, OR DIAG [ASK] AT A>", 0

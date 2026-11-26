; The ROM's diagnostics, flash block 22 (hw/diag.md, plan 2, D3b): stages 7-10,
; the north bridge and the clock, DMA, the video card and the sound card.
; Then block 21, or, if block 21 isn't this build's, the end here.
;
; Every card is the core's: interrupts are off for the whole run, so each
; measurement polls, and every DMA channel is driven directly. A card is
; found by HW_PROBE's rule (its ID byte read twice); one that isn't there
; makes its stage SKIP, and one whose revision this build doesn't know is
; listed and skipped. All of RAM was tested by stages 3-6; what a test here
; needs for a buffer is in W0's block past its zero page and stack
; ($0200-$3FFF of the CPU's), once stage 8's D2 has filled it.

.setcpu "65C02"
.macpack longbranch
.include "backplane.inc"
.include "diag.inc"
OOHW_ROM        = 1
OOHW_BLK        = 22
.include "oohw.inc"

.import SKIPS

HZ              = 7159090              ; PHI2 at the board's /2 (hw/design.md)
FLASH30         = NB_ROM | NB_RO | 30  ; the font
FRAME           = 119437               ; one video frame in PHI2 cycles (800 x 525 at 25.175 MHz)
V2SAVE          = $0200                ; W0's: V2's registers kept
SRC4K           = $0400                ; W0's: a 4 KB pattern (V7, S3)
DST4K           = $1400                ; W0's: 4 KB read back (V7)
TEXTSAVE        = $2400                ; W0's: the screen's text while V4-V7 have card memory
TEXTLEN         = DG_ROWS * DG_COLS * 2

.segment "D22ZP": zeropage
T_REV:          .res    1
T_SEC0:         .res    1
T_SEC1:         .res    1
T_PAT:          .res    1
T_NBAD:         .res    1
T_BLKA:         .res    1
T_BLKB:         .res    1
T_SRC:          .res    3
T_DST:          .res    3
T_CNT:          .res    2
T_K:            .res    2
T_BANKS:        .res    2
T_SUM:          .res    2
T_RUNS:         .res    1              ; stage 10: the firmware came up (S1)
T_CH:           .res    1              ; D1: the channel
T_FIT:          .res    1              ;   how many are fitted
T_FITS:         .res    1              ;   which (bit n channel n)
T_UNFIT:        .res    1              ;   $FF if T_CH isn't, else 0
T_RND:          .res    32             ; NB9: the pool's 32 bytes
T_MS:           .res    4              ; NB10: MS0-MS3 as last read
T_MSB:          .res    4              ;   at the first SEC change, then the difference
T_MSP:          .res    3              ;   MS0-MS2 before, for the never-backwards test
                DTVARS
                DSVARS

.segment "D22"
                DG_HEADER 1, START
.assert * = $8025, error, "block 22's header"

START:
                cld
                ldx     #$FF
                txs
                jsr     HW_PROBE
                jsr     STAGE7
                jsr     STAGE8
                jsr     STAGE9
                jsr     STAGE10
                lda     #DG_B21
                ldx     #DE_START
                jsr     HANDOFF
                lda     #13            ; block 21 isn't this build's: its stages said, and the end here
                sta     DG_T0 + 2
                lda     #11
                jsr     NOTFLASH
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
                jmp     CBEG

; Module X's card at revision A: carry clear. Else the stage's end said (no
; card, or a revision not known), carry set.
CARDIS:
                sta     T_REV
                lda     HW_REV,x
                cmp     #HW_NONE
                beq     @none
                cmp     T_REV
                bne     @rev
                clc
                rts
@none:          lda     #<SNOCARD
                ldx     #>SNOCARD
                jsr     CSKIP
                sec
                rts
@rev:           jsr     CSKIPREV
                sec
                rts

; Carry clear if there is a DMA controller this build knows: revision 2 (ID
; $82 exactly).
HAVEDMA:
                lda     HW_REV + 8
                cmp     #2
                beq     @yes
                sec
                rts
@yes:           clc
                rts

; T_PA into T_SRC, or T_DST.
PA2SRC:         ldx     #2
@c:             lda     T_PA,x
                sta     T_SRC,x
                dex
                bpl     @c
                rts
PA2DST:         ldx     #2
@c:             lda     T_PA,x
                sta     T_DST,x
                dex
                bpl     @c
                rts

; W0's page A: T_SRC (SRCW0) or T_DST (DSTW0).
SRCW0:          tax
                lda     DG_W0
                jsr     PHYSB
                bra     PA2SRC
DSTW0:          tax
                lda     DG_W0
                jsr     PHYSB
                bra     PA2DST

; Channel 3, memory to memory: T_SRC to T_DST, T_CNT bytes (0: 65,536), A its
; MODE's other bits (DMA_HOLD, DMA_DEC, DMA_MODE_IRQ). Waited for: carry set
; if it never ended. CONTROL is the caller's, with SELECT 3 (DMA_ON | DMA_SEL3,
; or D5's burst): the window must be channel 3's.
DMA3:
                ora     #DMA_M2M
                sta     DMA_MODE
                ldx     #2
@a:             lda     T_SRC,x
                sta     DMA_ADDR,x
                lda     T_DST,x
                sta     DMA_DEST,x
                dex
                bpl     @a
                lda     T_CNT
                sta     DMA_COUNT
                lda     T_CNT + 1
                sta     DMA_COUNT + 1
                lda     #DMA_GO
                sta     DMA_CTRL
; The selected channel's CTRL (the caller's SELECT) waited for ENABLE to
; clear: carry set after about 13 ms (28 x 256 polls of about 13 cycles), for
; transfers of a few KB that take well under 1 ms.
DMAWAIT:
                lda     #28
                sta     T_REV
                ldy     #0
@w:             lda     DMA_CTRL
                and     #DMA_ENABLE
                beq     @done
                dey
                bne     @w
                dec     T_REV
                bne     @w
                sec
                rts
@done:          clc
                rts

; --- Stage 7: the north bridge and the clock ---------------------------------

STAGE7:
                lda     #7
                jsr     SBEGIN
                ldx     #$D
                lda     #3             ; ID $D3, revision 3; another SKIPs, not known to this build
                jsr     CARDIS
                bcc     @card
                rts
@card:          jsr     NB1
                jsr     NB2
                jsr     NB3
                jsr     NB45
                jsr     NB6
                jsr     NB7
                jsr     NB8
                jsr     NB9
                jsr     NB10
                jsr     NB12
                lda     #DR_PASS
                jmp     CEND

NB1:
                TB      "NB1  ID $"
                lda     NB_ID
                jsr     HEX2
                SAY     "; STATUS'S REVISION "
                lda     NB_STATUS
                lsr     a
                lsr     a
                lsr     a
                lsr     a
                pha
                jsr     PUTDEC
                pla
                cmp     HW_REV + $D
                bne     @bad
                jmp     TPASS
@bad:           TD      "STATUS'S BITS 4-7 AREN'T THE ID'S REVISION"
                rts

; W0's window bits (stage 4 tested W1's): every tested block's number, and
; its complement, at +$3FF0 through W1; then each block in W0 for a moment,
; with no RAM: the block in Y and W0's own in X.
NB2:
                TB      "NB2  W0'S WINDOW BITS: EACH BLOCK AS ITSELF"
                lda     #$FF
@w:             jsr     GOODBLK
                bcs     @check
                sta     NB_WIN1
                sta     $7FF0
                eor     #$FF
                sta     $7FF1
                eor     #$FF
                bra     @w
@check:         lda     #1
                sta     NB_WIN1
                lda     #$FF
@c:             jsr     GOODBLK
                bcs     @ok
                sta     DG_BLK
                tay
                ldx     DG_W0
                sty     NB_WIN0        ; no zero page, no stack, until W0 is back
                cpy     $3FF0
                bne     @bad
                tya
                eor     #$FF
                cmp     $3FF1
                bne     @bad
                stx     NB_WIN0
                lda     DG_BLK
                bra     @c
@bad:           lda     $3FF0
                stx     NB_WIN0
                sta     DG_MGOT
                jsr     TDET
                SAY     "BLOCK "
                lda     DG_BLK
                jsr     PUTDEC
                SAY     " IN W0 READ "
                lda     DG_MGOT
                jsr     HEX2
                SAY     ", NOT ITS NUMBER"
                jsr     CRLF
                jmp     SCROFF
@ok:            jmp     TPASS

; IRQL and IRQH, every card quiet: all 1s. Then a line that works: channel
; 3's terminal count with MODE's IRQ bit pulls IRQH bit 0 low, and clearing
; STATUS lets it go (interrupts are off: nothing is taken).
NB3:
                TB      "NB3  IRQL AND IRQH"
                lda     NB_IRQL
                and     NB_IRQH
                cmp     #$FF
                beq     @quiet
                jsr     TDET
                SAY     "IRQL "
                lda     NB_IRQL
                jsr     HEX2
                SAY     ", IRQH "
                lda     NB_IRQH
                jsr     HEX2
                SAY     ": A LINE IS LOW WITH EVERY CARD QUIET"
                jsr     CRLF
                jmp     SCROFF
@quiet:         jsr     HAVEDMA
                bcc     @dma
                SAY     "; NO DMA CONTROLLER"
                jmp     TPASS
@dma:           SAY     "; DMA'S LINE"
                lda     DMA_CONTROL    ; CONTROL kept, SELECT 3 for NB3GO, put back however it ends
                pha
                lda     #DMA_ON | DMA_SEL3
                sta     DMA_CONTROL
                jsr     NB3GO
                pla
                sta     DMA_CONTROL
                rts
NB3GO:          lda     #$30           ; 16 bytes, W0 $3000 to $3100, with MODE's IRQ
                jsr     SRCW0
                lda     #$31
                jsr     DSTW0
                lda     #16
                sta     T_CNT
                stz     T_CNT + 1
                lda     #DMA_MODE_IRQ
                jsr     DMA3
                lda     NB_IRQH
                and     #NB_IRQ_DMA
                bne     @noline
                lda     #DMA_CH3
                sta     DMA_STATUS
                lda     NB_IRQH
                and     #NB_IRQ_DMA
                beq     @held
                stz     DMA_MODE
                jmp     TPASS
@noline:        stz     DMA_MODE
                TD      "A TERMINAL COUNT WITH MODE'S IRQ BIT LEFT IRQH BIT 0 HIGH"
                rts
@held:          stz     DMA_MODE
                TD      "IRQH BIT 0 STAYED LOW WITH STATUS CLEARED"
                rts

; NB4, the clock chip: BUSY clears within 1 ms of an I2C read; SEC steps by
; one a second (four changes over NB5's measurement); LOST clear, or a
; warning. NB5, the CPU's clock: HW_CLOCK's cycles in a second of the
; DS3231's (its least-gap pair of four changes), within 0.1% of 7,159,090.
NB45:
                TB      "NB4  THE CLOCK CHIP: BUSY, SEC STEPS A SECOND"
                lda     #$11           ; the temperature's register: an I2C read
                sta     NB_RTCREG
                ldx     #0             ; 256 x about 28 cycles: 1 ms
@busy:          lda     NB_RTCCTL
                and     #NB_RTC_BUSY
                beq     @idle
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                dex
                bne     @busy
                TD      "BUSY STAYED ON PAST 1 MS"
@idle:          lda     NB_SEC
                sta     T_SEC0
                lda     #<NB_SEC
                sta     HW_WATCH
                lda     #>NB_SEC
                sta     HW_WATCH + 1
                lda     #1
                sta     HW_EDGES
                jsr     HW_CLOCK
                lda     #0
                rol     a
                sta     T_REV          ; 1: no change in time
                lda     NB_SEC
                sta     T_SEC1
                lda     T_REV
                jeq     @ticked
                TD      "SEC DIDN'T CHANGE WITHIN 2.3 S"
                TB      "NB5  THE CPU'S CLOCK BY THE CLOCK CHIP"
                TD      "NOT MEASURED: THE CLOCK CHIP DIDN'T TICK"
                rts
@ticked:        sed                    ; four steps on, in BCD, round at 60
                clc
                lda     T_SEC0
                adc     #4
                cmp     #$60
                bcc     @in
                sbc     #$60
@in:            cld
                cmp     T_SEC1
                beq     @steps
                jsr     TDET
                SAY     "SEC WENT FROM "
                lda     T_SEC0
                jsr     HEX2
                SAY     " TO "
                lda     T_SEC1
                jsr     HEX2
                SAY     " IN FOUR CHANGES"
                jsr     CRLF
                jsr     SCROFF
                bra     @nb5
@steps:         lda     NB_RTCCTL
                and     #NB_RTC_LOST
                bne     @lost
                jsr     TPASS
                bra     @nb5
@lost:          jsr     TINFO
                lda     #<SLOST
                ldx     #>SLOST
                jsr     NOTE9
@nb5:           TB      "NB5  CPU "
                ldx     #3
@cp:            lda     HW_CYC,x
                sta     T_N,x
                dex
                bpl     @cp
                jsr     DEC32
                SAY     " HZ BY THE CLOCK CHIP"
                lda     #<HZREF
                ldx     #>HZREF
                jsr     CLOSE
                bcs     @far
                jmp     TPASS
@far:           TD      "NOT WITHIN 0.1% OF 7159090"
                rts

; The CPU's clock against the video frame: the cycles over 60 frames (120
; changes of STATUS, its vertical blank's bit), HW_CLOCK's least-gap pair,
; within 0.1% of 60 frames of FRAME cycles; the line gives them as Hz
; (HW_FRAMEHZ: 800 x 525 pixel clocks at 25.175 MHz a frame).
NB6:
                TB      "NB6  CPU "
                lda     HW_REV + 9
                cmp     #2
                beq     @video
                SAY     "BY THE VIDEO FRAME"
                TS      "NO VIDEO CARD"
                rts
@video:         lda     #<VIDEO_STATUS
                sta     HW_WATCH
                lda     #>VIDEO_STATUS
                sta     HW_WATCH + 1
                lda     #120
                sta     HW_EDGES
                jsr     HW_CLOCK
                bcc     @frames
                SAY     "BY THE VIDEO FRAME"
                TD      "NO 60 FRAMES WITHIN 2.3 S"
                rts
@frames:        ldx     #3             ; 60 frames' cycles against 60 x FRAME
@cyc:           lda     HW_CYC,x
                sta     T_N,x
                dex
                bpl     @cyc
                lda     #<FRAMESREF
                ldx     #>FRAMESREF
                jsr     CLOSE
                lda     #0
                rol     a
                sta     T_REV          ; 1: too far
                jsr     HW_FRAMEHZ     ; as Hz, for the line
                ldx     #3
@cp:            lda     HW_NUM,x
                sta     T_N,x
                dex
                bpl     @cp
                jsr     DEC32
                SAY     " HZ BY THE VIDEO FRAME"
                lda     T_REV
                bne     @far
                jmp     TPASS
@far:           TD      "60 FRAMES AREN'T 60 X 119437 CYCLES WITHIN 0.1%"
                rts

; WAIT0-3 and VEC written with two patterns and read back, then put back
; (WAIT0-3 0, VEC on block 23 for Break). The options WAIT3 carried were read
; by stage 2 already.
NB7:
                TB      "NB7  WAIT0-3 AND VEC READ BACK"
                ldx     #3
@w:             lda     #$55
                sta     NB_WAIT0,x
                cmp     NB_WAIT0,x
                bne     @badw
                lda     #$AA
                sta     NB_WAIT0,x
                cmp     NB_WAIT0,x
                bne     @badw
                stz     NB_WAIT0,x
                dex
                bpl     @w
                lda     #$55
                sta     NB_VEC
                ldy     NB_VEC
                lda     #$AA
                sta     NB_VEC
                ldx     NB_VEC
                lda     #DG_B23
                sta     NB_VEC
                cpy     #$55
                bne     @badv
                cpx     #$AA
                bne     @badv
                jmp     TPASS
@badw:          sta     DG_MWANT
                lda     NB_WAIT0,x
                sta     DG_MGOT
                stz     NB_WAIT0,x
                stx     T_REV
                jsr     TDET
                SAY     "WAIT"
                lda     T_REV
                jsr     PUTDEC
                jsr     RWBITS
                jmp     SCROFF
@badv:          TD      "VEC DIDN'T READ BACK AS WRITTEN"
                rts

; A write through a read-only window (NB_RO) onto a tested RAM block leaves
; the block as it was.
NB8:
                TB      "NB8  A READ-ONLY WINDOW KEEPS ITS RAM"
                lda     #$FF
@b:             jsr     GOODBLK
                bcs     @none
                cmp     DG_W0
                beq     @b
                sta     DG_BLK
                sta     NB_WIN1
                lda     $5A5A
                sta     T_REV
                lda     DG_BLK
                ora     #NB_RO
                sta     NB_WIN1
                lda     T_REV
                eor     #$FF
                sta     $5A5A          ; dropped
                lda     DG_BLK
                sta     NB_WIN1
                lda     $5A5A
                ldx     #1
                stx     NB_WIN1
                cmp     T_REV
                bne     @bad
                jmp     TPASS
@bad:           TD      "THE WRITE CHANGED THE BLOCK"
                rts
@none:          TS      "NO TESTED BLOCK"
                rts

; NB9, the random number chip (hw/design.md, "The random source"): no chip, SKIP; a chip not locked, FAIL. Then the
; pool's 32 bytes, read through RNGDATA: FAIL if they are all one value or one 4-byte group repeated (an unlocked
; part's fixed pattern is such a group). Then DRAW: BUSY must clear with READY set within 150 ms; FAIL on FAULT. Its
; sample is in D22ZP (T_RND).
NB9:
                TB      "NB9  THE RANDOM NUMBER CHIP"
                lda     NB_RNGST
                and     #NB_RNG_CHIP
                bne     @chip
                TS      "NO RANDOM NUMBER CHIP"
                rts
@chip:          lda     NB_RNGST
                and     #NB_RNG_LOCKED
                bne     @locked
                TD      "NOT LOCKED"
                rts
@locked:        ldx     #0
@byte:          phx
                jsr     RNGW           ; a byte there (the pool full on a good board)
                plx
                bcs     @slow
                lda     NB_RNGDATA     ; a load: the read takes the byte
                sta     T_RND,x
                inx
                cpx     #32
                bne     @byte
                ldx     #31            ; all one value?
@one:           lda     T_RND,x
                cmp     T_RND
                bne     @group
                dex
                bne     @one
                TD      "32 BYTES OF ONE VALUE"
                rts
@group:         ldx     #4             ; one 4-byte group repeated?
@rep:           lda     T_RND,x
                cmp     T_RND - 4,x
                bne     @draw
                inx
                cpx     #32
                bne     @rep
                TD      "ONE 4-BYTE GROUP REPEATED"
                rts
@draw:          lda     #NB_RNG_DRAW
                sta     NB_RNGST
                jsr     RNGW
                bcs     @slow
                jmp     TPASS
@slow:          cmp     #0
                bne     @fault
                TD      "NO FRESH BYTES WITHIN 150 MS"
                rts
@fault:         TD      "THE DRAW FAILED (FAULT)"
                rts

; Wait up to about 150 ms (191 x 256 passes of 22 cycles) for the pool: READY with BUSY clear, carry clear; FAULT,
; carry set and A not 0; the time out, carry set and A 0. A and T_K.
RNGW:           stz     T_K
                lda     #191
                sta     T_K + 1
@w:             lda     NB_RNGST
                bit     #NB_RNG_FAULT
                bne     @fault
                and     #NB_RNG_READY | NB_RNG_BUSY
                cmp     #NB_RNG_READY
                beq     @ok
                dec     T_K
                bne     @w
                dec     T_K + 1
                bne     @w
                lda     #0
                sec
                rts
@fault:         sec
                rts
@ok:            clc
                rts

; NB10, the timebase (hw/design.md, "The north bridge, revision 3"). With interrupts off throughout: MSF sets within
; 1.1 ms of being cleared, and a read doesn't clear it; with MSIE, IRQH bit 5 reads 0 while MSF is set and 1 once it
; is cleared, then MSIE off. Then across three of the DS3231's seconds (three SEC changes): MSS reads 0 or 1 at each
; change and never more than 999; MS0-MS3 gain 1,000 +/- 1 between the first change and the second; MS0-MS2, read
; through the latch, never go backwards. Then SYNC set. A FAIL names the step. Its counts are in D22ZP (T_MS, T_MSB,
; T_MSP).
NB10:
                TB      "NB10 THE TIMEBASE"
                lda     #NB_TB_MSF | NB_TB_OVR ; both cleared, MSIE off
                sta     NB_TBCTL
                lda     NB_TBCTL
                bpl     @wait
                lda     #NB_TB_MSF | NB_TB_OVR ; a tick just then: once more
                sta     NB_TBCTL
                lda     NB_TBCTL
                bpl     @wait
                TD      "MSF: A 1 WRITTEN DIDN'T CLEAR IT"
                rts
@wait:          jsr     MSFW
                bcc     @set
                TD      "MSF: NOT SET WITHIN 1.1 MS"
                rts
@set:           lda     NB_TBCTL
                bmi     @irq
                TD      "MSF: A READ CLEARED IT"
                rts
@irq:           lda     #NB_TB_MSIE    ; MSF left set, MSIE on: the line low
                sta     NB_TBCTL
                lda     NB_IRQH
                and     #NB_IRQ_NB
                beq     @low
                lda     #NB_TB_MSF | NB_TB_OVR
                sta     NB_TBCTL
                TD      "IRQH BIT 5: HIGH WITH MSF AND MSIE SET"
                rts
@low:           lda     #NB_TB_MSF | NB_TB_OVR | NB_TB_MSIE ; the acknowledge: the line high again
                sta     NB_TBCTL
                lda     NB_IRQH
                ldx     #NB_TB_MSF | NB_TB_OVR ; MSIE off
                stx     NB_TBCTL
                and     #NB_IRQ_NB
                bne     @secs
                TD      "IRQH BIT 5: STILL LOW WITH MSF CLEARED"
                rts
@secs:          stz     T_REV          ; the SEC changes seen
                lda     NB_SEC
                sta     T_SEC0
                jsr     MSREAD
                jsr     MSKEEP
                stz     T_K            ; the give-up: 6 x 65,536 passes of the same second (about 4 s)
                stz     T_K + 1
                lda     #6
                sta     T_SEC1
@loop:          lda     NB_SEC         ; SEC, then MSS, from its latch
                tax
                lda     NB_MSSH
                cmp     #3
                jcc     @in
                jne     @past
                lda     NB_MSSL
                cmp     #$E8
                jcs     @past
@in:            cpx     T_SEC0
                jeq     @same
                stx     T_SEC0         ; a new second: MSS 0 or 1
                lda     NB_MSSH
                jne     @late
                lda     NB_MSSL
                cmp     #2
                jcs     @late
                inc     T_REV
                lda     T_REV
                cmp     #1
                jne     @two
                jsr     MSREAD         ; the first change: MS kept
                jsr     MSKEEP
                ldx     #3
@k:             lda     T_MS,x
                sta     T_MSB,x
                dex
                bpl     @k
                jmp     @loop
@two:           cmp     #2
                jne     @three
                jsr     MSREAD         ; the second: 1,000 +/- 1 on
                sec
                lda     T_MS
                sbc     T_MSB
                sta     T_MSB
                lda     T_MS + 1
                sbc     T_MSB + 1
                sta     T_MSB + 1
                lda     T_MS + 2
                sbc     T_MSB + 2
                ora     T_MS + 3
                jne     @steps         ; (T_MS + 3 is 0 here: MS is under 2^24 within 4.6 hours of a reset)
                lda     T_MSB + 1
                cmp     #>1000
                jne     @steps
                lda     T_MSB
                cmp     #<999
                jcc     @steps
                cmp     #<1001 + 1
                jcs     @steps
                jsr     MSKEEP
                jmp     @loop
@three:         lda     NB_TBCTL
                and     #NB_TB_SYNC
                bne     @sync
                TD      "SYNC: CLEAR"
                rts
@sync:          jmp     TPASS
@same:          jsr     MSREAD         ; MS0-MS2 never backwards
                lda     T_MS + 2
                cmp     T_MSP + 2
                jcc     @back
                jne     @fwd
                lda     T_MS + 1
                cmp     T_MSP + 1
                jcc     @back
                jne     @fwd
                lda     T_MS
                cmp     T_MSP
                jcc     @back
@fwd:           jsr     MSKEEP
                dec     T_K
                jne     @loop1
                dec     T_K + 1
                jne     @loop1
                dec     T_SEC1
                jne     @loop1
                TD      "SEC: NOT THREE CHANGES IN 4 S"
                rts
@loop1:         jmp     @loop
@past:          TD      "MSS: PAST 999"
                rts
@late:          TD      "MSS: NOT 0 OR 1 AT A NEW SECOND"
                rts
@steps:         TD      "MS: NOT 1000 +/- 1 ACROSS A SECOND"
                rts
@back:          TD      "MS: BACKWARDS ACROSS A CARRY"
                rts

; NB12, the common hole (hw/design.md, "The common hole"; NB11 is the deferred WINnH and MAP check's number). A tested
; RAM block's top 2 KB, seen through W1 at $7800-$7FFF, gets a tag at $7800 and its complement at $7FF0, and VEC's
; vectors at $7FFA (a Break while the hole is on still lands in the core). With interrupts off, HOLE EN on it: $F800
; and $FFF0 must read the tags and $F7FF W3's own byte; then with RO set a write at $F800 must leave the block as it
; was; and with HOLE 0, $F800 is W3's again. HOLE is 0 and W1 on block 1 at the end, whatever the outcome.
NB12:
                TB      "NB12 THE COMMON HOLE"
                lda     #$FF
@b:             jsr     GOODBLK
                jcs     @none
                cmp     DG_W0
                beq     @b
                sta     DG_BLK
                lda     NB_VEC         ; VEC's block in W1: its six vector bytes on the stack
                sta     NB_WIN1
                ldx     #5
@v:             lda     $7FFA,x
                pha
                dex
                bpl     @v
                lda     DG_BLK
                sta     NB_WIN1
                ldx     #0
@v2:            pla
                sta     $7FFA,x
                inx
                cpx     #6
                bne     @v2
                lda     DG_BLK         ; the tags
                sta     $7800
                eor     #$FF
                sta     $7FF0
                lda     $F7FF          ; W3's bytes, with the hole off
                sta     T_REV
                lda     $F800
                sta     T_N
                php
                sei
                lda     DG_BLK
                ora     #NB_HOLE_EN
                sta     NB_HOLE
                lda     $F800
                cmp     DG_BLK
                jne     @badmap
                lda     $FFF0
                eor     #$FF
                cmp     DG_BLK
                jne     @badmap
                lda     $F7FF
                cmp     T_REV
                jne     @badmap
                lda     DG_BLK
                ora     #NB_HOLE_EN | NB_HOLE_RO
                sta     NB_HOLE
                lda     DG_BLK
                eor     #$FF
                sta     $F800          ; dropped
                stz     NB_HOLE
                plp
                lda     $F800
                cmp     T_N
                jne     @badoff
                lda     $7800          ; the block, through W1
                ldx     #1
                stx     NB_WIN1
                cmp     DG_BLK
                jne     @badro
                jmp     TPASS
@badmap:        stz     NB_HOLE
                plp
                ldx     #1
                stx     NB_WIN1
                TD      "F800-FFFF DIDN'T SHOW THE BLOCK'S TOP"
                rts
@badoff:        ldx     #1
                stx     NB_WIN1
                TD      "HOLE 0 DIDN'T GIVE F800 BACK TO W3"
                rts
@badro:         TD      "A WRITE WITH RO SET CHANGED THE BLOCK"
                rts
@none:          TS      "NO TESTED BLOCK"
                rts

; MSF set within 1.1 ms: 716 passes of 11 cycles (204, then 256 twice), carry clear; else carry set. A, X, Y.
MSFW:           ldx     #204
                ldy     #3
@m:             lda     NB_TBCTL
                bmi     @ok
                dex
                bne     @m
                dey
                bne     @m
                sec
                rts
@ok:            clc
                rts

; MS0-MS3 into T_MS, MS0 first (its read latches the rest). A and X.
MSREAD:         ldx     #0
@r:             lda     NB_MS0,x
                sta     T_MS,x
                inx
                cpx     #4
                bne     @r
                rts
; T_MS's low three bytes into T_MSP. A and X.
MSKEEP:         ldx     #2
@c:             lda     T_MS,x
                sta     T_MSP,x
                dex
                bpl     @c
                rts

; --- Stage 8: DMA --------------------------------------------------------------

STAGE8:
                lda     #8
                jsr     SBEGIN
                ldx     #8
                lda     #2             ; revision 2 (ID $82)
                jsr     CARDIS
                bcc     @card
                rts
@card:          lda     DMA_CONTROL    ; CONTROL kept for the stage, put back at its end
                pha
                lda     #DMA_ON
                sta     DMA_CONTROL
                jsr     D1             ; (it selects each channel itself, and leaves the window on one)
                lda     #DMA_ON | DMA_SEL3 ; D2-D5 are channel 3's: SELECT 3, written here, not assumed
                sta     DMA_CONTROL
                jsr     D2
                jsr     D3
                jsr     D4
                jsr     D5
                lda     #DMA_ON | DMA_SEL3 ; D5 writes CONTROL itself: SELECT 3 again before
                sta     DMA_CONTROL    ;   anything after it touches channel 3
                pla
                sta     DMA_CONTROL
                lda     #DR_PASS
                jmp     CEND

; D1: each channel through SELECT 7 down to 0, the master off. First the
; channels fitted are counted: one whose window reads $FF at every offset isn't
; (B2 had SELECT 4-7 so, B3 fits all 8; a fitted channel's CARD has bits 7-5 0,
; so it never reads all $FF), and the count goes on D1's line. Then each register of a
; fitted channel, ADDR and DEST (24 bits), COUNT, MODE and CARD (5 bits), is
; written with $55s and $AAs and read back, and its reserved bytes ($03, $0B,
; $0D-$0F) read $FF whatever is written; a channel not fitted reads $FF at
; every offset whatever is written. Any other mix fails, naming the channel
; and the register. Each register goes back to 0, and CARD to its reset
; module (7, 9, A, then none), not 0. Last, CONTROL's BURST and SELECT bits
; both ways, the master off and bit 7 written 1 (it reads 0); then CONTROL on.
D1:
                TB      "D1   ID $82; REGISTERS READ BACK ON "
                stz     T_FIT
                ldx     #7
@scan:          txa
                asl     a              ; SELECT X, the master off
                sta     DMA_CONTROL
                ldy     #$0F
@ff:            lda     DMA,y
                cmp     #$FF
                bne     @fit
                dey
                bpl     @ff
                clc                    ; $FF throughout: not fitted
                bra     @next
@fit:           inc     T_FIT
                sec
@next:          rol     T_FITS         ; ...so channel n's bit ends as bit n
                dex
                bpl     @scan
                lda     T_FIT
                jsr     PUTDEC
                SAY     " CHANNELS"
                lda     T_FIT
                bne     @some
                lda     #DMA_ON
                sta     DMA_CONTROL
                TD      "NO CHANNEL'S REGISTERS ANSWER"
                rts
@some:          ldx     #7
@ch:            stx     T_CH
                txa
                asl     a              ; SELECT T_CH, the master off
                sta     DMA_CONTROL
                asl     T_FITS         ; channel 7's bit first: carry set if fitted
                lda     #0
                sbc     #0             ; fitted 0; not fitted $FF, every offset's want
                sta     T_UNFIT
                ldy     #DREGS - 1
@reg:           ldx     DREGOFS,y
                lda     #$55
                jsr     @try
                bne     @bad
                lda     #$AA
                jsr     @try
                bne     @bad
                stz     DMA,x
                dey
                bpl     @reg
                jsr     D1CARD
                ldx     T_CH
                dex
                bpl     @ch
                lda     #$AA           ; bit 7 1, BURST 010, SELECT 101, the master off
                sta     DMA_CONTROL
                ldx     DMA_CONTROL
                lda     #$D4           ; bit 7 1, BURST 101, SELECT 010, the master off
                sta     DMA_CONTROL
                ldy     DMA_CONTROL
                lda     #DMA_ON
                sta     DMA_CONTROL
                cpx     #$2A
                bne     @badc
                cpy     #$54
                bne     @badc
                jmp     TPASS
; Register X (Y its index in DREGOFS) written with A and its mask, read back
; against what it should read: Z set if it did.
@try:           and     DREGMSK,y
                sta     DMA,x
                ora     DREGFIX,y      ; a reserved byte reads $FF
                ora     T_UNFIT        ;   as every byte of a channel not fitted does
                sta     DG_MWANT
                lda     DMA,x
                sta     DG_MGOT
                cmp     DG_MWANT
                rts
@bad:           stx     T_REV
                stz     DMA,x
                jsr     D1CARD
                lda     #DMA_ON
                sta     DMA_CONTROL
                jsr     TDET
                SAY     "CHANNEL "
                lda     T_CH
                jsr     PUTDEC
                SAY     ", REGISTER $C4"
                lda     T_REV
                jsr     HEX2
                jsr     RWBITS
                jmp     SCROFF
@badc:          TD      "CONTROL'S BURST AND SELECT BITS DIDN'T READ BACK"
                rts

; D1's channel T_CH (selected): CARD back to its reset module, 7, 9 or A for
; channels 0-2, none for 3-7 (as QUIET puts them).
D1CARD:
                lda     #DMA_NONE
                ldx     T_CH
                cpx     #3
                bcs     @set
                txa                    ; 0, 1, 2: 7, 9, A (carry set from 1 on)
                cmp     #1
                adc     #DMA_MOD_SD
@set:           sta     DMA_CARD
                rts

; D2: every RAM block filled by channel 3 with HOLD (the source a byte of the
; zero page, so the fill is the controller's alone), each its own byte (its
; number XOR $A5), all before any is read; then the CPU checks each, as
; stage 6 names a fault. W0's block from +$200: its zero page and stack stay.
; Blocks stages 3-6 failed are filled and checked too (a fault under DMA is
; named as stage 6 named it), but not one that answers as W0's (ASW0): its
; fill would land on the zero page and the stack.
D2:
                TB      "D2   DMA FILL OF ALL RAM, CHECKED BY THE CPU"
                lda     #$FF
@fill:          jsr     RAMBLK
                jcs     @check
                sta     DG_BLK
                cmp     DG_W0
                beq     @own
                jsr     ASW0           ; a block that answers as W0's (since stage 6 moved it): not filled
                bcs     @asw0
                lda     DG_BLK
                jsr     ASALIAS        ; a failed block that answers as another: not filled (review P3)
                bcc     @own
                lda     DG_BLK
                jmp     @fill
@asw0:
                jsr     TDET
                SAY     "BLOCK "
                lda     DG_BLK
                jsr     PUTDEC
                SAY     " ANSWERS AS BLOCK "
                lda     DG_W0
                jsr     PUTDEC
                SAY     ", W0'S: NOT FILLED"
                jsr     CRLF
                jsr     SCROFF
                lda     DG_BLK
                jmp     @fill
@own:           lda     DG_BLK
                eor     #$A5
                sta     T_PAT
                lda     #0
                jsr     SRCW0
                lda     #<T_PAT
                sta     T_SRC          ; the zero page's byte, W0's page 0
                ldx     #0
                lda     #$40
                ldy     DG_BLK
                cpy     DG_W0
                bne     @whole
                ldx     #$02
                lda     #$3E
@whole:         stz     T_CNT
                sta     T_CNT + 1
                tya
                jsr     PHYSB
                jsr     PA2DST
                lda     #DMA_HOLD
                jsr     DMA3
                bcs     @stuck
                lda     DG_BLK
                jmp     @fill
@check:         stz     T_NBAD
                lda     #$FF
@c:             jsr     RAMBLK
                bcs     @done
                sta     DG_BLK
                sta     NB_WIN1
                eor     #$A5
                sta     T_PAT
                stz     HW_PTR
                lda     #$40
                ldx     DG_BLK
                cpx     DG_W0
                bne     @from
                lda     #$42
@from:          sta     HW_PTR + 1
                ldy     #0
@byte:          lda     (HW_PTR),y
                cmp     T_PAT
                bne     @wrong
                iny
                bne     @byte
                inc     HW_PTR + 1
                lda     HW_PTR + 1
                cmp     #$80
                bne     @byte
                lda     DG_BLK
                bra     @c
@wrong:         sta     DG_MGOT
                lda     T_PAT
                sta     DG_MWANT
                lda     DG_BLK
                sta     DG_T0
                sty     DG_T0 + 1
                lda     HW_PTR + 1
                sec
                sbc     #$40
                sta     DG_T0 + 2
                lda     DG_BLK         ; every faulty block failed (review P3); the first eight said
                jsr     MARKBAD
                lda     T_NBAD
                cmp     #8
                bcs     @many
                inc     T_NBAD
                jsr     TRAMF
@many:          lda     DG_BLK         ; the block's next bytes would say little more: the next block
                bra     @c
@done:          lda     #1
                sta     NB_WIN1
                jmp     TEND0
@stuck:         lda     #1
                sta     NB_WIN1
                TD      "CHANNEL 3 NEVER REACHED ITS TERMINAL COUNT"
                rts

; Carry set if block A, one stages 3-6 failed, answers as another block: its
; +$3FF0 holds another block's number and +$3FF1 its complement, NB2's tags
; of the blocks that passed (the last written of two blocks that answer as
; one), so filling it would write over that block, a good one, which D2 would
; then name. It is kept out of D2's check and what follows (SKIPS); stage 4
; has named the two blocks already.
ASALIAS:
                pha
                jsr     BLKBIT
                and     DG_BAD,x
                beq     @no
                pla
                pha
                sta     NB_WIN1
                lda     $7FF0
                eor     #$FF
                cmp     $7FF1
                bne     @no
                lda     $7FF0
                sta     T_REV
                pla
                pha
                cmp     T_REV          ; another block's tag
                beq     @no
                pla
                jsr     BLKBIT
                ora     SKIPS,x
                sta     SKIPS,x
                lda     #1
                sta     NB_WIN1
                sec
                rts
@no:            lda     #1
                sta     NB_WIN1
                pla
                clc
                rts

; Carry set if block A answers as W0's block: a byte written through W1
; changes W0's own at $07, which nothing keeps (as stage 4's ALIASW0 asks of
; the block W0 had then); the block is then kept out of what follows
; (SKIPS). W1 back on block 1.
ASW0:
                sta     NB_WIN1
                lda     $4007
                pha
                lda     $07
                eor     #$FF
                sta     $4007
                cmp     $07
                beq     @alias
                pla
                sta     $4007
                lda     #1
                sta     NB_WIN1
                clc
                rts
@alias:         eor     #$FF           ; W0's byte back (it is the other's too)
                sta     $07
                pla
                lda     #1
                sta     NB_WIN1
                lda     DG_BLK
                jsr     BLKBIT
                ora     SKIPS,x
                sta     SKIPS,x
                sec
                rts

; Two tested blocks other than W0's, in T_BLKA and T_BLKB: carry set if there
; aren't two.
TWOBLK:
                lda     #$FF
@a:             jsr     GOODBLK
                bcs     @no
                cmp     DG_W0
                beq     @a
                sta     T_BLKA
@b:             jsr     GOODBLK
                bcs     @no
                cmp     DG_W0
                beq     @b
                sta     T_BLKB
                clc
@no:            rts

; D3: channel 3 copies 4 KB of block A (address in address) to block B, up;
; then down (MODE's DEC), A's last byte first, into B's next 4 KB.
D3:
                TB      "D3   DMA COPY BETWEEN TWO BLOCKS, UP AND DOWN"
                jsr     TWOBLK
                bcc     @two
                TS      "NOT TWO TESTED BLOCKS"
                rts
@two:           lda     T_BLKA
                sta     NB_WIN1
                sta     HW_KEY
                jsr     RUN4K
                jsr     HW_AIAW
                lda     T_BLKA         ; up: A +0 to B +0
                ldx     #0
                jsr     PHYSB
                jsr     PA2SRC
                lda     T_BLKB
                ldx     #0
                jsr     PHYSB
                jsr     PA2DST
                stz     T_CNT
                lda     #$10
                sta     T_CNT + 1
                lda     #0
                jsr     DMA3
                lda     T_BLKB
                sta     NB_WIN1
                lda     T_BLKA
                sta     HW_KEY
                jsr     RUN4K
                jsr     HW_AIAC
                bcs     @badup
                lda     T_BLKA         ; down: A +$0FFF to B +$1FFF, a byte at a time down
                ldx     #$0F
                jsr     PHYSB
                jsr     PA2SRC
                lda     #$FF
                sta     T_SRC
                lda     T_BLKB
                ldx     #$1F
                jsr     PHYSB
                jsr     PA2DST
                lda     #$FF
                sta     T_DST
                lda     #DMA_DEC
                jsr     DMA3
                lda     T_BLKB         ; B +$1000-$1FFF is A +$0000-$0FFF: its page $10 more
                sta     NB_WIN1
                lda     T_BLKA
                eor     #$10
                sta     HW_KEY
                stz     HW_PTR
                lda     #$50
                sta     HW_PTR + 1
                stz     HW_LEN
                lda     #$10
                sta     HW_LEN + 1
                jsr     HW_AIAC
                bcs     @baddn
                lda     #1
                sta     NB_WIN1
                jmp     TPASS
@badup:         lda     #<SCOPYUP
                ldx     #>SCOPYUP
                bra     @say
@baddn:         lda     #<SCOPYDN
                ldx     #>SCOPYDN
@say:           pha
                phx
                lda     #1
                sta     NB_WIN1
                plx
                pla
                jmp     TDETS

RUN4K:          stz     HW_PTR
                lda     #$40
                sta     HW_PTR + 1
                stz     HW_LEN
                lda     #$10
                sta     HW_LEN + 1
                rts

; D4: a fill of 2 KB timed by timer 2, less a fill of none (one byte, the
; same cost to start): 2 cycles a byte, 4,096 cycles.
D4:
                TB      "D4   A 2 KB DMA FILL: "
                jsr     TWOBLK
                bcc     @two
                TS      "NOT TWO TESTED BLOCKS"
                rts
@two:           lda     #0
                jsr     SRCW0
                lda     #<T_PAT
                sta     T_SRC
                lda     T_BLKA
                ldx     #0
                jsr     PHYSB
                jsr     PA2DST
                lda     #1
                sta     T_CNT
                stz     T_CNT + 1
                jsr     @timed
                lda     T_N
                sta     T_D            ; (T_D: the short fill's cycles)
                lda     T_N + 1
                sta     T_D + 1
                lda     #<2049
                sta     T_CNT
                lda     #>2049
                sta     T_CNT + 1
                jsr     @timed
                sec                    ; the long less the short
                lda     T_N
                sbc     T_D
                sta     T_N
                lda     T_N + 1
                sbc     T_D + 1
                sta     T_N + 1
                stz     T_N + 2
                stz     T_N + 3
                jsr     DEC32
                SAY     " CYCLES FOR 2048 BYTES"
                lda     #<DMAREF
                ldx     #>DMAREF
                jsr     CLOSE
                bcs     @far
                jmp     TPASS
@far:           TD      "NOT 2 CYCLES A BYTE"
                rts
; One fill of T_CNT bytes, timed: T_N (2 bytes) its cycles.
@timed:         jsr     TZERO
                lda     #DMA_HOLD
                jsr     DMA3
                jsr     T2NOW
                sta     T_N
                stx     T_N + 1
                sec
                lda     T_PREV
                sbc     T_N
                sta     T_N
                lda     T_PREV + 1
                sbc     T_N + 1
                sta     T_N + 1
                rts

; D5: STATUS's terminal count, set at the end and cleared by a 1; then ABORT
; part way through a copy, with a burst of one byte so the CPU runs between
; them: COUNT reads what was left, and no terminal count.
D5:
                TB      "D5   TERMINAL COUNT; ABORT PART WAY"
                jsr     TWOBLK
                bcc     @two
                TS      "NOT TWO TESTED BLOCKS"
                rts
@two:           lda     #DMA_CH3
                sta     DMA_STATUS
                lda     T_BLKA
                ldx     #0
                jsr     PHYSB
                jsr     PA2SRC
                lda     T_BLKB
                ldx     #0
                jsr     PHYSB
                jsr     PA2DST
                lda     #16
                sta     T_CNT
                stz     T_CNT + 1
                lda     #0
                jsr     DMA3
                lda     DMA_STATUS
                and     #DMA_CH3
                jeq     @notc
                lda     #DMA_CH3
                sta     DMA_STATUS
                lda     DMA_STATUS
                and     #DMA_CH3
                jne     @stays
                lda     #DMA_MASTER | DMA_SEL3 ; a burst of one: the CPU between each byte; SELECT 3
                sta     DMA_CONTROL    ;   kept (DMA_MASTER alone would be SELECT 0, channel 0's window)
                ldx     #2
@a:             lda     T_SRC,x
                sta     DMA_ADDR,x
                lda     T_DST,x
                sta     DMA_DEST,x
                dex
                bpl     @a
                stz     DMA_COUNT
                lda     #$10
                sta     DMA_COUNT + 1
                lda     #DMA_M2M
                sta     DMA_MODE
                lda     #DMA_GO
                ldx     #DMA_CH3
                sta     DMA_CTRL
                stx     DMA_ABORT
                lda     DMA_COUNT
                sta     T_N
                lda     DMA_COUNT + 1
                sta     T_N + 1
                ldy     DMA_CTRL
                lda     #DMA_ON | DMA_SEL3
                sta     DMA_CONTROL
                tya
                and     #DMA_ENABLE
                jne     @running
                lda     DMA_STATUS
                and     #DMA_CH3
                jne     @ended
                lda     T_N            ; 0 < left < 4096
                ora     T_N + 1
                jeq     @ended
                lda     T_N + 1
                cmp     #$10
                jcs     @none
                SAY     ": "
                stz     T_N + 2
                stz     T_N + 3
                jsr     DEC32
                SAY     " LEFT"
                jmp     TPASS
@notc:          TD      "NO TERMINAL COUNT IN STATUS"
                rts
@stays:         TD      "STATUS'S TERMINAL COUNT DIDN'T CLEAR"
                rts
@running:       TD      "ABORT LEFT THE CHANNEL ENABLED"
                rts
@ended:         TD      "THE COPY ENDED: ABORT CAME TOO LATE OR DID NOTHING"
                rts
@none:          TD      "NOTHING WAS MOVED BEFORE THE ABORT"
                rts

; --- Stage 9: the video card ----------------------------------------------------

STAGE9:
                lda     #9
                jsr     SBEGIN
                ldx     #9
                lda     #2
                jsr     CARDIS
                bcc     @card
                rts
@card:          jsr     V1
                jsr     V2
                jsr     V3
                jsr     TEXTOUT
                lda     DG_SCR         ; card memory is the tests' until REDRAW: no screen meanwhile
                pha
                and     #$3F
                sta     DG_SCR
                jsr     V4
                jsr     V5
                jsr     V6
                jsr     V7
                jsr     REDRAW
                pla
                sta     DG_SCR
                lda     #DR_PASS
                jmp     CEND

V1:
                TB      "V1   ID $92; MEMSIZE "
                lda     VIDEO_MEMSIZE
                sta     T_N
                jsr     PUTDEC
                SAY     ": "
                stz     T_N + 1        ; x 64 KB
                ldx     #6
@x64:           asl     T_N
                rol     T_N + 1
                dex
                bne     @x64
                stz     T_N + 2
                stz     T_N + 3
                jsr     DEC32
                SAY     " KB OF CARD MEMORY"
                lda     VIDEO_MEMSIZE
                beq     @none
                jmp     TPASS
@none:          TD      "MEMSIZE IS 0"
                rts

; V2: every register that reads back as written (video-rev2.md's map) with
; $55 and $AA, each under its mask, then put back: in one vertical blank.
V2:
                TB      "V2   EVERY REGISTER THAT READS BACK"
                jsr     VBSTART
                ldy     #VREGS - 1
@save:          ldx     VREGOFS,y
                lda     VIDEO,x
                sta     V2SAVE,y
                dey
                bpl     @save
                ldy     #VREGS - 1
@reg:           ldx     VREGOFS,y
                lda     #$55
                and     VREGMSK,y
                sta     VIDEO,x
                cmp     VIDEO,x
                bne     @bad
                lda     #$AA
                and     VREGMSK,y
                sta     VIDEO,x
                cmp     VIDEO,x
                bne     @bad
                lda     V2SAVE,y
                sta     VIDEO,x
                dey
                bpl     @reg
                jmp     TPASS
@bad:           sta     DG_MWANT
                lda     VIDEO,x
                sta     DG_MGOT
                stx     T_REV
@back:          ldx     VREGOFS,y      ; this one and those not yet done put back
                lda     V2SAVE,y
                sta     VIDEO,x
                dey
                bpl     @back
                jsr     TDET
                SAY     "REGISTER $C4"
                lda     T_REV
                clc
                adc     #<VIDEO
                jsr     HEX2
                jsr     RWBITS
                jmp     SCROFF

; Wait for a vertical blank's start (STATUS bit 7 rising): carry set after
; about 0.1 s with none.
VBSTART:
                ldx     #0
                ldy     #0
@out:           bit     VIDEO_STATUS
                bpl     @in
                dex
                bne     @out
                dey
                bne     @out
                sec
                rts
@in:            bit     VIDEO_STATUS
                bmi     @go
                dex
                bne     @in
                dey
                bne     @in
                sec
                rts
@go:            clc
                rts

; The beam's line, A low, X high, one moment's.
LINENOW:
                ldx     VIDEO_LINE + 1
                lda     VIDEO_LINE
                cpx     VIDEO_LINE + 1
                bne     LINENOW
                rts

; V3: the beam: through a vertical blank LINE reaches 524, then starts the
; next frame at 0; a frame (two changes of STATUS's bit, by HW_CLOCK) is
; 119,437 cycles within 0.1%: its own measurement, apart from NB6's.
V3:
                TB      "V3   THE BEAM: LINES 0-"
                jsr     VBSTART
                stz     T_N
                stz     T_N + 1
@vb:            jsr     LINENOW
                cpx     T_N + 1
                bcc     @less
                bne     @more
                cmp     T_N
                bcc     @less
@more:          sta     T_N
                stx     T_N + 1
@less:          bit     VIDEO_STATUS
                bmi     @vb
                jsr     LINENOW
                sta     T_D
                stx     T_D + 1
                stz     T_N + 2
                stz     T_N + 3
                lda     T_N
                pha
                lda     T_N + 1
                pha
                jsr     DEC32
                pla
                sta     T_N + 1
                pla
                sta     T_N
                SAY     ", A FRAME "
                lda     T_N
                cmp     #<524
                jne     @lines
                lda     T_N + 1
                cmp     #>524
                jne     @lines
                lda     T_D + 1        ; the next frame's first line: 0 (or 1, a poll late)
                jne     @start
                lda     T_D
                cmp     #2
                jcs     @start
                lda     #<VIDEO_STATUS ; one frame by HW_CLOCK: to its gap's grain (79 cycles)
                sta     HW_WATCH
                lda     #>VIDEO_STATUS
                sta     HW_WATCH + 1
                lda     #2
                sta     HW_EDGES
                jsr     HW_CLOCK
                bcs     @noframe
                ldx     #3
@cp:            lda     HW_CYC,x
                sta     T_N,x
                dex
                bpl     @cp
                jsr     DEC32
                SAY     " CYCLES"
                lda     #<FRAMEREF
                ldx     #>FRAMEREF
                jsr     CLOSE
                bcs     @far
                jmp     TPASS
@lines:         TD      "THE LAST LINE ISN'T 524"
                rts
@start:         TD      "THE FRAME DIDN'T START AT LINE 0"
                rts
@noframe:       TD      "NO FRAME"
                rts
@far:           TD      "NOT WITHIN 0.1% OF 119437 CYCLES"
                rts

; The screen's text into W0's TEXTSAVE, by the port, before V4 overwrites
; card memory; REDRAW puts it back with the font and the palette.
TEXTOUT:
                jsr     TEXTAT
                lda     #<TEXTSAVE
                sta     HW_PTR
                lda     #>TEXTSAVE
                sta     HW_PTR + 1
                ldx     #>TEXTLEN + 1
                ldy     #0
@b:             lda     VIDEO_PDATA
                sta     (HW_PTR),y
                iny
                bne     @b
                inc     HW_PTR + 1
                dex
                bne     @b
                rts
TEXTAT:         lda     #<DG_TEXT
                sta     VIDEO_PADDR
                lda     #>DG_TEXT
                sta     VIDEO_PADDR + 1
                lda     #^DG_TEXT
                sta     VIDEO_PADDR + 2
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                stz     VIDEO_PCTL
                rts

; The screen again: the font from flash block 30, the palette, the text kept
; by TEXTOUT; the aperture's slots and the port as /RESET leaves them.
REDRAW:
                jsr     SLOTS
                lda     #<DG_FONT
                sta     VIDEO_PADDR
                lda     #>DG_FONT
                sta     VIDEO_PADDR + 1
                lda     #^DG_FONT
                sta     VIDEO_PADDR + 2
                lda     #FLASH30
                sta     NB_WIN1
                stz     HW_PTR
                lda     #$40
                sta     HW_PTR + 1
                ldx     #16
                ldy     #0
@f:             lda     (HW_PTR),y
                sta     VIDEO_PDATA
                iny
                bne     @f
                inc     HW_PTR + 1
                dex
                bne     @f
                lda     #1
                sta     NB_WIN1
                stz     VIDEO_PADDR
                stz     VIDEO_PADDR + 1
                stz     VIDEO_PADDR + 2
                ldx     #0
@p:             lda     CGA,x
                sta     VIDEO_PDATA
                inx
                cpx     #32
                bne     @p
                jsr     TEXTAT
                lda     #<TEXTSAVE
                sta     HW_PTR
                lda     #>TEXTSAVE
                sta     HW_PTR + 1
                ldx     #>TEXTLEN + 1
                ldy     #0
@t:             lda     (HW_PTR),y
                sta     VIDEO_PDATA
                iny
                bne     @t
                inc     HW_PTR + 1
                dex
                bne     @t
                rts

; SLOT0-3 0-3, PSTEP 1, PCTL 0: as /RESET leaves them.
SLOTS:
                ldx     #6
@s:             txa
                lsr     a
                sta     VIDEO_SLOT0,x
                stz     VIDEO_SLOT0 + 1,x
                dex
                dex
                bpl     @s
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                stz     VIDEO_PCTL
                rts

; W1 on aperture slot A (0-3): WIN1H $0E and block A ($E00000 + A x $4000,
; whatever AP8 says). A and X.
APW1:           ldx     #VIDEO_APER_H
                stx     NB_WIN1H
                sta     NB_WIN1
                rts
; W1 back on RAM block 1 (WIN1H 0). A only.
W1HOME:         stz     NB_WIN1H
                lda     #1
                sta     NB_WIN1
                rts

; Card bank T_K (16 KB) in W1 through aperture slot 0.
BANKW1:
                lda     T_K
                sta     VIDEO_SLOT0
                lda     T_K + 1
                sta     VIDEO_SLOT0 + 1
                lda     #0
                jsr     APW1
; HW_PTR and HW_LEN: W1, all 16 KB.
RUN16K:         stz     HW_PTR
                lda     #$40
                sta     HW_PTR + 1
                stz     HW_LEN
                sta     HW_LEN + 1
                rts

; The aperture's fault at HW_BAD (a CPU address in W1) of card bank T_K:
; "CARD $aaaaaa: READ ...".
APERFAULT:
                lda     HW_GOT
                sta     DG_MGOT
                lda     HW_WANT
                sta     DG_MWANT
                lda     HW_BAD
                sta     T_N
                lda     T_K            ; bank x $4000 + the offset in W1
                asl     a
                asl     a
                asl     a
                asl     a
                asl     a
                asl     a
                sta     T_N + 1
                lda     HW_BAD + 1
                and     #$3F
                ora     T_N + 1
                sta     T_N + 1
                lda     T_K + 1
                asl     a
                asl     a
                asl     a
                asl     a
                asl     a
                asl     a
                sta     T_N + 2
                lda     T_K
                lsr     a
                lsr     a
                ora     T_N + 2
                sta     T_N + 2
                lda     #3
                sta     T_N + 3
                lda     #<SCARD
                ldx     #>SCARD
                jmp     TCARDF

; The banks card memory has: MEMSIZE x 4, into T_BANKS.
BANKS:
                lda     VIDEO_MEMSIZE
                sta     T_BANKS
                stz     T_BANKS + 1
                asl     T_BANKS
                rol     T_BANKS + 1
                asl     T_BANKS
                rol     T_BANKS + 1
                rts
; T_K on to the next bank: Z set at T_BANKS.
NEXTBANK:
                inc     T_K
                bne     @c
                inc     T_K + 1
@c:             lda     T_K
                cmp     T_BANKS
                bne     @r
                lda     T_K + 1
                cmp     T_BANKS + 1
@r:             rts

; V4: all of card memory, a 16 KB bank at a time in slot 0 (W1 on it, APW1):
; address in address, every bank written before any is checked (full: the
; march too, background $00); the first eight faults said.
V4:
                TB      "V4   CARD MEMORY: ADDRESS IN ADDRESS"
                lda     DG_OPT
                and     #DO_FULL
                beq     @q
                SAY     " AND THE MARCH"
@q:             jsr     BANKS
                stz     T_NBAD
                stz     T_K
                stz     T_K + 1
@w:             jsr     BANKW1
                lda     T_K
                sta     HW_KEY
                jsr     HW_AIAW
                jsr     NEXTBANK
                bne     @w
                stz     T_K
                stz     T_K + 1
@c:             jsr     BANKW1
                lda     T_K
                sta     HW_KEY
                jsr     HW_AIAC
                bcc     @march
                jsr     @fault
                bra     @nextc
@march:         lda     DG_OPT
                and     #DO_FULL
                beq     @nextc
                jsr     RUN16K
                stz     HW_BG
                jsr     HW_MARCH
                bcc     @nextc
                jsr     @fault
@nextc:         jsr     NEXTBANK
                bne     @c
                jsr     SLOTS
                jsr     W1HOME
                jmp     TEND0
@fault:         lda     T_NBAD
                cmp     #8
                bcs     @r
                inc     T_NBAD
                jmp     APERFAULT
@r:             rts

; V5: a card address past MEMSIZE (the first bank past it) reads $FF and drops a write.
V5:
                TB      "V5   PAST MEMSIZE: $FF, AND A WRITE DROPPED"
                jsr     BANKS
                lda     T_BANKS
                sta     T_K
                lda     T_BANKS + 1
                sta     T_K + 1
                jsr     BANKW1
                lda     $4123
                sta     T_D
                stz     $4123
                lda     $4123
                sta     T_D + 1
                jsr     SLOTS
                jsr     W1HOME
                lda     T_D
                and     T_D + 1
                cmp     #$FF
                bne     @bad
                jmp     TPASS
@bad:           TD      "IT ANSWERED"
                rts

; V6: the port against the aperture, card $000000-$00FFFF through slots 0-3
; (W1 on each, APW1): address in address written through PDATA (PSTEP 1),
; read through the aperture; read through the port stepping down
; (VIDEO_PCTL_DOWN) from $00FFFF; and written through the aperture, read
; through the port. Byte n through the port is n's low byte XOR its middle
; XOR its high XOR the key; through slot k the CPU's pattern is its
; address's low byte XOR its page XOR a key, the same bytes with the key the
; port's XOR $40 XOR k x $40.
V6:
                TB      "V6   THE PORT AGAINST THE APERTURE, 64 KB, UP AND DOWN"
                jsr     SLOTS
                lda     #<VIDEO_PDATA
                sta     HW_PORT
                lda     #>VIDEO_PDATA
                sta     HW_PORT + 1
                stz     HW_PLEN
                lda     #1
                sta     HW_PLEN + 1
                jsr     PORT0
                lda     #$5A
                sta     HW_KEY
                jsr     HW_PAIAW
                lda     #$5A           ; read through the aperture
                jsr     APERC
                bcs     @bad
                lda     #$FF           ; read through the port, down from $00FFFF
                sta     VIDEO_PADDR
                sta     VIDEO_PADDR + 1
                stz     VIDEO_PADDR + 2
                lda     #VIDEO_PCTL_DOWN
                sta     VIDEO_PCTL
                lda     #$5A
                sta     HW_KEY
                jsr     HW_PAIAC
                stz     VIDEO_PCTL
                bcs     @baddn
                stz     T_K            ; written through the aperture
@aw:            lda     T_K
                jsr     APW1
                jsr     RUN16K
                jsr     AKEY
                eor     #$A5
                sta     HW_KEY
                jsr     HW_AIAW
                inc     T_K
                lda     T_K
                cmp     #4
                bne     @aw
                jsr     W1HOME
                jsr     PORT0          ; read through the port
                lda     #$A5
                sta     HW_KEY
                jsr     HW_PAIAC
                bcs     @badup
                jmp     TPASS
@bad:           jsr     W1HOME
                stz     T_K + 1
                jmp     APERFAULT
@baddn:         lda     HW_BAD         ; byte n down from $FFFF is at $FFFF - n
                eor     #$FF
                sta     T_N
                lda     HW_BAD + 1
                eor     #$FF
                sta     T_N + 1
                bra     @port
@badup:         lda     HW_BAD
                sta     T_N
                lda     HW_BAD + 1
                sta     T_N + 1
@port:          stz     T_N + 2
                lda     #3
                sta     T_N + 3
                lda     HW_GOT
                sta     DG_MGOT
                lda     HW_WANT
                sta     DG_MWANT
                lda     #<SPORT
                ldx     #>SPORT
                jmp     TCARDF

; The port at card $000000, stepping up by 1.
PORT0:          stz     VIDEO_PADDR
                stz     VIDEO_PADDR + 1
                stz     VIDEO_PADDR + 2
                lda     #1
                sta     VIDEO_PSTEP
                stz     VIDEO_PSTEP + 1
                stz     VIDEO_PCTL
                rts
; $40 XOR T_K x $40: the aperture's key against the port's (V6).
AKEY:           lda     T_K
                lsr     a
                ror     a
                ror     a
                and     #$C0
                eor     #$40
                rts
; The aperture's four slots checked against the port's key A: carry set and
; T_K the bank at a fault.
APERC:          sta     T_REV
                stz     T_K
@k:             lda     T_K
                jsr     APW1
                jsr     RUN16K
                jsr     AKEY
                eor     T_REV
                sta     HW_KEY
                jsr     HW_AIAC
                bcs     @r
                inc     T_K
                lda     T_K
                cmp     #4
                bne     @k
                jsr     W1HOME
                clc
@r:             rts

; V7: channel 1, 4 KB of a pattern from W0's SRC4K into card memory at
; $010000 through the port (PCTL's DRQ), and back (PCTL's FROM) into W0's
; DST4K; the aperture and the copy back agree with it.
V7:
                TB      "V7   DMA CHANNEL 1: 4 KB TO THE CARD AND BACK"
                jsr     HAVEDMA
                bcc     @dma
                TS      "NO DMA CONTROLLER"
                rts
@dma:           jsr     PAT4K
                jsr     PORT10K
                lda     #VIDEO_PCTL_DRQ
                sta     VIDEO_PCTL
                lda     #>SRC4K
                ldx     #DMA_DEMAND_OUT
                jsr     DMA1GO
                stz     VIDEO_PCTL
                bcs     @stuck
                lda     #4             ; card $010000 is bank 4: slot 0
                sta     VIDEO_SLOT0
                stz     VIDEO_SLOT0 + 1
                lda     #0
                jsr     APW1
                lda     #$40
                jsr     CMP4K
                php
                jsr     W1HOME
                jsr     SLOTS
                plp
                bcs     @badap
                jsr     PORT10K
                lda     #VIDEO_PCTL_DRQ | VIDEO_PCTL_FROM
                sta     VIDEO_PCTL
                lda     #>DST4K
                ldx     #DMA_DEMAND_IN
                jsr     DMA1GO
                stz     VIDEO_PCTL
                bcs     @stuck
                lda     #>DST4K
                jsr     CMP4K
                bcs     @badbk
                jmp     TPASS
@stuck:         TD      "CHANNEL 1 NEVER REACHED ITS TERMINAL COUNT"
                rts
@badap:         TD      "THE APERTURE DOESN'T SHOW WHAT CHANNEL 1 WROTE"
                rts
@badbk:         TD      "CHANNEL 1 READ BACK SOMETHING ELSE"
                rts

; W0's SRC4K: 4 KB of address in address, key $3C.
PAT4K:          lda     #<SRC4K
                sta     HW_PTR
                lda     #>SRC4K
                sta     HW_PTR + 1
                stz     HW_LEN
                lda     #$10
                sta     HW_LEN + 1
                lda     #$3C
                sta     HW_KEY
                jmp     HW_AIAW
; The port at card $010000, stepping up.
PORT10K:        jsr     PORT0
                lda     #1
                sta     VIDEO_PADDR + 2
                rts
; 4 KB at CPU page A against W0's SRC4K: carry set if they differ.
CMP4K:          sta     HW_PTR + 1
                stz     HW_PTR
                lda     #<SRC4K
                sta     DG_PTR
                lda     #>SRC4K
                sta     DG_PTR + 1
                ldx     #16
                ldy     #0
@b:             lda     (HW_PTR),y
                cmp     (DG_PTR),y
                bne     @no
                iny
                bne     @b
                inc     HW_PTR + 1
                inc     DG_PTR + 1
                dex
                bne     @b
                clc
                rts
@no:            sec
                rts

; Channel 1 between W0's page A and the card, X its MODE: 4 KB, waited for
; (carry set if it never ended). CONTROL is kept, the master on with SELECT 1
; written before any channel register, and CONTROL put back after.
DMA1GO:
                tay                    ; the page
                lda     DMA_CONTROL
                pha
                lda     #DMA_ON | DMA_SEL1
                sta     DMA_CONTROL
                stx     DMA_MODE
                tya
                jsr     SRCW0
                ldx     #2
@a:             lda     T_SRC,x
                sta     DMA_ADDR,x
                dex
                bpl     @a
                stz     DMA_COUNT
                lda     #$10
                sta     DMA_COUNT + 1
                lda     #DMA_ENABLE
                sta     DMA_CTRL
                jsr     DMAWAIT
                pla                    ; (the carry is DMAWAIT's)
                sta     DMA_CONTROL
                rts

; --- Stage 10: the sound card ---------------------------------------------------

; The sound card, revision 3, the device (hw/design.md, "Sound card revision 3"): driven
; only by its records (diagsnd.inc). S1 the ID and READY; S2 IDENTIFY; S3
; SELFTEST; S4 channel 2 into DATA, then CHECKSUM against the host's CRC-32;
; S5 the event path and its interrupt line; S6 a raw PCM stream's frames at
; its rate. The card is reset first (CTRL's RESET) and its store emptied after
; (FREEALL), so the boot finds it as a power-on leaves it but for its time.
STAGE10:
                lda     #10
                jsr     SBEGIN
                lda     #3
                ldx     #$A
                jsr     CARDIS
                bcc     @known
                rts
@known:         lda     #SOUND_CTRL_RESET
                sta     SOUND_CTRL
                jsr     S1
                jsr     S2
                jsr     S3
                jsr     S4
                jsr     S5
                jsr     S6
                lda     T_RUNS
                beq     @end
                lda     #<RFREE
                ldx     #>RFREE
                jsr     SNDDO
@end:           lda     #SOUND_CTRL_RESET
                sta     SOUND_CTRL
                lda     #DR_PASS
                jmp     CEND

; S1: the ID, then READY within 250 ms (T_RUNS 1 if it came). S2-S6 need it.
S1:
                TB      "S1   ID $A3; THE FIRMWARE READY"
                stz     T_RUNS
                lda     #SOUND_ST_READY
                ldy     #SND_250MS
                jsr     SNDWT
                bcs     @no
                inc     T_RUNS
                jmp     TPASS
@no:            TD      "NOT READY WITHIN 250 MS"
                rts

; Carry set, and the test skipped, if the firmware isn't up (S1).
NEEDUP:
                lda     T_RUNS
                bne     @ok
                TS      "THE FIRMWARE ISN'T READY (S1)"
                sec
                rts
@ok:            clc
                rts

; A failed record: carry set (no reply within 100 ms), else A the status.
SNDBAD:
                bcc     @st
                TD      "NO REPLY WITHIN 100 MS"
                rts
@st:            pha
                jsr     TDET
                SAY     "REFUSED: ERROR "
                pla
                jsr     PUTDEC
                jsr     CRLF
                jmp     SCROFF

; S2: IDENTIFY: the card's type, MEMSIZE and FEATURES as its registers say,
; and the firmware's version.
S2:
                TB      "S2   IDENTIFY: FIRMWARE "
                jsr     NEEDUP
                jcs     @r
                lda     #<RIDENT
                ldx     #>RIDENT
                jsr     SNDDO
                bcs     @bad
                bne     @bad
                lda     RBUF + 3       ; the version
                jsr     PUTDEC
                lda     #'.'
                jsr     PUTC
                lda     RBUF + 4
                jsr     PUTDEC
                SAY     ", MEMSIZE "
                lda     RBUF + 7
                jsr     PUTDEC
                SAY     ", FEATURES $"
                lda     RBUF + 8
                jsr     HEX2
                lda     RBUF + 2
                cmp     #SOUND_ID_V3
                bne     @type
                lda     RBUF + 7
                cmp     SOUND_MEMSIZE
                bne     @regs
                lda     RBUF + 8
                cmp     SOUND_FEATURES
                bne     @regs
                lda     RBUF + 3
                cmp     SOUND_FWVER
                bne     @regs
                lda     RBUF + 4
                cmp     SOUND_FWVER + 1
                bne     @regs
                jmp     TPASS
@bad:           jmp     SNDBAD
@type:          TD      "ITS CARD TYPE ISN'T $A3"
@r:             rts
@regs:          TD      "IT DISAGREES WITH MEMSIZE, FEATURES OR FWVER"
                rts

; S3: SELFTEST of the store, wave RAM, the SIDs and the codec's loop: each
; part that fails named.
S3:
                TB      "S3   SELFTEST: THE STORE, WAVE RAM, SIDS, CODEC"
                jsr     NEEDUP
                bcs     @r
                lda     #<RSELF
                ldx     #>RSELF
                jsr     SNDDO
                bcs     @bad
                bne     @bad
                lda     RBUF + 3       ; those that failed
                bne     @fail
                jmp     TPASS
@bad:           jmp     SNDBAD
@fail:          sta     T_PAT
                jsr     TDET
                SAY     "FAILED:"
                ldx     #0
@part:          lsr     T_PAT
                bcc     @next
                phx
                jsr     SPACE
                plx
                phx
                lda     PARTS,x
                pha
                lda     PARTS + 1,x
                tax
                pla
                jsr     PUTS
                plx
@next:          inx
                inx
                cpx     #8
                bne     @part
                jsr     CRLF
                jmp     SCROFF
@r:             rts

; S4: channel 2, 4 KB of a pattern from W0's SRC4K into DATA (a sample's load,
; CTRL's DRQ on), then CHECKSUM: the card's CRC-32 of what came against the
; host's of what was sent. A load that never ends (a byte lost on the way)
; is closed by XFERABORT, and the bytes that came said.
S4:
                TB      "S4   DMA CHANNEL 2: 4 KB, THE CARD'S CRC-32"
                jsr     NEEDUP
                bcs     @r
                jsr     HAVEDMA
                bcc     @dma
                TS      "NO DMA CONTROLLER"
@r:             rts
@dma:           jsr     PAT4K
                lda     #<RLOAD
                ldx     #>RLOAD
                jsr     SNDDO
                bcs     @bad
                bne     @bad
                lda     #SOUND_CTRL_DRQ
                sta     SOUND_CTRL
                lda     DMA_CONTROL    ; CONTROL kept; the master on, SELECT 2 before any
                pha                    ;   channel register; put back after
                lda     #DMA_ON | DMA_SEL2
                sta     DMA_CONTROL
                lda     #DMA_DEMAND_OUT
                sta     DMA_MODE
                lda     #>SRC4K
                jsr     SRCW0
                ldx     #2
@a:             lda     T_SRC,x
                sta     DMA_ADDR,x
                dex
                bpl     @a
                stz     DMA_COUNT
                lda     #$10
                sta     DMA_COUNT + 1
                lda     #DMA_ENABLE
                sta     DMA_CTRL
                jsr     DMAWAIT
                pla                    ; (the carry is DMAWAIT's)
                sta     DMA_CONTROL
                stz     SOUND_CTRL
                bcs     @stuck
                lda     #SOUND_EV_XFERDONE
                jsr     SNDEVT         ; the load taken and checked
                bcs     @short
                jsr     CRC4K
                lda     #<RCSUM
                ldx     #>RCSUM
                jsr     SNDDO
                bcs     @bad
                bne     @bad
                ldx     #3
@cmp:           lda     RBUF + 2,x
                cmp     T_N,x
                jne     @diff
                dex
                bpl     @cmp
                jsr     @bank
                jmp     TPASS
@bad:           jsr     SNDBAD
                jmp     @bank
@stuck:         TD      "CHANNEL 2 NEVER REACHED ITS TERMINAL COUNT"
                jsr     @abort
                jmp     @bank
@short:         jsr     @abort         ; never ended: closed, and what came said
                bcs     @bad
                bne     @bad
                ldx     #2
@n:             lda     RBUF + 2,x
                sta     T_N,x
                dex
                bpl     @n
                stz     T_N + 3
                jsr     TDET
                SAY     "THE LOAD TOOK "
                jsr     DEC32
                SAY     " OF 4096 BYTES"
                jsr     CRLF
                jsr     SCROFF
                bra     @bank
@diff:          jsr     TDET
                SAY     "THE CARD'S CRC-32 $"
                ldx     #3
@c1:            lda     RBUF + 2,x
                jsr     HEX2
                dex
                bpl     @c1
                SAY     ", THE HOST'S $"
                ldx     #3
@c2:            lda     T_N,x
                jsr     HEX2
                dex
                bpl     @c2
                jsr     CRLF
                jsr     SCROFF
@bank:          lda     #<RBFREE
                ldx     #>RBFREE
                jmp     SNDDO
@abort:         lda     #<RABORT
                ldx     #>RABORT
                jmp     SNDDO

; T_N: the CRC-32 of W0's SRC4K, 4 KB (the common one: $EDB88320 reflected,
; from $FFFFFFFF, the result inverted), a bit at a time.
CRC4K:
                lda     #$FF
                ldx     #3
@init:          sta     T_N,x
                dex
                bpl     @init
                lda     #<SRC4K
                sta     DG_PTR
                lda     #>SRC4K
                sta     DG_PTR + 1
                ldx     #16
                ldy     #0
@byte:          lda     (DG_PTR),y
                eor     T_N
                sta     T_N
                lda     #8
                sta     T_PAT
@bit:           lsr     T_N + 3
                ror     T_N + 2
                ror     T_N + 1
                ror     T_N
                bcc     @nx
                lda     T_N + 3
                eor     #$ED
                sta     T_N + 3
                lda     T_N + 2
                eor     #$B8
                sta     T_N + 2
                lda     T_N + 1
                eor     #$83
                sta     T_N + 1
                lda     T_N
                eor     #$20
                sta     T_N
@nx:            dec     T_PAT
                bne     @bit
                iny
                bne     @byte
                inc     DG_PTR + 1
                dex
                bne     @byte
                ldx     #3
@inv:           lda     T_N,x
                eor     #$FF
                sta     T_N,x
                dex
                bpl     @inv
                rts

; S5: the event path: with IEN's EVT on, a NOP's REPLY pulls the card's line
; (IRQH bit 2 low); once it is read the line lets go. IEN 0 after.
S5:
                TB      "S5   AN EVENT'S INTERRUPT: IRQH BIT 2"
                jsr     NEEDUP
                bcs     @r
                lda     #SOUND_INT_EVT
                sta     SOUND_IEN
                lda     #<RNOP
                ldx     #>RNOP
                jsr     SNDCMD
                bcs     @none
                lda     #SOUND_ST_EVT
                ldy     #SND_100MS
                jsr     SNDWT
                bcs     @none
                lda     NB_IRQH
                and     #NB_IRQ_SOUND
                bne     @high
                jsr     SNDREP
                bcs     @none
                lda     NB_IRQH
                and     #NB_IRQ_SOUND
                beq     @low
                stz     SOUND_IEN
                jmp     TPASS
@none:          stz     SOUND_IEN
                TD      "NO REPLY WITHIN 100 MS"
@r:             rts
@high:          stz     SOUND_IEN
                jsr     SNDREP         ; (the reply read)
                TD      "IRQH BIT 2 STAYED HIGH WITH AN EVENT WAITING"
                rts
@low:           stz     SOUND_IEN
                TD      "IRQH BIT 2 STAYED LOW WITH THE EVENTS READ"
                rts

; S6: a raw PCM stream, 4 KB of silence at 8,000 Hz, 8 bits mono: playing
; once its 100 ms are buffered, its frames played counting 8,000 a second
; within 0.5% over 1,835,008 cycles of timer 2 (2,051 frames), then ended
; once STREAM_END's data has played out.
S6:
                TB      "S6   A PCM STREAM AT 8000 HZ: "
                jsr     NEEDUP
                jcs     @r
                lda     #<ROPEN
                ldx     #>ROPEN
                jsr     SNDDO
                jcs     @bad
                jne     @bad
                lda     #<RDATA
                ldx     #>RDATA
                jsr     SNDDO
                jcs     @bad
                jne     @bad
                ldx     #16            ; 4 KB of silence, by the CPU
                ldy     #0
                lda     #$80
@fill:          sta     SOUND_DATA
                iny
                bne     @fill
                dex
                bne     @fill
                lda     #<REND
                ldx     #>REND
                jsr     SNDDO
                jcs     @bad
                jne     @bad
                lda     #S_PLAYING
                jsr     SWAITS
                jcs     @noplay
                lda     RBUF + 15      ; frames played (its low 2 bytes)
                sta     T_SUM
                lda     RBUF + 16
                sta     T_SUM + 1
                jsr     TZERO
@wait:          jsr     TTICK
                lda     T_ACC + 2
                cmp     #$1C
                bcc     @wait
                jsr     SSTAT
                bcs     @bad
                bne     @bad
                sec
                lda     RBUF + 15
                sbc     T_SUM
                sta     T_N
                lda     RBUF + 16
                sbc     T_SUM + 1
                sta     T_N + 1
                stz     T_N + 2
                stz     T_N + 3
                lda     T_N
                pha
                lda     T_N + 1
                pha
                jsr     DEC32
                pla
                sta     T_N + 1
                pla
                sta     T_N
                SAY     " FRAMES IN 1835008 CYCLES"
                lda     #S_ENDED
                jsr     SWAITS
                bcs     @noend
                jsr     @close
                stz     T_N + 2
                stz     T_N + 3
                lda     #<PCMREF
                ldx     #>PCMREF
                jsr     CLOSE
                bcs     @far
                jmp     TPASS
@bad:           jsr     SNDBAD
                bra     @close
@far:           TD      "NOT WITHIN 0.5% OF 8000 FRAMES A SECOND"
@r:             rts
@noplay:        TD      "NOT PLAYING WITHIN 0.6 S OF ITS DATA"
                bra     @close
@noend:         TD      "IT NEVER ENDED"
@close:         lda     #<RCLOSE
                ldx     #>RCLOSE
                jmp     SNDDO

S_PLAYING       = 2                    ; STREAM_STATUS's states
S_ENDED         = 5

; Stream 0's STATUS into RBUF: carry set (no reply), else A the status.
SSTAT:
                lda     #<RSTAT
                ldx     #>RSTAT
                jmp     SNDDO

; Until stream 0's state is A: asked every 9 ms or so, 64 times at most
; (carry set if it never was).
SWAITS:
                sta     T_PAT
                lda     #64
                sta     T_CNT
@ask:           jsr     SSTAT
                bcs     @no
                lda     RBUF + 2
                cmp     T_PAT
                beq     @yes
                lda     #0             ; a unit's wait (no STATUS bit asked)
                ldy     #1
                jsr     SNDWT
                dec     T_CNT
                bne     @ask
@no:            sec
                rts
@yes:           clc
                rts

        .include "diagcon.inc"
        .include "diagtest.inc"
        .include "diagsnd.inc"

; --- Tables and text -------------------------------------------------------------

; D1's registers: a channel's ADDR, COUNT, MODE, DEST and CARD, then its
; reserved bytes; the bits each keeps, and what each reads besides (a reserved
; byte $FF)
DREGOFS:        .byte   $00, $01, $02, $04, $05, $06, $08, $09, $0A, $0C
                .byte   $03, $0B, $0D, $0E, $0F
DREGMSK:        .byte   $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF, $1F
                .byte   $FF, $FF, $FF, $FF, $FF
DREGFIX:        .byte   $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
                .byte   $FF, $FF, $FF, $FF, $FF
DREGS           = * - DREGFIX
.assert DREGS = DREGFIX - DREGMSK && DREGS = DREGMSK - DREGOFS, error, "D1's tables"

; V2's registers (offsets from $C480) and the bits each keeps
VREGOFS:        .byte   $00, $01, $04, $0A, $0B, $0C, $0D
                .byte   $10, $11, $12, $13, $14, $15, $16, $17, $18, $19
                .byte   $20, $21, $22, $23, $24, $25, $26, $27, $28, $29
                .byte   $30, $31, $32, $33, $34, $35, $36, $37
                .byte   $38, $39, $3A, $3C, $3D, $3E
VREGMSK:        .byte   $0F, $FF, $07, $FF, $FF, $FF, $FF
                .byte   $FF, $FF, $FF, $FF, $FF, $FF, $03, $FF, $03, $0F
                .byte   $FF, $FF, $FF, $FF, $FF, $FF, $03, $FF, $03, $0F
                .byte   $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF
                .byte   $FF, $FF, $FF, $FF, $FF, $07
VREGS           = * - VREGMSK
.assert VREGS = VREGMSK - VREGOFS, error, "V2's tables"

; CLOSE's tables: the value (4 bytes), the tolerance (2)
HZREF:          .dword  HZ
                .word   HZ / 1000      ; 0.1%
FRAMEREF:       .dword  FRAME
                .word   FRAME / 1000
FRAMESREF:      .dword  60 * FRAME
                .word   60 * FRAME / 1000
DMAREF:         .dword  4096
                .word   16
PCMREF:         .dword  (1835008 * 8000 + HZ / 2) / HZ
                .word   10             ; 0.5% of 2,051

; The CGA's 16 colours, as video.s has them (GGGGBBBB, then 0000RRRR)
.macro RGB r, g, b
                .byte   (g << 4) | b, r
.endmacro
CGA:            RGB     $0, $0, $0
                RGB     $0, $0, $A
                RGB     $0, $A, $0
                RGB     $0, $A, $A
                RGB     $A, $0, $0
                RGB     $A, $0, $A
                RGB     $A, $5, $0
                RGB     $A, $A, $A
                RGB     $5, $5, $5
                RGB     $5, $5, $F
                RGB     $5, $F, $5
                RGB     $5, $F, $F
                RGB     $F, $5, $5
                RGB     $F, $5, $F
                RGB     $F, $F, $5
                RGB     $F, $F, $F

SNOCARD:        .byte   "NO CARD", 0
SLOST:          .byte   "LOST: THE CLOCK CHIP'S OSCILLATOR STOPPED; SET THE TIME", 0
SCOPYUP:        .byte   "THE COPY UP DOESN'T MATCH ITS SOURCE", 0
SCOPYDN:        .byte   "THE COPY DOWN (MODE'S DEC) DOESN'T MATCH ITS SOURCE", 0
SCARD:          .byte   "CARD $", 0
SPORT:          .byte   "THE PORT AT CARD $", 0
PARTS:          .word   SPSTORE, SPWAVE, SPSIDS, SPCODEC ; SELFTEST's bits 0-3
SPSTORE:        .byte   "THE STORE", 0
SPWAVE:         .byte   "WAVE RAM", 0
SPSIDS:         .byte   "THE SIDS", 0
SPCODEC:        .byte   "THE CODEC'S LOOP", 0

; Stage 10's records (diagsnd.inc): the command, n, the tag, n bytes
RIDENT:         .byte   SOUND_IDENTIFY, 0, SND_TAG
RSELF:          .byte   SOUND_SELFTEST, 1, SND_TAG, $0F         ; every part
RLOAD:          .byte   SOUND_SAMPLE_LOAD, 13, SND_TAG, 0       ; sample slot 0:
                .byte   $00, $10, $00, 0                        ;   4 KB, unsigned,
                .word   8000                                    ;   8,000 Hz,
                .byte   0, 0, 0, 0, 0, 0                        ;   no loop
RCSUM:          .byte   SOUND_CHECKSUM, 2, SND_TAG, SOUND_SAMPLE_LOAD, 0
RABORT:         .byte   SOUND_XFERABORT, 0, SND_TAG
RBFREE:         .byte   SOUND_BANK_FREE, 1, SND_TAG, 1          ; every sample
RNOP:           .byte   SOUND_NOP, 0, SND_TAG
ROPEN:          .byte   SOUND_STREAM_OPEN, 10, SND_TAG, 0, 0    ; stream 0, raw PCM,
                .word   8000                                    ;   8,000 Hz,
                .byte   0, $00, $10, $00                        ;   8 bits mono, 4 KB chunks,
                .word   100                                     ;   100 ms buffered to start
RDATA:          .byte   SOUND_STREAM_DATA, 4, SND_TAG, 0, $00, $10, $00
REND:           .byte   SOUND_STREAM_END, 1, SND_TAG, 0
RSTAT:          .byte   SOUND_STREAM_STATUS, 1, SND_TAG, 0
RCLOSE:         .byte   SOUND_STREAM_CLOSE, 1, SND_TAG, 0
RFREE:          .byte   SOUND_FREEALL, 0, SND_TAG

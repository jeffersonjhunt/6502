; OODOS's Disk II driver (hw/oodos.md, "The plan, phase 6", 6c): the card
; in native mode (disk2.inc), drives 1 and 2, O: and P:. Block 61, with
; the BIOS (bios.s), whose apple-do layer calls it: a whole track read into
; a 4 KB buffer of block 29, and one 256-byte sector written from it.
;
; The disk is DOS 3.3's 16-sector format, as a .dsk is written out (the
; emulator's DiskImage::from_sectors): each sector an address field (D5 AA
; 96, the volume, track, sector and their checksum in 4-and-4, DE AA) and a
; data field (D5 AA AD, 342 bytes of 6-and-2 and their checksum, DE AA). The
; file's sector f (the .dsk's DOS order) is the disk's physical sector
; FILE2PHYS[f], DOS 3.3's interleave.
;
; Every wait is bounded (6a's review): a byte waited for 256 polls of 11
; cycles, about 2,800 cycles, a dozen bytes' time; an address field for two
; turns' bytes, every byte read counted, D5s too; a data field's prologue
; within 32 bytes; a track for three tries. A card with no sequencer PROM
; (the WebUI's) never sets bit 7, so a read is an error in about 0.4 s,
; not a hang.
;
; The card has one motor line, to the drive selected (6c's review): the
; other drive's disk has stopped, whatever the data register does, so
; D2START and D2MEDIA say a drive not the last selected (D2LAST) wasn't
; turning.
;
; Writing: the byte to write is loaded (Q6 on) and held a whole loop of
; the sequencer's eight steps, 28 cycles, before Q6 off shifts it out (6a);
; a load every 224 cycles (a sync byte's ten bits, 280), counted to the
; cycle with the loop in one page; interrupts and the DMA controller's
; master enable off while it writes.
;
; Interrupts (hw/oolib.md, question 14, L1): the BIOS runs with them on, but
; a field can't wait for a handler, so the driver turns them off from an
; address field's first byte (its D5) to the end of the data field read or
; written after it (D2MASK), and back on, if its caller had them on, when it
; looks for another (D2UNM): about 12 ms a sector, less than a frame. The
; millisecond counter misses those ticks, so D2UNM adds the time to it:
; RBYTE counts the disk bytes read meanwhile, 224 cycles each (a sync
; byte's 280 counted as one: a few hundred cycles short a field), and a
; write adds its loop's. Timer 1's flag, set meanwhile, is cleared, its
; tick being in that count. A vertical blank that comes then is taken when
; they are back on, so the frame counter loses none.
;
; The motor: switched on for each READ or WRITE and off at its end; the
; card keeps it turning a second more (hw/oodos.md's question 2). Whether
; the disk is still turning is seen from the data register changing; one
; that has stopped may have been changed, so the BIOS empties its track
; buffer and the BDOS checks the directory (D2MEDIA).
;
; Times for the real drive (proposed; the emulator moves the head at once):
; a half track's step 3 ms, settling 15 ms, recalibration 80 half tracks
; out onto the stop.

.setcpu "65C02"
.include "sys.inc"
.include "disk2.inc"

; k loops of 5 cycles and 1 (LDX, DEX/BNE, the last BNE not taken), then r.
.macro DLYK k, r
        .assert k > 0 && k < 256, error, "DLY: a count the loop can't make"
                LDX     #k
:               DEX
                BNE     :-
        .if r = 2
                NOP
        .elseif r = 3
                BIT     TMP
        .elseif r = 4
                NOP
                NOP
        .elseif r = 6
                NOP
                NOP
                NOP
        .endif
.endmacro

; DLY n: exactly n cycles (n >= 2, not 1), X and the flags used. Its loop's
; branch mustn't cross a page: WLOOP is in one page.
.macro DLY n
        .if n = 2
                NOP
        .elseif n = 3
                BIT     TMP
        .elseif n = 4
                NOP
                NOP
        .elseif n = 5
                NOP
                BIT     TMP
        .else
                .if ((n - 1) .mod 5) = 1
                        DLYK ((n - 1) / 5 - 1), 6
                .else
                        DLYK ((n - 1) / 5), ((n - 1) .mod 5)
                .endif
        .endif
.endmacro
.importzp TMP, SPTR
.export D2INIT, D2START, D2STOP, D2TRACK, D2WSEC, D2MEDIA, D2CODE62, D2FS, D2GOTF
.export D2WRIT, D2DMAS                 ; common.s's NMI: a write cut short
.export D2BLK                          ; the BIOS's: the buffer's block, for W1
.export D2LAST, SPIN                   ; the BIOS's MEDIA: a stalled clock
.import CLOCKS, MS, MSACC, EVDIRTY     ; irq.s's: the catch-up

STEP_MS         = 3
SETTLE_MS       = 15
RECAL_STEPS     = 80                   ; half tracks: past track 34 to 0, onto the stop
NIB_LEN         = 3 + 343 + 3          ; D5 AA AD, the field, DE AA EB
NIB_B           = NIB_LEN - 256        ; its bytes in NIBBUF's second page
BUDGET          = 13000                ; disk bytes: two turns and more
D2_WBYTES       = (5 * 280 + NIB_LEN * 224 + 190) / 224 ; WLOOP's time, in disk bytes
TRIES           = 3

.segment "SYSBSS"
D2DRV:          .res    1              ; the drive, 0 or 1
D2HT:           .res    2              ; each drive's half track, $FF: not known (recalibrate)
D2TGT:          .res    1              ; the track wanted
D2BUFH:         .res    1              ; the track buffer's page in W1's view ($40-$7F)
D2BLK:          .res    1              ; and its block (29 or 30)
D2GOT:          .res    2              ; physical sectors read, bit n
D2GOTF:         .res    2              ; and as the file's sectors: D2TRACK's answer
D2TRY:          .res    1
D2CNT:          .res    2              ; ADDR's budget
D2TRK:          .res    1              ; the last address field's track, sector
D2SEC:          .res    1
D2X:            .res    1              ; the running XOR
D2W1:           .res    1              ; W1 as the caller had it
D2FS:           .res    1              ; D2WSEC's file sector
D2FIRST:        .res    1              ; SPIN's first sample
D2HTT:          .res    1              ; STEPTO's target half track
D2LAST:         .res    1              ; the drive the card last selected, $FF none: the other's motor is off
D2WRIT:         .res    1              ; 1 while WLOOP writes (NMI's: Q7 off, DMA back)
D2DMAS:         .res    1              ; DMA_CONTROL as it was, for WLOOP's end, or NMI's
D2CP:           .res    1              ; the caller's P: whether D2UNM turns interrupts back on
D2MSK:          .res    1              ; 1 while the driver has them off for a field
D2BYC:          .res    2              ; disk bytes read since (RBYTE's count)
D2CU:           .res    3              ; CATCHUP's sum

.segment "BIOS"

; ---------------------------------------------------------------------------
; Entries

; Cold boot: the heads not known, the decode table made.
D2INIT:         LDA     #$FF
                STA     D2HT
                STA     D2HT + 1
                STA     D2LAST
                LDX     #0
@ff:            STA     DEC62,X
                INX
                BNE     @ff
                LDX     #63
@code:          LDY     D2CODE62,X
                TXA
                STA     DEC62,Y
                DEX
                BPL     @code
                RTS

; Drive X (0 or 1) selected and its motor on, read mode. Carry set if its
; disk wasn't turning: it may have been changed.
D2START:        JSR     D2MEDIA
                LDA     DISK2 + D2_MOTORON
                RTS

D2STOP:         LDA     DISK2 + D2_MOTOROFF
                LDA     DISK2 + D2_Q7OFF
                LDA     DISK2 + D2_Q6OFF
                RTS

; Drive X selected (its motor as it was): carry set if its disk isn't
; turning, so may have been changed since it last turned.
D2MEDIA:        STX     D2DRV
                CPX     D2LAST
                PHP                    ; Z: the card had this drive
                STX     D2LAST
                JSR     SELECT
                JSR     SPIN
                BCS     @stopped
                PLP
                BNE     @other         ; the other's disk turned: this one's had stopped
                CLC
                RTS
@stopped:       PLP
                SEC
                RTS
@other:         SEC
                RTS

; Track A of drive D2DRV (D2START first) into the buffer at page Y in W1's
; view (W1 put on block 29 here), the file's 16 sectors in order. A = 0 and
; D2GOTF the file's sectors read (all 16, unless a field stayed bad for
; three tries: the others are good); A = 1 none read (no disk, no card
; answering, the head nowhere it should be).
D2TRACK:        STA     D2TGT
                STY     D2BUFH
                PHP
                PLA
                STA     D2CP
                LDA     D2TGT
                JSR     SEEK
                STZ     D2GOT
                STZ     D2GOT + 1
                LDA     #TRIES
                STA     D2TRY
                JSR     MAPW1
@pass:          JSR     BUDGETSET
@field:         JSR     ADDR
                BCS     @failed        ; no field in two turns, or no bytes at all
                LDA     D2TRK
                CMP     D2TGT
                BNE     @wrong
                LDX     D2SEC
                CPX     #16
                BCS     @field
                JSR     GOTBIT         ; A = its bit, Y = its byte
                AND     D2GOT,Y
                BNE     @field         ; had it
                LDX     D2SEC
                LDA     PHYS2FILE,X
                JSR     PLACE          ; SPTR the buffer's sector, TMP its 86
                JSR     RDATA
                BCS     @field         ; a bad field: on round
                LDX     D2SEC
                JSR     GOTBIT
                ORA     D2GOT,Y
                STA     D2GOT,Y
                LDA     D2GOT
                AND     D2GOT + 1
                CMP     #$FF
                BNE     @field
@got:           JSR     D2UNM          ; the last field's time
                JSR     COMBINE
                JSR     UNMAPW1
                LDA     #0
                RTS
@wrong:         JSR     D2UNM
                LDX     D2DRV          ; the head not where it was thought
                LDA     #$FF
                STA     D2HT,X
                LDA     D2TGT
                JSR     SEEK
@failed:        DEC     D2TRY
                BNE     @pass
                LDA     D2GOT          ; some sectors good: those
                ORA     D2GOT + 1
                BNE     @got
                JSR     UNMAPW1
                LDX     D2DRV          ; recalibrate at the next
                LDA     #$FF
                STA     D2HT,X
                LDA     #1
                RTS

; File sector D2FS of track A of drive D2DRV written from the buffer at
; page Y (W1's view). A = 0, 1 an error, 2 write-protected.
D2WSEC:         STA     D2TGT          ; interrupts off from the field's D5 to the end of
                PHP                    ; the write (ADDR's D2MASK), the caller's flag back
                PLA                    ; on every way out (D2UNM)
                STA     D2CP
                STY     D2BUFH
                LDA     D2TGT
                JSR     SEEK
                JSR     MAPW1
                LDA     D2FS
                JSR     PLACE
                JSR     PRENIB
                JSR     UNMAPW1
                LDA     #TRIES
                STA     D2TRY
@pass:          JSR     BUDGETSET
@field:         JSR     ADDR
                BCS     @failed
                LDA     D2TRK
                CMP     D2TGT
                BNE     @wrong
                LDX     D2FS
                LDA     FILE2PHYS,X
                CMP     D2SEC
                BNE     @field
                LDA     DISK2 + D2_Q6ON ; sense write-protect: bit 7
                DLY     30
                LDA     DISK2 + D2_Q7OFF
                BMI     @protected
                LDA     DMA_CONTROL
                STA     D2DMAS
                AND     #<~DMA_MASTER
                STA     DMA_CONTROL
                LDA     #1
                STA     D2WRIT
                JSR     WLOOP
                STZ     D2WRIT
                LDA     D2DMAS
                STA     DMA_CONTROL
                CLC                    ; the write's time, for the catch-up
                LDA     D2BYC
                ADC     #<D2_WBYTES
                STA     D2BYC
                LDA     D2BYC + 1
                ADC     #>D2_WBYTES
                STA     D2BYC + 1
                JSR     D2UNM
                LDA     #0
                RTS
@protected:     LDA     DISK2 + D2_Q6OFF
                JSR     D2UNM
                LDA     #2
                RTS
@wrong:         JSR     D2UNM
                LDX     D2DRV
                LDA     #$FF
                STA     D2HT,X
                LDA     D2TGT
                JSR     SEEK
@failed:        DEC     D2TRY
                BNE     @pass
                LDX     D2DRV
                LDA     #$FF
                STA     D2HT,X
                LDA     #1
                RTS

; ---------------------------------------------------------------------------
; The drive and the head

; D2DRV selected, read mode.
SELECT:         LDA     DISK2 + D2_Q7OFF
                LDA     DISK2 + D2_Q6OFF
                LDA     D2DRV
                BNE     @two
                LDA     DISK2 + D2_DRIVE1
                RTS
@two:           LDA     DISK2 + D2_DRIVE2
                RTS

; Carry clear if the data register changes over about 2,000 cycles (a
; byte's time and more): the disk turns.
SPIN:           LDA     DISK2 + D2_Q6OFF
                STA     D2FIRST
                LDY     #80
@sample:        LDA     DISK2 + D2_Q6OFF
                CMP     D2FIRST
                BNE     @turning
                LDX     #3
@wait:          DEX
                BNE     @wait
                DEY
                BNE     @sample
                SEC
                RTS
@turning:       CLC
                RTS

; The head of D2DRV to track A: recalibrated first if it isn't known.
SEEK:           ASL     A
                STA     D2HTT
                LDX     D2DRV
                LDA     D2HT,X
                CMP     #$FF
                BNE     @known
                LDA     #RECAL_STEPS   ; recalibrate: from beyond the last track to 0, onto the stop
                STA     D2HT,X
                LDA     D2HTT
                PHA
                STZ     D2HTT
                JSR     STEPTO
                PLA
                STA     D2HTT
@known:         ; fall through

; The head of D2DRV from D2HT,X a half track at a time to D2HTT: the next
; phase on, a step's time, the last off; then the settling time, and off.
STEPTO:         LDX     D2DRV
                LDA     D2HT,X
                CMP     D2HTT
                BEQ     @there
@step:          LDX     D2DRV
                LDA     D2HT,X
                PHA                    ; the half track it is at
                CMP     D2HTT
                BCC     @in
                DEC     A
                BRA     @to
@in:            INC     A
@to:            STA     D2HT,X
                JSR     PHASEON        ; the next's phase on
                LDA     #STEP_MS
                JSR     DELAYMS
                PLA
                JSR     PHASEOFF       ; the last's off
                LDX     D2DRV
                LDA     D2HT,X
                CMP     D2HTT
                BNE     @step
                LDA     #SETTLE_MS
                JSR     DELAYMS
                LDX     D2DRV
                LDA     D2HT,X
                JMP     PHASEOFF
@there:         RTS

; Half track A's phase on, or off. DISK2 + (A mod 4) x 2 + 1 or + 0: an
; absolute,X load inside the card's page, which the W65C02S makes once.
PHASEON:        AND     #3
                ASL     A
                TAX
                LDA     DISK2 + D2_PH0ON,X
                RTS
PHASEOFF:       AND     #3
                ASL     A
                TAX
                LDA     DISK2 + D2_PH0OFF,X
                RTS

; About A milliseconds (7,159 cycles each, near enough). Uses X and Y.
DELAYMS:        TAY
@ms:            LDX     #0
@inner:         NOP
                NOP
                DEX
                BNE     @inner         ; 256 x 9 = 2,304
                LDX     #0
@inner2:        NOP
                NOP
                DEX
                BNE     @inner2
                LDX     #0
@inner3:        NOP
                NOP
                DEX
                BNE     @inner3
                DEY
                BNE     @ms
                RTS

; W1 on D2BLK (the track buffer's block, 29 or 30), the caller's kept; and back.
MAPW1:          LDA     NB_WIN1
                STA     D2W1
                LDA     D2BLK
                STA     NB_WIN1
                RTS
UNMAPW1:        LDA     D2W1
                STA     NB_WIN1
                RTS

; ---------------------------------------------------------------------------
; Reading

BUDGETSET:      LDA     #<BUDGET
                STA     D2CNT
                LDA     #>BUDGET
                STA     D2CNT + 1
                RTS

; The next disk byte in A, carry clear; carry set if none came (no data:
; no disk, no sequencer, the motor stopped). Waits for the last byte to go
; (bit 7 clear), then for the next (bit 7 set): each wait 256 polls.
RBYTE:          LDX     #0
@clear:         LDA     DISK2 + D2_Q6OFF
                BPL     @next
                DEX
                BNE     @clear
                SEC
                RTS
@next:          LDX     #0
@set:           LDA     DISK2 + D2_Q6OFF
                BMI     @got
                DEX
                BNE     @set
                SEC
                RTS
@got:           INC     D2BYC          ; a byte's time, for D2UNM
                BNE     @c
                INC     D2BYC + 1
@c:             CLC
                RTS

; An address field, within D2CNT bytes (every byte read counts, D5s too): D2TRK
; and D2SEC, carry clear, its epilogue's DE AA read, so a write after starts
; past them, interrupts off (D2MASK) till the caller's D2UNM or ADDR again;
; carry set if none (or no bytes), interrupts as the caller had them.
ADDR:           JSR     D2UNM          ; on while it looks
@look:          JSR     NBYTE
                BCS     @none
                CMP     #$D5
                BNE     @look
@d5:            JSR     D2MASK         ; off from a prologue's first byte
                JSR     NBYTE
                BCS     @none
                CMP     #$AA
                BNE     @again
                JSR     NBYTE
                BCS     @none
                CMP     #$96
                BNE     @again
                JSR     R44            ; the volume
                BCS     @none
                STA     D2X
                JSR     R44
                BCS     @none
                STA     D2TRK
                EOR     D2X
                STA     D2X
                JSR     R44
                BCS     @none
                STA     D2SEC
                EOR     D2X
                STA     D2X
                JSR     R44            ; the checksum
                BCS     @none
                CMP     D2X
                BNE     ADDR
                JSR     RBYTE
                BCS     @none
                CMP     #$DE
                BNE     ADDR
                JSR     RBYTE
                BCS     @none
                CMP     #$AA
                BNE     ADDR
                CLC
                RTS
@again:         CMP     #$D5           ; not the field: another prologue, or looking again
                BEQ     @d5
                BRA     ADDR
@none:          JSR     D2UNM
                SEC
                RTS

; Interrupts off for a field, if they aren't already: the byte count from
; the D5 read.
D2MASK:         LDA     D2MSK
                BNE     @on
                SEI
                INC     D2MSK
                LDA     #1
                STA     D2BYC
                STZ     D2BYC + 1
@on:            RTS

; Interrupts back on after a field, if the caller had them on, and the
; millisecond counter caught up with the time they were off. Keeps nothing
; but D2BUFH's and the drive's state.
D2UNM:          LDA     D2MSK
                BEQ     @off
                STZ     D2MSK
                JSR     CATCHUP
                LDA     D2CP
                AND     #$04           ; I: the caller's
                BNE     @off
                CLI
@off:           RTS

; D2BYC disk bytes' time, 224 cycles each, into the millisecond counter, a
; tick for each TICK_CYCLES, MSACC keeping what is short of one; timer 1's
; flag cleared (its tick is in this), the event area's copy all written at
; the next tick. With interrupts off. No VIA, no counter: nothing.
CATCHUP:        LDA     CLOCKS
                AND     #CLK_MS
                BNE     @on
                RTS
@on:            LDA     D2BYC          ; D2CU = D2BYC x 32
                STA     D2CU
                LDA     D2BYC + 1
                STA     D2CU + 1
                STZ     D2CU + 2
                LDX     #5
@x32:           ASL     D2CU
                ROL     D2CU + 1
                ROL     D2CU + 2
                DEX
                BNE     @x32
                SEC                    ; D2BYC x 256 - that: x 224
                LDA     #0
                SBC     D2CU
                STA     D2CU
                LDA     D2BYC
                SBC     D2CU + 1
                STA     D2CU + 1
                LDA     D2BYC + 1
                SBC     D2CU + 2
                STA     D2CU + 2
                CLC                    ; and what was short of a tick before
                LDA     D2CU
                ADC     MSACC
                STA     D2CU
                LDA     D2CU + 1
                ADC     MSACC + 1
                STA     D2CU + 1
                BCC     @tick
                INC     D2CU + 2
@tick:          SEC                    ; a tick for each TICK_CYCLES
                LDA     D2CU
                SBC     #<TICK_CYCLES
                TAX
                LDA     D2CU + 1
                SBC     #>TICK_CYCLES
                TAY
                LDA     D2CU + 2
                SBC     #0
                BCC     @rest
                STA     D2CU + 2
                STY     D2CU + 1
                STX     D2CU
                INC     MS
                BNE     @tick
                INC     MS + 1
                BNE     @tick
                INC     MS + 2
                BNE     @tick
                INC     MS + 3
                BRA     @tick
@rest:          LDA     D2CU
                STA     MSACC
                LDA     D2CU + 1
                STA     MSACC + 1
                LDA     #VIA_IRQ_T1
                STA     VIA_IFR
                LDA     #1
                STA     EVDIRTY
                RTS

; RBYTE, D2CNT counted down first: carry set when it is spent.
NBYTE:          LDA     D2CNT
                BNE     @dec
                LDA     D2CNT + 1
                BEQ     @spent
                DEC     D2CNT + 1
@dec:           DEC     D2CNT
                JMP     RBYTE
@spent:         SEC
                RTS

; A byte in 4-and-4: two disk bytes, ((a << 1) | 1) & b.
R44:            JSR     RBYTE
                BCS     @out
                SEC
                ROL     A
                STA     D2FIRST
                JSR     RBYTE
                BCS     @out
                AND     D2FIRST
                CLC
@out:           RTS

; The data field after an address field: its 86 low values to TMP's 86
; bytes, its 256 high ones to the buffer's sector at SPTR (both before
; COMBINE), the checksum good. Carry set if not.
RDATA:          LDY     #32            ; D5 AA AD within 32 bytes, each counted
@find:          JSR     RBYTE
                BCS     @bad
@d5:            DEY
                BEQ     @bad
                CMP     #$D5
                BNE     @find
                JSR     RBYTE
                BCS     @bad
                DEY
                BEQ     @bad
                CMP     #$AA
                BNE     @d5
                JSR     RBYTE
                BCS     @bad
                CMP     #$AD
                BEQ     @field
                DEY
                BNE     @d5
@bad:           SEC
                RTS
@field:         STZ     D2X
                LDY     #0
@aux:           JSR     RBYTE
                BCS     @bad
                TAX
                LDA     DEC62,X
                BMI     @bad           ; not a disk byte of the code
                EOR     D2X
                STA     D2X
                STA     (TMP),Y
                INY
                CPY     #86
                BNE     @aux
                LDY     #0
@high:          JSR     RBYTE
                BCS     @bad
                TAX
                LDA     DEC62,X
                BMI     @bad
                EOR     D2X
                STA     D2X
                STA     (SPTR),Y
                INY
                BNE     @high
                JSR     RBYTE          ; the checksum
                BCS     @bad
                TAX
                LDA     DEC62,X
                CMP     D2X
                BNE     @bad
                JSR     RBYTE
                BCS     @bad
                CMP     #$DE
                BNE     @bad
                CLC
                RTS

; File sector A: SPTR its page of the buffer (W1's view), TMP its 86 in AUXA.
PLACE:          PHA
                CLC
                ADC     D2BUFH
                STA     SPTR + 1
                STZ     SPTR
                PLA
                TAX
                LDA     AUXLO,X
                STA     TMP
                LDA     AUXHI,X
                STA     TMP + 1
                RTS

; X = physical sector: A = its bit in D2GOT's byte Y.
GOTBIT:         TXA
                LSR     A
                LSR     A
                LSR     A
                TAY
                TXA
                AND     #7
                TAX
                LDA     BITS,X
                RTS

; Each of the 16 sectors: byte i = its high six << 2, and its low two from
; the 86, i mod 86's, bits 2 x (i / 86), swapped.
COMBINE:        STZ     D2GOTF
                STZ     D2GOTF + 1
                LDA     #15
@sector:        PHA
                TAX                    ; its physical sector read?
                LDA     FILE2PHYS,X
                TAX
                JSR     GOTBIT
                AND     D2GOT,Y
                BNE     @read
                JMP     @skip
@read:          PLA
                PHA
                TAX                    ; D2GOTF's bit for it
                JSR     GOTBIT
                ORA     D2GOTF,Y
                STA     D2GOTF,Y
                PLA
                PHA
                JSR     PLACE
                LDY     #0
@first:         LDA     (TMP),Y        ; i 0-85: bits 0-1
                AND     #3
                TAX
                LDA     (SPTR),Y
                ASL     A
                ASL     A
                ORA     SW2,X
                STA     (SPTR),Y
                INY
                CPY     #86
                BNE     @first
                LDY     #0
@second:        LDA     (TMP),Y        ; i 86-171: bits 2-3 of 86 back
                LSR     A
                LSR     A
                AND     #3
                TAX
                TYA
                CLC
                ADC     #86
                PHY
                TAY
                LDA     (SPTR),Y
                ASL     A
                ASL     A
                ORA     SW2,X
                STA     (SPTR),Y
                PLY
                INY
                CPY     #86
                BNE     @second
                LDY     #0
@third:         LDA     (TMP),Y        ; i 172-255: bits 4-5
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                AND     #3
                TAX
                TYA
                CLC
                ADC     #172
                PHY
                TAY
                LDA     (SPTR),Y
                ASL     A
                ASL     A
                ORA     SW2,X
                STA     (SPTR),Y
                PLY
                INY
                CPY     #84
                BNE     @third
@skip:          PLA
                DEC     A
                BMI     @done
                JMP     @sector
@done:          RTS

; ---------------------------------------------------------------------------
; Writing

; The sector at SPTR (W1's view) as the field to write, in NIBBUF: D5 AA AD,
; the 342 six-bit values each XORed with the one before as disk bytes, the
; last as the checksum, DE AA EB.
PRENIB:         LDY     #0             ; VBUF's 86 low values
@aux:           LDA     (SPTR),Y       ; byte i
                AND     #3
                TAX
                LDA     SW2,X
                STA     VBUF,Y
                TYA
                CLC
                ADC     #86
                PHY
                TAY
                LDA     (SPTR),Y       ; byte i + 86
                PLY
                AND     #3
                TAX
                LDA     SW2,X
                ASL     A
                ASL     A
                ORA     VBUF,Y
                STA     VBUF,Y
                CPY     #84            ; i + 172 < 256
                BCS     @two
                TYA
                CLC
                ADC     #172
                PHY
                TAY
                LDA     (SPTR),Y
                PLY
                AND     #3
                TAX
                LDA     SW2,X
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     VBUF,Y
                STA     VBUF,Y
@two:           INY
                CPY     #86
                BNE     @aux
                LDY     #0             ; the 256 high values
@high:          LDA     (SPTR),Y
                LSR     A
                LSR     A
                STA     VBUF + 86,Y
                INY
                BNE     @high
                LDA     #$D5
                STA     NIBBUF
                LDA     #$AA
                STA     NIBBUF + 1
                LDA     #$AD
                STA     NIBBUF + 2
                STZ     D2X            ; the XOR chain: VBUF 0-341 to NIBBUF 3-344
                LDX     #0
@chain:         LDA     VBUF,X
                PHA
                EOR     D2X
                TAY
                LDA     D2CODE62,Y
                STA     NIBBUF + 3,X
                PLA
                STA     D2X
                INX
                BNE     @chain
                LDX     #0
@chain2:        LDA     VBUF + 256,X
                PHA
                EOR     D2X
                TAY
                LDA     D2CODE62,Y
                STA     NIBBUF + 3 + 256,X
                PLA
                STA     D2X
                INX
                CPX     #342 - 256
                BNE     @chain2
                LDY     D2X            ; the checksum: the last value
                LDA     D2CODE62,Y
                STA     NIBBUF + 3 + 342
                LDA     #$DE
                STA     NIBBUF + 346
                LDA     #$AA
                STA     NIBBUF + 347
                LDA     #$EB
                STA     NIBBUF + 348
                RTS

; The field: five sync bytes ($FF, ten bits each: a load every 280
; cycles), then NIBBUF's 349, a load every 224; each load (a write to Q6
; on, the byte on the bus) held 28 cycles before Q6 off. The first is the
; write to Q7 on, which starts write mode, Q6 already on from the sense.
; Every count is to the cycle: STA's write is its last cycle, an absolute
; LDA's read its last, and the loop and its branches in one page.
.align 256
WLOOP:          LDA     #$FF
                STA     DISK2 + D2_Q7ON
                DLY     24
                LDA     DISK2 + D2_Q6OFF
.repeat 4
                DLY     246
                LDA     #$FF
                STA     DISK2 + D2_Q6ON
                DLY     24
                LDA     DISK2 + D2_Q6OFF
.endrepeat
                DLY     242
                LDY     #0
WLA:            LDA     NIBBUF,Y
                STA     DISK2 + D2_Q6ON
                DLY     24
                LDA     DISK2 + D2_Q6OFF
                INY
                BEQ     WLWRAP
                DLY     181
                JMP     WLA
WLWRAP:         DLY     183
WLB:            LDA     NIBBUF + 256,Y
                STA     DISK2 + D2_Q6ON
                DLY     24
                LDA     DISK2 + D2_Q6OFF
                INY
                CPY     #NIB_B
                BEQ     WLEND
                DLY     179
                JMP     WLB
WLEND:          DLY     185
                LDA     DISK2 + D2_Q7OFF
                LDA     DISK2 + D2_Q6OFF
                RTS
WLOOP_END:
.assert >WLOOP = >(WLOOP_END - 1), lderror, "the write loop crosses a page"

; ---------------------------------------------------------------------------
; Tables

; The 6-and-2 code's 64 disk bytes, in order (bit 7 set, no two 0 bits
; together but one pair, two 1 bits together in bits 0-6, not $AA or $D5).
D2CODE62:       .byte   $96, $97, $9A, $9B, $9D, $9E, $9F, $A6, $A7, $AB, $AC, $AD, $AE, $AF, $B2, $B3
                .byte   $B4, $B5, $B6, $B7, $B9, $BA, $BB, $BC, $BD, $BE, $BF, $CB, $CD, $CE, $CF, $D3
                .byte   $D6, $D7, $D9, $DA, $DB, $DC, $DD, $DE, $DF, $E5, $E6, $E7, $E9, $EA, $EB, $EC
                .byte   $ED, $EE, $EF, $F2, $F3, $F4, $F5, $F6, $F7, $F9, $FA, $FB, $FC, $FD, $FE, $FF
; DOS 3.3's interleave: the file's sector f is physical FILE2PHYS[f].
FILE2PHYS:      .byte   $0, $D, $B, $9, $7, $5, $3, $1, $E, $C, $A, $8, $6, $4, $2, $F
PHYS2FILE:      .byte   $0, $7, $E, $6, $D, $5, $C, $4, $B, $3, $A, $2, $9, $1, $8, $F
SW2:            .byte   0, 2, 1, 3      ; two bits swapped
BITS:           .byte   $01, $02, $04, $08, $10, $20, $40, $80
AUXLO:
.repeat 16, i
                .byte   <(AUXA + i * 86)
.endrepeat
AUXHI:
.repeat 16, i
                .byte   >(AUXA + i * 86)
.endrepeat

.align 256
NIBBUF:         .res    512            ; the field to write: page-aligned, no index crosses a page
DEC62:          .res    256            ; a disk byte's six-bit value, $FF not one of the code
VBUF:           .res    342
AUXA:           .res    16 * 86        ; each sector's 86 low values, till COMBINE

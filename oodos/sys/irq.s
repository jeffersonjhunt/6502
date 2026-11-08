; OODOS's interrupts and its clocks (hw/oolib.md, "The plan", L1): the IRQ
; dispatcher in common memory, the millisecond and frame counters, the
; event area, the frame and tick hooks, and function 80 (frame and timing).
;
; The dispatcher is the IRQ vector's (block 62's $FFFE, VEC). It runs in any
; window state: a program, the CCP, the BDOS or the BIOS interrupted. It
; puts W0 on the system block, so its state is there, but keeps S as it
; was: when the BDOS was running W0 was the system block already and the
; dispatcher stacks under it; when anything else was, the system's stack is
; empty (the BDOS stub starts it afresh at each call), so the same S points
; into free stack there. It reads IRQL and IRQH and answers each card:
;
;   the VIA (ID 6)     timer 1, free-running at 1 kHz: the millisecond
;                      counter, the event area's copy, the tick hook; with
;                      a layout-2 area open, the sticks and the paddles
;                      (input.s's L2TICK, when something changed or is due)
;   video (ID 9)       vertical blank: the frame counter, the event area,
;                      the frame hook
;   anything else      the USB card's events with a layout-2 area open
;                      (input.s's IRQOTH); else a card interrupting that
;                      OODOS didn't turn on (a program broke the rules):
;                      DEAFEN turns every such enable off, so it can't hold
;                      /IRQ low for ever
;
; It touches no BDOS state, only its own (here) and the windows, which it
; puts back as they were. The event area is the program's memory: W1 is put
; on its block for the writes and back after.
;
; The hooks (question 3): a routine of the program's, called with the
; windows the program had when it set the hook (W0-W3), A, X and Y saved by
; the dispatcher, ending in RTS. Its stack is page 1 of its own W0, under
; whatever was in use there: under the program's S, or under the S the
; program had at its BDOS call (the stub's ST_S) when the BDOS was
; interrupted, whichever is lower, so neither the program's stack nor the
; BDOS's (the same page index, the other block) is written over. The tick
; hook runs with interrupts off, and mustn't CLI (the next tick would call
; it again, on top of itself). The frame hook runs with them on, so a
; tick isn't lost while it runs (its budget, 8,000 cycles, is more than a
; tick); a vertical blank while it still runs is counted, but the hook
; isn't called again for it (FHSKIP counts those). Neither may call the
; BDOS, use DMA, far call, write the VIA's ACR, IER or IFR, or touch timer
; 1 but to read T1CH or its latch (reading T1CL clears the tick's flag).
; A hook stacks on page 1 of the W0 it was set with. Budgets:
; 8,000 cycles a frame hook, 300 a tick hook; not enforced. A debug build
; (OOLIB_DEBUG, `make DEBUG=1`) measures each hook by timer 1 and keeps the
; most each took, for FT_COSTS.
;
; Function 80, frame and timing: X = 0, A the operation, no parameters;
; else A/X a block, +0 the operation (oodos.inc's FT_*), its parameters
; after. A = X = $FF for an operation that isn't one, a block needed and
; not given, or what it can't do.

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.import NMI
.importzp PA, PX, PP, T0, T1, T2, T3, T4, T5 ; the BDOS's (bdos.inc), for 80
.import RETFF, PGET, PPUT
.import R62, QSLOTS, L2OPEN, CMAPDEF ; input.s's
.import R61, SIENSET, SIEN, C2ON, SCLAIM ; sound.s's
.export IRQ, DEAFEN, CLKOFF, CLKINIT, F_FRAME, CALL61, CALLHOOK, HOOKADDR, SETHOOK
.export HK, DHON
.export CLOCKS, MS, MSACC, EVDIRTY, EVON, FHON, THON, FHBUSY, FHSKIP
.export FRAMES, EVBLK, SNAP, STKA, STKB, PADFRM, PADDUE, UCARD
.exportzp EVP, VIAL
.ifdef OOLIB_DEBUG
.export FHMAX, THMAX, HKON, HOOKCOST
.endif

.zeropage
EVP:            .res    2              ; the event area, in W1's view ($4000-$7FFF)
VIAL:           .res    1              ; IRQL for the VIA alone, <~NB_IRQ_VIA, if cold boot found
                                       ; one; else 0, which no IRQL is (modules 0-5 never
                                       ; interrupt): ID 6 is the 6551's then, not the VIA's

; A hook's record in HK: sys.inc's HK_*, HF, HT, HD

.segment "SYSBSS"
CLOCKS:         .res    1              ; CLK_FRAME, CLK_MS: the clocks cold boot started
MS:             .res    4              ; the millisecond counter, low byte first
FRAMES:         .res    4              ; the frame counter; MS + 4, for EVALL
MSACC:          .res    2              ; the Disk II's catch-up: cycles short of a tick (disk2.s)
IW0:            .res    1              ; W0 as the interrupt found it ($SYS_BLOCK: the BDOS's)
EVON:           .res    1              ; 1: the event area open
EVBLK:          .res    1              ; its block
EVDIRTY:        .res    1              ; 1: write all its counters at the next tick, not the low byte
FHBUSY:         .res    1              ; 1 while the frame hook runs
FHSKIP:         .res    2              ; vertical blanks it was still running at
THPER:          .res    2              ; the tick hook every n ms
THCNT:          .res    2              ; ms to its next call
HK:             .res    3 * HK_LEN     ; the hooks: the frame hook's at HF, the tick hook's at HT,
                                       ;   the ES5503's at HD
SNAP:           .res    8              ; MS and FRAMES at one moment, for function 80; 81's event
STKA:           .res    1              ; the sticks as last sampled (ports A and B, bits 0-4), the
STKB:           .res    1              ;   tick's to compare (a layout-2 area open)
PADFRM:         .res    1              ; the frame counter's low byte at the paddles' last PTRIG
PADDUE:         .res    1              ; MS's low byte when they are to be read
UCARD:          .res    1              ; 1: a USB card (cold boot looked)

PROG_LIMIT      = 24                   ; blocks below: banks 0-5, the event area's
.ifdef OOLIB_DEBUG
DBGC:           .res    2              ; HOOKCOST's
DBGR:           .res    1
.endif

FHON            = HK + HF + HK_ON
DHON            = HK + HD + HK_ON
THON            = HK + HT + HK_ON
.ifdef OOLIB_DEBUG
FHMAX           = HK + HF + HK_MAX
THMAX           = HK + HT + HK_MAX
.endif


; ---------------------------------------------------------------------------
; The entry, in common memory. BRK is the monitor, as before (NMI). The RTI
; at IRQ + 12 is the way out when no card is interrupting at all.

.segment "COMMON"

IRQ:            PHA
                PHX
                TSX
                LDA     $0103,X        ; the P the interrupt pushed
                AND     #$10           ; B: a BRK
                BNE     IRQBRK
                BRA     DISPATCH
IRQRTI:         RTI
.assert IRQRTI = IRQ + 12, error, "IRQ's RTI moved (test_oodos_sys's an_irq_that_isnt_brk_returns)"
IRQBRK:         JMP     NMI

DISPATCH:       LDA     NB_WIN0
                LDX     #SYS_BLOCK
                STX     NB_WIN0        ; the system's state, and its stack page: S as it was
                PHA                    ; the interrupted W0
                STA     IW0
                PHY
                LDA     NB_IRQL
                CMP     VIAL
                BNE     @more          ; not the VIA alone, or no VIA
                LDA     NB_IRQH
                INC     A
                BNE     @more
; The VIA alone, the tick's case: as VIASRV, inline. Any flag but timer 1's
; goes the long way.
                LDA     VIA_IFR
                AND     VIA_IER
                AND     #<~VIA_IER_SET
                CMP     #VIA_IRQ_T1
                BNE     @via
                JSR     TICK
                BRA     @out
@via:           JSR     VIASRV
                BRA     @out
@more:          LDA     NB_IRQL
                AND     NB_IRQH
                INC     A
                BNE     @some
                PLY                    ; no card interrupting: nothing to answer
                PLA
                STA     NB_WIN0
                PLX
                PLA
                BRA     IRQRTI
@some:          LDA     NB_IRQL
                AND     #NB_IRQ_VIA
                BNE     @novia
                BIT     VIAL
                BMI     @isvia
                JSR     DEAF62         ; no VIA: the 6551 (DEAFEN), its registers under $C34x;
                BRA     @novia         ; then the video card's, as with a VIA
@isvia:         JSR     VIASRV
@novia:         LDA     NB_IRQH
                AND     #NB_IRQ_VIDEO
                BNE     @novideo
                JSR     VIDSRV
@novideo:       LDA     NB_IRQL        ; any other card's
                ORA     #NB_IRQ_VIA
                INC     A
                BNE     @deaf
                LDA     NB_IRQH
                ORA     #NB_IRQ_VIDEO
                INC     A
                BEQ     @out
@deaf:          LDX     #R62_OTHER     ; the USB card's events (a layout-2 area), or DEAFEN
                JSR     CALL62
@out:           PLY
                PLA
                STA     NB_WIN0        ; the interrupted stack page, under the same S
                PLX
                PLA
IRQEND:         RTI                    ; (test_oodos_input times the dispatcher to here)

; The VIA (cold boot found one: VIAL): timer 1 the tick; any other flag
; enabled (OODOS enables none) turned off. Nothing at all (the stand-in
; 6551, a program's, on the same ID): DEAFEN.
VIASRV:         LDA     VIA_IFR
                AND     VIA_IER
                AND     #<~VIA_IER_SET
                BEQ     @none
                PHA
                AND     #VIA_IRQ_OTHERS
                BEQ     @t1
                STA     VIA_IER        ; bit 7 clear: these off
                STA     VIA_IFR        ; and their flags
@t1:            PLA
                AND     #VIA_IRQ_T1
                BNE     TICK
                RTS
@none:          ; fall through

; DEAFEN, in block 62's code window, whatever W3 has; CALL62, input.s's
; R62 routine X there.
DEAF62:         LDX     #R62_DEAF
CALL62:         LDA     NB_WIN3
                PHA
                LDA     #RES_BLOCK
                STA     NB_WIN3
                JSR     R62
                PLA
                STA     NB_WIN3
                RTS

; A millisecond: timer 1's flag cleared, the counter on, the event area's
; copy (its low byte, or all of it after a carry or a catch-up), the sticks
; and the paddles with a layout-2 area open, the tick hook if it is due.
; With no area open the path is L1's, cycle for cycle (test_oodos_input's
; tick cost); a layout-1 area's is 4 cycles shorter (W1 kept in X, not on
; the stack). Layout 2 compares the sticks with STKA and STKB and the frame
; and ms counters with PADFRM and PADDUE inline, and goes to block 62
; (L2TICK) only when one differs: a stick moved, a frame began (PTRIG), or
; the paddles are due. With no frames (no video card) PADDUE is passed
; every 128 ms with no PTRIG sent: L2TICK is entered then, a harmless
; no-op (PADARM clear), and puts it 128 ms on again.
TICK:           LDA     VIA_T1CL       ; the flag cleared
                INC     MS
                BNE     @ev
                INC     MS + 1
                BNE     @dirty
                INC     MS + 2
                BNE     @dirty
                INC     MS + 3
@dirty:         LDA     #1
                STA     EVDIRTY
@ev:            LDA     EVON
                BEQ     @hook
                LDA     EVDIRTY
                BNE     @all
                LDX     NB_WIN1
                LDA     EVBLK
                STA     NB_WIN1
                LDY     #EV_MS
                LDA     MS
                STA     (EVP),Y
                STX     NB_WIN1
@l2:            BIT     EVON
                BMI     @in
@hook:          LDA     THON
                BEQ     @done
                LDA     THCNT
                BNE     @dec
                DEC     THCNT + 1
@dec:           DEC     THCNT
                LDA     THCNT
                ORA     THCNT + 1
                BEQ     THOOK
@done:          RTS
@all:           JSR     EVALL
                BRA     @l2
@in:            LDA     VIA_ORA_NH     ; a layout-2 area: anything to do?
                EOR     STKA
                AND     #$1F
                BNE     @go
                LDA     VIA_ORB        ; (reading port B clears CB1's and CB2's flags)
                EOR     STKB
                AND     #$1F
                BNE     @go
                LDA     FRAMES
                EOR     PADFRM
                BNE     @go
                LDA     MS             ; MS - PADDUE >= 0: due (L2TICK checks a PTRIG went)
                SEC
                SBC     PADDUE
                BMI     @hook
@go:            LDX     #R62_TICK
                JSR     CALL62
                BRA     @hook
.assert EVON_L2 = $80, error, "TICK tests layout 2 by EVON's bit 7"

; The tick hook, due again in THPER ms; called with interrupts off.
THOOK:          LDA     THPER
                STA     THCNT
                LDA     THPER + 1
                STA     THCNT + 1
                LDY     #HT
                ; fall through

; Call hook Y (HF, HT) with its windows: the interrupted W1-W3 on the stack
; (W0 is there already) and S in its record; S lowered to the program's at
; its call if the BDOS was interrupted (above); the frame hook with
; interrupts on. Its RTS comes to FHRET or THRET, which put it all back.
CALLHOOK:       LDA     NB_WIN1
                PHA
                LDA     NB_WIN2
                PHA
                LDA     NB_WIN3
                PHA
                TSX
                TXA
                STA     HK + HK_S,Y
                LDA     IW0
                CMP     #SYS_BLOCK
                BNE     @s             ; not the BDOS: S as it is
                CPX     ST_S
                BCC     @s             ; under the program's S at its call already
                LDX     ST_S
                TXS
@s:             LDA     HK + HK_WIN + 1,Y
                STA     NB_WIN1
                LDA     HK + HK_WIN + 2,Y
                STA     NB_WIN2
                LDA     HK + HK_WIN + 3,Y
                STA     NB_WIN3        ; its common memory the same: this goes on
.ifdef OOLIB_DEBUG
                LDX     #R62_HKON      ; its start counted (HKON, block 62's): Y kept
                JSR     CALL62
.endif
                CPY     #HT            ; carry set: the tick hook's (kept to the BCS)
                LDA     HK + HK_ADDR + 1,Y
                TAX
                LDA     HK + HK_WIN,Y
                PHA
                LDA     HK + HK_ADDR,Y
                PLY                    ; its W0
                STY     NB_WIN0        ; its zero page and stack page from here
                BCS     @tick
                LDY     #>(FHRET - 1)
                PHY
                LDY     #<(FHRET - 1)
                PHY
                PHX
                PHA
                CLI
                RTS                    ; to the frame hook
@tick:          LDY     #>(THRET - 1)
                PHY
                LDY     #<(THRET - 1)
                PHY
                PHX
                PHA
                RTS                    ; to the tick hook
FHRET:          SEI
                LDY     #HF
                BRA     HKBACK
THRET:          LDY     #HT
HKBACK:         LDA     #SYS_BLOCK
                STA     NB_WIN0
                LDX     HK + HK_S,Y
                TXS
.ifdef OOLIB_DEBUG
                LDX     #R62_HKCOST    ; its cycles counted (HOOKCOST, block 62's): Y kept
                JSR     CALL62
.endif
                PLA
                STA     NB_WIN3
                PLA
                STA     NB_WIN2
                PLA
                STA     NB_WIN1
                RTS

; Video: a vertical blank counted, the event area written, the frame hook
; called; the line and collision interrupts (OODOS enables neither) off.
VIDSRV:         LDA     VIDEO_ISR
                AND     VIDEO_IEN
                AND     #VIDEO_IRQ_ALL
                PHA
                AND     #<~VIDEO_IRQ_VBLANK
                BEQ     @vbl
                STA     VIDEO_ISR      ; theirs cleared, and turned off
                EOR     #$FF
                AND     VIDEO_IEN
                STA     VIDEO_IEN
@vbl:           PLA
                AND     #VIDEO_IRQ_VBLANK
                BEQ     @done
                STA     VIDEO_ISR
                INC     FRAMES
                BNE     @ev
                INC     FRAMES + 1
                BNE     @ev
                INC     FRAMES + 2
                BNE     @ev
                INC     FRAMES + 3
@ev:            LDA     EVON
                BEQ     @hook
                JSR     EVALL
                BIT     EVON           ; a layout-2 area and the stream claimed: the sound part
                BPL     @hook          ; (sound.s's SFIELDS, block 61)
                LDA     SCLAIM
                AND     #SC_STREAM
                BEQ     @hook
                LDX     #R61_FRAME
                JSR     CALL61
@hook:          LDA     FHON
                BEQ     @done
                LDA     FHBUSY
                BEQ     FHOOK
                INC     FHSKIP         ; still running from the last
                BNE     @done
                INC     FHSKIP + 1
@done:          RTS

; The frame hook, with interrupts on while it runs: nothing nests in it but
; the tick's handler and its hook (FHBUSY).
FHOOK:          INC     FHBUSY
                LDY     #HF
                JSR     CALLHOOK
                STZ     FHBUSY
                RTS

; The event area's counters, all eight bytes, through W1.
EVALL:          LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDY     #EV_FRAMES + 3
@copy:          LDA     MS - EV_MS,Y
                STA     (EVP),Y
                DEY
                CPY     #EV_MS
                BCS     @copy
                PLA
                STA     NB_WIN1
                STZ     EVDIRTY
                RTS
.assert FRAMES = MS + 4 && EV_FRAMES = EV_MS + 4, error, "EVALL copies MS and FRAMES as one"

; CALL61, sound.s's R61 routine X in block 61 (the BIOS), W2 borrowed for it
; and put back byte for byte, as CALL62 borrows W3 (L3's handlers: W1 is
; the event area's, never code). Here, after the tick's path, so the tick's
; branches stay where they were (test_oodos_input times the tick).
CALL61:         LDA     NB_WIN2
                PHA
                LDA     #BIOS_BLOCK
                STA     NB_WIN2
                JSR     R61
                PLA
                STA     NB_WIN2
                RTS


.ifdef OOLIB_DEBUG
; The debug build's counters, in block 62's code window (RES), reached by
; CALL62 (L3's step (b): out of common memory, which L3 needs). Each keeps
; Y. Going through CALL62 adds about 35 cycles to each hook's measured
; cost (the switch back after HKON, and to HOOKCOST before its count).
.segment "RES"

; Hook Y's call: timer 1's count and MS's low byte, for HOOKCOST.
HKON:           JSR     T1NOW
                STA     HK + HK_C0,Y
                TXA
                STA     HK + HK_C0 + 1,Y
                LDA     MS
                STA     HK + HK_M0,Y
                RTS

; Timer 1's count now in A (low) and X (high); a tick that came due
; meanwhile counted first, since reading the count clears its flag (its
; hook, if due, waits for the next). One that comes due between the IFR's
; check and the read of T1CL has its flag cleared by that read unseen: the
; count, just reloaded, is then within a few cycles of the top, so a count
; read within T1LATE cycles of it with no flag seen is counted too (TICK
; reads T1CL about 87 cycles after its expiry at the soonest: the
; interrupt's 7, IRQ's 19 and the dispatcher's 57 to TICK, and its LDA's 4;
; a hook's call is later still, so no tick is counted twice). No VIA: 0,
; the 6551's registers left alone, and FT_COSTS says it counted nothing
; (A = 0), as a build without OOLIB_DEBUG does.
T1LATE          = 24
.assert <TICK_LATCH >= T1LATE, error, "T1NOW's top: the latch's low byte less T1LATE"
T1NOW:          BIT     VIAL
                BMI     @via
                LDA     #0
                TAX
                RTS
@via:           LDA     VIA_IFR
                AND     #VIA_IRQ_T1
                BEQ     @late
                JSR     @tick
@read:          LDX     VIA_T1CH       ; the high byte the same either side of the low
                LDA     VIA_T1CL
                CPX     VIA_T1CH
                BNE     @read
                RTS
@late:          LDX     VIA_T1CH       ; no flag seen: read it the same way, and then
                LDA     VIA_T1CL
                CPX     VIA_T1CH
                BNE     @late
                CPX     #>TICK_LATCH   ; just reloaded? Then its expiry came meanwhile
                BNE     @done
                CMP     #<TICK_LATCH - T1LATE
                BCC     @done
                PHA
                JSR     @tick
                PLA
@done:          RTS
@tick:          INC     MS
                BNE     @d
                INC     MS + 1
                BNE     @d
                INC     MS + 2
                BNE     @d
                INC     MS + 3
@d:             LDA     #1
                STA     EVDIRTY
                RTS

; A/X (high in A) + Y ticks of TICK_CYCLES: exact in 16 bits while Y is 8 or
; less (a count's difference is under a tick either way), else $FFFF.
ADDTK:          CPY     #9
                BCS     @max
                CPY     #0
                BEQ     @done
@add:           PHA
                TXA
                CLC
                ADC     #<TICK_CYCLES
                TAX
                PLA
                ADC     #>TICK_CYCLES
                DEY
                BNE     @add
@done:          RTS
@max:           LDA     #$FF
                TAX
                RTS

; Hook Y's cycles, from its call (HK_C0, HK_M0) to now, kept in its HK_MAX
; if they are the most yet. Keeps Y.
HOOKCOST:       CLD
                STY     DBGR
                JSR     T1NOW
                STA     DBGC
                STX     DBGC + 1
                SEC                    ; C0 - now: timer 1 counts down
                LDA     HK + HK_C0,Y
                SBC     DBGC
                TAX
                LDA     HK + HK_C0 + 1,Y
                SBC     DBGC + 1
                PHA
                LDA     MS             ; and the ticks between
                SEC
                SBC     HK + HK_M0,Y
                TAY
                PLA
                JSR     ADDTK
                LDY     DBGR
                CMP     HK + HK_MAX + 1,Y
                BCC     @less
                BNE     @more
                PHA
                TXA
                CMP     HK + HK_MAX,Y
                PLA
                BCC     @less
@more:          STA     HK + HK_MAX + 1,Y
                TXA
                STA     HK + HK_MAX,Y
@less:          RTS
.endif

; ---------------------------------------------------------------------------
; Cold boot's part, in block 62: the clocks started. A VIA is there if its
; IER reads bit 7 set (the stand-in 6551's mirror at $C34E, on a machine
; without one, is its command register, $0B); a video card by its ID.

.segment "RESBSS"
DMAIRQ:         .res    1              ; DEAFEN's: the channels whose interrupt was on

.segment "RES"

; Every interrupt enable that isn't OODOS's own off, and what is pending
; there cleared: a program's (the warm boot), or one no handler answers
; (the dispatcher, through W3). OODOS's own, timer 1 and vertical blank, stay
; on, and timer 1 is set up again as CLKINIT has it (free-running, its
; latch, its enable), in case a program changed it; MS runs on, though a
; program that changed the latch may cost it a tick (below). A DMA
; channel's terminal count is cleared only if its interrupt was on: the
; others' are their drivers' (SDIO reads channel 0's), and an interrupt
; can come while one waits.
DEAFEN:         LDA     #NB_RTC_TICK   ; the clock: TICK cleared, TIE off (SET 0 sends nothing)
                STA     NB_RTCCTL
                STZ     SD_IEN
                STZ     USB_IEN
                LDA     SIEN           ; the sound card's: L3's own kept (sound.s)
                STA     SOUND_IEN
                STZ     CONSOLE_IEN
                LDA     #1             ; a front-panel key pending pulls /IRQ whatever IEN says
                STA     CONSOLE_ISR
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @acia
                STZ     DMAIRQ
                LDX     #$30           ; channel 3 to 0
@dma:           CPX     #$20           ; channel 2 while a stream buffer moves on it is L3's:
                BNE     @mode          ; its interrupt and its count left as they are
                LDA     C2ON
                BEQ     @mode
                ASL     DMAIRQ
                BRA     @next
@mode:          LDA     DMA0_MODE,X
                ASL     A              ; its interrupt bit (7) to carry,
                ROL     DMAIRQ         ; and on into the mask: channel 3's ends in bit 3
                LSR     A              ; the mode with its interrupt off
                STA     DMA0_MODE,X
@next:          TXA
                SEC
                SBC     #$10
                TAX
                BCS     @dma
                LDA     DMAIRQ         ; none to come, and none left to interrupt
                STA     DMA_STATUS
.assert DMA_MODE_IRQ = $80, error, "DEAFEN's mask: the interrupt bit is MODE's bit 7"
@acia:          LDA     ACIA_CMD       ; the stand-in 6551's, should a program have turned them
                AND     #<~ACIA_CMD_TXMASK ; on: its receiver's interrupt off, and its
                ORA     #ACIA_CMD_RXOFF | ACIA_CMD_TXOFF ; transmitter's (RTS low), the rest kept;
                STA     ACIA_CMD
                LDA     ACIA_STATUS    ; then its flag cleared, with nothing to set it again
                LDA     CLOCKS
                AND     #CLK_MS
                BEQ     @video         ; no VIA: $C34E would be the 6551's command
                LDA     #VIA_IRQ_OTHERS
                STA     VIA_IER        ; all but timer 1 off
                STA     VIA_IFR
                LDA     #VIA_ACR_T1FREE
                STA     VIA_ACR        ; timer 1 as CLKINIT left it
                LDA     #VIA_IER_SET | VIA_IRQ_T1
                STA     VIA_IER
                LDA     VIA_T1LL       ; the latch read (safe: no flag cleared) and
                CMP     #<TICK_LATCH   ; written only if a program changed it, since
                BNE     @latch         ; writing its high byte clears timer 1's flag: a
                                       ; tick already due then is lost, MS a ms behind
                LDA     VIA_T1LH
                CMP     #>TICK_LATCH
                BEQ     @video
@latch:         LDA     #<TICK_LATCH
                STA     VIA_T1LL
                LDA     #>TICK_LATCH
                STA     VIA_T1LH       ; the counter takes it at its next expiry
@video:         LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @done
                STA     VIDEO_IEN      ; vertical blank alone
                LDA     #VIDEO_IRQ_LINE | VIDEO_IRQ_COLLIDE
                STA     VIDEO_ISR
@done:          RTS

; OODOS's own interrupts off, for leaving it (Apple II mode, function 75):
; the clocks', and the USB card's (a layout-2 area's).
CLKOFF:         STZ     USB_IEN
                STZ     SOUND_IEN      ; and the sound card's (L3's)
                LDA     CLOCKS
                AND     #CLK_MS
                BEQ     @video
                LDA     #<~VIA_IER_SET
                STA     VIA_IER
@video:         LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @done
                STZ     VIDEO_IEN
@done:          RTS

CLKINIT:        STZ     CLOCKS
                STZ     VIAL
                STZ     UCARD
                LDA     USB_ID
                CMP     #USB_ID_V1
                BNE     @via
                INC     UCARD
@via:           JSR     CMAPDEF        ; the controllers as question 12 has them (the warm boot's too)
                LDX     #R61_INIT      ; the sound card and the DMA controller (sound.s)
                JSR     CALL61
                LDA     VIA_IER
                BPL     @video
                LDA     #<~VIA_IER_SET
                STA     VIA_IER        ; all off, all flags cleared
                STA     VIA_IFR
                LDA     #VIA_ACR_T1FREE
                STA     VIA_ACR
                LDA     #<TICK_LATCH
                STA     VIA_T1CL
                LDA     #>TICK_LATCH
                STA     VIA_T1CH       ; the latch, and timer 1 started from it
                LDA     #VIA_IER_SET | VIA_IRQ_T1
                STA     VIA_IER
                LDA     #CLK_MS
                TSB     CLOCKS
                LDA     #<~NB_IRQ_VIA
                STA     VIAL           ; the dispatcher's fast path, and VIASRV, from here
@video:         LDA     VIDEO_ID
                CMP     #VIDEO_ID_V1
                BNE     @done
                LDA     #VIDEO_IRQ_ALL
                STA     VIDEO_ISR
                LDA     #VIDEO_IRQ_VBLANK
                STA     VIDEO_IEN
                LDA     #CLK_FRAME
                TSB     CLOCKS
@done:          RTS

; ---------------------------------------------------------------------------
; Function 80, frame and timing.

.segment "BDOS"

F_FRAME:        LDX     PX
                BEQ     @op            ; X = 0: A is the operation
                JSR     PGET           ; the block's +0; PP then at +1
@op:            CMP     #FT_OPS
                BCS     @ff
                ASL     A
                TAX
                JMP     (FTJUMPS,X)
@ff:            JMP     RETFF
FTJUMPS:        .word   FT_INFOX, FT_COUNTX, FT_OPENX, FT_CLOSEX, FT_WAITX, FT_DELAYX
                .word   FT_FHOOKX, FT_THOOKX, FT_COSTSX
.assert (* - FTJUMPS) / 2 = FT_OPS, error, "FTJUMPS has an entry for each of oodos.inc's FT_*"

; OOLIB's interface version in A and the service areas in X; a block gets
; them at +1 and +2, then the clocks that run (+3) and the event area's
; layout, the highest this OODOS writes (+4).
FT_INFOX:       LDX     PX
                BEQ     @ret
                LDA     #OOLIB_IFACE
                JSR     PPUT
                LDA     #OOLIB_AREAS
                JSR     PPUT
                LDA     CLOCKS
                JSR     PPUT
                LDA     #EV_LAYOUT2
                JSR     PPUT
@ret:           LDA     #OOLIB_IFACE
                LDX     #OOLIB_AREAS
                RTS
.assert OOLIB_AREAS = OA_FRAME | OA_INPUT | OA_SOUND, error, "L3 has frame and timing, input, and sound"
.assert CLK_FRAME = FC_FRAME && CLK_MS = FC_MS, error, "CLOCKS is FT_INFO's +3, EV_CLOCKS"

; MS and FRAMES at one moment, into SNAP.
SNAPIT:         PHP
                SEI
                LDX     #7
@copy:          LDA     MS,X
                STA     SNAP,X
                DEX
                BPL     @copy
                PLP
                RTS

FT_COUNTX:      JSR     SNAPIT
                LDX     PX
                BEQ     @ret
                LDX     #0
@put:           LDA     SNAP,X
                JSR     PPUT
                INX
                CPX     #8
                BNE     @put
@ret:           LDA     SNAP
                LDX     SNAP + 1
                RTS

; The event area: +1-2 its address, +3 its size (0: 256). In one window,
; W0-W2, on a block of banks 0-5 (the program's and the memory functions';
; not flash, read-only, the system's, M: or video memory); EV_LEN bytes at
; least: layout 1; EV_LEN2 or more: layout 2 (L2's input), its queue's
; slots as many as fit. Filled from scratch (the layout, the counters, the
; queue's slots, the rest 0), layout 2's input then put in by L2OPEN, then
; the handlers write it.
FT_OPENX:       LDX     PX
                BNE     @block
@no0:           JMP     RETFF
@block:         JSR     PGET
                STA     T0             ; T0/T1 the address, T2 the size
                JSR     PGET
                STA     T1
                JSR     PGET
                STA     T2
                CMP     #0
                BEQ     @big
                CMP     #EV_LEN
                BCC     @no0
@big:           CLC                    ; T3/T4 its last byte
                LDA     T0
                ADC     T2
                STA     T3
                LDA     T1
                ADC     #0
                STA     T4
                LDA     T2
                BNE     @last
                INC     T4             ; 256 bytes
@last:          LDA     T3
                BNE     @dec
                DEC     T4
@dec:           DEC     T3
                LDA     T1             ; the same window, and not W3
                EOR     T4
                AND     #$C0
                BNE     @no0
                LDA     T1
                CMP     #$C0
                BCS     @no0
                ROL     A
                ROL     A
                ROL     A
                AND     #3
                TAX
                LDA     ST_WIN,X
                CMP     #PROG_LIMIT    ; banks 0-5, RAM: not the system's, M:, video memory or flash
                BCS     @no0
                PHP                    ; any area before closed, its input first,
                SEI                    ; with no interrupt between the two
                STZ     USB_IEN
                STZ     EVON
                PHA
                JSR     SIENSET        ; the mailbox's interrupt off with it (sound.s)
                PLA
                PLP
                STA     EVBLK
                STZ     QSLOTS         ; the layout in T5, the queue's slots (layout 2)
                LDA     #EV_LAYOUT1
                LDX     T2
                BEQ     @two
                CPX     #EV_LEN2
                BCC     @one
@two:           TXA                    ; (256 - 128 is 128, as 0 - 128 is)
                SEC
                SBC     #EV_QUEUE
                LSR     A
                LSR     A
                LSR     A
                STA     QSLOTS
                LDA     #EV_LAYOUT2
@one:           STA     T5
                LDA     T0
                STA     EVP
                LDA     T1
                AND     #$3F
                ORA     #$40
                STA     EVP + 1
                JSR     SNAPIT
                LDA     T0             ; the area from its start
                STA     PP
                LDA     T1
                STA     PP + 1
                LDY     #0
@fill:          LDA     #0
                CPY     #EV_LAYOUT
                BNE     @slots
                LDA     T5
@slots:         CPY     #EV_QSLOTS
                BNE     @1
                LDA     QSLOTS         ; 0 in layout 1
@1:             CPY     #EV_CLOCKS
                BNE     @2
                LDA     CLOCKS
@2:             CPY     #EV_SIZE
                BNE     @3
                LDA     T2
@3:             CPY     #EV_MS
                BCC     @4
                CPY     #EV_FRAMES + 4
                BCS     @4
                LDA     SNAP - EV_MS,Y
@4:             JSR     PPUT
                INY
                CPY     T2
                BNE     @fill
                LDA     #1
                STA     EVDIRTY        ; the next tick writes it all, ticks meanwhile too
                LDX     T5
                CPX     #EV_LAYOUT2
                BEQ     @l2
                STA     EVON
                BRA     @ok
@l2:            JSR     L2OPEN         ; layout 2's input, and EVON (input.s)
@ok:            JSR     SIENSET        ; the mailbox's interrupt, if the 6510 is claimed
                LDA     #0
                TAX
                RTS
@no:            JMP     RETFF
.assert EV_QUEUE = $80 && EV_LEN2 > EV_QUEUE, error, "FT_OPEN's slots: (size - 128) / 8"

FT_CLOSEX:      PHP                    ; a layout-2 area's input off, then the area,
                SEI                    ; with no interrupt between the two
                STZ     USB_IEN
                STZ     EVON
                JSR     SIENSET        ; and the mailbox's (sound.s)
                PLP
                LDA     #0
                TAX
                RTS

; The next frame, or the block's +1 frames from now (0 as 1). A/X = the
; frame counter's low word then. No video card: A = X = $FF at once.
FT_WAITX:       LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @no
                LDA     #1
                LDX     PX
                BEQ     @n
                JSR     PGET
                CMP     #0
                BNE     @n
                LDA     #1
@n:             STA     T0
                LDA     FRAMES         ; one byte: read whole
                STA     T1
@wait:          WAI                    ; until an interrupt: a tick, or the frame
                LDA     FRAMES
                SEC
                SBC     T1
                CMP     T0
                BCC     @wait
                JSR     SNAPIT
                LDA     SNAP + 4
                LDX     SNAP + 5
                RTS
@no:            JMP     RETFF

; +1-2 milliseconds from now. No VIA: A = X = $FF at once. A = X = $FF too
; if the millisecond counter stands still for STALL frames: timer 1 stopped
; (a program changed it against the rules), so the wait would never end.
; The frame counter is the witness: with no video card nothing gives up.
STALL           = 4
FT_DELAYX:      LDA     CLOCKS
                AND     #CLK_MS
                BEQ     @no
                LDX     PX
                BEQ     @no
                JSR     PGET
                STA     T0             ; T0/T1 the count
                JSR     PGET
                STA     T1
                JSR     SNAPIT
                LDA     SNAP           ; T2/T3 the start
                STA     T2
                LDA     SNAP + 1
                STA     T3
@moved:         LDA     SNAP           ; T4 MS's low byte when it last moved,
                STA     T4             ; T5 the frame counter's then
                LDA     SNAP + 4
                STA     T5
@wait:          JSR     SNAPIT
                LDA     SNAP
                CMP     T4
                BNE     @moved
                LDA     SNAP + 4
                SEC
                SBC     T5
                CMP     #STALL
                BCS     @no            ; stood still: given up
                SEC                    ; now - start, against the count
                LDA     SNAP
                SBC     T2
                TAX
                LDA     SNAP + 1
                SBC     T3
                CMP     T1
                BCC     @more
                BNE     @done
                CPX     T0
                BCS     @done
@more:          WAI
                BRA     @wait
@done:          LDA     #0
                TAX
                RTS
@no:            JMP     RETFF

; A hook's address from the block's +1-2 (none without a block), checked:
; not in the I/O hole or common memory; the program's W0 not the system's.
; T0/T1 the address less 1, carry set if it is refused, Z set if it is 0.
HOOKADDR:       STZ     T0
                STZ     T1
                LDX     PX
                BEQ     @none
                JSR     PGET
                STA     T0
                JSR     PGET
                STA     T1
                ORA     T0
                BEQ     @none
                LDA     T1
                CMP     #>COMMON
                BCS     @bad
                CMP     #$C8
                BCS     @ok
                CMP     #$C0
                BCS     @bad
@ok:            LDA     ST_W0
                CMP     #SYS_BLOCK
                BEQ     @bad
                LDA     T0             ; less 1, for the dispatcher's RTS
                BNE     @lo
                DEC     T1
@lo:            DEC     T0
                LDA     #1             ; Z clear
                CLC
                RTS
@none:          LDA     #0
                CLC
                RTS
@bad:           SEC
                RTS

FT_FHOOKX:      LDY     #HF
                STZ     FHON           ; the old one off before the new is set
                JSR     HOOKADDR
                BCS     SETNO
                BEQ     SETOK          ; none
                BRA     SETHOOK

FT_THOOKX:      LDY     #HT
                STZ     THON
                JSR     HOOKADDR
                BCS     SETNO
                BEQ     SETOK
                JSR     PGET           ; +3-4: every n ms, 1 at least
                STA     THPER
                JSR     PGET
                STA     THPER + 1
                ORA     THPER
                BEQ     SETNO
                LDA     THPER
                STA     THCNT
                LDA     THPER + 1
                STA     THCNT + 1
                ; fall through

; Hook Y set: T0/T1 its address less 1, the windows the program has now.
SETHOOK:        LDA     T0
                STA     HK + HK_ADDR,Y
                LDA     T1
                STA     HK + HK_ADDR + 1,Y
                LDX     #0
@win:           LDA     ST_WIN,X
                STA     HK + HK_WIN,Y
                INY
                INX
                CPX     #4
                BNE     @win
                DEY
                DEY
                DEY
                DEY
                LDA     #1
                STA     HK + HK_ON,Y   ; set last: the dispatcher reads it first
SETOK:          LDA     #0
                TAX
                RTS
SETNO:          JMP     RETFF

; +1-2 the most cycles a frame hook took, +3-4 a tick hook, +5-6 the
; vertical blanks a frame hook was still running at, since the warm boot.
; A = 1 if this OODOS counts the cycles (a debug build), 0 if not (they
; read 0).
FT_COSTSX:      LDX     PX
                BEQ     @no
                PHP
                SEI
.ifdef OOLIB_DEBUG
                LDA     FHMAX
                STA     SNAP
                LDA     FHMAX + 1
                STA     SNAP + 1
                LDA     THMAX
                STA     SNAP + 2
                LDA     THMAX + 1
                STA     SNAP + 3
.else
                STZ     SNAP
                STZ     SNAP + 1
                STZ     SNAP + 2
                STZ     SNAP + 3
.endif
                LDA     FHSKIP
                STA     SNAP + 4
                LDA     FHSKIP + 1
                STA     SNAP + 5
                PLP
                LDX     #0
@put:           LDA     SNAP,X
                JSR     PPUT
                INX
                CPX     #6
                BNE     @put
.ifdef OOLIB_DEBUG
                LDA     VIAL           ; counted by timer 1: none without the VIA
                BEQ     @a
                LDA     #1
@a:
.else
                LDA     #0
.endif
                LDX     #0
                RTS
@no:            JMP     RETFF

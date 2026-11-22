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
;   the 6551 (ID 6)    a byte on the serial line, into its ring (serial.s's
;                      SERID6, through block 62)
;   anything else      the USB card's events with a layout-2 area open
;                      (input.s's IRQOTH); else a card interrupting that
;                      OODOS didn't turn on (a program broke the rules):
;                      DEAFEN turns every such enable off, so it can't hold
;                      /IRQ low for ever
;
; It touches no BDOS state, only its own (here) and the windows, which it
; puts back as they were. WIN1H too (B7): it runs with WIN1H 0, and puts back
; what it read, on a revision 2 north bridge only (the value tells: 0, or
; $FF where there is no register, is never written). The event area is the
; program's memory: W1 is put on its block for the writes and back after.
; A hook gets its own WIN1H (HK_W1H, the program's when it set the hook).
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
.import VBON                           ; video.s's
.export IRQ, DEAFEN, CLKOFF, CLKINIT, F_FRAME, F_RANDOM, CALL61, CALLHOOK, HOOKADDR, SETHOOK
.export HK, DHON, DMACTL, WBDMA
.import DMAWARM                        ; video.s's (block 61)
.import RXDEAF, RXFLUSH                ; serial.s's (block 62)
.import RNGOK, RNGNEW, RNGBYTE         ; bios.s's: 78's driver
.export CLOCKS, EVDIRTY, EVON, FHON, THON, FHBUSY, FHSKIP
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
FRAMES:         .res    4              ; the frame counter (the millisecond counter is the bridge's: MS0-MS3)
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
SNAP:           .res    8              ; MS0-MS3 and FRAMES at one moment, for function 80; 81's event
STKA:           .res    1              ; the sticks as last sampled (ports A and B, bits 0-4), the
STKB:           .res    1              ;   tick's to compare (a layout-2 area open)
PADFRM:         .res    1              ; the frame counter's low byte at the paddles' last PTRIG
PADDUE:         .res    1              ; MS0 when they are to be read
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
                LDA     NB_WIN1H       ; and its WIN1H (B7), 0 while the dispatcher runs: cleared
                PHA                    ; only when it isn't 0
                BEQ     @w1h
                STZ     NB_WIN1H
@w1h:           LDA     NB_IRQL
                INC     A
                BNE     @more          ; a card in IDs 0-7
                LDA     NB_IRQH
                CMP     #<~NB_IRQ_NB
                BNE     @more          ; not the bridge's tick alone
; The tick alone (north bridge revision 3's MSF, IRQH bit 5; the bridge has no
; other interrupt): no flag to check.
                JSR     TICK
                BRA     @out
@more:          LDA     NB_IRQL
                AND     NB_IRQH
                INC     A
                BNE     @some
                PLA                    ; no card interrupting: nothing to answer; WIN1H back
                BEQ     @none
                STA     NB_WIN1H
@none:          PLY
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
                JSR     DEAF62         ; no VIA: the 6551 (SERID6, or DEAFEN), its registers under
                BRA     @novia         ; $C34x; then the video card's, as with a VIA
@isvia:         JSR     VIASRV
@novia:         LDA     NB_IRQH
                AND     #NB_IRQ_VIDEO
                BNE     @novideo
                JSR     VIDSRV
@novideo:       LDA     NB_IRQH        ; the tick with another card (flag 5)
                AND     #NB_IRQ_NB
                BNE     @notick
                JSR     TICK
@notick:        LDA     NB_IRQL        ; any other card's
                ORA     #NB_IRQ_VIA
                INC     A
                BNE     @deaf
                LDA     NB_IRQH
                ORA     #NB_IRQ_VIDEO | NB_IRQ_NB
                INC     A
                BEQ     @out
@deaf:          LDX     #R62_OTHER     ; the USB card's events (a layout-2 area), or DEAFEN
                JSR     CALL62
@out:           PLA                    ; the interrupted WIN1H, as it was read
                BEQ     @back
                STA     NB_WIN1H
@back:          PLY
                PLA
                STA     NB_WIN0        ; the interrupted stack page, under the same S
                PLX
                PLA
IRQEND:         RTI                    ; (test_oodos_input times the dispatcher to here)

; The VIA (cold boot found one: VIAL): OODOS enables none of its interrupts
; (the tick is the bridge's), so any flag enabled is a stray: turned off, its
; flag cleared. A program's timer 1 is polled only, so this never reads T1CL.
; Nothing at all: the stand-in 6551, on the same ID: its byte into the ring
; (SERID6), or DEAFEN.
VIASRV:         LDA     VIA_IFR
                AND     VIA_IER
                AND     #<~VIA_IER_SET
                BEQ     @none
                STA     VIA_IER        ; bit 7 clear: these off
                STA     VIA_IFR        ; and their flags
                RTS
@none:          ; fall through

; ID 6's other source, the 6551 (serial.s's SERID6, which DEAFENs if it
; isn't the 6551), in block 62's code window, whatever W3 has; CALL62,
; input.s's R62 routine X there.
DEAF62:         LDX     #R62_ID6
CALL62:         LDA     NB_WIN3
                PHA
                LDA     #RES_BLOCK
                STA     NB_WIN3
                JSR     R62
                PLA
                STA     NB_WIN3
                RTS

; A millisecond: the bridge's MSF acknowledged (its first write; the counter is the bridge's), the event area's
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
TICK:           LDA     EVON
                BNE     @ev
                LDA     #NB_TB_MSF | NB_TB_OVR | NB_TB_MSIE ; MSF and OVR cleared, MSIE kept: the
                STA     NB_TBCTL       ; acknowledge, the handler's first write
                BRA     @hook          ; (no area: the counter is the bridge's, nothing to do)
@ev:            BIT     NB_TBCTL       ; OVR in V: ticks ran together, perhaps across a carry
                LDA     #NB_TB_MSF | NB_TB_OVR | NB_TB_MSIE
                STA     NB_TBCTL
                BVS     @all
                LDA     EVDIRTY
                BNE     @all
                LDX     NB_WIN1
                LDA     EVBLK
                STA     NB_WIN1
                LDY     #EV_MS
                LDA     NB_MS0         ; the area's low byte from the bridge's (a load: it latches)
                STA     (EVP),Y
                STX     NB_WIN1
                BEQ     @all           ; 0: the low byte went round, the rest with it
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
@in:            BIT     VIAL           ; a layout-2 area: anything to do? (the sticks: a VIA's ports)
                BPL     @frm
                LDA     VIA_ORA_NH
                EOR     STKA
                AND     #$1F
                BNE     @go
                LDA     VIA_ORB        ; (reading port B clears CB1's and CB2's flags)
                EOR     STKB
                AND     #$1F
                BNE     @go
@frm:           LDA     FRAMES
                EOR     PADFRM
                BNE     @go
                LDA     NB_MS0         ; MS0 - PADDUE >= 0: due (L2TICK checks a PTRIG went)
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
                LDA     HK + HK_W1H,Y  ; its WIN1H (B7): the dispatcher's is 0, so only if it isn't
                BEQ     @w1h
                STA     NB_WIN1H
@w1h:
.ifdef OOLIB_DEBUG
                LDX     #R61_HKON      ; its start counted (HKON, block 61's): Y kept
                JSR     CALL61
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
                LDX     #R61_HKCOST    ; its cycles counted (HOOKCOST, block 61's): Y kept
                JSR     CALL61
.endif
                LDA     NB_WIN1H       ; WIN1H 0 again (B7), whatever the hook left there (a
                BEQ     @w1h           ; frame hook may map W1 itself): the register's, not
                STZ     NB_WIN1H       ; the record's
@w1h:           PLA
                STA     NB_WIN3
                PLA
                STA     NB_WIN2
                PLA
                STA     NB_WIN1
                RTS

; Video: a vertical blank counted, the event area written (and, while
; VBON, the vertical-blank copies and the pointer: L4), the frame hook
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
                LDA     VBON           ; the vertical-blank copies and the pointer (video.s's
                BEQ     @snd           ; VBWORK, block 61), first: they have the blank's time
                LDX     #R61_VB
                JSR     CALL61
@snd:           BIT     EVON           ; a layout-2 area and the stream claimed: the sound part
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

; The event area's counters, all eight bytes, through W1: the bridge's MS0-MS3
; upward from MS0 (its read latches the other three), then FRAMES. In a
; handler, or with interrupts off.
EVALL:          LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDY     #EV_MS
                LDX     #0
@ms:            LDA     NB_MS0,X       ; MS0 first
                STA     (EVP),Y
                INY
                INX
                CPX     #4
                BNE     @ms
                LDX     #0
@fr:            LDA     FRAMES,X
                STA     (EVP),Y
                INY
                INX
                CPX     #4
                BNE     @fr
                PLA
                STA     NB_WIN1
                STZ     EVDIRTY
                RTS
.assert EV_FRAMES = EV_MS + 4, error, "EVALL writes the counters as one run"

; CALL61, sound.s's R61 routine X in block 61 (the BIOS), W2 borrowed for it
; and put back byte for byte, as CALL62 borrows W3, the routine's A
; returned (X not kept) (L3's handlers: W1 is
; the event area's, never code). Here, after the tick's path, so the tick's
; branches stay where they were (test_oodos_input times the tick).
CALL61:         LDA     NB_WIN2
                PHA
                LDA     #BIOS_BLOCK
                STA     NB_WIN2
                JSR     R61
                TAX                    ; the routine's A kept (SNDIRQ's answer): X is free
                PLA
                STA     NB_WIN2
                TXA
                RTS


.ifdef OOLIB_DEBUG
; The debug build's counters, in block 61 (BIOS), reached by CALL61 (L3's
; step (b): out of common memory, which L3 needs; out of block 62 since the
; serial line's receive buffer, which needs its room there). Each keeps Y.
; Going through CALL61 adds about 48 cycles to each hook's measured cost
; (the switch back after HKON, and to HOOKCOST before its count): the tick
; hook of test_oodos_clock_debug measures 479, against 431 with neither.
.segment "BIOS"

; Hook Y's call: the bridge's MS0 and TBSUB from one latch (MS0 first), for
; HOOKCOST. Timer 1 is the program's: never touched. Interrupts are off.
HKON:           LDA     NB_MS0
                STA     HK + HK_M0,Y
                LDA     NB_TBSUBL
                STA     HK + HK_C0,Y
                LDA     NB_TBSUBH
                STA     HK + HK_C0 + 1,Y
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
                LDA     NB_MS0         ; one latch: MS0, then TBSUB
                SEC
                SBC     HK + HK_M0,Y
                STA     DBGC           ; the whole ticks between
                SEC                    ; now - C0: TBSUB counts up (modulo 2^16 with the ticks added)
                LDA     NB_TBSUBL
                SBC     HK + HK_C0,Y
                TAX
                LDA     NB_TBSUBH
                SBC     HK + HK_C0 + 1,Y
                LDY     DBGC
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
DMACTL:         .res    1              ; DEAFEN's: CONTROL as it found it, less SELECT

.segment "RES"

; Every interrupt enable that isn't OODOS's own off, and what is pending
; there cleared: a program's (the warm boot), or one no handler answers
; (the dispatcher, through W3). OODOS's own, the bridge's tick and vertical
; blank, stay on (TBCTL $E0 again, in case a program wrote it); the VIA's are
; all off and ACR is $00, as /RESET leaves it, so a program's timer modes and
; PB7 end with it (timer 1 is the program's, polled: its latch isn't
; OODOS's to compare or write). A DMA
; channel's terminal count is cleared only if its interrupt was on: the
; others' are their drivers' (SDIO reads channel 0's), and an interrupt
; can come while one waits. The channels are reached through the window,
; SELECT 7 down to 0, so CONTROL is saved first and put back after, SELECT
; with it (the SELECT rule: the dispatcher's DEAFEN may come between the
; code it interrupted and that code's channel registers). Channels 4-7
; are live since B3 (in B2 their window read $FF, so their MODE's bit 7
; put 1s in DMAIRQ's bits 4-7); now, like 0-3, a channel 4-7's interrupt
; bit is cleared and its count cleared only if that interrupt was on.
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
                CMP     #DMA_ID_V2
                BNE     @acia
                LDA     DMA_CONTROL
                PHA                    ; put back after
                AND     #<~DMA_SEL
                STA     DMACTL
                STZ     DMAIRQ
                LDX     #DMA_SEL7      ; channel 7 to 0, X its SELECT (2 x the channel)
@dma:           CPX     #DMA_SEL2      ; channel 2 while a stream buffer moves on it is L3's:
                BNE     @mode          ; its interrupt and its count left as they are
                LDA     C2ON
                BEQ     @mode
                ASL     DMAIRQ
                BRA     @next
@mode:          TXA
                ORA     DMACTL         ; the master and BURST as found
                STA     DMA_CONTROL    ; the window on channel X / 2
                LDA     DMA_MODE
                ASL     A              ; its interrupt bit (7) to carry,
                ROL     DMAIRQ         ; and on into the mask: channel 7's ends in bit 7
                LSR     A              ; the mode with its interrupt off
                STA     DMA_MODE
@next:          DEX
                DEX
                BPL     @dma
                LDA     DMAIRQ         ; none to come, and none left to interrupt
                STA     DMA_STATUS
                PLA
                STA     DMA_CONTROL    ; CONTROL as DEAFEN found it, SELECT with it
.assert DMA_MODE_IRQ = $80, error, "DEAFEN's mask: the interrupt bit is MODE's bit 7"
.assert DMA_SEL7 = 14 && DMA_SEL2 = 4 && DMA_SEL = $0E, error, "DEAFEN's X is SELECT's field, 2 x the channel"
@acia:          JSR     RXDEAF         ; the stand-in 6551's: OODOS's receiver interrupt on (off
                                       ; while the ring is full), its transmitter's off (serial.s)
                LDA     #NB_TB_MSF | NB_TB_OVR | NB_TB_MSIE
                STA     NB_TBCTL       ; the tick on (revision 3: NB3CHK)
                BIT     VIAL
                BPL     @video         ; no VIA: $C34E would be the 6551's command
                LDA     #<~VIA_IER_SET
                STA     VIA_IER        ; every VIA interrupt off
                STA     VIA_IFR
                STZ     VIA_ACR        ; as /RESET leaves it: timer 1 the program's
@video:         LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @done
                STA     VIDEO_IEN      ; vertical blank alone
                LDA     #VIDEO_IRQ_LINE | VIDEO_IRQ_COLLIDE
                STA     VIDEO_ISR
@done:          RTS

; OODOS's own interrupts off, for leaving it (Apple II mode, function 75):
; the clocks' (TBCTL $C0: the tick's MSIE off), the USB card's (a layout-2 area's), and the serial line's
; receiver (the 6551 isn't reset by /RESET; the monitor's own command, $0B,
; has it off too). Keeps X and Y.
CLKOFF:         STZ     USB_IEN
                STZ     SOUND_IEN      ; and the sound card's (L3's)
                LDA     #ACIA_CMD_RXOFF
                TSB     ACIA_CMD
                LDA     #NB_TB_MSF | NB_TB_OVR
                STA     NB_TBCTL       ; the tick off
                LDA     CLOCKS
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
                BPL     @tick
                LDA     #<~VIA_IER_SET
                STA     VIA_IER        ; all off, all flags cleared
                STA     VIA_IFR
                STZ     VIA_ACR        ; timer 1 the program's: not loaded
                LDA     #<~NB_IRQ_VIA
                STA     VIAL           ; VIASRV, and the sticks' ports, from here
@tick:          LDA     #NB_TB_MSF | NB_TB_OVR | NB_TB_MSIE
                STA     NB_TBCTL       ; the tick: north bridge revision 3's, MSIE on
                LDA     #CLK_MS
                TSB     CLOCKS         ; always: every bridge OODOS 0.16 runs on has the tick
@video:         LDA     VIDEO_ID       ; revision 2 only (hw/video-rev2.md): CLK_FRAME says
                CMP     #VIDEO_ID_V2   ; there is one, for function 83 too
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
.assert OOLIB_AREAS = OA_FRAME | OA_INPUT | OA_SOUND | OA_DMA | OA_SERIAL, error, "frame and timing, input, sound, DMA, and the serial line"
.assert CLK_FRAME = FC_FRAME && CLK_MS = FC_MS, error, "CLOCKS is FT_INFO's +3, EV_CLOCKS"

; The bridge's millisecond counter and FRAMES at one moment, into SNAP: with
; interrupts off, so no handler's read of MS0 takes a new latch between this
; read of MS0 and of MS3; MS0 first (its read latches MS1-MS3).
SNAPIT:         PHP
                SEI
                LDX     #0
@ms:            LDA     NB_MS0,X
                STA     SNAP,X
                INX
                CPX     #4
                BNE     @ms
@fr:            LDA     FRAMES - 4,X
                STA     SNAP,X
                INX
                CPX     #8
                BNE     @fr
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
                CPX     #1             ; W1 while the program's WIN1H isn't 0 (B7): not a block of
                BNE     @win           ; banks 0-5
                LDA     ST_W1H
                BNE     @no0
@win:           LDA     ST_WIN,X
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

; 78: the random source (interface 17; hw/future/random-source.md). X = 0: A
; is the operation; else A/X a block, its +0 the operation. RN_INFO: A the
; RS_* bits (0: no chip), X 0. RN_FILL (a block): +1 n (1-255), n bytes of the
; chip's to +2 on; A = X = 0, or $FF $FF refused (n 0, no source, the chip
; failed twice: the bytes at +2 then undefined). Waits on the bridge (its
; driver, bios.s's RNGBYTE) with interrupts on.
F_RANDOM:       LDX     PX
                BEQ     @op
                JSR     PGET           ; the block's +0; PP then at +1
@op:            CMP     #RN_INFO
                BNE     @fill
                JSR     RNGOK
                LDX     #0
                RTS
@fill:          CMP     #RN_FILL
                BNE     @no
                LDX     PX
                BEQ     @no            ; RN_FILL takes a block
                JSR     PGET           ; n
                CMP     #0
                BEQ     @no
                STA     T0
                JSR     RNGOK
                BCS     @no            ; no source
                JSR     RNGNEW
@byte:          JSR     RNGBYTE
                BCS     @no
                JSR     PPUT
                DEC     T0
                BNE     @byte
                LDA     #0
                TAX
                RTS
@no:            JMP     RETFF

; +1-2 milliseconds from now, by the bridge's counter (it can't stop: N10).
FT_DELAYX:      LDX     PX
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
@wait:          JSR     SNAPIT
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

; Hook Y set: T0/T1 its address less 1, the windows the program has now,
; WIN1H with them.
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
                LDA     ST_W1H         ; and its WIN1H (B7)
                STA     HK + HK_W1H,Y
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
                LDA     #1             ; counted (by the bridge's TBSUB: always)
.else
                LDA     #0
.endif
                LDX     #0
                RTS
@no:            JMP     RETFF

; The warm boot's in place of DEAFEN (input.s's WBQUIET; B5): the DMA
; controller's channels 4-7 stopped and every CARD put back (video.s's
; DMAWARM, in block 61, through W2 as CALL61 maps it, without a CALL61 entry
; so that nothing in block 61 moves), then DEAFEN, then the serial line's
; ring emptied (serial.s's RXFLUSH: what a sender still streaming sent the
; program isn't the CCP's). In RES2, so that nothing in RES moves:
; WBQUIET's JSR is the same length.
.segment "RES2"
WBDMA:          LDA     NB_WIN2
                PHA
                LDA     #BIOS_BLOCK
                STA     NB_WIN2
                JSR     DMAWARM
                PLA
                STA     NB_WIN2
                JSR     DEAFEN
                JMP     RXFLUSH

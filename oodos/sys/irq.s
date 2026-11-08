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
;                      counter, the event area's copy, the tick hook
;   video (ID 9)       vertical blank: the frame counter, the event area,
;                      the frame hook
;   anything else      a card interrupting that OODOS didn't turn on (a
;                      program broke the rules): DEAFEN turns every such
;                      enable off, so it can't hold /IRQ low for ever
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
; hook runs with interrupts off. The frame hook runs with them on, so a
; tick isn't lost while it runs (its budget, 8,000 cycles, is more than a
; tick); a vertical blank while it still runs is counted, but the hook
; isn't called again for it (FHSKIP counts those). Neither may call the
; BDOS, use DMA, far call, or write the VIA's ACR, IER or IFR. Budgets:
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
.importzp PA, PX, PP, T0, T1, T2, T3, T4 ; the BDOS's (bdos.inc), for 80
.import RETFF, PGET, PPUT
.export IRQ, DEAFEN, CLKOFF, CLKINIT, F_FRAME
.export CLOCKS, MS, MSACC, EVDIRTY, EVON, FHON, THON, FHBUSY, FHSKIP
.ifdef OOLIB_DEBUG
.export FHMAX, THMAX
.endif

.zeropage
EVP:            .res    2              ; the event area, in W1's view ($4000-$7FFF)

; A hook's record in HK
HK_ON           = 0                    ; 1: set
HK_ADDR         = 1                    ; its address less 1, for RTS
HK_WIN          = 3                    ; its windows, W0-W3
HK_S            = 7                    ; the dispatcher's S while it runs
HK_C0           = 8                    ; debug: timer 1's count at its call,
HK_M0           = 10                   ;   and MS's low byte
HK_MAX          = 11                   ;   the most cycles it took, since the warm boot
HK_LEN          = 16
HF              = 0                    ; the frame hook's
HT              = HK_LEN               ; the tick hook's

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
HK:             .res    2 * HK_LEN     ; the hooks: the frame hook's at HF, the tick hook's at HT
SNAP:           .res    8              ; MS and FRAMES at one moment, for function 80

PROG_LIMIT      = 24                   ; blocks below: banks 0-5, the event area's
.ifdef OOLIB_DEBUG
DBGC:           .res    2              ; HOOKCOST's
DBGR:           .res    1
.endif

FHON            = HK + HF + HK_ON
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
                CMP     #<~NB_IRQ_VIA
                BNE     @more          ; not the VIA alone
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
                JSR     VIASRV
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
@deaf:          JSR     DEAF62
@out:           PLY
                PLA
                STA     NB_WIN0        ; the interrupted stack page, under the same S
                PLX
                PLA
                RTI

; The VIA: timer 1 the tick; any other flag enabled (OODOS enables none)
; turned off. Nothing at all (the stand-in 6551, a program's, on the same
; ID): DEAFEN.
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

; DEAFEN, in block 62's code window, whatever W3 has.
DEAF62:         LDA     NB_WIN3
                PHA
                LDA     #RES_BLOCK
                STA     NB_WIN3
                JSR     DEAFEN
                PLA
                STA     NB_WIN3
                RTS

; A millisecond: timer 1's flag cleared, the counter on, the event area's
; copy (its low byte, or all of it after a carry or a catch-up), the tick
; hook if it is due.
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
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDY     #EV_MS
                LDA     MS
                STA     (EVP),Y
                PLA
                STA     NB_WIN1
                BRA     @hook
@all:           JSR     EVALL
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
                JSR     T1NOW
                STA     HK + HK_C0,Y
                TXA
                STA     HK + HK_C0 + 1,Y
                LDA     MS
                STA     HK + HK_M0,Y
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
                JSR     HOOKCOST
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

.ifdef OOLIB_DEBUG
; Timer 1's count now in A (low) and X (high); a tick that came due
; meanwhile counted first, since reading the count clears its flag (its
; hook, if due, waits for the next).
T1NOW:          LDA     VIA_IFR
                AND     #VIA_IRQ_T1
                BEQ     @read
                INC     MS
                BNE     @d
                INC     MS + 1
                BNE     @d
                INC     MS + 2
                BNE     @d
                INC     MS + 3
@d:             LDA     #1
                STA     EVDIRTY
@read:          LDX     VIA_T1CH       ; the high byte the same either side of the low
                LDA     VIA_T1CL
                CPX     VIA_T1CH
                BNE     @read
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

.segment "RES"

; Every interrupt enable that isn't OODOS's own off, and what is pending
; there cleared: a program's (the warm boot), or one no handler answers
; (the dispatcher, through W3). OODOS's own, timer 1 and vertical blank, stay on.
DEAFEN:         LDA     #NB_RTC_TICK   ; the clock: TICK cleared, TIE off (SET 0 sends nothing)
                STA     NB_RTCCTL
                STZ     SD_IEN
                STZ     USB_IEN
                STZ     SOUND_IEN
                STZ     CONSOLE_IEN
                LDA     #1             ; a front-panel key pending pulls /IRQ whatever IEN says
                STA     CONSOLE_ISR
                LDA     DMA_ID
                CMP     #DMA_ID_V1
                BNE     @acia
                LDA     #DMA_ALL       ; no terminal count left to interrupt, nor one to come
                STA     DMA_STATUS
                LDX     #$30
@dma:           LDA     DMA0_MODE,X
                AND     #<~DMA_MODE_IRQ
                STA     DMA0_MODE,X
                TXA
                SEC
                SBC     #$10
                TAX
                BCS     @dma
@acia:          LDA     ACIA_STATUS    ; the stand-in 6551's, should a program have turned it on
                LDA     CLOCKS
                AND     #CLK_MS
                BEQ     @video         ; no VIA: $C34E would be the 6551's command
                LDA     #VIA_IRQ_OTHERS
                STA     VIA_IER        ; all but timer 1 off
                STA     VIA_IFR
@video:         LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @done
                STA     VIDEO_IEN      ; vertical blank alone
                LDA     #VIDEO_IRQ_LINE | VIDEO_IRQ_COLLIDE
                STA     VIDEO_ISR
@done:          RTS

; OODOS's own interrupts off, for leaving it (Apple II mode, function 75).
CLKOFF:         LDA     CLOCKS
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
; layout (+4).
FT_INFOX:       LDX     PX
                BEQ     @ret
                LDA     #OOLIB_IFACE
                JSR     PPUT
                LDA     #OOLIB_AREAS
                JSR     PPUT
                LDA     CLOCKS
                JSR     PPUT
                LDA     #EV_LAYOUT1
                JSR     PPUT
@ret:           LDA     #OOLIB_IFACE
                LDX     #OOLIB_AREAS
                RTS
.assert OOLIB_AREAS = OA_FRAME, error, "L1 has frame and timing alone"
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

; The event area: +1-2 its address, +3 its size. In one window, W0-W2, on a
; block of banks 0-5 (the program's and the memory functions'; not flash,
; read-only, the system's, M: or video memory); EV_LEN bytes
; at least. Filled from scratch (layout 1, the counters, the rest 0), then
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
                CMP     #EV_LEN
                BCC     @no0
                CLC                    ; T3/T4 its last byte
                LDA     T0
                ADC     T2
                STA     T3
                LDA     T1
                ADC     #0
                STA     T4
                LDA     T3
                BNE     @dec
                DEC     T4
@dec:           DEC     T3
                LDA     T1             ; the same window, and not W3
                EOR     T4
                AND     #$C0
                BNE     @no
                LDA     T1
                CMP     #$C0
                BCS     @no
                ROL     A
                ROL     A
                ROL     A
                AND     #3
                TAX
                LDA     ST_WIN,X
                CMP     #PROG_LIMIT    ; banks 0-5, RAM: not the system's, M:, video memory or flash
                BCS     @no
                STZ     EVON           ; any area before closed
                STA     EVBLK
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
                BNE     @1
                LDA     #EV_LAYOUT1
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
                STA     EVON
                LDA     #0
                TAX
                RTS
@no:            JMP     RETFF

FT_CLOSEX:      STZ     EVON
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

; +1-2 milliseconds from now. No VIA: A = X = $FF at once.
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
                LDA     #1
.else
                LDA     #0
.endif
                LDX     #0
                RTS
@no:            JMP     RETFF

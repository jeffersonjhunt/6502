; OODOS's input (hw/oolib.md, "The plan", L2): the event area's layout 2,
; the USB card's events, the DE-9 sticks, the game port, the mouse, and
; function 81.
;
; Everything here but function 81 is in block 62's code window (RES),
; reached from irq.s's dispatcher by CALL62 (W3 switched to block 62 and
; back), so common memory holds only the tick's inline checks and the
; switch. It runs as the dispatcher does: W0 on the system block (this
; file's state is there), interrupts masked, A, X and Y free. The event
; area is written through W1, put on its block (EVBLK) for each handler
; and put back after; EVP is the area in W1's view.
;
; Layout 2 (oodos.inc's EV_*): an area FT_OPEN was given EV_LEN2 bytes or
; more for. While one is open (EVON is EVON_L2):
;
;   the USB card    its interrupt on (IEN: KEY, MOUSE, GAME, DEVICE): USBSRV
;                   drains the event FIFO (UBATCH events an interrupt at
;                   most), keeps the keys-held map and the modifiers, and
;                   queues each key; puts the mouse's position and buttons
;                   in the area, queueing button and wheel changes (moves
;                   aren't queued: the position is always current); the
;                   game port's buttons; a device plugged in or out
;   the sticks      sampled at each tick (irq.s compares the ports inline,
;                   L2TICK when one moved): a controller event for a change
;   the paddles     read at the open; then PTRIG at the first tick of each
;                   frame, and read at the first tick PAD_MS ms or more
;                   after it, once for each PTRIG (PADARM) (L2TICK): the
;                   paddle bytes, and controller 3's X, Y and directions
;                   (GP_LOW, GP_HIGH). It needs the video card's frames (and
;                   the VIA's ticks): without them, the open's read alone
;
; The queue is a ring of EV_QSLOTS slots of EQ_LEN bytes from EV_QUEUE:
; OODOS writes the slot at the head and then moves the head (QHEAD is its
; own copy: the area's is for the program to read); the program reads at
; the tail and moves it. Head = tail is empty, so one slot always stays
; empty; an event that would fill it is counted in EV_QLOST instead, whole
; or not at all.
;
; The controllers (question 12): CMAP maps each of the four to a piece of
; hardware (CH_*), as cold boot and each warm boot set it (1-4 in order);
; function 81 swaps them. The hardware's own state is HWB, HWX and HWY, by
; CH_* - 1.
;
; Function 81, input, in the BDOS: X = 0, A the operation; else A/X a block,
; +0 the operation (oodos.inc's IN_*).

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.importzp PX, T0, T1, T2, T3, T4      ; the BDOS's (bdos.inc), for 81
.importzp EVP                          ; irq.s's
.import RETFF, PGET, PPUT
.import DEAFEN, EVON, EVBLK, MS, FRAMES, CLOCKS, SNAP, STKA, STKB, PADFRM, PADDUE, UCARD
.ifdef OOLIB_DEBUG
.import HKON, HOOKCOST, FHMAX, THMAX  ; irq.s's (a debug build)
.endif
.import CALL61, SIEN, C2ON          ; irq.s's, sound.s's
.export R62, QSLOTS, L2OPEN, CMAPDEF, WBQUIET, F_INPUT, QEV, EVK, EVC, EVV, MXS, MYS

.segment "SYSBSS"
QSLOTS:         .res    1              ; the queue's slots (FT_OPEN's; 0 in layout 1)
QHEAD:          .res    1              ; the slot OODOS writes next: the area's EV_QHEAD is its copy
QNEXT:          .res    1              ; QEV's: the head after this event
MODS:           .res    1              ; the modifiers held, kept from the key events
MBTN:           .res    1              ; the mouse's buttons, as last read
MXS:            .res    2              ; the mouse's X and Y as the area has them (MPOS's copy
MYS:            .res    2              ;   follows USB_MX's order: X, then Y)
HWB:            .res    4              ; each piece of hardware's CB_* bits, CH_STICK1's first,
HWX:            .res    4              ;   its X,
HWY:            .res    4              ;   its Y
CMAP:           .res    4              ; each controller's hardware (CH_*), controller 1's first
EVK:            .res    1              ; the event for QEV: its kind, code and value
EVC:            .res    1
EVV:            .res    1
UCNT:           .res    1              ; UKEYS's batch
UTMP:           .res    1
UHW:            .res    1              ; CTLPUT's hardware
UCHG:           .res    1              ; UMOUSE's buttons changed
PADARM:         .res    1              ; 1: PTRIG sent, the paddles not yet read
ISRN:           .res    1              ; USBSRV's flags
IRQY:           .res    1              ; IRQOTH's: IRQH's bits that aren't strays

.segment "RES"

; irq.s's CALL62: routine X (sys.inc's R62_*).
R62:            JMP     (R62TAB,X)
R62TAB:         .word   DEAFEN, IRQOTH, L2TICK
.ifdef OOLIB_DEBUG
                .word   HKON, HOOKCOST ; irq.s's hook counters (a debug build)
.assert R62_HKON = 6 && R62_HKCOST = 8, error, "R62TAB's debug entries are sys.inc's R62_HKON, R62_HKCOST"
.endif
.assert R62_DEAF = 0 && R62_OTHER = 2 && R62_TICK = 4, error, "R62TAB is in sys.inc's R62_* order"

BITS:           .byte   $01, $02, $04, $08, $10, $20, $40, $80

; The warm boot's (common.s's WARM): every interrupt enable a program set
; off (DEAFEN: the USB card's too), then the USB card's mouse (relative, the
; limits at /RESET's), its LEDs (off), its repeat (/RESET's), Pause on /NMI,
; and the controllers as question 12 numbers them. A debug build's hook
; costs (FT_COSTS) start again from 0 here. Cold boot's warm boot
; sets them first, so these are what cold boot set.
WBQUIET:
.ifdef OOLIB_DEBUG
                STZ     FHMAX          ; the hooks' most cycles counted afresh (a debug build)
                STZ     FHMAX + 1
                STZ     THMAX
                STZ     THMAX + 1
.endif
                LDX     #R61_QUIET     ; the sound card quiet, channel 2 stopped, the claims
                JSR     CALL61         ; released (sound.s's SNDQUIET), before DEAFEN
                JSR     DEAFEN
                LDX     #R61_WARM      ; the vertical-blank slots freed, the pointer off, the
                JSR     CALL61         ; console's screen shown again (video.s's VBQUIET, L4)
                LDA     UCARD
                BEQ     CMAPDEF
                STZ     USB_MCTRL
                LDA     #<USB_MAX_DEF_X
                STA     USB_MAXX
                LDA     #>USB_MAX_DEF_X
                STA     USB_MAXX + 1
                LDA     #<USB_MAX_DEF_Y
                STA     USB_MAXY
                LDA     #>USB_MAX_DEF_Y
                STA     USB_MAXY + 1
                STZ     USB_LEDS
                LDA     #USB_REPEAT_DEF
                STA     USB_REPEAT
                LDA     #USB_NMI_PAUSE
                STA     USB_KBCFG
CMAPDEF:        LDX     #3             ; controller n is hardware n
@map:           TXA
                INC     A
                STA     CMAP,X
                DEX
                BPL     @map
                RTS

; A card's interrupt that isn't the VIA's or the video card's (irq.s's
; dispatcher): the USB card's, with a layout-2 area open, answered; L3's
; (channel 2's terminal count, the sound card's mailbox and ES5503) while it
; has any on, through CALL61 (sound.s's SNDIRQ, which says which it
; answered); then
; any other card still interrupting, which OODOS didn't turn on, DEAFENed,
; and the USB card's events on again if the area wants them. After USBSRV
; the USB card pulling again is a new event's, which the next interrupt
; takes: not a stray.
IRQOTH:         STZ     IRQY
                BIT     EVON
                BPL     @rest
                LDA     NB_IRQH
                AND     #NB_IRQ_USB
                BNE     @rest
                JSR     USBSRV
                LDA     #NB_IRQ_USB
                STA     IRQY
@rest:          LDA     SIEN           ; L3's: the stream's channel 2, the card's interrupts
                ORA     C2ON
                BEQ     @other
                LDX     #R61_IRQ
                JSR     CALL61
                TSB     IRQY           ; what it answered isn't a stray
@other:         LDA     NB_IRQL
                ORA     #NB_IRQ_VIA
                INC     A
                BNE     @deaf
                LDA     NB_IRQH
                ORA     #NB_IRQ_VIDEO
                ORA     IRQY
                INC     A
                BEQ     @done
@deaf:          JSR     DEAFEN
                BIT     EVON
                BPL     @done
                LDA     UCARD
                BEQ     @done
                LDA     #USB_IRQ_ALL
                STA     USB_IEN
@done:          RTS

; The USB card's interrupt, a layout-2 area open: each flag cleared before
; what it flags is read (a change after it is another interrupt), then the
; device, the mouse, the game port's buttons and the keys.
USBSRV:         LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDA     USB_ISR
                AND     #USB_IRQ_MOUSE | USB_IRQ_GAME | USB_IRQ_DEVICE
                STA     USB_ISR
                STA     ISRN
                AND     #USB_IRQ_DEVICE
                BEQ     @mouse
                JSR     UDEV
@mouse:         LDA     ISRN
                AND     #USB_IRQ_MOUSE
                BEQ     @game
                JSR     UMOUSE
@game:          LDA     ISRN
                AND     #USB_IRQ_GAME
                BEQ     @keys
                JSR     GPBITS
                JSR     CTLHW
@keys:          JSR     UKEYS
                PLA
                STA     NB_WIN1
                RTS

; The event FIFO's events, UBATCH at most (a pass of 16 measures 3,000 to
; 3,400 cycles, about 5,000 when a tick shares it: under a tick's 7,159, so
; no tick comes behind another and is lost). Its flag is cleared only when the
; FIFO is seen empty, and the FIFO looked at again after: so while events
; wait the flag stays set, and /IRQ with it, and the next interrupt takes
; the rest (a tick due meanwhile first, in the same pass).
UKEYS:          LDA     #UBATCH
                STA     UCNT
@look:          LDA     USB_EVCOUNT
                CMP     #2
                BCS     @take
                LDA     #USB_IRQ_KEY
                STA     USB_ISR
                LDA     USB_EVCOUNT
                CMP     #2
                BCC     @done
@take:          DEC     UCNT
                BMI     @done
                LDA     USB_EVENT      ; the usage
                STA     EVC
                LDA     USB_EVENT      ; its flags
                JSR     KEYEV
                BRA     @look
@done:          RTS
.assert UBATCH >= 1 && UBATCH <= 128, error, "UKEYS counts its batch down to BMI: 128 ($80) at most"

; A key event: EVC the usage, A its flags. The keys-held map, and the
; modifiers if it is one ($E0-$E7 only), kept first, so the event carries the
; modifiers with it.
KEYEV:          ASL     A              ; USB_RELEASE to carry
                LDA     #0
                ROL     A
                EOR     #1             ; 1 pressed, 0 released
                STA     EVV
                LDA     EVC
                AND     #7
                TAX
                LDA     EVC
                LSR     A
                LSR     A
                LSR     A
                ORA     #EV_KEYS
                TAY
                LDA     BITS,X
                LDX     EVV
                BEQ     @up
                ORA     (EVP),Y
                BRA     @held
@up:            EOR     #$FF
                AND     (EVP),Y
@held:          STA     (EVP),Y
                LDA     EVC
                CMP     #$E0
                BCC     @queue
                CMP     #$E8           ; $E8-$FF: keys, not modifiers
                BCS     @queue
                AND     #7
                TAX
                LDA     BITS,X
                LDX     EVV
                BEQ     @mup
                TSB     MODS
                BRA     @mods
@mup:           TRB     MODS
@mods:          LDA     MODS
                LDY     #EV_MODS
                STA     (EVP),Y
@queue:         LDA     #EK_KEY
                STA     EVK
                JMP     QEV
.assert USB_RELEASE = $80, error, "KEYEV takes the release from bit 7"
.assert EV_KEYS = $40, error, "KEYEV ORs the byte's index in: 32 bytes from $40"

; A device plugged in or out: the area's EV_DEVICES, and an event.
UDEV:           LDA     USB_DEVICES
                LDY     #EV_DEVICES
                STA     (EVP),Y
                STA     EVV
                STZ     EVC
                LDA     #EK_DEVICE
                STA     EVK
                JMP     QEV

; The mouse: X and Y (absolute: the position; relative: the movement
; added in), the wheel's steps added to the area's and queued, and each
; button that changed queued.
UMOUSE:         LDA     USB_MX         ; MX, MY and the wheel latched (once: a second read
                STA     UTMP           ; would latch again, the wheel's steps and relative
                LDA     USB_MCTRL      ; mode's sums gone)
                AND     #USB_MOUSE_ABS
                BEQ     @rel
                LDA     UTMP
                JSR     MABS
                BRA     @wheel
@rel:           CLC
                LDA     MXS
                ADC     UTMP
                STA     MXS
                LDA     MXS + 1
                ADC     USB_MX + 1
                STA     MXS + 1
                CLC
                LDA     MYS
                ADC     USB_MY
                STA     MYS
                LDA     MYS + 1
                ADC     USB_MY + 1
                STA     MYS + 1
                JSR     MPUT
@wheel:         LDA     USB_MWHEEL
                BEQ     @buttons
                STA     EVV
                CLC
                LDY     #EV_MWHEEL
                ADC     (EVP),Y
                STA     (EVP),Y
                STZ     EVC
                LDA     #EK_WHEEL
                STA     EVK
                JSR     QEV
@buttons:       LDA     USB_MBTN
                AND     #$1F
                TAX
                EOR     MBTN
                BEQ     @done
                STA     UCHG
                STX     MBTN
                TXA
                LDY     #EV_MBUTTONS
                STA     (EVP),Y
                LDX     #0             ; each that changed, button 0 first
@bit:           LSR     UCHG
                BCC     @next
                STX     EVC
                LDA     MBTN
                AND     BITS,X
                BEQ     @v
                LDA     #1
@v:             STA     EVV
                LDA     #EK_BUTTON
                STA     EVK
                PHX
                JSR     QEV
                PLX
@next:          INX
                LDA     UCHG
                BNE     @bit
@done:          RTS

; Absolute mode's position to MXS and MYS, and the area: A is MX's low
; byte, as the read that latched them gave it.
MABS:           STA     MXS
                LDA     USB_MX + 1
                STA     MXS + 1
                LDA     USB_MY
                STA     MYS
                LDA     USB_MY + 1
                STA     MYS + 1
                ; fall through
; MXS and MYS to the area's EV_MX and EV_MY.
MPUT:           LDY     #EV_MX
                LDX     #0
@copy:          LDA     MXS,X
                STA     (EVP),Y
                INY
                INX
                CPX     #4
                BNE     @copy
                RTS
.assert MYS = MXS + 2 && EV_MY = EV_MX + 2, error, "MPUT copies X and Y as one"

; The mouse's X and Y afresh (the open, function 81's mode): the latch
; read, so relative mode's sums start again, and absolute mode's position,
; or relative mode's 0.
MPOS:           LDA     USB_MX
                STA     UTMP
                LDA     USB_MCTRL
                AND     #USB_MOUSE_ABS
                BEQ     @rel
                LDA     UTMP
                BRA     MABS
@rel:           STZ     MXS
                STZ     MXS + 1
                STZ     MYS
                STZ     MYS + 1
                BRA     MPUT

; A tick's (irq.s's TICK, through CALL62) with a layout-2 area open and a
; stick moved, a frame begun, or the paddles due: each stick that moved, a
; controller event; at a frame's first tick, PTRIG; PAD_MS ms after it (or
; at the first tick past, should MS have jumped), the paddles read. Without
; the video card's frames there is no PTRIG, so no read.
L2TICK:         LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDA     VIA_ORA_NH     ; port A, no handshake: stick 1
                AND     #$1F
                CMP     STKA
                BEQ     @b
                STA     STKA
                LDX     #CH_STICK1 - 1
                JSR     STICK
@b:             LDA     VIA_ORB        ; port B: stick 2
                AND     #$1F
                CMP     STKB
                BEQ     @frame
                STA     STKB
                LDX     #CH_STICK2 - 1
                JSR     STICK
@frame:         LDA     FRAMES         ; (moves only with a video card: CLK_FRAME)
                CMP     PADFRM
                BEQ     @due
                STA     PADFRM
                STA     USB_PTRIG      ; (no USB card: nothing there to write)
                LDA     #1
                STA     PADARM
                LDA     MS
                CLC
                ADC     #PAD_MS
                STA     PADDUE
                BRA     @out
.assert PAD_MS >= 1 && PAD_MS < 128, error, "the paddles' due test is MS - PADDUE >= 0, signed"
@due:           LDA     MS             ; due once MS has reached PADDUE, by any step (the Disk
                SEC                    ; II's catch-up moves it on more than one at a tick)
                SBC     PADDUE
                BMI     @out
                LDA     MS             ; not due again for 128 ms (a new PTRIG comes first)
                EOR     #$80
                STA     PADDUE
                LDA     PADARM         ; read only what a PTRIG started: none without frames
                BEQ     @out
                STZ     PADARM
                JSR     PADS
@out:           PLA
                STA     NB_WIN1
                RTS

; Stick X (CH_* - 1) as its port reads, A (bits 0-4, 0 pressed): its
; controller, and an event if its bits changed.
STICK:          JSR     STKBITS
                ; fall through
; Hardware X's new bits A (its HWX and HWY set already): its controller
; written, and an event if the bits changed.
CTLHW:          CMP     HWB,X
                BEQ     CTLPUT
                STA     HWB,X
                JSR     CTLPUT
                BCS     @done
                LDA     HWB,X
                STA     EVV
                LDA     #EK_CTL
                STA     EVK
                JMP     QEV
@done:          RTS

; Hardware X's controller in the area: its bits, X, Y and which hardware it
; is; EVC its number (1-4). Keeps X. Carry set if no controller has it.
CTLPUT:         STX     UHW
                INX
                TXA
                LDX     #3
@find:          CMP     CMAP,X
                BEQ     @got
                DEX
                BPL     @find
                LDX     UHW
                SEC
                RTS
@got:           TXA
                INC     A
                STA     EVC
                DEC     A
                ASL     A
                ASL     A
                ADC     #EV_CTL        ; carry clear: the ASLs of 0-3
                TAY
                LDX     UHW
                LDA     HWB,X
                STA     (EVP),Y
                INY
                LDA     HWX,X
                STA     (EVP),Y
                INY
                LDA     HWY,X
                STA     (EVP),Y
                INY
                TXA
                INC     A
                STA     (EVP),Y
                CLC
                RTS
.assert EC_BITS = 0 && EC_X = 1 && EC_Y = 2 && EC_HW = 3 && EC_LEN = 4, error, "CTLPUT writes a controller in EC_* order"

; A DE-9's port bits A (0 pressed) as CB_* bits in A; its X and Y (0, 128,
; 255) in HWX and HWY, X.
STKBITS:        EOR     #$1F
                STA     UTMP
                LDY     #128
                AND     #CB_LEFT
                BEQ     @right
                LDY     #0
                BRA     @x
@right:         LDA     UTMP
                AND     #CB_RIGHT
                BEQ     @x
                LDY     #255
@x:             TYA
                STA     HWX,X
                LDY     #128
                LDA     UTMP
                AND     #CB_UP
                BEQ     @down
                LDY     #0
                BRA     @y
@down:          LDA     UTMP
                AND     #CB_DOWN
                BEQ     @y
                LDY     #255
@y:             TYA
                STA     HWY,X
                LDA     UTMP
                RTS
.assert CB_UP = $01 && CB_DOWN = $02 && CB_LEFT = $04 && CB_RIGHT = $08 && CB_FIRE = $10, error, "a DE-9's lines are CB_*'s, inverted"

; The game port's bits in A, X its hardware (CH_GAMEPORT - 1): its three
; buttons as fire bits 4-6, and the directions its X and Y (paddles 0 and
; 1, as last read) give: below GP_LOW up or left, GP_HIGH or more down or
; right.
GPBITS:         LDA     USB_GAME
                AND     #$70
                STA     UTMP
                LDA     HWX + CH_GAMEPORT - 1
                CMP     #GP_LOW
                BCS     @r
                LDA     #CB_LEFT
                BRA     @xs
@r:             CMP     #GP_HIGH
                BCC     @y
                LDA     #CB_RIGHT
@xs:            TSB     UTMP
@y:             LDA     HWY + CH_GAMEPORT - 1
                CMP     #GP_LOW
                BCS     @d
                LDA     #CB_UP
                BRA     @ys
@d:             CMP     #GP_HIGH
                BCC     @done
                LDA     #CB_DOWN
@ys:            TSB     UTMP
@done:          LDA     UTMP
                LDX     #CH_GAMEPORT - 1
                RTS

; The paddles read (a USB card): the area's four, and the game port's
; controller.
PADS:           LDA     UCARD
                BEQ     @no
                JSR     PADREAD
                JMP     CTLHW
@no:            RTS
; The four paddles to the area, 0 and 1 to the game port's X and Y; its
; bits in A, X its hardware (GPBITS).
PADREAD:        LDY     #EV_PADDLES
                LDX     #0
@pad:           LDA     USB_PADDLE0,X
                STA     (EVP),Y
                INY
                INX
                CPX     #4
                BNE     @pad
                LDA     USB_PADDLE0
                STA     HWX + CH_GAMEPORT - 1
                LDA     USB_PADDLE1
                STA     HWY + CH_GAMEPORT - 1
                BRA     GPBITS

; An event into the queue: EVK, EVC, EVV, the modifiers and the ms counter
; in the head's slot, then the head moved. A full queue (the next head the
; program's tail) counts it lost instead.
QEV:            LDX     QHEAD
                INX
                CPX     QSLOTS
                BCC     @next
                LDX     #0
@next:          STX     QNEXT
                TXA
                LDY     #EV_QTAIL
                CMP     (EVP),Y
                BEQ     @full
                LDA     QHEAD
                ASL     A
                ASL     A
                ASL     A
                ORA     #EV_QUEUE
                TAY
                LDA     EVK
                STA     (EVP),Y
                INY
                LDA     EVC
                STA     (EVP),Y
                INY
                LDA     EVV
                STA     (EVP),Y
                INY
                LDA     MODS
                STA     (EVP),Y
                INY
                LDX     #0
@ms:            LDA     MS,X
                STA     (EVP),Y
                INY
                INX
                CPX     #4
                BNE     @ms
                LDA     QNEXT
                STA     QHEAD
                LDY     #EV_QHEAD
                STA     (EVP),Y
                RTS
@full:          LDY     #EV_QLOST
                LDA     (EVP),Y
                INC     A
                STA     (EVP),Y
                BNE     @done
                INY
                LDA     (EVP),Y
                INC     A
                STA     (EVP),Y
@done:          RTS
.assert EQ_KIND = 0 && EQ_CODE = 1 && EQ_VALUE = 2 && EQ_MODS = 3 && EQ_MS = 4 && EQ_LEN = 8, error, "QEV writes an event in EQ_* order"
.assert EV_QUEUE = $80, error, "QEV ORs the slot's offset in"

; FT_OPEN's, for a layout-2 area (filled with its layout, counters and
; slots, the rest 0): what the input is now, without events; the USB
; card's FIFO emptied (what was typed before the open isn't the program's)
; and its interrupt on; EVON set.
L2OPEN:         PHP
                SEI
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                STZ     QHEAD
                STZ     MODS
                STZ     MBTN
                LDA     #$1F           ; the sticks: as their ports read (no VIA: nothing pressed)
                STA     STKA
                STA     STKB
                LDA     CLOCKS
                AND     #CLK_MS
                BEQ     @sticks
                LDA     VIA_ORA_NH
                AND     #$1F
                STA     STKA
                LDA     VIA_ORB
                AND     #$1F
                STA     STKB
@sticks:        LDA     STKA
                LDX     #CH_STICK1 - 1
                JSR     STKBITS
                STA     HWB + CH_STICK1 - 1
                LDA     STKB
                LDX     #CH_STICK2 - 1
                JSR     STKBITS
                STA     HWB + CH_STICK2 - 1
                LDA     #128           ; the game port, and the gamepad (reserved): nothing yet
                STA     HWX + CH_GAMEPORT - 1
                STA     HWY + CH_GAMEPORT - 1
                STA     HWX + CH_GAMEPAD - 1
                STA     HWY + CH_GAMEPAD - 1
                STZ     HWB + CH_GAMEPORT - 1
                STZ     HWB + CH_GAMEPAD - 1
                LDA     UCARD
                BEQ     @counts
                STZ     USB_IEN
@drain:         LDA     USB_EVCOUNT
                BEQ     @empty
                LDA     USB_EVENT
                BRA     @drain
@empty:         LDA     #USB_IRQ_ALL
                STA     USB_ISR
                LDA     USB_MODS
                STA     MODS
                LDY     #EV_MODS
                STA     (EVP),Y
                LDA     USB_DEVICES
                LDY     #EV_DEVICES
                STA     (EVP),Y
                LDA     USB_MBTN
                AND     #$1F
                STA     MBTN
                LDY     #EV_MBUTTONS
                STA     (EVP),Y
                JSR     MPOS
                JSR     PADREAD
                STA     HWB + CH_GAMEPORT - 1
                STA     USB_PTRIG
@counts:        LDA     FRAMES         ; the paddles next read PAD_MS from now, if a PTRIG
                STA     PADFRM         ; went (a USB card and the video card's frames)
                LDA     MS
                CLC
                ADC     #PAD_MS
                STA     PADDUE
                STZ     PADARM
                LDA     UCARD
                BEQ     @ctls
                LDA     CLOCKS
                AND     #CLK_FRAME
                BEQ     @ctls
                INC     PADARM
@ctls:
                LDX     #3             ; the four controllers
@ctl:           JSR     CTLPUT
                DEX
                BPL     @ctl
                LDA     #EVON_L2
                STA     EVON
                LDA     UCARD
                BEQ     @on
                LDA     #USB_IRQ_ALL
                STA     USB_IEN
@on:            PLA
                STA     NB_WIN1
                PLP
                RTS

; Function 81's, with a layout-2 area open (else nothing), interrupts
; masked and W1 on the area meanwhile:
; The queue emptied (the tail put at the head) and its lost count 0.
QFLUSH:         BIT     EVON
                BPL     @no
                PHP
                SEI
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDA     QHEAD
                LDY     #EV_QTAIL
                STA     (EVP),Y
                LDA     #0
                LDY     #EV_QLOST
                STA     (EVP),Y
                INY
                STA     (EVP),Y
                JMP     AREAOUT
@no:            RTS

; The four controllers written again (a swap).
CTLALL:         BIT     EVON
                BPL     @no
                PHP
                SEI
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                LDX     #3
@ctl:           JSR     CTLPUT
                DEX
                BPL     @ctl
                BRA     AREAOUT
@no:            RTS

; The mouse's X and Y afresh (a new mode).
MRESET:         BIT     EVON
                BPL     @no
                PHP
                SEI
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                JSR     MPOS
                BRA     AREAOUT
@no:            RTS

; The event at the tail into SNAP, and the tail moved on: A its kind, or 0
; (Z set) if none waits. A tail that isn't a slot is none.
QTAKE:          BIT     EVON
                BMI     @open
                LDA     #0
                RTS
@open:          PHP
                SEI
                LDA     NB_WIN1
                PHA
                LDA     EVBLK
                STA     NB_WIN1
                STZ     UTMP
                LDY     #EV_QTAIL
                LDA     (EVP),Y
                CMP     QHEAD
                BEQ     AREAOUT
                CMP     QSLOTS
                BCS     AREAOUT
                ASL     A
                ASL     A
                ASL     A
                ORA     #EV_QUEUE
                TAY
                LDX     #0
@copy:          LDA     (EVP),Y
                STA     SNAP,X
                INY
                INX
                CPX     #EQ_LEN
                BNE     @copy
                LDY     #EV_QTAIL
                LDA     (EVP),Y
                INC     A
                CMP     QSLOTS
                BCC     @tail
                LDA     #0
@tail:          STA     (EVP),Y
                LDA     SNAP + EQ_KIND
                STA     UTMP
                ; fall through
; W1 and the interrupt mask put back; A = UTMP, Z by it. UTMP is the
; handlers' scratch too, so it is read while the mask is still set: an
; interrupt just after the PLP can't change what is returned.
AREAOUT:        LDY     UTMP
                PLA
                STA     NB_WIN1
                PLP
                TYA
                RTS

; ---------------------------------------------------------------------------
; Function 81, input.

.segment "BDOS"

F_INPUT:        LDX     PX
                BEQ     @op            ; X = 0: A is the operation
                JSR     PGET           ; the block's +0; PP then at +1
@op:            CMP     #IN_OPS
                BCS     INFF
                ASL     A
                TAX
                JMP     (INJUMPS,X)
INFF:           JMP     RETFF
INJUMPS:        .word   IN_INFOX, IN_FLUSHX, IN_MAPX, IN_SWAPX, IN_MOUSEX, IN_LEDSX, IN_REPEATX, IN_NEXTX
.assert (* - INJUMPS) / 2 = IN_OPS, error, "INJUMPS has an entry for each of oodos.inc's IN_*"

; What is plugged in (the USB card's DEVICES, 0 without one) in A, the
; layout L2 writes in X; a block gets them at +1 and +2.
IN_INFOX:       LDA     #0
                LDX     UCARD
                BEQ     @devs
                LDA     USB_DEVICES
@devs:          STA     T0
                LDX     PX
                BEQ     @ret
                JSR     PPUT
                LDA     #EV_LAYOUT2
                JSR     PPUT
@ret:           LDA     T0
                LDX     #EV_LAYOUT2
                RTS

IN_FLUSHX:      JSR     QFLUSH
RET00:          LDA     #0
                TAX
                RTS

; +1-4 the hardware of controllers 1-4.
IN_MAPX:        LDX     PX
                BEQ     INFF
                LDX     #0
@map:           LDA     CMAP,X
                JSR     PPUT
                INX
                CPX     #4
                BNE     @map
                BRA     RET00

; +1 and +2 two controllers, 1-4 (the same twice: nothing changes), whose
; hardware is swapped.
IN_SWAPX:       LDX     PX
                BEQ     INFF
                JSR     PGET
                JSR     CTLNUM
                BCS     INFF
                STA     T0
                JSR     PGET
                JSR     CTLNUM
                BCS     INFF
                TAY
                LDX     T0
                PHP
                SEI
                LDA     CMAP,X
                PHA
                LDA     CMAP,Y
                STA     CMAP,X
                PLA
                STA     CMAP,Y
                JSR     CTLALL
                PLP
                BRA     RET00
CTLNUM:         DEC     A              ; 1-4 to 0-3; carry set if it isn't
                CMP     #4
                RTS

; +1 the mode (0 relative, 1 absolute), +2-3 X's limit and +4-5 Y's
; (absolute mode's); the area's X and Y afresh. No USB card: A = X = $FF.
IN_MOUSEX:      LDX     PX
                BEQ     MFF
                LDX     UCARD
                BEQ     MFF
                JSR     PGET
                CMP     #2
                BCS     MFF
                STA     T0
                JSR     PGET
                STA     T1
                JSR     PGET
                STA     T2
                JSR     PGET
                STA     T3
                JSR     PGET
                STA     T4
                PHP
                SEI
                LDA     T0
                STA     USB_MCTRL
                LDA     T1
                STA     USB_MAXX       ; low byte first: the high byte's write sets the limit
                LDA     T2
                STA     USB_MAXX + 1
                LDA     T3
                STA     USB_MAXY
                LDA     T4
                STA     USB_MAXY + 1
                JSR     MRESET
                PLP
                JMP     RET00
MFF:            JMP     RETFF
.assert USB_MOUSE_ABS = 1, error, "IN_MOUSE's mode is MCTRL's"

; +1 the LEDs (bits 0-2); +1 the console's repeat (REPEAT's bits 0-5).
IN_LEDSX:       JSR     ONEBYTE
                BCS     INFF2
                AND     #$07
                STA     USB_LEDS
                JMP     RET00
IN_REPEATX:     JSR     ONEBYTE
                BCS     INFF2
                AND     #$3F
                STA     USB_REPEAT
                JMP     RET00
; The block's +1 in A, carry clear; carry set if there is no block or no
; USB card.
ONEBYTE:        LDX     PX
                BEQ     @no
                LDX     UCARD
                BEQ     @no
                JSR     PGET
                CLC
                RTS
@no:            SEC
                RTS

; The next event to +1-8, taken: A its kind, X 0; A = X = 0 if none waits.
; No layout-2 area: A = X = $FF.
IN_NEXTX:       LDX     PX
                BEQ     INFF2
                BIT     EVON
                BPL     INFF2
                JSR     QTAKE
                BEQ     @none
                LDX     #0
@copy:          LDA     SNAP,X
                JSR     PPUT
                INX
                CPX     #EQ_LEN
                BNE     @copy
                LDA     SNAP + EQ_KIND
@none:          LDX     #0
                RTS
INFF2:          JMP     RETFF

; Backplane I/O addresses (hw/design.md). In native mode module n's registers
; are at $C000 + n * $80, in the I/O hole $C000-$C7FF.

; North bridge (module D): the four 16 KB windows, VEC and the rest.
NB              = $C680
NB_WIN0         = NB + $00             ; CPU $0000-$3FFF
NB_WIN1         = NB + $01             ; CPU $4000-$7FFF
NB_WIN2         = NB + $02             ; CPU $8000-$BFFF
NB_WIN3         = NB + $03             ; CPU $C000-$FFFF
NB_VEC          = NB + $04             ; $C000-$FFFF while VPB is low
NB_MODE         = NB + $05
NB_STATUS       = NB + $06
NB_RTCCTL       = NB + $07             ; the real-time clock (hw/design.md, "Real-time clock")
NB_IRQL         = NB + $08
NB_IRQH         = NB + $09
NB_WAIT0        = NB + $0A             ; WAIT0-3: 2 bits a module ID, IDs 0-3 in WAIT0; storage only, reserved
                                       ;   for a turbo (B2: every wait state comes by /WAIT)
NB_WAIT1        = NB + $0B
NB_WAIT2        = NB + $0C
NB_WAIT3        = NB + $0D
NB_SEC          = NB + $10             ; SEC-YEAR: the DS3231's $00-$06, BCD; reading SEC latches all seven
NB_MIN          = NB + $11
NB_HOUR         = NB + $12
NB_DAY          = NB + $13             ; day of the week, 1-7
NB_DATE         = NB + $14
NB_MONTH        = NB + $15             ; bit 7: the century
NB_YEAR         = NB + $16
NB_RTCREG       = NB + $17             ; a DS3231 register number: writing it reads the register...
NB_RTCDATA      = NB + $18             ; ...into here; writing here writes it. Wait for BUSY clear
NB_ID           = NB + $7F
; Step B7 (hw/design.md, "The video aperture moved out of RAM"): A20-A23 for each window (bits 3-0;
; a window with the ROM bit set ignores its WINnH), for VEC's vector pulls, and MAP.
NB_WIN0H        = NB + $20             ; W0's A20-A23
NB_WIN1H        = NB + $21
NB_WIN2H        = NB + $22
NB_WIN3H        = NB + $23
NB_VECH         = NB + $24             ; a vector pull's A20-A23 (VEC's ROM bit clear)
NB_MAP          = NB + $25             ; bit 0, AP8: bank 8 goes to the video card's aperture ($01 at reset)
NB_MAP_AP8      = $01
NB_ID_V2        = $D2                  ; superseded; kept only until OODOS's W1HTEST stops naming it
NB_ID_V3        = $D3                  ; revision 3: the windows' high bits, the random source and the timebase
; Revision 3 (ID $D3; hw/design.md, "The north bridge, revision 3"). MS0 and SEC latch when read, and RNGDATA gives
; up its byte: read each with a load (LDA, LDX, LDY), never a read-modify-write or a 16-bit read.
NB_RNGST        = NB + $19             ; the random source: READY CHIP LOCKED FAULT . . . BUSY; write DRAW
NB_RNGCNT       = NB + $1A             ; the bytes in the pool, 0-32
NB_RNGDATA      = NB + $1B             ; the pool's next byte, taken by the read ($00 when empty)
NB_MSSL         = NB + $1C             ; ms into the DS3231's second, 0-999: low byte, from SEC's latch
NB_MSSH         = NB + $1D             ;   and high
NB_HOLE         = NB + $26             ; the common hole: EN RO BLOCK(6); $00 at every reset (OODOS's: $BE)
NB_TBCTL        = NB + $28             ; the timebase: MSF OVR MSIE SYNC; write 1 to MSF, OVR to clear
NB_MS0          = NB + $29             ; the millisecond counter, low byte, live: reading it latches MS1-MS3, TBSUB
NB_MS1          = NB + $2A
NB_MS2          = NB + $2B
NB_MS3          = NB + $2C
NB_TBSUBL       = NB + $2D             ; native cycles into the millisecond (0-7,159), from MS0's latch
NB_TBSUBH       = NB + $2E
NB_RNG_READY    = $80                  ; RNGST: the pool holds a byte
NB_RNG_CHIP     = $40                  ; RNGST: a chip answered at power-on
NB_RNG_LOCKED   = $20                  ; RNGST: its configuration is locked, so the bridge draws from it
NB_RNG_FAULT    = $10                  ; RNGST: the last draw failed three tries
NB_RNG_BUSY     = $01                  ; RNGST read: a draw under way
NB_RNG_DRAW     = $01                  ; RNGST write: clear FAULT, empty the pool and draw 32 fresh bytes
NB_TB_MSF       = $80                  ; TBCTL: a tick since the last acknowledge; write 1 to clear
NB_TB_OVR       = $40                  ; TBCTL: a tick came while MSF was set; write 1 to clear
NB_TB_MSIE      = $20                  ; TBCTL: MSF interrupts (IRQH bit 5, ID $D); takes the value written
NB_TB_SYNC      = $10                  ; TBCTL: MSS is in step with the DS3231's second (read-only)
NB_HOLE_EN      = $80                  ; HOLE: native CPU cycles to $F800-$FFFF go to the block's top 2 KB
NB_HOLE_RO      = $40                  ; HOLE: CPU writes into the hole are dropped

NB_IRQ_VIA      = $40                  ; IRQL bit 6, ID 6: the serial card (its VIA, or the stand-in 6551); 0 = interrupting
NB_IRQ_VIDEO    = $02                  ; IRQH bit 1, ID 9: the video card
NB_IRQ_USB      = $08                  ; IRQH bit 3, ID B: the USB card
NB_IRQ_DMA      = $01                  ; IRQH bit 0, ID 8: the DMA controller's terminal counts
NB_IRQ_SOUND    = $04                  ; IRQH bit 2, ID A: the sound card (the mailbox, the ES5503)
NB_IRQ_NB       = $20                  ; IRQH bit 5, ID D: the north bridge (revision 3: TBCTL's MSF; 2: TICK)
NB_ROM          = $80                  ; window bit: flash, not RAM
NB_RO           = $40                  ; window bit: read-only
CART_BLOCK      = 32                   ; flash block: ROM B's first 16 KB, the game cartridge's (a ROM BIOS
                                       ; before 0.16 looked there for the IIe ROM, and OODOS's 75 does under one)
IIE_IMAGES      = 4                    ; ROM A blocks 0-3: the IIe ROM images, each 16 KB as at $C000-$FFFF
IIE_CHARS_BLOCK = 4                    ; and their character sets, 8 KB each: set n at $10000 + n x $2000
IIE_ID_AT       = $3BB3                ; in an image ($FBB3): IIE_ID if it is a IIe's (apple.s, OODOS's 75)
IIE_ID          = $06
NB_STATUS_BOOTSEL = $01                ; booted from RAM (the bus spy loaded it)
NB_RTC_TICK     = $80                  ; RTCCTL: the second's read happened; write 1 to clear
NB_RTC_TIE      = $40                  ; RTCCTL: TICK interrupts (IRQH bit 5, ID $D); revision 2; reads 0 on 3
NB_RTC_ALARM    = $20                  ; RTCCTL: the DS3231 flagged an alarm (A1F or A2F)
NB_RTC_LOST     = $04                  ; RTCCTL: the DS3231's oscillator stopped (OSF): set the time
NB_RTC_SET      = $02                  ; RTCCTL: write 1 to send SEC-YEAR to the DS3231 (reads 0)
NB_RTC_BUSY     = $01                  ; RTCCTL: the bridge is talking to the DS3231

; STAND-IN terminal: a 6551 in the serial card's block (module 6) until the
; TL16C552A card exists.
ACIA            = $C300
ACIA_DATA       = ACIA + 0
ACIA_STATUS     = ACIA + 1
ACIA_CMD        = ACIA + 2
ACIA_CTRL       = ACIA + 3
ACIA_CMD_RXOFF  = $02                  ; CMD: the receiver's interrupt off
ACIA_CMD_TXMASK = $0C                  ; CMD bits 3-2: the transmitter's control
ACIA_CMD_TXOFF  = $08                  ; CMD bits 3-2 %10: its interrupt off, RTS low

; The serial card's W65C22S VIA, over the 6551's mirror: the joysticks on
; port A (1) and port B (2), bits 0-4, 0 = pressed. OODOS owns ACR, IER and
; IFR (hw/oolib.md, "The VIA is shared").
VIA             = $C340
VIA_ORB         = VIA + $00            ; port B: joystick 2
VIA_ORA         = VIA + $01            ; port A: joystick 1
VIA_DDRB        = VIA + $02
VIA_DDRA        = VIA + $03
VIA_T1CL        = VIA + $04            ; timer 1: counter, low and high (writing the high starts it)
VIA_T1CH        = VIA + $05
VIA_T1LL        = VIA + $06            ; timer 1: latch, low and high
VIA_T1LH        = VIA + $07
VIA_T2CL        = VIA + $08            ; timer 2: counter, low and high (writing the high starts it)
VIA_T2CH        = VIA + $09
VIA_SR          = VIA + $0A            ; shift register
VIA_ACR         = VIA + $0B            ; auxiliary control: the timers', the shift register's, latching
VIA_PCR         = VIA + $0C            ; peripheral control: CA1, CA2, CB1, CB2
VIA_IFR         = VIA + $0D            ; interrupt flags
VIA_IER         = VIA + $0E            ; interrupt enables
VIA_ORA_NH      = VIA + $0F            ; port A with no handshake

VIA_ACR_T1FREE  = $40                  ; ACR: timer 1 free-running, interrupting each period, PB7 not its
VIA_IRQ_T1      = $40                  ; IFR, IER: timer 1
VIA_IRQ_OTHERS  = $3F                  ; IFR, IER: timer 2, CB1, CB2, the shift register, CA1, CA2
VIA_IER_SET     = $80                  ; IER: write 1 to turn on the bits that are 1 (0: turn them off)

; DMA controller (module 8), revision 2 (hw/design.md, "The DMA controller, revision 2"; B2). One
; channel window at $00-$0F shows the channel CONTROL's SELECT names; the shared registers sit
; beside it. Code that sets SELECT saves CONTROL first and puts it back after (interrupt code too).
; Channel 0 serves the SD card, 1 the video card's port, 2 the sound card; channel 3 does the BIOS's
; memory-to-memory copies.
DMA             = $C400
DMA_ADDR        = DMA + $00            ; the selected channel's address (the source), 24 bits
DMA_COUNT       = DMA + $04            ; bytes to move, 0 = 65,536; reads what is left
DMA_MODE        = DMA + $06
DMA_CTRL        = DMA + $07
DMA_DEST        = DMA + $08            ; memory to memory: the destination, 24 bits
DMA_CARD        = DMA + $0C            ; bits 3-0 the module the channel serves (DMA_NONE: none), bit 4 WIDE
DMA_STATUS      = DMA + $10            ; bit n: channel n's terminal count; write 1s to clear
DMA_ABORT       = DMA + $11            ; bit n stops channel n
DMA_REQ         = DMA + $12            ; bit n: channel n's module asked at the last poll
DMA_CONTROL     = DMA + $7E            ; master enable, SELECT (bits 1-3), BURST (bits 4-6)
DMA_ID          = DMA + $7F

DMA_M2M         = $0A                  ; MODE: memory to memory, block (the CPU waits)
DMA_GO          = $03                  ; CTRL: ENABLE + REQ
DMA_ON          = $71                  ; CONTROL: master enable, no burst limit, SELECT 0
DMA_MASTER      = $01                  ; CONTROL: master enable
DMA_SEL         = $0E                  ; CONTROL: SELECT's bits
DMA_SEL0        = $00                  ; CONTROL: SELECT channel n (n * 2)
DMA_SEL1        = $02
DMA_SEL2        = $04
DMA_SEL3        = $06
DMA_SEL4        = $08
DMA_SEL5        = $0A
DMA_SEL6        = $0C
DMA_SEL7        = $0E
DMA_ID_V2       = $82
DMA_CH0         = $01                  ; ABORT, STATUS, REQ: channel 0, the SD card's
DMA_CH1         = $02                  ; channel 1, the video card's port (OODOS's vertical-blank copies)
DMA_CH2         = $04                  ; channel 2, the sound card's
DMA_CH3         = $08                  ; channel 3, memory to memory
DMA_CH4         = $10                  ; channels 4-7: free for later drivers (live since B3)
DMA_CH5         = $20
DMA_CH6         = $40
DMA_CH7         = $80
DMA_ALL         = $FF                  ; ABORT, STATUS: every channel
DMA_NONE        = $08                  ; CARD: no module (the controller's own ID, which never asks)
DMA_WIDE        = $10                  ; CARD: 16-bit transfers (B4)
DMA_MOD_SD      = $07                  ; CARD: the module channel 0 serves at reset, the SD card
DMA_MOD_VIDEO   = $09                  ;   channel 1's, the video card
DMA_MOD_SOUND   = $0A                  ;   channel 2's, the sound card (channels 3-7: DMA_NONE)
DMA_MODE_IRQ    = $80                  ; MODE: interrupt at terminal count

; Video card (module 9): VGA 640x480. Revision 1 (ID $91) draws from banks 8-B,
; which it snoops; revision 2 (ID $92, hw/video-rev2.md, proposed) from 2 MB of its
; own memory, through the aperture and the port. The _R1 names are revision 1's
; registers where revision 2 moved them; the plain names are revision 2's.
VIDEO           = $C480
VIDEO_CTRL      = VIDEO + $00
VIDEO_BORDER    = VIDEO + $01
VIDEO_PALBASE_R1 = VIDEO + $02         ; revision 1
VIDEO_SPRBASE_R1 = VIDEO + $03         ; revision 1
VIDEO_IEN       = VIDEO + $04
VIDEO_ISR       = VIDEO + $05          ; pending: write 1s to clear
VIDEO_LINE      = VIDEO + $06          ; 2 bytes: write the line to interrupt at; read the beam's line
VIDEO_STATUS    = VIDEO + $08          ; bit 7: in vertical blank
VIDEO_SPLASH    = VIDEO + $09          ; the boot splash (splash.s)
VIDEO_PALBASE   = VIDEO + $0A          ; revision 2: 2 bytes, card address / 256
VIDEO_SPRBASE   = VIDEO + $0C          ; revision 2: 2 bytes, card address / 256
VIDEO_SPRCTL    = VIDEO + $0E          ; revision 2: sprite control, bit 0 FULL (hw/sprites640.md)
VIDEO_L0        = VIDEO + $10          ; layer 0
VIDEO_L0_CONFIG = VIDEO_L0 + 0
VIDEO_L0_MAP_R1 = VIDEO_L0 + 1         ; revision 1's layer registers
VIDEO_L0_TILE_R1 = VIDEO_L0 + 2
VIDEO_L0_HSCROLL_R1 = VIDEO_L0 + 3
VIDEO_L0_VSCROLL_R1 = VIDEO_L0 + 5
VIDEO_L0_PALOFS_R1 = VIDEO_L0 + 7
VIDEO_L0_MAP    = VIDEO_L0 + 1         ; revision 2's: 2 bytes, card address / 256
VIDEO_L0_TILE   = VIDEO_L0 + 3         ; 2 bytes, card address / 256
VIDEO_L0_HSCROLL = VIDEO_L0 + 5        ; 2 bytes
VIDEO_L0_VSCROLL = VIDEO_L0 + 7        ; 2 bytes
VIDEO_L0_PALOFS = VIDEO_L0 + 9
VIDEO_L1        = VIDEO + $20          ; layer 1, the same registers
VIDEO_L1_CONFIG = VIDEO_L1 + 0
VIDEO_L1_MAP_R1 = VIDEO_L1 + 1         ; revision 1's
VIDEO_L1_TILE_R1 = VIDEO_L1 + 2
VIDEO_L1_HSCROLL_R1 = VIDEO_L1 + 3
VIDEO_L1_VSCROLL_R1 = VIDEO_L1 + 5
VIDEO_L1_PALOFS_R1 = VIDEO_L1 + 7
VIDEO_L1_MAP    = VIDEO_L1 + 1         ; revision 2's
VIDEO_L1_TILE   = VIDEO_L1 + 3
VIDEO_L1_HSCROLL = VIDEO_L1 + 5
VIDEO_L1_VSCROLL = VIDEO_L1 + 7
VIDEO_L1_PALOFS = VIDEO_L1 + 9
VIDEO_SLOT0     = VIDEO + $30          ; revision 2: aperture slot 0's 16 KB page, 2 bytes
VIDEO_SLOT1     = VIDEO + $32
VIDEO_SLOT2     = VIDEO + $34
VIDEO_SLOT3     = VIDEO + $36
VIDEO_PADDR     = VIDEO + $38          ; revision 2: the port's address, 3 bytes
VIDEO_PDATA     = VIDEO + $3B          ; the port's data: each access moves PADDR by PSTEP
VIDEO_PSTEP     = VIDEO + $3C          ; 2 bytes
VIDEO_PCTL      = VIDEO + $3E
VIDEO_MEMSIZE   = VIDEO + $3F          ; card memory in 64 KB units, read-only
VIDEO_ID        = VIDEO + $7F

VIDEO_ON_L0     = $03                  ; CTRL: display on, layer 0 on
VIDEO_TEXT80B   = $80                  ; CONFIG: text 80x30, attribute bit 7 blinks
VIDEO_TEXT40    = $01                  ; CONFIG: text 40x30
VIDEO_TILE      = $02                  ; CONFIG: tiles, 8x8 at 4 bits...
VIDEO_MAP_W64   = $20                  ; ...from a map 64 wide (32 high)
VIDEO_BITMAP640 = $06                  ; CONFIG, revision 2: bitmap 640, 1,024-byte rows of palette indices
VIDEO_DIRECT640 = $07                  ; CONFIG, revision 2: direct 640, 1,280-byte rows of $0RGB words
VIDEO_DIRECT_T  = $80                  ; direct 640: a pixel's byte 1 bit 7, transparent (hw/video640.md)
VIDEO_SPR_FULL  = $01                  ; SPRCTL: an entry's byte 3 bit 7 makes that sprite full resolution
VIDEO_SPRX_FULL = $80                  ; a sprite entry's byte 3 (X high) bit 7: full resolution, 640x480 units
VIDEO_ALL       = $0F                  ; CTRL: display, both layers, sprites
VIDEO_ID_V1     = $91
VIDEO_ID_V2     = $92
VIDEO_IRQ_VBLANK = $01                 ; IEN, ISR: vertical blank
VIDEO_IRQ_LINE  = $02                  ; IEN, ISR: the scan line LINE
VIDEO_IRQ_COLLIDE = $04                ; IEN, ISR: sprite collision
VIDEO_IRQ_ALL   = $07
VIDEO_SPLASH_ON = $01                  ; SPLASH: play it (clears itself at the end)
VIDEO_PCTL_DRQ  = $01                  ; PCTL: request DMA (/DREQ), served by channel 1 (CARD 9)
VIDEO_PCTL_FROM = $02                  ; PCTL: DMA reads the card (0: DMA writes it)
VIDEO_PCTL_DOWN = $04                  ; PCTL: PADDR steps down
VIDEO_MEMSIZE_2MB = 32                 ; MEMSIZE on a 2 MB card
VIDEO_APERTURE  = $80000               ; revision 2: the aperture, physical bank 8 (four 16 KB slots), while
                                       ;   the north bridge's AP8 is set
VIDEO_APERTURE2 = $E00000              ; the aperture's own address (B7): slot n at $E00000 + n x $4000
VIDEO_APER_H    = $0E                  ; the WINnH that reaches it, with BLOCK the slot (0-3)

; Zero page and low RAM, and who has them:
;
;   $00-$09    the boot's copy pointers and C800R's stub (bios.s), Apple II
;              mode's way in (apple.s): free once the monitor is up
;   $10-$21    the flash update (flash.s), while it runs
;   $24-$2B    the Woz Monitor
;   $30-$7B    the video demo (demo.s)
;   $E0-$E7    the screen (video.s). ECHO ($C80F) uses them: a program that
;              prints leaves them alone
;   $F0-$FB    the SD card (sd.s)
;   $0100      the stack; Break and BRK start it afresh
;   $0200-$027F the monitor's input line
;   $0280-$029A the flash update's path and commit bytes (flash.s)
;   $0400-$05FF the SD card's sector buffer; the demo's sprite table
;   $FC000-$FC7FF RAM block 63's first 2 KB, under the I/O hole: the
;              screen's scroll buffer (video.s), reached only by DMA
BIOS_ZP         = $E0                  ; the screen (video.s)

; USB card (module B): keyboard, mouse, game port.
USB             = $C580
USB_KEY         = USB + $00            ; cooked ASCII, bit 7 the strobe
USB_KEYCLR      = USB + $01            ; any access clears the strobe
USB_KBCFG       = USB + $07
USB_MODS        = USB + $02            ; the modifiers held
USB_EVCOUNT     = USB + $03            ; bytes waiting in the event FIFO
USB_EVENT       = USB + $04            ; the FIFO's next byte: a usage, then its flags
USB_LEDS        = USB + $05
USB_REPEAT      = USB + $06            ; the repeat's rate and delay
USB_MCTRL       = USB + $10            ; the mouse
USB_MBTN        = USB + $11
USB_MX          = USB + $12            ; 2 bytes; reading MX's low byte latches MX, MY and MWHEEL
USB_MY          = USB + $14            ; 2 bytes
USB_MWHEEL      = USB + $16
USB_MAXX        = USB + $17            ; 2 bytes: absolute mode's limits
USB_MAXY        = USB + $19            ; 2 bytes
USB_GAME        = USB + $20            ; the game port: the paddles' timers and the buttons
USB_PTRIG       = USB + $21            ; any access starts the paddles' timers
USB_PADDLE0     = USB + $22            ; the paddles, 0-255
USB_PADDLE1     = USB + $23
USB_PADDLE2     = USB + $24
USB_PADDLE3     = USB + $25
USB_SPKR        = USB + $26            ; any access toggles the speaker
USB_IEN         = USB + $30
USB_ISR         = USB + $31            ; pending: write 1s to clear
USB_DEVICES     = USB + $32            ; what is plugged in
USB_ID          = USB + $7F

USB_NMI_PAUSE   = $80                  ; KBCFG: Pause/Break pulls /NMI
USB_RELEASE     = $80                  ; EVENT's flags: a release
USB_MOUSE_ABS   = $01                  ; MCTRL: absolute mode
USB_REPEAT_DEF  = $10                  ; REPEAT at /RESET: 500 ms, then 30 a second
USB_MAX_DEF_X   = 639                  ; MAXX and MAXY at /RESET
USB_MAX_DEF_Y   = 479
USB_IRQ_KEY     = $01                  ; IEN, ISR: a key event (or a repeat)
USB_IRQ_MOUSE   = $02                  ;   the mouse changed
USB_IRQ_GAME    = $04                  ;   a game port button changed
USB_IRQ_DEVICE  = $08                  ;   a device plugged in or out
USB_IRQ_ALL     = $0F
USB_ID_V1       = $B1

NB_MODE_APPLE   = $01                  ; MODE: enter Apple II mode (only a hard reset leaves)
DMA_HOLD        = $20                  ; MODE: the source address holds still (a fill)
DMA_DEC         = $10                  ; MODE: the addresses step down, not up

; Mass storage card (module 7): an SD card behind an RP2350 (sd.s).
SD              = $C380
SD_CTRL         = SD + $00
SD_STATUS       = SD + $01
SD_ERROR        = SD + $02
SD_CMD          = SD + $03
SD_UNIT         = SD + $04
SD_LBA          = SD + $06             ; 4 bytes, low first
SD_COUNT        = SD + $0A             ; 2 bytes
SD_DATA         = SD + $0C
SD_BUFPTR       = SD + $0D             ; 2 bytes: the position in the sector, 0-511
SD_IEN          = SD + $10
SD_ISR          = SD + $11             ; pending: write 1s to clear
SD_ID           = SD + $7F

SD_BUSY         = $80                  ; STATUS
SD_READY        = $40
SD_PRESENT      = $20
SD_DATA_READY   = $08
SD_ERR          = $01
SD_CTRL_DMA     = $01                  ; CTRL: the card's DMA request follows DATA (channel 0, CARD 7)
SD_CTRL_RESET   = $02                  ; CTRL: abandon the command, start the card up again
SD_READ         = $20                  ; CMD
SD_MOUNT        = $A0
SD_LIST         = $A2
SD_IDENTIFY     = $EC
SD_NOT_FOUND    = 8                    ; ERROR
SD_ID_V1        = $71
SD_ZP           = $F0                  ; $F0-$FB: sd.s

; DMA modes for a card's channel (channel 0, storage's, among them).
DMA_DEMAND_IN   = $01                  ; MODE: demand mode, card to memory
DMA_DEMAND_OUT  = $05                  ; MODE: demand mode, memory to card
DMA_BLOCK_IN    = $02                  ; MODE: block, card to memory (once DRQ or REQ starts it)
DMA_BLOCK_OUT   = $06                  ; MODE: block, memory to card
DMA_ENABLE      = $01                  ; CTRL

; The sound card (module A), revision 3: the card as a device (hw/future/sound-device.md). A program writes
; command records to CMD and reads event records from EVT; data goes in through DATA, by DMA channel 2.
SOUND           = $C500
SOUND_CTRL      = SOUND + $00          ; DRQ; FWRESTART and RESET, strobes
SOUND_STATUS    = SOUND + $01          ; READY, CMDRDY, EVT, XFER, DATA, EVLOST, ERR, the card interrupting
SOUND_ERROR     = SOUND + $02          ; why the last record was refused at the door (SOUND_E_*)
SOUND_CMD       = SOUND + $03          ; W: a command record's bytes, in order
SOUND_CMDFREE   = SOUND + $04          ; the command FIFO's room in bytes (255: 255 or more)
SOUND_EVT       = SOUND + $05          ; the event FIFO's next byte, taken by the read (0: none waiting)
SOUND_EVTLEN    = SOUND + $06          ; the bytes waiting in the event FIFO (255: 255 or more)
SOUND_DATA      = SOUND + $07          ; W: the open transfer's bytes (DMA channel 2)
SOUND_XLEFT     = SOUND + $08          ; 3 bytes: the bytes the transfer still wants; reading the low latches
SOUND_XTAG      = SOUND + $0B          ; the tag of the command that opened the transfer
SOUND_IEN       = SOUND + $0C          ; the interrupts on: ISR's bits
SOUND_ISR       = SOUND + $0D          ; EVT (a level), XDONE, CMDROOM, READY; write 1 to clear bits 1-3
SOUND_MEMSIZE   = SOUND + $0E          ; the store in 64 KB units: 32, 64 or 128
SOUND_FEATURES  = SOUND + $0F          ; SOUND_FT_*
SOUND_FWVER     = SOUND + $10          ; 2 bytes: the firmware's version, major then minor
SOUND_ID        = SOUND + $7F
SOUND_ID_V3     = $A3
SOUND_TYPE      = $A0                  ; ID's high nibble, the module: the card at any revision
SOUND_CTRL_DRQ  = $01                  ; CTRL: DATA asks for DMA while a transfer is open and has room
SOUND_CTRL_FWRESTART = $40             ; CTRL: the firmware starts again (READY clear until it is up)
SOUND_CTRL_RESET = $80                 ; CTRL: as /RESET: the transfer, both FIFOs and every sound gone
SOUND_ST_READY  = $01                  ; STATUS: the firmware is up
SOUND_ST_CMDRDY = $02                  ; STATUS: CMD has room for a whole record
SOUND_ST_EVT    = $04                  ; STATUS: an event record waits
SOUND_ST_XFER   = $08                  ; STATUS: a transfer is open
SOUND_ST_DATA   = $10                  ; STATUS: DATA can take a byte now
SOUND_ST_EVLOST = $20                  ; STATUS: events were lost since the last LOSTCLR
SOUND_ST_ERR    = $40                  ; STATUS: the last record was refused before it ran
SOUND_ST_IRQ    = $80                  ; STATUS: the card is interrupting (ISR AND IEN)
SOUND_INT_EVT   = $01                  ; IEN, ISR: an event waits (a level)
SOUND_INT_XDONE = $02                  ;   a transfer took its last byte
SOUND_INT_CMDROOM = $04                ;   CMD had been full and has room again
SOUND_INT_READY = $08                  ;   the firmware came up
SOUND_FT_TWOSID = $01                  ; FEATURES: two SIDs, the 6510 (tunes), the ES5503, raw PCM, MP3,
SOUND_FT_TUNES  = $02                  ;   the EQ, the vis page, more in IDENTIFY
SOUND_FT_DOC    = $04
SOUND_FT_PCM    = $08
SOUND_FT_MP3    = $10
SOUND_FT_EQ     = $20
SOUND_FT_VIS    = $40
SOUND_FT_MORE   = $80
; A command record: +0 the command, +1 n (the parameters' bytes), +2 a tag, +3 on the parameters. An event
; record: +0 the kind, +1 n (the bytes after +1); a REPLY's +2 the tag, +3 the status (0 or SOUND_E_*), +4 on
; the results; another event's +2 its object, +3-6 the card's ms, +7 on its payload.
SOUND_NOP       = $00                  ; the commands: the device
SOUND_IDENTIFY  = $01
SOUND_STOPALL   = $02
SOUND_FREEALL   = $03
SOUND_EVENTS    = $04
SOUND_LOSTCLR   = $05
SOUND_TIME      = $06
SOUND_SELFTEST  = $07
SOUND_CHECKSUM  = $08
SOUND_BOOTSOUND = $09
SOUND_FW_BEGIN  = $0D
SOUND_FW_COMMIT = $0E
SOUND_XFERABORT = $0F
SOUND_TUNE_LOAD = $20                  ; tunes
SOUND_TUNE_PLAY = $21
SOUND_TUNE_STOP = $22
SOUND_TUNE_PAUSE = $23
SOUND_TUNE_RESUME = $24
SOUND_TUNE_STATUS = $25
SOUND_TUNE_INFO = $26
SOUND_TUNE_FREE = $27
SOUND_SAMPLE_LOAD = $30                ; the sound bank
SOUND_EFFECT_DEF = $31
SOUND_EFFECT_LOAD = $32
SOUND_PLAY      = $33
SOUND_VOICE_STOP = $34
SOUND_BANK_FREE = $35
SOUND_MUSIC_LOAD = $40                 ; music
SOUND_MUSIC_PLAY = $41
SOUND_MUSIC_STOP = $42
SOUND_MUSIC_PAUSE = $43
SOUND_MUSIC_RESUME = $44
SOUND_MUSIC_STATUS = $45
SOUND_MUSIC_THRESH = $46
SOUND_STREAM_OPEN = $50                ; streams
SOUND_STREAM_DATA = $51
SOUND_STREAM_END = $52
SOUND_STREAM_NEXT = $53
SOUND_STREAM_PAUSE = $54
SOUND_STREAM_RESUME = $55
SOUND_STREAM_FLUSH = $56
SOUND_STREAM_STATUS = $57
SOUND_STREAM_CLOSE = $58
SOUND_BEEP      = $60                  ; the beep
SOUND_BELL      = $61
SOUND_VOLUME    = $70                  ; the mix, the EQ and the vis page
SOUND_MIX       = $71
SOUND_EQ        = $72
SOUND_EQ_ALL    = $73
SOUND_EQ_ON     = $74
SOUND_VIS       = $75
SOUND_SIDREGS   = $76
SOUND_LEVELS    = $77
SOUND_EV_REPLY  = $01                  ; the event kinds (0: none)
SOUND_EV_XFERDONE = $10
SOUND_EV_FWRESULT = $11
SOUND_EV_TUNE_STARTED = $20
SOUND_EV_TUNE_STALL = $21
SOUND_EV_TUNE_JAM = $22
SOUND_EV_TUNE_ENDED = $23
SOUND_EV_VOICE_DONE = $30
SOUND_EV_VOICE_STOLEN = $31
SOUND_EV_MUSIC_MARK = $40
SOUND_EV_MUSIC_COUNT = $41
SOUND_EV_MUSIC_DONE = $42
SOUND_EV_NEED   = $50
SOUND_EV_UNDERRUN = $51
SOUND_EV_UNDERRUN_END = $52
SOUND_EV_TRACK  = $53
SOUND_EV_STREAM_ENDED = $54
SOUND_EV_BEEP_DONE = $60
SOUND_E_UNKNOWN = 1                    ; the errors: ERROR's, and a REPLY's status
SOUND_E_PARAM   = 2
SOUND_E_SLOT    = 3
SOUND_E_MEMORY  = 4
SOUND_E_BUSY    = 5
SOUND_E_DATA    = 6
SOUND_E_XFER    = 7
SOUND_E_STATE   = 8
SOUND_E_NOTREADY = 9
SOUND_E_JAM     = 10
SOUND_E_FIRMWARE = 11

; Disk II controller (module C), slot 6 in Apple II mode. In native mode its block has slot 6's sixteen
; switches, in the same order, and its ID (hw/design.md, "Disk II controller clone"). Any access works a
; switch; a read of an even one gives the data register, of an odd one $FF.
DISK2           = $C600
DISK2_PH0OFF    = DISK2 + $00          ; stepper phase 0 off, then on; phases 1-3 the same
DISK2_PH0ON     = DISK2 + $01
DISK2_PH1OFF    = DISK2 + $02
DISK2_PH1ON     = DISK2 + $03
DISK2_PH2OFF    = DISK2 + $04
DISK2_PH2ON     = DISK2 + $05
DISK2_PH3OFF    = DISK2 + $06
DISK2_PH3ON     = DISK2 + $07
DISK2_MOTOROFF  = DISK2 + $08          ; off: the card keeps it turning a second more
DISK2_MOTORON   = DISK2 + $09
DISK2_DRIVE1    = DISK2 + $0A
DISK2_DRIVE2    = DISK2 + $0B
DISK2_Q6OFF     = DISK2 + $0C
DISK2_Q6ON      = DISK2 + $0D
DISK2_Q7OFF     = DISK2 + $0E          ; read mode
DISK2_Q7ON      = DISK2 + $0F          ; write mode
DISK2_ID        = DISK2 + $7F
DISK2_ID_V1     = $C1

; Console card (module E): the front panel.
CONSOLE         = $C700
CONSOLE_SWR     = CONSOLE + $00        ; 3 bytes: the switches, 20 bits
CONSOLE_PANEL   = CONSOLE + $03        ; the HALT and S BUS CYCLE switches
CONSOLE_KEYS    = CONSOLE + $04        ; the keys pressed since the last read
CONSOLE_DISPLAY = CONSOLE + $05        ; the data lights' byte (write-only)
CONSOLE_CTRL    = CONSOLE + $06
CONSOLE_BPADDR  = CONSOLE + $08        ; 3 bytes: the breakpoint's address, 20 bits
CONSOLE_BPCTRL  = CONSOLE + $0B
CONSOLE_STATUS  = CONSOLE + $0C
CONSOLE_LADDR   = CONSOLE + $10        ; 3 bytes: the latched cycle's address,
CONSOLE_LDATA   = CONSOLE + $13        ;   its data
CONSOLE_LSTAT   = CONSOLE + $14        ;   and its signals
CONSOLE_PADDR   = CONSOLE + $15        ; 3 bytes: the panel's address
CONSOLE_IEN     = $C718
CONSOLE_ISR     = CONSOLE + $19        ; pending: write 1 to clear
CONSOLE_ID      = $C77F
CONSOLE_ID_V1   = $E1

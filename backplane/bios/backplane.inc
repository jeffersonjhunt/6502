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
NB_WAIT0        = NB + $0A             ; WAIT0-3: wait states, 2 bits a module ID, IDs 0-3 in WAIT0
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

NB_IRQ_VIA      = $40                  ; IRQL bit 6, ID 6: the serial card (its VIA, or the stand-in 6551); 0 = interrupting
NB_IRQ_VIDEO    = $02                  ; IRQH bit 1, ID 9: the video card
NB_ROM          = $80                  ; window bit: flash, not RAM
NB_RO           = $40                  ; window bit: read-only
CART_BLOCK      = 32                   ; flash block: the cartridge's first 16 KB, a IIe ROM as at $C000-$FFFF
IIE_ID_AT       = $3BB3                ; in it ($FBB3): IIE_ID if it is a IIe's (apple.s, OODOS's 75)
IIE_ID          = $06
NB_STATUS_BOOTSEL = $01                ; booted from RAM (the bus spy loaded it)
NB_RTC_TICK     = $80                  ; RTCCTL: the second's read happened; write 1 to clear
NB_RTC_TIE      = $40                  ; RTCCTL: TICK interrupts (IRQH bit 5, ID $D)
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

; DMA controller (module 8): channel 3 does the BIOS's memory-to-memory copies.
DMA             = $C400
DMA3_ADDR       = DMA + $30            ; source, 20 bits
DMA3_COUNT      = DMA + $34
DMA3_MODE       = DMA + $36
DMA3_CTRL       = DMA + $37
DMA3_DEST       = DMA + $38            ; destination, 20 bits
DMA1_ADDR       = DMA + $10            ; channel 1: as channel 0's
DMA1_COUNT      = DMA + $14
DMA1_MODE       = DMA + $16
DMA1_CTRL       = DMA + $17
DMA1_DEST       = DMA + $18
DMA2_ADDR       = DMA + $20            ; channel 2, the sound card's RAM: as channel 0's
DMA2_COUNT      = DMA + $24
DMA2_MODE       = DMA + $26
DMA2_CTRL       = DMA + $27
DMA2_DEST       = DMA + $28
DMA_STATUS      = DMA + $40            ; terminal counts: write 1s to clear
DMA_CONTROL     = DMA + $41
DMA_ABORT       = DMA + $42            ; bit n stops channel n
DMA_ID          = DMA + $7F

DMA_M2M         = $0A                  ; MODE: memory to memory, block (the CPU waits)
DMA_GO          = $03                  ; CTRL: ENABLE + REQ
DMA_ON          = $71                  ; CONTROL: master enable, no burst limit
DMA_MASTER      = $01                  ; CONTROL: master enable
DMA_ID_V1       = $81
DMA_CH3         = $08                  ; ABORT, STATUS: channel 3
DMA_ALL         = $0F                  ; ABORT, STATUS: every channel
DMA_MODE_IRQ    = $80                  ; MODE: interrupt at terminal count

; Video card (module 9): VGA 640x480 from banks 8-B.
VIDEO           = $C480
VIDEO_CTRL      = VIDEO + $00
VIDEO_BORDER    = VIDEO + $01
VIDEO_PALBASE   = VIDEO + $02
VIDEO_SPRBASE   = VIDEO + $03
VIDEO_IEN       = VIDEO + $04
VIDEO_ISR       = VIDEO + $05          ; pending: write 1s to clear
VIDEO_LINE      = VIDEO + $06          ; 2 bytes: write the line to interrupt at; read the beam's line
VIDEO_STATUS    = VIDEO + $08          ; bit 7: in vertical blank
VIDEO_SPLASH    = VIDEO + $09          ; the boot splash (splash.s)
VIDEO_L0        = VIDEO + $10          ; layer 0
VIDEO_L0_CONFIG = VIDEO_L0 + 0
VIDEO_L0_MAP    = VIDEO_L0 + 1
VIDEO_L0_TILE   = VIDEO_L0 + 2
VIDEO_L0_HSCROLL = VIDEO_L0 + 3
VIDEO_L0_VSCROLL = VIDEO_L0 + 5
VIDEO_L0_PALOFS = VIDEO_L0 + 7
VIDEO_L1        = VIDEO + $20          ; layer 1, the same registers
VIDEO_L1_CONFIG = VIDEO_L1 + 0
VIDEO_L1_MAP    = VIDEO_L1 + 1
VIDEO_L1_TILE   = VIDEO_L1 + 2
VIDEO_L1_HSCROLL = VIDEO_L1 + 3
VIDEO_L1_VSCROLL = VIDEO_L1 + 5
VIDEO_L1_PALOFS = VIDEO_L1 + 7
VIDEO_ID        = VIDEO + $7F

VIDEO_ON_L0     = $03                  ; CTRL: display on, layer 0 on
VIDEO_TEXT80B   = $80                  ; CONFIG: text 80x30, attribute bit 7 blinks
VIDEO_TEXT40    = $01                  ; CONFIG: text 40x30
VIDEO_TILE      = $02                  ; CONFIG: tiles, 8x8 at 4 bits...
VIDEO_MAP_W64   = $20                  ; ...from a map 64 wide (32 high)
VIDEO_ALL       = $0F                  ; CTRL: display, both layers, sprites
VIDEO_ID_V1     = $91
VIDEO_IRQ_VBLANK = $01                 ; IEN, ISR: vertical blank
VIDEO_IRQ_LINE  = $02                  ; IEN, ISR: the scan line LINE
VIDEO_IRQ_COLLIDE = $04                ; IEN, ISR: sprite collision
VIDEO_IRQ_ALL   = $07
VIDEO_SPLASH_ON = $01                  ; SPLASH: play it (clears itself at the end)

; Zero page and low RAM, and who has them:
;
;   $00-$09    the boot's copy pointers and C800R's stub (bios.s), Apple II
;              mode's way in (apple.s): free once the monitor is up
;   $24-$2B    the Woz Monitor
;   $30-$7B    the video demo (demo.s)
;   $E0-$E7    the screen (video.s). ECHO ($C80F) uses them: a program that
;              prints leaves them alone
;   $F0-$F6    the SD card (sd.s)
;   $0100      the stack; Break and BRK start it afresh
;   $0200-$027F the monitor's input line
;   $0400-$05FF the SD card's sector buffer; the demo's sprite table
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
USB_ID_V1       = $B1

NB_MODE_APPLE   = $01                  ; MODE: enter Apple II mode (only a hard reset leaves)
DMA_HOLD        = $20                  ; MODE: the source address holds still (a fill)

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
SD_CTRL_DMA     = $01                  ; CTRL: DRQ0 follows DATA
SD_CTRL_RESET   = $02                  ; CTRL: abandon the command, start the card up again
SD_READ         = $20                  ; CMD
SD_MOUNT        = $A0
SD_LIST         = $A2
SD_IDENTIFY     = $EC
SD_NOT_FOUND    = 8                    ; ERROR
SD_ID_V1        = $71
SD_ZP           = $F0                  ; $F0-$F6: sd.s

; DMA channel 0, storage's.
DMA0_ADDR       = DMA + $00
DMA0_COUNT      = DMA + $04
DMA0_MODE       = DMA + $06
DMA0_CTRL       = DMA + $07
DMA0_DEST       = DMA + $08            ; memory to memory: the destination, 20 bits
DMA_DEMAND_IN   = $01                  ; MODE: demand mode, card to memory
DMA_ENABLE      = $01                  ; CTRL

; Sound card (module A): two SIDs and a 6510 of its own.
SOUND           = $C500
SOUND_SIDS      = $3D                  ; $00-$3C: SID A, a gap, SID B
SOUND_SIDA      = SOUND + $00          ; SID A's registers, $00-$1C
SOUND_SIDB      = SOUND + $20          ; SID B's, the same
SOUND_STATUS    = SOUND + $41          ; the mailboxes' flags
SOUND_MBOUT     = SOUND + $46          ; the mailbox to the card
SOUND_MBIN      = SOUND + $47          ; the mailbox from it: reading empties it
SOUND_CTRL      = SOUND + $40
SOUND_NTSC      = $01                  ; CTRL: the SIDs' clock is NTSC's
SOUND_RUN       = $02                  ; CTRL: the card's 6510 runs
SOUND_RESET     = $04                  ; CTRL: reset the card's 6510 (vector at its $FFFC)
SOUND_RAMADDR   = SOUND + $43          ; the card's RAM: address, then data,
SOUND_RAMDATA   = SOUND + $45          ;   which moves the address on
SOUND_IEN       = SOUND + $42
SOUND_ID        = SOUND + $7F
SOUND_ID_V1     = $A1
SOUND_ID_V2     = $A2                  ; revision 2: a PCM stream and an ES5503 (hw/sound-rev2.md, proposed)
SOUND_TYPE      = $A0                  ; ID's high nibble, the module: the card at any revision
; Revision 2's registers.
SOUND_PCMCTL    = SOUND + $48          ; RUN, PAUSE, FLUSH (a strobe), EOF
SOUND_CODEC     = SOUND + $49          ; 0 raw PCM, 1 MP3
SOUND_RATE      = SOUND + $4A          ; 2 bytes: raw PCM's rate in Hz; reads the stream's
SOUND_PCMFMT    = SOUND + $4C          ; bit 0 stereo, bit 1 16-bit
SOUND_FREE      = SOUND + $4D          ; 2 bytes: the FIFO's room, 256-byte units; reading the low latches
SOUND_PCMDATA   = SOUND + $4F          ; the stream
SOUND_STATE     = SOUND + $50          ; idle, buffering, playing, paused, underrun, ended; $80 up errors
SOUND_POS       = SOUND + $51          ; 4 bytes: frames played; reading the low latches, writing it zeroes
SOUND_KBPS      = SOUND + $55          ; 2 bytes: the MP3's bitrate
SOUND_VOLL      = SOUND + $57          ; the master volumes, 255 unity
SOUND_VOLR      = SOUND + $58
SOUND_EQIDX     = SOUND + $59          ; 0 the preamp, 1-10 the bands; bit 7 the enable
SOUND_EQVAL     = SOUND + $5A          ; the gain at EQIDX in half-dB, +-24; EQIDX moves on
SOUND_VISIDX    = SOUND + $5B          ; the vis page's index; writing 0 takes a snapshot
SOUND_VISDATA   = SOUND + $5C          ; the byte there, and the index moves on
SOUND_CODECS    = SOUND + $5D          ; the codecs there are: $83
SOUND_DOCCTL    = SOUND + $5E          ; the ES5503's port: RUN, its IRQ, auto-increment, RAM
SOUND_DOCDATA   = SOUND + $5F          ; the register or wave RAM byte at DOCADDR
SOUND_DOCADDR   = SOUND + $60          ; 2 bytes: the register (low), or wave RAM's address
SOUND_DOCBANK   = SOUND + $62          ; bit 0: wave RAM's address bit 16
SOUND_DRQSEL    = SOUND + $63          ; the DRQ's source: 0 RAMDATA, 1 PCMDATA, 2 DOCDATA
SOUND_PCM_RUN   = $01                  ; PCMCTL
SOUND_PCM_PAUSE = $02
SOUND_PCM_FLUSH = $04
SOUND_PCM_EOF   = $08
SOUND_DOC_RUN   = $01                  ; DOCCTL
SOUND_DOC_IRQ_CARD = $04               ; DOCCTL: its interrupt to the 6510, not the host
SOUND_DOC_AUTOINC = $20
SOUND_DOC_RAM   = $40                  ; DOCCTL: DOCDATA reaches wave RAM, not the registers

; Disk II controller (module C), slot 6 in Apple II mode. In native mode only its ID answers in its block
; (hw/design.md, "Disk II controller clone"); the switches there are still to come in the emulator.
DISK2           = $C600
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

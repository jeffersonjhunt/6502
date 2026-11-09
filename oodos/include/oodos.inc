; OODOS for programs (hw/oodos.md): the program header, the base page and the
; BDOS's function numbers. Link with cfg/com.cfg or cfg/prl.cfg.
;
; Call the BDOS with JSR BDOS: the function in Y, a byte parameter in A, a
; word parameter in A (low) and X (high). The result comes back in A and X.

.ifndef OODOS_INC
OODOS_INC = 1

; The header (hw/oodos.md, "The program header"). The configs supply the
; flags and the layout; one module of a program says OODOS_HEADER, once.
.define OODOS_MAGIC $4F, $4F, $44, $1A ; "OOD", then ^Z so TYPE stops there
OH_FLAGS        = 4                    ; offsets in the header
OH_PAGE         = 5
OH_SIZE         = 6
OH_BSS          = 8
OH_ZPBASE       = 10
OH_ZPSIZE       = 11
OH_RSXCOUNT     = 12
OH_RSXREC       = 14
OH_LEN          = 16
OF_RELOC        = $01                  ; flag: a .PRL, with its bitmaps
OF_RSX          = $02                  ; flag: the image starts with the RSX prefix
OF_NOT816       = $80                  ; flag: breaks the 65C816 rules (never set by these tools)

.macro OODOS_HEADER
        .import __OODOS_FLAGS__, __TPA_START__, __BSS_RUN__, __BSS_SIZE__
        .import __ZP_START__, __ZEROPAGE_SIZE__
        .pushseg
        .segment "HEADER"
        .byte   OODOS_MAGIC
        .byte   <__OODOS_FLAGS__
        .byte   >__TPA_START__          ; the link page
        .word   __BSS_RUN__ - __TPA_START__ ; the image's size
        .word   __BSS_SIZE__
        .byte   <__ZP_START__, <__ZEROPAGE_SIZE__
        .byte   0                       ; attached RSXs (GENCOM)
        .byte   0                       ; reserved
        .word   0                       ; the RSX table's record
        .popseg
.endmacro

; The RSX prefix, at the start of an RSX's image (CP/M 3's offsets).
RSX_SERIAL      = 0                    ; 6 bytes, filled in by the loader
RSX_START       = 6                    ; JMP to the RSX's BDOS entry
RSX_NEXT        = 9                    ; JMP to the next RSX or the BDOS
RSX_PREV        = 12                   ; the previous entry's address
RSX_REMOVE      = 14                   ; $FF: remove at the next warm boot
RSX_NONBANK     = 15                   ; $FF: load only in a non-banked system
RSX_NAME        = 16                   ; 8 bytes, space padded
RSX_LOADER      = 24                   ; the loader's flag
RSX_PREFIX_LEN  = 27                   ; 2 bytes reserved before here

; RSX_PREFIX name, entry, remove{, nonbank}: the prefix, in the RSXHDR
; segment that cfg/rsx.cfg puts first in the image. name is up to 8
; characters (space padded); entry the RSX's code, where every BDOS call
; arrives; remove the remove flag as the file has it: $FF for an RSX that
; serves one program (the warm boot after it removes it), 0 for one that
; stays until it sets $FF itself; nonbank $FF for an RSX that loads only in
; a non-banked system (OODOS is banked: it is passed over), 0 if omitted.
; The loader writes the serial number, offset 9's operand, offset 12 and
; the loader flag. The RSX passes on what it doesn't take, and makes its own
; BDOS calls, by JMP or JSR to its offset 9 (RSX_NEXT), never $0203.
.macro RSX_PREFIX name, entry, remove, nonbank
        .assert .strlen(name) >= 1 && .strlen(name) <= 8, error, "an RSX's name is 1-8 characters"
        .pushseg
        .segment "RSXHDR"
        .byte   0, 0, 0, 0, 0, 0        ; the serial number (the loader's)
        jmp     entry                   ; RSX_START
        .byte   $4C, 0, 0               ; RSX_NEXT: JMP to the next (the loader's)
        .word   0                       ; RSX_PREV (the loader's)
        .byte   remove                  ; RSX_REMOVE
.ifblank nonbank
        .byte   0                       ; RSX_NONBANK
.else
        .byte   nonbank
.endif
        .byte   name                    ; RSX_NAME
        .res    8 - .strlen(name), ' '
        .byte   0                       ; RSX_LOADER (the loader's: 0)
        .byte   0, 0                    ; reserved
        .popseg
.endmacro

; The attached RSX table (GENCOM, hw/oodos.md "The program header"): 16-byte
; entries from the header's OH_RSXREC record, OH_RSXCOUNT of them.
RT_REC          = 0                    ; the record the RSX's file starts at (2 bytes)
RT_FLAGS        = 2
RT_NONBANK      = $01                  ;   load only in a non-banked system: passed over
RT_SCB          = $02                  ;   not an RSX: an SCB byte to set (ours)
RT_NAME         = 3                    ; 8 bytes, space padded
RT_LEN          = 16
RT_SCBOFF       = 0                    ; an SCB entry's offset and value, in place of RT_REC
RT_SCBVAL       = 1

; The function bytes of 60 OODOS's RSXs take (rsx.inc's RSXs, GET.COM, PUT.COM) (128-255, the system's: the
; Programmer's Guide, bdos-60), the block's first byte:

RXF_GETFILE     = $80                  ; GET.COM: an input file (GET's RSX)
RXF_GETSTOP     = $81                  ; GET CONSOLE: the newest active GET's ends
RXF_PUTFILE     = $90                  ; PUT.COM: an output file (PUT's RSX)
RXF_PUTSTOP     = $91                  ; PUT ... TO CONSOLE/PRINTER: the active PUT of the kind ends
RXF_PUTASK      = $92                  ; is a PUT of the kind sending output to a file?
RXF_PUTCMD      = $93                  ; PUT.COM runs: a PUT armed for the next command waits for the one after
RXF_SUBFILE     = $A0                  ; SUBMIT.COM: its temporary file (SUBMIT's RSX)
RXF_SUBASK      = $A1                  ; does a SUBMIT RSX serve a file?

SCB_CCPST       = $19                  ; the CCP's state: bit 7 it runs, bit 6 a command does,
RX_CCP          = $80                  ;   bits 0-5 the commands' count (hw/oodos.md, 5a, 5b)
RX_CMD          = $40
RX_COUNT        = $3F

; The base page: CP/M's page 0 at $0200.
BP_WBOOT        = $0200                ; JMP warm boot
BP_BDOS         = $0203                ; JMP to the BDOS or the top RSX
BP_TOP          = $0204                ; its operand: the top of the TPA + 1
BP_FCB          = $025C                ; the default FCB
BP_FCB2         = $026C                ; the second FCB
BP_DMA          = $0280                ; the default DMA buffer, the command tail
BP_TPA          = $0300                ; where a .COM's image runs
WBOOT           = BP_WBOOT
BDOS            = BP_BDOS

; What OODOS's services are (hw/oolib.md, question 1): function 80's FT_INFO,
; A = OOLIB's interface version (1 from L1, 2 from L2, 3 from L3, 4 from L4), X = a bit for each
; service area. An OODOS without them answers 80 with A = X = $FF.
OA_FRAME        = $01                  ;   80, frame and timing
OA_INPUT        = $02                  ;   81, input (L2)
OA_SOUND        = $04                  ;   82, sound (L3)
OA_DMA          = $08                  ;   83, DMA and loading (L4)
OA_VIDEO        = $10                  ;   84, video memory (not yet)

; Function 80's operations. X = 0: A is the operation, with no block. Else
; A/X is a block, its +0 the operation and its parameters after.
FT_INFO         = 0                    ; A = OOLIB's version, X = OA_*; a block gets +1 those, +3 FC_*, +4 layout
FT_COUNT        = 1                    ; A/X = the ms counter's low word; a block gets +1-4 ms, +5-8 frames
FT_OPEN         = 2                    ; block: +1-2 the event area, +3 its size (EV_LEN at least; 0 is 256)
FT_CLOSE        = 3                    ; the event area closed
FT_WAIT         = 4                    ; the next frame; a block's +1 a count of frames; A/X = frames
FT_DELAY        = 5                    ; block: +1-2 a count of milliseconds
FT_FRAMEHOOK    = 6                    ; block: +1-2 the frame hook, 0 none; no block: none
FT_TICKHOOK     = 7                    ; block: +1-2 the tick hook, 0 none, +3-4 every n ms; no block: none
FT_COSTS        = 8                    ; block: +1-2, +3-4 each hook's most cycles, +5-6 frame hooks missed
FT_OPS          = 9
FC_FRAME        = $01                  ; FT_INFO's A, EV_CLOCKS: the frame counter runs (a video card)
FC_MS           = $02                  ;   the millisecond counter runs (the serial card's VIA)

; The event area (FT_OPEN): the program's memory, which OODOS's interrupt
; handlers write. Layout 1 (L1); a later layout only adds after EV_LEN. An
; area of EV_LEN2 bytes or more is layout 2 (L2, input), less layout 1.
EV_LAYOUT       = 0                    ; the layout OODOS writes: 1 or 2
EV_CLOCKS       = 1                    ; FC_* as at the open
EV_SIZE         = 2                    ; the size it was opened with (0: 256)
EV_MS           = 4                    ; the millisecond counter, 4 bytes, low first
EV_FRAMES       = 8                    ; the frame counter, 4 bytes
EV_VBGO         = 12                   ; the vertical-blank copies' go bits (L4): bit n starts slot n
                                       ;   (set it with TSB; OODOS clears it when the copy is done)
EV_LEN          = 16                   ; layout 1's size: +3 and +13-15 reserved, 0
EV_LAYOUT1      = 1
; Layout 2 (L2): input. Reserved bytes are 0.
EV_QHEAD        = 16                   ; the queue: the next slot OODOS writes (OODOS's)
EV_QTAIL        = 17                   ;   the next slot the program reads (the program's)
EV_QSLOTS       = 18                   ;   its slots, 8 bytes each from EV_QUEUE; one stays empty
EV_QLOST        = 20                   ;   events lost to a full queue, 2 bytes (the program may clear it)
EV_MODS         = 24                   ; the modifiers held, MODS's bits (L-Ctrl, L-Shift, L-Alt, L-GUI, then right)
EV_DEVICES      = 25                   ; what is plugged in: ED_* (the USB card's DEVICES)
EV_MX           = 32                   ; the mouse: X, 2 bytes (relative mode: movement summed, wrapping)
EV_MY           = 34                   ;   Y, 2 bytes
EV_MBUTTONS     = 36                   ;   its buttons, bits 0-4
EV_MWHEEL       = 37                   ;   the wheel's steps, signed, summed until the program clears it
EV_CTL          = 40                   ; four controllers, EC_LEN bytes each: controller 1 first
EV_PADDLES      = 56                   ; the game port's four paddles, 0-255
EV_KEYS         = 64                   ; the keys held: 32 bytes, bit (usage AND 7) of byte usage / 8
EV_SOUND        = 96                   ; the sound part (L3), 32 bytes: written while the stream is claimed
EV_SSTATE       = 96                   ;   the stream's state (SOUND_STATE's: 0 idle ... 5 ended, $80 up errors)
EV_SBUFS        = 97                   ;   the two buffers: bit 0 buffer 0 free, bit 1 buffer 1 (function 82)
EV_SFREE        = 98                   ;   the FIFO's room, 256-byte units, 2 bytes
EV_SPOS         = 100                  ;   the frames played, 4 bytes (SOUND_POS)
EV_SKBPS        = 104                  ;   an MP3's bitrate, kbit/s, 2 bytes
EV_SUNDER       = 106                  ;   underruns seen, 2 bytes (from the stream's start); +108-127 reserved, 0
EV_QUEUE        = 128                  ; the queue's slots
EV_LEN2         = EV_QUEUE + 16        ; layout 2's least size: two slots (EQ_LEN each), one event waiting
EV_LAYOUT2      = 2
EC_BITS         = 0                    ; a controller: CB_* now
EC_X            = 1                    ;   X and Y, 0-255 (a digital stick: 0, 128 or 255)
EC_Y            = 2
EC_HW           = 3                    ;   which hardware it is: CH_*
EC_LEN          = 4
CB_UP           = $01                  ; a controller's bits: the directions,
CB_DOWN         = $02
CB_LEFT         = $04
CB_RIGHT        = $08
CB_FIRE         = $10                  ;   fire buttons from bit 4 (the game port's three: bits 4-6)
CH_STICK1       = 1                    ; the hardware: the DE-9 on the VIA's port A,
CH_STICK2       = 2                    ;   the DE-9 on port B,
CH_GAMEPORT     = 3                    ;   the USB card's game port (paddles 0 and 1 as X and Y),
CH_GAMEPAD      = 4                    ;   a USB gamepad (reserved: nothing plugged in reads 0, X and Y 128)
GP_LOW          = 64                   ; the game port's directions: a paddle below 64 is up or left,
GP_HIGH         = 192                  ;   192 or more down or right
ED_KEYBOARD     = $01                  ; EV_DEVICES
ED_MOUSE        = $02
ED_GAMEPAD      = $04
; An event in the queue: EQ_LEN bytes.
EQ_KIND         = 0                    ; EK_*
EQ_CODE         = 1                    ; the key's HID usage, the button (0-4), the controller (1-4); else 0
EQ_VALUE        = 2                    ; 1 pressed, 0 released; a controller's new CB_* bits; the wheel's
                                       ; steps (signed); the devices now (ED_*)
EQ_MODS         = 3                    ; the modifiers held then
EQ_MS           = 4                    ; the millisecond counter then, 4 bytes
EQ_LEN          = 8
EK_KEY          = 1                    ; a key pressed or released
EK_BUTTON       = 2                    ; a mouse button
EK_WHEEL        = 3                    ; the mouse's wheel
EK_CTL          = 4                    ; a controller's bits changed
EK_DEVICE       = 5                    ; a device plugged in or out
EK_SOUND        = 6                    ; a byte from the card's 6510 (the mailbox, L3): code 0, value the byte

; Function 81's operations (L2, input), as 80's: X = 0, A is the operation;
; else A/X is a block, its +0 the operation. A = X = $FF for one that isn't,
; a block needed and not given, or what it can't do (no USB card).
IN_INFO         = 0                    ; A = ED_* plugged in, X = the layout L2 writes (2); a block +1, +2 those
IN_FLUSH        = 1                    ; the queue emptied (its tail put at the head), its lost count 0
IN_MAP          = 2                    ; block: +1-4 get the hardware (CH_*) of controllers 1-4
IN_SWAP         = 3                    ; block: +1, +2 two controllers (1-4) whose hardware is swapped
IN_MOUSE        = 4                    ; block: +1 the mode (0 relative, 1 absolute), +2-3 X's limit, +4-5 Y's
IN_LEDS         = 5                    ; block: +1 the keyboard's LEDs (bit 0 Num, 1 Caps, 2 Scroll Lock)
IN_REPEAT       = 6                    ; block: +1 the console's key repeat: bits 0-3 rate, 4-5 delay (REPEAT)
IN_NEXT         = 7                    ; block: +1-8 get the next event, taken; A = its kind, 0 if none waits
IN_OPS          = 8

; Function 82's operations (L3, sound), as 80's: X = 0, A is the operation;
; else A/X is a block, its +0 the operation. A = X = $FF for one that isn't,
; a block needed and not given, a part not claimed, or what it can't do;
; A = $FE, X = $FF (SN_BUSY) when DMA channel 2 is busy with the stream: try
; again later. [part] is the claim (SC_*) an operation needs.
SN_INFO         = 0                    ; A = the card's revision (0 none), X = its CODECS (0 on revision 1);
                                       ;   a block +1, +2 those, +3 the parts (SC_*) no one has claimed
SN_CLAIM        = 1                    ; block: +1 the parts (SC_*), all of them or none
SN_RELEASE      = 2                    ; block: +1 the parts, each stopped and given back
SN_LOAD         = 3                    ; block: +1-3 a physical address, +4-5 the card's RAM address, +6-7 the
                                       ;   length (0: 65,536): the card's RAM loaded by DMA [SC_6510]
SN_START        = 4                    ; block: +1-2 the 6510's entry, +3 CTRL's other bits (NTSC, $08, BOTH):
                                       ;   the vector written, the 6510 reset and run [SC_6510]
SN_STOP         = 5                    ; the 6510 stopped, the SIDs silenced [SC_6510]
SN_SEND         = 6                    ; block: +1 a byte to the 6510's mailbox [SC_6510]
SN_STREAM       = 7                    ; block: +1 the codec, +2-3 the rate, +4 the format: a stream started
                                       ;   afresh, both buffers free [SC_STREAM]
SN_BUFFER       = 8                    ; block: +1-3 a physical address, +4-5 its length (0: 65,536): A = the
                                       ;   buffer it took (0 or 1), sent to the card by DMA [SC_STREAM]
SN_PAUSE        = 9                    ; [SC_STREAM]
SN_RESUME       = 10                   ; [SC_STREAM]
SN_SEEK         = 11                   ; the FIFO emptied (FLUSH) and both buffers dropped, free [SC_STREAM]
SN_END          = 12                   ; no more data (EOF): what is in plays out [SC_STREAM]
SN_STATUS       = 13                   ; A = the state, X = the buffers free (EV_SBUFS); a block +1 those,
                                       ;   +3-4 the FIFO's room, +5-8 frames, +9-10 kbit/s, +11-12 underruns
SN_WAVE         = 14                   ; block: +1-3 a physical address, +4-6 the wave RAM address (17 bits),
                                       ;   +7-8 the length (0: 65,536): wave RAM loaded by DMA [SC_DOC]
SN_DOCHOOK      = 15                   ; block: +1-2 the ES5503 hook, 0 none; +3 a zero-page byte ($00-$EF)
                                       ;   that gets its interrupt register ($E0) at each call; no block: none [SC_DOC]
SN_BEEP         = 16                   ; block: +1-2 the pitch in Hz (20-20,000), +3-4 the length in ms
SN_HOLD         = 17                   ; the 6510 held (RUN off, no reset), each SID's $D418 volume nibble 0,
                                       ;   the bytes kept; refused if it isn't running [SC_6510]
SN_GO           = 18                   ; a hold undone: the $D418s back, RUN on; refused if not held [SC_6510]
SN_VOLUME       = 19                   ; block: +1 VOLL, +2 VOLR, the master volumes (255 unity; revision 2)
SN_OPS          = 20
SN_BUSY         = $FE                  ; A when channel 2 is busy (X $FF)
SC_6510         = $01                  ; the parts: the 6510 and the SIDs,
SC_STREAM       = $02                  ;   the PCM stream (revision 2),
SC_DOC          = $04                  ;   the ES5503 (revision 2)

; Function 83's operations (L4, DMA and loading), as 80's: X = 0, A is the
; operation; else A/X is a block, its +0 the operation. Addresses are
; physical, 3 bytes (function 73 makes one of a CPU address). A destination
; must be memory a DMA segment may reach (51) and not the console's video
; memory, units 0-12 ($80000-$833FF). A = X = $FF for one that isn't, a
; block needed and not given, or a refusal: nothing done.
DM_INFO         = 0                    ; A = the first video unit a program may use (13), X = the slots (4);
                                       ;   a block +1, +2 those, +3 the slots' budget in 256-byte pages (4),
                                       ;   +4 the pointer's sprite ($FF off), +5-12 the W1 map: bit n & 7
                                       ;   of byte n / 8 set for each block n that OOLIB may put in W1 now
DM_LOAD         = 1                    ; block: +1-2 an open FCB, +3-5 its first record, +6-7 a count of
                                       ;   records (1-512), +8-10 the destination: A = 0; 1 the file ended
                                       ;   or a block was never written; else 33's error. +6-7 rewritten
                                       ;   with the records loaded
DM_COPY         = 2                    ; block: +1-3 the source, +4-6 the destination, +7-8 the length
                                       ;   (0: 65,536), by DMA, in the BDOS's time
DM_FILL         = 3                    ; block: +1 the byte, +2-4 the destination, +5-6 the length (0: 65,536)
DM_VBSET        = 4                    ; block: +1 a slot (0-3), +2-4 the source, +5-7 the destination, +8-9
                                       ;   the length (1-1,024, the four 1,024 at most; 0 frees it): copied at
                                       ;   the vertical blank after the program sets the slot's EV_VBGO bit
DM_POINTER      = 5                    ; block: +1 a sprite (0-63; $FF off), +2 and +3 the hotspot: its X and
                                       ;   Y set from the mouse's at each vertical blank (a layout-2 area)
DM_CONSOLE      = 6                    ; the console's screen shown again, as the warm boot shows it
DM_OPS          = 7
DM_UNIT0        = 13                   ; DM_INFO's A: units 0-12 are the console's (OODOS's)
DM_SLOTS        = 4                    ; DM_INFO's X
DM_BUDGET       = 1024                 ; the slots' bytes, all four

; BDOS functions, in Y. CP/M 3's, then CP/M-86's memory functions, then
; OODOS's own from 72.
P_TERMCPM       = 0
C_READ          = 1
C_WRITE         = 2
A_READ          = 3
A_WRITE         = 4
L_WRITE         = 5
C_RAWIO         = 6
A_STATIN        = 7
A_STATOUT       = 8
C_WRITESTR      = 9                    ; the string ends with '$'
C_READSTR       = 10
C_STAT          = 11
S_BDOSVER       = 12
DRV_ALLRESET    = 13
DRV_SET         = 14
F_OPEN          = 15
F_CLOSE         = 16
F_SFIRST        = 17
F_SNEXT         = 18
F_DELETE        = 19
F_READ          = 20
F_WRITE         = 21
F_MAKE          = 22
F_RENAME        = 23
DRV_LOGINVEC    = 24
DRV_GET         = 25
F_DMAOFF        = 26
DRV_ALLOCVEC    = 27
DRV_SETRO       = 28
DRV_ROVEC       = 29
F_ATTRIB        = 30
DRV_DPB         = 31
F_USERNUM       = 32
F_READRAND      = 33
F_WRITERAND     = 34
F_SIZE          = 35
F_RANDREC       = 36
DRV_RESET       = 37
DRV_ACCESS      = 38                   ; MP/M's: here, as CP/M 3, nothing done, A = 0
DRV_FREEDRV     = 39                   ; MP/M's: here, as CP/M 3, nothing done, A = 0
F_WRITEZF       = 40
F_LOCK          = 42
F_UNLOCK        = 43
F_MULTISEC      = 44
F_ERRMODE       = 45
DRV_SPACE       = 46
P_CHAIN         = 47
DRV_FLUSH       = 48
S_SCB           = 49
S_BIOS          = 50
F_DMASEG        = 51
F_DMAGET        = 52
MC_MAX          = 53
MC_ABSMAX       = 54
MC_ALLOC        = 55
MC_ABSALLOC     = 56
MC_FREE         = 57
MC_ALLFREE      = 58
P_LOAD          = 59
P_RSX           = 60
DRV_FREE        = 98
F_TRUNCATE      = 99
DRV_SETLABEL    = 100
DRV_GETLABEL    = 101
F_TIMEDATE      = 102
F_WRITEXFCB     = 103
T_SET           = 104
T_GET           = 105
F_PASSWD        = 106
S_SERIAL        = 107
P_CODE          = 108
C_MODE          = 109
C_DELIMIT       = 110
C_WRITEBLK      = 111
L_WRITEBLK      = 112
F_PARSE         = 152
O_VERSION       = 72                   ; OODOS's own
O_PHYSADDR      = 73
O_MOUNT         = 74
O_ROM           = 75                   ; leave OODOS for the ROM: A = 0 its monitor, 1 Apple II mode
O_SECTOR        = 76                   ; an Apple disk's DOS 3.3 sector (6d)
O_FRAME         = 80                   ; frame and timing (OOLIB's first area: hw/oolib.md, L1)
O_INPUT         = 81                   ; input (OOLIB's second: hw/oolib.md, L2)
O_SOUND         = 82                   ; sound (OOLIB's third: hw/oolib.md, L3)
O_DMA           = 83                   ; DMA and loading (OOLIB's fourth: hw/oolib.md, L4)

.endif

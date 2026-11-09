; OODOS.SYS's own layout (hw/oodos.md, "The system, phase 2"): what the
; BDOS, the BIOS, common memory and the resident code share. system.inc has
; what OOLDR and the tests share too; backplane.inc the cards' registers.
;
; The system block (31) is W0 while the BDOS or the BIOS runs:
;
;   $0000-$00EF  zero page, handed out by ld65 (sys.cfg's ZP)
;   $00F0-$00FF  the BDOS stub's copy of the program's state, and the
;                BIOS's two word parameters (fixed: common memory and
;                both blocks use them)
;   $0100-$01FF  the system's stack, fresh at each BDOS entry
;   $0200-$0263  the SCB, CP/M 3's offsets
;   $0300-$07FF  the BDOS's and the BIOS's variables (sys.cfg's SYSBSS)
;   $0800-$09FF  SECBUF: a sector, for IDENTIFY, M:'s format and READ or
;                WRITE through a buffer that crosses a window
;   $0A00-$0A0F  the way to the monitor (common.s: TRAMP), written there
;                each time
;   $0B00-$0CFF  function 10's line and the previous line (bdossys.s)
;   $1000-$2FFF  the drives' allocation vectors (ALV), 1 KB a drive's slot
;                (bdosdisk.s), made at each login
;   $3000-$37FF  and O:'s and P:'s (slots 8 and 9, 6c)
;
; Cold boot clears $0000-$0CFF.

.include "backplane.inc"                ; first: system.inc defers to it
.include "system.inc"

; The program's state while it is in the BDOS (common.s's stub)
ST_A            = $F0                  ; A and X as the program passed them
ST_X            = $F1
ST_S            = $F2                  ; its S after the stub's PHP
ST_WIN          = $F3                  ; its WIN0-WIN3, in order
ST_W0           = ST_WIN
ST_W1           = ST_WIN + 1
ST_W2           = ST_WIN + 2
ST_W3           = ST_WIN + 3
; The BIOS's word parameters, beside A, X and Y (bios.s's header): function
; 50's +4-5 and +6-7.
BW1             = $F8
BW2             = $FA
; The jump table's entries (common.s): W2 as its caller had it, which bank
; 0's $8000-$BFFF means to the BIOS (bios.s's PHYSOF), and A for a moment.
CALLW2          = $FC
JTA             = $FD

; A jump table entry as an operand: system.inc's BIOS_ENTRY is in brackets,
; which ca65 takes as JMP's and JSR's indirect mode.
.define JT(n) COMMON + 3 * (n)

SCB             = $0200
SECBUF          = $0800
SECBUF_PHYS     = SYS_BLOCK * $4000 + SECBUF
DPH_DPB         = 12                   ; a DPH's DPB pointer (bios.s)
DPB_BSH         = 2                    ; a DPB's BSH, and its DSM (a word)
DPB_DSM         = 5
TRAMP           = $0A00
LINEBUF         = $0B00                ; function 10's line, 255 bytes
PREVBUF         = $0C00                ; the previous line
SYS_CLEAR_END   = $0D00

; The SCB's fields (CP/M 3's offsets: the Programmer's Guide, Appendix A)
SCB_VERSION     = SCB + $05            ; $31
SCB_USERFLAGS   = SCB + $06            ; 4 bytes, the program's
SCB_RETCODE     = SCB + $10            ; the program return code (108)
SCB_CCPFLAGS    = SCB + $17            ; OODOS's, in a reserved byte: function 47's
CCP_CHAIN       = $80                  ;   a command waits at $0280
CCP_CHAINDU     = $40                  ;   with the program's drive and user
CCP_CMD         = $40                  ; SCB $19 (loader.s's CCPST): bit 7 the CCP runs, bit 6 a
CCP_COUNT       = $3F                  ;   command runs, bits 0-5 the count of commands started
SCB_CCPDISP     = SCB + $18            ; OODOS's, reserved in CP/M 3: bit 7 SETDEF's [DISPLAY]
SCB_WIDTH       = SCB + $1A            ; console width - 1
SCB_COLUMN      = SCB + $1B            ; the console's column
SCB_PAGELEN     = SCB + $1C
SCB_CIVEC       = SCB + $22            ; redirection vectors: bit 15 device 0
SCB_COVEC       = SCB + $24
SCB_AIVEC       = SCB + $26
SCB_AOVEC       = SCB + $28
SCB_LOVEC       = SCB + $2A
SCB_PAGEMODE    = SCB + $2C
SCB_CTLH        = SCB + $2E            ; $FF: ^H is rub/del (echoes what it takes)
SCB_RUBOUT      = SCB + $2F            ; $FF: rub/del is ^H (takes back, erased)
SCB_CONMODE     = SCB + $33            ; the console mode (109), a word
CM_CTLC         = $01                  ;   11 reports only ^C
CM_NOSTOP       = $02                  ;   no ^S, ^Q, ^P handling
CM_RAW          = $04                  ;   no tabs or printer echo for 2, 9, 111
CM_NOCTLC       = $08                  ;   no ^C ending the program
SCB_DELIM       = SCB + $37            ; function 9's end, '$'
SCB_LISTFLAG    = SCB + $38            ; printer echo (^P): non-zero on
SCB_DMA         = SCB + $3C            ; the program's DMA address
SCB_DISK        = SCB + $3E            ; the current drive
SCB_USER        = SCB + $44
SCB_MULTI       = SCB + $4A            ; the multi-sector count, 1-128: 44 sets it, 49 too; 1 by default
SCB_ERRMODE     = SCB + $4B            ; function 45's: $FF return, $FE return and say, else default
SCB_DRVCHAIN    = SCB + $4C            ; the drive search chain, 4 bytes, $FF ends it
SCB_TEMPDRV     = SCB + $50
SCB_ERRDRV      = SCB + $51            ; the drive of the last error
SCB_FLAGS       = SCB + $57            ; bit 7: the long error messages
SCB_DATE        = SCB + $58            ; days, 1 January 1978 day 1
SCB_HOUR        = SCB + $5A            ; BCD
SCB_MIN         = SCB + $5B
SCB_SEC         = SCB + $5C
SCB_COMBASE     = SCB + $5D            ; common memory's base, $F800
SCB_LEN         = $64

; Character devices (DEVTBL's order) and their bits in a vector's high byte
DEV_CRT         = 0
DEV_TTY         = 1
DEV_LPT         = 2
DEV_COUNT       = 3
VEC_CRT         = $80
VEC_TTY         = $40
VEC_LPT         = $20

; Drives
DRV_M           = 12                   ; M:, the RAM disk
DRV_O           = 14                   ; O: and P:, the Disk II's drives 1 and 2 (6c)
DRV_P           = 15
SD_DRIVES       = 7                    ; A-G, units 1-7
SD_SECTORS_8M   = 16384
SD_SECTORS_32M  = 65536
SD_SECTORS_APPLE = 280                 ; a 140 KB .dsk: apple-do (cpmtools's, the SoftCard's)
SPT_PHYS        = 16                   ; 512-byte sectors a track, OODOS's own formats
SPT_APPLE       = 8                    ; apple-do's: 16 of 256 bytes, two to a sector
APPLE_TRACKS    = 35

; The BDOS's memory in banks 6-7 (hw/oodos.md, "The BDOS, phase 3")
HASH_BLOCK      = 24                   ; 24-27: the directory hash, 8 KB a drive's slot
BCB_BLOCK       = 28                   ; 32 sector buffers of 512 bytes
TRKBUF_PHYS     = 29 * $4000           ; block 29: the BIOS's apple-do track buffers, 4 KB each: an SD unit's, O:'s, P:'s
DIRBUF_PHYS     = 30 * $4000           ; block 30: O:'s and P:'s directory-track buffers, 4 KB each (6c)
DIR_TRACK       = 3                    ; apple-do's directory track: its first after the 3 boot tracks
RHASH_BLOCK     = 29                   ; and from $3000 O:'s and P:'s directory hashes, 256 bytes each (6c)
RHASH_BASE      = $3000
ALV_BASE        = $1000                ; the allocation vectors, in the system block
AMAP_LEN        = 20                   ; the memory functions' blocks: banks 1-5 (bdosmem.s)

; Versions
OODOS_MAJOR     = SYS_MAJOR
OODOS_MINOR     = SYS_MINOR
BIOS_MAJOR      = 0
BIOS_MINOR      = 2

; The clocks (irq.s; hw/oolib.md, "The plan", L1)
TICK_CYCLES     = 7159                 ; the CPU's cycles a millisecond, 7,159,090 Hz: timer 1's period
TICK_LATCH      = TICK_CYCLES - 2      ; a free-running period is the latch + 2
CLK_FRAME       = VIDEO_IRQ_VBLANK     ; CLOCKS: the frame counter runs (VIDEO_IEN's bit, for crt.s)
CLK_MS          = $02                  ; CLOCKS: the millisecond counter runs
OOLIB_IFACE     = 5                    ; OOLIB's interface version: function 80's FT_INFO (L2: 2, L3: 3, L4: 4, L4b: 5)
OOLIB_AREAS     = $0F                  ; the service areas there are (FT_INFO's): frame and timing (80),
                                       ; input (81), sound (82), DMA and loading (83)
EVON_L2         = $80                  ; EVON: a layout-2 area open (1: layout 1)
PAD_MS          = 4                    ; the paddles read this many ms after PTRIG (255 counts: 2.8 ms)
UBATCH          = 16                   ; the USB FIFO's events taken an interrupt at most (input.s)
R62_DEAF        = 0                    ; irq.s's CALL62: input.s's R62TAB, DEAFEN
R62_OTHER       = 2                    ;   IRQOTH: a card's interrupt not the VIA's or the video card's
R62_TICK        = 4                    ;   L2TICK: the sticks and the paddles
;   (the debug build's, below)
R61_IRQ         = 0                    ; irq.s's CALL61: sound.s's R61TAB, SNDIRQ (block 61)
R61_FRAME       = 2                    ;   SFIELDS: the event area's sound part
R61_QUIET       = 4                    ;   SNDQUIET: the warm boot's
R61_INIT        = 6                    ;   SNDINIT: cold boot's
R61_VB          = 8                    ;   VBWORK: the vertical-blank copies and the pointer (video.s, L4)
R61_WARM        = 10                   ;   VBQUIET: the warm boot's video part
R62_HKON        = 6                    ;   a debug build's HKON and HOOKCOST: a hook's cycles
R62_HKCOST      = 8                    ;   counted (irq.s)

; A hook's record in irq.s's HK (L1; the ES5503's, L3)
HK_ON           = 0                    ; 1: set
HK_ADDR         = 1                    ; its address less 1, for RTS
HK_WIN          = 3                    ; its windows, W0-W3
HK_S            = 7                    ; the dispatcher's S while it runs
HK_C0           = 8                    ; debug: timer 1's count at its call,
HK_M0           = 10                   ;   and MS's low byte
HK_MAX          = 11                   ;   the most cycles it took, since the warm boot
HK_ZP           = 13                   ; the ES5503 hook's: the byte of its zero page that gets $E0's value
HK_LEN          = 16
HF              = 0                    ; the frame hook's
HT              = HK_LEN               ; the tick hook's
HD              = 2 * HK_LEN           ; the ES5503 hook's (sound.s): called masked, in HT's place

; BIOS banks (SETBNK, SELMEM, XMOVE)
BANK_SYS        = 0                    ; the system's windows: 31, 60, 61, 62
BANK_PROG       = 1                    ; the program's, as the stub saved them

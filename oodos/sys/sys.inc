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
;   $1000-$2FFF  the drives' allocation vectors (ALV), 1 KB a drive's slot
;                (bdosdisk.s), made at each login
;
; Cold boot clears $0000-$0AFF.

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
TRAMP           = $0A00
SYS_CLEAR_END   = $0B00

; The SCB's fields that OODOS keeps (CP/M 3's offsets)
SCB_VERSION     = SCB + $05            ; $31
SCB_WIDTH       = SCB + $1A            ; console width - 1
SCB_COLUMN      = SCB + $1B            ; the console's column
SCB_PAGELEN     = SCB + $1C
SCB_CIVEC       = SCB + $22            ; redirection vectors: bit 15 device 0
SCB_COVEC       = SCB + $24
SCB_AIVEC       = SCB + $26
SCB_AOVEC       = SCB + $28
SCB_LOVEC       = SCB + $2A
SCB_DELIM       = SCB + $37            ; function 9's end, '$'
SCB_DMA         = SCB + $3C            ; the program's DMA address
SCB_DISK        = SCB + $3E            ; the current drive
SCB_USER        = SCB + $44
SCB_ERRMODE     = SCB + $4B            ; function 45's: $FF return, $FE return and say, else default
SCB_DATE        = SCB + $58            ; days, 1 January 1978 day 1
SCB_HOUR        = SCB + $5A            ; BCD
SCB_MIN         = SCB + $5B
SCB_SEC         = SCB + $5C
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
SD_DRIVES       = 7                    ; A-G, units 1-7
SD_SECTORS_8M   = 16384
SD_SECTORS_32M  = 65536
SPT_PHYS        = 16                   ; 512-byte sectors a track, every format

; The BDOS's memory in banks 6-7 (hw/oodos.md, "The BDOS, phase 3")
HASH_BLOCK      = 24                   ; 24-27: the directory hash, 8 KB a drive's slot
BCB_BLOCK       = 28                   ; 32 sector buffers of 512 bytes
ALV_BASE        = $1000                ; the allocation vectors, in the system block

; Versions
OODOS_MAJOR     = SYS_MAJOR
OODOS_MINOR     = SYS_MINOR
BIOS_MAJOR      = 0
BIOS_MINOR      = 2

; BIOS banks (SETBNK, SELMEM, XMOVE)
BANK_SYS        = 0                    ; the system's windows: 31, 60, 61, 62
BANK_PROG       = 1                    ; the program's, as the stub saved them

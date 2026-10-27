; OODOS's system side (hw/oodos.md, "The system, phase 2"): the blocks, common
; memory, OODOS.SYS's header and the BIOS's jump table. OOLDR, OODOS.SYS and
; the tests share these; programs use oodos.inc.

.ifndef SYSTEM_INC
SYSTEM_INC = 1

; The north bridge's windows (hw/design.md, "North bridge registers"),
; unless backplane.inc, included first, has them already
.ifndef NB_WIN0
NB_WIN0         = $C680
NB_WIN1         = $C681
NB_WIN2         = $C682
NB_WIN3         = $C683
NB_VEC          = $C684
NB_RO           = $40                  ; a window's read-only bit
.endif
ROM_BLOCK       = 63                   ; the ROM BIOS's RAM copy, the monitor's

; Blocks
PROG_BLOCK0     = 0                    ; a program's $0000-$BFFF: blocks 0-2
SYS_BLOCK       = 31                   ; the system's zero page, stack, data, SCB
BDOS_BLOCK      = 60
BIOS_BLOCK      = 61
RES_BLOCK       = 62                   ; W3: resident code and common memory
MDISK_BASE      = $C0000               ; M:, banks C-D
MDISK_SIZE      = $20000

; The CPU's view
BDOS_ENTRY      = $BFFD                ; JMP to the stub; $0204 holds this
TPA_TOP         = BDOS_ENTRY - 1
CODE_WINDOW     = $C800                ; $C800-$F7FF
COMMON          = $F800                ; $F800-$FFFF, in every W3 block

; The BIOS's jump table, at COMMON: CP/M 3's entries in CP/M 3's order
.enum BIOS
        BOOT
        WBOOT
        CONST
        CONIN
        CONOUT
        LIST
        AUXOUT
        AUXIN
        HOME
        SELDSK
        SETTRK
        SETSEC
        SETDMA
        READ
        WRITE
        LISTST
        SECTRN
        CONOST
        AUXIST
        AUXOST
        DEVTBL
        DEVINI
        DRVTBL
        MULTIO
        FLUSH
        MOVE
        TIME
        SELMEM
        SETBNK
        XMOVE
        COUNT
.endenum
.define BIOS_ENTRY(n) (COMMON + 3 * (n))

; OODOS.SYS's header, its first 128-byte record
SYS_MAGIC_0     = $4F                  ; "OOSY"
SYS_MAGIC_1     = $4F
SYS_MAGIC_2     = $53
SYS_MAGIC_3     = $59
SH_MAGIC        = 0
SH_MAJOR        = 4
SH_MINOR        = 5
SH_ENTRY        = 6                    ; the cold boot, a CPU address in block 62
SH_COUNT        = 8                    ; segments, 1-8
SH_SEGMENTS     = 16                   ; 8 bytes each:
SS_ADDR         = 0                    ;   physical address, 3 bytes
SS_LENGTH       = 3                    ;   bytes, at most 16 KB
SS_RECORD       = 5                    ;   the record of the file it starts at
SS_LEN          = 8
SYS_MAX_SEGMENTS = 8
SYS_MAJOR       = 0                    ; phase 2's
SYS_MINOR       = 2

; The CP/M file reader's errors (sys/cpmread.s), in A with carry set
CRE_EOF         = 0                    ; CR_READ: past the file's last record
CRE_SD          = 1                    ; the card failed a command: CR_SDERR has its ERROR
CRE_UNIT        = 2                    ; CR_INIT: not a unit 1-7, or not mounted
CRE_SIZE        = 3                    ; CR_INIT: neither 16,384 sectors nor 65,536
CRE_NOFILE      = 4                    ; CR_FIND: no such file in user 0
CRE_BIG         = 5                    ; CR_FIND: more directory entries than it keeps (16)
CRE_BAD         = 6                    ; an entry missing, twice, part full or with a block out of range

.endif

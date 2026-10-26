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

.endif

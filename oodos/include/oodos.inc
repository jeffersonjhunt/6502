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
O_ROM           = 75                   ; leave OODOS for the ROM: A = 0 its monitor, 1 Apple II mode

.endif

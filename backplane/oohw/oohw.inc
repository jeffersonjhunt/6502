; oohw: the hardware library DIAG, SYSINFO and the ROM's diagnostics share
; (hw/diag.md, "The library: oohw.lib"). Each module is a file here,
; assembled into an ar65 archive, so a program takes only those it uses.
;
; Built twice from the same source: for OODOS (-D OOHW_OS, oodos/Makefile's
; build/oohw.lib: its zero page the program's, $00-$EF, and the 65C816 rules
; kept, since the rules gate checks the CODE of the program that links it)
; and, from D3, for the ROM core (-D OOHW_ROM). Include backplane.inc first.
;
; Every routine that prints calls HW_PUTC, which the program provides:
;
;   HW_PUTC     A, a character, written; keeps X and Y (SYSINFO's and
;               DIAG's: function 2, cmd.inc's CM_PUTC)
;
; The routines (A, X and Y not kept unless said; carry set for an error):
;
; Discovery (hwprobe.s, hwname.s)
;
;   HW_PROBE    every ID byte of modules 6-E read twice, by an absolute
;               load and by one indexed from the address less $FF (for an
;               even module, from the page before: a floating bus can't
;               give the same byte both times, hw/diag.md, "Card
;               discovery"); a card is found only if both reads agree and
;               the high nibble is its module. Out: HW_FOUND, a word, bit n
;               module n; HW_REV, 16 bytes, module n's revision, or $FF for
;               none; HW_IDB, 16 bytes, the first read of each, as it was
;   HW_NAME     A a module (0-15): A/X its name, a string ended by 0
;   HW_KNOWN    A a module, X a revision: carry clear if this library knows
;               that card at that revision (hw/diag.md's table)
;
; Time (hwclock.s)
;
;   HW_CLOCK    PHI2 cycles from a change of the byte at HW_WATCH to the
;               HW_EDGES-th change after it, counted by the VIA's timer 2
;               (the program's: it is set running from $FFFF, in the
;               one-shot mode OODOS's warm boot leaves, which counts on
;               past 0) and its wraps, so OODOS's interrupts in between
;               don't count against it. Out: HW_CYC, 4 bytes; carry set if
;               no change came within about 2.3 s (2^24 cycles). For the
;               DS3231: HW_WATCH NB_SEC, HW_EDGES 1. The poll loop is about
;               80 cycles, the measurement's grain
;   HW_FRAMEHZ  HW_CYC measured over 60 video frames (800 x 525 pixel
;               clocks at 25.175 MHz each) as the CPU's Hz: HW_CYC less
;               HW_CYC / 1008, into HW_NUM
;   HW_DIV      HW_NUM, 4 bytes, divided by A/X (1-65535): the quotient in
;               HW_NUM, the remainder in A/X
;
; Numbers (hwfmt.s)
;
;   HW_PUTS     the string at A/X, ended by 0: out Y its length
;   HW_HEX2     A in hex, two digits; keeps X and Y
;   HW_HEX4     A/X (low, high) in hex, four digits
;   HW_HEX6     A/X and Y (low, middle, high) in hex, six digits
;   HW_DEC      HW_NUM, 4 bytes, in decimal; carry set: with commas
;               (7,159,090). HW_NUM not kept
;
; The clock chip (hwrtc.s)
;
;   HW_RTC      the DS3231's status and temperature: HW_RTCST +0 RTCCTL,
;               +1 the temperature's register $11 (whole degrees, signed),
;               +2 its $12 (bits 7-6 quarters), each through RTCREG and
;               RTCDATA, BUSY waited for; carry set if BUSY stayed on
;
; The SD card (hwsdid.s)
;
;   HW_SD_ID    IDENTIFY's 512 bytes to HW_PTR's address, by the CPU through
;               DATA with CTRL's DMA bit clear (CTRL put back as it was), as
;               OODOS's own BIOS reads it, each wait about a second at most.
;               Carry set if not: A 0 no mass storage card, 1 no card in
;               the slot (or not started up), 2 the card stayed busy, else
;               $80 + the card's ERROR (an ERROR of 0: the command ended
;               with no sector)

.ifndef OOHW_INC
OOHW_INC = 1

.global HW_PUTC
.global HW_PROBE, HW_FOUND, HW_REV, HW_IDB, HW_NAME, HW_KNOWN
.global HW_CLOCK, HW_FRAMEHZ, HW_DIV, HW_WATCH, HW_EDGES, HW_CYC
.global HW_PUTS, HW_HEX2, HW_HEX4, HW_HEX6, HW_DEC, HW_NUM
.global HW_RTC, HW_RTCST
.global HW_SD_ID
.globalzp HW_PTR

HW_FIRST        = 6                    ; the modules with registers HW_PROBE reads: 6-E
HW_LAST         = $E
HW_NONE         = $FF                  ; HW_REV: no card
HW_SERIAL_ID    = $61                  ; the TL16C552A serial card's ID (hw/design.md; none on the stand-in)
HW_NB_ID        = $D1                  ; the north bridge's NB_ID, revision 1 (the emulator's BackplaneBridge::ID)

; HW_SD_ID's errors (A, carry set)
HE_NOCARD       = 0                    ; no mass storage card (its ID)
HE_EMPTY        = 1                    ; no SD card in the slot, or not started up
HE_BUSY         = 2                    ; the card stayed BUSY
HE_ERROR        = $80                  ; + the card's ERROR

; IDENTIFY's sector (sd_card.h; hw/design.md, "Mass storage registers")
SI_SECTORS      = $000                 ; the card's sectors, 4 bytes
SI_TYPE         = $004                 ; 0 none, 1 SDSC, 2 SDHC, 3 SDXC
SI_FLAGS        = $005                 ; bit 0 write-protected, bit 1 a FAT volume
SI_FIRMWARE     = $006                 ; major, minor
SI_NAME         = $010                 ; 32 bytes, NUL padded
SI_UNITS        = $040                 ; units 1-7, 32 bytes each:
SU_SECTORS      = 0                    ;   sectors, 4 bytes
SU_FLAGS        = 4                    ;   bit 0 mounted, bit 1 read-only
SU_NAME         = 8                    ;   the file's name, 24 bytes
SU_LEN          = 32
SIF_WP          = $01
SIF_FAT         = $02
SUF_MOUNTED     = $01
SUF_RO          = $02

.endif

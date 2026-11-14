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
;
; Memory (hwmem.s, hwport.s; hw/diag.md, "The memory tests"): a run is
; HW_PTR (a CPU address; not kept) and HW_LEN (1-65,535 bytes), mapped by
; the program; a failure is carry set, HW_BAD the address (3 bytes for a
; port), HW_GOT what was read, HW_WANT what was expected
;
;   HW_WALK     walking 1s and 0s at HW_PTR, the byte put back
;   HW_TAGW     a 16 KB block at HW_PTR (page aligned): HW_KEY at +0, and
;               HW_KEY XOR $FF XOR n at +2^n, n 0-13: the address lines
;   HW_TAGC     those read back (all blocks' written first)
;   HW_AIAW     address in address written: each byte its CPU address's low
;               byte XOR its page XOR HW_KEY
;   HW_AIAC     the same checked (all blocks' written first)
;   HW_MARCH    March C-, background HW_BG ($00, $55, $33, $0F)
;   HW_PAIAW    address in address through a data port that steps itself:
;               HW_PORT its register, HW_PLEN the pages; byte n is n's low
;               byte XOR its middle XOR its high XOR HW_KEY (the program
;               sets the port's address first)
;   HW_PAIAC    the same checked
;
; The report (hwline.s): one line a test, its result at column 65
;
;   HW_TEST     A/X the test's name, ended by 0: "  NAME " in a field of 7,
;               HW_COL 7. The program's HW_PUTC adds 1 to HW_COL for each
;               character (DIAG's does; SYSINFO's needn't)
;   HW_LINE     A the result (HL_*): a blank, dots to column 64, a blank and
;               the word; counted in HW_TALLY (4 bytes, HL_* each)
;   HW_FAULT    "read $gg, expected $ww: bit n" (bits n, m) from HW_GOT and
;               HW_WANT; the program writes where first
;   HW_SUMMARY  "SUMMARY  n tests: p passed, f failed, s skipped" and ", m
;               measured" if any were: A the failures
;
; The sound card's 6510 (card6510.s, its own program, not a module): the
; commands C65_* below; hw/diag.md, "The 6510 test program"

.ifndef OOHW_INC
OOHW_INC = 1

; The segments a module's code, constants, variables and zero page go in: OODOS's own for a program, and for
; the ROM (OOHW_ROM, hw/diag.md's plan 2) block 23's, with its variables in the core's zero page and stack page
; (the first 512 bytes of the RAM block stage 2 chose for W0, tested before anything is kept there).
.ifdef OOHW_ROM
.macro OOHW_CODE
        .segment "D23CODE"
.endmacro
.macro OOHW_RODATA
        .segment "D23CODE"
.endmacro
.macro OOHW_BSS
        .segment "D23BSS"
.endmacro
.macro OOHW_ZP
        .segment "D23ZP": zeropage
.endmacro
.else
.macro OOHW_CODE
        .code
.endmacro
.macro OOHW_RODATA
        .rodata
.endmacro
.macro OOHW_BSS
        .bss
.endmacro
.macro OOHW_ZP
        .zeropage
.endmacro
.endif

.global HW_PUTC
.global HW_PROBE, HW_FOUND, HW_REV, HW_IDB, HW_NAME, HW_KNOWN
.global HW_CLOCK, HW_FRAMEHZ, HW_DIV, HW_WATCH, HW_EDGES, HW_CYC
.global HW_PUTS, HW_HEX2, HW_HEX4, HW_HEX6, HW_DEC, HW_NUM
.global HW_RTC, HW_RTCST
.global HW_SD_ID
.global HW_WALK, HW_TAGW, HW_TAGC, HW_AIAW, HW_AIAC, HW_MARCH, HW_LEN, HW_KEY, HW_BG, HW_BAD, HW_GOT, HW_WANT
.global HW_PAIAW, HW_PAIAC, HW_PORT, HW_PLEN
.global HW_TEST, HW_LINE, HW_FAULT, HW_SUMMARY, HW_COL, HW_TALLY
.globalzp HW_PTR

HL_PASS         = 0                    ; HW_LINE's results, HW_TALLY's counts
HL_FAIL         = 1
HL_SKIP         = 2
HL_INFO         = 3                    ; a measurement, with no limit

; card6510.s: its place in the card's RAM and its commands (each answered)
C65_ORG         = $0200                ; where it is loaded and started: $0200-$05FF at most
C65_FIRST       = $0600                ; the first byte of the card's RAM it may test; to $FFF9
C65_HELLO       = $A5                  ; sent when it starts
C65_ECHO        = $01                  ; b: b back
C65_SUM         = $02                  ; a (2), n (2): the sum of n bytes from a, low then high
C65_TEST        = $03                  ; a (2, a page's start), e (2), f: a to e - 1 tested (f bit 0: the march
                                       ;   too); 5 bytes back: 0 or 1, the address (2), read, expected
C65_SID         = $04                  ; s (0 A, 1 B): voice 3 run; back bit 0 OSC3 moved, bit 1 ENV3 rose
C65_TONE        = $05                  ; s, on (1) or off (0): voice 1 at 440 Hz; back C65_TONE

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

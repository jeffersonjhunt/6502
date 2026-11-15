; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIOTBL.ASM's device tables, translated, in block P4 (hw/oodos.md, "The
; plan, phase 7": 7c, the devices as files):
;
;   DEVTBL    the devices' names, each followed by its number (bit 7 set:
;             $FF KYBD, -2 SCRN, ... -8 COM2), a 0 at the end (PARDEV's
;             DEVSRC, p4/gio86.s)
;   DVPTR     each device's functions by its number (CDEVID's place): the
;             disk's first. OO-1: an entry is the device's own jump, which
;             indexes its table (DSKDSP, KYBDSP, ...) by GFCODE, as the 8086's
;             TBLDSP indexes it; 0 is Device unavailable (TBLDSP)
;   DVINI     each device's initialisation (GIOINI's DOALDV, p4/gio86.s)
;
; The table is OEM.H's: KYBD, SCRN and CONS, then NMLPT (3) printers, no
; cassette (GWCASS 0), NMCOMT (2) ports. OO-1: LPT1 is LST: (function 5) and
; COM1 is AUX: (functions 3, 4, 7); LPT2, LPT3 and COM2 wait for the serial
; card's TL16C552A in the emulator (hw/oodos.md, the OEM layer's table), so
; their entries are 0: Device unavailable at OPEN. Their names stay in the
; table, so the name is known and the answer is the source's for a device
; with no driver. GIOTRM's termination table (_DVTRM) isn't kept: every end
; is a RET on the OO-1 (ledger.txt).

.include "gwbasic.inc"

.export DEVTBL, DVPTR, DVINI
.import GFCODE, DSKDSP, KYBDSP, SCNDSP, CONDSP, LPTDSP, COMDSP
.import KYBINI, SCNINI, LPTINI, COMINI

.rodata

; _DVTBL: each name, then its number, $_KYBD ($FF) down.
DEVTBL:         .byte   "KYBD", DEVKYB
                .byte   "SCRN", DEVSCN
                .byte   "CONS", DEVCON
                .byte   "LPT1", DEVLP1
                .byte   "LPT2", DEVLP1 - 1
                .byte   "LPT3", DEVLP1 - 2
                .byte   "COM1", DEVCM1
                .byte   "COM2", DEVCM1 - 1
                .byte   0              ; 0 ends the table
.assert DEVCM1 - 1 = $100 - NDEV, error, "DEVTBL: NDEV devices"

; _DVPTR: the disk's, then one for each device, in DEVTBL's order.
DVPTR:          .word   DSKJ           ; the disk
                .word   KYBJ           ; KYBD
                .word   SCNJ           ; SCRN
                .word   CONJ           ; CONS
                .word   LPTJ           ; LPT1
                .word   0, 0           ; LPT2, LPT3 (OO-1: no driver)
                .word   COMJ           ; COM1
                .word   0              ; COM2 (OO-1: no driver)

; _DVINI: each device's initialisation, KYBD first (DOALDV calls them from
; the last). OO-1: LPT2's, LPT3's and COM2's are RETs, as they have no DCB.
DVINI:          .word   KYBINI, SCNINI, DVRET
                .word   LPTINI, DVRET, DVRET
                .word   COMINI, DVRET

.code

; The jumps DVPTR names: the function GFCODE of the device's table (the
; 8086's TBLDSP, ADD DI,AX; JMP IOJUMP).
DSKJ:           LDX     GFCODE
                JMP     (DSKDSP,X)
KYBJ:           LDX     GFCODE
                JMP     (KYBDSP,X)
SCNJ:           LDX     GFCODE
                JMP     (SCNDSP,X)
CONJ:           LDX     GFCODE
                JMP     (CONDSP,X)
LPTJ:           LDX     GFCODE
                JMP     (LPTDSP,X)
COMJ:           LDX     GFCODE
                JMP     (COMDSP,X)
; _RET (GIOCON.ASM's): CONS's initialisation, and the drivers' absent.
DVRET:          RTS

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIOCON.ASM's CONS: device, translated, in block P4 (hw/oodos.md, "The plan,
; phase 7": 7c, the devices as files): the console's raw output, for a file
; OPENed on "CONS:" for output.
;
;   CONDSP   its functions, by GIO86U's G_* (TBLDSP's table)
;   CONOPN   OPEN: output only; width 255, column 0
;   CONSOT   a byte to the console as it is, by function 6 (MS-DOS's direct
;            console I/O): no column, no width, no CR LF of BASIC's
;   CONGPS   its column, always 0; CONGWD its width, 255
;
; OO-1: function 6 takes $FD, $FE and $FF as requests (a key, the status),
; not bytes, so CONSOT doesn't send them (the source leaves out $FF alone,
; which MS-DOS's function 6 reads as its request).

.include "gwbasic.inc"

.export CONDSP
.import DERBFM, INIFDB, DEVBIN, DEVBOT

.rodata

; CONDSP: the raw console's functions, by G_*.
CONDSP:         .word   DERBFM         ; G_EOF
                .word   DERBFM         ; G_LOC
                .word   DERBFM         ; G_LOF
                .word   CONRET         ; G_CLS
                .word   DERBFM         ; G_SWD
                .word   DERBFM         ; G_RND
                .word   CONOPN         ; G_OPN
                .word   DERBFM         ; G_SIN
                .word   CONSOT         ; G_SOT
                .word   CONGPS         ; G_GPS
                .word   CONGWD         ; G_GWD
                .word   CONRET         ; G_SCW
                .word   CONRET         ; G_GCW
                .word   DEVBIN         ; G_BIN
                .word   DEVBOT         ; G_BOT

.code

; CONOPN (GIOCON.ASM 81): A the device id: output only, width 255, column 0.
CONOPN:         LDX     #255           ; DX: width 255, column 0
                STX     RDL
                STZ     RDH
                LDX     #MD_SQO
                JMP     INIFDB

; CONGPS (91): A the column, 0. CONGWD (99): A the width, 255.
CONGPS:         LDA     #0
CONRET:         RTS                    ; _RET
CONGWD:         LDA     #255
                RTS

; CONSOT (108): A to the console, raw (C_RAWIO). A kept.
CONSOT:         CMP     #$FD           ; OO-1: function 6's requests aren't sent
                BCS     CONSOX
                PHA
                LDY     #C_RAWIO
                JSR     BDOS
                PLA
CONSOX:         RTS

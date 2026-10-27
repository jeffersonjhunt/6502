; A test OODOS.SYS, for OOLDR's tests (emulator/test/host/test_oodos_boot.cpp):
; six segments in block 31 and blocks 60, 61 and 62, and a cold boot that
; records what it found and stops. The real OODOS.SYS takes its place on
; the card (tools/make_card.sh) once there is one.
;
;
; A data segment's byte i is ((i >> 7) * 31 + i * 7 + seed) & $FF, as the
; card's other binary files are. The segments, in the header's order, and
; the records of the file each takes (testsys.cfg):
;
;   1  $7E000   1,000 bytes, seed 11  block 31        records 1-8
;   2  $F76B0   2,000 bytes, seed 14  block 61        9-24
;   3  $F8800   the cold boot         block 62, $C800 25-28
;   4  $FBFFA   the vectors, 6 bytes  block 62, $FFFA 29
;   5  $F4000  14,000 bytes, seed 13  block 61        30-139, over record 128
;   6  $F0000  16,384 bytes, seed 12  block 60        140-267, over record 256
;
; 2 starts where 5 ends and is loaded first: a last part record of 5's
; written whole would show on it. The file is 268 records, two directory
; entries on oodos-8m.
;
; The cold boot, at $C800 in block 62 (segment 3), runs with the windows OOLDR
; leaves. It keeps P and S as it found them, reads WIN0-WIN3 and VEC, and
; writes them with a signature to $7FF00 in block 31 (through W1), outside
; every segment:
;
;   +0  "TESTSYS!"   +8 WIN0  +9 WIN1  +10 WIN2  +11 WIN3  +12 VEC
;   +13 P            +14 S    +15 $A5
;
; then stops (STP), so a test can look.

.setcpu "65C02"
.include "backplane.inc"
.include "system.inc"
.import __S1_FILEOFFS__, __S2_FILEOFFS__, __S3_FILEOFFS__, __S4_FILEOFFS__
.import __S5_FILEOFFS__, __S6_FILEOFFS__
.import __SEG1_SIZE__, __SEG2_SIZE__, __COLD_SIZE__, __VECTORS_SIZE__
.import __SEG5_SIZE__, __SEG6_SIZE__

S1_AT   = $7E000                       ; block 31
S2_AT   = S5_AT + 14000                ; block 61, right after 5
S3_AT   = $F8800                       ; block 62: CPU $C800
S4_AT   = $FBFFA                       ; block 62: CPU $FFFA
S5_AT   = $F4000                       ; block 61
S6_AT   = $F0000                       ; block 60, all of it
RECORD  = $7FF00                       ; what the cold boot found
SCRATCH = $F000                        ; block 62, outside every segment

.macro  SEGMENT at, size, offset
        .faraddr at
        .word   size
        .word   offset / 128
        .byte   0
.endmacro

.macro  PATTERN size, seed
        .repeat size, i
        .byte   <((i >> 7) * 31 + i * 7 + seed)
        .endrepeat
.endmacro

.segment "HEADER"
        .byte   SYS_MAGIC_0, SYS_MAGIC_1, SYS_MAGIC_2, SYS_MAGIC_3
        .byte   SYS_MAJOR, SYS_MINOR
        .word   COLD
        .byte   6
        .res    7, 0
        SEGMENT S1_AT, __SEG1_SIZE__, __S1_FILEOFFS__
        SEGMENT S2_AT, __SEG2_SIZE__, __S2_FILEOFFS__
        SEGMENT S3_AT, __COLD_SIZE__, __S3_FILEOFFS__
        SEGMENT S4_AT, __VECTORS_SIZE__, __S4_FILEOFFS__
        SEGMENT S5_AT, __SEG5_SIZE__, __S5_FILEOFFS__
        SEGMENT S6_AT, __SEG6_SIZE__, __S6_FILEOFFS__

.segment "SEG1"
        PATTERN 1000, 11
.segment "SEG2"
        PATTERN 2000, 14
.segment "SEG5"
        PATTERN 14000, 13
.segment "SEG6"
        PATTERN 16384, 12

.segment "COLD"
COLD:   PHP                            ; P and S as OOLDR left them
        PLA
        TAY
        TSX
        STY     SCRATCH + 13
        STX     SCRATCH + 14
        LDX     #4
@win:   LDA     NB_WIN0,X
        STA     SCRATCH + 8,X
        DEX
        BPL     @win
        LDX     #7
@sig:   LDA     SIGNATURE,X
        STA     SCRATCH,X
        DEX
        BPL     @sig
        LDA     #$A5
        STA     SCRATCH + 15
        LDA     #RECORD >> 14          ; block 31 at $4000, then the record there
        STA     NB_WIN1
        LDX     #15
@copy:  LDA     SCRATCH,X
        STA     $4000 + (RECORD & $3FFF),X
        DEX
        BPL     @copy
HALT:   STP
SIGNATURE:
        .byte   "TESTSYS!"

.segment "VECTORS"
        .word   HALT, HALT, HALT       ; NMI, reset, IRQ

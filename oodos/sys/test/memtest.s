; MEMTEST: the BDOS's memory functions of phase 3c (hw/oodos.md, "The BDOS,
; phase 3"): the DMA segment (51, 52), CP/M-86's memory blocks (53-58), a
; CPU address's physical one (73) and mounting drives (74), a case by the
; command tail's first letter, in the error mode that returns errors ($FF),
; a line a result for test_oodos_sys.cpp. "aa xx" is A and X as the BDOS
; returned them; a block (52's, an MCB, 73's) is its bytes in hex, in order.
; Physical memory is looked at through W2, mapped to its block for the
; moment (blocks 4-23 are banks 1-5), and W2 put back before each BDOS call:
; the BDOS's JMP is at $BFFD.
;
;   D   on M:, with a DMA segment: GET0 (52: 8002FFFF, $0280 and none);
;       block 4's first 512 bytes a pattern, 51 $1000 and 26 0: GET1
;       (00000010); D.DAT made (DMK), 4 records written from $10000 (44 4:
;       DW4 00 00), closed with no segment (DCL); read back to the CPU: DRD
;       Y; records 1-2 by 33 to $1401:$00F0, $14100 (DR2 00 00, DR2CHK Y: the
;       bytes around them as they were); record 0 by 20 to $1FF7:$000F
;       (DRF 00 00, DRFCR 01, DRFCHK Y: MOVE's end is $FFFF, no refusal);
;       record 3 to $1FFC:0, across banks
;       1 and 2 (DRX, DRXCHK Y); again to the CPU (DRC) and to $11000 (DRS),
;       the test's cycles of each; 17 to $1800:0 (D17 0n 00, D17CHK Y: the
;       entry there), 18 (D18 FF 00); refused, FF FF, nothing moved: DRST
;       ($5FF0:$00F0, from block 23 into 24; DRSTCHK Y), DRB6 ($6000), DRM
;       ($C000, M:), DRBD ($F000, the BDOS), DRPAST ($FFF0:$0100, past
;       $FFFFF), DRWRAP ($1000:$FFC0, past $FFFF); 44 16 from $5F80:0, to
;       $5FFFF: DRM16 01 04 (4 records, then the end); 44 17: DRM17 FF FF;
;       49's count of 255, three blocks: DR255 01 04 ($2BF8:0, blocks
;       10-12), DR255X FF FF ($5BF8:0, 22-24);
;       DWM (21 to $C000: FF FF) DWMCR (the FCB's CR as it was: 04); D17M
;       (17 to $C000: FF FF); 46 with a segment: DFREE 00 00 DFREECPU Y (to
;       the CPU's DMA buffer); 51 $1234 then 13: GET13 (80023412: 13 keeps
;       the segment, left set for W)
;   W   after a warm boot: WGET (8002FFFF: WARM reset D's segment), WMAX
;       00 00 WMAXM (53 of $FFFF: 0010005000, all 20 blocks free)
;   M   CP/M-86's memory, each "KEY aa xx" then "KEYM" and the MCB (base,
;       length, ext); 53 and 54 allocate their run, freed by 57 (xxF, 00
;       00) where the next needs it: MAX (53 $5000: 0010005000), MXA (55:
;       FF 00, MAX took all), MXF; MAX1 (53 1: 0010000401), M1A (55 1
;       block: 0014000400, not MAX1's), M1F, M1AF; MAX0 (53 0: FF 00, the
;       MCB as it was); AL1 (55 $0800: 0010000800),
;       AL2 (55 $0401, two blocks: 0018000800); AB1 (56 $2000 $0400:
;       0020000400); FF 00 for AB2 ($1C00, taken), AB3 ($1100, not a
;       block's), AB4 ($0C00, bank 0), AB5 ($6000, bank 6), AB6 ($1C00 3
;       blocks, the first taken), AB7 (a length of 0); AM1 (54 $1000, taken:
;       FF 00), AM2 (54 $2400 $FFFF: the 15 free, 0024003C00), AM3 (54
;       $2400 $0801: three, M_EXT 1), AM4 (54 of a taken block: FF 00); MAX2
;       (0024003C00); FR1 (57 $1C00, AL2's second block, a length of 0: FF
;       00), FR2 (57 $1800: 00
;       00), FR3 (again: FF 00); AB8 (56 $1800 3 blocks, the third taken:
;       FF 00); AL3 (55 $5000: FF 00); MAX3 (M_EXT 1:
;       0024003C01); FRALL (57 with M_EXT $FF: 00 00); MAX4 (0010005000); AL4
;       (all 20), AL5 (none left: FF 00); ALLFREE (58: 00 00); MAX5; MHOLE
;       (an MCB in the I/O hole: FF FF); 57 of a tail: TA (55 3 blocks:
;       0010000C00), TS (57 $1400 1 block, short of the end: FF 00), TB
;       (57 $1400 $0801: 00 00), TC (55 2: 0014000800, block 4 kept), TD
;       (57 $1000, the head alone), TE; AL6 (a block left for the warm boot)
;   P   73, "KEY aa xx" then "KEYM" and the 4 bytes: P0300 (00003000),
;       P4123 (03001204), P8FFF (0F00FF08), PC900 (000090F8: block 62);
;       PHOLE, PC7FF (the I/O hole: FF FF, the bytes as they were); P37 (W1
;       on block 37: 00000194); PFL (W1 on flash: FF FF); PRO (W1 read-only:
;       00000004); PBLK (the 4 bytes in the I/O hole: FF FF)
;   B   the BIOS's XMOVE with a physical bank, through function 50: BUF's
;       128 bytes (0-127) to $12000 (bank $81): BTO aa xx (MOVE's: the end,
;       $2080); BUF zeroed, back from there: BFROM aa xx (BUF + 128), BEND
;       (BUF + 128, low then high, for the test), BCHK Y
;   N   74, units 1 (A.IMG) only: NA, NM, NH (FF 04); NP1-NP9 (bad paths:
;       FF 10); NLONG (510 bytes: FF 10), NLEN (509: FF 11); NNF, NDIR (FF
;       11); NSZ (/BOOT.BIN: FF 12) NSZSEL (14 of C: FF 04); NSAME
;       (/OODOS/A.IMG: FF 13) NSAMESEL (FF 04); NC (/oodos/b.img: 00 00)
;       NCSEL (00 00) NC17 (FF 00) NCW (X.DAT made, a record, closed: 00 00);
;       SECT? (the test puts Y.DAT in C:'s directory behind OODOS's back);
;       NC2 (the same image again: 00 00) NCY (17 of Y.DAT: 0n 00, the
;       drive read again); NUC (unmounted: 00 00) NUCSEL (FF 04) NUC2 (00
;       00: nothing mounted); NRO (read-only: 00 00) NROMK (FF 02); NC3 (00
;       00) NWW (X.DAT's record 0 written again, not closed); FAULT? (the
;       card's writes fail); NWB (FF 01: not written back) NWBSEL (00 00:
;       C: is still there); MEND? (they work again); NWB2 (00 00); NC4 (00
;       00) NWBCHK Y (the record as written); RSCN? (the test takes the
;       card out at 74's rescan); NRS (B.IMG on C:: FF 01, the rescan
;       failed); RMEND? (the card back)

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

DRV_M   = 12
DRV_C   = 2                             ; C:, unit 3
ROMBIT  = $80                           ; a window on flash (backplane.inc's NB_ROM)
WIN2    = $8000

        .zeropage
WP:     .res    2                       ; a byte through W2
PI:     .res    2                       ; the pattern's byte

; "KEY " and the n bytes at addr in hex, CR LF.
.macro BYTES key, addr, n
        SAY     key
        lda     #<(addr)
        sta     PTR
        lda     #>(addr)
        sta     PTR+1
        ldx     #n
        jsr     HEXN
        jsr     CRLF
.endmacro

; W2 on block b.
.macro BLK b
        lda     #b
        sta     NB_WIN2
.endmacro

; The MCB = base, length, ext.
.macro MCB base, len, ext
        lda     #<(base)
        sta     mcb
        lda     #>(base)
        sta     mcb+1
        lda     #<(len)
        sta     mcb+2
        lda     #>(len)
        sta     mcb+3
        lda     #ext
        sta     mcb+4
.endmacro

; TRY fn on the MCB as "KEY aa xx", then its bytes as "KEYM ...".
.macro MTRY key, fn
        TRY     .concat(key, " "), fn, mcb
        BYTES   .concat(key, "M "), mcb, 5
.endmacro

; 51 seg, 26 off.
.macro SEG seg, off
        CALLF   F_DMASEG, seg
        CALLF   F_DMAOFF, off
.endmacro

; R0-R2 of fcb = r.
.macro RREC r
        lda     #<(r)
        sta     fcb+33
        lda     #>(r)
        sta     fcb+34
        stz     fcb+35
.endmacro

; 74 of drive d with the path str (and the flags fl).
.macro MOUNT key, d, str, fl
        .local s
        .pushseg
        .rodata
s:      .byte   str, 0
        .popseg
        lda     #<s
        ldx     #>s
        ldy     #d
        jsr     mtpath
        .ifblank fl
        stz     mblk+1
        .else
        lda     #fl
        sta     mblk+1
        .endif
        TRY     key, O_MOUNT, mblk
.endmacro

        .code
start:  CALLF   F_DMAOFF, BUF
        lda     #$FF                    ; errors come back
        ldy     #F_ERRMODE
        jsr     BDOS
        lda     BP_DMA+2                ; the case
        cmp     #'D'
        bne     :+
        jmp     cased
:       cmp     #'W'
        bne     :+
        jmp     casew
:       cmp     #'M'
        bne     :+
        jmp     casem
:       cmp     #'P'
        bne     :+
        jmp     casep
:       cmp     #'N'
        bne     :+
        jmp     casen
:       cmp     #'B'
        bne     :+
        jmp     caseb
:       SAY     "NO CASE"
        jmp     CRLF

done:   BLK     2
        SAY     "MEMTEST DONE"
        jmp     CRLF

; ---------------------------------------------------------------------------
; D: the DMA segment

cased:  lda     #DRV_M
        sta     TDRV
        CALLF   F_DMAOFF, $0280
        CALLF   F_DMAGET, get
        BYTES   "GET0 ", get, 4
        BLK     4                       ; $10000-$101FF the pattern, $10200-$103FF $E7
        jsr     patw
        lda     #$E7
        ldx     #2
        ldy     #2
        jsr     fillw
        BLK     2
        SEG     $1000, 0
        CALLF   F_DMAGET, get
        BYTES   "GET1 ", get, 4
        NAME    fcb, "D       DAT"
        TRY     "DMK ", F_MAKE, fcb
        CALLF   F_MULTISEC, 4
        TRY     "DW4 ", F_WRITE, fcb
        CALLF   F_MULTISEC, 1
        CALLF   F_DMASEG, $FFFF
        TRY     "DCL ", F_CLOSE, fcb
        NAME    fcb, "D       DAT"
        CALLF   F_OPEN, fcb
        CALLF   F_DMAOFF, BUF           ; read back to the CPU
        stz     PI
        stz     PI+1
        ldx     #4
@rd:    phx
        CALLF   F_READ, fcb
        ldy     #0
:       jsr     pat
        cmp     BUF,y
        bne     @rdbad
        iny
        bpl     :-
        plx
        dex
        bne     @rd
        clc
        bra     :+
@rdbad: plx
        sec
:       SAY     "DRD "
        jsr     YESNO
; records 1-2 to $14100
        BLK     5
        lda     #$E7
        ldx     #0
        ldy     #4
        jsr     fillw
        BLK     2
        SEG     $1401, $00F0
        CALLF   F_MULTISEC, 2
        RREC    1
        TRY     "DR2 ", F_READRAND, fcb
        CALLF   F_MULTISEC, 1
        BLK     5
        lda     #0                      ; $14000-$140FF E7, then the pattern from 128, then E7
        ldx     #1
        jsr     e7w
        bcs     @dr2
        lda     #128
        sta     PI
        stz     PI+1
        lda     #<(WIN2 + $100)
        sta     WP
        lda     #>(WIN2 + $100)
        sta     WP+1
        ldx     #1
        jsr     patcmp
        bcs     @dr2
        lda     #2
        ldx     #2
        jsr     e7w
@dr2:   php
        BLK     2
        plp
        SAY     "DR2CHK "
        jsr     YESNO
; record 0 by 20 to $1FF7F: MOVE's end, $FFFF, is no refusal
        SEG     $1FF7, $000F
        stz     fcb+32
        TRY     "DRF ", F_READ, fcb
        BYTES   "DRFCR ", fcb+32, 1
        BLK     7
        lda     #<(WIN2 + $3F7F)
        sta     WP
        lda     #>(WIN2 + $3F7F)
        sta     WP+1
        stz     PI
        stz     PI+1
        lda     #128
        jsr     patn
        php
        BLK     2
        plp
        SAY     "DRFCHK "
        jsr     YESNO
; record 3 across $1FFFF/$20000
        BLK     7
        lda     #$E7
        ldx     #$3F
        ldy     #1
        jsr     fillw
        BLK     8
        lda     #$E7
        ldx     #0
        ldy     #1
        jsr     fillw
        BLK     2
        SEG     $1FFC, 0
        RREC    3
        TRY     "DRX ", F_READRAND, fcb
        BLK     7                       ; $1FF00-$1FFBF E7, then record 3's first 64
        lda     #<(WIN2 + $3F00)
        sta     WP
        lda     #>(WIN2 + $3F00)
        sta     WP+1
        lda     #$C0
        jsr     e7n
        bcs     @drx
        lda     #<384
        sta     PI
        lda     #>384
        sta     PI+1
        lda     #64
        jsr     patn
        bcs     @drx
        BLK     8                       ; then its last 64 from $20000, then E7
        lda     #<WIN2
        sta     WP
        lda     #>WIN2
        sta     WP+1
        lda     #64
        jsr     patn
        bcs     @drx
        lda     #$C0
        jsr     e7n
@drx:   php
        BLK     2
        plp
        SAY     "DRXCHK "
        jsr     YESNO
        CALLF   F_DMASEG, $FFFF         ; record 3 again, buffered: to the CPU, then to $11000
        CALLF   F_DMAOFF, BUF
        RREC    3
        TRY     "DRC ", F_READRAND, fcb
        SEG     $1100, 0
        RREC    3
        TRY     "DRS ", F_READRAND, fcb
; 17 and 18 to $18000
        BLK     6
        lda     #$E7
        ldx     #0
        ldy     #1
        jsr     fillw
        BLK     2
        SEG     $1800, 0
        NAME    sfcb, "D       DAT"
        TRY     "D17 ", F_SFIRST, sfcb
        and     #3
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        sta     WP
        lda     #>WIN2
        sta     WP+1
        BLK     6
        ldy     #11
:       lda     (WP),y
        and     #$7F
        cmp     sfcb,y
        bne     :+
        dey
        bne     :-
        lda     (WP)                    ; user 0
        bne     :+
        clc
        bra     :++
:       sec
:       php
        BLK     2
        plp
        SAY     "D17CHK "
        jsr     YESNO
        TRY     "D18 ", F_SNEXT, 0
; refused
        BLK     23
        lda     #$E7
        ldx     #$3F
        ldy     #1
        jsr     fillw
        BLK     2
        RREC    0
        SEG     $5FF0, $00F0
        TRY     "DRST ", F_READRAND, fcb
        BLK     23
        lda     #<(WIN2 + $3F00)
        sta     WP
        lda     #>(WIN2 + $3F00)
        sta     WP+1
        lda     #0
        jsr     e7n
        php
        BLK     2
        plp
        SAY     "DRSTCHK "
        jsr     YESNO
        SEG     $6000, 0
        TRY     "DRB6 ", F_READRAND, fcb
        SEG     $C000, 0
        TRY     "DRM ", F_READRAND, fcb
        SEG     $F000, 0
        TRY     "DRBD ", F_READRAND, fcb
        SEG     $FFF0, $0100
        TRY     "DRPAST ", F_READRAND, fcb
        SEG     $1000, $FFC0
        TRY     "DRWRAP ", F_READRAND, fcb
        SEG     $5F80, 0
        CALLF   F_MULTISEC, 16
        TRY     "DRM16 ", F_READRAND, fcb
        CALLF   F_MULTISEC, 17
        TRY     "DRM17 ", F_READRAND, fcb
        lda     #$4A                    ; 49: a count of 255, three blocks' span
        sta     get
        lda     #$FF
        sta     get+1
        sta     get+2
        CALLF   S_SCB, get
        SEG     $2BF8, 0                ; blocks 10-12
        TRY     "DR255 ", F_READRAND, fcb
        SEG     $5BF8, 0                ; blocks 22-24
        TRY     "DR255X ", F_READRAND, fcb
        CALLF   F_MULTISEC, 1
        SEG     $C000, 0
        BYTES   "DWMCR0 ", fcb+32, 1
        TRY     "DWM ", F_WRITE, fcb
        BYTES   "DWMCR ", fcb+32, 1
        NAME    sfcb, "D       DAT"
        TRY     "D17M ", F_SFIRST, sfcb
        SEG     $1000, BUF              ; 46: the CPU's buffer
        ldx     #2
        lda     #$E7
:       sta     BUF,x
        dex
        bpl     :-
        TRY     "DFREE ", DRV_SPACE, DRV_M
        lda     BUF
        and     BUF+1
        and     BUF+2
        cmp     #$E7
        clc
        bne     :+
        sec
:       SAY     "DFREECPU "
        jsr     YESNO
        CALLF   F_DMASEG, $FFFF
        CALLF   F_DMAOFF, BUF
        CALLF   F_CLOSE, fcb
        CALLF   F_DMASEG, $1234
        CALLF   DRV_ALLRESET, 0
        CALLF   F_DMAGET, get
        BYTES   "GET13 ", get, 4
        jmp     done                    ; the segment left set: W sees WARM reset it

; ---------------------------------------------------------------------------
; W: what the warm boot left

casew:  CALLF   F_DMAOFF, $0280
        CALLF   F_DMAGET, get
        BYTES   "WGET ", get, 4
        MCB     0, $FFFF, 0
        MTRY    "WMAX", MC_MAX
        jmp     done

; ---------------------------------------------------------------------------
; M: CP/M-86's memory functions

casem:  MCB     0, $5000, 0
        MTRY    "MAX", MC_MAX
        MCB     0, $0400, 0             ; 53 allocated all 20
        MTRY    "MXA", MC_ALLOC
        MCB     $1000, 0, 0             ; and 57 frees it
        MTRY    "MXF", MC_FREE
        MCB     0, 1, 0
        MTRY    "MAX1", MC_MAX
        MCB     0, $0400, 0             ; 55 doesn't get 53's block
        MTRY    "M1A", MC_ALLOC
        MCB     $1000, 0, 0
        MTRY    "M1F", MC_FREE
        MCB     $1400, 0, 0
        MTRY    "M1AF", MC_FREE
        MCB     $1234, 0, 7
        MTRY    "MAX0", MC_MAX
        MCB     0, $0800, 0
        MTRY    "AL1", MC_ALLOC
        MCB     0, $0401, 0
        MTRY    "AL2", MC_ALLOC
        MCB     $2000, $0400, 0
        MTRY    "AB1", MC_ABSALLOC
        MCB     $1C00, $0400, 0
        MTRY    "AB2", MC_ABSALLOC
        MCB     $1100, $0400, 0
        MTRY    "AB3", MC_ABSALLOC
        MCB     $0C00, $0400, 0
        MTRY    "AB4", MC_ABSALLOC
        MCB     $6000, $0400, 0
        MTRY    "AB5", MC_ABSALLOC
        MCB     $1C00, $0C00, 0         ; 7, then 8 taken by AB1
        MTRY    "AB6", MC_ABSALLOC
        MCB     $2400, 0, 0
        MTRY    "AB7", MC_ABSALLOC
        MCB     $1000, $0400, 0
        MTRY    "AM1", MC_ABSMAX
        MCB     $2400, $FFFF, 0
        MTRY    "AM2", MC_ABSMAX
        MCB     $2400, 0, 0
        MTRY    "AM2F", MC_FREE
        MCB     $2400, $0801, 0
        MTRY    "AM3", MC_ABSMAX
        MCB     $2400, 0, 0
        MTRY    "AM3F", MC_FREE
        MCB     $1C00, $FFFF, 0
        MTRY    "AM4", MC_ABSMAX
        MCB     0, $FFFF, 0
        MTRY    "MAX2", MC_MAX
        MCB     $2400, 0, 0
        MTRY    "MAX2F", MC_FREE
        MCB     $1C00, 0, 0             ; AL2's second block, a length of 0
        MTRY    "FR1", MC_FREE
        MCB     $1800, 0, 0
        MTRY    "FR2", MC_FREE
        MCB     $1800, 0, 0
        MTRY    "FR3", MC_FREE
        MCB     $1800, $0C00, 0         ; 6 and 7 free, 8 taken by AB1: no shrinking
        MTRY    "AB8", MC_ABSALLOC
        MCB     0, $5000, 0
        MTRY    "AL3", MC_ALLOC
        MCB     0, $FFFF, 0
        MTRY    "MAX3", MC_MAX
        MCB     0, 0, $FF
        MTRY    "FRALL", MC_FREE
        MCB     0, $FFFF, 0
        MTRY    "MAX4", MC_MAX
        MCB     $1000, 0, 0
        MTRY    "MAX4F", MC_FREE
        MCB     0, $5000, 0
        MTRY    "AL4", MC_ALLOC
        MCB     0, $0001, 0
        MTRY    "AL5", MC_ALLOC
        TRY     "ALLFREE ", MC_ALLFREE, 0
        MCB     0, $FFFF, 0
        MTRY    "MAX5", MC_MAX
        TRY     "MHOLE ", MC_MAX, $C000
        MCB     $1000, 0, 0
        MTRY    "MAX5F", MC_FREE
        MCB     0, $0C00, 0             ; 57 of a tail: blocks 4-6
        MTRY    "TA", MC_ALLOC
        MCB     $1400, $0400, 0         ; 5 only: short of the end
        MTRY    "TS", MC_FREE
        MCB     $1400, $0801, 0         ; 5 and 6, rounded past the end: freed
        MTRY    "TB", MC_FREE
        MCB     0, $0800, 0             ; 4 kept: 5 and 6 the lowest two free
        MTRY    "TC", MC_ALLOC
        MCB     $1000, 0, 0             ; the head alone
        MTRY    "TD", MC_FREE
        MCB     $1400, 0, 0
        MTRY    "TE", MC_FREE
        MCB     0, $0400, 0             ; one left for the warm boot to free
        MTRY    "AL6", MC_ALLOC
        jmp     done

; ---------------------------------------------------------------------------
; P: 73

.macro PHYS key, addr
        lda     #<(addr)
        sta     get
        lda     #>(addr)
        sta     get+1
        stz     get+2
        stz     get+3
        TRY     .concat(key, " "), O_PHYSADDR, get
        BYTES   .concat(key, "M "), get, 4
.endmacro

casep:  PHYS    "P0300", $0300
        PHYS    "P4123", $4123
        PHYS    "P8FFF", $8FFF
        PHYS    "PC900", $C900
        PHYS    "PHOLE", $C000
        PHYS    "PC7FF", $C7FF
        lda     #37                     ; W1, not W2: the BDOS's JMP is at $BFFD
        sta     NB_WIN1
        PHYS    "P37", $4010
        lda     #ROMBIT | 31
        sta     NB_WIN1
        PHYS    "PFL", $4000
        lda     #NB_RO | 1
        sta     NB_WIN1
        PHYS    "PRO", $4000
        lda     #1
        sta     NB_WIN1
        TRY     "PBLK ", O_PHYSADDR, $C001
        jmp     done

; ---------------------------------------------------------------------------
; B: XMOVE's physical bank

caseb:  ldx     #127
:       txa
        sta     BUF,x
        dex
        bpl     :-
        lda     #1                      ; XMOVE: from bank 1, the program's, to $1xxxx
        ldx     #$81
        jsr     xmove
        lda     #<BUF
        sta     bpb+4
        lda     #>BUF
        sta     bpb+5
        lda     #$00
        sta     bpb+6
        lda     #$20
        sta     bpb+7
        jsr     move
        SAY     "BTO "
        lda     RA
        ldx     RX
        jsr     RES
        ldx     #127
:       stz     BUF,x
        dex
        bpl     :-
        lda     #$81
        ldx     #1
        jsr     xmove
        stz     bpb+4
        lda     #$20
        sta     bpb+5
        lda     #<BUF
        sta     bpb+6
        lda     #>BUF
        sta     bpb+7
        jsr     move
        SAY     "BFROM "
        lda     RA
        ldx     RX
        jsr     RES
        BYTES   "BEND ", bend, 2
        ldx     #127
:       txa
        cmp     BUF,x
        bne     :+
        dex
        bpl     :-
        clc
        bra     :++
:       sec
:       SAY     "BCHK "
        jsr     YESNO
        jmp     done

        .rodata
bend:   .word   BUF + 128               ; BFROM's A/X: the end of the move to BUF
        .code

; Function 50's XMOVE, A the source bank, X the destination.
xmove:  sta     bpb+1
        stx     bpb+2
        lda     #29
        sta     bpb
        CALLF   S_BIOS, bpb
        rts
; Function 50's MOVE of 128 bytes, bpb+4 the source, +6 the destination; RA, RX its A/X.
move:   lda     #25
        sta     bpb
        lda     #128
        sta     bpb+1
        stz     bpb+2
        CALLF   S_BIOS, bpb
        sta     RA
        stx     RX
        rts

; ---------------------------------------------------------------------------
; N: 74

casen:  MOUNT   "NA ", 0, "/OODOS/B.IMG"
        MOUNT   "NM ", DRV_M, "/OODOS/B.IMG"
        MOUNT   "NH ", 7, "/OODOS/B.IMG"
        MOUNT   "NP1 ", DRV_C, "OODOS/B.IMG"
        MOUNT   "NP2 ", DRV_C, "/OODOS//B.IMG"
        MOUNT   "NP3 ", DRV_C, "/OODOS/./B.IMG"
        MOUNT   "NP4 ", DRV_C, "/OODOS/../OODOS/B.IMG"
        MOUNT   "NP5 ", DRV_C, "/OODOS/B.IMG/"
        MOUNT   "NP6 ", DRV_C, "/"
        MOUNT   "NP7 ", DRV_C, "/OODOS/B IMG"
        MOUNT   "NP8 ", DRV_C, "/OODOS/B#.IMG"
        MOUNT   "NP9 ", DRV_C, {"/OODOS/B", 9, ".IMG"}
        lda     #<510                   ; "/" and 509 more: 510 bytes
        ldx     #>510
        jsr     longpath
        TRY     "NLONG ", O_MOUNT, mblk
        lda     #<509
        ldx     #>509
        jsr     longpath
        TRY     "NLEN ", O_MOUNT, mblk
        MOUNT   "NNF ", DRV_C, "/OODOS/NONE.IMG"
        MOUNT   "NDIR ", DRV_C, "/OODOS"
        MOUNT   "NSZ ", DRV_C, "/BOOT.BIN"
        TRY     "NSZSEL ", DRV_SET, DRV_C
        MOUNT   "NSAME ", DRV_C, "/OODOS/A.IMG"
        TRY     "NSAMESEL ", DRV_SET, DRV_C
        MOUNT   "NC ", DRV_C, "/oodos/b.img"
        TRY     "NCSEL ", DRV_SET, DRV_C
        lda     #DRV_C
        sta     TDRV
        NAME    sfcb, "???????????"
        TRY     "NC17 ", F_SFIRST, sfcb
        NAME    fcb, "X       DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_WRITE, fcb
        TRY     "NCW ", F_CLOSE, fcb
        KEYWAIT "SECT?"
        MOUNT   "NC2 ", DRV_C, "/OODOS/B.IMG"
        NAME    sfcb, "Y       DAT"
        TRY     "NCY ", F_SFIRST, sfcb
        MOUNT   "NUC ", DRV_C, ""
        TRY     "NUCSEL ", DRV_SET, DRV_C
        MOUNT   "NUC2 ", DRV_C, ""
        MOUNT   "NRO ", DRV_C, "/OODOS/B.IMG", 1
        NAME    fcb, "Z       DAT"
        TRY     "NROMK ", F_MAKE, fcb
        MOUNT   "NC3 ", DRV_C, "/OODOS/B.IMG"
        NAME    fcb, "X       DAT"
        CALLF   F_OPEN, fcb
        ldx     #127                    ; record 0 again: $C3s
        lda     #$C3
:       sta     BUF,x
        dex
        bpl     :-
        TRY     "NWW ", F_WRITE, fcb
        KEYWAIT "FAULT?"
        MOUNT   "NWB ", DRV_C, ""
        TRY     "NWBSEL ", DRV_SET, DRV_C
        KEYWAIT "MEND?"
        MOUNT   "NWB2 ", DRV_C, ""
        MOUNT   "NC4 ", DRV_C, "/OODOS/B.IMG"
        NAME    fcb, "X       DAT"
        CALLF   F_OPEN, fcb
        CALLF   F_READ, fcb
        ldx     #127
:       lda     BUF,x
        cmp     #$C3
        bne     :+
        dex
        bpl     :-
        clc
        bra     :++
:       sec
:       SAY     "NWBCHK "
        jsr     YESNO
        CALLF   DRV_SET, 0
        KEYWAIT "RSCN?"                 ; the test takes the card out at the rescan
        MOUNT   "NRS ", DRV_C, "/OODOS/B.IMG"
        KEYWAIT "RMEND?"                ; and puts it back, A.IMG on unit 1
        jmp     done

; mblk: the drive Y, the path at A/X (to its NUL) from +2.
mtpath: sty     mblk
        sta     PTR
        stx     PTR+1
        ldy     #0
:       lda     (PTR),y
        sta     mblk+2,y
        beq     :+
        iny
        bra     :-
:       rts

; mblk: C:, "/" and A/X - 1 'A's, its NUL.
longpath:
        sta     WP
        stx     WP+1
        lda     #DRV_C
        sta     mblk
        stz     mblk+1
        lda     #<(mblk+2)
        sta     PTR
        lda     #>(mblk+2)
        sta     PTR+1
        lda     #'/'
@byte:  sta     (PTR)
        inc     PTR
        bne     :+
        inc     PTR+1
:       lda     WP
        bne     :+
        dec     WP+1
:       dec     WP
        lda     WP
        ora     WP+1
        beq     @end
        lda     #'A'
        bra     @byte
@end:   lda     #0
        sta     (PTR)
        rts

; ---------------------------------------------------------------------------
; Through W2

; The pattern's byte PI (then PI + 1): (PI + PI / 128 x $11) ^ $A5. Keeps X, Y.
pat:    lda     PI+1
        asl     a
        sta     HEXB                    ; PI / 128: PI+1 x 2 + bit 7
        lda     PI
        asl     a
        lda     HEXB
        adc     #0
        sta     HEXB
        asl     a
        asl     a
        asl     a
        asl     a
        adc     HEXB                    ; x $11
        clc
        adc     PI
        eor     #$A5
        inc     PI
        bne     :+
        inc     PI+1
:       rts

; W2's first 512 bytes the pattern from 0.
patw:   stz     PI
        stz     PI+1
        lda     #<WIN2
        sta     WP
        lda     #>WIN2
        sta     WP+1
        ldx     #2
@page:  ldy     #0
:       jsr     pat
        sta     (WP),y
        iny
        bne     :-
        inc     WP+1
        dex
        bne     @page
        rts

; Carry set if the X pages at WP aren't the pattern from PI.
patcmp: ldy     #0
:       jsr     pat
        cmp     (WP),y
        bne     @bad
        iny
        bne     :-
        inc     WP+1
        dex
        bne     patcmp
        clc
        rts
@bad:   sec
        rts

; Carry set if the A bytes at WP (0: 256) aren't the pattern from PI; WP on past them.
patn:   sta     HEXC
        ldy     #0
:       jsr     pat
        cmp     (WP),y
        bne     @bad
        iny
        cpy     HEXC
        bne     :-
        tya
        clc
        adc     WP
        sta     WP
        bcc     :+
        inc     WP+1
:       clc
        rts
@bad:   sec
        rts

; Carry set if the A bytes at WP (0: 256) aren't all $E7; WP on past them.
e7n:    sta     HEXC
        ldy     #0
:       lda     (WP),y
        cmp     #$E7
        bne     @bad
        iny
        cpy     HEXC
        bne     :-
        tya
        clc
        adc     WP
        sta     WP
        bcc     :+
        inc     WP+1
:       clc
        rts
@bad:   sec
        rts

; Carry set if the X pages from W2's page A aren't all $E7.
e7w:    clc
        adc     #>WIN2
        sta     WP+1
        stz     WP
@page:  lda     #0
        phx
        jsr     e7n
        plx
        bcs     @bad
        dex
        bne     @page
        clc
@bad:   rts

; Y pages from W2's page X all A.
fillw:  sta     HEXC
        txa
        clc
        adc     #>WIN2
        sta     WP+1
        stz     WP
        lda     HEXC
@page:  phy
        ldy     #0
:       sta     (WP),y
        iny
        bne     :-
        ply
        inc     WP+1
        dey
        bne     @page
        rts

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36
sfcb:   .res    36
get:    .res    4
mcb:    .res    5
bpb:    .res    8
mblk:   .res    2 + 512

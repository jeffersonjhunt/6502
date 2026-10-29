; XTRATEST: the BDOS's file extras of phase 3b1 (hw/oodos.md, "The BDOS,
; phase 3"): labels, XFCBs, passwords and the multi-sector count, against
; the Programmer's Guide, a case by the command tail's first letter, in the
; error mode that returns errors ($FF), a line a result for
; test_oodos_sys.cpp. "aa xx" is A and X as the BDOS returned them; a
; password is its 8 characters.
;
;   L   on M:: GETL aa xx (101: 31, M:'s label from the cold boot); P.DAT
;       made, a record written, closed; XOFF aa xx: 103 with passwords off
;       (FF 00); LSET aa xx: 100, $B1 (passwords, update and creation
;       stamps) and the label's password LABELPW (bit 0, the DMA's 8-15);
;       GETL2 (B1); LNOPW (FF 07: no password given); LPW (LABELPW in the
;       DMA: 00); XSET: 103 of P.DAT, read mode, SECRET (00); STAMPS aa xx
;       STMODE mm STAMPV 8 bytes (102: 80 and P.DAT's stamps); OPNOPW (FF
;       07: read-protected); OPPW (SECRET given: 00) OPF7 hh (20: f7'
;       clear); XWRITE (103, write mode, the password kept: 00); XFCB?;
;       OPW (no password: 00) OPWF7 (A0: f7' set); WRF7 (FF 03: File R/O);
;       RDF7 (00); DELNOPW / RENNOPW / ATTNOPW / TRNOPW (FF 07); DEFPW (106:
;       SECRET); XDEL5 (19 with f5': the XFCB only, by the default: 00);
;       STAMPS2 STMODE2 (00); XGONE?; MAKEQ (22 with f6': QPASS in the DMA,
;       delete mode in its byte 8: 00); CLOSEQ; QXFCB?; OPQ (00) OPQF7 (20:
;       delete mode opens it for writing); WRQ (00); ATTQ (t1' with QPASS:
;       00); DELQRO (no password: FF 03, File R/O before Password Error);
;       ATTQ2 (t1' clear); DELQNO (FF 07); DELQ (QPASS: 00); QGONE?; LOFF
;       (100, $30: passwords off) GETL3 (31); XOFF2 (FF 00); LON (100, $B0,
;       the label's password the default: 00); MAKES (S.DAT, read mode,
;       SPASS); OPSNO (FF 07, in error mode $FE: said); OPSDEF (106 SPASS:
;       00)
;   N   on M:: NOSFCB? (the test takes M:'s SFCBs and label away), M:
;       reset; GETL0 (00: no label); NSTAMP aa xx (100 asking for stamps: FF
;       00); NPLAIN (100, $81, a new label, its new password 8 zeros: none,
;       kept as blanks: 00); GETLN (81); N.DAT made,
;       NSTAMPS aa xx NSTAMPV (102: 00, zeros)
;   S   on M:: M0, M129 (44: FF 00), M128, M1 (00 00); MS.DAT: 8 records
;       (44 8) MW8 aa xx MW8CR cc (08); 128 more (44 128) MW128 MW128CR
;       (08) EX (01); closed, opened; MR128 (00 00) MRCHK Y (records 0-127);
;       MREND (01 08: 8 read, then the end) MRECHK Y (128-135, the 9th
;       record's place untouched) MRECR (08); MRR (33, record 134, 44 4: 01
;       02) MRRR rrrrrr (R back: 000086) MRRCR (08) MRRCHK Y; MRW (34, 200,
;       44 3: 00 00) MRWR (0000C8) MRWCR (4A: 202's); MRZ (40, 300, 44 2:
;       00 00); closed; MWRO (t1', 44 4: FF 03, the code, not a count);
;       FULL.DAT with 44 128 to the disk's end: MFREE xxxxxx (46 before)
;       MFULL aa xx (02, the records done) MCALLS nn (the calls that wrote
;       128)
;   P   on B:: 13; PH.DAT's records 0-63 written (44 64); FAULT? (the card's
;       writes fail); 44 128: PWRITE aa xx (FF 01) PCR cc PEX ee: the FCB as
;       the records done left it
;   Q   the same by 34: PR.DAT's records 0-63 (44 64), FAULT?, from record
;       64 with 44 128: QWRITE aa xx (FF 01) QR rrrrrr (000040: R back) QCR
;       cc
;   W   on M:: passwords on (100, $B0); W.DAT made with f6' (write mode,
;       WPASS), 129 records, closed; opened without the password; 128 read
;       (44 128) then one more (record 128, extent 1): WRD aa xx (00 00);
;       WRX1 aa xx (FF 03: f7' kept into extent 1)
;   Z   on M:: the label asking for no stamps (100, $00: ZLBL 00 00); DIRTY?
;       (the test fills entry 1's SFCB slot); Z.DAT made (entry 1), closed;
;       ZSTAMPS aa xx ZSTAMPV (102: 00 00, zeros); the slot all 0
;   C   44 = 128, and the program ends: COUNT SET
;   D   CNT.DAT made on M:, one 21 from BIG: DCR cc (01: the CCP set the
;       count to 1 for this program)

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

DRV_M   = 12
BIGRECS = 128

        .zeropage
CNT:    .res    1                       ; records to go
NC:     .res    1                       ; MFULL's calls
BP:     .res    2                       ; a record of BIG

; R0-R2 of `fcb` = r.
.macro RREC fcb, r
        lda     #<(r)
        sta     fcb+33
        lda     #>(r)
        sta     fcb+34
        stz     fcb+35
.endmacro

; "KEY hh": the byte at addr, then CR LF.
.macro BYTE key, addr
        SAY     key
        lda     addr
        jsr     HEX2
        jsr     CRLF
.endmacro

; The 8 characters at addr.
.macro PW addr, str
        .local s
        .assert .strlen(str) = 8, error, "a password is 8 characters"
        .pushseg
        .rodata
s:      .byte   str
        .popseg
        ldx     #7
:       lda     s,x
        sta     addr,x
        dex
        bpl     :-
.endmacro

; Bit 7 of the FCB's byte n set.
.macro ATTR fcb, n
        lda     fcb+n
        ora     #$80
        sta     fcb+n
.endmacro

; RN = the first record, X the count, for fillbig and chkbig.
.macro RECS n, c
        lda     #<(n)
        sta     RN
        lda     #>(n)
        sta     RN+1
        ldx     #c
.endmacro

; Byte 12 of the FCB = v.
.macro EXB fcb, v
        lda     #v
        sta     fcb+12
.endmacro

        .code
start:  CALLF   F_DMAOFF, BUF
        lda     #$FF                    ; errors come back
        ldy     #F_ERRMODE
        jsr     BDOS
        lda     BP_DMA+2                ; the case
        cmp     #'L'
        bne     :+
        jmp     casel
:       cmp     #'N'
        bne     :+
        jmp     casen
:       cmp     #'S'
        bne     :+
        jmp     cases
:       cmp     #'P'
        bne     :+
        jmp     casep
:       cmp     #'Q'
        bne     :+
        jmp     caseq
:       cmp     #'W'
        bne     :+
        jmp     casew
:       cmp     #'Z'
        bne     :+
        jmp     casez
:       cmp     #'C'
        bne     :+
        CALLF   F_MULTISEC, 128
        SAY     "COUNT SET"
        jmp     CRLF
:       cmp     #'D'
        bne     :+
        jmp     cased
:       SAY     "NO CASE"
        jmp     CRLF

done:   CALLF   F_MULTISEC, 1
        SAY     "XTRATEST DONE"
        jmp     CRLF

; ---------------------------------------------------------------------------

casel:  lda     #DRV_M
        sta     TDRV
        TRY     "GETL ", DRV_GETLABEL, DRV_M
        NAME    fcb, "P       DAT"
        TRY     "MAKEP ", F_MAKE, fcb
        stz     RN
        stz     RN+1
        jsr     FILL
        TRY     "WRP ", F_WRITE, fcb
        TRY     "CLOSEP ", F_CLOSE, fcb
        NAME    fcb, "P       DAT"
        EXB     fcb, $81
        TRY     "XOFF ", F_WRITEXFCB, fcb
; The label: passwords on, and its own
        jsr     zero16
        NAME    lfcb, "MYDISK  LBL"
        EXB     lfcb, $B1
        PW      BUF+8, "LABELPW "
        TRY     "LSET ", DRV_SETLABEL, lfcb
        TRY     "GETL2 ", DRV_GETLABEL, DRV_M
        jsr     zero16
        NAME    lfcb, "MYDISK  LBL"
        EXB     lfcb, $B0
        TRY     "LNOPW ", DRV_SETLABEL, lfcb
        PW      BUF, "LABELPW "
        NAME    lfcb, "MYDISK  LBL"
        EXB     lfcb, $B0
        TRY     "LPW ", DRV_SETLABEL, lfcb
; P.DAT: read mode
        jsr     zero16
        PW      BUF+8, "SECRET  "
        NAME    fcb, "P       DAT"
        EXB     fcb, $81
        TRY     "XSET ", F_WRITEXFCB, fcb
        NAME    fcb, "P       DAT"
        TRY     "STAMPS ", F_TIMEDATE, fcb
        BYTE    "STMODE ", fcb+12
        SAY     "STAMPV "
        lda     #<(fcb+24)
        sta     PTR
        lda     #>(fcb+24)
        sta     PTR+1
        ldx     #8
        jsr     HEXN
        jsr     CRLF
        jsr     zero16
        NAME    fcb, "P       DAT"
        TRY     "OPNOPW ", F_OPEN, fcb
        PW      BUF, "SECRET  "
        NAME    fcb, "P       DAT"
        TRY     "OPPW ", F_OPEN, fcb
        BYTE    "OPF7 ", fcb+7
; write mode, the password kept
        NAME    fcb, "P       DAT"
        EXB     fcb, $40
        TRY     "XWRITE ", F_WRITEXFCB, fcb
        KEYWAIT "XFCB?"
        jsr     zero16
        NAME    fcb, "P       DAT"
        TRY     "OPW ", F_OPEN, fcb
        BYTE    "OPWF7 ", fcb+7
        lda     #1
        sta     RN
        jsr     FILL
        TRY     "WRF7 ", F_WRITE, fcb
        stz     fcb+32
        TRY     "RDF7 ", F_READ, fcb
; what needs the password
        jsr     zero16
        NAME    fcb, "P       DAT"
        TRY     "DELNOPW ", F_DELETE, fcb
        NAME    fcb, "P       DAT"
        ldx     #10
:       lda     rname,x
        sta     fcb+17,x
        dex
        bpl     :-
        TRY     "RENNOPW ", F_RENAME, fcb
        NAME    fcb, "P       DAT"
        TRY     "ATTNOPW ", F_ATTRIB, fcb
        NAME    fcb, "P       DAT"
        RREC    fcb, 0
        TRY     "TRNOPW ", F_TRUNCATE, fcb
; the default password
        PW      dpw, "SECRET  "
        TRY     "DEFPW ", F_PASSWD, dpw
        NAME    fcb, "P       DAT"
        ATTR    fcb, 5
        TRY     "XDEL5 ", F_DELETE, fcb
        NAME    fcb, "P       DAT"
        TRY     "STAMPS2 ", F_TIMEDATE, fcb
        BYTE    "STMODE2 ", fcb+12
        KEYWAIT "XGONE?"
; make with f6': delete mode
        jsr     zero16
        PW      BUF, "QPASS   "
        lda     #$20
        sta     BUF+8
        NAME    fcb, "Q       DAT"
        ATTR    fcb, 6
        TRY     "MAKEQ ", F_MAKE, fcb
        TRY     "CLOSEQ ", F_CLOSE, fcb
        KEYWAIT "QXFCB?"
        jsr     zero16
        NAME    fcb, "Q       DAT"
        TRY     "OPQ ", F_OPEN, fcb
        BYTE    "OPQF7 ", fcb+7
        lda     #2
        sta     RN
        jsr     FILL
        TRY     "WRQ ", F_WRITE, fcb
        CALLF   F_CLOSE, fcb
        jsr     zero16
        PW      BUF, "QPASS   "
        NAME    fcb, "Q       DAT"
        ATTR    fcb, 9
        TRY     "ATTQ ", F_ATTRIB, fcb
        jsr     zero16
        NAME    fcb, "Q       DAT"
        TRY     "DELQRO ", F_DELETE, fcb
        PW      BUF, "QPASS   "
        NAME    fcb, "Q       DAT"
        TRY     "ATTQ2 ", F_ATTRIB, fcb
        jsr     zero16
        NAME    fcb, "Q       DAT"
        TRY     "DELQNO ", F_DELETE, fcb
        PW      BUF, "QPASS   "
        NAME    fcb, "Q       DAT"
        TRY     "DELQ ", F_DELETE, fcb
        KEYWAIT "QGONE?"
; passwords off, and on again by the default
        PW      BUF, "LABELPW "
        NAME    lfcb, "MYDISK  LBL"
        EXB     lfcb, $30
        TRY     "LOFF ", DRV_SETLABEL, lfcb
        TRY     "GETL3 ", DRV_GETLABEL, DRV_M
        NAME    fcb, "P       DAT"
        EXB     fcb, $80
        TRY     "XOFF2 ", F_WRITEXFCB, fcb
        PW      dpw, "LABELPW "
        CALLF   F_PASSWD, dpw
        jsr     zero16
        NAME    lfcb, "MYDISK  LBL"
        EXB     lfcb, $B0
        TRY     "LON ", DRV_SETLABEL, lfcb
        jsr     zero16
        PW      BUF, "SPASS   "
        lda     #$80
        sta     BUF+8
        NAME    fcb, "S       DAT"
        ATTR    fcb, 6
        TRY     "MAKES ", F_MAKE, fcb
        CALLF   F_CLOSE, fcb
        jsr     zero16
        lda     #$FE                    ; return and say
        ldy     #F_ERRMODE
        jsr     BDOS
        NAME    fcb, "S       DAT"
        TRY     "OPSNO ", F_OPEN, fcb
        lda     #$FF
        ldy     #F_ERRMODE
        jsr     BDOS
        PW      dpw, "SPASS   "
        CALLF   F_PASSWD, dpw
        NAME    fcb, "S       DAT"
        TRY     "OPSDEF ", F_OPEN, fcb
        jmp     done

; BUF's first 16 bytes 0.
zero16: ldx     #15
:       stz     BUF,x
        dex
        bpl     :-
        rts

; ---------------------------------------------------------------------------

casen:  lda     #DRV_M
        sta     TDRV
        CALLF   DRV_SPACE, DRV_M        ; logged in
        KEYWAIT "NOSFCB?"
        CALLF   DRV_RESET, 1 << DRV_M
        TRY     "GETL0 ", DRV_GETLABEL, DRV_M
        NAME    lfcb, "NOSTAMP LBL"
        EXB     lfcb, $B1
        TRY     "NSTAMP ", DRV_SETLABEL, lfcb
        NAME    lfcb, "NOSTAMP LBL"
        EXB     lfcb, $81
        TRY     "NPLAIN ", DRV_SETLABEL, lfcb
        TRY     "GETLN ", DRV_GETLABEL, DRV_M
        NAME    fcb, "N       DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_CLOSE, fcb
        NAME    fcb, "N       DAT"
        lda     #$55                    ; 102 sets bytes 24-31
        ldx     #7
:       sta     fcb+24,x
        dex
        bpl     :-
        TRY     "NSTAMPS ", F_TIMEDATE, fcb
        SAY     "NSTAMPV "
        lda     #<(fcb+24)
        sta     PTR
        lda     #>(fcb+24)
        sta     PTR+1
        ldx     #8
        jsr     HEXN
        jsr     CRLF
        jmp     done

; ---------------------------------------------------------------------------

cases:  lda     #DRV_M
        sta     TDRV
        TRY     "M0 ", F_MULTISEC, 0
        TRY     "M129 ", F_MULTISEC, 129
        TRY     "M128 ", F_MULTISEC, 128
        TRY     "M1 ", F_MULTISEC, 1
        CALLF   F_DMAOFF, BIG
        NAME    fcb, "MS      DAT"
        CALLF   F_MAKE, fcb
        RECS    0, 8 ; records 0-7
        jsr     fillbig
        CALLF   F_MULTISEC, 8
        TRY     "MW8 ", F_WRITE, fcb
        BYTE    "MW8CR ", fcb+32
        RECS    8, 128 ; 8-135
        jsr     fillbig
        CALLF   F_MULTISEC, 128
        TRY     "MW128 ", F_WRITE, fcb
        BYTE    "MW128CR ", fcb+32
        BYTE    "MW128EX ", fcb+12
        CALLF   F_CLOSE, fcb
        NAME    fcb, "MS      DAT"
        CALLF   F_OPEN, fcb
        jsr     e7big
        TRY     "MR128 ", F_READ, fcb
        RECS    0, 128
        jsr     chkbig
        SAY     "MRCHK "
        jsr     YESNO
        jsr     e7big
        TRY     "MREND ", F_READ, fcb
        RECS    128, 8
        jsr     chkbig
        bcs     :+
        lda     BIG+8*128               ; the 9th record's place as it was
        cmp     #$E7
        bne     :+
        lda     BIG+9*128-1
        cmp     #$E7
        bne     :+
        clc
        bra     :++
:       sec
:       SAY     "MRECHK "
        jsr     YESNO
        BYTE    "MRECR ", fcb+32
; random
        CALLF   F_MULTISEC, 4
        RREC    fcb, 134
        jsr     e7big
        TRY     "MRR ", F_READRAND, fcb
        SAY     "MRRR "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF
        BYTE    "MRRCR ", fcb+32
        RECS    134, 2
        jsr     chkbig
        SAY     "MRRCHK "
        jsr     YESNO
        CALLF   F_MULTISEC, 3
        RREC    fcb, 200
        RECS    200, 3
        jsr     fillbig
        TRY     "MRW ", F_WRITERAND, fcb
        SAY     "MRWR "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF
        BYTE    "MRWCR ", fcb+32
        CALLF   F_MULTISEC, 2
        RREC    fcb, 300
        RECS    300, 2
        jsr     fillbig
        TRY     "MRZ ", F_WRITEZF, fcb
        CALLF   F_CLOSE, fcb
; a physical error's code, not a count
        NAME    fcb, "MS      DAT"
        ATTR    fcb, 9
        CALLF   F_ATTRIB, fcb
        NAME    fcb, "MS      DAT"
        CALLF   F_OPEN, fcb
        CALLF   F_MULTISEC, 4
        TRY     "MWRO ", F_WRITE, fcb
        NAME    fcb, "MS      DAT"
        CALLF   F_ATTRIB, fcb
; to the disk's end
        CALLF   F_DMAOFF, BUF
        CALLF   DRV_SPACE, DRV_M
        SAY     "MFREE "
        lda     #<BUF
        ldx     #>BUF
        jsr     HEX6
        jsr     CRLF
        CALLF   F_DMAOFF, BIG
        NAME    fcb, "FULL    DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_MULTISEC, 128
        stz     NC
:       CALLF   F_WRITE, fcb
        sta     RA
        stx     RX
        cmp     #0
        bne     :+
        inc     NC
        bra     :-
:       SAY     "MFULL "
        lda     RA
        ldx     RX
        jsr     RES
        BYTE    "MCALLS ", NC
        CALLF   F_CLOSE, fcb
        jmp     done

; ---------------------------------------------------------------------------

casep:  lda     #1                      ; B:
        sta     TDRV
        CALLF   DRV_ALLRESET, 0         ; every buffer empty
        CALLF   F_DMAOFF, BIG
        NAME    fcb, "PH      DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_MULTISEC, 64
        CALLF   F_WRITE, fcb
        KEYWAIT "FAULT?"
        CALLF   F_MULTISEC, 128
        TRY     "PWRITE ", F_WRITE, fcb
        BYTE    "PCR ", fcb+32
        BYTE    "PEX ", fcb+12
        jmp     done

caseq:  lda     #1
        sta     TDRV
        CALLF   DRV_ALLRESET, 0
        CALLF   F_DMAOFF, BIG
        NAME    fcb, "PR      DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_MULTISEC, 64
        RREC    fcb, 0
        CALLF   F_WRITERAND, fcb
        KEYWAIT "FAULT?"
        CALLF   F_MULTISEC, 128
        RREC    fcb, 64
        TRY     "QWRITE ", F_WRITERAND, fcb
        SAY     "QR "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF
        BYTE    "QCR ", fcb+32
        jmp     done

; ---------------------------------------------------------------------------

casew:  lda     #DRV_M
        sta     TDRV
        jsr     zero16
        NAME    lfcb, "PWON    LBL"
        EXB     lfcb, $B0
        CALLF   DRV_SETLABEL, lfcb
        PW      BUF, "WPASS   "
        lda     #$40
        sta     BUF+8
        NAME    fcb, "W       DAT"
        ATTR    fcb, 6
        CALLF   F_MAKE, fcb
        CALLF   F_DMAOFF, BIG
        RECS    0, 128
        jsr     fillbig
        CALLF   F_MULTISEC, 128
        CALLF   F_WRITE, fcb
        CALLF   F_MULTISEC, 1
        CALLF   F_WRITE, fcb
        CALLF   F_CLOSE, fcb
        CALLF   F_DMAOFF, BUF
        jsr     zero16
        NAME    fcb, "W       DAT"
        CALLF   F_OPEN, fcb
        CALLF   F_DMAOFF, BIG
        CALLF   F_MULTISEC, 128
        CALLF   F_READ, fcb
        CALLF   F_MULTISEC, 1
        TRY     "WRD ", F_READ, fcb
        TRY     "WRX1 ", F_WRITE, fcb
        jmp     done

casez:  lda     #DRV_M
        sta     TDRV
        NAME    lfcb, "NOSTAMPSLBL"
        EXB     lfcb, $00
        TRY     "ZLBL ", DRV_SETLABEL, lfcb
        KEYWAIT "DIRTY?"
        NAME    fcb, "Z       DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_CLOSE, fcb
        NAME    fcb, "Z       DAT"
        TRY     "ZSTAMPS ", F_TIMEDATE, fcb
        SAY     "ZSTAMPV "
        lda     #<(fcb+24)
        sta     PTR
        lda     #>(fcb+24)
        sta     PTR+1
        ldx     #8
        jsr     HEXN
        jsr     CRLF
        jmp     done

cased:  lda     #DRV_M
        sta     TDRV
        CALLF   F_DMAOFF, BIG
        NAME    fcb, "CNT     DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_WRITE, fcb
        BYTE    "DCR ", fcb+32
        CALLF   F_CLOSE, fcb
        jmp     done

; ---------------------------------------------------------------------------

; BP = BIG, its first record; nextbp the next.
bigat:  lda     #<BIG
        sta     BP
        lda     #>BIG
        sta     BP+1
        rts
nextbp: clc
        lda     BP
        adc     #128
        sta     BP
        bcc     :+
        inc     BP+1
:       rts

; BIG's records = the patterns of records RN, RN + 1, ..., X of them.
fillbig:
        stx     CNT
        jsr     bigat
@rec:   jsr     FILL
        ldy     #127
:       lda     BUF,y
        sta     (BP),y
        dey
        bpl     :-
        jsr     nextbp
        jsr     nextrn
        dec     CNT
        bne     @rec
        rts

nextrn: inc     RN
        bne     :+
        inc     RN+1
:       rts

; Carry set if BIG's X records aren't the patterns of RN, RN + 1, ...
chkbig: stx     CNT
        jsr     bigat
@rec:   ldy     #127
:       lda     (BP),y
        sta     BUF,y
        dey
        bpl     :-
        jsr     CHECK
        bcs     @bad
        jsr     nextbp
        jsr     nextrn
        dec     CNT
        bne     @rec
        clc
@bad:   rts

; BIG all $E7.
e7big:  jsr     bigat
        ldx     #BIGRECS
@rec:   ldy     #127
        lda     #$E7
:       sta     (BP),y
        dey
        bpl     :-
        jsr     nextbp
        dex
        bne     @rec
        rts

        .rodata
rname:  .byte   "R       DAT"

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36
lfcb:   .res    36
dpw:    .res    8
BIG:    .res    BIGRECS * 128

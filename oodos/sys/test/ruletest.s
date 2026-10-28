; RULETEST: the BDOS against the Programmer's Guide where the review of
; phase 3a found it wrong (hw/oodos.md, "The BDOS, phase 3"), a case by the
; command tail's first letter, in the error mode that returns errors ($FF),
; a line a result for test_oodos_sys.cpp. "aa xx" is A and X as the BDOS
; returned them.
;
;   M   on M::
;       MAKE / OPEN / CLOSE aa xx  X.DAT, entry 1: directory code 0 (2.3)
;       SWRITE3 aa xx SWCR cc EX ee  two FCBs on X.DAT with different
;                       blocks for one record: the second's write at CR 128
;                       can't close its extent (1), F as it was: CR 80, EX 0
;       FPTR1 hh        the second FCB's pointer 1, not taken from the entry
;       SREAD3 aa xx SRCR cc  a read at CR 128 the same: 1, CR 80
;       RWRITE3 aa xx RWCR cc  a random write to record 300: 3, CR 2C
;       XPTR0 hh        X.DAT's entry on the disk after its sector's next
;                       write (Y.DAT made, M: read again): pointer 0 not
;                       the second FCB's
;       ATTR aa xx      30 with f6', CR 55, on H.DAT (records 0, 40, 200,
;                       300, 600): 0
;       BYTES cc        open with CR FF: CR the byte count, 55 (fn 15)
;       F6CR cc         open with f6' and CR 3: CR 03 (f6' is 30's)
;       OPENS2 aa xx S2 ss RC rr  open of EX 2 with S2 1: S2 taken as 0
;       SRCHS2 aa xx    search of EX 2 with S2 5: found, entry 5
;       MAKEATTR aa xx FCBATTR hh hh ss  make with f1', f6', t1', t2' and
;                       S2 7: the FCB's attributes 0, S2 80 (2.3.4, fn 22)
;       ATTRWRITE aa xx  a write: not read-only
;       ENTATTR hh hh hh ss  the entry's bytes 1, 9, 10 and 14
;       MAKEEXT aa xx   make of H.DAT's extent 6, which it hasn't: FF 08
;       THOLE / TNOBLK / TPASTRC / TNOEXT aa xx  truncate to records 20,
;                       270, 210, 400, where H.DAT has no data: FF 00
;       TDATA aa xx TSIZE xxxxxx  to record 40: 00 00, size 000029
;   D   on B: (a writable unit): DFREE0 xxxxxx with DEL.DAT and TR.DAT
;       written; REMOUNT? (the unit made read-only behind the BDOS's back);
;       DELRO aa xx (FF 02) DFREE1 xxxxxx: no block freed; WRITABLE? (the
;       unit writable again), B: reset: TFREE0 xxxxxx; REMOUNT2?; TRRO aa
;       xx (FF 02) TFREE1 xxxxxx
;   C   on B:: CHG1.TXT written, not closed; BUSY? (the card busy at the
;       next login); A: reset and selected: SELA aa xx (FF 04); READY?;
;       CLOSEB aa xx (00 00: B:'s drive table kept); SELA2 aa xx; CHG2.TXT
;       made and its record ($A5C3's pattern) written, not closed; SWAP?
;       (another image as unit 2); A: reset (the units read again: B:'s
;       buffers dropped, not written to the new image) and selected: SELA3
;       aa xx, LOGV aa xx (01 00: B: logged out), NEWB aa xx (README.TXT
;       found on the new image); A: again, the units as they were: SELA4 aa
;       xx, LOGV2 aa xx (03 00)
;   R   on B:: SWAP37.TXT made, its record ($A5C3's) written, not closed;
;       SWAP?; RESETB aa xx: 37 of B: (00 00), which reads the units first,
;       so the old image's buffer is dropped, not written to the new one
;   W   the same with SWAP48.TXT; SWAP?; a BIOS SELDSK of B: as a first
;       select (the units read again: DRVCHG); SELDSK; then the program's
;       end, the warm boot's 48, which drops B:'s buffer (CHANGED)
;   F   on B:: BIG.DAT, 160 records, closed; FAULT.DAT's records 0-2
;       written and closed, opened, record 3 written (one changed buffer):
;       FWRITE aa; FAULT? (the card's writes fail); FCLOSE1 and FCLOSE2 aa
;       xx (FF 01: the buffer kept, so a close tried again fails again);
;       FBIG nn: BIG.DAT read, the reads that failed (00: GETBUF takes the
;       kept buffer last, so other sectors still come and go); FIXED?; FCLOSE3 aa xx (00 00); B:
;       reset, FREAD yyyy (records 0-3 read back, Y each); record 4
;       written, FAULT2?; FRESET aa xx: 37 of B: (FF 01: the buffer dropped,
;       B: logged out all the same); FIXED2?; FRESET2 aa xx (00 00:
;       nothing left to write)
;   X   on M:: SIZENONE aa xx SIZER xxxxxx: 35 of no file, R0-R2 $123456
;       before: FF 00, 000000; ST.DAT, 300 records, closed (its second
;       entry, extent 2, has no stamps: the test reads them); SEQ.DAT, 256
;       records (extents 0-1, one entry), closed; the directory filled:
;       DIRFULL aa; SEQ.DAT read to its end, SEQEND aa (01: extent 2 has no
;       entry), then written: SEQW aa (01: no directory space, at once);
;       RREAD aa xx (04) of record 256 and RWRITE aa xx (05) to it; RREADZ
;       and RWRITEZ the same with 40
;   E   on A: (oodos-32m, EXM 3) after M: (EXM 1) logged in: EXM.DAT, 384
;       records (extents 0-2, in one entry), closed: EXMENTS nn, its
;       entries (01)
;   U   the user 5: USER SET
;   V   USER uu

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

DRV_M   = 12

        .zeropage
CNT:    .res    1                       ; make40's records to go
CNT2:   .res    2                       ; a loop's count

; R0-R2 of `fcb` = r.
.macro RREC fcb, r
        lda     #<(r)
        sta     fcb+33
        lda     #>(r)
        sta     fcb+34
        stz     fcb+35
.endmacro

; "KEY hh": the byte at addr.
.macro BYTE key, addr
        SAY     key
        lda     addr
        jsr     HEX2
.endmacro

        .code
start:  CALLF   F_DMAOFF, BUF
        lda     #$FF                    ; errors come back
        ldy     #F_ERRMODE
        jsr     BDOS
        lda     BP_DMA+2                ; the case
        cmp     #'M'
        bne     :+
        jmp     casem
:       cmp     #'D'
        bne     :+
        jmp     cased
:       cmp     #'C'
        bne     :+
        jmp     casec
:       cmp     #'R'
        bne     :+
        jmp     caser
:       cmp     #'W'
        bne     :+
        jmp     casew
:       cmp     #'F'
        bne     :+
        jmp     casef
:       cmp     #'X'
        bne     :+
        jmp     casex
:       cmp     #'E'
        bne     :+
        jmp     casee
:       cmp     #'U'
        bne     :+
        lda     #5
        ldy     #F_USERNUM
        jsr     BDOS
        SAY     "USER SET"
        jmp     CRLF
:       cmp     #'V'
        bne     :+
        SAY     "USER "
        lda     #$FF
        ldy     #F_USERNUM
        jsr     BDOS
        jsr     HEX2
        jmp     CRLF
:       SAY     "NO CASE"
        jmp     CRLF

; ---------------------------------------------------------------------------

casem:  lda     #DRV_M
        sta     TDRV
        NAME    fcb, "X       DAT"
        TRY     "MAKE ", F_MAKE, fcb
        NAME    fcb2, "X       DAT"
        TRY     "OPEN ", F_OPEN, fcb2
        RREC    fcb, 16                 ; fcb: records 16 and 32 (pointers 1, 2)
        CALLF   F_WRITERAND, fcb
        RREC    fcb, 32
        CALLF   F_WRITERAND, fcb
        RREC    fcb2, 0                 ; fcb2: 0 and 32 (pointers 0, 2)
        CALLF   F_WRITERAND, fcb2
        RREC    fcb2, 32
        CALLF   F_WRITERAND, fcb2
        TRY     "CLOSE ", F_CLOSE, fcb
        lda     #128                    ; pointer 2: two blocks, so fcb2's
        sta     fcb2+32                 ; extent can't be closed
        SAY     "SWRITE3 "
        CALLF   F_WRITE, fcb2
        jsr     RES
        BYTE    "SWCR ", fcb2+32
        BYTE    " EX ", fcb2+12
        jsr     CRLF
        BYTE    "FPTR1 ", fcb2+17
        jsr     CRLF
        SAY     "SREAD3 "
        CALLF   F_READ, fcb2
        jsr     RES
        BYTE    "SRCR ", fcb2+32
        jsr     CRLF
        RREC    fcb2, 300
        TRY     "RWRITE3 ", F_WRITERAND, fcb2
        BYTE    "RWCR ", fcb2+32
        jsr     CRLF
        NAME    fcb3, "Y       DAT"     ; entry 2: X.DAT's sector written
        CALLF   F_MAKE, fcb3
        CALLF   DRV_RESET, 1 << DRV_M   ; read again
        NAME    fcb3, "X       DAT"
        CALLF   F_SFIRST, fcb3
        jsr     entry
        SAY     "XPTR0 "
        ldy     #16
        lda     (PTR),y
        jsr     HEX2
        jsr     CRLF

        NAME    fcb, "H       DAT"      ; records 0, 40, 200, 300, 600
        CALLF   F_MAKE, fcb
        ldx     #0
@rec:   lda     hrecs,x
        sta     fcb+33
        lda     hrecs+1,x
        sta     fcb+34
        phx
        CALLF   F_WRITERAND, fcb
        plx
        inx
        inx
        cpx     #10
        bne     @rec
        CALLF   F_CLOSE, fcb
        NAME    fcb, "H       DAT"
        lda     fcb+6
        ora     #$80                    ; f6': the byte count
        sta     fcb+6
        lda     #$55
        sta     fcb+32
        TRY     "ATTR ", F_ATTRIB, fcb
        NAME    fcb, "H       DAT"
        lda     #$FF
        sta     fcb+32
        CALLF   F_OPEN, fcb
        BYTE    "BYTES ", fcb+32
        jsr     CRLF
        NAME    fcb, "H       DAT"
        lda     fcb+6
        ora     #$80
        sta     fcb+6
        lda     #3
        sta     fcb+32
        CALLF   F_OPEN, fcb
        BYTE    "F6CR ", fcb+32
        jsr     CRLF
        NAME    fcb, "H       DAT"
        lda     #2
        sta     fcb+12
        lda     #1
        sta     fcb+14
        SAY     "OPENS2 "
        CALLF   F_OPEN, fcb
        jsr     RES0
        BYTE    " S2 ", fcb+14
        BYTE    " RC ", fcb+15
        jsr     CRLF
        NAME    fcb3, "H       DAT"
        lda     #2
        sta     fcb3+12
        lda     #5
        sta     fcb3+14
        TRY     "SRCHS2 ", F_SFIRST, fcb3

        NAME    fcb, "A       TXT"
        lda     fcb+1                   ; f1', f6', t1', t2'
        ora     #$80
        sta     fcb+1
        lda     fcb+6
        ora     #$80
        sta     fcb+6
        lda     fcb+9
        ora     #$80
        sta     fcb+9
        lda     fcb+10
        ora     #$80
        sta     fcb+10
        lda     #7
        sta     fcb+14
        TRY     "MAKEATTR ", F_MAKE, fcb
        BYTE    "FCBATTR ", fcb+1
        BYTE    " ", fcb+9
        BYTE    " ", fcb+14
        jsr     CRLF
        TRY     "ATTRWRITE ", F_WRITE, fcb
        CALLF   F_CLOSE, fcb
        NAME    fcb3, "A       TXT"
        CALLF   F_SFIRST, fcb3
        jsr     entry
        SAY     "ENTATTR "
        ldy     #1
        jsr     ebyte
        ldy     #9
        jsr     ebyte
        ldy     #10
        jsr     ebyte
        ldy     #14
        lda     (PTR),y
        jsr     HEX2
        jsr     CRLF
        NAME    fcb, "H       DAT"
        lda     #6
        sta     fcb+12
        TRY     "MAKEEXT ", F_MAKE, fcb

        NAME    fcb, "H       DAT"
        RREC    fcb, 20                 ; pointer 1: no block
        TRY     "THOLE ", F_TRUNCATE, fcb
        RREC    fcb, 270                ; extent 2's entry, its pointer 0: none
        TRY     "TNOBLK ", F_TRUNCATE, fcb
        RREC    fcb, 210                ; extent 1, past its RC (73)
        TRY     "TPASTRC ", F_TRUNCATE, fcb
        RREC    fcb, 400                ; extent 3: its group's entry ends at 2
        TRY     "TNOEXT ", F_TRUNCATE, fcb
        RREC    fcb, 40
        TRY     "TDATA ", F_TRUNCATE, fcb
        NAME    fcb, "H       DAT"
        CALLF   F_SIZE, fcb
        SAY     "TSIZE "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF
        jmp     done

; PTR at the entry 17 found (A its place in BUF's record).
entry:  asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        clc
        adc     #<BUF
        sta     PTR
        lda     #>BUF
        adc     #0
        sta     PTR+1
        rts

; The entry's byte Y, and a space.
ebyte:  lda     (PTR),y
        jsr     HEX2
        jmp     SPACE

; RES without its CR LF.
RES0:   sta     RA
        stx     RX
        jsr     HEX2
        jsr     SPACE
        lda     RX
        jmp     HEX2

; ---------------------------------------------------------------------------

cased:  lda     #1                      ; B:
        sta     TDRV
        NAME    fcb, "DEL     DAT"
        jsr     make40
        NAME    fcb, "TR      DAT"
        jsr     make40
        SAY     "DFREE0 "
        jsr     space
        KEYWAIT "REMOUNT?"
        NAME    fcb, "DEL     DAT"
        TRY     "DELRO ", F_DELETE, fcb
        SAY     "DFREE1 "
        jsr     space
        KEYWAIT "WRITABLE?"
        CALLF   DRV_RESET, 2            ; B: read-write, read again
        SAY     "TFREE0 "
        jsr     space
        KEYWAIT "REMOUNT2?"
        NAME    fcb, "TR      DAT"
        RREC    fcb, 5
        TRY     "TRRO ", F_TRUNCATE, fcb
        SAY     "TFREE1 "
        jsr     space
        jmp     done

; `fcb` made with 40 records, closed.
make40: CALLF   F_MAKE, fcb
        lda     #40
        sta     CNT
@rec:   CALLF   F_WRITE, fcb
        dec     CNT
        bne     @rec
        CALLF   F_CLOSE, fcb
        rts

; B:'s free records.
space:  lda     #1
        ldy     #DRV_SPACE
        jsr     BDOS
        lda     #<BUF
        ldx     #>BUF
        jsr     HEX6
        jmp     CRLF

; ---------------------------------------------------------------------------

casec:  lda     #1
        sta     TDRV
        NAME    fcb, "CHG1    TXT"
        CALLF   F_MAKE, fcb
        CALLF   F_WRITE, fcb            ; in a buffer, not on the card
        KEYWAIT "BUSY?"
        CALLF   DRV_RESET, 1            ; A: logged out, so a select logs it in
        TRY     "SELA ", DRV_SET, 0
        KEYWAIT "READY?"
        TRY     "CLOSEB ", F_CLOSE, fcb
        TRY     "SELA2 ", DRV_SET, 0
        NAME    fcb, "CHG2    TXT"
        jsr     marked                  ; in a buffer, not on the card
        KEYWAIT "SWAP?"
        CALLF   DRV_RESET, 1
        TRY     "SELA3 ", DRV_SET, 0
        TRY     "LOGV ", DRV_LOGINVEC, 0
        NAME    fcb, "README  TXT"
        TRY     "NEWB ", F_SFIRST, fcb
        CALLF   DRV_RESET, 1
        TRY     "SELA4 ", DRV_SET, 0
        TRY     "LOGV2 ", DRV_LOGINVEC, 0
        ; fall through

done:   SAY     "RULETEST DONE"
        jmp     CRLF

; `fcb` made, its record 0 $A5C3's pattern, not closed: in a buffer.
marked: CALLF   F_MAKE, fcb
        lda     #$C3
        sta     RN
        lda     #$A5
        sta     RN+1
        jsr     FILL
        CALLF   F_WRITE, fcb
        rts

; ---------------------------------------------------------------------------

caser:  lda     #1
        sta     TDRV
        NAME    fcb, "SWAP37  TXT"
        jsr     marked
        KEYWAIT "SWAP?"
        TRY     "RESETB ", DRV_RESET, 2
        jmp     done

casew:  lda     #1
        sta     TDRV
        NAME    fcb, "SWAP48  TXT"
        jsr     marked
        KEYWAIT "SWAP?"
        lda     #BIOS::SELDSK
        ldx     #1
        ldy     #0                      ; a first select: the units read again
        jsr     bios
        sta     RA
        stx     RX
        SAY     "SELDSK "
        lda     RA
        ldx     RX
        jsr     RES
        jmp     done

; BIOS function A, with X and Y as its A and X, through function 50.
bios:   sta     pb
        stx     pb+1
        sty     pb+2
        lda     #<pb
        ldx     #>pb
        ldy     #S_BIOS
        jmp     BDOS

; ---------------------------------------------------------------------------

casef:  lda     #1
        sta     TDRV
        NAME    fcb, "BIG     DAT"      ; 160 records: 40 sectors, more than the buffers
        lda     #<160
        ldx     #>160
        jsr     makeN
        NAME    fcb, "FAULT   DAT"
        CALLF   F_MAKE, fcb
        stz     RN+1
        stz     RN
@rec:   jsr     FILL
        CALLF   F_WRITE, fcb
        inc     RN
        lda     RN
        cmp     #3
        bne     @rec
        CALLF   F_CLOSE, fcb
        NAME    fcb, "FAULT   DAT"
        CALLF   F_OPEN, fcb
        lda     #3
        sta     fcb+32
        jsr     FILL                    ; RN 3
        SAY     "FWRITE "
        CALLF   F_WRITE, fcb
        jsr     HEX2
        jsr     CRLF
        KEYWAIT "FAULT?"
        TRY     "FCLOSE1 ", F_CLOSE, fcb
        TRY     "FCLOSE2 ", F_CLOSE, fcb
        NAME    fcb2, "BIG     DAT"     ; every buffer taken in turn: but the kept one
        CALLF   F_OPEN, fcb2
        stz     CNT
        lda     #160
        sta     CNT2
@big:   CALLF   F_READ, fcb2
        cmp     #0
        beq     :+
        inc     CNT
:       dec     CNT2
        bne     @big
        SAY     "FBIG "
        lda     CNT
        jsr     HEX2
        jsr     CRLF
        KEYWAIT "FIXED?"
        TRY     "FCLOSE3 ", F_CLOSE, fcb
        CALLF   DRV_RESET, 2            ; read from the card again
        NAME    fcb, "FAULT   DAT"
        CALLF   F_OPEN, fcb
        SAY     "FREAD "
        stz     RN
@read:  CALLF   F_READ, fcb
        jsr     CHECK
        lda     #'Y'
        bcc     :+
        lda     #'N'
:       jsr     PUTC
        inc     RN
        lda     RN
        cmp     #4
        bne     @read
        jsr     CRLF
        jsr     FILL                    ; RN 4, at CR 4
        CALLF   F_WRITE, fcb
        KEYWAIT "FAULT2?"
        TRY     "FRESET ", DRV_RESET, 2
        KEYWAIT "FIXED2?"
        TRY     "FRESET2 ", DRV_RESET, 2
        jmp     done

; ---------------------------------------------------------------------------

casex:  lda     #DRV_M
        sta     TDRV
        NAME    fcb, "NOFILE  DAT"
        lda     #$56
        sta     fcb+33
        lda     #$34
        sta     fcb+34
        lda     #$12
        sta     fcb+35
        TRY     "SIZENONE ", F_SIZE, fcb
        SAY     "SIZER "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF
        NAME    fcb, "ST      DAT"      ; extents 0-1 one entry, 2 another
        lda     #<300
        ldx     #>300
        jsr     makeN
        NAME    fcb, "SEQ     DAT"
        lda     #<256
        ldx     #>256
        jsr     makeN
        NAME    fcb3, "DF00    DAT"     ; the directory filled: DFAA.DAT ...
        stz     CNT2
@fill:  lda     CNT2
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        clc
        adc     #'A'
        sta     fcb3+3
        lda     CNT2
        and     #$0F
        clc
        adc     #'A'
        sta     fcb3+4
        stz     fcb3+12
        stz     fcb3+14
        stz     fcb3+15
        CALLF   F_MAKE, fcb3
        cmp     #$FF
        beq     @full
        inc     CNT2
        bne     @fill
@full:  sta     RA
        SAY     "DIRFULL "
        lda     RA
        jsr     HEX2
        jsr     CRLF
        NAME    fcb, "SEQ     DAT"
        CALLF   F_OPEN, fcb
        stz     CNT2
        stz     CNT2+1
@read:  CALLF   F_READ, fcb
        inc     CNT2
        bne     @read
        inc     CNT2+1
        lda     CNT2+1
        cmp     #1
        bne     @read
        SAY     "SEQEND "
        CALLF   F_READ, fcb
        jsr     HEX2
        jsr     CRLF
        SAY     "SEQW "
        CALLF   F_WRITE, fcb
        jsr     HEX2
        jsr     CRLF
        NAME    fcb2, "SEQ     DAT"
        CALLF   F_OPEN, fcb2
        RREC    fcb2, 256
        TRY     "RREAD ", F_READRAND, fcb2
        TRY     "RWRITE ", F_WRITERAND, fcb2
        NAME    fcb2, "SEQ     DAT"
        CALLF   F_OPEN, fcb2
        RREC    fcb2, 256
        TRY     "RREADZ ", F_READRAND, fcb2
        TRY     "RWRITEZ ", F_WRITEZF, fcb2
        jmp     done

; `fcb` made with A/X records of 0s, closed.
makeN:  sta     CNT2
        stx     CNT2+1
        CALLF   F_MAKE, fcb
@rec:   CALLF   F_WRITE, fcb
        lda     CNT2
        bne     :+
        dec     CNT2+1
:       dec     CNT2
        lda     CNT2
        ora     CNT2+1
        bne     @rec
        CALLF   F_CLOSE, fcb
        rts

; ---------------------------------------------------------------------------

casee:  stz     TDRV                    ; A:, then M: logged in
        lda     #0
        ldy     #DRV_SPACE
        jsr     BDOS
        lda     #DRV_M
        ldy     #DRV_SPACE
        jsr     BDOS
        NAME    fcb, "EXM     DAT"
        lda     #<384
        ldx     #>384
        jsr     makeN
        NAME    fcb3, "EXM     DAT"
        lda     #'?'
        sta     fcb3+12                 ; every extent
        stz     CNT
        CALLF   F_SFIRST, fcb3
@count: cmp     #$FF
        beq     @counted
        inc     CNT
        CALLF   F_SNEXT, fcb3
        bra     @count
@counted:
        SAY     "EXMENTS "
        lda     CNT
        jsr     HEX2
        jsr     CRLF
        jmp     done

        .rodata
hrecs:  .word   0, 40, 200, 300, 600

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36
fcb2:   .res    36
fcb3:   .res    36
pb:     .res    8

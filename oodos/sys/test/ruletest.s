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
;       CLOSEB aa xx (00 00: B:'s drive table kept); SELA2 aa xx; SWAP?
;       (another image as unit 2); A: again: SELA3 aa xx, LOGV aa xx (01 00:
;       B: logged out), NEWB aa xx (README.TXT found on the new image); A:
;       again, the units as they were: SELA4 aa xx, LOGV2 aa xx (03 00)
;   U   the user 5: USER SET
;   V   USER uu

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

DRV_M   = 12

        .zeropage
CNT:    .res    1                       ; make40's records to go

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

        .rodata
hrecs:  .word   0, 40, 200, 300, 600

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36
fcb2:   .res    36
fcb3:   .res    36

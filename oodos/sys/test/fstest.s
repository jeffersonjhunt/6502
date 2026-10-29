; FSTEST: the BDOS's files on drive D, the command tail's first letter (A:
; if none), a line a result for test_oodos_sys.cpp (hw/oodos.md, "The BDOS,
; phase 3"). "aa xx" is A and X as the BDOS returned them.
;
;   FREE0 xxxxxx          function 46: free records before
;   MAKE aa xx            D:SEQ.DAT
;   WRITE nnnn aa         300 records by function 21: those written, the
;                         last result. Then WRITTEN? waits for a key, so the
;                         test can see the last sector isn't on the disk yet
;   CLOSE aa xx           and CLOSED? waits again: now it is
;   FREE1 xxxxxx
;   OPEN aa xx RC rr      extent 0's RC
;   READ nnnn aa          records read by function 20 and checked to the end
;                         (between rdbegin and rdend, for the test to count
;                         the BIOS's READs), the last result: 01
;   RANDREC xxxxxx        function 36 there: 300
;   SIZE xxxxxx           function 35
;   RREAD r aa Y|N        function 33 of record r: the result, and the data
;                         right (Y): 200, 299, then 300 (01: never written),
;                         5000 (04: no such extent), $40000 (06)
;   RWRITE 1000 aa        function 34, past the end: sparse
;   RREAD 600 aa          04: an extent between that has no entry
;   RREAD 900 aa          01: the extent is there, the block not
;   RREAD 1001 aa Y|N     01: past the file's end, 1000
;   RWRITEZ 1003 aa       function 40
;   RREAD 1002 aa Y|N     00 and zeros: in 1000's block, which was zeroed
;   CLOSE2 aa xx
;   SIZE2 xxxxxx          1004
;   DELETE aa xx
;   OPEN2 aa xx           FF: gone
;   FREE2 xxxxxx          as FREE0
;   FSTEST DONE

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

RECORDS = 300

        .code
start:  lda     #0                      ; the drive: the tail's first letter
        ldx     BP_DMA
        beq     @drive
        lda     BP_DMA+2                ; after its space
        sec
        sbc     #'A'
@drive: sta     TDRV
        CALLF   F_DMAOFF, BUF
        jsr     free
        SAY     "FREE0 "
        jsr     showfree
        NAME    fcb, "SEQ     DAT"
        TRY     "MAKE ", F_MAKE, fcb

        stz     RN                      ; 300 records, each its pattern
        stz     RN+1
@write: jsr     FILL
        CALLF   F_WRITE, fcb
        sta     RA
        cmp     #0
        bne     @wrote
        inc     RN
        bne     @more
        inc     RN+1
@more:  lda     RN
        cmp     #<RECORDS
        bne     @write
        lda     RN+1
        cmp     #>RECORDS
        bne     @write
@wrote: SAY     "WRITE "
        lda     RN
        ldx     RN+1
        jsr     HEX4
        jsr     SPACE
        lda     RA
        jsr     HEX2
        jsr     CRLF
        KEYWAIT "WRITTEN?"
        TRY     "CLOSE ", F_CLOSE, fcb
        KEYWAIT "CLOSED?"
        jsr     free
        SAY     "FREE1 "
        jsr     showfree

        NAME    fcb, "SEQ     DAT"
        TRY     "OPEN ", F_OPEN, fcb
        SAY     "RC "
        lda     fcb+15
        jsr     HEX2
        jsr     CRLF
        stz     RN
        stz     RN+1
rdbegin:
@read:  CALLF   F_READ, fcb
        sta     RA
        cmp     #0
        bne     @end
        jsr     CHECK
        bcs     @end
        inc     RN
        bne     @read
        inc     RN+1
        bra     @read
@end:
rdend:  SAY     "READ "
        lda     RN
        ldx     RN+1
        jsr     HEX4
        jsr     SPACE
        lda     RA
        jsr     HEX2
        jsr     CRLF
        CALLF   F_RANDREC, fcb
        SAY     "RANDREC "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF
        CALLF   F_SIZE, fcb
        SAY     "SIZE "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF

        lda     #<200
        ldx     #>200
        jsr     rread
        lda     #<299
        ldx     #>299
        jsr     rread
        lda     #<300
        ldx     #>300
        jsr     rread
        lda     #<5000
        ldx     #>5000
        jsr     rread
        stz     fcb+33                  ; $40000: past 32 MB
        stz     fcb+34
        lda     #4
        sta     fcb+35
        SAY     "RREAD 40000 "
        CALLF   F_READRAND, fcb
        jsr     HEX2
        jsr     CRLF

        lda     #<1000                  ; sparse
        sta     RN
        lda     #>1000
        sta     RN+1
        jsr     FILL
        jsr     setr
        SAY     "RWRITE 1000 "
        CALLF   F_WRITERAND, fcb
        jsr     HEX2
        jsr     CRLF
        lda     #<600
        ldx     #>600
        jsr     rread
        lda     #<900
        ldx     #>900
        jsr     rread
        lda     #<1000
        ldx     #>1000
        jsr     rread
        lda     #<1001
        ldx     #>1001
        jsr     zread
        lda     #<1003
        sta     RN
        lda     #>1003
        sta     RN+1
        jsr     FILL
        jsr     setr
        SAY     "RWRITEZ 1003 "
        CALLF   F_WRITEZF, fcb
        jsr     HEX2
        jsr     CRLF
        lda     #<1002
        ldx     #>1002
        jsr     zread
        TRY     "CLOSE2 ", F_CLOSE, fcb
        CALLF   F_SIZE, fcb
        SAY     "SIZE2 "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF
        TRY     "DELETE ", F_DELETE, fcb
        NAME    fcb, "SEQ     DAT"
        TRY     "OPEN2 ", F_OPEN, fcb
        jsr     free
        SAY     "FREE2 "
        jsr     showfree
        SAY     "FSTEST DONE"
        jsr     CRLF
        rts

; Function 33 of record A/X: "RREAD r aa Y|N", the data checked.
rread:  sta     RN
        stx     RN+1
        jsr     setr
        SAY     "RREAD "
        lda     RN
        ldx     RN+1
        jsr     HEX4
        jsr     SPACE
        CALLF   F_READRAND, fcb
        pha
        jsr     HEX2
        jsr     SPACE
        pla
        sec
        bne     @say
        jsr     CHECK
@say:   jmp     YESNO

; The same, its data checked for zeros.
zread:  sta     RN
        stx     RN+1
        jsr     setr
        SAY     "RREAD "
        lda     RN
        ldx     RN+1
        jsr     HEX4
        jsr     SPACE
        CALLF   F_READRAND, fcb
        pha
        jsr     HEX2
        jsr     SPACE
        pla
        sec
        bne     @say
        jsr     ZEROS
@say:   jmp     YESNO

; R0-R2 = RN.
setr:   lda     RN
        sta     fcb+33
        lda     RN+1
        sta     fcb+34
        stz     fcb+35
        rts

; Function 46 of the drive into freerec (from the DMA buffer).
free:   lda     TDRV
        cmp     #$FF
        bne     @drive
        lda     #0
@drive: ldy     #DRV_SPACE
        jsr     BDOS
        ldx     #2
@copy:  lda     BUF,x
        sta     freerec,x
        dex
        bpl     @copy
        rts
showfree:
        lda     #<(freerec)
        ldx     #>(freerec)
        jsr     HEX6
        jmp     CRLF

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36
freerec: .res   3

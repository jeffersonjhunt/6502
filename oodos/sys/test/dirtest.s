; DIRTEST: the BDOS's directory functions on M: (hw/oodos.md, "The BDOS,
; phase 3"), in the error mode that returns errors ($FF), a line a result
; for test_oodos_sys.cpp. "aa xx" is A and X as the BDOS returned them.
;
;   SEARCH A1 A2          17 and 18 of A?.TXT, extent 0: the names found
;   COUNT nn              17 and 18 of ????????.??? and '?' extents: entries
;   ALL nnnn LABEL nn SFCB nn FREE nnnn  17 and 18 with '?' as the drive:
;                         every entry, each kind counted
;   USER3 nn              user 3's entries; its own file made there
;   OPEN3 aa xx           A1.TXT from user 3: FF, not a SYS file
;   SYSOPEN aa xx         once A1 is SYS (t2'): opened,
;   F8 hh                 f8' set
;   SYSREAD aa xx / SYSWRITE aa xx   read it; write it: FF 03, File R/O
;   SEARCH3 aa xx         17 doesn't look in user 0: FF
;   DELRO / RENRO / WRITERO aa xx    A2 read-only (t1'): FF 03 each
;   BYTECOUNT hh          f6': 30 sets A1's byte count, Open gives it in CR
;   DELX aa xx / XKEPT aa xx  f5': Delete of XFCBs only, which A1 hasn't
;   BITS hh hh            B1 given f1' and t3': its entry's bytes 1 and 11
;   OPENBITS hh hh        and the FCB's after Open
;   REN aa xx             B1 to C1; SEARCHB1 aa xx: FF; SEARCHC1 aa xx
;   RENEX / RENWILD / MAKEEX / MAKEWILD / OPENWILD aa xx   FF 08, 09
;   HASHOPEN / HASHFIND aa xx   HASHBA.TXT, whose hash is HASHAG.TXT's: FF
;   HASHAG aa xx          HASHAG.TXT itself
;   BIGREN aa xx          a 300-record file (two entries) renamed
;   BIGCOUNT nn OLDCOUNT nn  its entries by the new name and the old
;   BIGDEL aa xx BIGGONE nn FREEBACK Y|N   deleted, every entry, its blocks
;   DIRFULL nnnn aa       files made until the directory is full
;   WILDDEL aa xx AFTERDEL nnnn   deleted by F???????.TXT; then one made
;   DISKFULL nnnn aa      records written until the disk is full: 02
;   RANDFULL aa           a random write then: 02
;   FREEFULL xxxxxx       0
;   FREE98 xxxxxx xxxxxx  free with 40 records written, not closed; and
;                         after function 98
;   TRUNC aa xx SIZE xxxxxx R99 aa R100 aa TRUNCFREE Y|N
;   TRUNCBIG / TRUNCWILD aa xx   past the end: FF 00; '?': FF 09
;   ROMAKE aa xx          M: read-only by 28: FF 02
;   ROVEC aa xx / ROWRITE aa xx  A/X the vector; a write: FF 02
;   ROVEC2 aa xx          after 37 resets M:
;   LOGIN aa xx DRIVE aa xx ALLOC aa xx VERSION aa xx LOCK aa xx UNLOCK aa xx
;   DPB <17 bytes>        from function 31's address
;   SIZEGONE aa xx / SNEXTEND aa xx / FREEBAD aa xx / SELBAD aa xx
;   RESET aa xx / RDRIVE aa xx / RLOGIN aa xx / RROVEC aa xx  after 13
;   RDMA xxxxxx xxxxxx    46 after 13 writes at $0280: as it does at BUF
;   FLUSH aa xx / PURGE aa xx
;   DIRTEST DONE

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

DRV_M   = 12

.macro NEWNAME str
        .local s
        .pushseg
        .rodata
s:      .byte   str
        .popseg
        lda     #<s
        ldx     #>s
        jsr     setnew
.endmacro

        .zeropage
CNT:    .res    2
KIND:   .res    4                       ; DRIVEQ's counts: labels, SFCBs, free (2)

        .code
start:  lda     #$FF                    ; errors come back
        ldy     #F_ERRMODE
        jsr     BDOS
        CALLF   F_DMAOFF, BUF
        lda     #DRV_M                  ; M: current, and the FCBs' drive
        ldy     #DRV_SET
        jsr     BDOS
        lda     #DRV_M
        sta     TDRV
        jsr     free
        jsr     keepfree

        NAME    fcb, "A1      TXT"
        jsr     mk1
        NAME    fcb, "A2      TXT"
        jsr     mk1
        NAME    fcb, "B1      TXT"
        jsr     mk1

        ; searches
        SAY     "SEARCH"
        NAME    sfcb, "A?      TXT"
        CALLF   F_SFIRST, sfcb
@names: cmp     #$FF
        beq     @named
        jsr     showname
        CALLF   F_SNEXT, sfcb
        bra     @names
@named: jsr     CRLF
        NAME    sfcb, "???????????"
        lda     #'?'
        sta     sfcb+12
        SAY     "COUNT "
        jsr     count
        jsr     HEX2
        jsr     CRLF
        lda     #'?' - 1                ; '?' as the drive: every entry
        sta     TDRV
        NAME    sfcb, "XXXXXXXXXXX"
        lda     #DRV_M
        sta     TDRV
        stz     CNT
        stz     CNT+1
        stz     KIND
        stz     KIND+1
        stz     KIND+2
        stz     KIND+3
        CALLF   F_SFIRST, sfcb
@all:   cmp     #$FF
        beq     @counted
        jsr     entryat                 ; PTR at the entry in BUF
        inc     CNT
        bne     @kind
        inc     CNT+1
@kind:  lda     (PTR)
        cmp     #$20
        bne     @sfcb
        inc     KIND
@sfcb:  cmp     #$21
        bne     @free
        inc     KIND+1
@free:  cmp     #$E5
        bne     @nextall
        inc     KIND+2
        bne     @nextall
        inc     KIND+3
@nextall:
        CALLF   F_SNEXT, sfcb
        bra     @all
@counted:
        SAY     "ALL "
        lda     CNT
        ldx     CNT+1
        jsr     HEX4
        SAY     " LABEL "
        lda     KIND
        jsr     HEX2
        SAY     " SFCB "
        lda     KIND+1
        jsr     HEX2
        SAY     " FREE "
        lda     KIND+2
        ldx     KIND+3
        jsr     HEX4
        jsr     CRLF

        ; user numbers, and a SYS file from user 0
        lda     #3
        ldy     #F_USERNUM
        jsr     BDOS
        NAME    fcb, "X3      TXT"
        jsr     mk1
        SAY     "USER3 "
        NAME    sfcb, "???????????"
        lda     #'?'
        sta     sfcb+12
        jsr     count
        jsr     HEX2
        jsr     CRLF
        NAME    fcb, "A1      TXT"
        TRY     "OPEN3 ", F_OPEN, fcb
        jsr     user0
        NAME    fcb, "A1      TXT"
        lda     fcb+10
        ora     #$80                    ; t2': SYS
        sta     fcb+10
        CALLF   F_ATTRIB, fcb
        lda     #3
        ldy     #F_USERNUM
        jsr     BDOS
        NAME    fcb, "A1      TXT"
        SAY     "SYSOPEN "
        CALLF   F_OPEN, fcb
        jsr     RES
        SAY     "F8 "
        lda     fcb+8
        and     #$80
        jsr     HEX2
        jsr     CRLF
        TRY     "SYSREAD ", F_READ, fcb
        TRY     "SYSWRITE ", F_WRITE, fcb
        NAME    sfcb, "A1      TXT"
        TRY     "SEARCH3 ", F_SFIRST, sfcb
        jsr     user0

        ; read-only files
        NAME    fcb, "A2      TXT"
        lda     fcb+9
        ora     #$80                    ; t1'
        sta     fcb+9
        CALLF   F_ATTRIB, fcb
        NAME    fcb, "A2      TXT"
        TRY     "DELRO ", F_DELETE, fcb
        NAME    fcb, "A2      TXT"
        lda     #'Z'
        sta     fcb+17
        TRY     "RENRO ", F_RENAME, fcb
        NAME    fcb, "A2      TXT"
        CALLF   F_OPEN, fcb
        TRY     "WRITERO ", F_WRITE, fcb

        ; attributes
        NAME    fcb, "B1      TXT"
        lda     fcb+1
        ora     #$80                    ; f1'
        sta     fcb+1
        lda     fcb+11
        ora     #$80                    ; t3'
        sta     fcb+11
        CALLF   F_ATTRIB, fcb
        NAME    sfcb, "B1      TXT"
        CALLF   F_SFIRST, sfcb
        jsr     entryat
        SAY     "BITS "
        ldy     #1
        lda     (PTR),y
        jsr     HEX2
        jsr     SPACE
        ldy     #11
        lda     (PTR),y
        jsr     HEX2
        jsr     CRLF
        NAME    fcb, "B1      TXT"
        CALLF   F_OPEN, fcb
        SAY     "OPENBITS "
        lda     fcb+1
        jsr     HEX2
        jsr     SPACE
        lda     fcb+11
        jsr     HEX2
        jsr     CRLF

        ; f6': the last record's byte count, set by 30, given by Open
        NAME    fcb, "A1      TXT"
        lda     fcb+6
        ora     #$80
        sta     fcb+6
        lda     #$55
        sta     fcb+32
        CALLF   F_ATTRIB, fcb
        NAME    fcb, "A1      TXT"
        lda     fcb+6
        ora     #$80
        sta     fcb+6
        CALLF   F_OPEN, fcb
        SAY     "BYTECOUNT "
        lda     fcb+32
        jsr     HEX2
        jsr     CRLF
        ; f5': XFCBs only, of which A1 has none: the file stays
        NAME    fcb, "A1      TXT"
        lda     fcb+5
        ora     #$80
        sta     fcb+5
        TRY     "DELX ", F_DELETE, fcb
        NAME    sfcb, "A1      TXT"
        TRY     "XKEPT ", F_SFIRST, sfcb

        ; rename, and what may not be made or renamed
        NAME    fcb, "B1      TXT"
        NEWNAME "C1      TXT"
        TRY     "REN ", F_RENAME, fcb
        NAME    sfcb, "B1      TXT"
        TRY     "SEARCHB1 ", F_SFIRST, sfcb
        NAME    sfcb, "C1      TXT"
        TRY     "SEARCHC1 ", F_SFIRST, sfcb
        NAME    fcb, "C1      TXT"
        NEWNAME "A1      TXT"
        TRY     "RENEX ", F_RENAME, fcb
        NAME    fcb, "C1      TXT"
        NEWNAME "C?      TXT"
        TRY     "RENWILD ", F_RENAME, fcb
        NAME    fcb, "A1      TXT"
        TRY     "MAKEEX ", F_MAKE, fcb
        NAME    fcb, "A?      TXT"
        TRY     "MAKEWILD ", F_MAKE, fcb
        NAME    fcb, "A?      TXT"
        TRY     "OPENWILD ", F_OPEN, fcb

        ; the hash: two names with one hash, told apart in full
        NAME    fcb, "HASHAG  TXT"
        jsr     mk1
        NAME    fcb, "HASHBA  TXT"
        TRY     "HASHOPEN ", F_OPEN, fcb
        NAME    sfcb, "HASHBA  TXT"
        TRY     "HASHFIND ", F_SFIRST, sfcb
        NAME    fcb, "HASHAG  TXT"
        TRY     "HASHAG ", F_OPEN, fcb

        ; a file of two entries renamed and deleted
        jsr     free
        jsr     keepfree
        NAME    fcb, "BIG     DAT"
        CALLF   F_MAKE, fcb
        lda     #<300
        ldx     #>300
        jsr     writen
        CALLF   F_CLOSE, fcb
        NAME    fcb, "BIG     DAT"
        NEWNAME "BIG2    DAT"
        TRY     "BIGREN ", F_RENAME, fcb
        NAME    sfcb, "BIG2    DAT"
        lda     #'?'
        sta     sfcb+12
        SAY     "BIGCOUNT "
        jsr     count
        jsr     HEX2
        NAME    sfcb, "BIG     DAT"
        lda     #'?'
        sta     sfcb+12
        SAY     " OLDCOUNT "
        jsr     count
        jsr     HEX2
        jsr     CRLF
        NAME    fcb, "BIG2    DAT"
        TRY     "BIGDEL ", F_DELETE, fcb
        NAME    sfcb, "BIG2    DAT"
        lda     #'?'
        sta     sfcb+12
        SAY     "BIGGONE "
        jsr     count
        jsr     HEX2
        SAY     " FREEBACK "
        jsr     free
        jsr     samefree
        jsr     YESNO

        ; the directory full, then emptied by a wildcard
        stz     CNT
        stz     CNT+1
dirloop: NAME       fcb, "F0000   TXT"
        lda     CNT                     ; F and the count in hex
        ldx     CNT+1
        jsr     hexname
        CALLF   F_MAKE, fcb
        sta     RA
        cmp     #$FF
        beq     dirfull
        inc     CNT
        bne     dirloop
        inc     CNT+1
        bra     dirloop
dirfull:
        SAY     "DIRFULL "
        lda     CNT
        ldx     CNT+1
        jsr     HEX4
        jsr     SPACE
        lda     RA
        jsr     HEX2
        jsr     CRLF
        NAME    fcb, "F???????TXT"
        TRY     "WILDDEL ", F_DELETE, fcb
        NAME    sfcb, "F???????TXT"
        lda     #'?'
        sta     sfcb+12
        SAY     "AFTERDEL "
        jsr     count
        jsr     HEX2
        jsr     CRLF

        ; the disk full
        jsr     free
        jsr     keepfree
        NAME    fcb, "FULL    DAT"
        CALLF   F_MAKE, fcb
        stz     RN
        stz     RN+1
@fill:  jsr     FILL
        CALLF   F_WRITE, fcb
        sta     RA
        cmp     #0
        bne     @full
        inc     RN
        bne     @fill
        inc     RN+1
        bra     @fill
@full:  SAY     "DISKFULL "
        lda     RN
        ldx     RN+1
        jsr     HEX4
        jsr     SPACE
        lda     RA
        jsr     HEX2
        jsr     CRLF
        lda     #<2000
        sta     fcb+33
        lda     #>2000
        sta     fcb+34
        stz     fcb+35
        SAY     "RANDFULL "
        CALLF   F_WRITERAND, fcb
        jsr     HEX2
        jsr     CRLF
        CALLF   F_CLOSE, fcb
        jsr     free
        SAY     "FREEFULL "
        jsr     showfree
        NAME    fcb, "FULL    DAT"
        CALLF   F_DELETE, fcb

        ; blocks of a file not closed, freed by 98
        NAME    fcb, "T       DAT"
        CALLF   F_MAKE, fcb
        lda     #40
        ldx     #0
        jsr     writen
        jsr     free
        SAY     "FREE98 "
        jsr     showfree0
        jsr     SPACE
        ldy     #DRV_FREE
        jsr     BDOS
        jsr     free
        jsr     showfree
        NAME    fcb, "T       DAT"
        CALLF   F_DELETE, fcb

        ; truncate
        jsr     free
        jsr     keepfree
        NAME    fcb, "TR      DAT"
        CALLF   F_MAKE, fcb
        lda     #<300
        ldx     #>300
        jsr     writen
        CALLF   F_CLOSE, fcb
        NAME    fcb, "TR      DAT"
        lda     #99
        sta     fcb+33
        TRY     "TRUNC ", F_TRUNCATE, fcb
        NAME    fcb, "TR      DAT"
        CALLF   F_SIZE, fcb
        SAY     "SIZE "
        lda     #<(fcb+33)
        ldx     #>(fcb+33)
        jsr     HEX6
        jsr     CRLF
        NAME    fcb, "TR      DAT"
        CALLF   F_OPEN, fcb
        lda     #99
        sta     fcb+33
        SAY     "R99 "
        CALLF   F_READRAND, fcb
        jsr     HEX2
        lda     #99
        sta     RN
        stz     RN+1
        jsr     CHECK
        lda     #'Y'
        bcc     @r99
        lda     #'N'
@r99:   jsr     PUTC
        lda     #100
        sta     fcb+33
        SAY     " R100 "
        CALLF   F_READRAND, fcb
        jsr     HEX2
        jsr     CRLF
        SAY     "TRUNCFREE "            ; 300 records were 19 blocks, 100 are 7
        jsr     free
        sec
        lda     keep
        sbc     #<(7*16)
        sta     keep
        lda     keep+1
        sbc     #>(7*16)
        sta     keep+1
        jsr     samefree
        jsr     YESNO
        NAME    fcb, "TR      DAT"
        lda     #<500
        sta     fcb+33
        lda     #>500
        sta     fcb+34
        TRY     "TRUNCBIG ", F_TRUNCATE, fcb
        NAME    fcb, "TR??    DAT"
        TRY     "TRUNCWILD ", F_TRUNCATE, fcb

        ; a read-only drive
        NAME    wfcb, "W       DAT"
        CALLF   F_MAKE, wfcb
        ldy     #DRV_SETRO
        jsr     BDOS
        NAME    fcb, "RO      DAT"
        TRY     "ROMAKE ", F_MAKE, fcb
        TRY     "ROVEC ", DRV_ROVEC, 0
        TRY     "ROWRITE ", F_WRITE, wfcb
        lda     #<(1 << DRV_M)
        ldx     #>(1 << DRV_M)
        ldy     #DRV_RESET
        jsr     BDOS
        TRY     "ROVEC2 ", DRV_ROVEC, 0
        NAME    fcb, "RO      DAT"
        TRY     "MAKEAGAIN ", F_MAKE, fcb

        ; the rest
        TRY     "LOGIN ", DRV_LOGINVEC, 0
        TRY     "DRIVE ", DRV_GET, 0
        TRY     "ALLOC ", DRV_ALLOCVEC, 0
        TRY     "VERSION ", S_BDOSVER, 0
        TRY     "LOCK ", F_LOCK, fcb
        TRY     "UNLOCK ", F_UNLOCK, fcb
        CALLF   DRV_DPB, 0
        sta     PTR
        stx     PTR+1
        SAY     "DPB "
        ldx     #17
        jsr     HEXN
        jsr     CRLF
        NAME    fcb, "NOSUCH  TXT"
        TRY     "SIZEGONE ", F_SIZE, fcb
        NAME    sfcb, "A1      TXT"
        CALLF   F_SFIRST, sfcb
        CALLF   F_SNEXT, sfcb
        TRY     "SNEXTEND ", F_SNEXT, sfcb
        SAY     "FREEBAD "
        lda     #5                      ; F:, not mounted
        ldy     #DRV_SPACE
        jsr     BDOS
        jsr     RES
        SAY     "SELBAD "
        lda     #15
        ldy     #DRV_SET
        jsr     BDOS
        jsr     RES
        TRY     "FLUSH ", DRV_FLUSH, 0
        TRY     "PURGE ", DRV_FLUSH, $FF
        ldy     #DRV_SETRO
        jsr     BDOS
        TRY     "RESET ", DRV_ALLRESET, 0
        TRY     "RDRIVE ", DRV_GET, 0
        TRY     "RLOGIN ", DRV_LOGINVEC, 0
        TRY     "RROVEC ", DRV_ROVEC, 0
        lda     #DRV_M                  ; 13 put the DMA address back at $0280
        ldy     #DRV_SPACE
        jsr     BDOS
        SAY     "RDMA "
        lda     #<BP_DMA
        ldx     #>BP_DMA
        jsr     HEX6
        jsr     SPACE
        CALLF   F_DMAOFF, BUF
        jsr     free                    ; into BUF
        jsr     showfree
        SAY     "DIRTEST DONE"
        jsr     CRLF
        rts

; Make the FCB's file, one record written, closed.
mk1:    CALLF   F_MAKE, fcb
        stz     RN
        stz     RN+1
        jsr     FILL
        CALLF   F_WRITE, fcb
        CALLF   F_CLOSE, fcb
        rts

; A/X records of the pattern written to fcb, from record 0.
writen: sta     CNT
        stx     CNT+1
        stz     RN
        stz     RN+1
@rec:   lda     RN
        cmp     CNT
        bne     @write
        lda     RN+1
        cmp     CNT+1
        beq     @done
@write: jsr     FILL
        CALLF   F_WRITE, fcb
        inc     RN
        bne     @rec
        inc     RN+1
        bra     @rec
@done:  rts

; fcb's new name (+16) from A/X's 11 bytes, its drive 0.
setnew: sta     PTR
        stx     PTR+1
        ldy     #0
@copy:  lda     (PTR),y
        sta     fcb+17,y
        iny
        cpy     #11
        bne     @copy
        stz     fcb+16
        rts

; fcb's name's bytes 2-5: A/X as four hex digits.
hexname:
        pha
        txa
        jsr     @two
        sta     fcb+2
        stx     fcb+3
        pla
        jsr     @two
        sta     fcb+4
        stx     fcb+5
        rts
@two:   pha                             ; A as two digits: A the high, X the low
        and     #$0F
        jsr     @digit
        tax
        pla
        lsr     a
        lsr     a
        lsr     a
        lsr     a
@digit: cmp     #10
        bcc     @num
        adc     #'A' - '0' - 10 - 1
@num:   adc     #'0'
        rts

; Back to user 0.
user0:  lda     #0
        ldy     #F_USERNUM
        jmp     BDOS

; A = the entries 17 and 18 find for sfcb.
count:  stz     CNT
        CALLF   F_SFIRST, sfcb
@next:  cmp     #$FF
        beq     @done
        inc     CNT
        CALLF   F_SNEXT, sfcb
        bra     @next
@done:  lda     CNT
        rts

; PTR at the entry in BUF a search's code A names.
entryat:
        asl     a
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

; " NAME" of the entry a search's code A names, its trailing spaces dropped.
showname:
        jsr     entryat
        jsr     SPACE
        ldy     #1
@char:  lda     (PTR),y
        and     #$7F
        cmp     #' '
        beq     @done
        phy
        jsr     PUTC
        ply
        iny
        cpy     #9
        bne     @char
@done:  rts

; Function 46 of M: into freerec.
free:   lda     #DRV_M
        ldy     #DRV_SPACE
        jsr     BDOS
        ldx     #2
@copy:  lda     BUF,x
        sta     freerec,x
        dex
        bpl     @copy
        rts
keepfree:
        lda     freerec
        sta     keep
        lda     freerec+1
        sta     keep+1
        rts
; Carry clear if freerec is keep.
samefree:
        lda     freerec
        cmp     keep
        bne     @no
        lda     freerec+1
        cmp     keep+1
        bne     @no
        clc
        rts
@no:    sec
        rts
showfree0:
        lda     #<freerec
        ldx     #>freerec
        jmp     HEX6
showfree:
        jsr     showfree0
        jmp     CRLF

        .include "print.inc"
        .include "fs.inc"

        .bss
fcb:    .res    36
sfcb:   .res    36
wfcb:   .res    36
freerec: .res   3
keep:   .res    2

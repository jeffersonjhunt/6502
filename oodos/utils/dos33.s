; DOS33.COM (hw/oodos.md, "The DOS 3.3 exchange utility (6d)"; the User's
; Guide's DOS33): files to and from a DOS 3.3 disk, through function 76.
; Ours, written from the disk's layout (dos33.py's header has it).
;
;   DOS33 d:                            the catalog: the lock, the type, the
;                                       sectors and the name; the free sectors
;   DOS33 GET d:NAME [e:]FILE.TYP [T|B|R]   a file from the Apple disk
;   DOS33 PUT [e:]FILE.TYP d:NAME [T|B[=addr]]  a file to it
;   DOS33 ERA d:NAME                    a file deleted
;
; d: is O:, P:, or an SD drive whose unit is a 140 KB image (76 says which).
; A name with blanks is in quotes ("MY FILE"); up to 30 characters.
;
;   T   text: GET clears bit 7, writes CR as CR LF, ends at the first 0 and
;       pads with ^Z; PUT sets bit 7, drops LF, ends at ^Z and puts a 0 after
;   B   binary: GET drops the address and length DOS keeps first and writes
;       the length's bytes (the address said); PUT puts them first, the
;       address $2000 unless B=addr (hex) gives it, the length the file's
;       records x 128 (511 at most)
;   R   raw (GET): every sector of the file as stored
;
; GET's default is by the file's type: T for T, B for B, R for the others.
; PUT's is T for a .TXT file, B for any other. GET replaces the CP/M file;
; PUT refuses a name already on the Apple disk, ERA a locked file. Each
; refusal is a message of ours and the return code $FF00.

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

CR      = $0D
LF      = $0A
CTLZ    = $1A
QUOTE   = '"'
VTRK    = 17                            ; the VTOC: track 17, sector 0
NPAIR   = 122                           ; track/sector pairs a list holds
NENT    = 7                             ; catalog entries a sector
ENTLEN  = 35

        OODOS_HEADER

        .zeropage
ptr:    .res    2                       ; an entry in cbuf
src:    .res    2

        .code
start:  jsr     UT_INIT
        stz     gtemp
        jsr     UT_SKIP
        bne     :+
        jmp     usage
:       lda     #<kget
        ldx     #>kget
        jsr     keyword
        bcs     :+
        jmp     get
:       lda     #<kput
        ldx     #>kput
        jsr     keyword
        bcs     :+
        jmp     put
:       lda     #<kera
        ldx     #>kera
        jsr     keyword
        bcs     catalog
        jmp     era

; ---------------------------------------------------------------------------
; DOS33 d:

catalog:
        jsr     drive
        bcs     usage1
        jsr     UT_SKIP
        bne     usage1
        jsr     rdvtoc
        jsr     CM_PAGE
        lda     adrv
        jsr     UT_PUTDRV
        lda     #<svol
        ldx     #>svol
        jsr     CM_PUTS
        lda     vtoc + 6
        ldx     #0
        jsr     CM_DECOUT
        jsr     CM_NL
        jsr     catfirst
@entry: jsr     catnext
        bcs     @end
        ldy     #0
        lda     (ptr),y
        beq     @entry                  ; never used
        cmp     #$FF
        beq     @entry                  ; deleted
        ldy     #2
        lda     (ptr),y
        pha
        ldx     #' '                    ; the lock
        asl     a
        bcc     :+
        ldx     #'*'
:       txa
        jsr     CM_PUTC
        pla
        jsr     typech
        jsr     CM_PUTC
        lda     #' '
        jsr     CM_PUTC
        ldy     #$21
        lda     (ptr),y
        tax
        iny
        lda     (ptr),y
        jsr     dec3
        lda     #' '
        jsr     CM_PUTC
        jsr     putname
        jsr     CM_BRK
        jsr     CM_NL
        bra     @entry
@end:   jsr     nfree
        jsr     CM_DECOUT
        lda     #<sfreeq
        ldx     #>sfreeq
        jmp     sayend

usage1: jmp     usage

; ---------------------------------------------------------------------------
; DOS33 GET d:NAME [e:]FILE.TYP [T|B|R]

get:    jsr     drive
        bcs     usage1
        jsr     name
        jsr     cpmspec
        jsr     mode
        cmp     #'='
        beq     usage1
        jsr     samedrv
        jsr     rdvtoc
        jsr     find
        bcc     :+
        jmp     notfound
:       ldy     #2                      ; the default by its type
        lda     (ptr),y
        and     #$7F
        ldx     gmode
        bne     @mode
        ldx     #'T'
        cmp     #0
        beq     @mode
        ldx     #'B'
        cmp     #$04
        beq     @mode
        cmp     #$40
        beq     @mode
        ldx     #'R'
@mode:  cpx     #'T'
        beq     :+
        cpx     #'B'
        beq     :+
        cpx     #'R'
        beq     :+
        jmp     usage
:       stx     gmode
        jsr     chkfile                 ; its lists and pairs, before anything is written
        ldx     #35                     ; a $$$ file of its name, made anew: the old one
:       lda     fcb,x                   ; is replaced only when this one is whole
        sta     tfcb,x
        dex
        bpl     :-
        ldx     #2
        lda     #'$'
:       sta     tfcb + 9,x
        dex
        bpl     :-
        stz     tfcb + 12
        stz     tfcb + 32
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_MAKE
        jsr     BDOS
        cmp     #$FF
        bne     :+
        jmp     cpmfail
:       inc     gtemp
        lda     flt
        sta     lt
        lda     fls
        sta     ls
        lda     #<rec
        ldx     #>rec
        ldy     #F_DMAOFF
        jsr     BDOS
        stz     ri
        stz     zpend
        stz     zpend + 1
        stz     first
        stz     done
@list:  lda     lt                      ; each list in turn
        beq     @end
        lda     #<lbuf
        ldx     #>lbuf
        jsr     rdsec_tl
        stz     pi
@pair:  lda     pi
        asl     a
        tax
        lda     lbuf + $0C,x            ; track 0: a sector never written
        bne     @data
        inc     zpend                   ; a sector never written: zeros, if more follow
        bne     @npair
        inc     zpend + 1
        bra     @npair
@data:  lda     lbuf + $0C,x
        sta     strk
        lda     lbuf + $0D,x
        sta     ssec
        lda     #<dbuf
        ldx     #>dbuf
        jsr     rdsec
@zeros: lda     zpend                   ; the zeros owed first
        ora     zpend + 1
        beq     @this
        ldx     #0
:       lda     #0
        phx
        jsr     gbyte
        plx
        inx
        bne     :-
        lda     zpend
        bne     :+
        dec     zpend + 1
:       dec     zpend
        bra     @zeros
@this:  lda     done
        bne     @end
        jsr     gsector
        lda     done
        bne     @end
@npair: inc     pi
        lda     pi
        cmp     #NPAIR
        bne     @pair
        lda     lbuf + 1
        sta     lt
        lda     lbuf + 2
        sta     ls
        bra     @list
@end:   lda     #0                      ; the last record: ^Z after text, 0 after the rest
        ldx     gmode
        cpx     #'T'
        bne     :+
        lda     #CTLZ
:       ldx     ri
        beq     @close
:       sta     rec,x
        inx
        bpl     :-
        jsr     wrec
@close: lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_CLOSE
        jsr     BDOS
        cmp     #$FF
        bne     :+
        jmp     cpmfail
:       lda     #<fcb                   ; whole: the old file deleted, this one renamed
        ldx     #>fcb
        ldy     #F_DELETE
        jsr     BDOS
        ldx     #11                     ; tfcb + 16: the new name
:       lda     fcb,x
        sta     tfcb + 16,x
        dex
        bpl     :-
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_RENAME
        jsr     BDOS
        stz     gtemp
        cmp     #$FF
        bne     :+
        jmp     cpmfail
:       lda     gmode
        cmp     #'B'
        bne     @quiet
        lda     #<saddr                 ; B: where DOS would load it
        ldx     #>saddr
        jsr     CM_PUTS
        lda     #'$'
        jsr     CM_PUTC
        lda     baddr + 1
        jsr     hex2
        lda     baddr
        jsr     hex2
        lda     #<scomma
        ldx     #>scomma
        jsr     CM_PUTS
        lda     blen
        ldx     blen + 1
        jsr     CM_DECOUT
        lda     #<sbytes
        ldx     #>sbytes
        jmp     sayend
@quiet: jmp     UT_EXIT

; dbuf, a data sector, to the CP/M file by gmode.
gsector:
        ldx     #0
        lda     gmode
        cmp     #'B'
        bne     @each
        lda     first
        bne     @each
        inc     first                   ; B's first: the address and length
        lda     dbuf
        sta     baddr
        lda     dbuf + 1
        sta     baddr + 1
        lda     dbuf + 2
        sta     blen
        sta     bleft
        lda     dbuf + 3
        sta     blen + 1
        sta     bleft + 1
        ldx     #4
@each:  lda     dbuf,x
        phx
        jsr     gbyte
        plx
        lda     done
        bne     @out
        inx
        bne     @each
@out:   rts

; A, a byte of the Apple file, to the CP/M file as gmode has it.
gbyte:  ldy     gmode
        cpy     #'T'
        bne     @bin
        cmp     #0
        bne     :+
        inc     done                    ; text ends at its first 0 (as stored: $80 isn't one)
        rts
:       and     #$7F
        cmp     #CR
        bne     putrec
        jsr     putrec
        lda     #LF
        bra     putrec
@bin:   cpy     #'B'
        bne     putrec
        ldy     bleft                   ; B: the length's bytes only
        bne     :+
        ldy     bleft + 1
        bne     :+
        inc     done
        rts
:       ldy     bleft
        bne     :+
        dec     bleft + 1
:       dec     bleft
; A to the CP/M record; a full one written.
putrec: ldx     ri
        sta     rec,x
        inx
        stx     ri
        bpl     recok
wrec:   lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_WRITE
        jsr     BDOS
        cmp     #0
        beq     :+
        jmp     cpmfull
:       stz     ri
recok:  rts

; ---------------------------------------------------------------------------
; DOS33 PUT [e:]FILE.TYP d:NAME [T|B[=addr]]

put:    jsr     cpmspec
        jsr     drive
        bcc     :+
        jmp     usage
:       jsr     name
        jsr     samedrv
        lda     #<$2000
        sta     baddr
        lda     #>$2000
        sta     baddr + 1
        jsr     mode
        cmp     #'='
        bne     @mode
        lda     gmode
        cmp     #'B'
        beq     :+
        jmp     usage
:       jsr     hexin
        bcc     @mode
        jmp     usage
@mode:  ldx     gmode
        bne     :+
        ldx     #'B'                    ; by the CP/M type: T for .TXT
        lda     fcb + 9
        and     #$7F
        cmp     #'T'
        bne     :+
        lda     fcb + 10
        and     #$7F
        cmp     #'X'
        bne     :+
        lda     fcb + 11
        and     #$7F
        cmp     #'T'
        bne     :+
        ldx     #'T'
:       cpx     #'T'
        beq     :+
        cpx     #'B'
        beq     :+
        jmp     usage
:       stx     gmode
        jsr     rdvtoc
        jsr     find
        bcs     :+
        lda     #<sexists
        ldx     #>sexists
        jmp     failsay
:       jsr     freeent                 ; an entry for it: ct, cs, ci
        bcc     :+
        lda     #<scatfull
        ldx     #>scatfull
        jmp     failsay
:       lda     #<fcb                   ; the CP/M file
        ldx     #>fcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     :+
        jmp     cpmnone
:       lda     #<fcb                   ; its records
        ldx     #>fcb
        ldy     #F_SIZE
        jsr     BDOS
        lda     #<rec
        ldx     #>rec
        ldy     #F_DMAOFF
        jsr     BDOS
        stz     di
        stz     lastcr
        stz     ndata
        stz     ndata + 1
        stz     nlist
        jsr     newlist                 ; the first list
        lda     lt
        sta     flt
        lda     ls
        sta     fls
        lda     gmode
        cmp     #'B'
        bne     @read
        lda     fcb + 35                ; B: the address and the length first
        bne     @big                    ; (35's count at R0-R2, 33-35)
        lda     fcb + 34
        cmp     #2
        bcs     @big
        lsr     a                       ; records x 128
        lda     fcb + 33
        ror     a
        sta     blen + 1
        lda     #0
        ror     a
        sta     blen
        lda     baddr
        jsr     pbyte
        lda     baddr + 1
        jsr     pbyte
        lda     blen
        jsr     pbyte
        lda     blen + 1
        jsr     pbyte
        bra     @read
@big:   lda     #<sbig
        ldx     #>sbig
        jmp     failsay
@read:  lda     #<fcb
        ldx     #>fcb
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        beq     :+
        cmp     #1                      ; the end; any other a read that failed
        beq     @eof
        lda     #<scpmrd
        ldx     #>scpmrd
        jmp     failsay
:       ldx     #0
@byte:  lda     rec,x
        ldy     gmode
        cpy     #'T'
        bne     @put
        cmp     #CTLZ
        beq     @eof
        cmp     #0                      ; NULs dropped
        beq     @next
        cmp     #LF                     ; CR LF a CR; a lone LF a CR too
        bne     @char
        ldy     lastcr
        stz     lastcr
        cpy     #0
        bne     @next
        lda     #CR
@char:  ldy     #0
        cmp     #CR
        bne     :+
        iny
:       sty     lastcr
        ora     #$80
@put:   phx
        jsr     pbyte
        plx
@next:  inx
        bpl     @byte
        bra     @read
@eof:   lda     gmode                   ; text ends with a 0
        cmp     #'T'
        bne     :+
        lda     #0
        jsr     pbyte
:       lda     di                      ; the last sector, its rest 0
        beq     :++
        tax
        lda     #0
:       sta     dbuf,x
        inx
        bne     :-
        jsr     flushd
:       lda     lt                      ; the last list
        sta     strk
        lda     ls
        sta     ssec
        lda     #<lbuf
        ldx     #>lbuf
        jsr     wrsec
        jsr     wrvtoc                  ; the sectors taken first: a failure after leaks
        lda     ct                      ; them, never leaves a file on free ones
        sta     strk
        lda     cs
        sta     ssec
        lda     #<cbuf
        ldx     #>cbuf
        jsr     rdsec
        jsr     entptr
        ldy     #0
        lda     flt
        sta     (ptr),y
        iny
        lda     fls
        sta     (ptr),y
        iny
        lda     #$00                    ; T
        ldx     gmode
        cpx     #'T'
        beq     :+
        lda     #$04                    ; B
:       sta     (ptr),y
        ldx     #0
:       iny
        lda     aname,x
        ora     #$80
        sta     (ptr),y
        inx
        cpx     #30
        bne     :-
        iny                             ; the sectors: its data and its lists
        lda     ndata
        clc
        adc     nlist
        sta     (ptr),y
        iny
        lda     ndata + 1
        adc     #0
        sta     (ptr),y
        lda     #<cbuf
        ldx     #>cbuf
        jsr     wrsec
        jmp     UT_EXIT

; A, a byte of the Apple file, into dbuf; a full sector written.
pbyte:  ldx     di
        sta     dbuf,x
        inx
        stx     di
        beq     flushd
        rts
flushd: jsr     alloc                   ; its sector
        lda     pi                      ; its pair in the list; a full list chained first
        cmp     #NPAIR
        bne     :+
        jsr     newlist
:       lda     pi
        asl     a
        tax
        lda     strk
        sta     lbuf + $0C,x
        lda     ssec
        sta     lbuf + $0D,x
        inc     pi
        inc     ndata
        bne     :+
        inc     ndata + 1
:       lda     #<dbuf
        ldx     #>dbuf
        jsr     wrsec
        stz     di
        rts

; A new track/sector list: its sector allocated, the full one (if any)
; chained to it and written; lbuf empty but for the file's sector it starts
; at (ndata); pi 0. strk/ssec kept.
newlist:
        lda     strk
        pha
        lda     ssec
        pha
        jsr     alloc
        lda     nlist
        beq     @first
        lda     strk                    ; the full one: its next is this
        sta     lbuf + 1
        lda     ssec
        sta     lbuf + 2
        lda     lt
        sta     strk
        lda     ls
        sta     ssec
        lda     #<lbuf
        ldx     #>lbuf
        jsr     wrsec
        lda     lbuf + 1
        sta     strk
        lda     lbuf + 2
        sta     ssec
@first: lda     strk
        sta     lt
        lda     ssec
        sta     ls
        inc     nlist
        ldx     #0
        txa
:       sta     lbuf,x
        inx
        bne     :-
        lda     ndata
        sta     lbuf + 5
        lda     ndata + 1
        sta     lbuf + 6
        stz     pi
        pla
        sta     ssec
        pla
        sta     strk
        rts

; A free sector, marked used in the VTOC: strk/ssec. Outward from track
; 17, 18 to 34 then 16 to 0, each from sector 15 down. Disk Full ends it
; (nothing of the file is in the catalog or the VTOC yet).
alloc:  lda     #18
        sta     strk
@track: lda     #15
        sta     ssec
@sect:  jsr     bitat
        and     vtoc,x
        bne     @got
        dec     ssec
        bpl     @sect
        lda     strk
        cmp     #17
        bcc     @down
        inc     strk
        lda     strk
        cmp     #35
        bne     @track
        lda     #16
        sta     strk
        bra     @track
@down:  dec     strk
        bpl     @track
        lda     #<sfull
        ldx     #>sfull
        jmp     failsay
@got:   eor     #$FF
        and     vtoc,x
        sta     vtoc,x
        lda     strk                    ; the last track allocated and the way, as DOS keeps them
        sta     vtoc + $30
        lda     #1
        ldx     strk
        cpx     #VTRK
        bcs     :+
        lda     #$FF
:       sta     vtoc + $31
        rts

; X the VTOC's bitmap byte for strk/ssec, A the sector's bit in it.
bitat:  lda     strk
        asl     a
        asl     a
        clc
        adc     #$38
        tax
        lda     ssec
        cmp     #8
        bcs     :+
        inx                             ; sectors 7-0: the second byte
:       and     #7
        tay
        lda     #1
:       dey
        bmi     :+
        asl     a
        bra     :-
:       rts

; ---------------------------------------------------------------------------
; DOS33 ERA d:NAME

era:    jsr     drive
        bcc     :+
        jmp     usage
:       jsr     name
        jsr     UT_SKIP
        beq     :+
        jmp     usage
:       jsr     rdvtoc
        jsr     find
        bcc     :+
        jmp     notfound
:       ldy     #2
        lda     (ptr),y
        bpl     :+
        lda     #<slocked
        ldx     #>slocked
        jmp     failsay
:       jsr     chkfile                 ; its lists and pairs, before anything is freed
        ldy     #0                      ; the entry deleted: its track kept at +$20
        lda     (ptr),y
        sta     lt
        ldy     #$20
        sta     (ptr),y
        ldy     #1
        lda     (ptr),y
        sta     ls
        lda     #$FF
        sta     (ptr)
        lda     ct                      ; the catalog's sector kept: cbuf is read again below
        pha
        lda     cs
        pha
        lda     #<cbuf
        ldx     #>cbuf
        jsr     copyc
@list:  lda     lt                      ; every list and data sector freed
        beq     @done
        sta     strk
        lda     ls
        sta     ssec
        jsr     sfree
        lda     #<lbuf
        ldx     #>lbuf
        jsr     rdsec_tl
        ldx     #0
@pair:  lda     lbuf + $0D,x
        sta     ssec
        lda     lbuf + $0C,x
        sta     strk
        beq     :+                      ; track 0: none
        phx
        jsr     sfree
        plx
:       inx
        inx
        cpx     #NPAIR * 2
        bne     @pair
        lda     lbuf + 1
        sta     lt
        lda     lbuf + 2
        sta     ls
        bra     @list
@done:  pla
        sta     ssec
        pla
        sta     strk
        lda     #<ebuf
        ldx     #>ebuf
        jsr     wrsec
        jsr     wrvtoc
        jmp     UT_EXIT

; The file at ptr checked before anything is written: at most 5 lists (a
; whole disk's 560 sectors), each list and each data sector on tracks 1-34
; but 17, sectors 0-15 (a pair of track 0 is none). Else Bad file, the end.
; flt/fls its first list.
chkfile:
        ldy     #0
        lda     (ptr),y
        sta     flt
        sta     lt
        iny
        lda     (ptr),y
        sta     fls
        sta     ls
        stz     nchk
@list:  lda     lt
        beq     @ok
        inc     nchk
        lda     nchk
        cmp     #6
        bcs     @bad
        ldy     ls
        lda     lt
        jsr     okts
        bcs     @bad
        lda     #<lbuf
        ldx     #>lbuf
        jsr     rdsec_tl
        ldx     #0
@pair:  lda     lbuf + $0C,x
        beq     @next
        ldy     lbuf + $0D,x
        jsr     okts
        bcs     @bad
@next:  inx
        inx
        cpx     #NPAIR * 2
        bne     @pair
        lda     lbuf + 1
        sta     lt
        lda     lbuf + 2
        sta     ls
        bra     @list
@ok:    rts
@bad:   lda     #<sbadf
        ldx     #>sbadf
        jmp     failsay
; Carry clear if track A, sector Y can hold a file's sector (1-34 but 17, 0-15).
okts:   cmp     #35
        bcs     @no
        cmp     #VTRK
        beq     @no
        cmp     #0
        beq     @no
        cpy     #16
        bcs     @no
        clc
        rts
@no:    sec
        rts

; The CP/M file's drive (fcb's, or the current one) mustn't be the Apple disk.
samedrv:
        lda     fcb
        beq     :+
        dec     a
        bra     :++
:       lda     UT_DRV0
:       cmp     adrv
        bne     :+
        lda     #<ssame
        ldx     #>ssame
        jmp     failsay
:       rts

; strk/ssec marked free in the VTOC.
sfree:  jsr     bitat
        ora     vtoc,x
        sta     vtoc,x
        rts

; cbuf copied to ebuf (A/X its address).
copyc:  ldx     #0
:       lda     cbuf,x
        sta     ebuf,x
        inx
        bne     :-
        rts

; ---------------------------------------------------------------------------
; The catalog

; The VTOC read and checked as DOS 3.3 would take it.
rdvtoc: lda     #VTRK
        sta     strk
        stz     ssec
        lda     #<vtoc
        ldx     #>vtoc
        jsr     rdsec
        lda     vtoc + $27
        cmp     #NPAIR
        bne     @not
        lda     vtoc + $34
        cmp     #35
        bne     @not
        lda     vtoc + $35
        cmp     #16
        bne     @not
        lda     vtoc + $36
        bne     @not
        lda     vtoc + $37
        cmp     #1
        bne     @not
        lda     vtoc + 1
        cmp     #35
        bcs     @not
        lda     vtoc + 2
        cmp     #16
        bcs     @not
        rts
@not:   lda     #<snotdos
        ldx     #>snotdos
        jmp     failsay
wrvtoc: lda     #VTRK
        sta     strk
        stz     ssec
        lda     #<vtoc
        ldx     #>vtoc
        jmp     wrsec

; The catalog from its start: catnext gives each entry.
catfirst:
        stz     ncat
        lda     vtoc + 1
        sta     nct
        lda     vtoc + 2
        sta     ncs
        lda     #NENT
        sta     ci
        rts
; The next entry: carry clear, ptr at it in cbuf (ct, cs its sector, ci its
; index); carry set at the chain's end. A chain that loops ends after 255
; sectors read, so it can't hang.
catnext:
        inc     ci
        lda     ci
        cmp     #NENT
        bcc     @have
        lda     nct
        bne     :+
        sec
        rts
:       sta     ct
        sta     strk
        lda     ncs
        sta     cs
        sta     ssec
        lda     #<cbuf
        ldx     #>cbuf
        jsr     rdsec
        lda     cbuf + 1
        sta     nct
        lda     cbuf + 2
        sta     ncs
        lda     nct                     ; chained to itself, or 255 sectors read: a loop
        cmp     ct
        bne     :+
        lda     ncs
        cmp     cs
        beq     @loop
:       inc     ncat
        bne     :+
@loop:  lda     #<sbadc
        ldx     #>sbadc
        jmp     failsay
:       stz     ci
@have:
entptr: lda     ci                      ; ptr = cbuf + $0B + ci x 35
        asl     a
        sta     ptr
        asl     a
        asl     a
        asl     a
        asl     a
        clc
        adc     ptr
        adc     ci
        clc
        adc     #<(cbuf + $0B)
        sta     ptr
        lda     #>(cbuf + $0B)
        adc     #0
        sta     ptr + 1
        clc
        rts

; aname's entry: carry clear, ptr at it (ct, cs, ci); carry set if none.
find:   jsr     catfirst
@entry: jsr     catnext
        bcs     @done
        lda     (ptr)
        beq     @entry
        cmp     #$FF
        beq     @entry
        ldy     #3
        ldx     #0
@char:  lda     (ptr),y
        and     #$7F
        cmp     aname,x
        bne     @entry
        iny
        inx
        cpx     #30
        bne     @char
        clc
@done:  rts

; A free entry (never used, or deleted): carry clear, ct, cs, ci; set if
; the catalog has none.
freeent:
        jsr     catfirst
@entry: jsr     catnext
        bcs     @done
        lda     (ptr)
        beq     :+
        cmp     #$FF
        bne     @entry
:       clc
@done:  rts

; The entry's name, its blanks after it not written.
putname:
        ldy     #3 + 29
:       lda     (ptr),y
        and     #$7F
        cmp     #' '
        bne     :+
        dey
        cpy     #3
        bne     :-
:       sty     src
        ldy     #3
@char:  lda     (ptr),y
        and     #$7F
        cmp     #' '                    ; a control character shown as ^X
        bcs     @plain
        pha
        lda     #'^'
        jsr     CM_PUTC
        pla
        ora     #$40
@plain: cmp     #$7F
        bne     :+
        lda     #'?'                    ; DEL too
:       jsr     CM_PUTC
        cpy     src
        beq     :+
        iny
        bra     @char
:       rts

; A, a type byte: its letter; '?' for one of more than one bit (dos33.py's).
typech: and     #$7F
        ldx     #7
:       cmp     tcode,x
        beq     :+
        dex
        bpl     :-
        lda     #'?'
        rts
:       lda     tletter,x
        rts

; The free sectors in the VTOC's bitmap: A/X.
nfree:  stz     src
        stz     src + 1
        ldx     #$38
@byte:  lda     vtoc,x
@bit:   lsr     a
        bcc     :+
        inc     src
        bne     :+
        inc     src + 1
:       cmp     #0
        bne     @bit
        inx
        cpx     #$38 + 35 * 4
        bne     @byte
        lda     src
        ldx     src + 1
        rts

; A/X (0-65535) as three digits, as DOS shows a file's sectors (mod 1000).
dec3:   sta     src + 1                 ; A the high byte, X the low
        stx     src
@mod:   lda     src + 1                 ; under 1000 ($03E8)?
        cmp     #>1000
        bcc     @digits
        bne     :+
        lda     src
        cmp     #<1000
        bcc     @digits
:       lda     src
        sec
        sbc     #<1000
        sta     src
        lda     src + 1
        sbc     #>1000
        sta     src + 1
        bra     @mod
@digits:
        ldy     #0                      ; the hundreds
@h:     lda     src + 1
        bne     :+
        lda     src
        cmp     #100
        bcc     @hd
:       lda     src
        sec
        sbc     #100
        sta     src
        lda     src + 1
        sbc     #0
        sta     src + 1
        iny
        bra     @h
@hd:    tya
        ora     #'0'
        jsr     CM_PUTC
        ldy     #0
        lda     src
@t:     cmp     #10
        bcc     :+
        sbc     #10
        iny
        bra     @t
:       pha
        tya
        ora     #'0'
        jsr     CM_PUTC
        pla
        ora     #'0'
        jmp     CM_PUTC

; A as two hex digits.
hex2:   pha
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jsr     :+
        pla
        and     #$0F
:       cmp     #10
        bcc     :+
        adc     #6
:       adc     #'0'
        jmp     CM_PUTC

; ---------------------------------------------------------------------------
; Sectors, by 76

; The sector strk/ssec into the 256 bytes at A/X (rdsec_tl: lt/ls's).
rdsec_tl:
        ldy     lt
        sty     strk
        ldy     ls
        sty     ssec
rdsec:  ldy     #0
        bra     sect
wrsec:  ldy     #1
sect:   sty     sblk + 3
        sta     sblk + 4
        stx     sblk + 5
        lda     adrv
        sta     sblk
        lda     strk
        sta     sblk + 1
        lda     ssec
        sta     sblk + 2
        lda     #<sblk
        ldx     #>sblk
        ldy     #O_SECTOR
        jsr     BDOS
        cmp     #0
        bne     :+
        rts
:       cmp     #2
        bne     :+
        lda     #<swp
        ldx     #>swp
        jmp     failsay
:       cmp     #$FF
        bne     :+
        lda     #<snotap
        ldx     #>snotap
        jmp     failsay
:       lda     #<serr
        ldx     #>serr
        jmp     failsay

; ---------------------------------------------------------------------------
; The tail

; The word at A/X ("GET", 3 letters) at UT_IDX, then a blank: carry clear,
; UT_IDX past it; carry set if not.
keyword:
        sta     src
        stx     src + 1
        ldx     UT_IDX
        ldy     #0
:       lda     UT_TAIL,x
        cmp     (src),y
        bne     @no
        inx
        iny
        cpy     #3
        bne     :-
        lda     UT_TAIL,x
        cmp     #' '
        bne     @no
        stx     UT_IDX
        clc
        rts
@no:    sec
        rts

; d: (A-P and a colon) into adrv: carry clear; carry set if not there.
drive:  jsr     UT_SKIP
        sec
        sbc     #'A'
        cmp     #16
        bcs     @no
        sta     adrv
        lda     UT_TAIL + 1,x
        cmp     #':'
        bne     @no
        inx
        inx
        stx     UT_IDX
        clc
        rts
@no:    sec
        rts

; An Apple name, right after d:, into aname (30, blanks after): in quotes,
; to the closing one; else to a blank or the end. 1-30 characters.
name:   ldx     #29
        lda     #' '
:       sta     aname,x
        dex
        bpl     :-
        ldx     UT_IDX
        ldy     #0
        lda     UT_TAIL,x
        cmp     #QUOTE
        bne     @plain
        inx
@q:     lda     UT_TAIL,x
        beq     @bad                    ; not closed
        inx
        cmp     #QUOTE
        beq     @end
        jsr     @char
        bra     @q
@plain: lda     UT_TAIL,x
        beq     @end
        cmp     #' '
        beq     @end
        inx
        jsr     @char
        bra     @plain
@end:   stx     UT_IDX
        cpy     #0
        beq     @bad
        rts
@char:  cpy     #30
        bcs     @bad2
        sta     aname,y
        iny
        rts
@bad2:  pla                             ; @char's return
        pla
@bad:   lda     #<sname
        ldx     #>sname
        jmp     failsay

; [e:]FILE.TYP by 152 into fcb: refused or none, the usage.
cpmspec:
        lda     #<fcb
        ldx     #>fcb
        jsr     UT_SPEC
        bcs     :+
        lda     UT_SDELIM               ; ',' or '=' after it: not ours
        bne     :+
        lda     #<(fcb + 1)         ; no wildcard
        ldx     #>(fcb + 1)
        jsr     UT_ISWILD
        bcs     :+
        rts
:       jmp     usage

; The mode letter after the names, if any: gmode (0 none). A: '=' if one
; follows it (PUT's B=addr), else 0. Anything else: the usage.
mode:   stz     gmode
        jsr     UT_SKIP
        beq     @none
        cmp     #'T'
        beq     @got
        cmp     #'B'
        beq     @got
        cmp     #'R'
        bne     @bad
@got:   sta     gmode
        inx
        lda     UT_TAIL,x
        cmp     #'='
        bne     :+
        inx
        stx     UT_IDX
        rts
:       stx     UT_IDX
        jsr     UT_SKIP
        bne     @bad
@none:  lda     #0
        rts
@bad:   jmp     usage

; Hex digits (a '$' first allowed) to the end into baddr: carry set if
; none, or past 4.
hexin:  stz     baddr
        stz     baddr + 1
        ldx     UT_IDX
        lda     UT_TAIL,x
        cmp     #'$'
        bne     :+
        inx
:       ldy     #0
@dig:   lda     UT_TAIL,x
        beq     @end
        cmp     #' '
        beq     @end
        sec
        sbc     #'0'
        cmp     #10
        bcc     @val
        sbc     #'A' - '0' - 10
        cmp     #10
        bcc     @bad
        cmp     #16
        bcs     @bad
@val:   iny
        cpy     #5
        bcs     @bad
        phx
        ldx     #4
:       asl     baddr
        rol     baddr + 1
        dex
        bne     :-
        plx
        ora     baddr
        sta     baddr
        inx
        bra     @dig
@end:   stx     UT_IDX
        cpy     #0
        beq     @bad
        jsr     UT_SKIP
        bne     @bad
        clc
        rts
@bad:   sec
        rts

; ---------------------------------------------------------------------------
; The end

usage:  lda     #<susage
        ldx     #>susage
        bra     failsay
notfound:
        lda     #<snofile
        ldx     #>snofile
        bra     failsay
cpmnone:
        lda     #<scpmno
        ldx     #>scpmno
        bra     failsay
cpmfull:
        lda     #<scpmfull
        ldx     #>scpmfull
        bra     failsay
cpmfail:
        lda     #<scpmerr
        ldx     #>scpmerr
; The message at A/X, the return code $FF00, the end.
failsay:
        jsr     UT_SAY
        jsr     UT_FAIL
        lda     gtemp                   ; GET's $$$ file, not whole: deleted, the old one kept
        beq     :+
        stz     gtemp
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_CLOSE
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
:       jmp     UT_EXIT
; The message at A/X, a line owed, the end.
sayend: jsr     UT_SAY
        jmp     UT_EXIT

        .include "cmd.inc"
        .include "util.inc"

        .rodata
kget:   .byte   "GET"
kput:   .byte   "PUT"
kera:   .byte   "ERA"
tcode:  .byte   $00, $01, $02, $04, $08, $10, $20, $40
tletter: .byte  "TIABSRAB"              ; each code's letter
svol:   .byte   " DOS 3.3 disk, volume $"
sfreeq: .byte   " sectors free$"
saddr:  .byte   "Address $"
scomma: .byte   ", $"
sbytes: .byte   " bytes$"
susage: .byte   "Use: DOS33 d: | GET d:NAME file [T|B|R] | PUT file d:NAME [T|B[=addr]] | ERA d:NAME$"
snotap: .byte   "Not an Apple disk drive.$"
snotdos: .byte  "Not a DOS 3.3 disk.$"
serr:   .byte   "Disk error.$"
swp:    .byte   "Disk write-protected.$"
snofile: .byte  "File not found.$"
sexists: .byte  "File exists.$"
slocked: .byte  "File locked.$"
sfull:  .byte   "Disk full.$"
scatfull: .byte "Catalog full.$"
sname:  .byte   "Bad name.$"
sbig:   .byte   "File too big for B.$"
scpmno: .byte   "No File$"
scpmfull: .byte "CP/M disk full.$"
scpmerr: .byte  "CP/M directory full.$"
scpmrd: .byte   "CP/M read error.$"
sbadf:  .byte   "Bad file: its sector lists aren't a DOS 3.3 file's.$"
sbadc:  .byte   "Bad catalog: its chain loops.$"
ssame:  .byte   "The CP/M file can't be on the Apple disk.$"

        .bss
adrv:   .res    1                       ; the Apple disk's drive, 0-15
aname:  .res    30                      ; its file's name, blanks after
gmode:  .res    1                       ; 'T', 'B', 'R'
sblk:   .res    6                       ; 76's: drive, track, sector, op, address
strk:   .res    1                       ; a sector's track and sector
ssec:   .res    1
lt:     .res    1                       ; the current track/sector list's
ls:     .res    1
flt:    .res    1                       ; PUT's first list
fls:    .res    1
nct:    .res    1                       ; the catalog's next sector
ncs:    .res    1
ct:     .res    1                       ; an entry's catalog sector and index
cs:     .res    1
ci:     .res    1
ncat:   .res    1                       ; catalog sectors read: a loop's end
pi:     .res    1                       ; a pair's index in the list
di:     .res    1                       ; PUT's byte in dbuf
ri:     .res    1                       ; GET's byte in rec
zpend:  .res    2                       ; GET's sectors never written, owed as zeros
first:  .res    1
done:   .res    1
ndata:  .res    2                       ; PUT's data sectors
nlist:  .res    1                       ; and lists
baddr:  .res    2                       ; B's address and length
blen:   .res    2
bleft:  .res    2
fcb:    .res    36
tfcb:   .res    36                      ; GET's $$$ file
gtemp:  .res    1                       ; 1 while it exists
nchk:   .res    1                       ; chkfile's lists
lastcr: .res    1                       ; PUT's text: the last byte a CR
rec:    .res    128
vtoc:   .res    256
cbuf:   .res    256
ebuf:   .res    256
lbuf:   .res    256
dbuf:   .res    256

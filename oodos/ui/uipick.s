; OOUI's file picker, ui_pick and ui_relist (hw/ooui.md, "Message box and
; file picker"; U2, proposed): a modal window on the overlay with a drive
; (UP_DRIVES: A:-G: and M:) and a user (UP_USERS: 0-15), each a choice, a
; list of the files (UF_MULTI with UP_MULTI), what it found, a name field
; (typed into with UP_NEW), and OK, Cancel and the extra button.
;
; The directory is read by functions 17 and 18 in idle polls, a walk from
; its start each poll (OODOS keeps one search, which the program may use
; between polls: the review's Y1), passing what is listed already and
; listing a few more (4, and half as many as are listed, at most 64): for
; each, the program's user, DMA address and segment and error mode are
; kept, the picker's set (functions 32, 26, 51, 45), and the program's put
; back after (32, 26, 51, 45; read by 32, 52 and 49). The search names the
; drive in its FCB, so the current drive never changes, and its extent 0, so
; a file is listed once however many extents it has (and a name listed
; already isn't again). A file whose type isn't one of UP_TYPES is left
; out. A drive that can't be read says so ("Drive error"); one with no
; files, "No files"; a work buffer too small, "n of t files" (Y6). A name
; typed with UP_NEW is one file's, "d:" before it the drive, or "Not a file
; name" (Y3). ui_hide(UI_PICKER) stops the walk (Y2).
;
; The answer is UE_PICK with X UI_PICKER: UV_VALUE 0 chosen (the picker
; gone; the entries in the result buffer, their count in the block's
; UP_CHOSEN): the files marked, or the one selected, or with UP_NEW the name
; typed; 1 cancelled (gone); 2 the extra button (still up). OO-Amp: Load
; Files (its gui/ooadlg.s), with its plugins' types and PROBE's titles as
; the column routine.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .import DLGON, DLGREC, DLGH, UIW_CHOICE, UIW_LIST, UIW_FIELD

PW              = 0                    ; the dialog records' places
PDRV            = 1
PUSR            = 2
PLST            = 3
PFLD            = 4
POK             = 5
PCAN            = 6
PEXT            = 7
PSTA            = 8
ROWS            = 16                   ; the list's rows

        .bss
pblk:   .res    2                      ; the program's block
blk:    .res    UP_LEN
pdrive: .res    1                      ; the drive listed (1 A:)
puser:  .res    1
count:  .res    2                      ; the files listed
pmax:   .res    2                      ; the work buffer's room, in entries
lst:    .res    1                      ; the listing: 0 done, 1 going on
perr:   .res    1                      ; its error (X of 17 or 18), 0 none
pcol:   .res    1                      ; 1: a column routine said "not yet": the list again
rec:    .res    UW_LEN
fcb:    .res    36
dbuf:   .res    128                    ; the directory records
keep:   .res    8                      ; the program's user, DMA address and segment, error mode
scbb:   .res    4
rowbuf: .res    40                     ; a row's text
fbuf:   .res    13                     ; the name field: 8 + "." + 3 + 0
sbuf:   .res    24                     ; the status text
ent:    .res    2                      ; an entry's address
n:      .res    1
didx:   .res    1
dofs:   .res    1                      ; CONSIDER's entry in dbuf
lim:    .res    1                      ; IDLEFN's: the new entries a poll lists
total:  .res    2                      ; the wanted files the last walk found
rrow:   .res    1                      ; ROWFN's row
ans:    .res    1                      ; FINISH's answer
rrowh:  .res    1

        .code

ui_pick:
        jsr     ENTER
        sta     t0                      ; (pblk and blk kept till it is known not to be up)
        stx     t1
        lda     uopen
        beq     @busy
        jsr     DLGON
        lda     #PW                     ; up already: refused, the live one untouched
        jsr     DLGH
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        beq     @take
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_HIDDEN
        bne     @take
@busy:  lda     #UX_BUSY
        sec
        rts
@bad:   lda     #UX_PICK
        sec
        rts
@take:  lda     t0
        sta     pblk
        sta     UIQ
        lda     t1
        sta     pblk + 1
        sta     UIQ + 1
        ldy     #UP_LEN - 1
@b:     lda     (UIQ),y
        sta     blk,y
        dey
        bpl     @b
        lda     blk + UP_WORKLEN        ; the work buffer's room: at least a file
        sta     pmax
        lda     blk + UP_WORKLEN + 1
        sta     pmax + 1
        ldx     #4
@d:     lsr     pmax + 1
        ror     pmax
        dex
        bne     @d
        lda     pmax
        ora     pmax + 1
        beq     @bad
        lda     blk + UP_ROOM
        beq     @bad
@free:  ldx     #0                      ; the widgets it uses
@w:     lda     MODS,x
        sta     UIP
        lda     MODS + 1,x
        sta     UIP + 1
        phx
        jsr     ADDW
        plx
        inx
        inx
        cpx     #6
        bne     @w
        jsr     START
        jsr     MAKE
        lda     #<FILTER
        sta     udf2
        lda     #>FILTER
        sta     udf2 + 1
        lda     #<IDLEFN
        sta     uidle
        lda     #>IDLEFN
        sta     uidle + 1
        jsr     REFOCUS
        lda     #PLST                   ; the focus on the list
        jsr     DLGH
        jsr     SETFOCUS
        clc
        rts

; The drive and user to start on: the block's, or the current ones.
START:  lda     blk + UP_DRIVE
        bne     @dr
        ldy     #DRV_GET
        jsr     BDOS
        inc     a
@dr:    sta     pdrive
        lda     blk + UP_USER
        cmp     #$FF
        bne     @us
        lda     #$FF
        ldy     #F_USERNUM
        jsr     BDOS
@us:    and     #$0F
        sta     puser
; The listing started again: nothing listed, the selection none.
RELIST: stz     count
        stz     count + 1
        stz     total
        stz     total + 1
        stz     perr
        lda     #1
        sta     lst
        lda     #PLST
        jsr     DLGH
        sta     n
        jsr     REC
        lda     udlgp + 1
        beq     @r
        ldy     #UW_TYPE
        lda     (UIR),y
        beq     @r
        ldy     #UW_VALUE               ; no selection, no rows, the top 0
        lda     #$FF
        sta     (UIR),y
        iny
        sta     (UIR),y
        lda     #0
        iny
        sta     (UIR),y
        iny
        sta     (UIR),y
        iny
        sta     (UIR),y
        iny
        sta     (UIR),y
        lda     n
        jsr     DIRTY
        lda     #<T_READING
        ldx     #>T_READING
        jsr     STATUS
@r:     rts

ui_relist:
        jsr     ENTER
        jsr     RELIST
        clc
        rts

; The status label's text A/X.
STATUS: pha
        phx
        lda     #PSTA
        jsr     DLGH
        sta     n
        jsr     REC
        ply
        pla
        ldy     #UW_TEXT
        sta     (UIR),y
        iny
        txa
        sta     (UIR),y
        lda     n
        jmp     DIRTY

; The records.
MAKE:   jsr     CLEAR                   ; the window, centred
        lda     #UT_WINDOW
        sta     rec + UW_TYPE
        lda     #UI_NONE
        sta     rec + UW_PARENT
        lda     #7
        sta     rec + UW_X
        lda     #2
        sta     rec + UW_Y
        lda     #66
        sta     rec + UW_W
        lda     #ROWS + 9
        sta     rec + UW_H
        lda     #UF_MODAL | UF_OVERLAY
        sta     rec + UW_FLAGS
        lda     blk + UP_TITLE
        sta     rec + UW_TEXT
        lda     blk + UP_TITLE + 1
        sta     rec + UW_TEXT + 1
        lda     #PW
        jsr     PUT
        jsr     CHILD                   ; the drive: a choice of 8
        lda     #UT_CHOICE
        sta     rec + UW_TYPE
        lda     #1
        sta     rec + UW_X
        lda     #12
        sta     rec + UW_W
        lda     #<DRIVES
        sta     rec + UW_TEXT
        lda     #>DRIVES
        sta     rec + UW_TEXT + 1
        lda     #DRIVES_N
        sta     rec + UW_A
        ldx     #DRIVES_N - 1           ; the value: the drive's place, A: if it has none
@dv:    lda     DRVMAP,x
        cmp     pdrive
        beq     @df
        dex
        bpl     @dv
        ldx     #0
        lda     #1
        sta     pdrive
@df:    stx     rec + UW_VALUE
        lda     blk + UP_FLAGS
        and     #UP_DRIVES
        bne     @dsh
        lda     #UF_HIDDEN
        sta     rec + UW_FLAGS
@dsh:   lda     #PDRV
        jsr     PUT
        jsr     CHILD                   ; the user: a choice of 16
        lda     #UT_CHOICE
        sta     rec + UW_TYPE
        lda     #15
        sta     rec + UW_X
        lda     #12
        sta     rec + UW_W
        lda     #<USERS
        sta     rec + UW_TEXT
        lda     #>USERS
        sta     rec + UW_TEXT + 1
        lda     #16
        sta     rec + UW_A
        lda     puser
        sta     rec + UW_VALUE
        lda     blk + UP_FLAGS
        and     #UP_USERS
        bne     @ush
        lda     #UF_HIDDEN
        sta     rec + UW_FLAGS
@ush:   lda     #PUSR
        jsr     PUT
        jsr     CHILD                   ; the list
        lda     #UT_LIST
        sta     rec + UW_TYPE
        lda     #1
        sta     rec + UW_X
        lda     #2
        sta     rec + UW_Y
        lda     #<-1
        sta     rec + UW_W
        lda     #ROWS
        sta     rec + UW_H
        lda     #<ROWFN
        sta     rec + UW_TEXT
        lda     #>ROWFN
        sta     rec + UW_TEXT + 1
        lda     #$FF
        sta     rec + UW_VALUE
        sta     rec + UW_VALUE + 1
        lda     blk + UP_FLAGS
        and     #UP_MULTI
        beq     @m
        lda     #UF_MULTI
@m:     ora     #UF_ROUTINE | UF_SCROLL
        sta     rec + UW_FLAGS
        lda     #PLST
        jsr     PUT
        jsr     CHILD                   ; the status
        lda     #UT_LABEL
        sta     rec + UW_TYPE
        lda     #1
        sta     rec + UW_X
        lda     #ROWS + 2
        sta     rec + UW_Y
        lda     #<-1
        sta     rec + UW_W
        lda     #1
        sta     rec + UW_H
        lda     #<T_READING
        sta     rec + UW_TEXT
        lda     #>T_READING
        sta     rec + UW_TEXT + 1
        lda     #PSTA
        jsr     PUT
        jsr     CHILD                   ; the name: typed only with UP_NEW
        stz     fbuf
        lda     #UT_FIELD
        sta     rec + UW_TYPE
        lda     #1
        sta     rec + UW_X
        lda     #ROWS + 4
        sta     rec + UW_Y
        lda     #14
        sta     rec + UW_W
        lda     #1
        sta     rec + UW_H
        lda     #<fbuf
        sta     rec + UW_TEXT
        lda     #>fbuf
        sta     rec + UW_TEXT + 1
        lda     #13
        sta     rec + UW_A
        lda     blk + UP_FLAGS
        and     #UP_NEW
        beq     @old
        lda     #0                      ; (UP_NEW's bit isn't a flag)
        bra     @nw
@old:   lda     #UF_DISABLED
@nw:    ora     #UF_UPPER
        sta     rec + UW_FLAGS
        lda     #PFLD
        jsr     PUT
        jsr     CHILD                   ; OK, Cancel, the extra button: its client's last row
        lda     #<T_OK
        ldx     #>T_OK
        ldy     #<-36
        jsr     BUTTON
        lda     #UF_DEFAULT
        sta     rec + UW_FLAGS
        lda     #POK
        jsr     PUT
        jsr     CHILD
        lda     #<T_CANCEL
        ldx     #>T_CANCEL
        ldy     #<-24
        jsr     BUTTON
        lda     #UF_CANCEL
        sta     rec + UW_FLAGS
        lda     #PCAN
        jsr     PUT
        jsr     CHILD
        lda     blk + UP_EXTRA
        ldx     blk + UP_EXTRA + 1
        ldy     #<-12
        jsr     BUTTON
        lda     blk + UP_EXTRA
        ora     blk + UP_EXTRA + 1
        bne     @x
        lda     #UF_HIDDEN
        sta     rec + UW_FLAGS
@x:     lda     #PEXT
        jsr     PUT
        jmp     RELIST

; rec cleared, the window's child at its client's top left.
CHILD:  jsr     CLEAR
        lda     #PW
        sta     rec + UW_PARENT
        lda     #1
        sta     rec + UW_H
        rts
CLEAR:  ldy     #UW_LEN - 1
        lda     #0
@z:     sta     rec,y
        dey
        bpl     @z
        rts
; rec a button: text A/X, at column Y from the right, the last row, 10 wide.
BUTTON: sta     rec + UW_TEXT
        stx     rec + UW_TEXT + 1
        sty     rec + UW_X
        lda     #UT_BUTTON
        sta     rec + UW_TYPE
        lda     #<-1
        sta     rec + UW_Y
        lda     #10
        sta     rec + UW_W
        rts
PUT:    pha
        lda     #<rec
        sta     UIQ
        lda     #>rec
        sta     UIQ + 1
        pla
        jmp     DLGREC

; ---------------------------------------------------------------------------
; The listing: an idle poll's walk.

IDLEFN: lda     pcol                    ; a column said "not yet": the list again
        beq     @l
        stz     pcol
        lda     #PLST
        jsr     DLGH
        jsr     DIRTY
@l:     lda     lst
        bne     @up
@none:  lda     #UE_NONE
        rts
@up:    lda     #PW                     ; taken away (ui_hide(UI_PICKER)): the walk stops (Y2)
        jsr     DLGH
        jsr     RECT
        bcc     @go
        stz     lst                     ;   (as FINISH does, with no answer)
        stz     uidle + 1
        stz     udf2 + 1
        bra     @none
@go:    jsr     SAVE                    ; a walk of the directory from its start each time (Y1):
        stz     n                       ;   OODOS keeps one search, and the program may use it
        stz     total                   ;   between polls; what is listed already is passed by
        stz     total + 1
        lda     count + 1               ; this poll's new: 4 and half as many as are listed, at
        bne     @most                   ;   most 64, so the walks from the start cost about three
        lda     count                   ;   times the directory's matches, not their square / 8
        lsr     a
        clc
        adc     #4
        cmp     #64
        bcc     @lim
@most:  lda     #64
@lim:   sta     lim
        jsr     MKFCB
        ldy     #F_SFIRST
@call:  lda     #<fcb
        ldx     #>fcb
        jsr     BDOS
        cmp     #$FF
        bne     @found
        stx     perr                    ; the walk's end (X 0), or an error: the listing done
        stz     lst
        bra     @end
@found: jsr     CONSIDER
        lda     n
        cmp     lim                     ; enough for this poll
        beq     @end
        ldy     #F_SNEXT
        bra     @call
@end:   jsr     PUTBACK
        jsr     COUNTED
        lda     #UE_NONE
        rts

; The FCB: the drive, every name and type, extent 0.
MKFCB:  ldx     #35
@z:     stz     fcb,x
        dex
        bpl     @z
        lda     pdrive
        sta     fcb
        lda     #'?'
        ldx     #11
@q:     sta     fcb,x
        dex
        bne     @q
        rts

; The program's user, DMA address, segment and error mode kept, the
; picker's set.
SAVE:   lda     #$FF
        ldy     #F_USERNUM
        jsr     BDOS
        sta     keep
        lda     #<(keep + 1)            ; +1-2 the address, +3-4 the segment
        ldx     #>(keep + 1)
        ldy     #F_DMAGET
        jsr     BDOS
        lda     #$4B                    ; the error mode: the SCB's
        sta     scbb
        stz     scbb + 1
        lda     #<scbb
        ldx     #>scbb
        ldy     #S_SCB
        jsr     BDOS
        sta     keep + 5
        lda     puser
        ldy     #F_USERNUM
        jsr     BDOS
        lda     #<dbuf
        ldx     #>dbuf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #$FF
        tax
        ldy     #F_DMASEG
        jsr     BDOS
        lda     #$FF                    ; errors returned, not said
        ldy     #F_ERRMODE
        jmp     BDOS
PUTBACK:
        lda     keep
        ldy     #F_USERNUM
        jsr     BDOS
        lda     keep + 1
        ldx     keep + 2
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     keep + 3
        ldx     keep + 4
        ldy     #F_DMASEG
        jsr     BDOS
        lda     keep + 5
        ldy     #F_ERRMODE
        jmp     BDOS

; The entry at dbuf + 32 x A: if its type is wanted, counted (total); if it
; isn't listed already, listed while there's room (n counting the new).
CONSIDER:
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        sta     dofs                    ; the entry in dbuf
        tax
        jsr     WANTED
        bcs     @r
        inc     total
        bne     @t
        inc     total + 1
@t:     jsr     LISTED
        bcs     @r                      ; listed already
        lda     count                   ; room?
        cmp     pmax
        lda     count + 1
        sbc     pmax + 1
        bcs     @r                      ; full: counted, not listed (the status says so: Y6)
        lda     count
        ldx     count + 1
        jsr     ENTRY
        ldx     dofs
        ldy     #0
        lda     pdrive
        sta     (UIQ),y
        iny
        lda     puser
        sta     (UIQ),y
@nm:    lda     dbuf + 1,x              ; the name and type, their attribute bits clear
        and     #$7F
        iny
        sta     (UIQ),y
        inx
        cpy     #UP_ENAME + 10
        bne     @nm
        lda     #0
@z:     iny
        sta     (UIQ),y
        cpy     #UP_ELEN - 1
        bne     @z
        inc     count
        bne     @c
        inc     count + 1
@c:     inc     n
@r:     rts

; Carry clear if the entry at dbuf + X has a type UP_TYPES wants (or there is
; no UP_TYPES).
WANTED: lda     blk + UP_TYPES
        sta     UIQ
        lda     blk + UP_TYPES + 1
        sta     UIQ + 1
        ora     UIQ
        beq     @yes
        ldy     #0
@type:  lda     (UIQ),y
        beq     @no
        sty     didx
        lda     dbuf + 9,x
        and     #$7F
        cmp     (UIQ),y
        bne     @nx
        iny
        lda     dbuf + 10,x
        and     #$7F
        cmp     (UIQ),y
        bne     @nx
        iny
        lda     dbuf + 11,x
        and     #$7F
        cmp     (UIQ),y
        beq     @yes
@nx:    lda     didx
        clc
        adc     #3
        tay
        bra     @type
@yes:   clc
        rts
@no:    sec
        rts

; Carry set if the entry at dbuf + dofs is listed already (its name and
; type): clear if it is new. The list is walked by address, its first byte
; compared first, so a walk costs little more than a byte a file.
LISTED: lda     blk + UP_WORK
        sta     UIQ
        lda     blk + UP_WORK + 1
        sta     UIQ + 1
        stz     ent
        stz     ent + 1
        ldx     dofs
@e:     lda     ent
        cmp     count
        lda     ent + 1
        sbc     count + 1
        bcs     @new
        ldy     #UP_ENAME
@b:     lda     dbuf + 1,x
        and     #$7F
        cmp     (UIQ),y
        bne     @next
        inx
        iny
        cpy     #UP_ENAME + 11
        bne     @b
        sec                             ; the same
        rts
@next:  ldx     dofs
        lda     UIQ
        clc
        adc     #UP_ELEN
        sta     UIQ
        bcc     @i
        inc     UIQ + 1
@i:     inc     ent
        bne     @e
        inc     ent + 1
        bra     @e
@new:   clc
        rts

; UIQ = ROWFN's entry again (the column routine may have used it).
ENTRY2: phx
        lda     rrow
        ldx     rrowh
        jsr     ENTRY
        plx
        rts

; UIQ = entry A/X of the work buffer.
ENTRY:  stx     t0
        asl     a
        rol     t0
        asl     a
        rol     t0
        asl     a
        rol     t0
        asl     a
        rol     t0
        clc
        adc     blk + UP_WORK
        sta     UIQ
        lda     t0
        adc     blk + UP_WORK + 1
        sta     UIQ + 1
        rts

; The list's rows the count; the status what was found when the listing
; is done.
COUNTED:
        lda     #PLST
        jsr     DLGH
        sta     n
        jsr     REC
        ldy     #UW_A
        lda     count
        sta     (UIR),y
        iny
        lda     count + 1
        sta     (UIR),y
        lda     n
        jsr     DIRTY
        lda     lst
        bne     @r
        lda     perr
        beq     @ok
        lda     #<T_ERROR
        ldx     #>T_ERROR
        jmp     STATUS
@ok:    lda     count
        ora     count + 1
        bne     @some
        lda     #<T_NONE
        ldx     #>T_NONE
        jmp     STATUS
@some:  ldx     #0                      ; "n files", or "n of t files" when the buffer was full (Y6)
        lda     count
        ldy     count + 1
        jsr     NUMBER
        lda     count
        cmp     total
        bne     @of
        lda     count + 1
        cmp     total + 1
        beq     @files
@of:    ldy     #0
@o:     lda     T_OF,y
        beq     @tot
        sta     sbuf,x
        inx
        iny
        bra     @o
@tot:   lda     total
        ldy     total + 1
        jsr     NUMBER
@files: ldy     #0
@f:     lda     T_FILES,y
        sta     sbuf,x
        beq     @show
        inx
        iny
        bra     @f
@show:  lda     #<sbuf
        ldx     #>sbuf
        jmp     STATUS
@r:     rts

; A/Y (low, high) in decimal at sbuf + X: X past it.
NUMBER: sta     t0
        sty     t1
        ldy     #4                      ; 10,000s to 1s
        stz     t3                      ; a digit put yet
@dig:   lda     #'0'
        sta     t2
@sub:   lda     t0
        sec
        sbc     POW,y
        pha
        lda     t1
        sbc     POWH,y
        bcc     @less
        sta     t1
        pla
        sta     t0
        inc     t2
        bra     @sub
@less:  pla
        lda     t2
        bit     t3                      ; no leading zeros
        bmi     @put
        cmp     #'0'
        bne     @put
        cpy     #0
        bne     @nd
@put:   sta     sbuf,x
        inx
        lda     #$FF
        sta     t3
@nd:    dey
        bpl     @dig
        rts

; ---------------------------------------------------------------------------
; The list's rows: A/X the row, Y the list: A/X its text, Y its style.

ROWFN:  sta     rrow
        stx     rrowh
        jsr     ENTRY
        ldx     #0                      ; the name, its spaces left out, a dot, the type
        ldy     #UP_ENAME
@n:     lda     (UIQ),y
        cmp     #' '
        beq     @s
        sta     rowbuf,x
        inx
@s:     iny
        cpy     #UP_ENAME + 8
        bne     @n
        lda     #'.'
        sta     rowbuf,x
        inx
@t:     lda     (UIQ),y
        cmp     #' '
        beq     @ts
        sta     rowbuf,x
        inx
@ts:    iny
        cpy     #UP_ENAME + 11
        bne     @t
        stz     rowbuf,x
        lda     blk + UP_COLUMN + 1     ; the column routine's text after a TAB
        beq     @style
        stx     didx
        phx
        lda     #9
        sta     rowbuf,x
        lda     UIQ
        ldx     UIQ + 1
        ldy     rrow
        jsr     @col
        sta     UIP
        stx     UIP + 1
        plx
        ora     UIP + 1
        bne     @cat
        lda     #1                      ; "not yet": the list drawn again later
        sta     pcol
        ldx     didx
        stz     rowbuf,x
        bra     @style
@cat:   jsr     ENTRY2
        inx
        ldy     #0
@c:     lda     (UIP),y
        sta     rowbuf,x
        beq     @style
        inx
        iny
        cpx     #39
        bcc     @c
        stz     rowbuf,x
@style: jsr     ENTRY2
        ldy     #UP_ELEN - 3            ; the mark
        lda     (UIQ),y
        beq     @plain
        ldy     #UL_MARKED
        bra     @ret
@plain: ldy     #UL_NORMAL
@ret:   lda     #<rowbuf
        ldx     #>rowbuf
        rts
@col:   jmp     (blk + UP_COLUMN)

; ---------------------------------------------------------------------------
; The picker's events, before the program sees them.

FILTER: sta     t2
        lda     evh
        cmp     #UI_NONE
        beq     @keep
        sec
        sbc     ucount
        bcc     @keep
        cmp     #PSTA + 1
        bcs     @keep
        tax                             ; X its place
        lda     t2
        cmp     #UE_MODAL
        beq     @keep
        cpx     #PDRV
        beq     @drv
        cpx     #PUSR
        beq     @usr
        cpx     #PLST
        beq     @lst
        cpx     #PFLD
        beq     @fld
        cmp     #UE_CLICK
        bne     @drop
        lda     #0
        cpx     #POK
        beq     @fin
        lda     #1
        cpx     #PCAN
        beq     @fin
        cpx     #PEXT
        bne     @drop
        lda     #2                      ; the extra button: UE_PICK 2, still up
        sta     evv
        stz     evv + 1
        lda     #PW
        jsr     DLGH
        sta     evh
        lda     #UE_PICK
        rts
@fin:   jmp     FINISH
@keep:  lda     t2
        rts
@drop:  lda     #UE_NONE
        rts
@drv:   cmp     #UE_CHANGE              ; another drive: listed
        bne     @drop
        ldx     evv
        lda     DRVMAP,x
        sta     pdrive
        jsr     RELIST
        bra     @drop
@usr:   cmp     #UE_CHANGE              ; another user
        bne     @drop
        lda     evv
        sta     puser
        jsr     RELIST
        bra     @drop
@lst:   cmp     #UE_SELECT              ; Enter or a double-click: chosen
        bne     @l2
        lda     #0
        jmp     FINISH
@l2:    cmp     #UE_MARK
        bne     @l3
        jsr     MARK
        bra     @drop
@l3:    cmp     #UE_CHANGE              ; the selection's name in the field
        bne     @drop
        jsr     NAMEOF
        bra     @drop
@fld:   cmp     #UE_CHANGE              ; Enter in the name (UP_NEW): chosen
        bne     @drop
        lda     evf
        and     #UV_LIVE
        bne     @drop
        lda     #0
        jmp     FINISH

; The selected row's mark flipped.
MARK:   lda     evv
        ldx     evv + 1
        jsr     ENTRY
        ldy     #UP_ELEN - 3
        lda     (UIQ),y
        eor     #1
        sta     (UIQ),y
        lda     #PLST
        jsr     DLGH
        jmp     DIRTY

; The selected row's name in the field, as "NAME.TYP".
NAMEOF: lda     evv
        ldx     evv + 1
        jsr     ENTRY
        ldx     #0
        ldy     #UP_ENAME
@n:     lda     (UIQ),y
        cmp     #' '
        beq     @s
        sta     fbuf,x
        inx
@s:     iny
        cpy     #UP_ENAME + 8
        bne     @n
        lda     #'.'
        sta     fbuf,x
        inx
@t:     lda     (UIQ),y
        cmp     #' '
        beq     @ts
        sta     fbuf,x
        inx
@ts:    iny
        cpy     #UP_ENAME + 11
        bne     @t
        stz     fbuf,x
        lda     #PFLD
        jsr     DLGH
        sta     n
        jsr     REC
        ldy     #UW_VALUE               ; the cursor at its end
        txa
        sta     (UIR),y
        lda     n
        jmp     DIRTY

; The answer A (0 chosen, 1 cancelled): the picker gone, UE_PICK. Chosen:
; the result buffer filled and UP_CHOSEN set; nothing to choose, nothing
; done (it stays).
FINISH: sta     ans
        stz     n                       ; the entries given
        lda     ans
        bne     @gone
        lda     blk + UP_FLAGS          ; a name typed (UP_NEW)
        and     #UP_NEW
        beq     @marks
        lda     fbuf
        beq     @marks
        jsr     TYPED
        bcc     @gone
        lda     #<T_BADNAME             ; not a name: it stays, and says so (Y3)
        ldx     #>T_BADNAME
        jsr     STATUS
        lda     #UE_NONE
        rts
@marks: lda     blk + UP_FLAGS
        and     #UP_MULTI
        beq     @sel
        stz     ent
        stz     ent + 1
@m:     lda     ent
        cmp     count
        lda     ent + 1
        sbc     count + 1
        bcs     @msel
        lda     ent
        ldx     ent + 1
        jsr     ENTRY
        ldy     #UP_ELEN - 3
        lda     (UIQ),y
        beq     @mn
        jsr     GIVE
@mn:    inc     ent
        bne     @m
        inc     ent + 1
        bra     @m
@msel:  lda     n
        bne     @gone
@sel:   lda     #PLST                   ; the selection
        jsr     DLGH
        jsr     REC
        ldy     #UW_VALUE
        lda     (UIR),y
        sta     ent
        iny
        lda     (UIR),y
        sta     ent + 1
        and     ent
        cmp     #$FF
        beq     @none                   ; nothing chosen: it stays
        lda     ent
        ldx     ent + 1
        jsr     ENTRY
        jsr     GIVE
@gone:  lda     pblk                    ; UP_CHOSEN in the program's block
        sta     UIQ
        lda     pblk + 1
        sta     UIQ + 1
        ldy     #UP_CHOSEN
        lda     n
        sta     (UIQ),y
        lda     #PW
        jsr     DLGH
        jsr     HIDEH
        stz     uidle + 1
        stz     lst
        lda     ans
        sta     evv
        stz     evv + 1
        lda     #PW
        jsr     DLGH
        sta     evh
        lda     #UE_PICK
        rts
@none:  lda     #UE_NONE
        rts

; The entry at UIQ copied to the result buffer's next, if it has room
; (its mark clear in the copy).
GIVE:   lda     n
        cmp     blk + UP_ROOM
        bcs     @r
        sta     t0                      ; the result's place: n x 16
        stz     t1
        ldx     #4
@x:     asl     t0
        rol     t1
        dex
        bne     @x
        clc
        lda     blk + UP_RESULT
        adc     t0
        sta     UIP
        lda     blk + UP_RESULT + 1
        adc     t1
        sta     UIP + 1
        ldy     #UP_ELEN - 1
@c:     lda     (UIQ),y
        sta     (UIP),y
        dey
        bpl     @c
        ldy     #UP_ELEN - 3
        lda     #0
        sta     (UIP),y
        inc     n
@r:     rts

; The name typed, as an entry (8 + 3, capitals, spaces): given, carry
; clear. "d:" before it is the drive (A: to P:). Carry set, nothing given, if
; it isn't one file's name (Y3): a wildcard, a blank or another of the
; parser's delimiters in it, a name of more than 8 or a type of more than 3,
; or no name.
TYPED:  lda     #<rec                   ; built in rec
        sta     UIQ
        lda     #>rec
        sta     UIQ + 1
        lda     pdrive
        sta     rec + UP_EDRIVE
        lda     puser
        sta     rec + UP_EUSER
        ldx     #10
        lda     #' '
@sp:    sta     rec + UP_ENAME,x
        dex
        bpl     @sp
        stz     rec + 13
        stz     rec + 14
        stz     rec + 15
        ldy     #0
        lda     fbuf + 1                ; a drive?
        cmp     #':'
        bne     @name
        lda     fbuf
        jsr     UPPER
        sec
        sbc     #'A'
        cmp     #16
        bcs     @bad
        inc     a
        sta     rec + UP_EDRIVE
        ldy     #2
@name:  ldx     #0                      ; the name, to the dot
@n:     lda     fbuf,y
        beq     @end
        iny
        cmp     #'.'
        beq     @dot
        jsr     BADCH
        bcs     @bad
        cpx     #8
        bcs     @bad
        jsr     UPPER
        sta     rec + UP_ENAME,x
        inx
        bra     @n
@dot:   cpx     #0
        beq     @bad
        ldx     #8                      ; the type
@t:     lda     fbuf,y
        beq     @g
        iny
        jsr     BADCH
        bcs     @bad
        cpx     #11
        bcs     @bad
        jsr     UPPER
        sta     rec + UP_ENAME,x
        inx
        bra     @t
@end:   cpx     #0
        beq     @bad
@g:     jsr     GIVE
        clc
        rts
@bad:   sec
        rts

; Carry set if A can't be in a file's name: a control or a blank, DEL and
; above, a wildcard, or one of the delimiters OODOS's parser stops at.
; Keeps X and Y.
BADCH:  cmp     #'!'
        bcc     @bad
        cmp     #$7F
        bcs     @bad
        phx
        ldx     #BADS_N - 1
@l:     cmp     BADS,x
        beq     @b
        dex
        bpl     @l
        plx
        clc
        rts
@b:     plx
@bad:   sec
        rts

        .rodata
MODS:   .word   UIW_CHOICE, UIW_LIST, UIW_FIELD
DRVMAP: .byte   1, 2, 3, 4, 5, 6, 7, 13
DRIVES_N = * - DRVMAP
DRIVES: .word   D_A, D_B, D_C, D_D, D_E, D_F, D_G, D_M
D_A:    .asciiz "Drive A:"
D_B:    .asciiz "Drive B:"
D_C:    .asciiz "Drive C:"
D_D:    .asciiz "Drive D:"
D_E:    .asciiz "Drive E:"
D_F:    .asciiz "Drive F:"
D_G:    .asciiz "Drive G:"
D_M:    .asciiz "Drive M:"
USERS:
.repeat 16, i
        .word   U_0 + i * 8
.endrepeat
U_0:
.repeat 16, i
.if i < 10
        .byte   "User ", '0' + i, 0, 0
.else
        .byte   "User 1", '0' + i - 10, 0
.endif
.endrepeat
POW:    .byte   <1, <10, <100, <1000, <10000
POWH:   .byte   >1, >10, >100, >1000, >10000
T_READING: .asciiz "Reading..."
T_ERROR: .asciiz "Drive error"
T_NONE: .asciiz "No files"
T_FILES: .asciiz " files"
T_OF:   .asciiz " of "
T_BADNAME: .asciiz "Not a file name"
BADS:   .byte   "*?.,:;=<>|[]"
BADS_N = * - BADS
T_OK:   .asciiz "&OK"
T_CANCEL: .asciiz "Cancel"

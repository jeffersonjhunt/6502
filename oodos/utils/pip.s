; PIP.COM (hw/oodos.md, "PIP, phase 4b2"; the User's Guide's PIP, 5-63 to
; 5-76): PIP {dest=src{[o]}{,src{[o]}}}.
;
;   a tail      one command: dest=src{,src}, a destination a file (a
;               password after ';'), a drive or a device ([Gn] its only
;               option), each source a file, a drive or a device with an
;               option list; the forms and the options as hw/oodos.md has
;               them
;   none        "OODOS PIP 0.2", then '*' and a command a line, until an
;               empty one; the line in capitals but for S's and Q's strings
;
; A command is split at '=' and ',' (not inside an option list) into its
; entries, the destination first; each entry's name is parsed by 152 (a
; device name first, CON: AUX: LST: PRN: NUL: EOF:), its options into a
; byte a letter. Then one of three: to a drive (each matching file under
; its own name), to a file (the sources joined), or to a device. Every
; transfer runs through one pipeline: by records unless an option or a
; device asks for characters. A file is written to NAME.$$$, then renamed.
; An error says its message and unwinds to the command's end (the stack
; as the command found it), the temporary file deleted; the return code
; $FF00. The BDOS's own errors it says itself (error mode $FE).

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

CTLZ    = $1A
CR      = $0D
LF      = $0A
FFEED   = $0C
TAB     = $09

MAXE    = 17                            ; the destination and 16 sources
NOPT    = 26                            ; a byte a letter, A to Z

K_FILE  = 0                             ; an entry's kind
K_DRIVE = 1
K_DEV   = 2

D_CON   = 1                             ; the devices
D_AUX   = 2
D_LST   = 3
D_PRN   = 4
D_NUL   = 5
D_EOF   = 6

        OODOS_HEADER

        .zeropage
ptr:    .res    2
ptr2:   .res    2
ptr3:   .res    2                       ; seen's, its own

        .code
start:  jsr     UT_INIT
        lda     #$FF                    ; the user PIP was started in
        ldy     #F_USERNUM
        jsr     BDOS
        sta     user0
        lda     #$FF                    ; the console mode, kept; bit 0 on, so 11
        tax                             ; reports only ^C and 6 then reads it
        ldy     #C_MODE
        jsr     BDOS
        sta     mode0
        stx     mode0 + 1
        ora     #1
        ldy     #C_MODE
        jsr     BDOS
        lda     BP_DMA
        beq     prompt
        stz     inter
        jsr     command
        jmp     leave

; No tail: the banner, then a command a line until an empty one.
prompt: lda     #1
        sta     inter
        lda     #<sbanner
        ldx     #>sbanner
        jsr     UT_SAY
@line:  jsr     readline
        beq     leave
        jsr     command
        bra     @line

; The end: the user and the console mode PIP found put back.
leave:  lda     user0
        ldy     #F_USERNUM
        jsr     BDOS
        lda     mode0
        ldx     mode0 + 1
        ldy     #C_MODE
        jsr     BDOS
        jmp     UT_EXIT

; '*' and a line read into the tail ($0281): in capitals, but for the
; strings of S and Q (in an option list, from the letter to a ^Z). Out: A
; (and Z) its length, blanks alone taken as none.
readline:
        lda     #<sstar
        ldx     #>sstar
        jsr     CM_PUTS
        lda     #126
        sta     lbuf
        stz     lbuf + 1
        lda     #<lbuf
        ldx     #>lbuf
        ldy     #C_READSTR
        jsr     BDOS
        lda     #1                      ; function 10 echoed only the CR
        sta     CM_LFDUE
        ldx     #0
        stz     rl_list
        stz     rl_str
@char:  cpx     lbuf + 1
        beq     @end
        lda     lbuf + 2,x
        beq     @end
        ldy     rl_str
        bne     @asis                   ; inside S's or Q's string
        cmp     #'['
        bne     :+
        sta     rl_list
:       cmp     #']'
        bne     :+
        stz     rl_list
:       cmp     #'a'
        bcc     @up
        cmp     #'z' + 1
        bcs     @up
        and     #$DF
@up:    ldy     rl_list
        beq     @put
        cmp     #'S'
        beq     @str
        cmp     #'Q'
        bne     @put
@str:   sta     rl_str
        bra     @put
@asis:  cmp     #CTLZ
        bne     @put
        stz     rl_str
@put:   sta     UT_TAIL,x
        inx
        bra     @char
@end:   stz     UT_TAIL,x
        stx     BP_DMA
        ldx     #0                      ; blanks alone: none
@blank: lda     UT_TAIL,x
        beq     @none
        inx
        cmp     #' '
        beq     @blank
        cmp     #TAB
        beq     @blank
        lda     BP_DMA
        rts
@none:  lda     #0
        rts

; ---- a command -------------------------------------------------------------

; The tail's command: split, parsed, done. An error unwinds to cmdend.
command:
        tsx
        stx     cmdsp
        stz     tmpopen
        stz     nent
        stz     ddrive
        jsr     split
        jsr     check
        lda     e_kind                  ; the destination's kind
        cmp     #K_DRIVE
        bne     :+
        jmp     todrive
:       cmp     #K_DEV
        bne     :+
        jmp     todev
:       jmp     tofile
; Every command comes back here, done or failed.
cmdend: ldx     cmdsp
        txs
        rts

; A failure: the message at A/X (and the name at failname, if fname is
; set), the temporary file deleted, the return code $FF00, the command's
; end.
fail:   pha
        phx
        jsr     CM_FLUSH
        plx
        pla
        jsr     CM_PUTS
        lda     fname
        beq     :+
        lda     #<failname
        ldx     #>failname
        jsr     CM_FNAME
        stz     fname
:       jsr     CM_EOL
        jsr     UT_FAIL
        jsr     droptmp
        jmp     cmdend

; The same, after "MESSAGE - " a name: the 11 bytes at ptr.
failnm: pha
        phx
        ldy     #10
:       lda     (ptr),y
        and     #$7F
        sta     failname,y
        dey
        bpl     :-
        lda     #1
        sta     fname
        plx
        pla
        jmp     fail

; The BDOS said an error itself: nothing owed, the return code, the end.
said:   jsr     UT_SAID
        jsr     droptmp
        jmp     cmdend

; The temporary file, if one is open, deleted (its user's).
droptmp:
        lda     tmpopen
        beq     @none
        stz     tmpopen
        lda     duser
        jsr     setuser
        jsr     tmpdma
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_CLOSE
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
@none:  rts

; ---- splitting and parsing -----------------------------------------------

; The tail into entries: the destination, '=', the sources between ','s.
; Each entry's text (blanks trimmed) parsed; its option lists into its
; option bytes.
split:  stz     idx
        stz     nent
@entry: ldx     nent
        cpx     #MAXE
        bcc     :+
        jmp     badfmt                  ; more than 16 sources (ours)
:       jsr     clearent
        ldx     idx
        stx     tstart
        stx     tend
        stz     tdone
@scan:  ldx     idx
        lda     UT_TAIL,x
        beq     @delim
        cmp     #'='
        beq     @delim
        cmp     #','
        beq     @delim
        cmp     #'['
        bne     @text
        lda     #1                      ; the text ends at the first list
        sta     tdone
        jsr     options
        bra     @scan
@text:  inx
        stx     idx
        ldy     tdone
        bne     @scan
        stx     tend
        bra     @scan
@delim: pha
        jsr     parse
        inc     nent
        pla
        ldx     nent
        cpx     #1
        bne     @source
        cmp     #'='                    ; after the destination: '='
        beq     @next
        jmp     badfmt
@source:
        cmp     #'='
        bne     :+
        jmp     badfmt
:       cmp     #0
        beq     @done
@next:  inc     idx
        jmp     @entry
@done:  lda     nent
        cmp     #2
        bcs     :+
        jmp     badfmt
:       rts

; Entry nent cleared: its options none, its kind a file.
clearent:
        ldx     nent
        stz     e_kind,x
        stz     e_dev,x
        jsr     optptr
        ldy     #NOPT - 1
        lda     #0
:       sta     (ptr),y
        dey
        bpl     :-
        rts

; ptr: entry X's option bytes. Keeps X.
optptr: lda     optlo,x
        sta     ptr
        lda     opthi,x
        sta     ptr + 1
        rts

; ptr: entry X's FCB. Keeps X.
fcbptr: lda     fcblo,x
        sta     ptr
        lda     fcbhi,x
        sta     ptr + 1
        rts

; The text tstart-tend of entry nent: a device, or a specification by 152.
parse:  ldx     tstart                  ; trimmed into seg, 0-ended
@lead:  cpx     tend
        beq     @copy
        lda     UT_TAIL,x
        cmp     #' '
        beq     :+
        cmp     #TAB
        bne     @copy
:       inx
        bra     @lead
@copy:  ldy     #0
@c:     cpx     tend
        beq     @trail
        lda     UT_TAIL,x
        sta     seg,y
        inx
        iny
        bra     @c
@trail: cpy     #0
        beq     @set
        lda     seg - 1,y
        cmp     #' '
        beq     :+
        cmp     #TAB
        bne     @set
:       dey
        bra     @trail
@set:   lda     #0
        sta     seg,y
        cpy     #0
        bne     :+
        jmp     badfmt                  ; an empty entry
:       cpy     #4                      ; a device: four characters, ':' last
        bne     @spec
        lda     seg + 3
        cmp     #':'
        bne     @spec
        stz     t0                      ; its name among the six, 3 bytes each
@dev:   ldx     t0
        ldy     #0
@dc:    lda     devnames,x
        cmp     seg,y
        bne     @dnext
        inx
        iny
        cpy     #3
        bne     @dc
        lda     t0                      ; its code: 1-6
        ldx     #0
:       inx
        sec
        sbc     #3
        bcs     :-
        txa
        ldx     nent
        sta     e_dev,x
        lda     #K_DEV
        sta     e_kind,x
        rts
@dnext: lda     t0
        clc
        adc     #3
        sta     t0
        cmp     #18
        bcc     @dev
@spec:  ldx     nent                    ; 152 into the entry's FCB
        jsr     fcbptr
        ldy     #35
@clr:   lda     #0
        cpy     #24
        bcs     @z
        cpy     #16
        bcs     @b
        cpy     #12
        bcs     @z
        cpy     #1
        bcc     @z
@b:     lda     #' '
@z:     sta     (ptr),y
        dey
        bpl     @clr
        lda     #<seg
        sta     pfcb
        lda     #>seg
        sta     pfcb + 1
        lda     ptr
        sta     pfcb + 2
        lda     ptr + 1
        sta     pfcb + 3
        lda     #<pfcb
        ldx     #>pfcb
        ldy     #F_PARSE
        jsr     BDOS
        cpx     #$FF
        bne     @ok
        cmp     #$FF
        bne     @ok
        jmp     badspec                 ; refused: a bad drive or name
@ok:    cpx     #0                      ; 0: the end
        bne     @more
        cmp     #0
        beq     @kind
@more:  sta     ptr2                    ; where 152 stopped: only blanks after
        stx     ptr2 + 1
        ldy     #0
@after: lda     (ptr2),y
        beq     @kind
        cmp     #' '
        beq     :+
        cmp     #':'
        bne     @sep
        jmp     badspec                 ; NAME: that isn't a device
@sep:   lda     #<ssep
        ldx     #>ssep
        jmp     fail
:       iny
        bne     @after
@kind:  ldx     nent                    ; its password kept (an open overwrites 16-31)
        jsr     fcbptr
        lda     pwlo,x
        sta     ptr2
        lda     pwhi,x
        sta     ptr2 + 1
        ldy     #16
:       lda     (ptr),y
        sta     (ptr2)
        inc     ptr2
        bne     :+
        inc     ptr2 + 1
:       iny
        cpy     #24
        bne     :--
        ldy     #1                      ; no name: a drive
        lda     (ptr),y
        cmp     #' '
        bne     @file
        ldy     #9
        lda     (ptr),y
        cmp     #' '
        bne     @file
        lda     #K_DRIVE
        sta     e_kind,x
@file:  rts

; A specification 152 refused, or NAME: no device: the destination's or a
; source's message.
badspec:
        lda     nent
        bne     :+
        lda     #<sbaddst
        ldx     #>sbaddst
        jmp     fail
:       lda     #<sbadsrc
        ldx     #>sbadsrc
        jmp     fail

badfmt: lda     #<sformat
        ldx     #>sformat
        jmp     fail

badpar: lda     #<sparam
        ldx     #>sparam
        jmp     fail

; An option list at idx ('['), into entry nent's option bytes: letters,
; packed or with blanks between, a number straight after D, G, N, P and
; T, a string to a ^Z after S and Q. Out: idx past the ']'.
options:
        ldx     nent
        jsr     optptr
        inc     idx
@next:  ldx     idx
        lda     UT_TAIL,x
        bne     :+
        jmp     badpar                  ; no ']'
:       inx
        stx     idx
        cmp     #' '
        beq     @next
        cmp     #TAB
        beq     @next
        cmp     #']'
        bne     :+
        rts
:       cmp     #'A'
        bcc     @bad
        cmp     #'Z' + 1
        bcs     @bad
        sec
        sbc     #'A'
        sta     letter
        tay
        lda     kinds,y                 ; 0 none, 1 a flag, 2 a number, 3 a string
        beq     @bad
        cmp     #1
        bne     @num
        lda     #1
        sta     (ptr),y
        cpy     #'I' - 'A'              ; I sets H
        bne     @next
        ldy     #'H' - 'A'
        sta     (ptr),y
        bra     @next
@bad:   jmp     badpar
@num:   cmp     #2
        bne     @str
        jsr     number                  ; A the number, carry clear: there was one
        ldy     letter
        bcc     @have
        cpy     #'P' - 'A'              ; P alone: 60
        beq     @p60
        cpy     #'N' - 'A'              ; N alone: N1
        bne     @bad
        lda     #1
        bra     @store
@have:  cpy     #'G' - 'A'
        bne     @notg
        cmp     #16
        bcc     :+
        lda     #<suser
        ldx     #>suser
        jmp     fail
:       inc     a                       ; G stored as the user + 1
        bra     @store
@notg:  cpy     #'N' - 'A'
        bne     @notn
        cmp     #1
        beq     @store
        cmp     #2
        beq     @store
        bra     @bad
@notn:  cpy     #'P' - 'A'
        bne     @nz
        cmp     #2
        bcs     @store
@p60:   lda     #60
        bra     @store
@nz:    cmp     #0                      ; D0, T0: refused (ours)
        beq     @bad
@store: sta     (ptr),y
        jmp     @next
@str:   lda     #1                      ; S, Q: the string to a ^Z
        sta     (ptr),y
        ldx     idx
        stx     sqat
@to:    lda     UT_TAIL,x
        beq     @bad                    ; no ^Z
        cmp     #CTLZ
        beq     @end
        inx
        bra     @to
@end:   txa
        sec
        sbc     sqat
        beq     @bad                    ; empty (ours)
        inx
        stx     idx
        ldx     nent
        ldy     letter
        cpy     #'S' - 'A'
        bne     @q
        sta     e_slen,x
        lda     sqat
        sta     e_sat,x
        jmp     @next
@q:     sta     e_qlen,x
        lda     sqat
        sta     e_qat,x
        jmp     @next

; A decimal number at idx (0-255): carry clear and A it, idx past it;
; carry set if no digit is there; over 255 BAD PARAMETER.
number: ldx     idx
        lda     UT_TAIL,x
        cmp     #'0'
        bcc     @none
        cmp     #'9' + 1
        bcs     @none
        stz     num
@dig:   lda     UT_TAIL,x
        cmp     #'0'
        bcc     @done
        cmp     #'9' + 1
        bcs     @done
        sec
        sbc     #'0'
        pha
        lda     num                     ; num * 10 + the digit
        cmp     #26
        bcs     @over
        asl     a
        sta     num
        asl     a
        asl     a
        clc
        adc     num
        sta     num
        pla
        clc
        adc     num
        bcs     @over2
        sta     num
        inx
        bra     @dig
@done:  stx     idx
        lda     num
        clc
        rts
@none:  sec
        rts
@over:  pla
@over2: jmp     badpar

; The command's forms, checked: the destination's only option G; NUL: and
; EOF: only sources, LST: and PRN: only destinations; each entry's user.
check:  ldx     #0
        jsr     optptr
        ldy     #NOPT - 1
@dopt:  cpy     #'G' - 'A'
        beq     :+
        lda     (ptr),y
        beq     :+
        jmp     badpar
:       dey
        bpl     @dopt
        lda     e_kind
        cmp     #K_DEV
        bne     @srcs
        lda     e_dev
        cmp     #D_NUL
        bcc     @srcs
        lda     #<sbaddst
        ldx     #>sbaddst
        jmp     fail
@srcs:  ldx     #1
@src:   lda     e_kind,x
        cmp     #K_DEV
        bne     @snext
        lda     e_dev,x
        cmp     #D_LST
        beq     @bad
        cmp     #D_PRN
        bne     @snext
@bad:   lda     #<sbadsrc
        ldx     #>sbadsrc
        jmp     fail
@snext: inx
        cpx     nent
        bne     @src
        ldx     #0                      ; each entry's user: its G, else PIP's
@user:  jsr     optptr
        ldy     #'G' - 'A'
        lda     (ptr),y
        beq     :+
        dec     a
        bra     @set
:       lda     user0
@set:   sta     e_user,x
        inx
        cpx     nent
        bne     @user
        lda     e_user
        sta     duser
        rts

; ---- the three forms -------------------------------------------------------

; To a drive: one source, a file (a wildcard too) or a drive (every file);
; each match copied under its own name. The same drive and user: refused.
todrive:
        lda     nent
        cmp     #2
        beq     :+
        jmp     badfmt
:       lda     e_kind + 1
        cmp     #K_DEV
        bne     :+
        jmp     badfmt
:       cmp     #K_DRIVE                ; a drive: all its files
        bne     @file
        ldx     #1
        jsr     fcbptr
        ldy     #11
        lda     #'?'
:       sta     (ptr),y
        dey
        bne     :-
        lda     #K_FILE
        sta     e_kind + 1
@file:  lda     e_fcb                   ; the same drive and user: onto itself
        jsr     realdrv
        sta     t0
        lda     e_fcb + 36
        jsr     realdrv
        cmp     t0
        bne     @differ
        lda     e_user
        cmp     e_user + 1
        bne     @differ
        jmp     badfmt
@differ:
        lda     #K_FILE                 ; each copy a file, named as its source
        sta     e_kind
        lda     #1                      ; (a drive alone: the source's password too)
        sta     ddrive
        ldx     #1
        jsr     collect                 ; the names, read first
        lda     ncount
        ora     ncount + 1
        bne     :+
        jmp     notfound
:       lda     #<(e_fcb + 37)
        ldx     #>(e_fcb + 37)
        jsr     UT_ISWILD
        stz     listing
        bcc     tdeach
        lda     opts1 + 'K' - 'A'       ; COPYING - unless K or C
        ora     opts1 + 'C' - 'A'
        bne     tdeach
        lda     #1
        sta     listing
        lda     #<scopying
        ldx     #>scopying
        jsr     UT_SAY
tdeach: stz     skipok                  ; (a file skipped comes back here)
        jsr     nextname
        bcc     :+
        rts
:       ldy     #10                     ; the source's name and the copy's
:       lda     (ptr2),y
        and     #$7F
        sta     e_fcb + 37,y
        sta     e_fcb + 1,y
        dey
        bpl     :-
        lda     opts1 + 'C' - 'A'
        beq     @noask
        lda     #<scopy
        ldx     #>scopy
        jsr     CM_PUTS
        lda     #<(e_fcb + 1)
        ldx     #>(e_fcb + 1)
        jsr     CM_FNAME
        lda     #<syn
        ldx     #>syn
        jsr     CM_PUTS
        jsr     CM_ASK
        bcc     tdeach
@noask: lda     listing
        beq     @copy
        lda     #<(e_fcb + 1)
        ldx     #>(e_fcb + 1)
        jsr     CM_FNAME
        jsr     CM_EOL
@copy:  lda     ptr2                    ; its directory bytes, for A and the attributes
        sta     curname
        lda     ptr2 + 1
        sta     curname + 1
        lda     #1
        sta     single
        tsx                             ; a source that won't open: skipped
        stx     filesp
        lda     #1
        sta     skipok
        jsr     xfer
        stz     skipok
        lda     opts1 + 'A' - 'A'
        beq     tdeach
        jsr     archive
        bra     tdeach

; A file of a copy to a drive that the BDOS wouldn't open (it said why):
; skipped, the Guide's way: the return code $FF00, the temporary file
; deleted, the next file.
skipped:
        jsr     UT_SAID
        jsr     droptmp
        ldx     filesp
        txs
        jmp     tdeach

; To a file: the sources joined, left to right; a source drive takes the
; destination's name (one source alone). A wildcard on either side:
; refused. A source the destination itself: refused.
tofile: lda     #<(e_fcb + 1)
        ldx     #>(e_fcb + 1)
        jsr     UT_ISWILD
        bcc     @dest
        ldx     #10                     ; all '?': a drive
@all:   lda     e_fcb + 1,x
        cmp     #'?'
        bne     @wild
        dex
        bpl     @all
        lda     #K_DRIVE
        sta     e_kind
        jmp     todrive
@wild:  jmp     badfmt
@dest:  ldx     #1
@src:   lda     e_kind,x
        cmp     #K_DRIVE
        bne     @notdrv
        lda     nent                    ; a drive: the destination's name, alone
        cmp     #2
        beq     :+
        jmp     badfmt
:       jsr     fcbptr
        ldy     #11
:       lda     e_fcb,y
        sta     (ptr),y
        dey
        bne     :-
        lda     #K_FILE
        sta     e_kind,x
@notdrv:
        lda     e_kind,x
        cmp     #K_FILE
        bne     @next
        jsr     fcbptr                  ; no wildcard
        phx
        lda     ptr
        clc
        adc     #1
        pha
        lda     ptr + 1
        adc     #0
        tax
        pla
        jsr     UT_ISWILD
        plx
        bcc     :+
        jmp     badfmt
:       jsr     itself                  ; not the destination itself
@next:  inx
        cpx     nent
        bne     @src
        stz     curname + 1             ; no directory bytes: names from the FCBs
        lda     nent                    ; one file source: its attributes
        cmp     #2
        bne     :+
        lda     e_kind + 1
        cmp     #K_FILE
        bne     :+
        lda     #1
        bra     @one
:       lda     #0
@one:   sta     single
        jmp     xfer

; Entry X a file of the destination's drive, user and name: refused. Keeps X.
itself: lda     e_user,x
        cmp     duser
        bne     @no
        jsr     fcbptr
        ldy     #0
        lda     (ptr),y
        jsr     realdrv
        sta     t0
        lda     e_fcb
        jsr     realdrv
        cmp     t0
        bne     @no
        ldy     #11
@name:  lda     (ptr),y
        cmp     e_fcb,y
        bne     @no
        dey
        bne     @name
        jmp     badfmt
@no:    rts

; To a device: files (no wildcard) and devices, joined.
todev:  ldx     #1
@src:   lda     e_kind,x
        cmp     #K_DRIVE
        bne     :+
        jmp     badfmt
:       cmp     #K_FILE
        bne     @next
        jsr     fcbptr
        lda     ptr
        clc
        adc     #1
        sta     t0
        lda     ptr + 1
        adc     #0
        phx
        tax
        lda     t0
        jsr     UT_ISWILD
        plx
        bcc     @next
        jmp     badfmt
@next:  inx
        cpx     nent
        bne     @src
        stz     curname + 1
        stz     single
        jmp     xfer

; A FCB drive byte (0 the current) as 1-16.
realdrv:
        cmp     #0
        bne     :+
        lda     UT_DRV0
        inc     a
:       rts

; ---- the names of a wildcard copy ------------------------------------------

; Entry X's matching files, each once (the extent-zero entries), read into
; memory past the BSS: 11 bytes each, as the directory has them (its
; attributes in bit 7). SYS files only with R. With A, only those not
; archived: every extent's entry looked at, since the BDOS clears t3' only
; in the entry of an extent written (the Guide, 2.3.4: archived if all of
; them have it), a file named once, by its first entry without t3'. Out:
; ncount, and nextname's place the first.
collect:
        stx     t1
        jsr     optptr                  ; the source's options, kept for the copies
        ldy     #NOPT - 1
:       lda     (ptr),y
        sta     opts1,y
        dey
        bpl     :-
        lda     e_user,x
        jsr     setuser
        ldx     t1                      ; (the BDOS keeps no register)
        jsr     fcbptr
        ldy     #0                      ; sfcb: its drive and pattern, extent 0
:       lda     (ptr),y
        sta     sfcb,y
        iny
        cpy     #12
        bne     :-
        lda     #0
:       sta     sfcb,y
        iny
        cpy     #36
        bne     :-
        lda     opts1 + 'A' - 'A'       ; A: every extent ('?' in EX)
        beq     :+
        lda     #'?'
        sta     sfcb + 12
:       jsr     UT_MEM
        lda     UT_MLO
        sta     nlist
        sta     nput
        lda     UT_MLO + 1
        sta     nlist + 1
        sta     nput + 1
        stz     ncount
        stz     ncount + 1
        lda     #<UT_REC
        ldx     #>UT_REC
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<sfcb
        ldx     #>sfcb
        ldy     #F_SFIRST
@search:
        jsr     BDOS
        cmp     #$FF
        bne     @found
        cpx     #0
        beq     @end
        jmp     said
@end:   jmp     @done
@found: asl     a                       ; its entry: UT_REC + A * 32
        asl     a
        asl     a
        asl     a
        asl     a
        clc
        adc     #<(UT_REC + 1)
        sta     ptr2
        lda     #>(UT_REC + 1)
        adc     #0
        sta     ptr2 + 1
        ldy     #9                      ; SYS (t2') without R: passed over
        lda     (ptr2),y
        bpl     :+
        lda     opts1 + 'R' - 'A'
        beq     @skip
:       ldy     #10                     ; archived (t3') with A: passed over
        lda     (ptr2),y
        bpl     :+
        lda     opts1 + 'A' - 'A'
        bne     @skip
:       lda     opts1 + 'A' - 'A'       ; A: a name collected already, passed over
        beq     @new
        jsr     seen
        bcs     @skip
@new:   lda     nput + 1                ; room for 11 more?
        cmp     UT_MHI + 1
        bcc     @room
        lda     nput
        clc
        adc     #11
        cmp     UT_MHI
        bcc     @room
        lda     #<smem
        ldx     #>smem
        jmp     fail
@room:  lda     nput
        sta     @dst + 1
        lda     nput + 1
        sta     @dst + 2
        ldy     #10
@copy:  lda     (ptr2),y
@dst:   sta     $FFFF,y
        dey
        bpl     @copy
        lda     nput
        clc
        adc     #11
        sta     nput
        bcc     :+
        inc     nput + 1
:       inc     ncount
        bne     @skip
        inc     ncount + 1
@skip:  lda     #<sfcb
        ldx     #>sfcb
        ldy     #F_SNEXT
        jmp     @search
@done:  lda     nlist                   ; nextname from the first
        sta     nget
        lda     nlist + 1
        sta     nget + 1
        lda     ncount
        sta     nleft
        lda     ncount + 1
        sta     nleft + 1
        rts

; Carry set if the 11 bytes at ptr2 name a file collected already (nlist to
; nput, their low 7 bits). ptr3 changed; ptr and ptr2 kept.
seen:
        lda     nlist
        sta     ptr3
        lda     nlist + 1
        sta     ptr3 + 1
@name:  lda     ptr3
        cmp     nput
        bne     @cmp
        lda     ptr3 + 1
        cmp     nput + 1
        bne     @cmp
        clc
        rts
@cmp:   ldy     #10
@byte:  lda     (ptr3),y
        eor     (ptr2),y
        and     #$7F
        bne     @next
        dey
        bpl     @byte
        sec
        rts
@next:  lda     ptr3
        clc
        adc     #11
        sta     ptr3
        bcc     @name
        inc     ptr3 + 1
        bra     @name

; The next name collected: carry clear and ptr2 at its 11 bytes; carry set
; when none is left.
nextname:
        lda     nleft
        ora     nleft + 1
        bne     :+
        sec
        rts
:       lda     nleft
        bne     :+
        dec     nleft + 1
:       dec     nleft
        lda     nget
        sta     ptr2
        lda     nget + 1
        sta     ptr2 + 1
        lda     nget
        clc
        adc     #11
        sta     nget
        bcc     :+
        inc     nget + 1
:       clc
        rts

; A: the source's archive bit set (t3'), its other attributes as found.
archive:
        lda     e_user + 1
        jsr     setuser
        lda     e_fcb + 36
        sta     wfcb
        ldy     #10
        lda     curname
        sta     ptr2
        lda     curname + 1
        sta     ptr2 + 1
:       lda     (ptr2),y
        sta     wfcb + 1,y
        dey
        bpl     :-
        lda     wfcb + 11
        ora     #$80
        sta     wfcb + 11
        ldx     #1
        jsr     srcpw
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_ATTRIB
        jsr     BDOS
        cmp     #$FF
        bne     :+
        jmp     said
:       rts

; Entry X's password (as 152 left it, kept in e_pw) to the DMA buffer, as
; the BDOS looks for it (UT_PWSET). Keeps nothing.
srcpw:  lda     pwlo,x
        pha
        lda     pwhi,x
        tax
        pla
        jmp     UT_PWSET

; Not found: the source's name.
notfound:
        lda     #<(e_fcb + 37)
        sta     ptr
        lda     #>(e_fcb + 37)
        sta     ptr + 1
        lda     #<snotfnd
        ldx     #>snotfnd
        jmp     failnm

; The user A (0-15) the current one. Keeps nothing.
setuser:
        ldy     #F_USERNUM
        jmp     BDOS

; ---- the transfer ----------------------------------------------------------

; The command's sources to its destination. By characters if a source has
; an option that asks (not A, C, G, K, O, R, V or W) or a device is either
; side; else by records. A file destination is written to NAME.$$$ and
; renamed at the end.
xfer:   stz     wantv                   ; V on any source
        ldx     #1
@v:     jsr     optptr
        ldy     #'V' - 'A'
        lda     (ptr),y
        beq     :+
        sta     wantv
:       inx
        cpx     nent
        bne     @v
        stz     charmode
        lda     e_kind
        cmp     #K_DEV
        beq     @char
        ldx     #1
@src:   lda     e_kind,x
        cmp     #K_DEV
        beq     @char
        jsr     optptr
        ldy     #NOPT - 1
@opt:   lda     (ptr),y
        beq     :+
        lda     forces,y
        bne     @char
:       dey
        bpl     @opt
        inx
        cpx     nent
        bne     @src
        bra     @mode
@char:  lda     #1
        sta     charmode
@mode:  stz     opos
        stz     col
        stz     lines
        stz     lineno
        stz     lineno + 1
        stz     lineno + 2
        lda     #1
        sta     linestart
        lda     #$FF
        sta     crc
        sta     crc + 1
        lda     e_kind
        cmp     #K_FILE
        bne     :+
        jsr     maketmp
:       ldx     #1
        stx     cur
@each:  jsr     onesrc
        inc     cur
        ldx     cur
        cpx     nent
        bne     @each
        lda     e_kind
        cmp     #K_FILE
        bne     @end
        lda     charmode                ; by characters: a ^Z after the last
        beq     :+
        lda     #CTLZ
        jsr     outraw
:       jsr     flush
        jmp     finish
@end:   rts

; Source cur, its options in force, through the pipeline.
onesrc: ldx     cur
        jsr     optptr
        ldy     #NOPT - 1
:       lda     (ptr),y
        sta     opts,y
        dey
        bpl     :-
        lda     e_kind                  ; PRN: T8, N and P60 on each source
        cmp     #K_DEV
        bne     @prn
        lda     e_dev
        cmp     #D_PRN
        bne     @prn
        lda     opts + 'T' - 'A'
        bne     :+
        lda     #8
        sta     opts + 'T' - 'A'
:       lda     opts + 'N' - 'A'
        bne     :+
        lda     #1
        sta     opts + 'N' - 'A'
:       lda     opts + 'P' - 'A'
        bne     @prn
        lda     #60
        sta     opts + 'P' - 'A'
@prn:   stz     sm                      ; S and Q: nothing matched yet
        stz     qm
        stz     qdone
        lda     opts + 'S' - 'A'
        sta     spend
        stz     hseen
        stz     hlen
        lda     opts + 'P' - 'A'        ; P: a form feed first
        beq     :+
        stz     lines
        lda     #FFEED
        jsr     emit
:       ldx     cur
        lda     e_kind,x
        cmp     #K_DEV
        bne     @file
        jmp     devsrc
@file:  jsr     opensrc
        lda     charmode
        bne     @chars
        jmp     records
@chars: jsr     getrec                  ; by characters, to a ^Z (O: the end)
        bcs     @eof
        ldx     #0
@c:     lda     ibuf,x
        cmp     #CTLZ
        bne     :+
        ldy     opts + 'O' - 'A'
        beq     @eof
:       phx
        jsr     filter
        plx
        lda     qdone
        bne     @eof
        inx
        cpx     #128
        bne     @c
        jsr     chkbrk
        bra     @chars
@eof:   jmp     srcend

; A device source: CON: (1, ^Z ends it), AUX: (3, the same), NUL: 64
; zeros, EOF: a ^Z.
devsrc: lda     e_dev,x
        cmp     #D_NUL
        bne     @eofd
        ldx     #64
:       phx
        lda     #0
        jsr     filter
        plx
        dex
        bne     :-
        jmp     srcend
@eofd:  cmp     #D_EOF
        bne     @in
        lda     #CTLZ
        jsr     filter
        jmp     srcend
@in:    ldy     #C_READ
        cmp     #D_CON
        beq     :+
        ldy     #A_READ
:       sty     @fn + 1
@read:  jsr     CM_FLUSH
@fn:    ldy     #C_READ
        jsr     BDOS
        cmp     #CTLZ
        beq     @done
        jsr     filter
        lda     qdone
        beq     @read
@done:  jmp     srcend

; A source's end: S never found, Q never found; H's last line and its end.
srcend: lda     opts + 'H' - 'A'
        beq     @s
        lda     hlen
        beq     :+
        jsr     hexline
:       lda     opts + 'I' - 'A'
        bne     @s
        lda     hseen
        bne     @s
        lda     #<shexend
        ldx     #>shexend
        jmp     failsrc
@s:     lda     spend
        beq     @q
        lda     #<sstart
        ldx     #>sstart
        jmp     fail
@q:     lda     opts + 'Q' - 'A'
        beq     @ok
        lda     qdone
        bne     @ok
        lda     #<squit
        ldx     #>squit
        jmp     fail
@ok:    rts

; A failure naming the source cur.
failsrc:
        pha
        phx
        ldx     cur
        jsr     fcbptr
        inc     ptr
        bne     :+
        inc     ptr + 1
:       plx
        pla
        jmp     failnm

; Source cur opened: its user, its password; FILE NOT FOUND, or the BDOS's
; error. SYS files only with R (ours: a name given, as a wildcard).
opensrc:
        ldx     cur
        lda     e_user,x
        jsr     setuser
        lda     curname + 1             ; a name of a wildcard copy: found already
        bne     @open
        ldx     cur
        jsr     collect1
@open:  ldx     cur
        jsr     srcpw
        ldx     cur
        jsr     fcbptr
        ldy     #12
        lda     #0
:       sta     (ptr),y
        iny
        cpy     #36
        bne     :-
        lda     ptr
        ldx     ptr + 1
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     @ok
        cpx     #0
        bne     :+
        lda     #<snotfnd
        ldx     #>snotfnd
        jmp     failsrc
:       lda     skipok
        beq     :+
        jmp     skipped
:       jmp     said
@ok:    stz     ieof
        lda     #<ibuf2                 ; the first record read ahead
        sta     next
        lda     #>ibuf2
        sta     next + 1
        jmp     readnext

; A named source (not a wildcard) found, as collect finds: SYS only with R.
collect1:
        jsr     collect
        lda     ncount
        ora     ncount + 1
        bne     :+
        lda     #<snotfnd
        ldx     #>snotfnd
        jmp     failsrc
:       lda     ncount                  ; its attributes, for a single copy
        bne     :+
        rts
:       lda     nlist
        sta     ptr2
        lda     nlist + 1
        sta     ptr2 + 1
        rts

; The source's next record, read ahead into ibuf2: ieof set at its end.
readnext:
        lda     #<ibuf2
        ldx     #>ibuf2
        ldy     #F_DMAOFF
        jsr     BDOS
        ldx     cur
        jsr     fcbptr
        lda     ptr
        ldx     ptr + 1
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        beq     @ok
        cmp     #1
        beq     @end
        jmp     said
@end:   lda     #1
        sta     ieof
@ok:    rts

; The next record into ibuf: carry set at the end. lastrec set if it is
; the last.
getrec: lda     ieof
        beq     :+
        sec
        rts
:       ldx     #127
:       lda     ibuf2,x
        sta     ibuf,x
        dex
        bpl     :-
        jsr     readnext
        lda     ieof
        sta     lastrec
        clc
        rts

; By records: each record as it is; joined, a text source's last cut at its
; first ^Z (O: not), so the next follows on.
records:
        jsr     getrec
        bcs     @done
        ldx     #0
@byte:  lda     ibuf,x
        cmp     #CTLZ
        bne     @put
        lda     lastrec                 ; a ^Z in the last record of a joined text
        beq     @put1
        lda     opts + 'O' - 'A'
        bne     @put1
        lda     nent
        cmp     #3
        bcc     @put1
        bra     @done
@put1:  lda     #CTLZ
@put:   phx
        jsr     outraw
        plx
        inx
        cpx     #128
        bne     @byte
        jsr     chkbrk
        bra     records
@done:  rts

; ^C between records or 128 characters: USER ABORTED. In console mode bit 0
; (set at the start) 11 reports only a ^C and drops other keys typed.
chkbrk: ldy     #C_STAT
        jsr     BDOS
        cmp     #0
        beq     @no
        lda     #$FF
        ldy     #C_RAWIO
        jsr     BDOS
        cmp     #CMC_CTLC
        bne     @no
        lda     #<sabort
        ldx     #>sabort
        jmp     fail
@no:    rts

; ---- the character pipeline ------------------------------------------------

; A source's character: Z, then S (nothing written until its string has
; come, then the string), then H's lines or F, L and U, then Q.
filter: ldy     opts + 'Z' - 'A'
        beq     :+
        and     #$7F
:       sta     ch
        lda     spend
        beq     @on
        ldx     cur                     ; matching S
        lda     e_sat,x
        clc
        adc     sm
        tay
        lda     UT_TAIL,y
        cmp     ch
        bne     @miss
        inc     sm
        lda     sm
        cmp     e_slen,x
        bne     @rts
        stz     spend                   ; found: the string written
        ldy     #0
@sout:  phy
        ldx     cur
        tya
        clc
        adc     e_sat,x
        tax
        lda     UT_TAIL,x
        jsr     filter2
        ply
        iny
        ldx     cur
        tya
        cmp     e_slen,x
        bne     @sout
@rts:   rts
@miss:  stz     sm                      ; begun again (its first character?)
        ldy     e_sat,x
        lda     UT_TAIL,y
        cmp     ch
        bne     @rts
        lda     #1
        sta     sm
        rts
@on:    lda     ch
        jsr     filter2
        lda     opts + 'Q' - 'A'        ; Q: the copy ends after its string
        beq     @rts
        ldx     cur
        lda     e_qat,x
        clc
        adc     qm
        tay
        lda     UT_TAIL,y
        cmp     ch
        bne     @qmiss
        inc     qm
        lda     qm
        cmp     e_qlen,x
        bne     @rts
        lda     #1
        sta     qdone
        rts
@qmiss: stz     qm
        ldy     e_qat,x
        lda     UT_TAIL,y
        cmp     ch
        bne     @rts
        lda     #1
        sta     qm
        rts

; H: a line gathered and checked; else on to filter3.
filter2:
        ldy     opts + 'H' - 'A'
        bne     hexch
        jmp     filter3

hexch:  cmp     #LF
        beq     @line
        cmp     #CR
        beq     @rts
        ldx     hlen
        cpx     #80
        bcc     :+
        lda     #<shexlong
        ldx     #>shexlong
        jmp     failsrc
:       sta     hbuf,x
        inc     hlen
@rts:   rts
@line:  jmp     hexline

; A hex line checked (':', pairs of hex digits, a length that matches, a
; sum of 0), then written with CR LF; I drops a :00 record. An empty line
; is written as it is.
hexline:
        lda     hlen
        bne     :+
        jmp     @write
:       lda     hbuf
        cmp     #':'
        bne     @digit
        lda     hlen                    ; ':' and pairs: an odd length
        lsr     a
        bcc     @digit
        ldx     #1
        stz     hsum
        stz     hcount
@pair:  cpx     hlen
        beq     @sum
        lda     hbuf,x
        jsr     hexval
        bcs     @digit
        asl     a
        asl     a
        asl     a
        asl     a
        sta     t0
        inx
        lda     hbuf,x
        jsr     hexval
        bcs     @digit
        ora     t0
        ldy     hcount
        bne     :+
        sta     hll                     ; the first: the record's length
:       clc
        adc     hsum
        sta     hsum
        inc     hcount
        inx
        bra     @pair
@sum:   lda     hsum
        bne     @check
        lda     hll                     ; the length and the count agree
        clc
        adc     #5
        cmp     hcount
        bne     @check
        lda     hll
        bne     @write
        lda     #1                      ; :00: the end record
        sta     hseen
        lda     opts + 'I' - 'A'
        beq     @write
        stz     hlen                    ; I: dropped
        rts
@digit: lda     #<shexdig
        ldx     #>shexdig
        jmp     failsrc
@check: lda     #<shexsum
        ldx     #>shexsum
        jmp     failsrc
@write: ldx     #0
@w:     cpx     hlen
        beq     @eol
        lda     hbuf,x
        phx
        jsr     filter3
        plx
        inx
        bra     @w
@eol:   stz     hlen
        lda     #CR
        jsr     filter3
        lda     #LF
        jmp     filter3

; A a hex digit's value: carry clear; carry set if it isn't one.
hexval: cmp     #'0'
        bcc     @no
        cmp     #'9' + 1
        bcc     @dec
        cmp     #'A'
        bcc     @no
        cmp     #'F' + 1
        bcs     @no
        sbc     #'A' - 10 - 1           ; carry clear here
        clc
        rts
@dec:   sbc     #'0' - 1                ; carry clear here
        clc
        rts
@no:    sec
        rts

; F, L, U, then the line's work.
filter3:
        cmp     #FFEED
        bne     :+
        ldy     opts + 'F' - 'A'
        beq     :+
        rts
:       ldy     opts + 'L' - 'A'
        beq     :+
        cmp     #'A'
        bcc     :+
        cmp     #'Z' + 1
        bcs     :+
        ora     #$20
:       ldy     opts + 'U' - 'A'
        beq     put
        cmp     #'a'
        bcc     put
        cmp     #'z' + 1
        bcs     put
        and     #$DF
        ; falls through
; A character written: N's number at a line's start, T's tabs, D's cut, P's
; pages; CR and LF end the column, LF the line.
put:    cmp     #LF
        bne     @notlf
        jsr     emit
        lda     #1
        sta     linestart
        stz     col
        lda     opts + 'P' - 'A'
        beq     @rts
        inc     lines
        lda     lines
        cmp     opts + 'P' - 'A'
        bcc     @rts
        stz     lines
        lda     #FFEED
        jmp     emit
@rts:   rts
@notlf: cmp     #CR
        bne     @notcr
        stz     col
        jmp     emit
@notcr: pha
        lda     linestart
        beq     @mid
        stz     linestart
        lda     opts + 'N' - 'A'
        beq     @mid
        jsr     linenum
@mid:   pla
        ; falls through
; A character in a line: a tab expanded (T), past column D dropped.
putcol: cmp     #TAB
        bne     @one
        ldy     opts + 'T' - 'A'
        beq     @one
@tab:   lda     #' '                    ; to the next column divisible by T
        jsr     @one
        lda     col
        ldy     opts + 'T' - 'A'
@mod:   sec                             ; col mod T
        sbc     opts + 'T' - 'A'
        bcs     @mod
        adc     opts + 'T' - 'A'
        bne     @tab
        rts
@one:   ldy     opts + 'D' - 'A'
        beq     @emit
        ldy     col
        cpy     opts + 'D' - 'A'
        bcc     @emit
        inc     col                     ; past D: dropped
        rts
@emit:  inc     col
        jmp     emit

; N: the line's number, "     1: " (N1) or "000001" and a tab (N2).
linenum:
        inc     lineno
        bne     :+
        inc     lineno + 1
        bne     :+
        inc     lineno + 2
:       lda     #<lineno
        ldx     #>lineno
        jsr     UT_LDN
        lda     #<numbuf                ; the digits, right to left
        sta     ptr2
        lda     #>numbuf
        sta     ptr2 + 1
        ldy     #5
@dig:   phy
        lda     #10
        jsr     UT_DIVN
        ply
        clc
        adc     #'0'
        sta     numbuf,y
        dey
        bpl     @dig
        ldy     #0                      ; N1: leading zeros as blanks
        lda     opts + 'N' - 'A'
        cmp     #2
        beq     @out
@lead:  cpy     #5
        beq     @out
        lda     numbuf,y
        cmp     #'0'
        bne     @out
        lda     #' '
        sta     numbuf,y
        iny
        bra     @lead
@out:   ldy     #0
@o:     phy
        lda     numbuf,y
        jsr     putcol
        ply
        iny
        cpy     #6
        bne     @o
        lda     opts + 'N' - 'A'
        cmp     #2
        beq     @tabn
        lda     #':'
        jsr     putcol
        lda     #' '
        jmp     putcol
@tabn:  lda     #TAB
        jmp     putcol

; A character to the destination, and E's echo of it to the console.
emit:   pha
        jsr     outraw
        pla
        ldy     opts + 'E' - 'A'
        beq     :+
        ldy     e_kind                  ; not twice to the console
        cpy     #K_DEV
        bne     @echo
        ldy     e_dev
        cpy     #D_CON
        beq     :+
@echo:  jmp     CM_PUTC
:       rts

; ---- the destination -------------------------------------------------------

; A byte to the destination: a device's function, or the record buffer.
outraw: ldy     e_kind
        cpy     #K_DEV
        bne     @file
        ldy     e_dev
        cpy     #D_CON
        bne     :+
        jmp     CM_PUTC
:       cpy     #D_AUX
        bne     :+
        ldy     #A_WRITE
        bra     @fn
:       ldy     #L_WRITE                ; LST:, PRN:
@fn:    jmp     BDOS
@file:  ldx     opos
        sta     obuf,x
        inx
        stx     opos
        cpx     #128
        bne     :+
        jmp     wrec
:       rts

; The record buffer, its rest ^Zs, written.
flush:  ldx     opos
        beq     @none
        lda     #CTLZ
:       sta     obuf,x
        inx
        cpx     #128
        bne     :-
        jmp     wrec
@none:  rts

; The record written to NAME.$$$ (a full disk: DISK WRITE ERROR), its CRC
; taken for V.
wrec:   stz     opos
        lda     duser
        jsr     setuser
        lda     #<obuf
        ldx     #>obuf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_WRITE
        jsr     BDOS
        cmp     #0
        beq     :+
        lda     #<(e_fcb + 1)
        sta     ptr
        lda     #>(e_fcb + 1)
        sta     ptr + 1
        lda     #<swrite
        ldx     #>swrite
        jmp     failnm
:       lda     #<obuf
        ldx     #>obuf
        jsr     crcrec
        ldx     cur                     ; back to the source's user
        lda     e_user,x
        jmp     setuser

; The CRC (CCITT, $1021) of the 128 bytes at A/X into crc.
crcrec: sta     @src + 1
        stx     @src + 2
        ldx     #0
@byte:
@src:   lda     $FFFF,x
        eor     crc + 1
        sta     crc + 1
        ldy     #8
@bit:   asl     crc
        rol     crc + 1
        bcc     :+
        lda     crc
        eor     #$21
        sta     crc
        lda     crc + 1
        eor     #$10
        sta     crc + 1
:       dey
        bne     @bit
        inx
        cpx     #128
        bne     @byte
        rts

; NAME.$$$ made on the destination's drive and user: one there deleted
; first (read-only: CAN'T DELETE TEMP FILE); a read-only drive R/O DISK.
maketmp:
        lda     e_fcb
        jsr     realdrv
        dec     a
        jsr     UT_ISRO
        bcc     :+
        lda     #<srodisk
        ldx     #>srodisk
        jmp     fail
:       lda     duser
        jsr     setuser
        ldy     #35
        lda     #0
:       sta     tfcb,y
        dey
        bpl     :-
        ldy     #8
:       lda     e_fcb,y
        sta     tfcb,y
        dey
        bpl     :-
        lda     #'$'
        sta     tfcb + 9
        sta     tfcb + 10
        sta     tfcb + 11
        jsr     tmpdma
        lda     #<tfcb                  ; one there already: read-only?
        ldx     #>tfcb
        ldy     #F_SFIRST
        jsr     BDOS
        cmp     #$FF
        beq     @none
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        lda     UT_REC + 9,x
        bpl     @del
        lda     #<stemp
        ldx     #>stemp
        jmp     fail
@del:   lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
@none:  ldy     #12
        lda     #0
:       sta     tfcb,y
        iny
        cpy     #36
        bne     :-
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_MAKE
        jsr     BDOS
        cmp     #$FF
        bne     @made
        cpx     #0                      ; X 0: no directory space, the BDOS silent
        beq     :+
        jmp     said
:       lda     #<(e_fcb + 1)
        sta     ptr
        lda     #>(e_fcb + 1)
        sta     ptr + 1
        lda     #<swrite
        ldx     #>swrite
        jmp     failnm
@made:  lda     #1
        sta     tmpopen
        rts

; The DMA address UT_REC (searches; the password, none, for the temporary
; file).
tmpdma: lda     #<UT_REC
        ldx     #>UT_REC
        ldy     #F_DMAOFF
        jmp     BDOS

; The end of a file destination: NAME.$$$ closed, V's check, the old file
; deleted (read-only: asked, unless W), NAME.$$$ renamed, a single copy's
; attributes, the password.
finish: lda     duser
        jsr     setuser
        jsr     tmpdma
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_CLOSE
        jsr     BDOS
        cmp     #$FF
        bne     :+
        jmp     said
:       lda     wantv                   ; V on any source
        beq     @old
        jsr     verify
@old:   ldy     #0                      ; wfcb: the destination's name
:       lda     e_fcb,y
        sta     wfcb,y
        iny
        cpy     #12
        bne     :-
        lda     #0
:       sta     wfcb,y
        iny
        cpy     #36
        bne     :-
        jsr     tmpdma
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_SFIRST
        jsr     BDOS
        cmp     #$FF
        beq     @rename
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        lda     UT_REC + 9,x            ; read-only?
        bpl     @delete
        jsr     wanted                  ; W on any source: no question
        bne     @rwoff
        lda     #<sdestro
        ldx     #>sdestro
        jsr     CM_PUTS
        jsr     CM_ASK
        bcs     @rwoff
        lda     #<snotdel
        ldx     #>snotdel
        jmp     fail
@rwoff: ldx     #0                      ; its attributes off, its password given
        jsr     srcpw
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_ATTRIB
        jsr     BDOS
        cmp     #$FF
        bne     @delete
        jmp     said
@delete:
        ldx     #0
        jsr     srcpw
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_DELETE
        jsr     BDOS
        cmp     #$FF
        bne     @rename
        jmp     said
@rename:
        ldx     #10                     ; tfcb: NAME.$$$, the new name at 16
:       lda     e_fcb + 1,x
        sta     tfcb + 17,x
        dex
        bpl     :-
        lda     tfcb
        sta     tfcb + 16
        jsr     tmpdma
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_RENAME
        jsr     BDOS
        stz     tmpopen
        cmp     #$FF
        bne     :+
        jmp     said
:       jsr     attrs
        jmp     passwd

; carry clear and Z clear if a source has W.
wanted: ldx     #1
@e:     jsr     optptr
        ldy     #'W' - 'A'
        lda     (ptr),y
        bne     @yes
        inx
        cpx     nent
        bne     @e
        lda     #0
@yes:   rts

; A single copy: the source's RO, SYS and F1-F4 on the copy, its archive
; bit clear (ours). The source's bits: the directory's (a wildcard copy),
; else the FCB its open filled in.
attrs:  lda     single
        bne     :+
        rts
:       lda     curname + 1
        beq     @fcb
        lda     curname
        sta     ptr2
        lda     curname + 1
        sta     ptr2 + 1
        bra     @bits
@fcb:   lda     #<(e_fcb + 37)
        sta     ptr2
        lda     #>(e_fcb + 37)
        sta     ptr2 + 1
@bits:  lda     #0                      ; any to set?
        ldy     #0
:       ora     (ptr2),y
        iny
        cpy     #4
        bne     :-
        ldy     #8
        ora     (ptr2),y
        iny
        ora     (ptr2),y
        and     #$80
        beq     @none
        ldy     #0                      ; wfcb: the copy's name, the bits
:       lda     e_fcb,y
        sta     wfcb,y
        iny
        cpy     #12
        bne     :-
        ldy     #3
:       lda     (ptr2),y
        and     #$80
        ora     wfcb + 1,y
        sta     wfcb + 1,y
        dey
        bpl     :-
        ldy     #8
        lda     (ptr2),y
        and     #$80
        ora     wfcb + 9
        sta     wfcb + 9
        iny
        lda     (ptr2),y
        and     #$80
        ora     wfcb + 10
        sta     wfcb + 10
        jsr     tmpdma
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_ATTRIB
        jsr     BDOS
        cmp     #$FF
        bne     @none
        jmp     said
@none:  rts

; A destination password: given, in READ mode; a drive's copy, the source's
; password and mode when it was given. Passwords off on the drive: none
; (ours, as the BDOS refuses it).
passwd: lda     e_pw
        cmp     #' '
        beq     @src
        lda     #$80                    ; READ
        sta     pwmode
        ldy     #7
:       lda     e_pw,y
        sta     newpw,y
        dey
        bpl     :-
        bra     @set
@src:   lda     ddrive                  ; d: alone: the source's, if given
        beq     @none
        lda     single
        beq     @none
        lda     e_pw + 8
        cmp     #' '
        beq     @none
        lda     e_user + 1              ; its mode (102)
        jsr     setuser
        ldx     #1
        jsr     srcpw
        ldx     #1
        jsr     fcbptr
        lda     ptr
        ldx     ptr + 1
        ldy     #F_TIMEDATE
        jsr     BDOS
        lda     duser
        jsr     setuser
        lda     e_fcb + 36 + 12
        and     #$E0
        beq     @none
        sta     pwmode
        ldy     #7
:       lda     e_pw + 8,y
        sta     newpw,y
        dey
        bpl     :-
@set:   ldy     #0                      ; wfcb: the copy, the mode and bit 0
:       lda     e_fcb,y
        sta     wfcb,y
        iny
        cpy     #12
        bne     :-
        lda     pwmode
        ora     #1
        sta     wfcb + 12
        lda     #0
        ldy     #13
:       sta     wfcb,y
        iny
        cpy     #36
        bne     :-
        ldy     #7                      ; DMA 0-7 the old (none), 8-15 the new
:       lda     #' '
        sta     UT_REC,y
        lda     newpw,y
        sta     UT_REC + 8,y
        dey
        bpl     :-
        jsr     tmpdma
        lda     #<wfcb
        ldx     #>wfcb
        ldy     #F_WRITEXFCB
        jsr     BDOS
@none:  rts

; V: NAME.$$$ read back, its CRC against the one taken as it was written.
verify: lda     crc
        sta     wcrc
        lda     crc + 1
        sta     wcrc + 1
        lda     #$FF
        sta     crc
        sta     crc + 1
        ldy     #12
        lda     #0
:       sta     tfcb,y
        iny
        cpy     #36
        bne     :-
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     @read
        jmp     said
@read:  lda     #<ibuf
        ldx     #>ibuf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        bne     @end
        lda     #<ibuf
        ldx     #>ibuf
        jsr     crcrec
        bra     @read
@end:   lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_CLOSE
        jsr     BDOS
        lda     crc
        cmp     wcrc
        bne     @bad
        lda     crc + 1
        cmp     wcrc + 1
        bne     @bad
        rts
@bad:   lda     #<(e_fcb + 1)
        sta     ptr
        lda     #>(e_fcb + 1)
        sta     ptr + 1
        lda     #<sverify
        ldx     #>sverify
        jmp     failnm

        .include "cmd.inc"
        .include "util.inc"

        .rodata
sbanner: .byte  "OODOS PIP 0.2$"
sstar:  .byte   "*$"
scopying: .byte "COPYING -$"
scopy:  .byte   "COPY $"
syn:    .byte   " (Y/N)?$"
sformat: .byte  "INVALID FORMAT$"
ssep:   .byte   "INVALID SEPARATOR$"
sparam: .byte   "BAD PARAMETER$"
suser:  .byte   "INVALID USER NUMBER$"
sbaddst: .byte  "INVALID DESTINATION$"
sbadsrc: .byte  "INVALID SOURCE$"
snotfnd: .byte  "FILE NOT FOUND - $"
srodisk: .byte  "R/O DISK$"
stemp:  .byte   "CAN'T DELETE TEMP FILE$"
sdestro: .byte  "DESTINATION IS R/O, DELETE (Y/N)?$"
snotdel: .byte  "**NOT DELETED**$"
swrite: .byte   "DISK WRITE ERROR - $"
sverify: .byte  "VERIFY ERROR - $"
sabort: .byte   "USER ABORTED$"
sstart: .byte   "START NOT FOUND$"
squit:  .byte   "QUIT NOT FOUND$"
shexdig: .byte  "INVALID DIGIT - $"
shexsum: .byte  "CHECKSUM ERROR - $"
shexlong: .byte "RECORD TOO LONG - $"
shexend: .byte  "UNEXPECTED END OF HEX FILE - $"
smem:   .byte   "Not Enough Memory$"
devnames: .byte "CONAUXLSTPRNNULEOF"     ; D_CON to D_EOF, in order
; Each letter: 0 not an option, 1 a flag, 2 a number, 3 a string.
kinds:  .byte   1, 0, 1, 2, 1, 1, 2, 1, 1, 0, 1, 1, 0  ; A-M
        .byte   2, 1, 2, 3, 1, 3, 2, 1, 1, 1, 0, 0, 1  ; N-Z
; Each letter: 1 if it makes a transfer go by characters.
forces: .byte   0, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 0  ; A-M
        .byte   1, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 1  ; N-Z
fcblo:
.repeat MAXE, i
        .byte   <(e_fcb + i * 36)
.endrepeat
fcbhi:
.repeat MAXE, i
        .byte   >(e_fcb + i * 36)
.endrepeat
pwlo:
.repeat MAXE, i
        .byte   <(e_pw + i * 8)
.endrepeat
pwhi:
.repeat MAXE, i
        .byte   >(e_pw + i * 8)
.endrepeat
optlo:
.repeat MAXE, i
        .byte   <(e_opt + i * NOPT)
.endrepeat
opthi:
.repeat MAXE, i
        .byte   >(e_opt + i * NOPT)
.endrepeat

        .bss
inter:  .res    1                       ; 1: the '*' mode
user0:  .res    1                       ; the user PIP was started in
mode0:  .res    2                       ; and the console mode
cmdsp:  .res    1                       ; the stack at the command's start
lbuf:   .res    128                     ; a line read (function 10)
rl_list: .res   1                       ; readline: in an option list
rl_str: .res    1                       ;   in S's or Q's string
idx:    .res    1                       ; the tail's next character
nent:   .res    1                       ; entries: the destination, sources
tstart: .res    1                       ; an entry's text
tend:   .res    1
tdone:  .res    1                       ; its text ended at an option list
letter: .res    1
num:    .res    1
sqat:   .res    1
seg:    .res    128                     ; an entry's text, 0-ended, for 152
pfcb:   .res    4                       ; 152's: the string, the FCB
e_kind: .res    MAXE
e_dev:  .res    MAXE
e_user: .res    MAXE
e_sat:  .res    MAXE                    ; S's and Q's strings in the tail
e_slen: .res    MAXE
e_qat:  .res    MAXE
e_qlen: .res    MAXE
e_fcb:  .res    MAXE * 36
e_opt:  .res    MAXE * NOPT
e_pw:   .res    MAXE * 8                ; each entry's password, as 152 left it
opts:   .res    NOPT                    ; the current source's options
opts1:  .res    NOPT                    ; the collected source's
duser:  .res    1                       ; the destination's user
t0:     .res    1
t1:     .res    1
sfcb:   .res    36                      ; searched
wfcb:   .res    36                      ; deleted, renamed, attributes
tfcb:   .res    36                      ; NAME.$$$
nlist:  .res    2                       ; the names collected
nput:   .res    2
nget:   .res    2
ncount: .res    2
nleft:  .res    2
listing: .res   1                       ; 1: COPYING - and the names
curname: .res   2                       ; a wildcard copy's directory bytes (0 high: none)
single: .res    1                       ; 1: one file copied alone
ddrive: .res    1                       ; 1: the destination was a drive alone
skipok: .res    1                       ; 1: a source that won't open is skipped
filesp: .res    1                       ; the stack before a drive copy's file
cur:    .res    1                       ; the source being copied
charmode: .res  1
ieof:   .res    1
lastrec: .res   1
next:   .res    2
ibuf:   .res    128
ibuf2:  .res    128
obuf:   .res    128
opos:   .res    1
col:    .res    1
linestart: .res 1
lines:  .res    1
lineno: .res    3
numbuf: .res    6
ch:     .res    1
spend:  .res    1                       ; S: still to come
sm:     .res    1                       ; S's characters matched
qm:     .res    1
qdone:  .res    1
hbuf:   .res    80                      ; H's line
hlen:   .res    1
hsum:   .res    1
hcount: .res    1
hll:    .res    1
hseen:  .res    1                       ; H: the end record seen
crc:    .res    2
wcrc:   .res    2
wantv:  .res    1
pwmode: .res    1
newpw:  .res    8
tmpopen: .res   1
fname:  .res    1                       ; 1: failname after the message
failname: .res  11

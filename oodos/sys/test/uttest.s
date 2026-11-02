; UTTEST: util.inc and cmd.inc's CM_EOL (hw/oodos.md, "The utilities,
; phase 4b"), for test_oodos_utils.cpp. Run by GO with a tail whose first
; letter says what to test; its lines are separated by CM_EOL, as a
; utility's, so the last is ended by the prompt's CR LF.
;
;   O [...]     UT_OPTS's answer, "LIST n", then (one list) each option by
;               UT_OPT against OPTAB: its index or END, UNK, AMB, the word,
;               and for a value " = W" (a word) or " = L" (a list) and each
;               item, "[word #n ON|OFF|- d]" (#n UT_WDEC's, #OVR, #-; d
;               UT_WDRIVE's, -), "!" for an error. SIZE's value isn't read,
;               so UT_OPT passes over it
;   S specs     each UT_SPEC: "S start end delim drive name;password
;               {as typed} Z" (Z: bytes 12-15 and 24-35 0; else NZ), then
;               NONE, or ERR at the index of one 152 refuses
;   R           UT_READ ("Line? "), "LEN n [tail]", then as S
;   N           numbers: UT_NOUT in fields between bars, UT_DECIN, the
;               24-bit helpers
;   D           dates: DAYS's days as "n MM/DD/YY Www YYYY n" (the days
;               back by UT_TODAYS), UT_MLEN, times and stamps
;   M           UT_MATCH and UT_PWDEC on fixed bytes
;   X {d}       the directory of drive d (or the current): DRV, DPB, SFCBS,
;               LABEL and its password, each entry not free, ENTRIES n;
;               then UT_END and the current drive, DRV n
;   P n         n lines by UT_SAY, paged (CM_PAGE), the last left owed and
;               dropped; a ^C at a stop goes to CMD_BREAK: BROKE, then
;               UT_BREAK
;   F           UT_FAIL, then UT_EXIT

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = broke

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        jsr     UT_SKIP
        jsr     UT_GET
        cmp     #'O'
        bne     :+
        jmp     opts
:       cmp     #'S'
        bne     :+
        jmp     specs
:       cmp     #'R'
        bne     :+
        jmp     line
:       cmp     #'N'
        bne     :+
        jmp     numbers
:       cmp     #'D'
        bne     :+
        jmp     dates
:       cmp     #'M'
        bne     :+
        jmp     match
:       cmp     #'X'
        bne     :+
        jmp     dir
:       cmp     #'P'
        bne     :+
        jmp     paged
:       cmp     #'F'
        bne     :+
        jsr     UT_FAIL
        lda     #<sfail
        ldx     #>sfail
        jsr     UT_SAY
        jmp     UT_EXIT
:       lda     #<swhat
        ldx     #>swhat
        jsr     UT_SAY
        jmp     UT_EXIT

broke:  lda     #<sbroke
        ldx     #>sbroke
        jsr     CM_PUTS
        jmp     UT_BREAK

; ---- helpers ---------------------------------------------------------------

puts:   jmp     CM_PUTS
putc:   jmp     CM_PUTC
blank:  lda     #' '
        jmp     CM_PUTC

; A in hex.
hex2:   pha
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jsr     @nib
        pla
        and     #$0F
@nib:   cmp     #10
        bcc     :+
        adc     #'A' - '0' - 11
:       adc     #'0'
        jmp     CM_PUTC

; A in decimal.
decout: ldx     #0
        jmp     CM_DECOUT

; UT_WORD, to its 0.
word:   ldx     #0
:       lda     UT_WORD,x
        beq     :+
        jsr     CM_PUTC
        inx
        bra     :-
:       rts

; ---- O: the option list ----------------------------------------------------

opts:   lda     #<slist
        ldx     #>slist
        jsr     puts
        jsr     UT_OPTS
        pha
        jsr     decout
        jsr     CM_EOL
        pla
        cmp     #UT_LONE
        beq     @next
        jmp     UT_EXIT
@next:  lda     #<OPTAB
        ldx     #>OPTAB
        jsr     UT_OPT
        bcc     @known
        cmp     #UT_OEND
        bne     :+
        lda     #<send
        ldx     #>send
        jsr     UT_SAY
        jmp     UT_EXIT
:       ldy     #<sunk
        ldx     #>sunk
        cmp     #UT_OUNK
        beq     :+
        ldy     #<samb
        ldx     #>samb
:       tya
        jsr     puts
        jsr     wordl
        jsr     CM_EOL
        bra     @next
@known: pha
        jsr     decout
        jsr     blank
        jsr     wordl
        pla
        cmp     #11                     ; SIZE: its value not read
        beq     @eolj
        jsr     UT_VALUE
        bcc     :+
@eolj:  jmp     @eol
:
        pha
        lda     #<seq
        ldx     #>seq
        jsr     puts
        pla
        ldx     #'W'
        cmp     #0
        beq     :+
        ldx     #'L'
:       txa
        jsr     putc
@item:  jsr     UT_VITEM
        bcc     @show
        cmp     #0
        beq     @eol
        lda     #'!'
        jsr     putc
        bra     @eol
@show:  lda     #' '
        jsr     putc
        lda     #'['
        jsr     putc
        jsr     word
        lda     #<shash
        ldx     #>shash
        jsr     puts
        jsr     UT_WDEC
        bcc     @num
        lda     UT_OVR
        beq     @nonum
        lda     #<sover
        ldx     #>sover
        jsr     puts
        bra     @onoff
@nonum: lda     #'-'
        jsr     putc
        bra     @onoff
@num:   jsr     CM_DECOUT
@onoff: jsr     blank
        jsr     UT_WONOFF
        bcs     @no
        ldy     #<son
        ldx     #>son
        cmp     #1
        beq     :+
        ldy     #<soff
        ldx     #>soff
:       tya
        jsr     puts
        bra     @drive
@no:    lda     #'-'
        jsr     putc
@drive: jsr     blank
        jsr     UT_WDRIVE
        bcs     :+
        jsr     decout
        bra     @close
:       lda     #'-'
        jsr     putc
@close: lda     #']'
        jsr     putc
        jmp     @item
@eol:   jsr     CM_EOL
        jmp     @next

; UT_WORD, or LONG for one over 16.
wordl:  lda     UT_WLEN
        cmp     #$FF
        beq     :+
        jmp     word
:       lda     #<slong
        ldx     #>slong
        jmp     puts

; ---- S and R: file specifications ------------------------------------------

line:   lda     #<sline
        ldx     #>sline
        jsr     UT_READ
        pha
        lda     #<slen
        ldx     #>slen
        jsr     puts
        pla
        jsr     decout
        lda     #<sbr
        ldx     #>sbr
        jsr     puts
        ldx     #0
:       lda     UT_TAIL,x
        beq     :+
        jsr     putc
        inx
        bra     :-
:       lda     #']'
        jsr     putc
        jsr     CM_EOL
specs:  lda     #6                      ; at most 6
        sta     count
@next:  ldx     #35                     ; the FCB filled, to see it cleared
        lda     #$AA
:       sta     fcb,x
        dex
        bpl     :-
        lda     #<fcb
        ldx     #>fcb
        jsr     UT_SPEC
        bcc     @got
        cmp     #0
        bne     @err
        lda     #<snone
        ldx     #>snone
        jsr     UT_SAY
        jmp     UT_EXIT
@err:   lda     #<serr
        ldx     #>serr
        jsr     puts
        lda     UT_IDX
        jsr     decout
        jsr     CM_EOL
        jmp     UT_EXIT
@got:   lda     #'S'
        jsr     putc
        jsr     blank
        lda     UT_SPAT
        jsr     decout
        jsr     blank
        lda     UT_SEND
        jsr     decout
        jsr     blank
        lda     UT_SDELIM
        bne     :+
        lda     #'-'
:       jsr     putc
        jsr     blank
        lda     fcb
        jsr     hex2
        jsr     blank
        ldx     #1
:       lda     fcb,x
        jsr     putc
        inx
        cpx     #12
        bne     :-
        lda     #';'
        jsr     putc
        ldx     #16
:       lda     fcb,x
        jsr     putc
        inx
        cpx     #24
        bne     :-
        lda     #<sbrace
        ldx     #>sbrace
        jsr     puts
        jsr     UT_PUTSPEC
        lda     #'}'
        jsr     putc
        jsr     blank
        ldx     #12                     ; 12-15 and 24-35: 0
        lda     #0
:       ora     fcb,x
        inx
        cpx     #16
        bne     :-
        ldx     #24
:       ora     fcb,x
        inx
        cpx     #36
        bne     :-
        tax
        lda     #'Z'
        cpx     #0
        beq     :+
        lda     #<snz
        ldx     #>snz
        jsr     puts
        bra     @eol
:       jsr     putc
@eol:   jsr     CM_EOL
        dec     count
        beq     :+
        jmp     @next
:       jmp     UT_EXIT

; ---- N: numbers ------------------------------------------------------------

numbers:
        ldx     #0                      ; UT_NOUT: value (3), field, commas
@out:   stx     count
        lda     NOUTS,x
        sta     UT_N
        lda     NOUTS + 1,x
        sta     UT_N + 1
        lda     NOUTS + 2,x
        sta     UT_N + 2
        lda     #'|'
        jsr     putc
        ldx     count
        lda     NOUTS + 4,x
        lsr     a                       ; carry: commas
        lda     NOUTS + 3,x
        jsr     UT_NOUT
        lda     #'|'
        jsr     putc
        jsr     nout                   ; UT_N kept: shown again
        lda     #'|'
        jsr     putc
        jsr     CM_EOL
        lda     count
        clc
        adc     #5
        tax
        cpx     #NOUTS_LEN
        bne     @out
        ldx     #0                      ; UT_DECIN: each string
@in:    stx     count
        lda     DECINS,x
        sta     ptr
        lda     DECINS + 1,x
        sta     ptr + 1
        lda     #'D'
        jsr     putc
        jsr     blank
        lda     ptr
        ldx     ptr + 1
        ldy     #0
        jsr     UT_DECIN
        sty     savey
        bcs     @bad
        jsr     nout
        bra     @y
@bad:   lda     UT_OVR
        beq     :+
        lda     #<sover
        ldx     #>sover
        jsr     puts
        bra     @y
:       lda     #'-'
        jsr     putc
@y:     lda     #<sy
        ldx     #>sy
        jsr     puts
        lda     savey
        jsr     decout
        jsr     CM_EOL
        ldx     count
        inx
        inx
        cpx     #DECINS_LEN
        bne     @in
        ; The 24-bit helpers
        lda     #<N1                    ; UT_LDN, UT_ADDN: 70000 + 70000
        ldx     #>N1
        jsr     UT_LDN
        lda     #<N1
        ldx     #>N1
        jsr     UT_ADDN
        lda     #<sadd
        ldx     #>sadd
        jsr     shown
        lda     #<NMAX                  ; $FFFFFF + 1: carry, 0
        ldx     #>NMAX
        jsr     UT_LDN
        lda     #<NONE
        ldx     #>NONE
        jsr     UT_ADDN
        lda     #0
        rol     a
        pha
        lda     #<sadd
        ldx     #>sadd
        jsr     puts
        jsr     nout
        lda     #<scarry
        ldx     #>scarry
        jsr     puts
        pla
        jsr     decout
        jsr     CM_EOL
        lda     #<NONE                  ; UT_SHLN: 1 << 0, 1 << 23
        ldx     #>NONE
        jsr     UT_LDN
        lda     #0
        jsr     UT_SHLN
        lda     #<sshl
        ldx     #>sshl
        jsr     shown
        lda     #23
        jsr     UT_SHLN
        lda     #<sshl
        ldx     #>sshl
        jsr     shown
        ldx     #0                      ; UT_DIV8: each
@d8:    stx     count
        lda     DIV8S,x
        sta     UT_N
        lda     DIV8S + 1,x
        sta     UT_N + 1
        lda     DIV8S + 2,x
        sta     UT_N + 2
        jsr     UT_DIV8
        lda     #<sdiv8
        ldx     #>sdiv8
        jsr     shown
        lda     count
        clc
        adc     #3
        tax
        cpx     #DIV8S_LEN
        bne     @d8
        ldx     #0                      ; UT_CMPN: 70000 against each
@cmp:   stx     count
        lda     #<N1
        ldx     #>N1
        jsr     UT_LDN
        ldx     count
        lda     CMPS,x
        pha
        lda     CMPS + 1,x
        tax
        pla
        jsr     UT_CMPN
        php
        lda     #<scmp
        ldx     #>scmp
        jsr     puts
        pla
        pha
        and     #$01                    ; carry
        jsr     decout
        jsr     blank
        pla
        and     #$02                    ; Z
        lsr     a
        jsr     decout
        jsr     CM_EOL
        ldx     count
        inx
        inx
        cpx     #CMPS_LEN
        bne     @cmp
        lda     #<N1                    ; UT_DIVN: 70000 / 7, / 127
        ldx     #>N1
        jsr     UT_LDN
        lda     #7
        jsr     divn
        lda     #<N1
        ldx     #>N1
        jsr     UT_LDN
        lda     #127
        jsr     divn
        ; CM_NOEOL: a line owed, dropped
        lda     #<sdrop
        ldx     #>sdrop
        jsr     UT_SAY
        jsr     CM_NOEOL
        lda     #<sjoined
        ldx     #>sjoined
        jsr     UT_SAY
        jmp     UT_EXIT

; UT_DIVN by A, shown: "DIV q r".
divn:   jsr     UT_DIVN
        pha
        lda     #<sdiv
        ldx     #>sdiv
        jsr     puts
        jsr     nout
        jsr     blank
        pla
        jsr     decout
        jmp     CM_EOL

; The '$' string at A/X, UT_N, a line owed.
shown:  jsr     puts
        jsr     nout
        jmp     CM_EOL

; UT_N, no field, no commas.
nout:  lda     #0
        clc
        jmp     UT_NOUT

; ---- D: dates and times ----------------------------------------------------

dates:  ldx     #0
@day:   stx     count
        lda     DAYS,x
        sta     days
        lda     DAYS + 1,x
        sta     days + 1
        tax
        lda     days
        jsr     noutw
        jsr     blank
        lda     days
        ldx     days + 1
        jsr     UT_PUTDATE
        jsr     blank
        lda     days
        ldx     days + 1
        jsr     UT_PUTWDAY
        jsr     blank
        lda     days
        ldx     days + 1
        jsr     UT_TODATE
        lda     UT_YEAR
        ldx     UT_YEAR + 1
        jsr     noutw
        lda     #'-'
        jsr     putc
        lda     UT_MONTH
        jsr     decout
        lda     #'-'
        jsr     putc
        lda     UT_DAY
        jsr     decout
        jsr     blank
        jsr     UT_TODAYS
        jsr     noutw
        jsr     blank
        lda     #2                      ; February's days that year
        jsr     UT_MLEN
        jsr     decout
        jsr     CM_EOL
        ldx     count
        inx
        inx
        cpx     #DAYS_LEN
        bne     @day
        lda     #$09                    ; times
        ldx     #$05
        jsr     UT_PUTTIME
        jsr     blank
        lda     #$23
        ldx     #$59
        ldy     #$58
        jsr     UT_PUTTIMES
        jsr     CM_EOL
        lda     #'|'                    ; stamps
        jsr     putc
        lda     #<STAMP0
        ldx     #>STAMP0
        jsr     UT_PUTSTAMP
        lda     #'|'
        jsr     putc
        lda     #<STAMP1
        ldx     #>STAMP1
        jsr     UT_PUTSTAMP
        lda     #'|'
        jsr     putc
        jmp     UT_EXIT

; A/X, a word, in decimal.
noutw: sta     UT_N
        stx     UT_N + 1
        stz     UT_N + 2
        jmp     nout

; ---- M: names and passwords ------------------------------------------------

match:  ldx     #0
@pair:  stx     count
        lda     MATCHES,x               ; the pattern's FCB (its name at +1)
        pha
        lda     MATCHES + 1,x
        tax
        pla
        jsr     UT_SETPAT
        ldx     count
        lda     MATCHES + 2,x
        pha
        lda     MATCHES + 3,x
        tax
        pla
        jsr     UT_MATCH
        lda     #'0'
        adc     #0                      ; carry: 1
        jsr     putc
        ldx     count
        inx
        inx
        inx
        inx
        cpx     #MATCHES_LEN
        bne     @pair
        jsr     CM_EOL
        lda     #<PWENTRY
        ldx     #>PWENTRY
        jsr     UT_PWDEC
        lda     #'['
        jsr     putc
        ldx     #0
:       lda     UT_PW,x
        jsr     putc
        inx
        cpx     #8
        bne     :-
        lda     #']'
        jsr     putc
        jmp     UT_EXIT

; ---- X: the directory ------------------------------------------------------

dir:    jsr     UT_SKIP
        beq     @here
        sec
        sbc     #'A'
        jsr     UT_SELECT
        cmp     #0
        beq     @here
        lda     #<sselerr
        ldx     #>sselerr
        jsr     UT_SAY
        jmp     UT_EXIT
@here:  lda     #<sdrv
        ldx     #>sdrv
        jsr     puts
        ldy     #DRV_GET
        jsr     BDOS
        jsr     decout
        jsr     CM_EOL
        jsr     UT_DPB
        lda     #<sdpb
        ldx     #>sdpb
        jsr     puts
        ldx     #0
:       lda     UT_DPBC,x
        phx
        jsr     hex2
        plx
        inx
        cpx     #17
        bne     :-
        jsr     CM_EOL
        lda     #<ssfcbs
        ldx     #>ssfcbs
        jsr     puts
        jsr     UT_SFCBS
        jsr     decout
        jsr     CM_EOL
        lda     #<slabel
        ldx     #>slabel
        jsr     puts
        jsr     UT_LABEL
        bcc     :+
        lda     #<snone
        ldx     #>snone
        jsr     puts
        bra     @scan
:       ldx     #1
:       lda     UT_LBL,x
        jsr     putc
        inx
        cpx     #12
        bne     :-
        jsr     blank
        lda     UT_LBL + 12
        jsr     hex2
        jsr     blank
        lda     #'['
        jsr     putc
        lda     #<UT_LBL
        ldx     #>UT_LBL
        jsr     UT_PWDEC
        jsr     pw
        lda     #']'
        jsr     putc
@scan:  jsr     CM_EOL
        jsr     UT_SCAN
@entry: bcc     :+
        jmp     @end
:       sta     @e0 + 1
        stx     @e0 + 2
        sta     ptr
        stx     ptr + 1
@e0:    lda     $FFFF
        cmp     #$E5
        bne     :+
        jmp     @next
:
        lda     #<sent
        ldx     #>sent
        jsr     puts
        lda     UT_ENT
        ldx     UT_ENT + 1
        jsr     noutw
        jsr     blank
        lda     ptr                     ; its place in the record, against its number
        sec
        sbc     #<UT_REC
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     savey
        lda     UT_ENT
        and     #3
        cmp     savey
        beq     :+
        lda     #<sbad
        ldx     #>sbad
        jsr     puts
:       lda     ptr
        sta     @eb + 1
        lda     ptr + 1
        sta     @eb + 2
        ldy     #0
@eb:    lda     $FFFF,y
        sta     entry,y
        iny
        cpy     #32
        bne     @eb
        lda     entry
        jsr     hex2
        jsr     blank
        lda     entry
        cmp     #$21                    ; an SFCB: no name
        beq     @eol
        lda     #<(entry + 1)
        ldx     #>(entry + 1)
        jsr     CM_DIRNM
        lda     entry
        cmp     #16                     ; an XFCB: its password
        bcc     @eol
        cmp     #32
        bcs     @eol
        jsr     blank
        lda     #'['
        jsr     putc
        lda     #<entry
        ldx     #>entry
        jsr     UT_PWDEC
        jsr     pw
        lda     #']'
        jsr     putc
@eol:   jsr     CM_EOL
@next:  jsr     UT_SNEXT
        jmp     @entry
@end:   phx
        lda     #<sents
        ldx     #>sents
        jsr     puts
        lda     UT_ENT
        ldx     UT_ENT + 1
        jsr     noutw
        jsr     blank
        pla
        jsr     decout
        jsr     CM_EOL
        jsr     CM_FLUSH                ; (UT_END drops a line owed)
        jsr     UT_END
        lda     #<sdrv
        ldx     #>sdrv
        jsr     puts
        ldy     #DRV_GET
        jsr     BDOS
        jsr     decout
        jmp     WBOOT

; UT_PW's 8 characters.
pw:     ldx     #0
:       lda     UT_PW,x
        jsr     putc
        inx
        cpx     #8
        bne     :-
        rts

; ---- P: paged lines --------------------------------------------------------

paged:  jsr     UT_SKIP
        lda     #<UT_TAIL
        ldx     #>UT_TAIL
        ldy     UT_IDX
        jsr     CM_DECIN
        sta     count
        jsr     CM_PAGE
        lda     #1
        sta     savey
@line:  lda     #<sline2
        ldx     #>sline2
        jsr     puts
        lda     savey
        jsr     decout
        jsr     CM_EOL
        lda     savey
        cmp     count
        beq     @done
        inc     savey
        bra     @line
@done:  jmp     UT_EXIT

        .include "cmd.inc"
        .include "util.inc"

        .rodata
; UT_OPT's table: DIR 0, DIRS 1, DISK 2, DRIVE 3, DATE 4, FULL 5, NOPAGE 6,
; PAGE 7, NOSORT 8, LENGTH 9, USER 10, SIZE 11
OPTAB:  .byte   "DIR", 0, "DIRS", 0, "DISK", 0, "DRIVE", 0, "DATE", 0, "FULL", 0
        .byte   "NOPAGE", 0, "PAGE", 0, "NOSORT", 0, "LENGTH", 0, "USER", 0, "SIZE", 0, 0
; UT_NOUT's cases: the value, the field, commas
NOUTS:  .faraddr 0
        .byte   0, 0
        .faraddr 0
        .byte   6, 0
        .faraddr 7
        .byte   1, 1
        .faraddr 999
        .byte   0, 1
        .faraddr 1000
        .byte   0, 1
        .faraddr 1000
        .byte   0, 0
        .faraddr 9488
        .byte   9, 1
        .faraddr 65536
        .byte   10, 1
        .faraddr 123456
        .byte   6, 1
        .faraddr 1234567
        .byte   0, 1
        .faraddr $FFFFFF
        .byte   0, 1
        .faraddr $FFFFFF
        .byte   12, 0
        .faraddr $FFFFFF
        .byte   5, 0
NOUTS_LEN = * - NOUTS
DECINS: .addr   d0, d1, d2, d3, d4, d5, d6
DECINS_LEN = * - DECINS
d0:     .byte   "0", 0
d1:     .byte   "65535", 0
d2:     .byte   "65536", 0
d3:     .byte   "123X", 0
d4:     .byte   "X", 0
d5:     .byte   "99999999", 0
d6:     .byte   "00042 ", 0
N1:     .faraddr 70000
NMAX:   .faraddr $FFFFFF
NONE:   .faraddr 1
DIV8S:  .faraddr 0, 1, 8, 9, 16, $FFFFF8, $FFFFFF
DIV8S_LEN = * - DIV8S
CMPS:   .addr   N1, NONE, NMAX
CMPS_LEN = * - CMPS
; Days: 1 January 1978; the end of 1978; 1979; 29 February and 1 March 1980;
; 2000's 29 February; 31 October 2026; 31 December 2099; 28 and 29 February
; and 1 March 2100; 31 December 2100; 65535
DAYS:   .word   1, 365, 366, 790, 791, 8095, 17836, 44560, 44619, 44620, 44621, 44926, 65535
DAYS_LEN = * - DAYS
STAMP0: .byte   0, 0, $12, $34
STAMP1: .word   17835
        .byte   $12, $34
; UT_MATCH's pairs: the pattern's FCB, the name
MATCHES: .addr  P1 - 1, N_ONE, P1 - 1, N_ONEA, P1 - 1, N_TWO, P2 - 1, N_ONE, P2 - 1, N_TWO
        .addr   P2 - 1, N_DAT, P3 - 1, N_ONE, P3 - 1, N_TWO
MATCHES_LEN = * - MATCHES
P1:     .byte   "ONE     TXT"
P2:     .byte   "????????TXT"
P3:     .byte   "O", 'N' | $80, "?     TXT" ; an attribute bit in the pattern
N_ONE:  .byte   "ONE     TXT"
N_ONEA: .byte   "ONE     ", 'T' | $80, 'X' | $80, "T" ; attributes: RO, SYS
N_TWO:  .byte   "TWO     TXT"
N_DAT:  .byte   "TWO     DAT"
; An XFCB with SECRET as the BDOS keeps it: the key (the sum), then the
; password reversed, each byte XORed with it
PWKEY = 'S' + 'E' + 'C' + 'R' + 'E' + 'T' + ' ' + ' '
PWENTRY: .byte  16, "ONE     TXT", $80, <PWKEY, 0, 0
        .byte   ' ' .bitxor (PWKEY & $FF), ' ' .bitxor (PWKEY & $FF), 'T' .bitxor (PWKEY & $FF), 'E' .bitxor (PWKEY & $FF)
        .byte   'R' .bitxor (PWKEY & $FF), 'C' .bitxor (PWKEY & $FF), 'E' .bitxor (PWKEY & $FF), 'S' .bitxor (PWKEY & $FF)
        .res    8, 0
slist:  .byte   "LIST $"
send:   .byte   "END$"
sunk:   .byte   "UNK $"
samb:   .byte   "AMB $"
seq:    .byte   " = $"
shash:  .byte   " #$"
sover:  .byte   "OVR$"
son:    .byte   "ON$"
soff:   .byte   "OFF$"
slong:  .byte   "LONG$"
sline:  .byte   "Line? $"
slen:   .byte   "LEN $"
sbr:    .byte   " [$"
snone:  .byte   "NONE$"
serr:   .byte   "ERR $"
sbrace: .byte   " {$"
snz:    .byte   "NZ$"
sy:     .byte   " Y$"
sadd:   .byte   "ADD $"
scarry: .byte   " C$"
sshl:   .byte   "SHL $"
sdiv8:  .byte   "DIV8 $"
scmp:   .byte   "CMP $"
sdiv:   .byte   "DIV $"
sdrop:  .byte   "DROPPED$"
sjoined: .byte  "JOINED$"
sdrv:   .byte   "DRV $"
sdpb:   .byte   "DPB $"
ssfcbs: .byte   "SFCBS $"
slabel: .byte   "LABEL $"
sent:   .byte   "ENT $"
sents:  .byte   "ENTRIES $"
sbad:   .byte   "BAD $"
sselerr: .byte  "SELECT FAILED$"
sline2: .byte   "LINE $"
sfail:  .byte   "FAILED$"
swhat:  .byte   "WHAT?$"
sbroke: .byte   "BROKE$"

        .bss
count:  .res    1
savey:  .res    1
ptr:    .res    2
days:   .res    2
fcb:    .res    36
entry:  .res    32

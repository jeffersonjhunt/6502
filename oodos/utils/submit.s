; SUBMIT.COM (hw/oodos.md, "The RSXs, phase 5", SUBMIT and 5c's decisions; the
; User's Guide's SUBMIT, 5-109):
;
;   SUBMIT
;   SUBMIT filespec {argument ...}
;
; The file (type SUB if none is given; on its drive, or the drive search
; chain's, each with user 0's SYS file) is expanded, a line at a time, into a
; temporary file: SUBnn.$$$, the first free nn from 00, in user 0 on the
; temporary drive (the SCB's $50; ours). $1-$9 are the arguments (none: nothing),
; $0 the file's name as typed, $$ a $; any other character after $ is Parameter
; Error. ^ before a letter or @ [ \ ] _ is that control character, ^^ a ^,
; before anything else both are kept. A line ends at CR LF, CR or LF; the file
; at a ^Z or its end. A line over 128 characters expanded is "Line too long"
; (ours), a byte $FF "Invalid ASCII character"; either says "Error on line n:"
; first, and nothing runs. The temporary file is given to SUBMIT's RSX
; (rsx/subrsx.s, attached by gencom.py) by 60. When no SUBMIT RSX serves a file
; (RXF_SUBASK), stale SUB??.$$$ there are deleted first. SUBMIT alone asks
; "Enter File to Submit: ".
;
; Errors: No 'SUB' file found; Error on line n: Parameter Error., Invalid ASCII
; character, Line too long; Directory full (no temporary file could be made);
; Disk Write Error; Insufficient Memory (no RSX took the file); the BDOS's,
; said. The return code $FF00 after any.

        .setcpu "65C02"
        .include "oodos.inc"
        .macpack longbranch

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

CR      = $0D
LF      = $0A
CTLZ    = $1A
OMAX    = 128
NARG    = 10                            ; $0-$9

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        lda     #$FF                    ; the user the file is read in
        ldy     #F_USERNUM
        jsr     BDOS
        sta     ouser
        jsr     spec
        bcc     :+
        lda     #<sask                  ; none: asked for
        ldx     #>sask
        jsr     UT_READ
        jeq     UT_EXIT
        jsr     spec
        jcs     UT_EXIT
:       jsr     args
        jsr     find
        lda     #<pbask                 ; no SUBMIT serving: stale files out
        ldx     #>pbask
        ldy     #P_RSX
        jsr     BDOS
        cmp     #0
        beq     :+
        jsr     stale
:       jsr     maketemp
        jsr     expand
        jsr     give
        jmp     done

; ---- the command --------------------------------------------------------------

; The tail's specification to sfcb, its type SUB if none; $0 kept: carry
; clear. Carry set if none.
spec:   lda     #<sfcb
        ldx     #>sfcb
        jsr     UT_SPEC
        bcc     @one
        cmp     #0
        bne     @nf                     ; one 152 refuses
        sec
        rts
@one:   lda     #<(sfcb + 1)
        ldx     #>(sfcb + 1)
        jsr     UT_ISWILD
        bcs     @nf
        ldx     #7                      ; its password, kept apart (an open writes over it)
@pw:    lda     sfcb + 16,x
        sta     pw,x
        dex
        bpl     @pw
        lda     sfcb + 9
        cmp     #' '
        bne     :+
        ldx     #2
@t:     lda     SUBT,x
        sta     sfcb + 9,x
        dex
        bpl     @t
:       lda     UT_SPAT                 ; $0: the specification as typed
        sta     astart
        sec
        lda     UT_SEND
        sbc     UT_SPAT
        sta     alen
        clc
        rts
@nf:    jmp     notfound

; $1-$9: the words after the specification, separated by blanks.
args:   ldx     #1
@clr:   stz     alen,x
        inx
        cpx     #NARG
        bne     @clr
        ldx     UT_SEND
        ldy     #1
@skip:  lda     UT_TAIL,x
        beq     @done
        cmp     #' '
        bne     @word
        inx
        bra     @skip
@word:  cpy     #NARG
        bcs     @done                   ; past $9: ignored
        txa
        sta     astart,y
@w:     lda     UT_TAIL,x
        beq     @end
        cmp     #' '
        beq     @end
        inx
        bra     @w
@end:   txa
        sec
        sbc     astart,y
        sta     alen,y
        iny
        bra     @skip
@done:  rts

; The file opened: its drive, or each drive of the search chain.
find:   lda     sfcb
        beq     @chain
        jsr     open
        jcs     notfound
        rts
@chain: stz     ci
@next:  lda     ci
        cmp     #4
        jcs     notfound
        clc
        adc     #$4C
        jsr     UT_SCBGET
        cmp     #$FF
        jeq     notfound
        inc     ci
        cmp     #0
        bne     :+
        ldy     #DRV_GET
        jsr     BDOS
        inc     a
:       sta     sfcb
        jsr     open
        bcs     @next
        rts

; sfcb opened (its password given): carry set if it isn't there.
open:   ldx     #23
:       stz     sfcb + 12,x
        dex
        bpl     :-
        lda     #<pw
        ldx     #>pw
        jsr     UT_PWSET
        lda     #<sfcb
        ldx     #>sfcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        beq     :+
        clc
        rts
:       cpx     #0
        beq     :+
        jmp     said
:       sec
        rts

; ---- the temporary file ---------------------------------------------------------

; The temporary drive (SCB $50: 0 the default) into A, 1-16.
tdrive: lda     #$50
        jsr     UT_SCBGET
        cmp     #0
        bne     :+
        ldy     #DRV_GET
        jsr     BDOS
        inc     a
:       rts

; Every SUB??.$$$ in user 0 on the temporary drive deleted.
stale:  jsr     tdrive
        sta     tfcb
        ldx     #10
:       lda     WILDN,x
        sta     tfcb + 1,x
        dex
        bpl     :-
        jsr     clrtf
        jsr     user0
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
        jmp     userback

; SUBnn.$$$, the first nn not there, made in user 0.
maketemp:
        jsr     tdrive
        sta     tfcb
        jsr     user0
        stz     nn
@try:   ldx     #10
:       lda     TEMPN,x
        sta     tfcb + 1,x
        dex
        bpl     :-
        lda     nn                      ; nn in decimal
        ldx     #'0'
@tens:  cmp     #10
        bcc     :+
        sbc     #10
        inx
        bra     @tens
:       stx     tfcb + 4
        ora     #'0'
        sta     tfcb + 5
        jsr     clrtf
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_SFIRST
        jsr     BDOS
        cmp     #$FF
        beq     @free
        inc     nn
        lda     nn
        cmp     #100
        bne     @try
        bra     @full
@free:  jsr     clrtf
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_MAKE
        jsr     BDOS
        cmp     #$FF
        beq     @full
        stz     widx
        jmp     userback
@full:  jsr     userback
        lda     #<sdirfull
        ldx     #>sdirfull
        jmp     fail
clrtf:  ldx     #23
:       stz     tfcb + 12,x
        dex
        bpl     :-
        rts

user0:  lda     #0
        bra     setu
userback:
        lda     ouser
setu:   ldy     #F_USERNUM
        jmp     BDOS

; ---- the expansion ---------------------------------------------------------------

expand: lda     #1
        sta     lineno
        stz     lineno + 1
        stz     olen
        stz     prevcr
        lda     #128
        sta     ridx
        stz     reof
@byte:  jsr     getb
        jcs     @eof
        cmp     #$FF
        bne     :+
        lda     #<sascii
        ldx     #>sascii
        jmp     lerror
:       cmp     #LF
        bne     @notlf
        lda     prevcr
        stz     prevcr
        bne     @byte                   ; CR LF: one end
        jsr     endline
        bra     @byte
@notlf: stz     prevcr
        cmp     #CR
        bne     @notcr
        lda     #1
        sta     prevcr
        jsr     endline
        bra     @byte
@notcr: cmp     #'$'
        beq     @dollar
        cmp     #'^'
        beq     @caret
        jsr     out
        bra     @byte
@dollar:
        jsr     getb
        bcs     @perr
        cmp     #'$'
        bne     :+
        jsr     out
        bra     @byte
:       sec
        sbc     #'0'
        cmp     #NARG
        bcs     @perr
        jsr     outarg
        bra     @byte
@perr:  lda     #<sparam
        ldx     #>sparam
        jmp     lerror
@caret: jsr     getb                    ; ^x
        bcs     @lone
        cmp     #'^'
        bne     :+
        jsr     out                     ; ^^: a ^
        bra     @byte
:       sta     t
        and     #$DF                    ; a letter, either case, or @ [ \ ] _
        cmp     #'@'
        bcc     @keep
        cmp     #'_' + 1
        bcs     @keep
        lda     t
        cmp     #$60
        beq     @keep                   ; ` isn't one
        cmp     #$7B
        bcs     @keep
        and     #$1F
        jsr     out
        jmp     @byte
@keep:  lda     #'^'                    ; anything else: both kept
        jsr     out
        lda     t
        cmp     #LF                     ; (a line's end after it ends the line)
        beq     :++
        cmp     #CR
        beq     :+
        jsr     out
        jmp     @byte
:       lda     #1
        sta     prevcr
:       jsr     endline
        jmp     @byte
@lone:  lda     #'^'
        jsr     out
@eof:   lda     olen                    ; a last line without its end
        beq     :+
        jsr     endline
:       jmp     wclose

; A to the line; past 128: Line too long.
out:    ldx     olen
        cpx     #OMAX
        bcc     :+
        lda     #<slong
        ldx     #>slong
        jmp     lerror
:       sta     obuf,x
        inc     olen
        rts
; Argument A (0-9) to the line.
outarg: tax
        lda     alen,x
        beq     @done
        sta     t
        lda     astart,x
        tax
@c:     lda     UT_TAIL,x
        phx
        jsr     out
        plx
        inx
        dec     t
        bne     @c
@done:  rts

; The line, then CR LF, to the temporary file; the next line counted.
endline:
        ldx     #0
@c:     cpx     olen
        beq     @end
        lda     obuf,x
        phx
        jsr     putw
        plx
        inx
        bra     @c
@end:   lda     #CR
        jsr     putw
        lda     #LF
        jsr     putw
        stz     olen
        inc     lineno
        bne     :+
        inc     lineno + 1
:       rts

; The SUB file's next byte (in its user): carry set at ^Z or its end.
getb:   lda     reof
        bne     @end
        lda     ridx
        bpl     @have
        lda     #<rbuf
        ldx     #>rbuf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<sfcb
        ldx     #>sfcb
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        bne     @last
        stz     ridx
@have:  ldx     ridx
        lda     rbuf,x
        cmp     #CTLZ
        beq     @last
        inc     ridx
        clc
        rts
@last:  lda     #1
        sta     reof
@end:   sec
        rts

; A to the temporary file's record, written when full (user 0).
putw:   ldx     widx
        sta     wbuf,x
        inx
        stx     widx
        bpl     :+
        jsr     wrec
        stz     widx
:       rts
wrec:   jsr     user0
        lda     #<wbuf
        ldx     #>wbuf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_WRITE
        jsr     BDOS
        pha
        jsr     userback
        pla
        cmp     #0
        bne     :+
        rts
:       jsr     deltemp
        lda     #<swrite
        ldx     #>swrite
        jmp     fail

; The last record ^Z-padded and written, the file closed.
wclose: lda     widx
        beq     @close
@pad:   lda     #CTLZ
        jsr     putw
        lda     widx
        bne     @pad
@close: jsr     user0
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_CLOSE
        jsr     BDOS
        jmp     userback

; Opened again, from its start, and given to SUBMIT's RSX.
give:   jsr     user0
        jsr     clrtf
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_OPEN
        jsr     BDOS
        pha
        jsr     userback
        pla
        cmp     #$FF
        bne     :+
        jsr     deltemp                 ; gone, or unreadable: no RSX given it
        lda     #<sread
        ldx     #>sread
        jmp     fail
:       lda     #<pbfile
        ldx     #>pbfile
        ldy     #P_RSX
        jsr     BDOS
        cmp     #0
        bne     :+
        rts
:       jsr     deltemp
        lda     #<smem
        ldx     #>smem
        jmp     fail

deltemp:
        jsr     user0
        ldx     #11
:       lda     tfcb,x
        sta     dfcb,x
        dex
        bpl     :-
        ldx     #23
:       stz     dfcb + 12,x
        dex
        bpl     :-
        lda     #<dfcb
        ldx     #>dfcb
        ldy     #F_DELETE
        jsr     BDOS
        jmp     userback

; ---- the ends --------------------------------------------------------------------

; "Error on line n: " and the message at A/X; the temporary file deleted.
lerror: pha
        phx
        jsr     deltemp
        lda     #<serr
        ldx     #>serr
        jsr     CM_PUTS
        lda     lineno
        sta     UT_N
        lda     lineno + 1
        sta     UT_N + 1
        stz     UT_N + 2
        lda     #0
        clc
        jsr     UT_NOUT
        lda     #<scolon
        ldx     #>scolon
        jsr     CM_PUTS
        plx
        pla
        bra     fail
notfound:
        lda     #<snofile
        ldx     #>snofile
fail:   jsr     UT_SAY
        jsr     UT_FAIL
        jsr     userback
        jmp     UT_EXIT
said:   jsr     UT_SAID
        jsr     userback
        jmp     UT_EXIT
done:   jsr     userback
        jmp     UT_EXIT

        .include "cmd.inc"
        .include "util.inc"

        .rodata
SUBT:   .byte   "SUB"
TEMPN:  .byte   "SUB00   $$$"
WILDN:  .byte   "SUB??   $$$"
sask:   .byte   "Enter File to Submit: $"
snofile: .byte  "No 'SUB' file found$"
serr:   .byte   "Error on line $"
scolon: .byte   ": $"
sparam: .byte   "Parameter Error.$"
sascii: .byte   "Invalid ASCII character$"
slong:  .byte   "Line too long$"
sdirfull: .byte "Directory full$"
swrite: .byte   "Disk Write Error$"
sread:  .byte   "Disk Read Error$"
smem:   .byte   "Insufficient Memory$"

        .data
pbask:  .byte   RXF_SUBASK, 0
pbfile: .byte   RXF_SUBFILE, 1
        .word   tfcb

        .bss
ouser:  .res    1
ci:     .res    1
nn:     .res    1
t:      .res    1
olen:   .res    1
widx:   .res    1
ridx:   .res    1
reof:   .res    1
prevcr: .res    1
lineno: .res    2
astart: .res    NARG
alen:   .res    NARG
pw:     .res    8
sfcb:   .res    36
tfcb:   .res    36
dfcb:   .res    36
obuf:   .res    OMAX
rbuf:   .res    128
wbuf:   .res    128

; DIAG.COM (hw/diag.md, "DIAG.COM", D2; the User's Guide's DIAG): the OO-1's
; diagnostics under OODOS. It finds each card by its ID byte (oohw's
; HW_PROBE), checks what the card does, and tests the memory OODOS doesn't
; own: its own TPA, the blocks functions 53-58 give it, banks 9-B and the
; program's part of the video card's memory, reached through OOLIB's
; GX_BLOCK and GX_VMAP under the W1 contract. It touches the hardware only
; as hw/diag.md's answer 1 allows the system tools (AGENTS.md), and every
; DMA transfer goes through OODOS's functions 82 and 83.
;
;   DIAG {area ...} {/FULL} {/I} {/WRITE} {/LOOP} {/?}
;
;   area    NB, MEM, DMA, VIDEO, SOUND, SD, USB, SERIAL, DISK2, PANEL;
;           none is all of them, in that order
;   /FULL   the march with all four backgrounds, and on card memory too;
;           S6 (wave RAM rewritten); with /I, P3 (the CPU halted)
;   /I      the tests that ask the user to do something
;   /WRITE  SD4: a 64 KB file A:DIAG$$$.TMP written, read back and erased,
;           after a Y
;   /LOOP   the run again until a key, its passes counted
;   /?      the options
;
; Each test writes one line (oohw's HW_TEST and HW_LINE): its name, what it
; checked, PASS, FAIL, SKIP and why, or INFO for a measurement with no
; limit; a failure adds a line of what was seen. The summary ends the run,
; and the return code (function 108) is the count of failures (0 none), or
; $FF00 for a command line it doesn't take, $FFFE for a ^C.
;
; Its code is in $0300-$3FFF and from $8000 (CODE2), its data and BSS
; after that, and its buffers from $AC00 (SCR), never in W1's $4000-$7FFF,
; which OOLIB maps (the W1 contract; cfg/diag.cfg). M1 tests the TPA DIAG
; doesn't use, $4000-$7FFF and past the BSS, buffers too, before anything
; uses them.

        .setcpu "65C02"
        .macpack longbranch
        .include "backplane.inc"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "oohw.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = brkx

        OODOS_HEADER

SCR     = $AC00                         ; the buffers, past the BSS (cfg/diag.cfg): 4 KB
SCRID   = $BC00                         ; IDENTIFY's sector
SCRREC  = $BE00                         ; SD4's record
TPAEND  = $BFFC                         ; the TPA's end: DIAG needs it all
W1      = $4000                         ; W1's window
NATIVE  = 7159090                       ; the CPU's clock, Hz
FRAME   = 119437                        ; a video frame's CPU cycles (800 x 525 at 25.175 MHz)
NAREA   = 10                            ; the areas, in arean's order

; The areas' bits in want (want + 1 for the last two).
A_NB    = $01
A_MEM   = $02
A_DMA   = $04
A_VIDEO = $08
A_SOUND = $10
A_SD    = $20
A_USB   = $40
A_SERIAL = $80

; The macros' strings: a string given is put in RODATA at an unnamed label
; (which leaves the cheap local labels' scope as it is); a name given is a
; string MDEF (or a line LDEF) made once, at the top.
.macro MDEF name, t
name:   .byte   t, 0
.endmacro
.macro LDEF name, n, t
name:   .byte   n, 0, t, 0
.endmacro
.macro AX t
.if .match({t}, "")
        .pushseg
        .rodata
:       .byte   t, 0
        .popseg
        lda     #<:-
        ldx     #>:-
.else
        lda     #<t
        ldx     #>t
.endif
.endmacro
; A test's line: its name, what it checks.
.macro LINE n, t
.ifblank t
        lda     #<n
        ldx     #>n
.else
        .pushseg
        .rodata
:       .byte   n, 0, t, 0
        .popseg
        lda     #<:-
        ldx     #>:-
.endif
        jsr     begin
.endmacro
; A string written (counted in HW_COL).
.macro SAY t
        AX      t
        jsr     HW_PUTS
.endmacro
; SKIP and why: " (why)".
.macro SKIP t
        AX      t
        jsr     skip
.endmacro
; A failure's line after the FAIL: seven blanks, then t.
.macro FLINE t
        AX      t
        jsr     fline
.endmacro
; Function 80, 81, 82 or 83 with the block at b: A and X its answer.
.macro CALL f, b
        lda     #<b
        ldx     #>b
        ldy     #f
        jsr     BDOS
.endmacro

        .import __BSS_RUN__, __BSS_SIZE__

        .rodata
; The messages used more than once.
        MDEF    mblock, "block "
        MDEF    mnocard, "no card"
        MDEF    mnomap, "no W1 map: no revision 2 video card"
        MDEF    mnoarea, "no event area"
        MDEF    mto, " to "
        MDEF    mhzrtc, "Hz by the clock chip"
        MDEF    mhzfrm, "Hz by the video frame"
        MDEF    mnofree, "none free"
        MDEF    mnovid, "no revision 2 video card"
        MDEF    mnoself, "no DIAG.COM on the drive it came from"
        MDEF    mnoload, "function 83's DM_LOAD refused"
        MDEF    mcardat, "card +$"
        MDEF    mexpect, ", expected "
        MDEF    mmute, "the 6510 didn't answer"
        MDEF    mnombox, "no event area for the mailbox"
        MDEF    minuse, "in use"
        MDEF    mnovmem, "no card memory for the program"
        MDEF    mfps, "frames a second"
        MDEF    mstatus, "STATUS "
        MDEF    mkbs, "KB a second"
        LDEF    lv6, "V6", "colour bars and a grid seen"
        LDEF    lsd4, "SD4", "A:DIAG$$$.TMP, 64 KB, written, read back and erased"
        LDEF    lu3, "U3", "keys: A, then Esc, seen in the event queue"
        LDEF    lu4, "U4", "the mouse, its buttons and the game port's"
        LDEF    lsr3, "SR3", "the joysticks: each direction and fire, both ports"
        LDEF    lp3, "P3", "the breakpoint: a read halts the CPU, CONT goes on"

        .zeropage
zp:     .res    2                       ; a pointer

        .code
start:  jsr     tinit
        stz     want
        stz     want + 1
        stz     full
        stz     inter
        stz     write
        stz     loop
        stz     held
        stz     guard
        stz     areaok
        stz     ftotal
        stz     ftotal + 1
        stz     nalloc
@arg:   jsr     tskip
        beq     @go
        jsr     word
        jsr     which
        bcc     @arg
        jmp     usage
@go:    lda     want
        ora     want + 1
        bne     @some
        lda     #$FF
        sta     want
        lda     #$03
        sta     want + 1
@some:  jsr     CM_PAGE                 ; console mode bit 0: 11 reports a ^C, which ends DIAG
        stz     CM_PLEN                 ;   at a line's end; no pages
        lda     BP_TOP + 1              ; the TPA to $BFFC: no RSX below the BDOS
        cmp     #>(TPAEND + 1)
        bcc     @small
        bne     @open
        lda     BP_TOP
        cmp     #<(TPAEND + 1)
        bcs     @open
@small: lda     #<ssmall
        ldx     #>ssmall
        jsr     tsay
        jsr     tfail
        jmp     texit
@open:  lda     #<area                  ; the event area: layout 2, 256 bytes
        sta     opblk + 1
        lda     #>area
        sta     opblk + 2
        CALL    O_FRAME, opblk
        cmp     #0
        bne     @noarea
        dec     areaok
@noarea:
        lda     #<area
        ldx     #>area
        ldy     areaok
        bne     @gx
        lda     #0
        tax
@gx:    jsr     GX_INIT                 ; carry set: no W1 map (no revision 2 card, an older OODOS)
        lda     #0
        rol     a
        eor     #1
        sta     gxok
        jsr     HW_PROBE
        jsr     alloc
        stz     npass
        stz     npass + 1
again:  inc     npass
        bne     @p
        inc     npass + 1
@p:     stz     HW_TALLY + HL_PASS
        stz     HW_TALLY + HL_FAIL
        stz     HW_TALLY + HL_SKIP
        stz     HW_TALLY + HL_INFO
        jsr     title
        jsr     CM_FLUSH
        jsr     CM_EOL
        lda     #0
        sta     anum
@area:  lda     anum
        jsr     wanted
        beq     @next
        jsr     CM_EOL
        lda     anum
        asl     a
        tax
        jsr     @call
@next:  inc     anum
        lda     anum
        cmp     #NAREA
        bne     @area
        jsr     CM_EOL
        jsr     HW_SUMMARY
        clc
        adc     ftotal
        sta     ftotal
        bcc     @f
        inc     ftotal + 1
@f:     lda     HW_TALLY + HL_SKIP
        beq     @nos
        lda     inter                   ; a skip because of /I or /FULL: said once
        and     full
        bne     @nos
        lda     #<snoi
        ldx     #>snoi
        jsr     HW_PUTS
@nos:   lda     loop
        beq     @done
        jsr     CM_EOL
        SAY     "pass "
        lda     npass
        ldx     npass + 1
        jsr     dec16
        SAY     " ended; a key stops"
        lda     #$FE                    ; a key? (6: $FF if one waits)
        ldy     #C_RAWIO
        jsr     BDOS
        cmp     #0
        bne     @stop
        jmp     again
@stop:  lda     #$FF
        ldy     #C_RAWIO
        jsr     BDOS
@done:  jsr     finish
        lda     ftotal
        ldx     ftotal + 1
        ldy     #P_CODE
        jsr     BDOS
        jmp     texit
@call:  jmp     (areas,x)

usage:  lda     #<susage
        ldx     #>susage
        jsr     tsay
        jsr     tfail
        jmp     texit

; Everything DIAG took, given back: the panel if P2 or P3 were under way, the
; sound card's parts, the blocks, the event area, W1 (and the console's
; screen, after V6).
finish: jsr     unguard
        jsr     sndrel
        lda     nalloc
        beq     @nofree
        lda     #<mcb
        ldx     #>mcb
        ldy     #MC_ALLFREE
        jsr     BDOS
        stz     nalloc
@nofree:
        lda     shown
        beq     @noshow
        stz     shown
        jsr     GX_CONSOLE
@noshow:
        lda     areaok
        beq     @noarea
        stz     areaok
        lda     #FT_CLOSE
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
@noarea:
        jmp     GX_W1BACK

; ^C (cmd.inc's CMD_BREAK, at a line's end): everything given back, then
; util.inc's break (the return code $FFFE).
brkx:   jsr     finish
        jmp     tbreak

; The tail's next word, to a blank or its end, into wbuf (0-ended; 15 at most).
word:   ldy     #0
@c:     jsr     tpeek
        beq     @end
        cmp     #' '
        beq     @end
        cmp     #9
        beq     @end
        jsr     tget
        cpy     #15
        bcs     @c
        sta     wbuf,y
        iny
        bra     @c
@end:   lda     #0
        sta     wbuf,y
        rts

; wbuf an option or an area: carry set if neither.
which:  lda     #<opttab                ; the options: a name, its 0, the byte it sets
        sta     zp
        lda     #>opttab
        sta     zp + 1
@opt:   lda     (zp)
        beq     @areas
        jsr     same
        bcs     @isopt
        jsr     pastname
        jsr     pastbyte
        bra     @opt
@isopt: iny                             ; the byte after the name's 0
        lda     (zp),y
        cmp     #$FF
        beq     help
        tax
        lda     #1
        sta     full,x
        clc
        rts
@areas: lda     #<areatab
        sta     zp
        lda     #>areatab
        sta     zp + 1
        stz     anum
@try:   jsr     same
        bcs     @isarea
        jsr     pastname
        inc     anum
        lda     anum
        cmp     #NAREA
        bne     @try
        sec
        rts
@isarea:
        lda     anum
        jsr     abit
        ora     want,x
        sta     want,x
        clc
        rts
; Carry set if wbuf is the name at zp; Y then at its 0.
same:   ldy     #0
@c:     lda     wbuf,y
        cmp     (zp),y
        bne     @no
        cmp     #0
        beq     @yes
        iny
        bra     @c
@yes:   sec
        rts
@no:    clc
        rts
; zp past the name there and its 0.
pastname:
        lda     (zp)
        pha
        jsr     pastbyte
        pla
        bne     pastname
        rts
pastbyte:
        inc     zp
        bne     @r
        inc     zp + 1
@r:     rts

; /?: the options.
help:   lda     #<shelp
        ldx     #>shelp
        jsr     CM_PUTS
        jmp     texit

; A an area: A its bit, X 0 or 1 (want's byte).
abit:   ldx     #0
        cmp     #8
        bcc     @lo
        inx
        sec
        sbc     #8
@lo:    tay
        lda     bit8,y
        rts
; Z clear if area A is wanted.
wanted: jsr     abit
        and     want,x
        rts

; The title: "OO-1 DIAGNOSTICS 0.1   quick, nothing destroyed   (DIAG /? ...)".
title:  lda     #<stitle
        ldx     #>stitle
        jsr     HW_PUTS
        lda     full
        beq     @quick
        SAY     "full"
        bra     @i
@quick: SAY     "quick"
@i:     lda     inter
        beq     @w
        SAY     ", interactive"
@w:     lda     write
        beq     @nw
        SAY     ", A:DIAG$$$.TMP written"
        bra     @opt
@nw:    SAY     ", nothing destroyed"
@opt:   lda     loop
        beq     @end
        SAY     "   PASS "
        lda     npass
        ldx     npass + 1
        jsr     dec16
@end:   SAY     "   (DIAG /? for the options)"
        jmp     CM_EOL

; ---------------------------------------------------------------------------
; Writing

HW_PUTC:                                ; oohw's output: counted for HW_LINE's column
        inc     HW_COL
        jmp     CM_PUTC

; A/X the test's name and what it checks (LINE's record): the line begun.
begin:  sta     zp
        stx     zp + 1
        lda     CM_LFDUE                ; a new line, after a FAIL's too (after an answer, its LF)
        bne     @lf
        jsr     CM_EOL
@lf:        lda     zp
        ldx     zp + 1
        jsr     HW_TEST
        ldy     #0                      ; what it checks: after the name's 0
@n:     lda     (zp),y
        beq     @t
        iny
        bra     @n
@t:     iny
        tya
        clc
        adc     zp
        pha
        lda     zp + 1
        adc     #0
        tax
        pla
        jmp     HW_PUTS
; The results: the line ended (owed).
pass:   lda     #HL_PASS
        bra     result
fail:   lda     #HL_FAIL
        bra     result
info:   lda     #HL_INFO
result: jsr     HW_LINE
        jmp     CM_EOL
; SKIP, then " (A/X)".
skip:   pha
        phx
        lda     #HL_SKIP
        jsr     HW_LINE
        lda     #' '
        jsr     HW_PUTC
        lda     #'('
        jsr     HW_PUTC
        plx
        pla
        jsr     HW_PUTS
        lda     #')'
        jsr     HW_PUTC
        jmp     CM_EOL
; FAIL, then a line of seven blanks and A/X.
failat: pha
        phx
        jsr     fail
        plx
        pla
fline:  pha
        phx
        jsr     CM_EOL
        jsr     CM_FLUSH
        ldx     #7
@b:     lda     #' '
        jsr     HW_PUTC
        dex
        bne     @b
        plx
        pla
        jmp     HW_PUTS
; An area's heading: A/X its name, then its ID ($xx) if module Y is found.
heading:
        sta     zp
        stx     zp + 1
        jsr     CM_EOL
        phy
        jsr     CM_FLUSH
        lda     zp
        ldx     zp + 1
        jsr     HW_PUTS
        ply
        lda     HW_REV,y
        cmp     #HW_NONE
        beq     @none
        lda     #' '
        jsr     HW_PUTC
        lda     #'$'
        jsr     HW_PUTC
        lda     HW_IDB,y
        jsr     HW_HEX2
@none:  jmp     CM_EOL

; A/X in decimal, with commas.
dec16:  sta     HW_NUM
        stx     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        sec
        jmp     HW_DEC
dec8:   ldx     #0
        bra     dec16
; HW_NUM in decimal, with commas.
decn:   sec
        jmp     HW_DEC
; "$" and A in hex.
hex2:   pha
        lda     #'$'
        jsr     HW_PUTC
        pla
        jmp     HW_HEX2
; "$" and A/X in hex.
hex4:   pha
        lda     #'$'
        jsr     HW_PUTC
        pla
        jmp     HW_HEX4
; "$" and the 20-bit address A/X/Y (low, middle, high) in five digits.
hex5:   pha
        lda     #'$'
        jsr     HW_PUTC
        tya
        and     #$0F
        ora     #'0'
        cmp     #'9' + 1
        bcc     @d
        adc     #'A' - '9' - 2
@d:     jsr     HW_PUTC
        pla
        jmp     HW_HEX4
; "$" and the 24-bit A/X/Y in six digits.
hex6:   pha
        lda     #'$'
        jsr     HW_PUTC
        pla
        jmp     HW_HEX6

; ---------------------------------------------------------------------------
; Time, the event area, the mailbox

; The event area's millisecond counter's low word: A/X, read until two reads
; agree (an interrupt may write it between the bytes).
msnow:  lda     area + EV_MS
        ldx     area + EV_MS + 1
        cmp     area + EV_MS
        bne     msnow
        cpx     area + EV_MS + 1
        bne     msnow
        rts
; t0 = now.
mark:   jsr     msnow
        sta     t0
        stx     t0 + 1
        rts
; A/X the milliseconds since t0.
since:  jsr     msnow
        sec
        sbc     t0
        pha
        txa
        sbc     t0 + 1
        tax
        pla
        rts
; Carry set once A/X milliseconds have passed since t0.
late:   sta     lim
        stx     lim + 1
        jsr     since
        cmp     lim
        txa
        sbc     lim + 1
        rts
; A/X milliseconds waited (function 80's FT_DELAY).
delay:  sta     dlyblk + 1
        stx     dlyblk + 2
        CALL    O_FRAME, dlyblk
        rts

; The event queue's next event into ev (8 bytes): carry clear and A its kind;
; carry set if none waits. The queue's tail is the program's.
evnext: lda     area + EV_QTAIL
        cmp     area + EV_QHEAD
        beq     @none
        asl     a
        asl     a
        asl     a
        tax
        ldy     #0
@cp:    lda     area + EV_QUEUE,x
        sta     ev,y
        inx
        iny
        cpy     #EQ_LEN
        bne     @cp
        lda     area + EV_QTAIL
        inc     a
        cmp     area + EV_QSLOTS
        bcc     @t
        lda     #0
@t:     sta     area + EV_QTAIL
        lda     ev + EQ_KIND
        clc
        rts
@none:  sec
        rts
; The queue emptied.
evflush:
        lda     area + EV_QHEAD
        sta     area + EV_QTAIL
        rts

; The 6510's next byte (an EK_SOUND event), within A/X ms: carry clear and A
; the byte; carry set if none came. Other events are passed over.
mbget:  sta     mblim
        stx     mblim + 1
        jsr     mark
@w:     jsr     evnext
        bcs     @late
        cmp     #EK_SOUND
        bne     @w
        lda     ev + EQ_VALUE
        clc
        rts
@late:  lda     mblim
        ldx     mblim + 1
        jsr     late
        bcc     @w
        rts
; A to the 6510 (SN_SEND, again while the card hasn't taken the last).
mbput:  sta     sndblk + 1
@try:   lda     #SN_SEND
        sta     sndblk
        CALL    O_SOUND, sndblk
        cmp     #SN_BUSY
        beq     @try
        rts
; A command and its operands to the 6510: X the bytes at cmdb.
mbcmd:  stx     tmp
        ldx     #0
@b:     phx
        lda     cmdb,x
        jsr     mbput
        plx
        inx
        cpx     tmp
        bne     @b
        rts

; ---------------------------------------------------------------------------
; Memory: the blocks DIAG may map

; Every free block of banks 1-5 allocated (53, again until none is left),
; and the lists made from the W1 map OOLIB read: blk1 the allocated ones
; (4-23), blk9 banks 9-B's (36-47).
alloc:  lda     #$FF                    ; the most there is
        sta     mcb + 2
        sta     mcb + 3
        lda     #<mcb
        ldx     #>mcb
        ldy     #MC_MAX
        jsr     BDOS
        cmp     #0
        bne     @lists
        inc     nalloc
        bra     alloc
@lists: lda     nalloc
        beq     @map
        jsr     GX_REMAP
@map:   stz     nblk1
        stz     nblk9
        lda     gxok                    ; no W1 map: nothing DIAG may map
        bne     @ok
        rts
@ok:    ldx     #4
@b1:    txa
        jsr     inmap
        beq     @n1
        ldy     nblk1
        txa
        sta     blk1,y
        inc     nblk1
@n1:    inx
        cpx     #24
        bne     @b1
        ldx     #36
@b9:    txa
        jsr     inmap
        beq     @n9
        ldy     nblk9
        txa
        sta     blk9,y
        inc     nblk9
@n9:    inx
        cpx     #48
        bne     @b9
        rts
; Z clear if block A is in the W1 map (and OOLIB may map: GX_INIT took).
; Keeps X.
inmap:  phx
        pha
        and     #7
        tay
        pla
        lsr     a
        lsr     a
        lsr     a
        tax
        lda     GX_W1MAP,x
        and     bit8,y
        plx
        cmp     #0
        rts

; Block A into W1 (GX_BLOCK): carry set if it can't be.
map:    jmp     GX_BLOCK

; The physical address of block A's +0 into A/X/Y (low, middle, high).
bphys:  pha
        lsr     a
        lsr     a
        tay
        pla
        and     #3
        lsr     a
        ror     a
        ror     a
        tax
        lda     #0
        rts

; "block bb +$oooo ($ppppp): " for a fault at HW_BAD in W1, block A.
wherew1:
        sta     fblk
        SAY     mblock
        lda     fblk
        jsr     dec8
        SAY     " +"
        sec
        lda     HW_BAD + 1
        sbc     #>W1
        tax
        lda     HW_BAD
        jsr     hex4
        SAY     " ("
        lda     fblk                    ; the block's base and the offset
        jsr     bphys
        stx     tmp
        sec
        lda     HW_BAD + 1
        sbc     #>W1
        ora     tmp
        tax
        lda     HW_BAD
        jsr     hex5
        SAY     "): "
        jmp     HW_FAULT

; The list at blk1 (A 0) or blk9 (A 1): zp the list, cnt its length.
list:   cmp     #0
        bne     @nine
        lda     #<blk1
        ldx     #>blk1
        ldy     nblk1
        bra     @set
@nine:  lda     #<blk9
        ldx     #>blk9
        ldy     nblk9
@set:   sta     zp
        stx     zp + 1
        sty     cnt
        rts

; The list's blocks (list): walking 1s and 0s at each one's first byte,
; then the address lines' tags in all, then address in address in all,
; each written in all before any is read. Carry set at the first error,
; fblk its block, HW_BAD and the rest.
blkaia: lda     #<hwwalk
        ldx     #>hwwalk
        jsr     each
        bcs     @r
        lda     #<HW_TAGW
        ldx     #>HW_TAGW
        jsr     each
        bcs     @r
        lda     #<HW_TAGC
        ldx     #>HW_TAGC
        jsr     each
        bcs     @r
        lda     #<HW_AIAW
        ldx     #>HW_AIAW
        jsr     each
        bcs     @r
        lda     #<HW_AIAC
        ldx     #>HW_AIAC
        jsr     each
@r:     rts
hwwalk: jmp     HW_WALK
; The list's blocks marched, with each background (bgs: one quick, four full).
blkmarch:
        stz     bgi
@bg:    ldx     bgi
        lda     bgtab,x
        sta     HW_BG
        lda     #<HW_MARCH
        ldx     #>HW_MARCH
        jsr     each
        bcs     @r
        inc     bgi
        lda     bgi
        cmp     nbg
        bne     @bg
        clc
@r:     rts
; The routine A/X for each block of the list: mapped in W1, HW_PTR W1,
; HW_LEN 16 KB, HW_KEY the block's number. Carry set at its first error.
each:   sta     eachjmp + 1
        stx     eachjmp + 2
        stz     idx
@b:     lda     idx
        cmp     cnt
        beq     @done
        tay
        lda     (zp),y
        sta     fblk
        jsr     map
        bcs     @nomap
        lda     fblk
        sta     HW_KEY
        stz     HW_PTR
        lda     #>W1
        sta     HW_PTR + 1
        stz     HW_LEN
        lda     #$40
        sta     HW_LEN + 1
        jsr     eachjmp
        bcs     @r
        inc     idx
        bra     @b
@done:  clc
@r:     rts
@nomap: stz     HW_BAD                  ; a block GX_BLOCK refused: reported at its +0
        lda     #>W1
        sta     HW_BAD + 1
        stz     HW_GOT
        stz     HW_WANT
        sec
        rts

; The number of backgrounds: 1 quick, 4 full.
setbgs: lda     #1
        ldx     full
        beq     @q
        lda     #4
@q:     sta     nbg
        rts

; ---------------------------------------------------------------------------
; The areas

; NB: the north bridge (module D)
anb:    lda     #<hnb
        ldx     #>hnb
        ldy     #$D
        jsr     heading
        jsr     nb1
        jsr     nb2
        jsr     nb3
        jsr     nb4
        jsr     nb5
        jmp     nb6

; The card's revision, or SKIP: carry set (the line ended) if module Y
; isn't there or isn't known.
known:  lda     HW_REV,y
        cmp     #HW_NONE
        beq     @none
        tax
        tya
        phy
        jsr     HW_KNOWN
        ply
        bcs     @unk
        clc
        rts
@none:  SKIP    mnocard
        sec
        rts
@unk:   lda     #<srevn
        ldx     #>srevn
        jsr     HW_PUTS
        lda     #HL_SKIP
        jsr     HW_LINE
        jsr     CM_EOL
        sec
        rts

nb1:    LINE    "NB1", "ID and STATUS agree, revision "
        ldy     #$D
        lda     HW_REV,y
        cmp     #HW_NONE
        beq     @r
        jsr     dec8
@r:     ldy     #$D
        jsr     known
        bcs     @end
        lda     NB_STATUS
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        ldy     #$D
        cmp     HW_REV,y
        bne     @bad
        jmp     pass
@bad:   jsr     fail
        FLINE   "STATUS bits 4-7 "
        lda     NB_STATUS
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jmp     dec8
@end:   rts

nb2:    LINE    "NB2", "W1 shows each of "
        clc
        lda     nblk1
        adc     nblk9
        sta     nall
        jsr     dec8
        SAY     " blocks as itself"
        lda     nall
        bne     @some
        SKIP    mnomap
        rts
@some:  lda     #0                      ; each block's number into its first 16 bytes, by DMA
        jsr     list
        jsr     @fill
        lda     #1
        jsr     list
        jsr     @fill
        lda     #0
        jsr     list
        jsr     @check
        bcs     @bad
        lda     #1
        jsr     list
        jsr     @check
        bcs     @bad
        jsr     GX_W1BACK
        jmp     pass
@bad:   jsr     GX_W1BACK
        jsr     fail
        FLINE   ""
        lda     fblk
        jmp     wherew1
@fill:  stz     idx
@f:     lda     idx
        cmp     cnt
        beq     @fe
        tay
        lda     (zp),y
        sta     fillblk + 1
        jsr     bphys
        sta     fillblk + 2
        stx     fillblk + 3
        sty     fillblk + 4
        lda     #16
        sta     fillblk + 5
        stz     fillblk + 6
        lda     #DM_FILL
        sta     fillblk
        CALL    O_DMA, fillblk
        inc     idx
        bra     @f
@fe:    rts
@check: stz     idx
@c:     lda     idx
        cmp     cnt
        beq     @ok
        tay
        lda     (zp),y
        sta     fblk
        jsr     map
        bcs     @nomap
        ldx     #0
@byte:  lda     W1,x
        cmp     fblk
        bne     @wrong
        inx
        cpx     #16
        bne     @byte
        inc     idx
        bra     @c
@ok:    clc
        rts
@wrong: sta     HW_GOT
        lda     fblk
        sta     HW_WANT
        stx     HW_BAD
        lda     #>W1
        sta     HW_BAD + 1
        sec
        rts
@nomap: stz     HW_BAD
        lda     #>W1
        sta     HW_BAD + 1
        stz     HW_GOT
        stz     HW_WANT
        sec
        rts

nb3:    LINE    "NB3", "no interrupt line stuck"
        ldy     #$D
        jsr     known
        bcs     @end
        lda     #<1000
        sta     n16
        lda     #>1000
        sta     n16 + 1
        lda     HW_FOUND                ; the lines of modules not found: always 1
        eor     #$FF
        sta     maskl
        lda     HW_FOUND + 1
        eor     #$FF
        sta     maskh
@poll:  lda     NB_IRQL
        sta     irql
        and     maskl
        cmp     maskl
        bne     @bad
        lda     NB_IRQH
        sta     irqh
        and     maskh
        cmp     maskh
        bne     @bad
        ldx     #10                     ; some 70 cycles a poll: 1,000 in 10 ms
@d:     dex
        bne     @d
        lda     n16
        bne     @lo
        dec     n16 + 1
@lo:    dec     n16
        lda     n16
        ora     n16 + 1
        bne     @poll
        jmp     pass
@bad:   jsr     fail
        FLINE   "IRQL "
        lda     irql
        jsr     hex2
        SAY     ", IRQH "
        lda     irqh
        jsr     hex2
        SAY     ": a module not found pulled one low"
@end:   rts

nb4:    LINE    "NB4", "the clock chip ticks; the time is "
        lda     NB_RTCCTL
        and     #NB_RTC_LOST
        sta     lost
        beq     @valid
        SAY     "not "
@valid: SAY     "valid"
        ldy     #$D
        jsr     known
        bcs     @end
        lda     areaok
        bne     @area
        SKIP    mnoarea
@end:   rts
@area:  lda     #$11                    ; BUSY: an I2C read (the temperature), done within 1 ms
        sta     NB_RTCREG
        ldx     #0                      ; 256 x 28 cycles: 1 ms
@busy:  lda     NB_RTCCTL
        and     #NB_RTC_BUSY
        beq     @idle
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        dex
        bne     @busy
        jsr     fail
        FLINE   "BUSY stayed set past 1 ms"
        rts
@idle:  lda     NB_SEC                  ; three steps of SEC, a second apart
        sta     sec0
        jsr     mark
        lda     #3
        sta     nstep
@wait:  lda     NB_SEC
        cmp     sec0
        bne     @step
        lda     #<1500
        ldx     #>1500
        jsr     late
        bcc     @wait
        jsr     fail
        FLINE   "SEC didn't change for 1.5 s"
        rts
@step:  sta     sec1
        lda     nstep                   ; the first change starts the count
        cmp     #3
        beq     @first
        jsr     since
        cmp     #<900
        txa
        sbc     #>900
        bcc     @time
        jsr     since
        cmp     #<1101
        txa
        sbc     #>1101
        bcs     @time
@first: lda     sec0                    ; one more, in BCD (59 then 00)
        sed
        clc
        adc     #1
        cld
        cmp     #$60
        bne     @inc
        lda     #0
@inc:   cmp     sec1
        bne     @skew
        lda     sec1
        sta     sec0
        jsr     mark
        dec     nstep
        bne     @wait
        lda     lost
        bne     @lost
        jmp     pass
@lost:  jsr     info
        FLINE   "LOST: the clock chip stopped; set the time (DATE)"
        rts
@time:  jsr     fail
        FLINE   "SEC changed after "
        jsr     since
        jsr     dec16
        SAY     " ms, not a second"
        rts
@skew:  jsr     fail
        FLINE   "SEC went from "
        lda     sec0
        jsr     hex2
        SAY     mto
        lda     sec1
        jmp     hex2

nb5:    LINE    "NB5", "CPU "
        ldy     #$D
        lda     HW_REV,y
        cmp     #HW_NONE
        bne     @card
        SAY     mhzrtc
        ldy     #$D
        jmp     known
@card:  lda     #<NB_SEC
        sta     HW_WATCH
        lda     #>NB_SEC
        sta     HW_WATCH + 1
        lda     #1
        sta     HW_EDGES
        jsr     HW_CLOCK
        bcs     @none
        ldx     #3
@cp:    lda     HW_CYC,x
        sta     hz,x
        dex
        bpl     @cp
        jsr     puthz
        SAY     " Hz by the clock chip"
        ldy     #$D
        jsr     known
        bcs     @end
        jmp     judge
@none:  SAY     mhzrtc
        jsr     fail
        FLINE   "SEC didn't step within 2.3 s"
@end:   rts

nb6:    LINE    "NB6", "CPU "
        lda     areaok
        beq     @no
        lda     area + EV_CLOCKS
        and     #FC_FRAME
        beq     @no
        lda     #<(area + EV_FRAMES)
        sta     HW_WATCH
        lda     #>(area + EV_FRAMES)
        sta     HW_WATCH + 1
        lda     #60
        sta     HW_EDGES
        jsr     HW_CLOCK
        bcs     @none
        jsr     HW_FRAMEHZ
        ldx     #3
@cp:    lda     HW_NUM,x
        sta     hz,x
        dex
        bpl     @cp
        jsr     puthz
        SAY     " Hz by the video frame"
        jmp     judge
@no:    SAY     mhzfrm
        SKIP    "no frame counter"
        rts
@none:  SAY     mhzfrm
        jsr     fail
        FLINE   "no 60 frames within 2.3 s"
        rts

; hz in decimal, then " (+0.000%)" is judge's.
puthz:  ldx     #3
@cp:    lda     hz,x
        sta     HW_NUM,x
        dex
        bpl     @cp
        jmp     decn
; hz against NATIVE: " (+0.000%)", PASS within 0.1%, else FAIL.
judge:  sec                             ; d = hz - NATIVE, and its sign
        lda     hz
        sbc     #<NATIVE
        sta     HW_NUM
        lda     hz + 1
        sbc     #>NATIVE
        sta     HW_NUM + 1
        lda     hz + 2
        sbc     #^NATIVE
        sta     HW_NUM + 2
        lda     hz + 3
        sbc     #0
        sta     HW_NUM + 3
        lda     #'+'
        sta     sign
        bit     HW_NUM + 3
        bpl     @pos
        lda     #'-'
        sta     sign
        ldx     #0                      ; |d|
        ldy     #4
        sec
@neg:   lda     #0
        sbc     HW_NUM,x
        sta     HW_NUM,x
        inx
        dey
        bne     @neg
@pos:   lda     HW_NUM + 3              ; past 2^24: "over 1%"
        jne     @far
        jsr     x100                    ; |d| x 100 / 7,159: thousandths of a percent
        lda     #<7159
        ldx     #>7159
        jsr     HW_DIV
        lda     HW_NUM + 2
        ora     HW_NUM + 3
        bne     @far
        lda     HW_NUM
        sta     milli
        lda     HW_NUM + 1
        sta     milli + 1
        cmp     #>10000
        bcc     @show
        bne     @far
        lda     milli
        cmp     #<10000
        bcs     @far
@show:  SAY     " ("
        lda     sign
        jsr     HW_PUTC
        lda     milli                   ; the percent, then three places
        sta     HW_NUM
        lda     milli + 1
        sta     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        lda     #<1000
        ldx     #>1000
        jsr     HW_DIV
        sta     frac
        stx     frac + 1
        lda     HW_NUM
        ora     #'0'
        jsr     HW_PUTC
        lda     #'.'
        jsr     HW_PUTC
        lda     frac
        ldx     frac + 1
        jsr     three
        SAY     "%)"
        lda     milli + 1               ; within 0.1%: 100 thousandths
        bne     @fail
        lda     milli
        cmp     #101
        bcs     @fail
        jmp     pass
@far:   SAY     " (over 9%)"
@fail:  jmp     fail
; HW_NUM x 100 (x 64 + x 32 + x 4).
x100:   ldx     #3
@cp:    lda     HW_NUM,x
        sta     t4,x
        dex
        bpl     @cp
        jsr     @x4
        jsr     @add                    ; x 4
        jsr     @x2
        jsr     @x2
        jsr     @x2
        jsr     @add                    ; x 32
        jsr     @x2
        jsr     @add                    ; x 64
        ldx     #3
@out:   lda     t8,x
        sta     HW_NUM,x
        dex
        bpl     @out
        rts
@x4:    stz     t8
        stz     t8 + 1
        stz     t8 + 2
        stz     t8 + 3
        jsr     @x2
@x2:    asl     t4
        rol     t4 + 1
        rol     t4 + 2
        rol     t4 + 3
        rts
@add:   clc
        ldx     #0
        ldy     #4
@a:     lda     t8,x
        adc     t4,x
        sta     t8,x
        inx
        dey
        bne     @a
        rts
; A/X (0-999) in three digits.
three:  sta     HW_NUM
        stx     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        lda     #<100
        ldx     #>100
        jsr     HW_DIV
        pha
        lda     HW_NUM
        ora     #'0'
        jsr     HW_PUTC
        pla
        sta     HW_NUM
        stz     HW_NUM + 1
        lda     #10
        ldx     #0
        jsr     HW_DIV
        pha
        lda     HW_NUM
        ora     #'0'
        jsr     HW_PUTC
        pla
        ora     #'0'
        jmp     HW_PUTC

; ---------------------------------------------------------------------------
; MEM: the memory OODOS doesn't own

amem:   lda     #<hmem
        ldx     #>hmem
        ldy     #0
        jsr     heading
        jsr     setbgs
        jsr     m1
        jsr     m2
        jsr     m3
        jsr     m4
        jmp     m5

; M1: the TPA DIAG doesn't use, by the CPU: $4000-$7FFF (block 1, in W1)
; and past the BSS to $BFFC, each address in address, then the march.
m1:     LINE    "M1", "TPA $4000-$7FFF, $"
        jsr     GX_W1BACK               ; block 1 in W1: the TPA is the program's own
        lda     #<M1START2
        ldx     #>M1START2
        jsr     HW_HEX4
        SAY     "-$"
        lda     #<TPAEND
        ldx     #>TPAEND
        jsr     HW_HEX4
        SAY     ", "
        lda     #<(M1LEN2 + $4000)
        ldx     #>(M1LEN2 + $4000)
        jsr     dec16
        SAY     " bytes"
        stz     HW_KEY
        jsr     @r1
        jsr     HW_AIAW
        jsr     @r2
        jsr     HW_AIAW
        jsr     @r1
        jsr     HW_AIAC
        bcs     @bad
        jsr     @r2
        jsr     HW_AIAC
        bcs     @bad
        stz     bgi
@bg:    ldx     bgi
        lda     bgtab,x
        sta     HW_BG
        jsr     @r1
        jsr     HW_MARCH
        bcs     @bad
        jsr     @r2
        jsr     HW_MARCH
        bcs     @bad
        inc     bgi
        lda     bgi
        cmp     nbg
        bne     @bg
        jmp     pass
@bad:   jsr     fail
        FLINE   mblock
        lda     HW_BAD + 1              ; the TPA's blocks 0-2: the CPU address is the physical one
        rol     a
        rol     a
        rol     a
        and     #3
        jsr     dec8
        SAY     " +"
        lda     HW_BAD + 1
        and     #$3F
        tax
        lda     HW_BAD
        jsr     hex4
        SAY     " ("
        lda     HW_BAD
        ldx     HW_BAD + 1
        ldy     #0
        jsr     hex5
        SAY     "): "
        jmp     HW_FAULT
@r1:    stz     HW_PTR                  ; $4000-$7FFF
        lda     #>W1
        sta     HW_PTR + 1
        stz     HW_LEN
        lda     #$40
        sta     HW_LEN + 1
        rts
@r2:    lda     #0                      ; past the BSS, from a page
        sta     HW_PTR
        lda     #>M1START2
        sta     HW_PTR + 1
        lda     #<M1LEN2
        sta     HW_LEN
        lda     #>M1LEN2
        sta     HW_LEN + 1
        rts
M1START2 = (__BSS_RUN__ + __BSS_SIZE__ + $FF) & $FF00
M1LEN2  = TPAEND - M1START2
.assert M1START2 <= SCR, error, "DIAG's BSS runs into its buffers"

; "n blocks, m KB" of list A.
nkb:    jsr     list
        lda     cnt
        jsr     dec8
        SAY     " blocks, "
        lda     cnt
        asl     a
        asl     a
        asl     a
        asl     a
        tay
        lda     #0
        rol     a
        tax
        tya
        jsr     dec16
        SAY     " KB"
        rts

m2:     LINE    "M2", "banks 1-5, "
        lda     #0
        jsr     nkb
        SAY     ": address in address"
        lda     nblk1
        bne     @go
        SKIP    mnofree
        rts
@go:    lda     #0
        jsr     list
        jsr     blkaia
        jmp     blkres

; The list's result: PASS, or FAIL and where (fblk, HW_BAD in W1).
blkres: php
        jsr     GX_W1BACK
        plp
        bcs     @bad
        jmp     pass
@bad:   jsr     fail
        FLINE   ""
        lda     fblk
        jmp     wherew1

m3:     LINE    "M3", "banks 1-5: march, "
        jsr     bgsay
        lda     nblk1
        bne     @go
        SKIP    mnofree
        rts
@go:    lda     #0
        jsr     list
        jsr     blkmarch
        jmp     blkres
; "background $00" or "backgrounds $00 $55 $33 $0F".
bgsay:  lda     full
        bne     @four
        SAY     "background $00"
        rts
@four:  SAY     "backgrounds $00 $55 $33 $0F"
        rts

m4:     LINE    "M4", "banks 9-B, "
        lda     #1
        jsr     nkb
        lda     nblk9
        bne     @go
        SKIP    mnomap
        rts
@go:    lda     #1
        jsr     list
        jsr     blkaia
        bcs     @r
        lda     #1
        jsr     list
        jsr     blkmarch
@r:     jmp     blkres

; M5: the program's card memory through slot 1, a 16 KB card bank at a time.
m5:     LINE    "M5", "card memory "
        jsr     vrange
        bcc     @have
        SAY     "(the program's)"
        SKIP    mnovid
        rts
@have:  lda     vfirst                  ; the first card address: page x 256
        stz     cad
        sta     cad + 1
        lda     vfirst + 1
        sta     cad + 2
        lda     cad
        ldx     cad + 1
        ldy     cad + 2
        jsr     hex6
        lda     #'-'
        jsr     HW_PUTC
        sec                             ; the last: the end's page less one, $FF
        lda     vend
        sbc     #1
        tax
        lda     vend + 1
        sbc     #0
        tay
        lda     #$FF
        jsr     hex6
        SAY     ", "
        lda     nbank                   ; x 16 KB
        asl     a
        asl     a
        asl     a
        asl     a
        tay
        lda     nbank
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        tax
        tya
        jsr     dec16
        SAY     " KB"
        lda     full
        beq     @q
        SAY     ", the march too"
@q:     lda     #<HW_AIAW
        ldx     #>HW_AIAW
        jsr     vbanks
        bcs     @bad
        lda     #<HW_AIAC
        ldx     #>HW_AIAC
        jsr     vbanks
        bcs     @bad
        lda     full
        beq     @ok
        stz     bgi
@bg:    ldx     bgi
        lda     bgtab,x
        sta     HW_BG
        lda     #<HW_MARCH
        ldx     #>HW_MARCH
        jsr     vbanks
        bcs     @bad
        inc     bgi
        lda     bgi
        cmp     #4
        bne     @bg
@ok:    jsr     GX_W1BACK
        jmp     pass
@bad:   jsr     GX_W1BACK
        jsr     fail
        FLINE   "card "
        jsr     cardat
        jmp     HW_FAULT
; "$cccccc: " the card address of HW_BAD in bank fbank's window.
cardat: lda     fbank                   ; bank x $4000 + (HW_BAD - W1)
        lsr     a
        lsr     a
        tay
        lda     fbank
        and     #3
        lsr     a
        ror     a
        ror     a
        sta     tmp
        sec
        lda     HW_BAD + 1
        sbc     #>W1
        ora     tmp
        tax
        lda     HW_BAD
        jsr     hex6
        SAY     ": "
        rts

; The program's card pages (DM_INFO): vfirst, vend (the page past), and
; nbank the 16 KB card banks from vfirst's; carry set with no card.
vrange: lda     #DM_INFO
        sta     infblk
        CALL    O_DMA, infblk
        cmp     #0
        beq     @no
        cmp     #$FF
        beq     @no
        lda     infblk + 13
        sta     vfirst
        lda     infblk + 14
        sta     vfirst + 1
        lda     infblk + 15
        sta     vend
        lda     infblk + 16
        sta     vend + 1
        sec                             ; (vend - vfirst) / 64
        lda     vend
        sbc     vfirst
        sta     tmp
        lda     vend + 1
        sbc     vfirst + 1
        bcc     @no
        ldx     #6
@d:     lsr     a
        ror     tmp
        dex
        bne     @d
        lda     tmp
        sta     nbank
        beq     @no
        lda     gxok
        beq     @no
        clc
        rts
@no:    sec
        rts

; The routine A/X for each card bank of the program's: slot 1 on it
; (GX_VMAP), HW_PTR its CPU address, HW_LEN 16 KB, HW_KEY the bank. Carry
; set at its first error, fbank the bank.
vbanks: sta     eachjmp + 1
        stx     eachjmp + 2
        lda     vfirst                  ; the first bank: the page / 64
        sta     tmp
        lda     vfirst + 1
        ldx     #6
@d:     lsr     a
        ror     tmp
        dex
        bne     @d
        lda     tmp
        sta     fbank
        stz     idx
@b:     lda     idx
        cmp     nbank
        beq     @done
        lda     fbank                   ; its first page: bank x 64
        sta     tmp
        stz     tmp + 1
        ldx     #6
@m:     asl     tmp
        rol     tmp + 1
        dex
        bne     @m
        lda     tmp
        ldx     tmp + 1
        jsr     GX_VMAP
        bcs     @nomap
        sta     HW_PTR
        stx     HW_PTR + 1
        lda     fbank
        sta     HW_KEY
        stz     HW_LEN
        lda     #$40
        sta     HW_LEN + 1
        jsr     eachjmp
        bcs     @r
        inc     fbank
        inc     idx
        bra     @b
@done:  clc
@r:     rts
@nomap: stz     HW_BAD
        lda     #>W1
        sta     HW_BAD + 1
        stz     HW_GOT
        stz     HW_WANT
        sec
        rts

; ---------------------------------------------------------------------------
; DMA (module 8): every transfer through function 83 (and 82 for D7)

admaa:  lda     #<hdma
        ldx     #>hdma
        ldy     #8
        jsr     heading
        jsr     d1
        jsr     d2
        jsr     d3
        jsr     d4
        jsr     d5
        jsr     d6
        jmp     d7

d1:     LINE    "D1", "ID "
        ldy     #8
        lda     HW_IDB,y
        jsr     hex2
        ldy     #8
        jsr     known
        bcs     @r
        jmp     pass
@r:     rts

; A tested block for the DMA tests: tblk (and ublk a second); carry set if
; there are not two.
twoblk: lda     nblk1
        cmp     #2
        bcc     @nine
        lda     blk1
        sta     tblk
        lda     blk1 + 1
        sta     ublk
        clc
        rts
@nine:  lda     nblk9
        cmp     #2
        bcc     @no
        lda     blk9
        sta     tblk
        lda     blk9 + 1
        sta     ublk
        clc
        rts
@no:    sec
        rts
; Carry set (the line ended SKIP) if the DMA tests can't run: no
; controller, or no blocks to map.
dmaok:  ldy     #8
        jsr     known
        bcs     @r
        jsr     twoblk
        bcc     @r
        SKIP    mnomap
        sec
@r:     rts

d2:     LINE    "D2", "2 KB filled by DMA, read back by the CPU"
        jsr     dmaok
        jcs     @r
        jsr     fill2k
        lda     tblk
        jsr     map
        ldx     #0
@b:     lda     W1,x                    ; $5A in all 2 KB
        cmp     #$5A
        bne     @bad
        lda     W1 + $100,x
        cmp     #$5A
        bne     @bad1
        lda     W1 + $200,x
        cmp     #$5A
        bne     @bad2
        lda     W1 + $300,x
        cmp     #$5A
        bne     @bad3
        lda     W1 + $400,x
        cmp     #$5A
        bne     @bad4
        lda     W1 + $500,x
        cmp     #$5A
        bne     @bad5
        lda     W1 + $600,x
        cmp     #$5A
        bne     @bad6
        lda     W1 + $700,x
        cmp     #$5A
        bne     @bad7
        inx
        bne     @b
        jsr     GX_W1BACK
        jmp     pass
@bad:   ldy     #0
        bra     @f
@bad1:  ldy     #1
        bra     @f
@bad2:  ldy     #2
        bra     @f
@bad3:  ldy     #3
        bra     @f
@bad4:  ldy     #4
        bra     @f
@bad5:  ldy     #5
        bra     @f
@bad6:  ldy     #6
        bra     @f
@bad7:  ldy     #7
@f:     sta     HW_GOT
        lda     #$5A
        sta     HW_WANT
        stx     HW_BAD
        tya
        clc
        adc     #>W1
        sta     HW_BAD + 1
        jsr     GX_W1BACK
        jsr     fail
        FLINE   ""
        lda     tblk
        jmp     wherew1
@r:     rts
; DM_FILL: 2,048 bytes of $5A at tblk's +0 (A/X the length: fillN).
fill2k: lda     #<2048
        ldx     #>2048
fillN:  sta     fillblk + 5
        stx     fillblk + 6
        lda     #DM_FILL
        sta     fillblk
        lda     #$5A
        sta     fillblk + 1
        lda     tblk
        jsr     bphys
        sta     fillblk + 2
        stx     fillblk + 3
        sty     fillblk + 4
        CALL    O_DMA, fillblk
        rts

d3:     LINE    "D3", "2 KB copied by DMA between two blocks, both ways"
        jsr     dmaok
        bcs     @r
        lda     tblk                    ; up: tblk to ublk
        ldx     ublk
        ldy     #1
        jsr     copy2k
        bcs     @bad
        lda     ublk                    ; and back, another pattern
        ldx     tblk
        ldy     #2
        jsr     copy2k
        bcs     @bad
        jsr     GX_W1BACK
        jmp     pass
@bad:   jsr     GX_W1BACK
        jsr     fail
        FLINE   ""
        lda     fblk
        jmp     wherew1
@r:     rts
; Block A's first 2 KB written (address in address, key Y), copied by
; DM_COPY to block X's, and checked there: carry set, fblk the block, if
; not.
copy2k: sta     fblk
        stx     cpblk
        sty     HW_KEY
        jsr     map
        jsr     w1run
        jsr     HW_AIAW
        lda     #DM_COPY
        sta     cpyblk
        lda     fblk
        jsr     bphys
        sta     cpyblk + 1
        stx     cpyblk + 2
        sty     cpyblk + 3
        lda     cpblk
        jsr     bphys
        sta     cpyblk + 4
        stx     cpyblk + 5
        sty     cpyblk + 6
        lda     #<2048
        sta     cpyblk + 7
        lda     #>2048
        sta     cpyblk + 8
        CALL    O_DMA, cpyblk
        lda     cpblk
        sta     fblk
        jsr     map
        jsr     w1run
        jmp     HW_AIAC
; HW_PTR W1, HW_LEN 2 KB.
w1run:  stz     HW_PTR
        lda     #>W1
        sta     HW_PTR + 1
        stz     HW_LEN
        lda     #>2048
        sta     HW_LEN + 1
        rts

; D4: DM_FILL timed by the VIA's timer 2: 2,048 bytes and 1,024, the least
; of three each; the difference over 1,024 is the cycles a byte.
d4:     LINE    "D4", "a DMA fill takes "
        jsr     dmaok
        bcc     @go
        rts
@go:    lda     #<2048
        ldx     #>2048
        jsr     least
        sta     tbig
        stx     tbig + 1
        lda     #<1024
        ldx     #>1024
        jsr     least
        sta     tsmall
        stx     tsmall + 1
        sec                             ; x 100 / 1,024: the hundredths
        lda     tbig
        sbc     tsmall
        sta     HW_NUM
        lda     tbig + 1
        sbc     tsmall + 1
        sta     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        jsr     x100
        lda     #<1024
        ldx     #>1024
        jsr     HW_DIV
        lda     HW_NUM
        sta     milli
        lda     HW_NUM + 1
        sta     milli + 1
        bne     @odd                    ; 2.56 or more
        lda     milli
        jsr     hund
        SAY     " cycles a byte"
        lda     milli
        cmp     #175
        bcc     @f
        cmp     #226
        bcs     @f
        jmp     pass
@odd:   SAY     "over 2.55 cycles a byte"
@f:     jmp     fail
; A (0-255) hundredths as n.nn.
hund:   sta     HW_NUM
        stz     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        lda     #100
        ldx     #0
        jsr     HW_DIV
        pha
        lda     HW_NUM
        ora     #'0'
        jsr     HW_PUTC
        lda     #'.'
        jsr     HW_PUTC
        pla
        sta     HW_NUM
        stz     HW_NUM + 1
        lda     #10
        ldx     #0
        jsr     HW_DIV
        pha
        lda     HW_NUM
        ora     #'0'
        jsr     HW_PUTC
        pla
        ora     #'0'
        jmp     HW_PUTC
; The fewest cycles of three DM_FILLs of A/X bytes: A/X.
least:  sta     flen
        stx     flen + 1
        lda     #$FF
        sta     tmin
        sta     tmin + 1
        lda     #3
        sta     nstep
@t:     lda     #$FF                    ; timer 2 from $FFFF
        sta     VIA_T2CL
        sta     VIA_T2CH
        lda     flen
        ldx     flen + 1
        jsr     fillN
        jsr     t2now
        sta     tmp                     ; the cycles: $FFFF less what is left
        txa
        eor     #$FF
        tax
        lda     tmp
        eor     #$FF
        cpx     tmin + 1
        bcc     @less
        bne     @next
        cmp     tmin
        bcs     @next
@less:  sta     tmin
        stx     tmin + 1
@next:  dec     nstep
        bne     @t
        lda     tmin
        ldx     tmin + 1
        rts
; Timer 2 now: A low, X high, of one moment.
t2now:  ldx     VIA_T2CH
        lda     VIA_T2CL
        cpx     VIA_T2CH
        bne     t2now
        rts

; D5: DIAG.COM's own records, by DM_LOAD into a block and by F_READ into
; the TPA: they agree.
d5:     LINE    "D5", "DIAG.COM read by DMA and by the CPU agree"
        jsr     dmaok
        bcc     @go
        rts
@go:    jsr     selfopen
        bcc     @open
        SKIP    mnoself
        rts
@open:  lda     #<SCR                   ; up to 32 records by F_READ into SCR
        sta     dmaad
        lda     #>SCR
        sta     dmaad + 1
        stz     nrec
@rd:    lda     dmaad
        ldx     dmaad + 1
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<selffcb
        ldx     #>selffcb
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        bne     @eof
        inc     nrec
        clc
        lda     dmaad
        adc     #128
        sta     dmaad
        bcc     @nc
        inc     dmaad + 1
@nc:    lda     nrec
        cmp     #32                     ; 4 KB: SCR
        bne     @rd
@eof:   jsr     dmabp                   ; the default DMA address back
        lda     #<selffcb               ; the same records by DM_LOAD, into tblk
        sta     ldblk + 1
        lda     #>selffcb
        sta     ldblk + 2
        stz     ldblk + 3
        stz     ldblk + 4
        stz     ldblk + 5
        lda     nrec
        sta     ldblk + 6
        stz     ldblk + 7
        lda     tblk
        jsr     bphys
        sta     ldblk + 8
        stx     ldblk + 9
        sty     ldblk + 10
        lda     #DM_LOAD
        sta     ldblk
        CALL    O_DMA, ldblk
        cmp     #0
        bne     @refused
        jsr     selfclose
        lda     tblk
        jsr     map
        lda     #<SCR                   ; compare nrec x 128 bytes
        sta     zp
        lda     #>SCR
        sta     zp + 1
        stz     HW_PTR
        lda     #>W1
        sta     HW_PTR + 1
        lda     nrec
        lsr     a
        sta     npg                     ; (pages, and a half page if odd)
        lda     #0
        ror     a
        sta     half
@pg:    lda     npg
        beq     @last
        ldy     #0
@b:     lda     (zp),y
        cmp     (HW_PTR),y
        bne     @bad
        iny
        bne     @b
        inc     zp + 1
        inc     HW_PTR + 1
        dec     npg
        bra     @pg
@last:  ldy     #0
@h:     cpy     half
        beq     @ok
        lda     (zp),y
        cmp     (HW_PTR),y
        bne     @bad
        iny
        bra     @h
@ok:    jsr     GX_W1BACK
        jmp     pass
@bad:   sta     HW_WANT
        lda     (HW_PTR),y
        sta     HW_GOT
        tya
        clc
        adc     HW_PTR
        sta     HW_BAD
        lda     HW_PTR + 1
        adc     #0
        sta     HW_BAD + 1
        jsr     GX_W1BACK
        jsr     fail
        FLINE   "by DMA, "
        lda     tblk
        jmp     wherew1
@refused:
        jsr     selfclose
        jsr     fail
        FLINE   mnoload
        rts
; DIAG.COM opened from the drive it was run from ($0250): carry set if not.
selfopen:
        ldx     #35
@z:     stz     selffcb,x
        dex
        bpl     @z
        lda     BP_WBOOT + $50          ; $0250: the drive the program was loaded from
        sta     selffcb
        ldx     #10
@n:     lda     sdiagcom,x
        sta     selffcb + 1,x
        dex
        bpl     @n
        lda     #<selffcb
        ldx     #>selffcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #4
        rts
selfclose:
        lda     #<selffcb
        ldx     #>selffcb
        ldy     #F_CLOSE
        jmp     BDOS
; The DMA address back at $0280.
dmabp:  lda     #<BP_DMA
        ldx     #>BP_DMA
        ldy     #F_DMAOFF
        jmp     BDOS

; D6: 4 KB to the card's program pages by DM_VWRITE, back by DM_VREAD into
; a tested block, and the same bytes through slot 1.
d6:     LINE    "D6", "4 KB to card memory by DMA, back, and by slot 1"
        jsr     dmaok
        bcs     @r
        jsr     vrange
        bcc     @go
        SKIP    mnovid
@r:     rts
@go:    lda     #$3C
        jsr     pattern                 ; SCR: 4 KB
        jsr     vwrite                  ; to the program's first card page
        jsr     vread                   ; back into tblk
        lda     tblk
        jsr     map
        jsr     cmpw1
        bcs     @port
        lda     vfirst                  ; through slot 1
        ldx     vfirst + 1
        jsr     GX_VMAP
        jsr     cmpw1
        bcs     @slot
        jsr     GX_W1BACK
        jmp     pass
@port:  jsr     GX_W1BACK
        jsr     fail
        FLINE   mcardat
        jsr     w1off
        SAY     " back by DMA: "
        jmp     HW_FAULT
@slot:  jsr     GX_W1BACK
        jsr     fail
        FLINE   mcardat
        jsr     w1off
        SAY     " by slot 1: "
        jmp     HW_FAULT
; HW_BAD's offset in W1, in hex.
w1off:  sec
        lda     HW_BAD + 1
        sbc     #>W1
        tax
        lda     HW_BAD
        jmp     HW_HEX4
; SCR's 4 KB: byte i = i's low XOR its high XOR A.
pattern:
        sta     tmp
        lda     #<SCR
        sta     zp
        lda     #>SCR
        sta     zp + 1
        ldx     #16
@pg:    ldy     #0
@b:     tya
        eor     zp + 1
        eor     tmp
        sta     (zp),y
        iny
        bne     @b
        inc     zp + 1
        dex
        bne     @pg
        rts
; SCR's 4 KB to the program's first card page (DM_VWRITE); back into tblk.
vwrite: lda     #DM_VWRITE
        sta     vblk
        lda     #<SCR
        sta     vblk + 1
        lda     #>SCR
        sta     vblk + 2
        stz     vblk + 3
        stz     vblk + 4
        lda     vfirst
        sta     vblk + 5
        lda     vfirst + 1
        sta     vblk + 6
        bra     vgo
vread:  lda     tblk                    ; into tblk's +0
        jsr     bphys
        sta     vblk + 4
        stx     vblk + 5
        sty     vblk + 6
        lda     #DM_VREAD
        sta     vblk
        stz     vblk + 1
        lda     vfirst
        sta     vblk + 2
        lda     vfirst + 1
        sta     vblk + 3
vgo:    stz     vblk + 7
        lda     #$10
        sta     vblk + 8
        CALL    O_DMA, vblk
        rts
; SCR against W1's first 4 KB: carry set and HW_BAD the address in W1, HW_GOT
; and HW_WANT, if they differ.
cmpw1:  stz     HW_PTR
        lda     #>W1
        sta     HW_PTR + 1
        lda     #<SCR
        sta     zp
        lda     #>SCR
        sta     zp + 1
        ldx     #16
@pg:    ldy     #0
@b:     lda     (zp),y
        cmp     (HW_PTR),y
        bne     @bad
        iny
        bne     @b
        inc     zp + 1
        inc     HW_PTR + 1
        dex
        bne     @pg
        clc
        rts
@bad:   sta     HW_WANT
        lda     (HW_PTR),y
        sta     HW_GOT
        sty     HW_BAD
        lda     HW_PTR + 1
        sta     HW_BAD + 1
        sec
        rts

; D7: 4 KB into the sound card's RAM by SN_LOAD (channel 2), summed by the
; 6510 there.
d7:     LINE    "D7", "4 KB to the sound card by DMA, summed by its 6510"
        ldy     #8
        jsr     known
        bcs     @r
        jsr     sndup
        bcc     @go
@r:     rts
@go:    lda     #$C3
        jsr     pattern
        lda     #<SCR
        sta     snload + 1
        lda     #>SCR
        sta     snload + 2
        stz     snload + 3
        stz     snload + 4              ; to the card's $1000
        lda     #$10
        sta     snload + 5
        stz     snload + 6
        lda     #$10
        sta     snload + 7
        lda     #SN_LOAD
        sta     snload
        CALL    O_SOUND, snload
        cmp     #0
        jne     @refused
        lda     #<SCR                   ; the sum here
        sta     zp
        lda     #>SCR
        sta     zp + 1
        stz     sum
        stz     sum + 1
        ldx     #16
@pg:    ldy     #0
@b:     lda     (zp),y
        clc
        adc     sum
        sta     sum
        bcc     @nc
        inc     sum + 1
@nc:    iny
        bne     @b
        inc     zp + 1
        dex
        bne     @pg
        lda     #C65_SUM                ; and there
        sta     cmdb
        stz     cmdb + 1
        lda     #$10
        sta     cmdb + 2
        stz     cmdb + 3
        lda     #$10
        sta     cmdb + 4
        ldx     #5
        jsr     mbcmd
        lda     #<1000
        ldx     #>1000
        jsr     mbget
        bcs     @mute
        sta     got16
        lda     #<1000
        ldx     #>1000
        jsr     mbget
        bcs     @mute
        sta     got16 + 1
        jsr     sndrel
        lda     got16
        cmp     sum
        bne     @diff
        lda     got16 + 1
        cmp     sum + 1
        bne     @diff
        jmp     pass
@diff:  jsr     fail
        FLINE   "the 6510's sum "
        lda     got16
        ldx     got16 + 1
        jsr     hex4
        SAY     mexpect
        lda     sum
        ldx     sum + 1
        jmp     hex4
@mute:  jsr     sndrel
        jsr     fail
        FLINE   mmute
        rts
@refused:
        jsr     sndrel
        jsr     fail
        FLINE   "function 82's SN_LOAD refused"
        rts

; ---------------------------------------------------------------------------
; The sound card's parts: claimed, the 6510's program loaded and started

; The parts this revision has claimed (sndcl), card6510.bin loaded at
; C65_ORG and started, and its $A5 waited for (0.5 s): carry clear. Carry
; set and the line ended SKIP if no card or the parts are held, FAIL if the
; 6510 didn't start.
sndup:  ldy     #$A
        jsr     known
        bcc     @card
        rts
@card:  lda     areaok
        bne     @area
        SKIP    mnombox
        sec
        rts
@area:  lda     held
        jne     @ok
        lda     #SC_6510                ; revision 2: the stream and the ES5503 too
        ldy     #$A
        ldx     HW_REV,y
        cpx     #2
        bcc     @rev1
        lda     #SC_6510 | SC_STREAM | SC_DOC
@rev1:  sta     clblk + 1
        sta     sndcl
        lda     #SN_CLAIM
        sta     clblk
        CALL    O_SOUND, clblk
        cmp     #0
        beq     @claimed
        SKIP    minuse
        sec
        rts
@claimed:
        inc     held
        jsr     evflush
        lda     #<card6510              ; the program, at C65_ORG
        sta     snload + 1
        lda     #>card6510
        sta     snload + 2
        stz     snload + 3
        lda     #<C65_ORG
        sta     snload + 4
        lda     #>C65_ORG
        sta     snload + 5
        lda     #<CARD6510_LEN
        sta     snload + 6
        lda     #>CARD6510_LEN
        sta     snload + 7
        lda     #SN_LOAD
        sta     snload
        CALL    O_SOUND, snload
        cmp     #0
        bne     @nogo
        lda     #SN_START
        sta     stblk
        lda     #<C65_ORG
        sta     stblk + 1
        lda     #>C65_ORG
        sta     stblk + 2
        stz     stblk + 3
        CALL    O_SOUND, stblk
        cmp     #0
        bne     @nogo
        lda     #<500
        ldx     #>500
        jsr     mbget
        bcs     @nogo
        cmp     #C65_HELLO
        bne     @nogo
@ok:    clc
        rts
@nogo:  jsr     sndrel
        jsr     fail
        FLINE   "no $A5 from the 6510 within 0.5 s"
        sec
        rts
; The parts given back, if DIAG holds them (SN_RELEASE stops each).
sndrel: lda     held
        beq     @r
        stz     held
        lda     #SN_RELEASE
        sta     clblk
        lda     sndcl
        sta     clblk + 1
        CALL    O_SOUND, clblk
@r:     rts

; ---------------------------------------------------------------------------
; VIDEO (module 9, revision 2)

avideo: lda     #<hvideo
        ldx     #>hvideo
        ldy     #9
        jsr     heading
        jsr     v1
        jsr     v2
        jsr     v3
        jsr     v4
        jsr     v5
        jmp     v6

v1:     LINE    "V1", "ID "
        ldy     #9
        lda     HW_IDB,y
        jsr     hex2
        ldy     #9
        jsr     known
        bcs     @r
        SAY     ", MEMSIZE "
        lda     VIDEO_MEMSIZE
        sta     tmp
        jsr     dec8
        SAY     " ("
        lda     tmp                     ; x 64 KB
        sta     HW_NUM
        stz     HW_NUM + 1
        ldx     #6
@k:     asl     HW_NUM
        rol     HW_NUM + 1
        dex
        bne     @k
        lda     HW_NUM
        ldx     HW_NUM + 1
        jsr     dec16
        SAY     " KB)"
        lda     tmp
        cmp     #28
        bcc     @small
        jmp     pass
@small: jsr     fail
        FLINE   "MEMSIZE under 28: too little for OODOS and Apple II mode"
@r:     rts

; Carry set (the line ended) unless the video card is revision 2.
vidok:  ldy     #9
        jsr     known
        bcs     @r
        ldy     #9
        lda     HW_REV,y
        cmp     #2
        bne     @old
        clc
        rts
@old:   lda     #<srevn
        ldx     #>srevn
        jsr     HW_PUTS
        lda     #HL_SKIP
        jsr     HW_LINE
        jsr     CM_EOL
        sec
@r:     rts

; V2: BORDER and layer 1's registers, layer 1 off, written and read back and
; put back, in one vertical blank.
v2:     LINE    "V2", "BORDER and layer 1's registers read back"
        jsr     vidok
        bcs     @r
        lda     VIDEO_CTRL
        and     #$04
        beq     @off
        SKIP    "layer 1 is on"
@r:     rts
@off:   jsr     vblank
        ldx     #NVREG - 1              ; kept
@keep:  ldy     vregs,x
        lda     VIDEO,y
        sta     vkept,x
        dex
        bpl     @keep
        lda     #$A5
        jsr     @try
        bcs     @bad
        lda     #$5A
        jsr     @try
        bcs     @bad
        jsr     @back
        jmp     pass
@bad:   jsr     @back
        jsr     fail
        FLINE   "register $C4"
        ldx     vbad
        lda     vregs,x
        jsr     HW_HEX2
        SAY     ": "
        jmp     HW_FAULT
@back:  ldx     #NVREG - 1
@b:     ldy     vregs,x
        lda     vkept,x
        sta     VIDEO,y
        dex
        bpl     @b
        rts
@try:   sta     tmp                     ; A, masked, into each; carry set at one that doesn't read back
        ldx     #NVREG - 1
@w:     ldy     vregs,x
        lda     tmp
        and     vmask,x
        sta     VIDEO,y
        dex
        bpl     @w
        ldx     #NVREG - 1
@c:     ldy     vregs,x
        lda     tmp
        and     vmask,x
        sta     HW_WANT
        lda     VIDEO,y
        sta     HW_GOT
        cmp     HW_WANT
        bne     @no
        dex
        bpl     @c
        clc
        rts
@no:    stx     vbad
        sec
        rts
; The start of a vertical blank (STATUS bit 7 rising), or about 2 frames.
vblank: ldx     #0
        ldy     #0
@in:    bit     VIDEO_STATUS            ; out of the blank first
        bpl     @out
        dex
        bne     @in
        dey
        bne     @in
@out:   ldx     #0
        ldy     #0
@w:     bit     VIDEO_STATUS
        bmi     @r
        dex
        bne     @w
        dey
        bne     @w
@r:     rts

; V3: the beam: LINE seen from 0 to 524, and a frame (two edges of
; STATUS's vertical blank) every FRAME cycles, within 0.1%, over 10.
v3:     LINE    "V3", "the beam: lines 0-"
        jsr     vidok
        bcc     @go
        rts
@go:    lda     #$FF                    ; the least and most LINE over some frames
        sta     lmin
        sta     lmin + 1
        stz     lmax
        stz     lmax + 1
        jsr     mark
@s:     ldx     VIDEO_LINE + 1          ; one moment's: the high byte on both sides
        lda     VIDEO_LINE
        cpx     VIDEO_LINE + 1
        bne     @s
        sta     tmp
        stx     tmp + 1
        cmp     lmin                    ; tmp < lmin?
        txa
        sbc     lmin + 1
        bcs     @nmin
        lda     tmp
        sta     lmin
        lda     tmp + 1
        sta     lmin + 1
@nmin:  lda     lmax                    ; lmax < tmp?
        cmp     tmp
        lda     lmax + 1
        sbc     tmp + 1
        bcs     @nmax
        lda     tmp
        sta     lmax
        lda     tmp + 1
        sta     lmax + 1
@nmax:  lda     #<60
        ldx     #>60
        jsr     late
        bcc     @s
        lda     lmax
        ldx     lmax + 1
        jsr     dec16
        SAY     ", a frame each "
        lda     #<VIDEO_STATUS
        sta     HW_WATCH
        lda     #>VIDEO_STATUS
        sta     HW_WATCH + 1
        lda     #20
        sta     HW_EDGES
        jsr     HW_CLOCK
        jcs     @none
        lda     #<10                    ; ten frames: / 10
        ldx     #>10
        ldy     #3
@cp:    pha
        lda     HW_CYC,y
        sta     HW_NUM,y
        pla
        dey
        bpl     @cp
        jsr     HW_DIV
        lda     HW_NUM
        sta     fcyc
        lda     HW_NUM + 1
        sta     fcyc + 1
        lda     HW_NUM + 2
        sta     fcyc + 2
        stz     HW_NUM + 3
        jsr     decn
        SAY     " cycles"
        lda     lmin                    ; 0 to 524
        ora     lmin + 1
        bne     @lines
        lda     lmax
        cmp     #<524
        bne     @lines
        lda     lmax + 1
        cmp     #>524
        bne     @lines
        sec                             ; |fcyc - FRAME| <= 119
        lda     fcyc
        sbc     #<FRAME
        sta     tmp
        lda     fcyc + 1
        sbc     #>FRAME
        sta     tmp + 1
        lda     fcyc + 2
        sbc     #^FRAME
        bpl     @pos
        sec                             ; negative: |d|
        lda     #0
        sbc     tmp
        sta     tmp
        lda     #0
        sbc     tmp + 1
        sta     tmp + 1
        lda     #0
@pos:   ora     tmp + 1
        bne     @slow
        lda     tmp
        cmp     #120
        bcs     @slow
        jmp     pass
@slow:  jsr     fail
        FLINE   "not within 0.1% of 119,437"
        rts
@lines: jsr     fail
        FLINE   "LINE went from "
        lda     lmin
        ldx     lmin + 1
        jsr     dec16
        SAY     mto
        lda     lmax
        ldx     lmax + 1
        jsr     dec16
        SAY     ", not 0 to 524"
        rts
@none:  jsr     fail
        FLINE   "no vertical blank seen"
        rts

; V4: a pattern through slot 1, read through the port (DM_VREAD, into a
; tested block); another through the port (DM_VWRITE), read through slot 1.
v4:     LINE    "V4", "slot 1 and the card's port agree, both ways"
        jsr     vidok
        bcs     @r
        jsr     twoblk
        bcs     @nob
        jsr     vrange
        bcc     @go
@nob:   SKIP    mnovmem
@r:     rts
@go:    lda     #$96                    ; SCR's pattern written through slot 1
        jsr     pattern
        lda     vfirst
        ldx     vfirst + 1
        jsr     GX_VMAP
        stz     HW_PTR
        lda     #>W1
        sta     HW_PTR + 1
        lda     #<SCR
        sta     zp
        lda     #>SCR
        sta     zp + 1
        ldx     #16
@pg:    ldy     #0
@b:     lda     (zp),y
        sta     (HW_PTR),y
        iny
        bne     @b
        inc     zp + 1
        inc     HW_PTR + 1
        dex
        bne     @pg
        jsr     vread                   ; through the port, into tblk
        lda     tblk
        jsr     map
        jsr     cmpw1
        bcs     @bad1
        lda     #$69                    ; the other way
        jsr     pattern
        jsr     vwrite
        lda     vfirst
        ldx     vfirst + 1
        jsr     GX_VMAP
        jsr     cmpw1
        bcs     @bad2
        jsr     GX_W1BACK
        jmp     pass
@bad1:  jsr     GX_W1BACK
        jsr     fail
        FLINE   "slot 1 to the port, card +$"
        bra     @at
@bad2:  jsr     GX_W1BACK
        jsr     fail
        FLINE   "the port to slot 1, card +$"
@at:    jsr     w1off
        SAY     ": "
        jmp     HW_FAULT

v5:     LINE    "V5", "card memory past MEMSIZE"
        SKIP    "the ROM core's: C812R"
        rts

; V6 (/I): a test card on layer 1: colour bars and a grid, over the text.
v6:     LINE    "V6", "a test card on layer 1"
        lda     inter
        bne     @i
        SKIP    "/I"
        rts
@i:     jsr     vidok
        bcs     @r
        jsr     vrange
        bcc     @go
        SKIP    mnovmem
@r:     rts
@go:    jsr     testcard                ; SCR: 80 x 30 cells, the bottom 6 rows clear
        lda     #DM_VWRITE              ; to the program's first card pages
        sta     vblk
        lda     #<SCR
        sta     vblk + 1
        lda     #>SCR
        sta     vblk + 2
        stz     vblk + 3
        stz     vblk + 4
        lda     vfirst
        sta     vblk + 5
        lda     vfirst + 1
        sta     vblk + 6
        lda     #<4800
        sta     vblk + 7
        lda     #>4800
        sta     vblk + 8
        CALL    O_DMA, vblk
        lda     vfirst                  ; layer 1: text 80 x 30, the console's font
        sta     GX_V
        lda     vfirst + 1
        sta     GX_V + 1
        lda     #<(VM_FONT >> 8)
        sta     GX_R
        lda     #>(VM_FONT >> 8)
        sta     GX_R + 1
        lda     #1
        ldx     #0
        jsr     GX_LAYER
        lda     #$06                    ; layers 0 and 1 on
        jsr     GX_SHOW
        inc     shown
        jsr     CM_EOL
        jsr     yesno
        php
        lda     #$02                    ; layer 1 off again
        jsr     GX_SHOW
        stz     shown
        plp
        bcc     @no
        LINE    lv6
        jmp     pass
@no:    LINE    lv6
        jmp     fail
; The test card into SCR: rows 0-23 of 80 cells, a character and its
; attribute: a bar of each of the 15 colours, a '+' grid every 8 columns
; and 6 rows; rows 24-29 0 (transparent), for the question.
testcard:
        lda     #<SCR
        sta     zp
        lda     #>SCR
        sta     zp + 1
        stz     row
@row:   stz     col
@cell:  lda     row
        cmp     #24
        bcs     @clear
        lda     col                     ; the bar: col / 5 + 1 (1-16; 16 is 15)
        ldx     #0
@d5:    cmp     #5
        bcc     @bar
        sbc     #5
        inx
        bra     @d5
@bar:   inx
        cpx     #16
        bcc     @c
        ldx     #15
@c:     txa
        asl     a
        asl     a
        asl     a
        asl     a
        ora     #$0F                    ; white on the bar's colour
        sta     tmp
        ldx     row                     ; the grid: rows 0, 6, 12, 18, columns 0, 8 ...
        cpx     #0
        beq     @g
        cpx     #6
        beq     @g
        cpx     #12
        beq     @g
        cpx     #18
        beq     @g
        lda     col
        and     #7
        bne     @sp
        lda     #'|'
        bra     @put
@sp:    lda     #' '
        bra     @put
@g:     lda     col
        and     #7
        bne     @dash
        lda     #'+'
        bra     @put
@dash:  lda     #'-'
@put:   sta     (zp)
        ldy     #1
        lda     tmp
        sta     (zp),y
        bra     @next
@clear: lda     #0
        sta     (zp)
        ldy     #1
        sta     (zp),y
@next:  clc
        lda     zp
        adc     #2
        sta     zp
        bcc     @nc
        inc     zp + 1
@nc:    inc     col
        lda     col
        cmp     #80
        bne     @cell
        inc     row
        lda     row
        cmp     #30
        jne     @row
        rts

; A question's answer: "Y or N? " then a line (CM_ASK): carry set for Y.
yesno:  jsr     CM_FLUSH
        lda     #<syn
        ldx     #>syn
        jsr     CM_PUTS
        jmp     CM_ASK                  ; (its line ended: an LF owed)

; ---------------------------------------------------------------------------
; SOUND (module A): the parts claimed, or every test skipped "in use"

asound: lda     #<hsound
        ldx     #>hsound
        ldy     #$A
        jsr     heading
        jsr     s1
        jsr     s2                      ; (sndup: claimed, loaded, started)
        jsr     s3
        jsr     s4
        jsr     s5
        jsr     s6
        jsr     s7
        jsr     s8
        jmp     sndrel

s1:     LINE    "S1", "ID "
        ldy     #$A
        lda     HW_IDB,y
        jsr     hex2
        ldy     #$A
        jsr     known
        bcs     @r
        lda     #SN_INFO                ; function 82 says the same revision
        sta     infblk
        CALL    O_SOUND, infblk
        sta     tmp
        stx     tmp + 1
        SAY     "; function 82: revision "
        lda     tmp
        jsr     dec8
        SAY     ", CODECS "
        lda     tmp + 1
        jsr     hex2
        lda     tmp
        ldy     #$A
        cmp     HW_REV,y
        bne     @bad
        cmp     #2
        bne     @ok
        lda     tmp + 1
        cmp     #$83
        bne     @bad
        lda     SOUND_CODECS
        cmp     #$83
        bne     @bad
@ok:    jmp     pass
@bad:   jmp     fail
@r:     rts

; Carry set (the line ended) if the sound tests can't go on: sndst, set by
; S2, says so (1 skipped already, 2 failed).
sndgo:  lda     sndst
        beq     @ok
        cmp     #1
        beq     @skip
        SKIP    "the 6510 didn't start"
        sec
        rts
@skip:  ldy     #$A
        lda     HW_REV,y
        cmp     #HW_NONE
        beq     @nocard
        lda     areaok
        beq     @noarea
        SKIP    minuse
        sec
        rts
@nocard:
        SKIP    mnocard
        sec
        rts
@noarea:
        SKIP    mnombox
        sec
        rts
@ok:    clc
        rts

s2:     LINE    "S2", "the 6510 runs the test program and answers $A5"
        stz     sndst
        lda     HW_TALLY + HL_SKIP
        sta     tmp
        lda     HW_TALLY + HL_FAIL
        sta     tmp + 1
        jsr     sndup
        bcs     @no
        jmp     pass
@no:    lda     #2                      ; FAIL said, or SKIP
        ldx     HW_TALLY + HL_FAIL
        cpx     tmp + 1
        bne     @st
        lda     #1
@st:    sta     sndst
        rts

s3:     LINE    "S3", "the mailbox both ways: 256 bytes echoed"
        jsr     sndgo
        bcs     @r
        stz     idx
@b:     lda     #C65_ECHO
        sta     cmdb
        lda     idx
        sta     cmdb + 1
        ldx     #2
        jsr     mbcmd
        lda     #<500
        ldx     #>500
        jsr     mbget
        bcs     @mute
        cmp     idx
        bne     @bad
        inc     idx
        bne     @b
        jmp     pass
@bad:   sta     HW_GOT
        lda     idx
        sta     HW_WANT
        jsr     fail
        FLINE   "echo: "
        jmp     HW_FAULT
@mute:  jsr     fail
        FLINE   "no echo of byte "
        lda     idx
        jmp     dec8
@r:     rts

s4:     LINE    "S4", "the card's RAM $0600-$FFF9 by the 6510"
        lda     full
        beq     @q
        SAY     ", march"
@q:     jsr     sndgo
        bcs     @r
        lda     #C65_TEST
        sta     cmdb
        lda     #<C65_FIRST
        sta     cmdb + 1
        lda     #>C65_FIRST
        sta     cmdb + 2
        lda     #<$FFFA
        sta     cmdb + 3
        lda     #>$FFFA
        sta     cmdb + 4
        lda     full                    ; f bit 0: the march (/FULL)
        sta     cmdb + 5
        ldx     #6
        jsr     mbcmd
        ldx     #0
@five:  phx
        lda     #<30000                 ; up to 30 s (the march at the 6510's 1 MHz)
        ldx     #>30000
        jsr     mbget
        plx
        bcs     @mute
        sta     res5,x
        inx
        cpx     #5
        bne     @five
        lda     res5
        bne     @bad
        jmp     pass
@bad:   lda     res5 + 3
        sta     HW_GOT
        lda     res5 + 4
        sta     HW_WANT
        jsr     fail
        FLINE   "card RAM "
        lda     res5 + 1
        ldx     res5 + 2
        jsr     hex4
        SAY     ": "
        jmp     HW_FAULT
@mute:  jsr     fail
        FLINE   mmute
@r:     rts

s5:     LINE    "S5", "SID A's and SID B's voice 3: OSC3 and ENV3"
        jsr     sndgo
        bcs     @r
        lda     #0
        jsr     @sid
        bcs     @mute
        sta     sida
        lda     #1
        jsr     @sid
        bcs     @mute
        sta     sidb
        and     sida
        cmp     #3
        bne     @bad
        jmp     pass
@bad:   jsr     fail
        FLINE   "SID A "
        lda     sida
        jsr     @say
        SAY     "; SID B "
        lda     sidb
        jmp     @say
@mute:  jsr     fail
        FLINE   mmute
@r:     rts
@sid:   sta     cmdb + 1
        lda     #C65_SID
        sta     cmdb
        ldx     #2
        jsr     mbcmd
        lda     #<500
        ldx     #>500
        jmp     mbget
@say:   pha
        and     #1
        bne     @o
        SAY     "OSC3 still, "
        bra     @e
@o:     SAY     "OSC3 moved, "
@e:     pla
        and     #2
        bne     @up
        SAY     "ENV3 flat"
        rts
@up:    SAY     "ENV3 rose"
        rts

; S6 (/FULL, revision 2): wave RAM, 128 KB through DOCDATA with AUTOINC,
; the ES5503 claimed and its oscillators halted.
s6:     LINE    "S6", "wave RAM, 128 KB, through DOCDATA"
        lda     full
        bne     @f
        SKIP    "/FULL"
        rts
@f:     ldy     #$A
        lda     HW_REV,y
        cmp     #2
        beq     @rev2
        cmp     #HW_NONE
        beq     @go                     ; (sndgo says no card)
        SKIP    "revision 1: no ES5503"
        rts
@rev2:
@go:    jsr     sndgo
        bcs     @r
        lda     SOUND_DOCCTL
        sta     dockept
        stz     SOUND_DOCCTL            ; the registers, not stepped: every oscillator halted
        ldx     #$A0
@halt:  stx     SOUND_DOCADDR
        stz     SOUND_DOCADDR + 1
        lda     #$01
        sta     SOUND_DOCDATA
        inx
        cpx     #$C0
        bne     @halt
        lda     #SOUND_DOC_RAM | SOUND_DOC_AUTOINC
        sta     SOUND_DOCCTL
        lda     #<SOUND_DOCDATA
        sta     HW_PORT
        lda     #>SOUND_DOCDATA
        sta     HW_PORT + 1
        lda     #<512
        sta     HW_PLEN
        lda     #>512
        sta     HW_PLEN + 1
        stz     HW_KEY
        jsr     @zero
        jsr     HW_PAIAW
        jsr     @zero
        jsr     HW_PAIAC
        php
        lda     dockept
        sta     SOUND_DOCCTL
        plp
        bcs     @bad
        jmp     pass
@bad:   jsr     fail
        FLINE   "wave RAM $"
        lda     HW_BAD
        ldx     HW_BAD + 1
        ldy     HW_BAD + 2
        jsr     HW_HEX6
        SAY     ": "
        jmp     HW_FAULT
@r:     rts
@zero:  stz     SOUND_DOCADDR
        stz     SOUND_DOCADDR + 1
        stz     SOUND_DOCBANK
        rts

; S7 (revision 2): a raw PCM stream of silence at 8,000 Hz: buffering on
; the first 2 KB, playing on the next, ended after; POS against the
; millisecond counter (the CPU's clock, which NB5 holds to the DS3231).
s7:     LINE    "S7", "a PCM stream at 8,000 Hz: "
        ldy     #$A
        lda     HW_REV,y
        cmp     #2
        beq     @go
        cmp     #HW_NONE
        beq     @go
        SAY     mfps
        SKIP    "revision 1: no stream"
        rts
@go:    jsr     sndgo
        bcc     @run
        rts
@run:   lda     #<SCR                   ; 4 KB of silence
        sta     zp
        lda     #>SCR
        sta     zp + 1
        ldx     #16
        lda     #$80
@pg:    ldy     #0
@sb:    sta     (zp),y
        iny
        bne     @sb
        inc     zp + 1
        dex
        bne     @pg
        lda     #SN_STREAM
        sta     strblk
        stz     strblk + 1              ; raw PCM, 8,000 Hz, mono, 8 bits
        lda     #<8000
        sta     strblk + 2
        lda     #>8000
        sta     strblk + 3
        stz     strblk + 4
        CALL    O_SOUND, strblk
        cmp     #0
        jne     @refused
        lda     #>SCR
        jsr     @buf
        jsr     snstat
        sta     st1
        lda     #>(SCR + $800)
        jsr     @buf
        lda     #SN_END
        ldx     #0
        ldy     #O_SOUND
        jsr     BDOS
        stz     seen
        stz     t1 + 1                  ; t1: 0 not yet playing
        jsr     mark
        lda     t0
        sta     tstart
        lda     t0 + 1
        sta     tstart + 1
@poll:  jsr     snstat
        cmp     #5
        beq     @ended
        cmp     #2
        bne     @late
        lda     seen
        bne     @meas
        inc     seen                    ; the first sight of playing: the start
        jsr     mark
        ldx     #3
@p0:    lda     stblk2 + 5,x
        sta     pos0,x
        dex
        bpl     @p0
        bra     @late
@meas:  lda     seen                    ; 400 ms on: the frames since
        cmp     #2
        bcs     @late
        lda     #<400
        ldx     #>400
        jsr     late
        bcc     @late
        inc     seen
        jsr     since
        sta     dms
        stx     dms + 1
        sec
        ldx     #0
        ldy     #4
@d:     lda     stblk2 + 5,x
        sbc     pos0,x
        sta     HW_NUM,x
        inx
        dey
        bne     @d
@late:  lda     t0                      ; 3 s at most
        pha
        lda     t0 + 1
        pha
        lda     tstart
        sta     t0
        lda     tstart + 1
        sta     t0 + 1
        lda     #<3000
        ldx     #>3000
        jsr     late
        pla
        sta     t0 + 1
        pla
        sta     t0
        bcc     @poll
        SAY     "it never ended"
        jmp     fail
@ended: lda     seen
        cmp     #2
        bcc     @short
        jsr     x1000                   ; frames x 1,000 / ms
        lda     dms
        ldx     dms + 1
        jsr     HW_DIV
        lda     HW_NUM
        sta     rate
        lda     HW_NUM + 1
        sta     rate + 1
        ldx     HW_NUM + 1
        lda     HW_NUM
        jsr     dec16
        SAY     " frames a second"
        lda     st1
        cmp     #1
        bne     @nobuf
        sec                             ; 7,960-8,040
        lda     rate
        sbc     #<7960
        tax
        lda     rate + 1
        sbc     #>7960
        bcc     @off
        bne     @off
        cpx     #81
        bcs     @off
        jmp     pass
@off:   jsr     fail
        FLINE   "not within 0.5% of 8,000"
        rts
@nobuf: jsr     fail
        FLINE   "STATE after the first 2 KB "
        lda     st1
        jsr     dec8
        SAY     ", not 1 (buffering)"
        rts
@short: SAY     "ended before 400 ms of it"
        jmp     fail
@refused:
        SAY     mfps
        jsr     fail
        FLINE   "function 82's SN_STREAM refused"
        rts
@buf:   sta     bufblk + 2              ; 2 KB at A's page, again while busy
        stz     bufblk + 1
        stz     bufblk + 3
        stz     bufblk + 4
        lda     #>2048
        sta     bufblk + 5
@try:   lda     #SN_BUFFER
        sta     bufblk
        CALL    O_SOUND, bufblk
        cmp     #SN_BUSY
        beq     @try
        rts
; SN_STATUS into stblk2: A the state.
snstat: lda     #SN_STATUS
        sta     stblk2
        CALL    O_SOUND, stblk2
        rts
; HW_NUM x 1,000 (x 1,024 - x 16 - x 8).
x1000:  ldx     #3
@cp:    lda     HW_NUM,x
        sta     t4,x
        sta     t8,x
        dex
        bpl     @cp
        ldy     #10                     ; t8 = x 1,024
@s10:   asl     t8
        rol     t8 + 1
        rol     t8 + 2
        rol     t8 + 3
        dey
        bne     @s10
        ldy     #3                      ; t4 = x 8
@s3:    asl     t4
        rol     t4 + 1
        rol     t4 + 2
        rol     t4 + 3
        dey
        bne     @s3
        jsr     @sub                    ; - x 8
        asl     t4
        rol     t4 + 1
        rol     t4 + 2
        rol     t4 + 3
        jsr     @sub                    ; - x 16
        ldx     #3
@out:   lda     t8,x
        sta     HW_NUM,x
        dex
        bpl     @out
        rts
@sub:   sec
        ldx     #0
        ldy     #4
@b:     lda     t8,x
        sbc     t4,x
        sta     t8,x
        inx
        dey
        bne     @b
        rts

; S8 (/I): a tone on each SID, then the ES5503's, and the user's word.
s8:     LINE    "S8", "a tone on SID A, SID B"
        ldy     #$A
        lda     HW_REV,y
        cmp     #2
        bne     @nodoc
        SAY     " and the ES5503"
@nodoc: lda     inter
        bne     @i
        SKIP    "/I"
        rts
@i:     jsr     sndgo
        bcc     @go
        rts
@go:    jsr     CM_EOL
        lda     #0
        jsr     @tone
        lda     #1
        jsr     @tone
        ldy     #$A
        lda     HW_REV,y
        cmp     #2
        bne     @ask
        jsr     doctone
@ask:   jsr     yesno
        php
        LINE    "S8", "the tones heard"
        plp
        bcc     @no
        jmp     pass
@no:    jmp     fail
@tone:  sta     cmdb + 1                ; SID A's, then off after 0.5 s
        sta     tmp
        lda     #C65_TONE
        sta     cmdb
        lda     #1
        sta     cmdb + 2
        ldx     #3
        jsr     mbcmd
        lda     #<500
        ldx     #>500
        jsr     mbget
        lda     #<500
        ldx     #>500
        jsr     delay
        lda     #C65_TONE
        sta     cmdb
        lda     tmp
        sta     cmdb + 1
        stz     cmdb + 2
        ldx     #3
        jsr     mbcmd
        lda     #<500
        ldx     #>500
        jmp     mbget
; Oscillator 0 of the ES5503 on a 256-byte triangle at wave RAM $00000 for
; 0.5 s, then halted.
doctone:
        lda     #SOUND_DOC_RAM | SOUND_DOC_AUTOINC
        sta     SOUND_DOCCTL
        stz     SOUND_DOCADDR
        stz     SOUND_DOCADDR + 1
        stz     SOUND_DOCBANK
        ldx     #0                      ; $40 up to $BF and down, never $00
@w:     txa
        bmi     @dn
        lsr     a
        ora     #$40
        bra     @put
@dn:    eor     #$FF
        lsr     a
        ora     #$40
@put:   sta     SOUND_DOCDATA
        inx
        bne     @w
        lda     #SOUND_DOC_RUN          ; the registers, the chip running
        sta     SOUND_DOCCTL
        ldx     #0
@reg:   lda     docregs,x
        cmp     #$FF
        beq     @on
        sta     SOUND_DOCADDR
        stz     SOUND_DOCADDR + 1
        lda     docregs + 1,x
        sta     SOUND_DOCDATA
        inx
        inx
        bra     @reg
@on:    lda     #<500
        ldx     #>500
        jsr     delay
        lda     #$A0                    ; halted
        sta     SOUND_DOCADDR
        lda     #$01
        sta     SOUND_DOCDATA
        rts

; ---------------------------------------------------------------------------
; SD: the mass storage card (module 7)

asd:    lda     #<hsd
        ldx     #>hsd
        ldy     #7
        jsr     heading
        jsr     sd1
        jsr     sd2
        jsr     sd3
        jmp     sd4

sd1:    LINE    "SD1", "ID "
        ldy     #7
        lda     HW_IDB,y
        jsr     hex2
        SAY     "; STATUS: a card, ready"
        ldy     #7
        jsr     known
        bcs     @r
        lda     SD_STATUS
        and     #SD_PRESENT | SD_READY
        cmp     #SD_PRESENT | SD_READY
        bne     @bad
        jmp     pass
@bad:   jsr     fail
        FLINE   mstatus
        lda     SD_STATUS
        jmp     hex2
@r:     rts

sd2:    LINE    "SD2", "IDENTIFY: "
        ldy     #7
        lda     HW_REV,y
        cmp     #HW_NONE
        jeq     @line
        lda     #<SCRID
        sta     HW_PTR
        lda     #>SCRID
        sta     HW_PTR + 1
        jsr     HW_SD_ID
        jcs     @err
        lda     SCRID + SI_TYPE
        sta     tmp
        cmp     #4
        bcs     @type
        asl     a
        tax
        lda     sdtypes,x
        pha
        lda     sdtypes + 1,x
        tax
        pla
        jsr     HW_PUTS
        bra     @size
@type:  SAY     "type "
        lda     tmp
        jsr     dec8
@size:  SAY     ", "
        ldx     #3
@cp:    lda     SCRID + SI_SECTORS,x
        sta     HW_NUM,x
        dex
        bpl     @cp
        jsr     decn
        SAY     " sectors, "
        lda     SCRID + SI_FLAGS
        and     #SIF_FAT
        bne     @fat
        SAY     "no "
@fat:   SAY     "FAT"
        ldy     #7
        jsr     known
        bcs     @r
        lda     tmp
        beq     @bad
        cmp     #4
        bcs     @bad
        lda     SCRID + SI_SECTORS
        ora     SCRID + SI_SECTORS + 1
        ora     SCRID + SI_SECTORS + 2
        ora     SCRID + SI_SECTORS + 3
        beq     @bad
        lda     SCRID + SI_FLAGS
        and     #SIF_FAT
        beq     @bad
        jmp     pass
@bad:   jmp     fail
@err:   pha
        SAY     "refused"
        jsr     fail
        FLINE   "HW_SD_ID's error "
        pla
        jmp     hex2
@line:  SAY     "the card's type, size and FAT"
        ldy     #7
        jmp     known
@r:     rts

; SD3: DM_LOAD of DIAG.COM's records, again until 64 KB, timed by the
; millisecond counter.
sd3:    LINE    "SD3", "read by DMA: "
        ldy     #7
        jsr     known
        bcc     @go
        rts
@go:    jsr     twoblk
        bcc     @blk
        SAY     mkbs
        SKIP    mnomap
        rts
@blk:   lda     areaok
        bne     @area
        SAY     mkbs
        SKIP    mnoarea
        rts
@area:  jsr     selfopen
        bcc     @open
        SAY     mkbs
        SKIP    mnoself
        rts
@open:  stz     nrecs                   ; the records loaded so far
        stz     nrecs + 1
        jsr     mark
@load:  lda     #<selffcb
        sta     ldblk + 1
        lda     #>selffcb
        sta     ldblk + 2
        stz     ldblk + 3
        stz     ldblk + 4
        stz     ldblk + 5
        lda     #120                    ; 15 KB at most a load: tblk's
        sta     ldblk + 6
        stz     ldblk + 7
        lda     tblk
        jsr     bphys
        sta     ldblk + 8
        stx     ldblk + 9
        sty     ldblk + 10
        lda     #DM_LOAD
        sta     ldblk
        CALL    O_DMA, ldblk
        cmp     #2
        bcs     @refused                ; (1: the file ended: +6-7 what it loaded)
        clc
        lda     nrecs
        adc     ldblk + 6
        sta     nrecs
        lda     nrecs + 1
        adc     ldblk + 7
        sta     nrecs + 1
        lda     ldblk + 6
        ora     ldblk + 7
        beq     @refused
        lda     nrecs + 1               ; 512 records: 64 KB
        cmp     #>512
        bcc     @load
        jsr     since
        sta     dms
        stx     dms + 1
        jsr     selfclose
        lda     nrecs                   ; KB = records / 8; x 1,000 / ms
        sta     HW_NUM
        lda     nrecs + 1
        sta     HW_NUM + 1
        stz     HW_NUM + 2
        stz     HW_NUM + 3
        jsr     x1000
        lda     #8
        ldx     #0
        jsr     HW_DIV
        lda     dms
        ldx     dms + 1
        bne     @div
        cmp     #0
        bne     @div
        lda     #1
@div:   jsr     HW_DIV
        jsr     decn
        SAY     " KB a second"
        jmp     info
@refused:
        jsr     selfclose
        SAY     mkbs
        jsr     fail
        FLINE   mnoload
        rts

; SD4 (/WRITE): A:DIAG$$$.TMP, 64 KB of a pattern, written (21), read back
; (20) and erased (19), after a Y.
sd4:    LINE    lsd4
        lda     write
        bne     @w
        SKIP    "/WRITE"
        rts
@w:     ldy     #7
        jsr     known
        bcc     @ask
        rts
@ask:   jsr     CM_EOL
        jsr     CM_FLUSH
        lda     #<swarn
        ldx     #>swarn
        jsr     CM_PUTS
        jsr     yesno
        php
        LINE    lsd4
        plp
        bcs     @go
        SKIP    "not confirmed"
        rts
@go:    jsr     tmpfcb
        lda     #<tmpf                  ; a leftover first
        ldx     #>tmpf
        ldy     #F_DELETE
        jsr     BDOS
        jsr     tmpfcb
        lda     #<tmpf
        ldx     #>tmpf
        ldy     #F_MAKE
        jsr     BDOS
        cmp     #4
        bcs     @nomake
        jsr     recdma
        stz     nrecs
        stz     nrecs + 1
@wr:    jsr     recfill
        lda     #<tmpf
        ldx     #>tmpf
        ldy     #F_WRITE
        jsr     BDOS
        cmp     #0
        bne     @nowrite
        jsr     recnext
        bcc     @wr
        lda     #<tmpf
        ldx     #>tmpf
        ldy     #F_CLOSE
        jsr     BDOS
        jsr     tmpfcb
        lda     #<tmpf
        ldx     #>tmpf
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #4
        bcs     @noread
        stz     nrecs
        stz     nrecs + 1
@rd:    lda     #<tmpf
        ldx     #>tmpf
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        bne     @noread
        jsr     reccheck
        bcs     @diff
        jsr     recnext
        bcc     @rd
        lda     #<tmpf
        ldx     #>tmpf
        ldy     #F_CLOSE
        jsr     BDOS
        jsr     erase
        jsr     dmabp
        jmp     pass
@nomake:
        jsr     dmabp
        jsr     fail
        FLINE   "A:DIAG$$$.TMP not made (22)"
        rts
@nowrite:
        jsr     erase
        jsr     fail
        FLINE   "a record not written (21)"
        rts
@noread:
        jsr     erase
        jsr     fail
        FLINE   "a record not read back (20)"
        rts
@diff:  jsr     erase
        jsr     fail
        FLINE   "record "
        lda     nrecs
        ldx     nrecs + 1
        jsr     dec16
        SAY     " +"
        lda     HW_BAD
        jsr     dec8
        SAY     ": "
        jmp     HW_FAULT
; The temporary file erased, and the DMA address back.
erase:  jsr     tmpfcb
        lda     #<tmpf
        ldx     #>tmpf
        ldy     #F_DELETE
        jsr     BDOS
        jmp     dmabp
; tmpf: A:DIAG$$$.TMP.
tmpfcb: ldx     #35
@z:     stz     tmpf,x
        dex
        bpl     @z
        lda     #1
        sta     tmpf
        ldx     #10
@n:     lda     stmpname,x
        sta     tmpf + 1,x
        dex
        bpl     @n
        rts
recdma: lda     #<SCRREC
        ldx     #>SCRREC
        ldy     #F_DMAOFF
        jmp     BDOS
; Record nrecs's 128 bytes: byte i is i XOR the record's number's low XOR
; its high XOR $A5.
recval: lda     nrecs
        eor     nrecs + 1
        eor     #$A5
        sta     tmp
        rts
recfill:
        jsr     recval
        ldy     #127
@b:     tya
        eor     tmp
        sta     SCRREC,y
        dey
        bpl     @b
        rts
reccheck:
        jsr     recval
        ldy     #127
@b:     tya
        eor     tmp
        cmp     SCRREC,y
        bne     @bad
        dey
        bpl     @b
        clc
        rts
@bad:   sta     HW_WANT
        lda     SCRREC,y
        sta     HW_GOT
        sty     HW_BAD
        sec
        rts
; The next record: carry set after 512.
recnext:
        inc     nrecs
        bne     @c
        inc     nrecs + 1
@c:     lda     nrecs + 1
        cmp     #>512
        rts

        .segment "CODE2"                ; (from here on: at $8000, cfg/diag.cfg)

; ---------------------------------------------------------------------------
; USB (module B)

ausb:   lda     #<husb
        ldx     #>husb
        ldy     #$B
        jsr     heading
        jsr     u1
        jsr     u2
        jsr     u3
        jsr     u4
        jmp     u5

u1:     LINE    "U1", "DEVICES "
        ldy     #$B
        lda     HW_REV,y
        cmp     #HW_NONE
        beq     @known
        lda     USB_DEVICES
        sta     tmp
        jsr     hex2
        lda     tmp
        and     #$07
        beq     @none
        lda     tmp
        lsr     a
        bcc     @m
        SAY     ": a keyboard"
@m:     lda     tmp
        and     #2
        beq     @g
        SAY     ", a mouse"
@g:     lda     tmp
        and     #4
        beq     @known
        SAY     ", a gamepad"
        bra     @known
@none:  SAY     ": nothing plugged in"
@known: ldy     #$B
        jsr     known
        bcs     @r
        jmp     info
@r:     rts

u2:     LINE    "U2", "the paddles settle:"
        ldy     #$B
        jsr     known
        bcs     @r
        lda     areaok
        bne     @go
        SKIP    mnoarea
@r:     rts
@go:    ldx     #3
@a:     lda     area + EV_PADDLES,x
        sta     pad0,x
        dex
        bpl     @a
        lda     #<20
        ldx     #>20
        jsr     delay
        ldx     #0
@b:     phx
        lda     #' '
        jsr     HW_PUTC
        plx
        phx
        lda     area + EV_PADDLES,x
        sta     pad1,x
        jsr     dec8
        plx
        inx
        cpx     #4
        bne     @b
        ldx     #3
@c:     lda     pad0,x
        cmp     pad1,x
        bne     @bad
        dex
        bpl     @c
        jmp     info
@bad:   stx     idx
        jsr     fail
        FLINE   "paddle "
        lda     idx
        jsr     dec8
        SAY     " read "
        ldx     idx
        lda     pad0,x
        jsr     dec8
        SAY     " then "
        ldx     idx
        lda     pad1,x
        jmp     dec8

; Carry set (the line ended SKIP) unless /I and the event area are there.
needi:  lda     inter
        bne     @i
        SKIP    "/I"
        sec
        rts
@i:     lda     areaok
        bne     @ok
        SKIP    mnoarea
        sec
        rts
@ok:    clc
        rts

; U3 (/I): A, then Esc, seen in the queue as key presses (usages $04, $29).
u3:     LINE    lu3
        ldy     #$B
        jsr     known
        bcs     @r
        jsr     needi
        bcs     @r
        jsr     CM_EOL
        jsr     evflush
        FLINE   "press A, then Esc"
        jsr     CM_EOL
        lda     #$04
        jsr     waitkey
        bcs     @no
        lda     #$29
        jsr     waitkey
        bcs     @no
        jsr     drain
        LINE    lu3
        jmp     pass
@no:    jsr     drain
        LINE    lu3
        jsr     fail
        FLINE   "not seen within 10 s"
@r:     rts
; A key's press (usage A) in the queue within 10 s: carry clear.
waitkey:
        sta     kwant
        jsr     mark
@w:     jsr     evnext
        bcs     @late
        cmp     #EK_KEY
        bne     @w
        lda     ev + EQ_CODE
        cmp     kwant
        bne     @w
        lda     ev + EQ_VALUE
        beq     @w
        clc
        rts
@late:  lda     #<10000
        ldx     #>10000
        jsr     late
        bcc     @w
        rts
; The console's keys the user typed for the queue, dropped (6, $FF).
drain:  lda     #$FF
        ldy     #C_RAWIO
        jsr     BDOS
        cmp     #0
        bne     drain
        rts

; U4 (/I): the mouse moved, its buttons, the game port's buttons.
u4:     LINE    lu4
        ldy     #$B
        jsr     known
        jcs     @r
        jsr     needi
        jcs     @r
        jsr     CM_EOL
        stz     nmiss
        FLINE   "move the mouse"
        jsr     CM_EOL
        jsr     mouse
        bcc     @b
        inc     nmiss
@b:     stz     idx
@btn:   FLINE   "press the mouse's button "
        lda     idx
        jsr     dec8
        jsr     CM_EOL
        lda     idx
        jsr     button
        bcc     @nb
        inc     nmiss
@nb:    inc     idx
        lda     idx
        cmp     #3
        bne     @btn
        stz     idx
@gp:    FLINE   "press the game port's button "
        lda     idx
        jsr     dec8
        jsr     CM_EOL
        lda     idx
        jsr     gpbutton
        bcc     @ng
        inc     nmiss
@ng:    inc     idx
        lda     idx
        cmp     #3
        bne     @gp
        LINE    lu4
        lda     nmiss
        bne     @bad
        jmp     pass
@bad:   jsr     fail
        FLINE   ""
        lda     nmiss
        jsr     dec8
        SAY     " of 7 not seen within 10 s each"
@r:     rts
; The mouse's X or Y moved within 10 s: carry clear.
mouse:  lda     area + EV_MX
        sta     tmp
        lda     area + EV_MY
        sta     tmp + 1
        jsr     mark
@w:     jsr     evnext
        lda     area + EV_MX
        cmp     tmp
        bne     @yes
        lda     area + EV_MY
        cmp     tmp + 1
        bne     @yes
        lda     #<10000
        ldx     #>10000
        jsr     late
        bcc     @w
        rts
@yes:   clc
        rts
; Mouse button A pressed (EK_BUTTON) within 10 s: carry clear.
button: sta     kwant
        jsr     mark
@w:     jsr     evnext
        bcs     @late
        cmp     #EK_BUTTON
        bne     @w
        lda     ev + EQ_CODE
        cmp     kwant
        bne     @w
        lda     ev + EQ_VALUE
        beq     @w
        clc
        rts
@late:  lda     #<10000
        ldx     #>10000
        jsr     late
        bcc     @w
        rts
; The game port's button A (CB_FIRE << A in its controller) within 10 s.
gpbutton:
        tax
        lda     #CB_FIRE
@sh:    cpx     #0
        beq     @m
        asl     a
        dex
        bra     @sh
@m:     sta     kwant
        lda     #CH_GAMEPORT
        jsr     ctlof
        bcs     @no
        jsr     mark
@w:     jsr     evnext
        ldx     ctlx
        lda     area + EV_CTL + EC_BITS,x
        and     kwant
        bne     @yes
        lda     #<10000
        ldx     #>10000
        jsr     late
        bcc     @w
@no:    sec
        rts
@yes:   clc
        rts
; The controller whose hardware is A (CH_*): ctlx its offset from EV_CTL;
; carry set if none is.
ctlof:  ldx     #0
@c:     cmp     area + EV_CTL + EC_HW,x
        beq     @yes
        inx
        inx
        inx
        inx
        cpx     #4 * EC_LEN
        bne     @c
        sec
        rts
@yes:   stx     ctlx
        clc
        rts

; U5 (/I): the speaker, 1 kHz for 0.5 s.
u5:     LINE    "U5", "the speaker: a 1 kHz beep"
        ldy     #$B
        jsr     known
        bcs     @r
        lda     inter
        bne     @i
        SKIP    "/I"
@r:     rts
@i:     jsr     CM_EOL
        lda     #SN_BEEP
        sta     beepblk
        lda     #<1000
        sta     beepblk + 1
        lda     #>1000
        sta     beepblk + 2
        lda     #<500
        sta     beepblk + 3
        lda     #>500
        sta     beepblk + 4
        CALL    O_SOUND, beepblk
        jsr     yesno
        php
        LINE    "U5", "the speaker: the beep heard"
        plp
        bcc     @no
        jmp     pass
@no:    jmp     fail

; ---------------------------------------------------------------------------
; SERIAL: the serial card and its VIA (module 6)

aserial:
        jsr     CM_FLUSH
        lda     #<hserial
        ldx     #>hserial
        jsr     HW_PUTS
        jsr     CM_EOL
        jsr     sr1
        jsr     sr2
        jsr     sr3
        jmp     sr4

sr1:    LINE    "SR1", "the card's ID"
        ldy     #6
        lda     HW_REV,y
        cmp     #HW_NONE
        bne     @card
        SKIP    "the stand-in 6551: no ID register"
        rts
@card:  SAY     " "
        ldy     #6
        lda     HW_IDB,y
        jsr     hex2
        ldy     #6
        jsr     known
        bcs     @r
        jmp     pass
@r:     rts

; SR2: timer 2 across a loop of 1,000 cycles, less across none (so the
; reads' own cycles, and any wait states on them, drop out): 1,000 within
; 20; again if an interrupt came between (five times at most).
sr2:    LINE    "SR2", "the VIA's timer 2 counts 1,000 cycles as "
        lda     #5
        sta     nstep
@try:   lda     #$FF
        sta     VIA_T2CL
        sta     VIA_T2CH
        jsr     t2now                   ; none between
        sta     tmp
        stx     tmp + 1
        jsr     t2now
        sta     t4
        stx     t4 + 1
        jsr     t2now                   ; 1,000: LDX # 2, the loop 5 x 199 - 1, two NOPs
        sta     t8
        stx     t8 + 1
        ldx     #199
@d:     dex
        bne     @d
        nop
        nop
        jsr     t2now
        sta     t8 + 2
        stx     t8 + 3
        sec                             ; (t8 - t8+2) - (tmp - t4)
        lda     t8
        sbc     t8 + 2
        sta     t8
        lda     t8 + 1
        sbc     t8 + 3
        sta     t8 + 1
        sec
        lda     tmp
        sbc     t4
        sta     tmp
        lda     tmp + 1
        sbc     t4 + 1
        sta     tmp + 1
        sec
        lda     t8
        sbc     tmp
        sta     t8
        lda     t8 + 1
        sbc     tmp + 1
        sta     t8 + 1
        lda     t8 + 1                  ; 980-1,020
        cmp     #>980
        bcc     @again
        bne     @hi
        lda     t8
        cmp     #<980
        bcc     @again
@hi:    lda     t8 + 1
        cmp     #>1021
        bcc     @ok
        bne     @again
        lda     t8
        cmp     #<1021
        bcc     @ok
@again: dec     nstep
        jne     @try
        lda     t8
        ldx     t8 + 1
        jsr     dec16
        jmp     fail
@ok:    lda     t8
        ldx     t8 + 1
        jsr     dec16
        jmp     pass

; SR3 (/I): each joystick's directions and fire, on both ports.
sr3:    LINE    lsr3
        jsr     needi
        bcc     @go
        rts
@go:    jsr     CM_EOL
        stz     nmiss
        lda     #CH_STICK1
        jsr     @port
        lda     #CH_STICK2
        jsr     @port
        LINE    lsr3
        lda     nmiss
        bne     @bad
        jmp     pass
@bad:   jsr     fail
        FLINE   ""
        lda     nmiss
        jsr     dec8
        SAY     " of 10 not seen within 10 s each"
        rts
@port:  sta     tmp + 1
        jsr     ctlof
        bcc     @have
        lda     nmiss
        clc
        adc     #5
        sta     nmiss
        rts
@have:  stz     idx
@dir:   FLINE   "joystick "
        lda     tmp + 1
        jsr     dec8
        SAY     ": "
        ldx     idx
        lda     sdirs,x
        pha
        lda     sdirs + 5,x
        tax
        pla
        jsr     HW_PUTS
        jsr     CM_EOL
        ldx     idx
        lda     stbits,x
        sta     kwant
        jsr     mark
@w:     jsr     evnext
        ldx     ctlx
        lda     area + EV_CTL + EC_BITS,x
        and     kwant
        bne     @seen
        lda     #<10000
        ldx     #>10000
        jsr     late
        bcc     @w
        inc     nmiss
@seen:  inc     idx
        lda     idx
        cmp     #5
        bne     @dir
        rts

sr4:    LINE    "SR4", "the TL16C552A's COM2 in its loopback"
        ldy     #6
        lda     HW_REV,y
        cmp     #HW_NONE
        bne     @card
        SKIP    "no TL16C552A card"
        rts
@card:  SKIP    "the card isn't built yet"
        rts

; ---------------------------------------------------------------------------
; DISK2 (module C): its ID alone

adisk2: lda     #<hdisk2
        ldx     #>hdisk2
        ldy     #$C
        jsr     heading
        LINE    "DK1", "ID "
        ldy     #$C
        lda     HW_IDB,y
        jsr     hex2
        SAY     " (alone: a switch moves the drive)"
        ldy     #$C
        jsr     known
        bcs     @r
        jmp     pass
@r:     rts

; ---------------------------------------------------------------------------
; PANEL: the front panel (module E)

apanel: lda     #<hpanel
        ldx     #>hpanel
        ldy     #$E
        jsr     heading
        jsr     p1
        jsr     p2
        jmp     p3

p1:     LINE    "P1", "switches "
        ldy     #$E
        lda     HW_REV,y
        cmp     #HW_NONE
        beq     @k
        lda     CONSOLE_SWR
        ldx     CONSOLE_SWR + 1
        ldy     CONSOLE_SWR + 2
        jsr     hex5
        SAY     ", HALT/ENABLE at "
        lda     CONSOLE_PANEL
        lsr     a
        bcc     @en
        SAY     "HALT"
        bra     @k
@en:    SAY     "ENABLE"
@k:     ldy     #$E
        jsr     known
        bcs     @r
        jmp     info
@r:     rts

; The console mode with bit 3 (^C doesn't end the program) and CTRL,
; BPADDR and BPCTRL kept, before P2 and P3 change any (review CC3).
guardon:
        lda     #$FF
        tax
        ldy     #C_MODE
        jsr     BDOS
        sta     cmode
        stx     cmode + 1
        ora     #$08
        ldy     #C_MODE
        jsr     BDOS
        lda     CONSOLE_CTRL
        sta     pctrl
        lda     CONSOLE_BPCTRL
        sta     pbpctrl
        ldx     #2
@a:     lda     CONSOLE_BPADDR,x
        sta     pbpaddr,x
        dex
        bpl     @a
        lda     #1
        sta     guard
        rts
; CTRL, BPCTRL (0 after P3's) and BPADDR back, STATUS's HIT cleared, DISPLAY
; 0; then the console mode. Nothing if P2 or P3 isn't under way.
unguard:
        lda     guard
        beq     @r
        stz     CONSOLE_BPCTRL
        lda     #$02                    ; HIT: a 1 clears it
        sta     CONSOLE_STATUS
        ldx     #2
@a:     lda     pbpaddr,x
        sta     CONSOLE_BPADDR,x
        dex
        bpl     @a
        lda     armed                   ; P2: DISPLAY 0, and the user's breakpoint as it was;
        bne     @off                    ;   P3: BPCTRL 0
        stz     CONSOLE_DISPLAY
        lda     pbpctrl
        sta     CONSOLE_BPCTRL
@off:   lda     pctrl
        sta     CONSOLE_CTRL
        stz     guard
        lda     cmode
        ldx     cmode + 1
        ldy     #C_MODE
        jmp     BDOS
@r:     rts

; P2: a bit walked on DISPLAY with CTRL's bit 3, 0.2 s a step, CTRL put back.
p2:     LINE    "P2", "the lights: a bit walked on DISPLAY"
        ldy     #$E
        jsr     known
        bcs     @r
        lda     areaok
        bne     @go
        SKIP    mnoarea
@r:     rts
@go:    lda     inter
        beq     @walk
        jsr     CM_EOL
        FLINE   "watch the data lights"
        jsr     CM_EOL
@walk:  stz     armed
        jsr     guardon
        lda     pctrl
        ora     #$08
        sta     CONSOLE_CTRL
        lda     #$01
@step:  sta     CONSOLE_DISPLAY
        pha
        lda     #<200
        ldx     #>200
        jsr     delay
        pla
        asl     a
        bne     @step
        jsr     unguard
        lda     inter
        bne     @ask
        jmp     info
@ask:   jsr     yesno
        php
        LINE    "P2", "the lights: the bit seen walking"
        plp
        bcc     @no
        jmp     pass
@no:    jmp     fail

; P3 (/FULL /I): a read of a byte of DIAG's own (p3byte) armed as a
; breakpoint that halts; CONT goes on; STATUS's HIT, LADDR and LDATA
; checked; BPCTRL 0 and HIT cleared, then the console mode back.
p3:     LINE    lp3
        lda     full
        and     inter
        bne     @go
        SKIP    "/FULL /I"
        rts
@go:    ldy     #$E
        jsr     known
        bcc     @card
        rts
@card:  lda     #<p3byte                ; its physical address
        sta     pab
        lda     #>p3byte
        sta     pab + 1
        lda     #<pab
        ldx     #>pab
        ldy     #O_PHYSADDR
        jsr     BDOS
        lda     pab + 2                 ; the paragraph x 16 + the offset
        sta     bpa
        lda     pab + 3
        sta     bpa + 1
        stz     bpa + 2
        ldx     #4
@x16:   asl     bpa
        rol     bpa + 1
        rol     bpa + 2
        dex
        bne     @x16
        lda     bpa
        ora     pab
        sta     bpa
        lda     #$A7
        sta     p3byte
        lda     #$05                    ; armed first, the console mode's bit 3 with it
        sta     armed
        jsr     guardon
        ldx     #2
@a:     lda     bpa,x
        sta     CONSOLE_BPADDR,x
        dex
        bpl     @a
        lda     #$02                    ; HIT cleared, then the read that halts armed
        sta     CONSOLE_STATUS
        lda     #$05                    ; BP_ENABLE | BP_READ
        sta     CONSOLE_BPCTRL
        jsr     CM_EOL                  ; the user's word: RETURN to go on; a ^C in the line
        FLINE   "the CPU will halt at a read of "   ;   (bit 3: not the warm boot) stops it
        lda     bpa
        ldx     bpa + 1
        ldy     bpa + 2
        jsr     hex5
        SAY     ": RETURN, then CONT on the panel"
        jsr     CM_EOL
        jsr     CM_FLUSH
        jsr     CM_READ
        jsr     CM_EOL
        ldx     CM_ANS + 1              ; a ^C typed: everything put back, then DIAG ends
@cc:    dex
        bmi     @halt
        lda     CM_ANS + 2,x
        cmp     #CMC_CTLC
        bne     @cc
        jsr     unguard
        jmp     brkx
@halt:
        lda     p3byte                  ; the CPU halts after this, until CONT
        sta     p3got
        lda     CONSOLE_STATUS
        sta     p3st
        ldx     #2
@l:     lda     CONSOLE_LADDR,x
        sta     p3la,x
        dex
        bpl     @l
        lda     CONSOLE_LDATA
        sta     p3ld
        jsr     unguard                 ; BPCTRL 0, HIT cleared, CTRL; then the console mode
        LINE    lp3
        lda     p3st
        and     #$02
        beq     @nohit
        ldx     #2
@c:     lda     p3la,x
        cmp     bpa,x
        bne     @wrong
        dex
        bpl     @c
        lda     p3ld
        cmp     #$A7
        bne     @wrong
        jmp     pass
@nohit: jsr     fail
        FLINE   mstatus
        lda     p3st
        jsr     hex2
        SAY     ": no HIT"
        rts
@wrong: jsr     fail
        FLINE   "latched "
        lda     p3la
        ldx     p3la + 1
        ldy     p3la + 2
        jsr     hex5
        SAY     " "
        lda     p3ld
        jsr     hex2
        SAY     mexpect
        lda     bpa
        ldx     bpa + 1
        ldy     bpa + 2
        jsr     hex5
        SAY     " $A7"
        rts

; ---------------------------------------------------------------------------

        .rodata
areas:  .word   anb, amem, admaa, avideo, asound, asd, ausb, aserial, adisk2, apanel
; The areas' names, in that order, each ended by 0.
areatab:
        .byte   "NB", 0, "MEM", 0, "DMA", 0, "VIDEO", 0, "SOUND", 0, "SD", 0, "USB", 0, "SERIAL", 0
        .byte   "DISK2", 0, "PANEL", 0
; The options: a name, its 0, the byte it sets (an offset from full; $FF /?).
opttab: .byte   "/FULL", 0, 0, "/I", 0, 1, "/WRITE", 0, 2, "/LOOP", 0, 3, "/?", 0, $FF, 0
bit8:   .byte   $01, $02, $04, $08, $10, $20, $40, $80
bgtab:  .byte   $00, $55, $33, $0F
stitle: .byte   "OO-1 DIAGNOSTICS 0.1   ", 0
hnb:    .byte   "NORTH BRIDGE", 0
hmem:   .byte   "MEMORY", 0
hdma:   .byte   "DMA", 0
hvideo: .byte   "VIDEO", 0
hsound: .byte   "SOUND", 0
hsd:    .byte   "STORAGE", 0
husb:   .byte   "USB", 0
hserial: .byte  "SERIAL AND VIA", 0
hdisk2: .byte   "DISK II", 0
hpanel: .byte   "FRONT PANEL", 0
srevn:  .byte   " (a revision not known to this DIAG)", 0
snoi:   .byte   " (DIAG /FULL /I runs them all)", 0
sdiagcom: .byte "DIAG    COM"
stmpname: .byte "DIAG$$$ TMP"
syn:    .byte   "       Y or N? $"
swarn:  .byte   "       DIAG will write a 64 KB file A:DIAG$$$.TMP, read it back and erase it.", 13, 10, "$"
sdtypes: .word  sdt0, sdt1, sdt2, sdt3
sdt0:   .byte   "no card", 0
sdt1:   .byte   "SDSC", 0
sdt2:   .byte   "SDHC", 0
sdt3:   .byte   "SDXC", 0
sdirs:  .byte   <sup, <sdown, <sleft, <sright, <sfire
        .byte   >sup, >sdown, >sleft, >sright, >sfire
sup:    .byte   "up", 0
sdown:  .byte   "down", 0
sleft:  .byte   "left", 0
sright: .byte   "right", 0
sfire:  .byte   "fire", 0
stbits: .byte   CB_UP, CB_DOWN, CB_LEFT, CB_RIGHT, CB_FIRE
; V2's registers (offsets from VIDEO), and the bits of each that hold.
vregs:  .byte   VIDEO_BORDER - VIDEO, VIDEO_L1_CONFIG - VIDEO, VIDEO_L1_MAP - VIDEO, VIDEO_L1_MAP + 1 - VIDEO
        .byte   VIDEO_L1_TILE - VIDEO, VIDEO_L1_TILE + 1 - VIDEO, VIDEO_L1_HSCROLL - VIDEO
        .byte   VIDEO_L1_HSCROLL + 1 - VIDEO, VIDEO_L1_VSCROLL - VIDEO, VIDEO_L1_VSCROLL + 1 - VIDEO
        .byte   VIDEO_L1_PALOFS - VIDEO
NVREG = * - vregs
vmask:  .byte   $FF, $FF, $FF, $FF, $FF, $FF, $FF, $03, $FF, $03, $0F
; S8's oscillator 0 (register, value; $FF ends): 440 Hz from 256 bytes at the
; chip's 7,159,090 / (8 x 3) Hz with one oscillator: 440 x 256 x 2^9 / 298,295.
docregs:
        .byte   $E1, $00                ; one oscillator
        .byte   $00, <193, $20, >193    ; the frequency
        .byte   $40, $C0                ; the volume
        .byte   $80, $00                ; the table at wave RAM $00000
        .byte   $C0, $00                ; 256 bytes, resolution 0
        .byte   $A0, $00                ; free run, running
        .byte   $FF
susage: .byte   "DIAG {area ...} {/FULL} {/I} {/WRITE} {/LOOP} {/?}$"
ssmall: .byte   "DIAG needs the TPA to $BFFC: remove the RSXs below it first$"
shelp:  .byte   "DIAG {area ...} {/FULL} {/I} {/WRITE} {/LOOP} {/?}", 13, 10
        .byte   "  areas   NB MEM DMA VIDEO SOUND SD USB SERIAL DISK2 PANEL (none: all)", 13, 10
        .byte   "  /FULL   every march background, card memory marched, wave RAM, P3", 13, 10
        .byte   "  /I      the tests that ask you to do something", 13, 10
        .byte   "  /WRITE  a 64 KB file on A:, written, read back and erased (after a Y)", 13, 10
        .byte   "  /LOOP   again and again until a key", 13, 10
        .byte   "The return code is the count of failures.$"
card6510:
        .incbin "card6510.bin"
CARD6510_LEN = * - card6510

        .data
opblk:  .byte   FT_OPEN, 0, 0, 0        ; the event area: 256 bytes, layout 2
dlyblk: .byte   FT_DELAY, 0, 0
eachjmp: jmp    $0000                   ; each's and vbanks's routine (written)

        .bss
want:   .res    2
full:   .res    1                       ; the options, in opttab's order
inter:  .res    1
write:  .res    1
loop:   .res    1
held:   .res    1
sndcl:  .res    1
sndst:  .res    1
guard:  .res    1
armed:  .res    1
areaok: .res    1
gxok:   .res    1                       ; 1: GX_INIT took (a W1 map)
shown:  .res    1
ftotal: .res    2
npass:  .res    2
anum:   .res    1
wbuf:   .res    16
tmp:    .res    2
t0:     .res    2
lim:    .res    2
mblim:  .res    2
tstart: .res    2
cnt:    .res    1
idx:    .res    1
nall:   .res    1
fblk:   .res    1
fbank:  .res    1
cpblk:  .res    1
tblk:   .res    1
ublk:   .res    1
nbg:    .res    1
bgi:    .res    1
nalloc: .res    1
nblk1:  .res    1
nblk9:  .res    1
blk1:   .res    20
blk9:   .res    12
mcb:    .res    5
n16:    .res    2
maskl:  .res    1
maskh:  .res    1
irql:   .res    1
irqh:   .res    1
lost:   .res    1
sec0:   .res    1
sec1:   .res    1
nstep:  .res    1
hz:     .res    4
sign:   .res    1
milli:  .res    2
frac:   .res    2
t4:     .res    4
t8:     .res    4
cad:    .res    3
vfirst: .res    2
vend:   .res    2
nbank:  .res    1
tbig:   .res    2
tsmall: .res    2
tmin:   .res    2
flen:   .res    2
dmaad:  .res    2
nrec:   .res    1
npg:    .res    1
half:   .res    1
sum:    .res    2
got16:  .res    2
res5:   .res    5
sida:   .res    1
sidb:   .res    1
dockept: .res   1
st1:    .res    1
seen:   .res    1
t1:     .res    2
pos0:   .res    4
dms:    .res    2
rate:   .res    2
nrecs:  .res    2
pad0:   .res    4
pad1:   .res    4
kwant:  .res    1
nmiss:  .res    1
ctlx:   .res    1
lmin:   .res    2
lmax:   .res    2
fcyc:   .res    3
row:    .res    1
col:    .res    1
vbad:   .res    1
vkept:  .res    NVREG
cmode:  .res    2
pctrl:  .res    1
pbpctrl: .res   1
pbpaddr: .res   3
pab:    .res    4
bpa:    .res    3
p3byte: .res    1
p3got:  .res    1
p3st:   .res    1
p3la:   .res    3
p3ld:   .res    1
cmdb:   .res    8
ev:     .res    EQ_LEN
fillblk: .res   7
cpyblk: .res    9
ldblk:  .res    11
vblk:   .res    9
infblk: .res    17
snload: .res    8
stblk:  .res    4
clblk:  .res    2
sndblk: .res    2
strblk: .res    5
bufblk: .res    6
stblk2: .res    13
beepblk: .res   5
selffcb: .res   36
tmpf:   .res    36
area:   .res    256                     ; the event area (layout 2)

        .code
; ---------------------------------------------------------------------------
; The start and the end, and the tail (in place of util.inc, which DIAG
; doesn't include: it would need more room than DIAG has below W1).

; Nothing owed, the tail from its start, the BDOS's errors returned (45).
tinit:  stz     CM_LFDUE
        stz     CM_EOLDUE
        stz     CM_PLEN
        stz     CM_LINES
        stz     tidx
        lda     #$FE
        ldy     #F_ERRMODE
        jmp     BDOS
; The tail ($0281, ended by 0): A the next character, Z at the end; keeps Y.
tpeek:  ldx     tidx
        lda     BP_DMA + 1,x
        rts
; The same, and past it.
tget:   jsr     tpeek
        beq     @r
        inc     tidx
@r:     rts
; Past blanks and tabs, then as tpeek.
tskip:  jsr     tpeek
        cmp     #' '
        beq     @past
        cmp     #9
        bne     @r
@past:  inc     tidx
        bra     tskip
@r:     cmp     #0
        rts
; The '$' string at A/X, and the line owed.
tsay:   jsr     CM_PUTS
        jmp     CM_EOL
; The return code $FF00: an error said.
tfail:  lda     #$00
        ldx     #$FF
        ldy     #P_CODE
        jmp     BDOS
; The end: the console mode back, the last line left to the prompt.
texit:  jsr     CM_UNPAGE
        jsr     CM_NOEOL
        jmp     WBOOT
; A ^C: the return code $FFFE, then the end.
tbreak: lda     #$FE
        ldx     #$FF
        ldy     #P_CODE
        jsr     BDOS
        bra     texit

        .bss
tidx:   .res    1

        .code
        .include "cmd.inc"

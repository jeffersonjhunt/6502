; PLAYSID.COM (hw/oolib.md, "The plan", L3; the User's Guide's PLAYSID):
; PLAYSID filespec {song}. A C64 .sid tune (PSID) played on the sound
; card's 6510 by OOLIB's linked library (lib/sid.s, include/oolib.inc),
; through function 82, until a key is pressed.
;
;   filespec    the file; .SID if its type is left out
;   song        1 to the tune's songs (decimal); the file's own start song
;               if left out
;
; It reads the file into the memory past its BSS (the byte count of its
; last record, which open gives for CR $FF, taken off where a file has
; one), says "Playing song n of m: name, author" and "Press a key to
; stop.", and waits for a key at the console (function 6); then it stops
; the 6510 and ends. The warm boot would stop it anyway.
;
; Errors: "Invalid file specification.", "No File", "Too big." (more than
; the TPA holds), the library's (SE_*) in words, "Song n of m?" for a song
; that isn't one. The return code $FF00 after any of them.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

        .zeropage
dst:    .res    2                       ; the next record's place

        .code
start:  jsr     UT_INIT
        lda     #<fcb
        ldx     #>fcb
        jsr     UT_SPEC
        bcc     @spec
        cmp     #0
        bne     @inv
        lda     #<susage
        ldx     #>susage
        jmp     failsay
@inv:   lda     #<sinv
        ldx     #>sinv
        jmp     failsay
@spec:  lda     fcb + 9                 ; no type: .SID
        cmp     #' '
        bne     @song
        lda     fcb + 10
        cmp     #' '
        bne     @song
        lda     fcb + 11
        cmp     #' '
        bne     @song
        ldx     #2
@typ:   lda     tsid,x
        sta     fcb + 9,x
        dex
        bpl     @typ
@song:  stz     song                    ; the song, if one follows
        stz     song + 1
        jsr     UT_SKIP
        beq     @open
        lda     #<UT_TAIL
        ldx     #>UT_TAIL
        ldy     UT_IDX
        jsr     UT_DECIN
        bcs     @inv
        sta     song
        stx     song + 1
        ora     song + 1
        beq     @inv
@open:  lda     #$FF                    ; CR $FF: open gives the last record's byte count
        sta     fcb + 32
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     @read
        cpx     #0
        beq     @nofile
        jsr     UT_SAID
        jmp     UT_EXIT
@nofile:
        jsr     UT_FAIL
        lda     #<snofile
        ldx     #>snofile
        jsr     UT_SAY
        jmp     UT_EXIT
@read:  lda     fcb + 32
        sta     bcount
        stz     fcb + 32                ; and from the first record
        jsr     UT_MEM                  ; the file from past the BSS to the TPA's top
        lda     UT_MLO
        sta     dst
        sta     buf
        lda     UT_MLO + 1
        sta     dst + 1
        sta     buf + 1
        stz     SID_LEN
        stz     SID_LEN + 1
@rec:   clc                             ; room for a record?
        lda     dst
        adc     #128
        tax
        lda     dst + 1
        adc     #0
        cmp     UT_MHI + 1
        bcc     @room
        bne     @big
        cpx     UT_MHI
        beq     @room
        bcc     @room
@big:   lda     #<sbig
        ldx     #>sbig
        jmp     failsay
@room:  lda     dst
        ldx     dst + 1
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        beq     @got
        cmp     #1
        beq     @eof
        jsr     UT_SAID
        jmp     UT_EXIT
@got:   clc
        lda     dst
        adc     #128
        sta     dst
        bcc     :+
        inc     dst + 1
:       clc
        lda     SID_LEN
        adc     #128
        sta     SID_LEN
        bcc     @rec
        inc     SID_LEN + 1
        bra     @rec
@eof:   lda     bcount                  ; the last record's byte count, 1-127: the rest off
        beq     @parse
        bmi     @parse
        lda     SID_LEN
        ora     SID_LEN + 1
        beq     @parse
        sec
        lda     SID_LEN
        sbc     #128
        tax
        lda     SID_LEN + 1
        sbc     #0
        sta     SID_LEN + 1
        txa
        clc
        adc     bcount
        sta     SID_LEN
        bcc     @parse
        inc     SID_LEN + 1
@parse: lda     buf
        ldx     buf + 1
        jsr     SID_PARSE
        bcs     err
        lda     song
        ldx     song + 1
        jsr     SID_PLAN
        bcs     err
        jsr     SID_PLAY
        bcs     err
        lda     #<splay                 ; Playing song n of m: name, author
        ldx     #>splay
        jsr     CM_PUTS
        lda     SID_SONG
        ldx     SID_SONG + 1
        jsr     dec16
        lda     #<sof
        ldx     #>sof
        jsr     CM_PUTS
        lda     SID_SONGS
        ldx     SID_SONGS + 1
        jsr     dec16
        lda     #<scolon
        ldx     #>scolon
        jsr     CM_PUTS
        lda     SID_NAME
        ldx     SID_NAME + 1
        jsr     text32
        lda     #<scomma
        ldx     #>scomma
        jsr     CM_PUTS
        lda     SID_AUTHOR
        ldx     SID_AUTHOR + 1
        jsr     text32
        jsr     CM_CRLF
        lda     #<skey
        ldx     #>skey
        jsr     UT_SAY
@wait:  lda     #$FF                    ; a key: function 6's input
        ldy     #C_RAWIO
        jsr     BDOS
        cmp     #0
        beq     @wait
        jsr     SID_STOP
        jmp     UT_EXIT

; The library's error A in words, or "Song n of m?".
err:    cmp     #SE_SONG
        beq     @song
        dec     a
        asl     a
        tax
        lda     errs,x
        pha
        lda     errs + 1,x
        tax
        pla
        bra     failsay
@song:  jsr     UT_FAIL
        lda     #<ssong
        ldx     #>ssong
        jsr     CM_PUTS
        lda     song
        ldx     song + 1
        jsr     dec16
        lda     #<sof
        ldx     #>sof
        jsr     CM_PUTS
        lda     SID_SONGS
        ldx     SID_SONGS + 1
        jsr     dec16
        lda     #<sq
        ldx     #>sq
        jsr     UT_SAY
        jmp     UT_EXIT

; The message at A/X, the return code $FF00, the end.
failsay:
        jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT

; A/X in decimal.
dec16:  sta     UT_N
        stx     UT_N + 1
        stz     UT_N + 2
        lda     #0
        clc
        jmp     UT_NOUT

; The 32 bytes at A/X as text, to the first 0, trailing blanks dropped.
text32: sta     @src + 1
        stx     @src + 2
        ldx     #0
@len:   jsr     @src                    ; X: its length, to the first 0
        beq     @trim
        inx
        cpx     #32
        bne     @len
@trim:  cpx     #0
        beq     @out
        dex
        jsr     @src
        cmp     #' '
        beq     @trim
        inx
@out:   stx     tlen
        ldx     #0
@put:   cpx     tlen
        beq     @done
        jsr     @src
        phx
        jsr     CM_PUTC
        plx
        inx
        bra     @put
@done:  rts
@src:   lda     $FFFF,x
        rts

        .rodata
tsid:   .byte   "SID"
susage: .byte   "PLAYSID filespec {song}$"
sinv:   .byte   "Invalid file specification.$"
snofile:
        .byte   "No File$"
sbig:   .byte   "Too big.$"
splay:  .byte   "Playing song $"
sof:    .byte   " of $"
scolon: .byte   ": $"
scomma: .byte   ", $"
skey:   .byte   "Press a key to stop.$"
ssong:  .byte   "Song $"
sq:     .byte   "?$"
errs:   .word   e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13
e1:     .byte   "Not a .sid file.$"
e2:     .byte   "An RSID tune: PLAYSID plays PSID only.$"
e3:     .byte   "Unknown .sid version.$"
e4:     .byte   "Bad data offset.$"
e5:     .byte   "No code in the file.$"
e6:     .byte   "The code runs past the 6510's 64 KB.$"
e7:     .byte   "Bad song count.$"
e8:     .byte   "No such song.$"
e9:     .byte   "A Compute! MUS file needs a player of its own.$"
e10:    .byte   "The tune covers the 6510's vectors.$"
e11:    .byte   "No free page for the player's stub.$"
e12:    .byte   "The sound card is in use, or not there.$"
e13:    .byte   "The sound card's RAM wasn't loaded.$"

        .bss
fcb:    .res    36
song:   .res    2
bcount: .res    1
buf:    .res    2
tlen:   .res    1

        .code
        .include "cmd.inc"
        .include "util.inc"

; SNDFW.COM (hw/future/sound-device.md, "Flash and the update" and "The
; firmware image"; the User's Guide's SNDFW): SNDFW filespec. A new
; firmware image for the sound card (revision 3, the card as a device),
; sent through function 82's SN_FIRMWARE: FW_BEGIN with the image's length,
; the image in pieces into the card's DATA by DMA, then FW_COMMIT and the
; card's FWRESULT. The card checks the image (its magic, card type, length
; and both CRC-32s) and writes it into the flash slot that isn't running,
; marked to boot: the card runs it from its next start (a power-on, or the
; firmware started again). A bad image leaves the old one the one that boots.
;
;   filespec    the image; .FW if its type is left out
;
; The image's 32-byte header gives its length (32 and the body's), so the
; file's last record's padding is never sent. SNDFW checks the magic
; ("OOSD") and the card type ($A3) itself before it sends anything.
;
; It says "Firmware m.n (build b) written: the sound card runs it from its
; next start." Errors: "Invalid file specification.", "No File", "Not a
; sound card firmware image.", "The sound card is in use, or there is no
; card OODOS drives." (every part must be free: SN_FIRMWARE refuses an
; update while any is claimed), "The sound card didn't answer.", "The card
; refused the image: its old firmware stays." (E_FIRMWARE), "The card
; refused the update: error n.", and a file's read error as the BDOS says.
; The return code $FF00 after any of them.

        .setcpu "65C02"
        .macpack longbranch
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

BUFREC  = 32                            ; records a piece (4 KB)
BUFLEN  = BUFREC * 128
E_FIRMWARE = 11                         ; the card's error for a bad image

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
@spec:  lda     fcb + 9                 ; no type: .FW
        cmp     #' '
        bne     @open
        lda     fcb + 10
        cmp     #' '
        bne     @open
        lda     fcb + 11
        cmp     #' '
        bne     @open
        lda     #'F'
        sta     fcb + 9
        lda     #'W'
        sta     fcb + 10
@open:  lda     #<fcb
        ldx     #>fcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     @phys
        cpx     #0
        beq     @nofile
        jsr     UT_SAID
        jmp     UT_EXIT
@nofile:
        lda     #<snofile
        ldx     #>snofile
        jmp     failsay
@phys:  stz     fcb + 32                ; from the first record
        lda     #<buf                   ; the buffer's physical address (function 73)
        sta     pblk
        lda     #>buf
        sta     pblk + 1
        stz     pblk + 2
        stz     pblk + 3
        lda     #<pblk
        ldx     #>pblk
        ldy     #O_PHYSADDR
        jsr     BDOS
        lda     pblk + 2                ; the paragraph x 16 + the offset (0-15)
        asl     a
        asl     a
        asl     a
        asl     a
        ora     pblk
        sta     phys
        lda     pblk + 2
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     phys + 1
        lda     pblk + 3
        asl     a
        asl     a
        asl     a
        asl     a
        ora     phys + 1
        sta     phys + 1
        lda     pblk + 3
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     phys + 2
        jsr     fill                    ; the first piece, and the header in it
        bcc     @hdr
        jmp     said
@hdr:   lda     got                     ; the header whole, its magic and card type
        ora     got + 1
        jeq     @notfw
        lda     got + 1
        bne     @magic
        lda     got
        cmp     #32
        jcc     @notfw
@magic: ldx     #4
@m:     lda     buf,x
        cmp     magic,x
        jne     @notfw
        dex
        bpl     @m
        ldx     #3                      ; its version and build, kept for the end
@ver:   lda     buf + 6,x
        sta     ver,x
        dex
        bpl     @ver
        clc                             ; the image's length: 32 and the body's
        lda     buf + 12
        adc     #32
        sta     total
        sta     left
        ldx     #1
@len:   lda     buf + 12,x
        adc     #0
        sta     total,x
        sta     left,x
        inx
        cpx     #4
        bne     @len
        lda     total + 3               ; 24 bits at most (SN_FIRMWARE's pieces)
        jne     @notfw
        lda     #1                      ; begin: the length, and the first piece
        sta     mode
@piece: jsr     cut                     ; the piece no longer than what is left
        lda     #SN_FIRMWARE
        sta     blk
        ldx     #2
@pa:    lda     phys,x
        sta     blk + 1,x
        dex
        bpl     @pa
        lda     got
        sta     blk + 4
        lda     got + 1
        sta     blk + 5
        stz     blk + 6
        lda     mode
        sta     blk + 7
        ldx     #3
@tl:    lda     total,x
        sta     blk + 8,x
        dex
        bpl     @tl
        jsr     f82
        jne     @fail
        stz     mode                    ; then pieces
        sec                             ; what is left
        lda     left
        sbc     got
        sta     left
        lda     left + 1
        sbc     got + 1
        sta     left + 1
        lda     left + 2
        sbc     #0
        sta     left + 2
        ora     left + 1
        ora     left
        beq     @commit
        jsr     fill
        jcs     said
        lda     got
        ora     got + 1
        bne     @piece
@notfw: lda     #<snotfw                ; (or the file ended before its length)
        ldx     #>snotfw
        jmp     failsay
@commit:
        lda     #SN_FIRMWARE            ; commit: FWRESULT's status
        sta     blk
        stz     blk + 1
        stz     blk + 2
        stz     blk + 3
        stz     blk + 4
        stz     blk + 5
        stz     blk + 6
        lda     #2
        sta     blk + 7
        jsr     f82
        jne     @fail
        lda     #<sdone1                ; Firmware m.n (build b) written ...
        ldx     #>sdone1
        jsr     CM_PUTS
        lda     ver
        jsr     dec8
        lda     #'.'
        jsr     CM_PUTC
        lda     ver + 1
        jsr     dec8
        lda     #<sdone2
        ldx     #>sdone2
        jsr     CM_PUTS
        lda     ver + 2
        ldx     ver + 3
        jsr     dec16
        lda     #<sdone3
        ldx     #>sdone3
        jsr     UT_SAY
        jmp     UT_EXIT
@fail:  cmp     #SN_REFUSED             ; A and X: the refusal in words
        beq     @ref
        cmp     #SN_BUSY
        beq     @busy
        lda     #<sinuse
        ldx     #>sinuse
        jmp     failsay
@busy:  lda     #<snoans
        ldx     #>snoans
        jmp     failsay
@ref:   cpx     #E_FIRMWARE
        bne     @err
        lda     #<sbad
        ldx     #>sbad
        jmp     failsay
@err:   phx
        jsr     UT_FAIL
        lda     #<srefn
        ldx     #>srefn
        jsr     CM_PUTS
        pla
        jsr     dec8
        lda     #'.'
        jsr     CM_PUTC
        jsr     CM_EOL
        jmp     UT_EXIT

; A read error: the BDOS's words, the end.
said:   jsr     UT_SAID
        jmp     UT_EXIT

; The buffer filled from the file, up to BUFREC records: got its bytes (0
; at the end of the file). Carry set on a read error.
fill:   lda     #<buf
        sta     dst
        lda     #>buf
        sta     dst + 1
        stz     got
        stz     got + 1
@rec:   lda     got + 1
        cmp     #>BUFLEN
        beq     @full
        lda     dst
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
        beq     @full                   ; the end of the file
        sec
        rts
@got:   clc
        lda     dst
        adc     #128
        sta     dst
        bcc     :+
        inc     dst + 1
:       clc
        lda     got
        adc     #128
        sta     got
        bcc     @rec
        inc     got + 1
        bra     @rec
@full:  clc
        rts

; got cut to what is left of the image, if that is less.
cut:    lda     left + 2
        bne     @ok
        lda     left + 1
        cmp     got + 1
        bcc     @cut
        bne     @ok
        lda     left
        cmp     got
        bcs     @ok
@cut:   lda     left
        sta     got
        lda     left + 1
        sta     got + 1
@ok:    rts

; Function 82 with blk: A 0 when it was done (Z set); else A and X its result.
f82:    lda     #<blk
        ldx     #>blk
        ldy     #O_SOUND
        jsr     BDOS
        cmp     #0
        rts

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
; A in decimal.
dec8:   ldx     #0
        bra     dec16

        .rodata
magic:  .byte   "OOSD", $A3
susage: .byte   "SNDFW filespec$"
sinv:   .byte   "Invalid file specification.$"
snofile:
        .byte   "No File$"
snotfw: .byte   "Not a sound card firmware image.$"
sinuse: .byte   "The sound card is in use, or there is no card OODOS drives.$"
snoans: .byte   "The sound card didn't answer.$"
sbad:   .byte   "The card refused the image: its old firmware stays.$"
srefn:  .byte   "The card refused the update: error $"
sdone1: .byte   "Firmware $"
sdone2: .byte   " (build $"
sdone3: .byte   ") written: the sound card runs it from its next start.$"

        .bss
fcb:    .res    36
pblk:   .res    4                       ; function 73's block
phys:   .res    3                       ; buf's physical address
blk:    .res    12                      ; function 82's
mode:   .res    1                       ; 1 begin, 0 a piece
total:  .res    4                       ; the image's length
left:   .res    4                       ; what is left to send
got:    .res    2                       ; the bytes in buf
ver:    .res    4                       ; the header's version and build
buf:    .res    BUFLEN

        .code
        .include "cmd.inc"
        .include "util.inc"

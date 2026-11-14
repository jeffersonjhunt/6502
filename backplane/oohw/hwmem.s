; HW_WALK, HW_TAGW, HW_TAGC, HW_AIAW, HW_AIAC and HW_MARCH (oohw.inc): the
; memory tests on a run the CPU reaches (hw/diag.md, "The memory tests").
; The program maps the run itself (DIAG through OOLIB's GX_BLOCK and
; GX_VMAP; the ROM core, from D3, through its own windows), so a test of
; many blocks is a pass over each in turn: every block written first, then
; every block checked, so a write that lands in another block is seen.
;
; The run is HW_PTR (its first CPU address) and HW_LEN (its bytes, 1-65,535;
; 0 is nothing); HW_PTR is not kept. A failure: carry set, HW_BAD the CPU
; address, HW_GOT what was read and HW_WANT what was expected.

        .setcpu "65C02"
        .include "oohw.inc"

        .code
; Walking 1s, then 0s, at HW_PTR: $01, $02 ... $80, then $FE ... $7F, each
; read back. The byte is put back as it was.
HW_WALK:
        lda     (HW_PTR)
        sta     keep
        lda     #$01
@one:   jsr     @try
        bcs     @bad
        asl     a
        bne     @one
        lda     #$FE
@zero:  jsr     @try
        bcs     @bad
        sec
        rol     a
        cmp     #$FF
        bne     @zero
        lda     keep
        sta     (HW_PTR)
        clc
        rts
@try:   sta     (HW_PTR)
        sta     HW_WANT
        lda     (HW_PTR)
        sta     HW_GOT
        cmp     HW_WANT
        bne     @no
        clc
        rts
@no:    sec
        rts
@bad:   lda     HW_PTR
        sta     HW_BAD
        lda     HW_PTR + 1
        sta     HW_BAD + 1
        lda     keep
        sta     (HW_PTR)
        sec
        rts

; The address lines' tags in a 16 KB block at HW_PTR (page aligned): HW_KEY
; at +0, and HW_KEY XOR $FF XOR n at +2^n, n 0-13 (+1 to +$2000). A line
; stuck or two shorted, or two blocks that answer as one, read another's.
HW_TAGW:
        lda     HW_KEY
        sta     (HW_PTR)
        ldx     #0
@tag:   jsr     tagat
        txa
        eor     #$FF
        eor     HW_KEY
        sta     (HW_PTR),y
        jsr     tagend
        inx
        cpx     #14
        bne     @tag
        clc
        rts

; The tags read back: carry set at the first that isn't.
HW_TAGC:
        lda     HW_KEY
        sta     HW_WANT
        ldy     #0
        lda     (HW_PTR)
        cmp     HW_WANT
        bne     @bad0
        ldx     #0
@tag:   txa
        eor     #$FF
        eor     HW_KEY
        sta     HW_WANT
        jsr     tagat
        lda     (HW_PTR),y
        cmp     HW_WANT
        bne     @bad
        jsr     tagend
        inx
        cpx     #14
        bne     @tag
        clc
        rts
@bad0:  sta     HW_GOT
        lda     HW_PTR
        sta     HW_BAD
        lda     HW_PTR + 1
        sta     HW_BAD + 1
        sec
        rts
@bad:   sta     HW_GOT
        tya
        clc
        adc     HW_PTR
        sta     HW_BAD
        lda     HW_PTR + 1
        adc     #0
        sta     HW_BAD + 1
        jsr     tagend
        sec
        rts

; +2^X: below a page, Y; from a page on, HW_PTR's page moved on (tagend
; moves it back).
tagat:  cpx     #8
        bcs     @page
        lda     bit8,x
        tay
        rts
@page:  lda     bit8 - 8,x
        clc
        adc     HW_PTR + 1
        sta     HW_PTR + 1
        ldy     #0
        rts
tagend: cpx     #8
        bcc     @ret
        lda     HW_PTR + 1
        sec
        sbc     bit8 - 8,x
        sta     HW_PTR + 1
@ret:   rts

; Address in address: each byte the low byte of its CPU address XOR its page
; XOR HW_KEY (DIAG's: the block's number, or the card bank's).
HW_AIAW:
        stz     dirn
        jsr     pages
        bcs     @done
@page:  lda     HW_PTR + 1
        eor     HW_KEY
        sta     base
        jsr     count
        ldy     #0
@byte:  tya
        eor     base
        sta     (HW_PTR),y
        iny
        cpy     cnt
        bne     @byte
        inc     HW_PTR + 1
        dec     npg
        bne     @page
@done:  clc
        rts

HW_AIAC:
        stz     dirn
        jsr     pages
        bcs     @done
@page:  lda     HW_PTR + 1
        eor     HW_KEY
        sta     base
        jsr     count
        ldy     #0
@byte:  tya
        eor     base
        cmp     (HW_PTR),y
        bne     @bad
        iny
        cpy     cnt
        bne     @byte
        inc     HW_PTR + 1
        dec     npg
        bne     @page
@done:  clc
        rts
@bad:   sta     HW_WANT
        jmp     failed

; March C-: up or down (each address), (w0); up (r0, w1); up (r1, w0); down
; (r0, w1); down (r1, w0); up or down (r0), with 0 HW_BG and 1 its
; complement. A stuck bit, a transition that doesn't happen, and a cell that
; another address's write changes are each seen; four backgrounds ($00, $55,
; $33, $0F) separate every pair of bits within a byte.
HW_MARCH:
        lda     HW_PTR
        sta     start
        lda     HW_PTR + 1
        sta     start + 1
        lda     HW_BG
        sta     bg
        eor     #$FF
        sta     bgc
        jsr     pages
        bcc     @go
        clc
        rts
@go:    lda     npg
        sta     npg0
        stz     dirn
        ; (w0), up
        jsr     up
@w0:    jsr     count
        ldy     #0
        lda     bg
@w0b:   sta     (HW_PTR),y
        iny
        cpy     cnt
        bne     @w0b
        jsr     upnext
        bne     @w0
        ; (r0, w1), up
        lda     bg
        sta     HW_WANT
        jsr     up
@e1:    jsr     count
        ldy     #0
@e1b:   lda     (HW_PTR),y
        cmp     bg
        bne     @b1
        lda     bgc
        sta     (HW_PTR),y
        iny
        cpy     cnt
        bne     @e1b
        jsr     upnext
        bne     @e1
        bra     @n1
@b1:    jmp     failed
@n1:
        ; (r1, w0), up
        lda     bgc
        sta     HW_WANT
        jsr     up
@e2:    jsr     count
        ldy     #0
@e2b:   lda     (HW_PTR),y
        cmp     bgc
        bne     @b2
        lda     bg
        sta     (HW_PTR),y
        iny
        cpy     cnt
        bne     @e2b
        jsr     upnext
        bne     @e2
        bra     @n2
@b2:    jmp     failed
@n2:
        ; (r0, w1), down
        lda     bg
        sta     HW_WANT
        jsr     down
@e3:    jsr     count
        ldy     cnt
@e3b:   dey
        lda     (HW_PTR),y
        cmp     bg
        bne     @b3
        lda     bgc
        sta     (HW_PTR),y
        cpy     #0
        bne     @e3b
        jsr     downnext
        bne     @e3
        bra     @n3
@b3:    jmp     failed
@n3:
        ; (r1, w0), down
        lda     bgc
        sta     HW_WANT
        jsr     down
@e4:    jsr     count
        ldy     cnt
@e4b:   dey
        lda     (HW_PTR),y
        cmp     bgc
        bne     @b4
        lda     bg
        sta     (HW_PTR),y
        cpy     #0
        bne     @e4b
        jsr     downnext
        bne     @e4
        bra     @n4
@b4:    jmp     failed
@n4:
        ; (r0)
        lda     bg
        sta     HW_WANT
        jsr     up
@e5:    jsr     count
        ldy     #0
@e5b:   lda     (HW_PTR),y
        cmp     bg
        bne     @b5
        iny
        cpy     cnt
        bne     @e5b
        jsr     upnext
        bne     @e5
        bra     @n5
@b5:    jmp     failed
@n5:
        clc
        rts
@bad:   jmp     failed

; From the run's first page, all of them to go.
up:     lda     start
        sta     HW_PTR
        lda     start + 1
        sta     HW_PTR + 1
        lda     npg0
        sta     npg
        stz     dirn
        rts
; The next page up; Z set after the last.
upnext: inc     HW_PTR + 1
        dec     npg
        rts
; From the run's last page.
down:   jsr     up
        clc
        lda     HW_PTR + 1
        adc     npg
        sta     HW_PTR + 1
        dec     HW_PTR + 1
        inc     dirn
        rts
; The next page down; Z set after the first.
downnext:
        dec     HW_PTR + 1
        dec     npg
        rts

; HW_LEN in pieces of 256 from HW_PTR (Y indexes from it, so a run may start
; anywhere): npg the pieces, the last perhaps a part one of last's bytes (0:
; a whole one). Carry set for a length of 0.
pages:  lda     HW_LEN
        sta     last
        lda     HW_LEN + 1
        sta     npg
        lda     last
        beq     @whole
        inc     npg
@whole: lda     HW_LEN
        ora     HW_LEN + 1
        bne     @some
        sec
        rts
@some:  clc
        rts
; This page's bytes to cnt (0: 256): the last page's are last's (the first
; to go down). up and down set dirn; the address in address passes go up.
count:  lda     dirn
        beq     @up
        lda     npg
        cmp     npg0
        bne     @full
        bra     @part
@up:    lda     npg
        cmp     #1
        bne     @full
@part:  lda     last
        sta     cnt
        rts
@full:  stz     cnt
        rts

; A failure at HW_PTR + Y: A what was read is lost to the compare, so the
; byte is read again (a stuck bit reads the same).
failed: lda     (HW_PTR),y
        sta     HW_GOT
        tya
        clc
        adc     HW_PTR
        sta     HW_BAD
        lda     HW_PTR + 1
        adc     #0
        sta     HW_BAD + 1
        sec
        rts

        .rodata
bit8:   .byte   $01, $02, $04, $08, $10, $20, $40, $80

        .bss
HW_LEN: .res    2
HW_KEY: .res    1
HW_BG:  .res    1
HW_BAD: .res    3
HW_GOT: .res    1
HW_WANT: .res   1
keep:   .res    1
base:   .res    1
cnt:    .res    1
npg:    .res    1
npg0:   .res    1
dirn:   .res    1
last:   .res    1
start:  .res    2
bg:     .res    1
bgc:    .res    1

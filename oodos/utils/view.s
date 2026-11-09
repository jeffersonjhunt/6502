; VIEW.COM (hw/oolib.md, "The plan", L4; the User's Guide's VIEW): VIEW
; filespec. An asset file (format 3, oodos/tools/mkasset.py's) shown on
; the screen by OOLIB's linked library (lib/gx*.s, include/oolib.inc) until
; a key is pressed, as PLAYSID plays a tune; the library's test program.
;
;   filespec    the file; .OGX if its type is left out
;
; Its parts are loaded by function 83 (GX_ASSET) to the units they name, or
; to units the library hands out; then the palette's unit is PALBASE, a
; bitmap or a tile map (with its tiles) is layer 0 as its CONFIG says, and
; a sprite table is SPRBASE with the sprites on. It says "Press a key to
; stop." (into the console's text, unseen while the asset shows: question
; 10), waits for a key at the console (function 6), puts the console's
; screen back and ends. The warm boot would put it back anyway.
;
; Errors: "Invalid file specification.", "No File", "Not an asset file.",
; "Not enough video memory.", "Needs OODOS's interface 4.", "The asset
; wasn't loaded." The return code $FF00 after any of them.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

        .zeropage
pp:     .res    2                       ; a part in GX_HDR

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
@spec:  lda     fcb + 9                 ; no type: .OGX
        cmp     #' '
        bne     @init
        lda     fcb + 10
        cmp     #' '
        bne     @init
        lda     fcb + 11
        cmp     #' '
        bne     @init
        ldx     #2
@typ:   lda     togx,x
        sta     fcb + 9,x
        dex
        bpl     @typ
@init:  lda     #0
        tax
        jsr     GX_INIT
        bcs     @err
        lda     #<fcb
        ldx     #>fcb
        jsr     GX_ASSET
        bcc     @ok
@err:   jmp     err
@ok:
        stz     ctrl
        lda     #$FF
        sta     tiles
        lda     #<(GX_HDR + AH_LEN)
        sta     pp
        lda     #>(GX_HDR + AH_LEN)
        sta     pp + 1
        lda     GX_HDR + AH_PARTS
        sta     parts
@part:  ldy     #AP_TYPE                ; the tiles' unit first, for a map after or before
        lda     (pp),y
        cmp     #AT_TILES
        bne     @nexta
        ldy     #AP_UNIT
        lda     (pp),y
        sta     tiles
@nexta: jsr     nextp
        bne     @part
        lda     #<(GX_HDR + AH_LEN)
        sta     pp
        lda     #>(GX_HDR + AH_LEN)
        sta     pp + 1
        lda     GX_HDR + AH_PARTS
        sta     parts
@show:  ldy     #AP_UNIT
        lda     (pp),y
        sta     unit
        ldy     #AP_TYPE
        lda     (pp),y
        cmp     #AT_PALETTE
        bne     @bmp
        lda     unit
        jsr     GX_PALBASE
        bra     @next
@bmp:   cmp     #AT_BITMAP
        beq     @layer
        cmp     #AT_MAP
        bne     @spr
@layer: lda     tiles                   ; layer 0: its CONFIG, its map (or bitmap), its tiles
        sta     GX_V
        ldy     #AP_CONFIG
        lda     (pp),y
        tax
        ldy     unit
        lda     #0
        jsr     GX_LAYER
        lda     #$02
        tsb     ctrl
        bra     @next
@spr:   cmp     #AT_SPRTABLE
        bne     @next
        lda     unit
        jsr     GX_SPRBASE
        lda     #$08
        tsb     ctrl
@next:  jsr     nextp
        bne     @show
        lda     ctrl
        jsr     GX_SHOW
        lda     #<skey
        ldx     #>skey
        jsr     UT_SAY
@wait:  lda     #$FF                    ; a key: function 6's input
        ldy     #C_RAWIO
        jsr     BDOS
        cmp     #0
        beq     @wait
        jsr     GX_CONSOLE
        jmp     UT_EXIT

; pp on to the next part: Z set after the last.
nextp:  clc
        lda     pp
        adc     #AP_LEN
        sta     pp
        bcc     @n
        inc     pp + 1
@n:     dec     parts
        rts

; The library's error A in words.
err:    pha
        jsr     GX_CONSOLE
        pla
        dec     a
        asl     a
        tax
        lda     errs,x
        pha
        lda     errs + 1,x
        tax
        pla
failsay:
        jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT

        .rodata
togx:   .byte   "OGX"
susage: .byte   "VIEW filespec$"
sinv:   .byte   "Invalid file specification.$"
skey:   .byte   "Press a key to stop.$"
errs:   .word   e1, e2, e3, e4, e5, e6
e1:     .byte   "Needs OODOS's interface 4.$"
e2:     .byte   "Video memory W1 can't reach.$"
e3:     .byte   "Not enough video memory.$"
e4:     .byte   "The asset wasn't loaded.$"
e5:     .byte   "No File$"
e6:     .byte   "Not an asset file.$"

        .bss
fcb:    .res    36
ctrl:   .res    1
tiles:  .res    1
unit:   .res    1
parts:  .res    1

        .code
        .include "cmd.inc"
        .include "util.inc"

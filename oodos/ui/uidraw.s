; OOUI's text backend (hw/ooui.md, "Text backend: resources"): its card
; memory, its font and glyphs, its layers, colours and pointer, and the
; painting: every widget but the menus' (uimenu.s) drawn as cells of the
; 80 x 30 text layers.
;
; OOUI's card memory is one card bank (UI_PAGES), so one GX_VMAP shows it
; all at $4000 in W1 (block 33, aperture slot 1): the base layer's map at
; +$0000, the overlay's at +$1400, the pointer's image at +$2800, the font
; at +$3000. Cells are written there by the CPU, a character and an
; attribute (background << 4 | foreground; colour 0 is transparent).
;
; PAINT: a pass for each layer, from the table's first record to its last
; (so a parent is drawn before its children, and a later window over an
; earlier one). A dirty widget is drawn whole and its rectangle (with its
; shadow) added to the layer's damage box; any other that overlaps the box
; is drawn clipped to it. The box is cleared to transparent first. The open
; menu is drawn last, over everything on the overlay.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "ui.inc"

        .bss
mapB:   .res    2                      ; the base map's CPU address (W1), the overlay's,
mapO:   .res    2
cmap:   .res    2                      ;   and the one being drawn
clip:   .res    4                      ; c0, r0, c1, r1: what may be drawn
ucol:   .res    1                      ; the next cell's column (GOTO, EMIT)
roff:   .res    1                      ;   1: its row is clipped
rowo:   .res    2                      ;   its row's offset in a map
uattr:  .res    1                      ; the attribute EMIT writes
uacc:   .res    1                      ;   and the one for a key's letter
er:     .res    4                      ; EXTRECT's: c0, r0, c1, r1
uptr:   .res    1                      ; 1: OOUI set the pointer
dh:     .res    1                      ; the record being drawn
ph:     .res    1                      ; PASS's
pl:     .res    1
pbx:    .res    1
pclr:   .res    1
tc:     .res    1                      ; TEXTLINE's
tr:     .res    1
tw:     .res    1
tl:     .res    1
tal:    .res    1
tlead:  .res    1
rch:    .res    1                      ; REPT's
rn:     .res    1
pm:     .res    1                      ; PUTS's
pidx:   .res    1
psv:    .res    1
bs:     .res    1                      ; BOXD's
bfill:  .res    1
bfr:    .res    1
bw2:    .res    1
bn:     .res    1
brow:   .res    1
bwhich: .res    1
fch:    .res    1                      ; FILLR's
frow:   .res    1
fn:     .res    1
sx:     .res    1                      ; SHADOW's
sy:     .res    1
sn:     .res    1
mo:     .res    2                      ; PTRIMG's
mf:     .res    2
po:     .res    1
pf:     .res    1
pb:     .res    1
pc:     .res    1
gi:     .res    1                      ; GLYPHS'
gn:     .res    1
lb:     .res    1                      ; LAYERS'
lrow:   .res    1                      ; DLABEL's
lrows:  .res    1
pi_:    .res    1                      ; SETPAL's
pdim:   .res    1
pbase:  .res    1
ubofs:  .res    1                      ; the base layer's PALOFS as OOUI set it
upost:  .res    2                      ; PAINT's last call, 0 none (U2), after a paint that drew
udrew:  .res    1                      ;   not 0: this paint drew a record (X4)
pdmg:   .res    8                      ; the last paint's damage, both boxes (POST's, U3a: Z3)
blk:    .res    6                      ; IN_MOUSE's block



        .code

; ---------------------------------------------------------------------------
; Setting up.

        .segment "UCODE"                ; UICOM: every block's (hw/ooui.md, U3a)
; OOUI's bank in W1: mapB its CPU address.
MAPW1:  lda     upage
        ldx     upage + 1
        jsr     GX_VMAP
        sta     mapB
        stx     mapB + 1
        rts
        .code

; The console's font copied to OOUI's, and OOUI's glyphs written over it:
; carry set if the copy was refused.
GLYPHS: lda     #<VM_FONT
        sta     GX_A
        lda     #>VM_FONT
        sta     GX_A + 1
        lda     #^VM_FONT
        sta     GX_A + 2
        stz     GX_C
        lda     upage
        clc
        adc     #UI_OFS_FONT
        sta     GX_C + 1
        lda     upage + 1
        adc     #0
        sta     GX_C + 2
        stz     GX_V
        lda     #>4096
        sta     GX_V + 1
        jsr     GX_VCOPY
        bcs     @r
        jsr     MAPW1
        lda     #1                      ; the vertical bars, 1-15 sixteenths from the bottom
        sta     gi
@v:     lda     gi
        jsr     GLYPHAT
        ldy     #15
@vr:    tya                             ; row y lit if y >= 16 - n
        clc
        adc     gi
        cmp     #16
        lda     #0
        bcc     @vb
        lda     #$FF
@vb:    sta     (UIP),y
        dey
        bpl     @vr
        inc     gi
        lda     gi
        cmp     #16
        bne     @v
        stz     gi                      ; the horizontal bars, 1-7 eighths from the left
@h:     ldx     gi
        lda     HCODES,x
        jsr     GLYPHAT
        ldx     gi
        lda     HMASK,x
        ldy     #15
@hr:    sta     (UIP),y
        dey
        bpl     @hr
        inc     gi
        lda     gi
        cmp     #7
        bne     @h
        stz     gi                      ; the drawn ones
        stz     gn
@d:     ldx     gi
        lda     DCODES,x
        jsr     GLYPHAT
        ldy     #0
        ldx     gn
@dr:    lda     GLYPHDATA,x
        sta     (UIP),y
        inx
        iny
        cpy     #16
        bne     @dr
        stx     gn
        inc     gi
        lda     gi
        cmp     #DCODES_LEN
        bne     @d
        clc
@r:     rts

; UIP = character A's 16 bytes in OOUI's font, through W1.
GLYPHAT:
        stz     UIP + 1
        asl     a
        rol     UIP + 1
        asl     a
        rol     UIP + 1
        asl     a
        rol     UIP + 1
        asl     a
        rol     UIP + 1
        clc
        adc     mapB
        sta     UIP
        lda     UIP + 1
        adc     mapB + 1
        adc     #UI_OFS_FONT
        sta     UIP + 1
        rts

; The layers, the colours and the pointer (ui_open, ui_resume).
SETUP:  jsr     LAYERS
        jsr     SETPAL
        jmp     PTRSET

; The base layer and the overlay (text 80 x 30, OOUI's font), and the
; display on with them, the sprites, and layer 0 if OOUI leaves it to the
; program (the base on 1, no overlay).
LAYERS: lda     upage
        sta     GX_V
        clc
        adc     #UI_OFS_FONT
        sta     GX_R
        lda     upage + 1
        sta     GX_V + 1
        adc     #0
        sta     GX_R + 1
        lda     ubase
        ldx     #0
        jsr     GX_LAYER
        ldy     #TH_GROUP               ; its palette group (the review's P1)
        lda     (UIT),y
        sta     ubofs
        tax
        lda     ubase
        jsr     GX_PALOFS
        lda     ubase
        jsr     LBIT
        sta     lb
        lda     uover
        cmp     #UI_NONE
        beq     @one
        lda     upage
        clc
        adc     #UI_OFS_OVER
        sta     GX_V
        lda     upage + 1
        adc     #0
        sta     GX_V + 1
        lda     uover
        ldx     #0
        jsr     GX_LAYER
        ldy     #TH_OVERGROUP
        lda     (UIT),y
        tax
        lda     uover
        jsr     GX_PALOFS
        lda     uover
        jsr     LBIT
        tsb     lb
        bra     @show
@one:   lda     ubase
        beq     @show
        lda     #$02                    ; the program's layer 0, under OOUI's
        tsb     lb
@show:  lda     lb
        ora     #$08
        jmp     GX_SHOW
; CTRL's bit for layer A.
LBIT:   and     #1
        asl     a
        clc
        adc     #2
        rts

; The theme's colours in the palette's shadow, shown at the next vertical
; blank: TH_PALETTE's 16 (the CGA's for 0) in group TH_GROUP; with TF_DIM
; and a dim group of its own, the same halved in TH_DIMGROUP; TH_OVERPAL's
; (TH_PALETTE's for 0) in TH_OVERGROUP. The groups are the layers' PALOFS
; (LAYERS, PAINT), so a theme keeps out of the program's own entries.
SETPAL: ldy     #TH_PALETTE
        lda     (UIT),y
        sta     UIQ
        iny
        lda     (UIT),y
        sta     UIQ + 1
        ora     UIQ
        bne     @own
        lda     #<CGA
        sta     UIQ
        lda     #>CGA
        sta     UIQ + 1
@own:   stz     pdim
        ldy     #TH_GROUP
        lda     (UIT),y
        jsr     PALGRP
        lda     uover                   ; no overlay layer: no dimming, its group untouched (W8)
        cmp     #UI_NONE
        beq     @over
        ldy     #TH_FLAGS
        lda     (UIT),y
        and     #TF_DIM
        beq     @over
        ldy     #TH_DIMGROUP
        lda     (UIT),y
        ldy     #TH_GROUP
        cmp     (UIT),y
        beq     @over
        inc     pdim
        jsr     PALGRP
        stz     pdim
@over:  ldy     #TH_OVERPAL
        lda     (UIT),y
        iny
        ora     (UIT),y
        beq     @same
        lda     (UIT),y
        sta     UIQ + 1
        dey
        lda     (UIT),y
        sta     UIQ
@same:  ldy     #TH_OVERGROUP
        lda     (UIT),y
        jsr     PALGRP
        jmp     GX_PALSHOW
; UIQ's 16 colours into group A (entries A x 16 on), halved if pdim.
PALGRP: asl     a
        asl     a
        asl     a
        asl     a
        sta     pbase
        stz     pi_
@e:     lda     pi_
        asl     a
        tay
        lda     (UIQ),y
        tax
        iny
        lda     (UIQ),y
        tay
        lda     pdim
        beq     @put
        txa                             ; each component halved
        lsr     a
        and     #$77
        tax
        tya
        lsr     a
        tay
@put:   lda     pi_
        clc
        adc     pbase
        jsr     GX_PALSET
        inc     pi_
        lda     pi_
        cmp     #16
        bne     @e
        rts

; The pointer: unless the program keeps it (UO_OWNMOUSE), or gave OOUI no
; sprite, or no mouse is plugged in: its image, sprite UO_SPRITE on it in
; front of the layers, the mouse in absolute mode over the sprites' 320 x
; 240, and OODOS moving the sprite with it (DM_POINTER).
PTRSET: lda     usprites
        beq     @r
        lda     uoflags
        and     #UO_OWNMOUSE
        bne     @r
        lda     uarea
        sta     UIP
        lda     uarea + 1
        sta     UIP + 1
        ldy     #EV_DEVICES
        lda     (UIP),y
        and     #ED_MOUSE
        beq     @r
        jsr     PTRATTR
        lda     #IN_MOUSE
        sta     blk
        lda     #1
        sta     blk + 1
        lda     #<319
        sta     blk + 2
        lda     #>319
        sta     blk + 3
        lda     #239
        sta     blk + 4
        stz     blk + 5
        lda     #<blk
        ldx     #>blk
        ldy     #O_INPUT
        jsr     BDOS
        lda     usprite
        sta     dmp + 1
        jsr     POINTER
        lda     #1
        sta     uptr
@r:     rts

; The pointer's sprite: its image, the image's address, its depth (in
; front of both layers) and size and group, shown at the next vertical
; blank (PTRSET, ui_theme).
PTRATTR:
        jsr     PTRIMG
        lda     upage
        clc
        adc     #UI_OFS_SPR
        sta     GX_V
        lda     upage + 1
        adc     #0
        sta     GX_V + 1
        lda     usprite
        jsr     GX_SPRIMG
        ldy     #TH_OVERGROUP           ; 16 x 16, in the overlay's group
        lda     (UIT),y
        and     #$0F
        ora     #1 << 6 | 1 << 4
        tay
        lda     usprite
        ldx     #3 << 2                 ; depth 3: in front of both layers
        jsr     GX_SPRATTR
        jsr     GX_SPRSHOW
        rts

; The pointer's image (16 x 16, 4 bits) in the theme's colours, through W1.
PTRIMG: jsr     MAPW1
        lda     mapB
        sta     UIP
        lda     mapB + 1
        clc
        adc     #UI_OFS_SPR
        sta     UIP + 1
        ldy     #TH_POINTER
        lda     (UIT),y
        pha
        and     #$0F
        sta     pf
        pla
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     po
        ldx     #0
        ldy     #0
@row:   lda     POINTERDATA,x
        sta     mo
        lda     POINTERDATA + 1,x
        sta     mo + 1
        lda     POINTERDATA + 2,x
        sta     mf
        lda     POINTERDATA + 3,x
        sta     mf + 1
        lda     #8
        sta     pc
@byte:  jsr     PIX
        asl     a
        asl     a
        asl     a
        asl     a
        sta     pb
        jsr     PIX
        ora     pb
        sta     (UIP),y
        iny
        dec     pc
        bne     @byte
        inx
        inx
        inx
        inx
        cpy     #128
        bne     @row
        rts
; The next pixel's colour: the outline's, the fill's or 0.
PIX:    asl     mf + 1
        rol     mf
        lda     #0
        bcc     @o
        lda     pf
@o:     asl     mo + 1
        rol     mo
        bcc     @r
        lda     po
@r:     rts

; ---------------------------------------------------------------------------
; Painting.

PAINT:  lda     ususp
        beq     @go
        rts
@go:    stz     udrew
        jsr     FINDMODAL               ; the base layer dimmed under a modal window (TF_DIM)
        ldy     #TH_GROUP
        lda     (UIT),y
        tax
        lda     uover
        cmp     #UI_NONE
        beq     @nd
        lda     umodal
        cmp     #UI_NONE
        beq     @nd
        ldy     #TH_FLAGS
        lda     (UIT),y
        and     #TF_DIM
        beq     @nd
        phx                             ; only a modal window on the overlay (round 2's W3)
        lda     umodal
        jsr     RECT
        plx
        lda     rover
        beq     @nd
        ldy     #TH_DIMGROUP
        lda     (UIT),y
        tax
@nd:    cpx     ubofs
        beq     @map
        stx     ubofs
        lda     ubase
        jsr     GX_PALOFS
@map:   jsr     MAPW1
        lda     mapB
        sta     mapO
        lda     mapB + 1
        sta     mapO + 1
        lda     uover
        cmp     #UI_NONE
        beq     @fw
        lda     mapO + 1
        clc
        adc     #UI_OFS_OVER
        sta     mapO + 1
@fw:    lda     ufocus
        jsr     FWIN
        sta     ufwin
        stz     pl                      ; the base layer: box 0, cleared
        stz     pbx
        lda     #1
        sta     pclr
        lda     mapB
        sta     cmap
        lda     mapB + 1
        sta     cmap + 1
        jsr     PASS
        lda     #1                      ; the overlay's
        sta     pl
        lda     uover
        cmp     #UI_NONE
        beq     @same
        ldx     #3                      ; its own layer: box 1, which the base's damage joins (shadows)
@j:     lda     udmg,x
        sta     er,x
        dex
        bpl     @j
        lda     er
        cmp     er + 2
        bcs     @nj
        ldx     #4
        jsr     BOXADD
@nj:    lda     #4
        sta     pbx
        lda     #1
        sta     pclr
        lda     mapO
        sta     cmap
        lda     mapO + 1
        sta     cmap + 1
        bra     @p2
@same:  stz     pclr                    ; on the base: box 0 again, not cleared again
@p2:    jsr     PASS
        jsr     MPASS
        stz     ph                      ; nothing dirty, no damage
@c:     lda     ph
        cmp     utot
        bcs     @e
        jsr     REC
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #<~UF_DIRTY
        sta     (UIR),y
        inc     ph
        bra     @c
@e:     lda     udmg                    ; damage with nothing drawn (a window hidden over
        cmp     udmg + 2                ;   nothing): what shows changed, upost too (Z2)
        bcc     @dmg
        lda     udmg + 4
        cmp     udmg + 6
        bcs     @clr
@dmg:   lda     #1
        sta     udrew
@clr:   ldx     #7                      ; the damage kept for upost (POST: U3a's Z3), then cleared
@z:     lda     udmg,x
        sta     pdmg,x
        stz     udmg,x
        dex
        bpl     @z
        lda     udrew                   ; a widget module's after a paint that drew (a slider's
        beq     PNONE                   ;   sprites, U2)
        lda     upost + 1
        beq     PNONE
        XGO     FARB, XB_POST           ; (a widget module's: block B's, U3a)
PDONE:  rts                             ; (test_oodos_ui times POST to here: Z3)
PNONE:  rts

; One layer's pass (pl the layer, pbx its box, pclr 1 to clear the box,
; cmap its map).
PASS:   ldx     pbx
        lda     udmg,x
        cmp     udmg + 2,x
        bcs     @go
        lda     pclr
        beq     @go
        jsr     CLIPBOX                 ; the box transparent
        ldx     pbx
        ldy     #0
@br:    lda     udmg,x
        sta     UI_RECT,y
        inx
        iny
        cpy     #2
        bne     @br
        lda     udmg,x
        sec
        sbc     UI_RECT
        sta     UI_RECT + 2
        lda     udmg + 1,x
        sec
        sbc     UI_RECT + 1
        sta     UI_RECT + 3
        stz     uattr
        lda     #0
        jsr     FILLR
@go:    stz     ph
@rec:   lda     ph
        cmp     utot
        bcs     @r
        jsr     REC
        ldy     #UW_TYPE
        lda     (UIR),y
        beq     @next
        cmp     #UT_MENU
        bcc     @place
        cmp     #UT_SEP + 1
        bcc     @next                   ; the menus: MPASS
@place: ldy     #UW_FLAGS               ; clean, and the box empty: nothing to draw, and no
        lda     (UIR),y                 ;   RECT to find so (U3a: an idle poll, which drew
        and     #UF_DIRTY               ;   nothing, placed every record twice)
        bne     @rect
        ldx     pbx
        lda     udmg,x
        cmp     udmg + 2,x
        bcs     @next
@rect:  lda     ph
        jsr     RECT
        bcs     @next
        lda     rover
        cmp     pl
        bne     @next
        lda     ph
        jsr     ONE
@next:  inc     ph
        bra     @rec
@r:     rts

; Record A (RECT just placed it) drawn: whole if dirty, its rectangle added
; to box pbx; clipped to the box if it overlaps it; else not.
ONE:    sta     dh
        jsr     REC
        jsr     EXTRECT
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DIRTY
        bne     @dirty
        jsr     OVERLAP
        bcs     @r
        lda     #1                      ; drawn (upost's)
        sta     udrew
        jsr     CLIPBOX
        jsr     CLIPR
        bra     DRAW
@dirty: lda     #1
        sta     udrew
        jsr     CLIPALL
        jsr     CLIPR
        jsr     DRAW
        ldx     pbx
        jmp     BOXADD
@r:     rts

; The menu that is down, last, on the overlay's pass.
MPASS:  lda     umenu
        cmp     #UI_NONE
        beq     @r
        jsr     RECT
        bcs     @r
        lda     umenu
        jmp     ONE
@r:     rts

; Record dh (UIR) drawn, rtype its type, at UI_RECT.
DRAW:   lda     UI_RECT + 2             ; nothing to draw in no cells (O7)
        beq     DNONE
        lda     UI_RECT + 3
        beq     DNONE
        lda     rtype
        cmp     #UT_SEP + 1
        bcs     DWID
        asl     a
        tax
        jmp     (DRAWS,x)
; A widget module's type: its WR_DRAW.
DWID:   ldy     #WR_DRAW
        jmp     WCALL

; The clip held inside rclip, the widget's parents' client areas (RECT's).
CLIPR:  ldx     #1
@a:     lda     rclip,x
        cmp     clip,x
        bcc     @e
        sta     clip,x
@e:     lda     rclip + 2,x
        cmp     clip + 2,x
        bcs     @n
        sta     clip + 2,x
@n:     dex
        bpl     @a
        rts

        .rodata
DRAWS:  .word   DNONE, DDESK, DWIN, DPANEL, DLABEL, DBUTTON, DCHECK, DWID, DWID
        .word   DMBAR, DMENU, DNONE, DNONE
        .code
DNONE:  rts

; er = UI_RECT as c0, r0, c1, r1, with the shadow a window or a menu casts,
; held to the screen. UIR the record.
EXTRECT:
        lda     UI_RECT                 ; (each sum held at $FF on a carry: the review's O7)
        sta     er
        clc
        adc     UI_RECT + 2
        jsr     SAT
        sta     er + 2
        lda     UI_RECT + 1
        sta     er + 1
        clc
        adc     UI_RECT + 3
        jsr     SAT
        sta     er + 3
        jsr     CASTS
        bcs     @hold
        ldy     #TM_SHADOWX
        lda     (UIT),y
        clc
        adc     er + 2
        jsr     SAT
        sta     er + 2
        iny
        lda     (UIT),y
        clc
        adc     er + 3
        jsr     SAT
        sta     er + 3
@hold:  lda     er + 2
        cmp     #UI_COLS + 1
        bcc     @h
        lda     #UI_COLS
        sta     er + 2
@h:     lda     er + 3
        cmp     #UI_ROWS + 1
        bcc     @r
        lda     #UI_ROWS
        sta     er + 3
@r:     rts

; A held at $FF if the add before carried.
SAT:    bcc     @r
        lda     #$FF
@r:     rts

; Carry clear if the record at UIR (rtype) casts a shadow: the theme has
; them, and it is a framed window or a menu.
CASTS:  ldy     #TH_FLAGS
        lda     (UIT),y
        and     #TF_SHADOW
        beq     @no
        lda     rtype
        cmp     #UT_MENU
        beq     @yes
        cmp     #UT_WINDOW
        bne     @no
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_NOFRAME
        bne     @no
@yes:   clc
        rts
@no:    sec
        rts

; Carry clear if er overlaps box pbx.
OVERLAP:
        ldx     pbx
        lda     udmg,x
        cmp     udmg + 2,x
        bcs     @no                     ; empty
        lda     er
        cmp     udmg + 2,x
        bcs     @no
        lda     udmg,x
        cmp     er + 2
        bcs     @no
        lda     er + 1
        cmp     udmg + 3,x
        bcs     @no
        lda     udmg + 1,x
        cmp     er + 3
        bcs     @no
        clc
        rts
@no:    sec
        rts

CLIPBOX:
        ldx     pbx
        ldy     #0
@c:     lda     udmg,x
        sta     clip,y
        inx
        iny
        cpy     #4
        bne     @c
        rts
CLIPALL:
        stz     clip
        stz     clip + 1
        lda     #UI_COLS
        sta     clip + 2
        lda     #UI_ROWS
        sta     clip + 3
        rts

; ---------------------------------------------------------------------------
; The cells.

        .segment "UCODE"                ; UICOM: every block's (hw/ooui.md, U3a)
; The next cell at column A, row X of cmap. UIP its row; roff 1 if clipped.
GOTO:   sta     ucol
        stz     roff
        cpx     clip + 1
        bcc     @off
        cpx     clip + 3
        bcs     @off
        stz     rowo + 1                ; rowo = X x 160: X x 32, and x 128 added
        txa
        asl     a                       ; x 8 (X < 30: still a byte)
        asl     a
        asl     a
        asl     a                       ; x 16
        rol     rowo + 1
        asl     a                       ; x 32
        rol     rowo + 1
        sta     rowo
        ldy     rowo + 1
        asl     a
        rol     rowo + 1
        asl     a
        rol     rowo + 1
        clc
        adc     rowo
        sta     rowo
        tya
        adc     rowo + 1
        sta     rowo + 1
        lda     rowo
        clc
        adc     cmap
        sta     UIP
        lda     rowo + 1
        adc     cmap + 1
        sta     UIP + 1
        rts
@off:   lda     #1
        sta     roff
        rts

; Character A at the next cell in uattr, unless clipped; the column on.
EMIT:   ldx     roff
        bne     @skip
        ldx     ucol
        cpx     clip
        bcc     @skip
        cpx     clip + 2
        bcs     @skip
        pha
        txa
        asl     a
        tay
        pla
        sta     (UIP),y
        iny
        lda     uattr
        sta     (UIP),y
@skip:  inc     ucol                    ; held at 255, never round to column 0
        bne     @r
        dec     ucol
@r:     rts

; Character A, X times.
REPT:   cpx     #0
        beq     @r
        sta     rch
        stx     rn
@l:     lda     rch
        jsr     EMIT
        dec     rn
        bne     @l
@r:     rts

; UIQ's string (0 none), at most X characters: a key's letter in uacc.
PUTS:   stx     pm
        stz     pidx
        lda     UIQ
        ora     UIQ + 1
        beq     @r
@n:     lda     pm
        beq     @r
        ldy     pidx
        lda     (UIQ),y
        beq     @r
        cmp     #$0D                    ; a CR ends a line too (U2: a label's lines)
        beq     @r
        inc     pidx
        cmp     #'&'
        bne     @e
        ldy     pidx
        lda     (UIQ),y
        beq     @r
        inc     pidx
        cmp     #'&'
        beq     @e
        pha
        lda     uattr
        sta     psv
        lda     uacc
        sta     uattr
        pla
        jsr     EMIT
        lda     psv
        sta     uattr
        bra     @d
@e:     jsr     EMIT
@d:     dec     pm
        bra     @n
@r:     rts

; UIQ's string on one row: column A, row X, Y cells wide, aligned by tal
; (0 left, 1 centred, 2 right), the rest spaces.
TEXTLINE:
        sta     tc
        stx     tr
        sty     tw
        lda     UIQ
        sta     UIP
        lda     UIQ + 1
        sta     UIP + 1
        jsr     DLEN
        cmp     tw
        bcc     @fits
        lda     tw
@fits:  sta     tl
        stz     tlead
        lda     tal
        beq     @go
        lda     tw
        sec
        sbc     tl
        ldx     tal
        cpx     #1
        bne     @lead
        lsr     a
@lead:  sta     tlead
@go:    lda     tc
        ldx     tr
        jsr     GOTO
        lda     #' '
        ldx     tlead
        jsr     REPT
        ldx     tl
        jsr     PUTS
        lda     tw
        sec
        sbc     tlead
        sec
        sbc     tl
        tax
        lda     #' '
        jmp     REPT

; Character A over UI_RECT, in uattr.
FILLR:  sta     fch
        lda     UI_RECT + 1
        sta     frow
        lda     UI_RECT + 3
        sta     fn
        beq     @r
@l:     lda     UI_RECT
        ldx     frow
        jsr     GOTO
        lda     fch
        ldx     UI_RECT + 2
        jsr     REPT
        inc     frow
        dec     fn
        bne     @l
@r:     rts

; A frame of style A (TS_*) round UI_RECT in uattr, its inside spaces in
; attribute X.
BOXD:   asl     a
        asl     a
        asl     a
        sta     bs
        stx     bfill
        lda     uattr
        sta     bfr
        lda     UI_RECT + 2
        sec
        sbc     #2
        bcc     @r
        sta     bw2
        lda     UI_RECT + 3
        sec
        sbc     #2
        bcc     @r
        sta     bn
        lda     UI_RECT
        ldx     UI_RECT + 1
        stx     brow
        jsr     GOTO
        ldy     #0                      ; the top: corner, line, corner
        jsr     BLINE
        lda     bn
        beq     @bot
@mid:   inc     brow
        lda     UI_RECT
        ldx     brow
        jsr     GOTO
        ldx     bs
        lda     FRAMES + 6,x
        jsr     EMIT
        lda     bfill
        sta     uattr
        lda     #' '
        ldx     bw2
        jsr     REPT
        lda     bfr
        sta     uattr
        ldx     bs
        lda     FRAMES + 6,x
        jsr     EMIT
        dec     bn
        bne     @mid
@bot:   inc     brow
        lda     UI_RECT
        ldx     brow
        jsr     GOTO
        ldy     #2                      ; the bottom
        jsr     BLINE
@r:     rts
; A frame's top (Y 0) or bottom (Y 2) line.
BLINE:  sty     bwhich
        tya
        clc
        adc     bs
        tax
        phx
        lda     FRAMES,x                ; the left corner
        jsr     EMIT
        plx
        phx
        lda     FRAMES + 4,x            ; the line (+4 the top's, +5 the bottom's)
        ldy     bwhich
        beq     @t
        lda     FRAMES + 3,x
@t:     ldx     bw2
        jsr     REPT
        plx
        lda     FRAMES + 1,x            ; the right corner
        jmp     EMIT
        .code

; The shadow of UI_RECT, if the theme has shadows: the cells to its right
; and below, their characters the base layer's, in TC_SHADOW.
SHADOW: ldy     #TH_FLAGS
        lda     (UIT),y
        and     #TF_SHADOW
        beq     @r
        ldy     #TC_SHADOW
        lda     (UIT),y
        sta     uattr
        ldy     #TM_SHADOWX
        lda     (UIT),y
        sta     sx
        iny
        lda     (UIT),y
        sta     sy
        lda     UI_RECT + 1             ; the right: rows r + sy to r + h + sy - 1
        clc
        adc     sy
        sta     frow
        lda     UI_RECT + 3
        sta     sn
@right: lda     UI_RECT
        clc
        adc     UI_RECT + 2
        ldx     frow
        jsr     GOTOS
        ldx     sx
        jsr     SHADES
        inc     frow
        dec     sn
        bne     @right
        lda     UI_RECT + 1             ; below: from column c + sx to c + w - 1, sy rows
        clc
        adc     UI_RECT + 3
        sta     frow
        lda     sy
        sta     sn
        beq     @r
@below: lda     UI_RECT
        clc
        adc     sx
        ldx     frow
        jsr     GOTOS
        lda     UI_RECT + 2
        sec
        sbc     sx
        bcc     @r
        tax
        jsr     SHADES
        inc     frow
        dec     sn
        bne     @below
@r:     rts
; GOTO, and UIQ the same row of the base layer's map.
GOTOS:  jsr     GOTO
        lda     roff
        bne     @r
        lda     rowo
        clc
        adc     mapB
        sta     UIQ
        lda     rowo + 1
        adc     mapB + 1
        sta     UIQ + 1
@r:     rts
; X shadow cells: each the base's character, in uattr.
SHADES: cpx     #0
        beq     @r
        stx     rn
@l:     lda     roff
        bne     @n
        ldx     ucol
        cpx     clip
        bcc     @n
        cpx     clip + 2
        bcs     @n
        txa
        asl     a
        tay
        lda     (UIQ),y
        sta     (UIP),y
        iny
        lda     uattr
        sta     (UIP),y
@n:     inc     ucol
        dec     rn
        bne     @l
@r:     rts

        .segment "UCODE"                ; UICOM: every block's (hw/ooui.md, U3a)
; uattr the theme's attribute at Y, and uacc it with TC_ACCEL's colour.
TATTR:  lda     (UIT),y
        sta     uattr
        and     #$F0
        sta     uacc
        ldy     #TC_ACCEL
        lda     (UIT),y
        and     #$0F
        ora     uacc
        sta     uacc
        rts
; The same, with no key's colour (a disabled widget's).
NOACC:  jsr     TATTR
        lda     uattr
        sta     uacc
        rts
        .code

; ---------------------------------------------------------------------------
; The widgets.

DDESK:  ldy     #TC_DESKTOP
        jsr     TATTR
        ldy     #TH_DESKCHAR
        lda     (UIT),y
        jmp     FILLR

DWIN:   ldy     #UW_FLAGS
        lda     (UIR),y
        bit     #UF_NOFRAME
        beq     @frame
        ldy     #TC_CLIENT
        jsr     TATTR
        lda     #' '
        jmp     FILLR
@frame: ldy     #TH_FRAMES              ; a dialog's style for a modal window
        and     #UF_MODAL
        beq     @st
        iny
@st:    lda     (UIT),y
        pha
        ldy     #TC_CLIENT
        lda     (UIT),y
        pha
        ldy     #TC_FRAME
        jsr     TATTR
        plx
        pla
        jsr     BOXD
        jsr     TEXTP
        beq     @close
        ldy     #TC_TITLE               ; the title, centred on the top line
        lda     dh
        cmp     ufwin
        beq     @act
        ldy     #TC_TITLEOFF
@act:   jsr     TATTR
        lda     UIQ
        sta     UIP
        lda     UIQ + 1
        sta     UIP + 1
        jsr     DLEN
        sta     tl
        lda     UI_RECT + 2
        sec
        sbc     #4
        bcc     @close
        cmp     tl
        bcs     @fit
        sta     tl
@fit:   lda     UI_RECT + 2
        sec
        sbc     tl
        sec
        sbc     #2
        lsr     a
        clc
        adc     UI_RECT
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #' '
        jsr     EMIT
        ldx     tl
        jsr     PUTS
        lda     #' '
        jsr     EMIT
@close: ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_CLOSE
        beq     @sh
        ldy     #TC_FRAME
        jsr     TATTR
        lda     UI_RECT
        clc
        adc     #2
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #'['
        jsr     EMIT
        lda     #$FE
        jsr     EMIT
        lda     #']'
        jsr     EMIT
@sh:    jmp     SHADOW

; UIQ = the record's text; Z set if it has none.
TEXTP:  ldy     #UW_TEXT
        lda     (UIR),y
        sta     UIQ
        iny
        lda     (UIR),y
        sta     UIQ + 1
        ora     UIQ
        rts

DPANEL: ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_FRAME
        bne     @frame
        ldy     #TC_CLIENT
        jsr     TATTR
        lda     #' '
        jmp     FILLR
@frame: ldy     #TC_CLIENT
        lda     (UIT),y
        pha
        ldy     #TC_FRAME
        jsr     TATTR
        ldy     #TH_FRAMES + 2
        lda     (UIT),y
        plx
        jsr     BOXD
        jsr     TEXTP
        beq     @r
        ldy     #TC_LABEL               ; its title at the top line's left
        jsr     TATTR
        lda     UI_RECT
        clc
        adc     #2
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #' '
        jsr     EMIT
        lda     UI_RECT + 2
        sec
        sbc     #6
        bcc     @r
        tax
        jsr     PUTS
        lda     #' '
        jmp     EMIT
@r:     rts

DLABEL: ldy     #UW_FLAGS
        lda     (UIR),y
        pha
        ldy     #TC_LABEL
        and     #UF_DISABLED
        beq     @en
        ldy     #TC_DISABLED
        jsr     NOACC
        bra     @al
@en:    jsr     TATTR
@al:    pla
        stz     tal
        bit     #UF_CENTRE
        beq     @rt
        inc     tal
@rt:    bit     #UF_RIGHT
        beq     @go
        lda     #2
        sta     tal
@go:    jsr     TEXTP
        lda     UI_RECT + 1             ; a line a row: the text split at CR (U2)
        sta     lrow
        lda     UI_RECT + 3
        sta     lrows
@line:  lda     UI_RECT
        ldx     lrow
        ldy     UI_RECT + 2
        jsr     TEXTLINE
        dec     lrows
        beq     @r
        inc     lrow
        lda     UIQ                     ; past the next CR, or stay on the 0 (blank rows after)
        ora     UIQ + 1
        beq     @line
@cr:    lda     (UIQ)
        beq     @line
        inc     UIQ
        bne     @t
        inc     UIQ + 1
@t:     cmp     #$0D
        bne     @cr
        bra     @line
@r:     rts

DBUTTON:
        jsr     BATTR
        lda     #1
        sta     tal
        jsr     TEXTP
        lda     UI_RECT
        ldx     UI_RECT + 1
        ldy     UI_RECT + 2
        jsr     TEXTLINE
        lda     dh                      ; the focus's: pointers at its ends
        cmp     ufocus
        bne     @r
        lda     UI_RECT + 2
        cmp     #2
        bcc     @r
        lda     UI_RECT
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #$10
        jsr     EMIT
        lda     UI_RECT
        clc
        adc     UI_RECT + 2
        dec     a
        ldx     UI_RECT + 1
        jsr     GOTO
        lda     #$11
        jmp     EMIT
@r:     rts

; A button's attribute: disabled, down (pressed, or a toggle on), the
; focus's, or plain.
BATTR:  ldy     #UW_FLAGS
        lda     (UIR),y
        bit     #UF_DISABLED
        beq     @en
        ldy     #TC_DISABLED
        jmp     NOACC
@en:    ldy     #UW_STATE
        lda     (UIR),y
        and     #US_PRESSED
        bne     @down
        ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_TOGGLE
        beq     @foc
        ldy     #UW_VALUE
        lda     (UIR),y
        beq     @foc
@down:  ldy     #TC_BUTTONDOWN
        jmp     TATTR
@foc:   ldy     #TC_BUTTON
        lda     dh
        cmp     ufocus
        bne     @a
        ldy     #TC_BUTTONFOCUS
@a:     jmp     TATTR

DCHECK: ldy     #UW_FLAGS
        lda     (UIR),y
        and     #UF_DISABLED
        beq     @en
        ldy     #TC_DISABLED
        jsr     NOACC
        bra     @go
@en:    ldy     #TC_CHECK
        lda     dh
        cmp     ufocus
        bne     @a
        ldy     #TC_BUTTONFOCUS
@a:     jsr     TATTR
@go:    lda     UI_RECT
        ldx     UI_RECT + 1
        jsr     GOTO
        ldy     #UW_VALUE
        lda     (UIR),y
        beq     @off
        lda     #UG_BOXON
        bra     @box
@off:   lda     #UG_BOX
@box:   jsr     EMIT
        lda     #' '
        jsr     EMIT
        stz     tal
        jsr     TEXTP
        lda     UI_RECT + 2
        sec
        sbc     #2
        bcc     @r
        tay
        lda     UI_RECT
        clc
        adc     #2
        ldx     UI_RECT + 1
        jmp     TEXTLINE
@r:     rts

        .rodata
; The frames, 8 bytes a style (TS_*): the top's corners, the bottom's, the
; top's line, the bottom's, the sides; +7 unused.
.global FRAMES
        .segment "URODATA"              ; (BOXD's: UICOM's, U3a)
FRAMES: .byte   ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '              ; TS_NONE
        .byte   $DA, $BF, $C0, $D9, $C4, $C4, $B3, $C3              ; TS_SINGLE
        .byte   $C9, $BB, $C8, $BC, $CD, $CD, $BA, $C7              ; TS_DOUBLE
        .byte   $DB, $DB, $DB, $DB, $DF, $DC, $DB, $DB              ; TS_HEAVY
        .rodata

; The horizontal bars' codes and masks, 1-7 eighths.
HCODES: .byte   $12, $13, $14, $15, $16, $17, $1C
HMASK:  .byte   $80, $C0, $E0, $F0, $F8, $FC, $FE

; A glyph from 16 rows of 8: '#' lit.
.macro GROW s
        .local b
        b .set 0
        .repeat 8, i
                .if .strat(s, i) = '#'
                        b .set b | ($80 >> i)
                .endif
        .endrepeat
        .byte   b
.endmacro
DCODES: .byte   UG_BOX, UG_BOXON, UG_RADIO, UG_RADIOON, UG_FOLDER, UG_DRIVE
DCODES_LEN = * - DCODES
GLYPHDATA:
        ; UG_BOX
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "#######."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#######."
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_BOXON
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "#######."
        GROW    "#.....#."
        GROW    "#....##."
        GROW    "#...#.#."
        GROW    "##.#..#."
        GROW    "#.##..#."
        GROW    "#..#..#."
        GROW    "#######."
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_RADIO
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "..###..."
        GROW    ".#...#.."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    "#.....#."
        GROW    ".#...#.."
        GROW    "..###..."
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_RADIOON
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "..###..."
        GROW    ".#...#.."
        GROW    "#..#..#."
        GROW    "#.###.#."
        GROW    "#..#..#."
        GROW    ".#...#.."
        GROW    "..###..."
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_FOLDER
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "###....."
        GROW    "#..#...."
        GROW    "#...####"
        GROW    "#......#"
        GROW    "#......#"
        GROW    "#......#"
        GROW    "#......#"
        GROW    "########"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        ; UG_DRIVE
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "########"
        GROW    "#......#"
        GROW    "#......#"
        GROW    "#....#.#"
        GROW    "#......#"
        GROW    "########"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
        GROW    "........"
.assert * - GLYPHDATA = DCODES_LEN * 16, error, "a glyph is 16 rows"

; The pointer, 16 rows of 16: 'X' its outline, '#' its fill. Each row its
; outline's 16 bits, then its fill's, the leftmost pixel in the first
; byte's bit 7.
.macro PROW s
        .local o, f
        o .set 0
        f .set 0
        .repeat 16, i
                .if .strat(s, i) = 'X'
                        o .set o | ($8000 >> i)
                .elseif .strat(s, i) = '#'
                        f .set f | ($8000 >> i)
                .endif
        .endrepeat
        .byte   >o, <o, >f, <f
.endmacro
POINTERDATA:
        PROW    "X..............."
        PROW    "XX.............."
        PROW    "X#X............."
        PROW    "X##X............"
        PROW    "X###X..........."
        PROW    "X####X.........."
        PROW    "X#####X........."
        PROW    "X######X........"
        PROW    "X#######X......."
        PROW    "X########X......"
        PROW    "X#####XXXXX....."
        PROW    "X##X##X........."
        PROW    "X#X.X##X........"
        PROW    "XX..X##X........"
        PROW    "X....X##X......."
        PROW    ".....XXXX......."

; The CGA colours, for a theme with no palette of its own.
.macro RGB c
        .byte   <(c), >(c)
.endmacro
CGA:    RGB     $000
        RGB     $00A
        RGB     $0A0
        RGB     $0AA
        RGB     $A00
        RGB     $A0A
        RGB     $A50
        RGB     $AAA
        RGB     $555
        RGB     $55F
        RGB     $5F5
        RGB     $5FF
        RGB     $F55
        RGB     $F5F
        RGB     $FF5
        RGB     $FFF

; ui_load (hw/ooui.md, "U3a: OOUI as a banked library"): OOUI.BNK loaded into
; blocks of the program's own and its stubs filled. A/X an FCB with its drive
; and name set, or 0: A:OOUI.BNK, opened in user 0 (the program's user put
; back after, whatever happens). Call it after GX_INIT, before ui_open.
; Carry clear, or set and A: UX_STATE (UISTATE not on a page: checked first),
; UX_BUSY (loaded already), UX_LOAD (no file, a short read, not an OOUI.BNK),
; UX_VERSION (a file older than this program's interface, or one needing
; more UISTATE or imports than it gives), UX_MEMORY (no block free), UX_GX
; (OOLIB refused a block in W1: GX_INIT not called first, or it refused). On
; a refusal after the first block is taken, every block taken is freed
; (function 57), the W1 map read again (GX_REMAP) and block 1 put back in
; W1; the file is closed and the user put back.
;
; How (mkbank.py's file): record 0, the header, by DM_LOAD into UISTATE's
; first 128 bytes (physical, function 73: the DMA address and segment are
; never changed); a block for each of the file's by function 55; each image
; by DM_LOAD to its block's $0800 on; then each block in W1 (GX_BLOCK), the
; top 2 KB of common memory copied to its top 2 KB ($7800-$7FFF) for
; FARCALL, and its fixups applied, a record at a time into UISTATE's second
; 128 bytes; then block 1 back (GX_W1BACK), the file closed, UISTATE zeroed
; (a program's BSS isn't cleared, and OOUI's state starts at 0: round 1's D2)
; and the stubs filled from the header's entry table. Everything it uses is
; in UISTATE or below, outside $4000-$7FFF.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"
        .include "../ui/uiimp.inc"
        .import UISTATE, UI_STUBS
        .importzp UIP
        .export ui_load, UI_BLOCKS

UB_FORMAT       = 1                    ; mkbank.py's
UB_BLOCKS       = 3
HDR             = UISTATE              ; the header's record (zeroed at the end with the rest)
BUF             = UISTATE + $80        ; a fixups' record
LV              = UISTATE + $100       ; ui_load's own, zeroed at the end too
lfcb    = LV                           ; 36: the default FCB, filled in
lblk    = LV + 36                      ; 11: DM_LOAD's block
lmcb    = LV + 47                      ; UB_BLOCKS x 5: the blocks' MCBs
lblks   = LV + 62                      ; UB_BLOCKS: their numbers
lnb     = LV + 65                      ; the blocks taken
luser   = LV + 66                      ; the program's user
lfcbp   = LV + 67                      ; 2: the FCB in use
lerr    = LV + 69
lb      = LV + 70                      ; the block being done
lphys   = LV + 71                      ; 3: BUF's physical address (HDR's is $80 below)
lver    = LV + 74                      ; 10: function 72's block (FARCALL at +8)
limp    = LV + 84                      ; 2 x the imports: their values (FARCALL first)
lleft   = LV + 84 + 2 * 32             ; 2: the fixups left
lrec    = lleft + 2                    ; 2: the next fixups' record
lbi     = lrec + 2                     ; the index in BUF
lpt     = lbi + 1                      ; 2: the patch's address, in W1
lk      = lpt + 2                      ; the kind
lo      = lk + 1                       ; 4: function 73's block
.assert lo + 4 - UISTATE <= UI_STATE_LEN, error, "ui_load's own fit UISTATE"

; The calls' count, as uistub.s makes them.
UI_NSTUBS .set 0
.macro UIENT name
UI_NSTUBS .set UI_NSTUBS + 1
.endmacro
        UI_ENTRIES

.macro UIIMP name
.if .xmatch(name, FARCALL)
        .word   0                       ; (ui_load's, from function 72)
.else
        .word   name
.endif
.endmacro

        .rodata
; The program's values for the file's imports, FARCALL's (0) filled in.
IMPS:   UI_IMPORTS
NIMPS   = (* - IMPS) / 2
.assert NIMPS <= 32, error, "limp holds 32"
DEFFCB: .byte   1, "OOUI    BNK"
DEFFCB_LEN = * - DEFFCB

        .data
lflag:  .byte   0                      ; 1: loaded (the program part's DATA, not UISTATE's)
UI_BLOCKS: .res UB_BLOCKS              ; OOUI's blocks, A, B and C (for a debugger, and the tests)

        .code

ui_load:
        cld
        sta     UIP                     ; (UIP is OOUI's: nothing is kept in it between calls)
        stx     UIP + 1
        lda     #<UISTATE               ; on a page, or nothing works (round 2's E2)
        beq     @aligned
        lda     #UX_STATE
        sec
        rts
@aligned:
        lda     lflag
        beq     @fresh
        lda     #UX_BUSY
        sec
        rts
@fresh: ldx     #0                      ; ui_load's own variables 0 (lnb, among them)
@z:     stz     LV,x
        inx
        cpx     #lo + 4 - LV
        bne     @z
        lda     #$FF                    ; the program's user
        ldy     #F_USERNUM
        jsr     BDOS
        sta     luser
        lda     UIP
        ora     UIP + 1
        bne     @given
        ldx     #DEFFCB_LEN - 1         ; the default name, in user 0
@f:     lda     DEFFCB,x
        sta     lfcb,x
        dex
        bpl     @f
        lda     #<lfcb
        sta     lfcbp
        lda     #>lfcb
        sta     lfcbp + 1
        lda     #0
        ldy     #F_USERNUM
        jsr     BDOS
        bra     @open
@given: lda     UIP
        sta     lfcbp
        lda     UIP + 1
        sta     lfcbp + 1
@open:  lda     lfcbp
        ldx     lfcbp + 1
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #4
        bcc     @opened
        lda     #UX_LOAD
        jmp     USER                    ; (not opened: nothing to close)
@opened:
        lda     #<lo                    ; BUF's physical address (function 73)
        sta     UIP
        lda     #>lo
        sta     UIP + 1
        lda     #<BUF
        sta     lo
        lda     #>BUF
        sta     lo + 1
        lda     #<lo
        ldx     #>lo
        ldy     #O_PHYSADDR
        jsr     BDOS
        cmp     #0
        bne     @short
        lda     lo + 2                  ; paragraph x 16 + offset (0-15)
        asl     a
        asl     a
        asl     a
        asl     a
        ora     lo
        sta     lphys
        lda     lo + 2
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     lphys + 1
        lda     lo + 3
        asl     a
        asl     a
        asl     a
        asl     a
        ora     lphys + 1
        sta     lphys + 1
        lda     lo + 3
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     lphys + 2
@hdr:   lda     lphys                   ; record 0 to HDR (BUF less $80)
        sec
        sbc     #$80
        sta     lblk + 8
        lda     lphys + 1
        sbc     #0
        sta     lblk + 9
        lda     lphys + 2
        sbc     #0
        sta     lblk + 10
        lda     #0
        tax
        ldy     #1
        jsr     LOAD
        bcc     @check
@short: lda     #UX_LOAD
        jmp     CLOSE
@check: lda     HDR                     ; 'OUB' and format 1
        cmp     #'O'
        bne     @short
        lda     HDR + 1
        cmp     #'U'
        bne     @short
        lda     HDR + 2
        cmp     #'B'
        bne     @short
        lda     HDR + 3
        cmp     #UB_FORMAT
        bne     @short
        lda     HDR + 5
        cmp     #UB_BLOCKS
        bne     @short
        lda     HDR + 4                 ; an interface as new as this program's
        cmp     #UI_IFACE
        bcc     @old
        lda     #<UI_STATE_LEN          ; its UISTATE within this program's
        cmp     HDR + 6
        lda     #>UI_STATE_LEN
        sbc     HDR + 7
        bcc     @old
        lda     #NIMPS                  ; its imports this program's (or fewer)
        cmp     HDR + 8
        bcc     @old
        lda     HDR + 9                 ; and every call this program has
        cmp     #UI_NSTUBS
        bcs     @far
@old:   lda     #UX_VERSION
        jmp     CLOSE
@far:   lda     #<lver                  ; FARCALL's address (function 72), import 0
        ldx     #>lver
        ldy     #O_VERSION
        jsr     BDOS
        lda     lver + 8
        sta     limp
        lda     lver + 9
        sta     limp + 1
        ldx     #2                      ; and the program's own
@i:     lda     IMPS,x
        sta     limp,x
        inx
        cpx     #NIMPS * 2
        bne     @i
@alloc: lda     lnb                     ; a block for each of the file's
        cmp     #UB_BLOCKS
        beq     @loaded
        jsr     MCB                     ; UIP its MCB: base 0, 1,024 paragraphs
        lda     #4
        sta     (UIP),y                 ; (MCB leaves Y on M_LENGTH + 1)
        lda     UIP
        ldx     UIP + 1
        ldy     #MC_ALLOC
        jsr     BDOS
        cmp     #0
        beq     @took
        lda     #UX_MEMORY
        jmp     FREE
@took:  ldy     #1                      ; its block: base / 1,024
        lda     (UIP),y
        lsr     a
        lsr     a
        ldx     lnb
        sta     lblks,x
        inc     lnb
        bra     @alloc
@loaded:
        jsr     GX_REMAP                ; the blocks in the W1 map
        stz     lb
@img:   jsr     IMAGE
        bcs     @bad
        inc     lb
        lda     lb
        cmp     #UB_BLOCKS
        bne     @img
        jsr     GX_W1BACK
        lda     lfcbp                   ; closed, the user back
        ldx     lfcbp + 1
        ldy     #F_CLOSE
        jsr     BDOS
        lda     luser
        ldy     #F_USERNUM
        jsr     BDOS
        jsr     STUBS                   ; the stubs, from HDR (nothing can fail past here)
        lda     #<UISTATE               ; UISTATE zeroed (and with it all of the above)
        sta     UIP
        lda     #>UISTATE
        sta     UIP + 1
        ldx     #>UI_STATE_LEN
        lda     #0
        tay
@zs:    sta     (UIP),y
        iny
        bne     @zs
        inc     UIP + 1
        dex
        bne     @zs
.assert <UI_STATE_LEN = 0, error, "UISTATE is whole pages"
        lda     #1
        sta     lflag
        clc
        rts
@bad:   jmp     FREE

; UIP = lmcb + 5 x lnb, zeroed, its length's low byte 0; Y = 3 (M_LENGTH + 1).
MCB:    lda     lnb
        asl     a
        asl     a
        adc     lnb
        adc     #<lmcb
        sta     UIP
        lda     #>lmcb
        adc     #0
        sta     UIP + 1
        lda     #0
        ldy     #4
@z:     sta     (UIP),y
        dey
        bpl     @z
        ldy     #3
        rts

; DM_LOAD: A/X the first record, Y the count (records), lblk + 8-10 the
; destination: carry set unless all loaded.
LOAD:   sta     lblk + 3
        stx     lblk + 4
        stz     lblk + 5
        sty     lblk + 6
        stz     lblk + 7
        lda     #DM_LOAD
        sta     lblk
        lda     lfcbp
        sta     lblk + 1
        lda     lfcbp + 1
        sta     lblk + 2
        lda     #<lblk
        ldx     #>lblk
        ldy     #O_DMA
        jsr     BDOS
        cmp     #1                      ; carry clear for 0
        rts

; Block lb: its image loaded, then in W1 with common memory and its fixups.
; Carry set and lerr the error.
IMAGE:  lda     lb                      ; HDR + 10 + 8 x lb: its image's record and length
        asl     a
        asl     a
        asl     a
        tax
        ldy     lb                      ; the destination: the block x $4000 + $0800
        lda     lblks,y
        lsr     a
        lsr     a
        sta     lblk + 10
        lda     lblks,y
        and     #3
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        ora     #$08
        sta     lblk + 9
        stz     lblk + 8
        lda     HDR + 12,x              ; the records: (length + 127) / 128, 96 at most
        clc
        adc     #127
        sta     lk
        lda     HDR + 13,x
        adc     #0
        asl     lk
        rol     a
        tay
        lda     HDR + 10,x
        pha
        lda     HDR + 11,x
        tax
        pla
        jsr     LOAD
        bcc     @w1
        lda     #UX_LOAD
        sta     lerr
        sec
        rts
@w1:    ldy     lb
        lda     lblks,y
        jsr     GX_BLOCK
        bcc     @common
        lda     #UX_GX
        sta     lerr
        sec
        rts
@common:
        ldx     #0                      ; $F800-$FFFF to $7800-$7FFF, 256 bytes x 8
@c:
.repeat 8, i
        lda     $F800 + i * $100,x
        sta     $7800 + i * $100,x
.endrepeat
        inx
        bne     @c
        lda     lb                      ; the fixups: their first record and count
        asl     a
        asl     a
        asl     a
        tax
        lda     HDR + 14,x
        sta     lrec
        lda     HDR + 15,x
        sta     lrec + 1
        lda     HDR + 16,x
        sta     lleft
        lda     HDR + 17,x
        sta     lleft + 1
        lda     #<$4800                 ; the image's start, $C800, in W1
        sta     lpt
        lda     #>$4800
        sta     lpt + 1
        lda     #$80                    ; BUF empty
        sta     lbi
@fix:   lda     lleft
        ora     lleft + 1
        bne     @more
        clc
        rts
@more:  lda     lbi
        bpl     @have
        lda     lphys                   ; the next record into BUF
        sta     lblk + 8
        lda     lphys + 1
        sta     lblk + 9
        lda     lphys + 2
        sta     lblk + 10
        lda     lrec
        ldx     lrec + 1
        ldy     #1
        jsr     LOAD
        bcc     @rd
        lda     #UX_LOAD
        sta     lerr
        sec
        rts
@rd:    inc     lrec
        bne     @z0
        inc     lrec + 1
@z0:    stz     lbi
@have:  ldx     lbi
        lda     BUF,x                   ; the step
        clc
        adc     lpt
        sta     lpt
        bcc     @k
        inc     lpt + 1
@k:     lda     BUF + 1,x               ; the kind
        sta     lk
        inx
        inx
        stx     lbi
        lda     lleft
        bne     @dec
        dec     lleft + 1
@dec:   dec     lleft
        jsr     PATCH
        bra     @fix

; Fixup lk at lpt (through W1).
PATCH:  lda     lpt
        sta     UIP
        lda     lpt + 1
        sta     UIP + 1
        lda     lk
        bmi     @word
        beq     @page
        cmp     #1
        beq     @zp
        cmp     #2
        beq     @blk
        rts                             ; $FF: a step and nothing
@page:  lda     (UIP)
        clc
        adc     #<((>UISTATE) - $10)
        sta     (UIP)
        rts
@zp:    lda     (UIP)
        clc
        adc     #<(UIP - $10)
        sta     (UIP)
        rts
@blk:   lda     (UIP)                   ; 1-3: the block's number
        tax
        lda     lblks - 1,x
        sta     (UIP)
        rts
@word:  and     #$7F                    ; import i: the word - $4000 - i x $400 + its value
        asl     a
        tax                             ; X = 2i (limp's index)
        asl     a                       ; A = 4i: the slot's high byte past $40
        clc
        adc     #$40
        sta     lo                      ; (lo: a scratch byte here)
        ldy     #1
        lda     (UIP),y
        sec
        sbc     lo
        sta     lo
        lda     (UIP)
        clc
        adc     limp,x
        sta     (UIP)
        lda     lo
        adc     limp + 1,x
        sta     (UIP),y
        rts

; The stubs from the header's entry table (+34: for each call its block,
; 0-2, and its address): JSR FARCALL, the block's number, the address, RTS.
STUBS:  ldx     #UB_BLOCKS - 1
@b:     lda     lblks,x
        sta     UI_BLOCKS,x
        dex
        bpl     @b
        lda     #<UI_STUBS
        sta     UIP
        lda     #>UI_STUBS
        sta     UIP + 1
        ldx     #0
@s:     cpx     #UI_NSTUBS * 3
        beq     @done
        lda     #$20                    ; JSR abs
        sta     (UIP)
        ldy     #1
        lda     limp                    ; FARCALL
        sta     (UIP),y
        iny
        lda     limp + 1
        sta     (UIP),y
        iny
        phx
        lda     HDR + 34,x              ; the block: its number
        tax
        lda     lblks,x
        plx
        sta     (UIP),y
        iny
        lda     HDR + 35,x
        sta     (UIP),y
        iny
        lda     HDR + 36,x
        sta     (UIP),y
        iny
        lda     #$60                    ; RTS
        sta     (UIP),y
        lda     UIP
        clc
        adc     #UI_STUB_LEN
        sta     UIP
        bcc     @n
        inc     UIP + 1
@n:     inx
        inx
        inx
        bra     @s
@done:  rts

; A refusal after the blocks were taken (lnb of them): each freed, the map
; read again, block 1 back; then CLOSE and USER. A the error.
FREE:   sta     lerr
@f:     lda     lnb
        beq     @none
        dec     lnb
        jsr     MCB                     ; (MCB zeroes it: its base again from lblks)
        ldx     lnb
        lda     lblks,x
        asl     a
        asl     a
        ldy     #1
        sta     (UIP),y                 ; base = block x 1,024 paragraphs
        lda     #4
        ldy     #3
        sta     (UIP),y
        lda     UIP
        ldx     UIP + 1
        ldy     #MC_FREE
        jsr     BDOS
        bra     @f
@none:  jsr     GX_REMAP
        jsr     GX_W1BACK
        lda     lerr
CLOSE:  sta     lerr
        lda     lfcbp
        ldx     lfcbp + 1
        ldy     #F_CLOSE
        jsr     BDOS
        lda     lerr
USER:   sta     lerr
        lda     luser
        ldy     #F_USERNUM
        jsr     BDOS
        lda     lerr
        sec
        rts


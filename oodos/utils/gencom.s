; GENCOM.COM (hw/oodos.md, "The RSXs, phase 5", 5b; the User's Guide's
; GENCOM, 5-40): RSXs attached to a .COM, as tools/gencom.py does, to the byte.
;
;   GENCOM name rsx {rsx...} {[LOADER | SCB=(o,v){,(o,v)...}]}   attach, or add
;                                               and replace in one made before
;   GENCOM name [LOADER | SCB=(o,v)...]         SCB entries only; LOADER alone
;                                               leaves the file as it was
;   GENCOM rsx {rsx...} [NULL {SCB=(o,v)...}]   an RSX-only .COM, named after
;                                               the first RSX
;   GENCOM name                                 what was attached taken off
;
; name's type is COM if none is given, an rsx's RSX. The new file is written
; to NAME.$$$ on name's drive, then NAME.COM deleted and NAME.$$$ renamed to
; it. The attached RSX table follows the image's last record (16-byte entries:
; the RSX file's record, the flags, RT_NONBANK when the RSX's prefix says
; non-banked, RT_SCB for an SCB entry with its offset and value, the name, the
; RSX file's, 5 reserved), then each RSX whole, on its records; header bytes 12
; and 14 say how many and where. SCB=(o,v) and SCB=(o,v),(o,v) both, in hex;
; the offset $00-$63 (ours: OODOS's SCB ends there). An RSX named again
; replaces the attached one of its name; one named twice is taken once; an
; SCB offset given again replaces its entry.
;
; Messages (the Guide's Appendix A): First submitted file must be a COM file;
; COM file found and NULL option; Duplicate input RSX; Duplicate RSX in
; header. Replacing old by new; Invalid RSX type; Invalid wild card in the FCB
; name or type field; Total file size exceeds 64K; No more space in the
; header for RSXs or SCB initialization.; Unrecognized option; No modifier
; for this option; Missing left parenthesis.; Missing right Parenthesis;
; Missing SCB value.; Invalid SCB offset; Read only; Cannot delete file; No
; File (ours); the BDOS's, said. The return code $FF00 after an error.

        .setcpu "65C02"
        .include "oodos.inc"
        .macpack longbranch

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

MAXSPEC = 16
MAXENT  = 15
FCBLEN  = 36
RECORD  = 128
K_OLD   = 0                             ; an entry: an RSX in the old .COM,
K_NEW   = 1                             ;   one from its own file,
K_SCB   = 2                             ;   an SCB byte
E_KIND  = 0                             ; an entry's fields
E_NAME  = 1                             ;   8 bytes
E_REC   = 9                             ;   where it is read from (K_OLD: the old file's record)
E_LEN   = 11                            ;   its records
E_SPEC  = 13                            ;   K_NEW: its specification; K_SCB: the offset
E_FLAGS = 14                            ;   RT_NONBANK; K_SCB: the value
E_OUT   = 15                            ;   where it goes in the new file (2)
ELEN    = 17

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        stz     nspec
        stz     nent
        stz     onull
        stz     oloader
        stz     nscb
        jsr     options
@spec:  lda     nspec                   ; the specifications
        cmp     #MAXSPEC
        bcs     @specs
        jsr     specat
        jsr     UT_SPEC
        bcs     @last
        inc     nspec
        bra     @spec
@last:  cmp     #0
        beq     @specs
        jmp     wild                    ; one 152 refuses
@specs: lda     nspec
        bne     :+
        jmp     notcom
:       ldx     #0                      ; no wildcards
@w:     stx     si
        txa
        jsr     specadr
        clc
        adc     #1
        bcc     :+
        inx
:       jsr     UT_ISWILD
        bcc     :+
        jmp     wild
:       ldx     si
        inx
        cpx     nspec
        bne     @w
        ; the types: the first COM (or, with NULL, RSX); the rest RSX
        lda     #0
        jsr     specadr
        jsr     settype
        ldx     #1
@t:     cpx     nspec
        beq     @typed
        stx     si
        txa
        jsr     specadr
        jsr     rsxtype
        ldx     si
        inx
        bra     @t
@typed: lda     onull
        beq     old
        jmp     null

; ---- the old .COM: its header, image and attached table ----------------------

old:    lda     nspec                   ; LOADER alone: nothing to do
        cmp     #1
        bne     :+
        lda     nscb
        bne     :+
        lda     oloader
        beq     :+
        jmp     UT_EXIT
:       lda     #0
        jsr     specadr
        jsr     openf
        jsr     setsrc
        lda     #0                      ; record 0: the header
        tax
        jsr     rdsrc
        ldx     #3
@m:     lda     rbuf,x
        cmp     MAGIC,x
        bne     @notcom
        dex
        bpl     @m
        lda     rbuf + OH_FLAGS
        bne     @notcom
        ldx     #OH_LEN - 1
:       lda     rbuf,x
        sta     hdr,x
        dex
        bpl     :-
        lda     hdr + OH_SIZE           ; the image's records
        ldy     hdr + OH_SIZE + 1
        jsr     imgrecs
        sta     irec
        stx     irec + 1
        stz     oi                      ; the table's entries kept as K_OLD and K_SCB
@ent:   lda     oi
        cmp     hdr + OH_RSXCOUNT
        bcs     @kept
        jsr     oldent
        inc     oi
        bra     @ent
@notcom: jmp    notcom
@kept:  lda     nspec                   ; the name alone, no options: all taken off
        cmp     #1
        bne     newrsx
        lda     nscb
        bne     newrsx
        stz     nent
        jmp     write

; ---- NULL: a header record of size 0 ----------------------------------------

null:   ldx     #RECORD - 1
:       stz     hdr,x
        dex
        bpl     :-
        ldx     #3
:       lda     MAGIC,x
        sta     hdr,x
        dex
        bpl     :-
        lda     #>BP_TPA
        sta     hdr + OH_PAGE
        lda     #1
        sta     irec
        stz     irec + 1
        ; falls through

; ---- the RSXs given, each from its file ---------------------------------------

newrsx: ldx     #0
        lda     onull
        bne     :+
        inx                             ; (not the .COM)
:       stx     si
@each:  lda     si
        cmp     nspec
        jcs     scbs
        jsr     specadr
        sta     cfcb
        stx     cfcb + 1
        jsr     dupin                   ; named before in this command?
        bcs     @next
        lda     cfcb
        ldx     cfcb + 1
        jsr     openf
        jsr     setrsx
        lda     #0
        tax
        jsr     rdrsx
        ldx     #3
@m:     lda     rbuf,x
        cmp     MAGIC,x
        bne     @bad
        dex
        bpl     @m
        lda     rbuf + OH_FLAGS
        cmp     #OF_RELOC | OF_RSX
        bne     @bad
        jsr     rsxlen                  ; its records
        ldx     #ELEN - 1               ; the new entry, in tent
:       stz     tent,x
        dex
        bpl     :-
        lda     #K_NEW
        sta     tent + E_KIND
        lda     si
        sta     tent + E_SPEC
        lda     rlen
        sta     tent + E_LEN
        lda     rlen + 1
        sta     tent + E_LEN + 1
        lda     rbuf + OH_LEN + RSX_NONBANK
        cmp     #$FF
        bne     :+
        lda     #RT_NONBANK
        sta     tent + E_FLAGS
:       ldy     #1
:       jsr     cpeek
        and     #$7F
        sta     tent + E_NAME - 1,y
        iny
        cpy     #9
        bne     :-
        jsr     place                   ; replacing one of its name, or added
@next:  inc     si
        jmp     @each
@bad:   lda     #<sbadrsx
        ldx     #>sbadrsx
        jmp     fail

; ---- the SCB entries given ---------------------------------------------------

scbs:   stz     si
@each:  lda     si
        cmp     nscb
        bcs     write
        asl     a
        tax
        lda     scbo,x
        sta     tent + E_SPEC
        lda     scbo + 1,x
        sta     tent + E_FLAGS
        lda     #K_SCB
        sta     tent + E_KIND
        jsr     scbplace                ; an entry of the offset replaced, or added
        inc     si
        bra     @each

; ---- the new file -------------------------------------------------------------

write:  lda     nent
        cmp     #MAXENT + 1
        bcc     :+
        lda     #<sfull
        ldx     #>sfull
        jmp     fail
:       ; where each RSX goes: after the table's records
        lda     nent
        asl     a
        asl     a
        asl     a
        stz     trecs                   ; the table's records: 8 entries a record
        lda     nent
        beq     @lay
        ldx     #1
        cmp     #8 + 1
        bcc     :+
        inx
:       stx     trecs
@lay:   clc
        lda     irec
        adc     trecs
        sta     cur
        lda     irec + 1
        adc     #0
        sta     cur + 1
        ldx     #0
@pl:    cpx     nent
        beq     @sized
        stx     ei
        jsr     entget
        lda     tent + E_KIND
        cmp     #K_SCB
        beq     @nopl
        lda     cur
        sta     tent + E_OUT
        lda     cur + 1
        sta     tent + E_OUT + 1
        clc
        lda     cur
        adc     tent + E_LEN
        sta     cur
        lda     cur + 1
        adc     tent + E_LEN + 1
        sta     cur + 1
        jsr     entput
@nopl:  ldx     ei
        inx
        bra     @pl
@sized: lda     cur + 1                 ; 512 records: 64 KB
        cmp     #>512
        bcc     :+
        bne     @big
        lda     cur
        beq     :+
@big:   lda     #<sbig
        ldx     #>sbig
        jmp     fail
:       jsr     target                  ; NAME.COM there? read-only: refused
        jsr     outmake                 ; NAME.$$$
        jsr     wimage
        jsr     wtable
        jsr     wrsxs
        jmp     replace

; ==== the pieces ================================================================

; A = the specification index: A/X its FCB.
specadr:
        tax
        lda     #<specs
        sta     sp
        lda     #>specs
        sta     sp + 1
@m:     cpx     #0
        beq     @d
        clc
        lda     sp
        adc     #FCBLEN
        sta     sp
        bcc     :+
        inc     sp + 1
:       dex
        bra     @m
@d:     lda     sp
        ldx     sp + 1
        rts
; UT_SPEC's FCB: the next specification's.
specat: lda     nspec
        jsr     specadr
        rts

; The first's type: COM (blank made COM); RSX with NULL.
settype:
        sta     cfcb
        stx     cfcb + 1
        lda     onull
        bne     rsxtype2
        ldy     #9
        jsr     cpeek
        cmp     #' '
        bne     @given
        ldx     #2
:       lda     COMT,x
        jsr     cpokex
        dex
        bpl     :-
        rts
@given: ldx     #2
:       txa
        clc
        adc     #9
        tay
        jsr     cpeek
        and     #$7F
        cmp     COMT,x
        bne     @rsx
        dex
        bpl     :-
        rts
@rsx:   jmp     notcom
rsxtype2:
        ldy     #9                      ; with NULL: a COM is refused
        jsr     ccom
        bcc     rsxt
        lda     #<snullcom
        ldx     #>snullcom
        jmp     fail
; A/X an RSX's specification: blank made RSX, else RSX or Invalid RSX type.
rsxtype:
        sta     cfcb
        stx     cfcb + 1
rsxt:   ldy     #9
        jsr     cpeek
        cmp     #' '
        bne     @given
        ldx     #2
:       lda     RSXT,x
        jsr     cpokex
        dex
        bpl     :-
        rts
@given: ldx     #2
:       txa
        clc
        adc     #9
        tay
        jsr     cpeek
        and     #$7F
        cmp     RSXT,x
        bne     @bad
        dex
        bpl     :-
        rts
@bad:   lda     #<sbadrsx
        ldx     #>sbadrsx
        jmp     fail
; Carry set if cfcb's type is COM.
ccom:   ldx     #2
:       txa
        clc
        adc     #9
        tay
        jsr     cpeek
        and     #$7F
        cmp     COMT,x
        bne     @no
        dex
        bpl     :-
        sec
        rts
@no:    clc
        rts

; cfcb's byte Y; cpokex: A to its byte 9 + X.
cpeek:  lda     cfcb
        sta     @l + 1
        lda     cfcb + 1
        sta     @l + 2
@l:     lda     $FFFF,y
        rts
cpokex: pha
        lda     cfcb
        clc
        adc     #9
        sta     @s + 1
        lda     cfcb + 1
        adc     #0
        sta     @s + 2
        pla
@s:     sta     $FFFF,x
        rts

; The FCB at A/X opened (its password where the BDOS looks); No File, or the
; BDOS's said, ends it.
openf:  sta     cfcb
        stx     cfcb + 1
        clc
        adc     #16
        tay
        txa
        adc     #0
        tax
        tya
        jsr     UT_PWSET
        ldy     #12                     ; EX, S1, S2, RC and R0-R2: 0
        lda     #0
:       jsr     cpoke
        iny
        cpy     #16
        bne     :-
        ldy     #32
:       jsr     cpoke
        iny
        cpy     #36
        bne     :-
        lda     cfcb
        ldx     cfcb + 1
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        beq     :+
        rts
:       cpx     #0
        beq     @nofile
        jmp     said
@nofile:
        lda     cfcb
        clc
        adc     #1
        pha
        lda     cfcb + 1
        adc     #0
        tax
        pla
        jsr     CM_FNAME
        lda     #<snofile
        ldx     #>snofile
        jmp     fail
cpoke:  pha
        lda     cfcb
        sta     @s + 1
        lda     cfcb + 1
        sta     @s + 2
        pla
@s:     sta     $FFFF,y
        rts

; The source .COM's FCB (cfcb) and an RSX's, for rdsrc and rdrsx.
setsrc: lda     cfcb
        sta     sfcb
        lda     cfcb + 1
        sta     sfcb + 1
        rts
setrsx: lda     cfcb
        sta     xfcb
        lda     cfcb + 1
        sta     xfcb + 1
        rts
; Record A/X of the source .COM (rdsrc) or of the RSX's file (rdrsx) to rbuf
; (33); a read that fails: the BDOS's said, or Total... no: READ ERROR (ours).
rdsrc:  ldy     sfcb
        sty     rf
        ldy     sfcb + 1
        sty     rf + 1
        bra     rdrec
rdrsx:  ldy     xfcb
        sty     rf
        ldy     xfcb + 1
        sty     rf + 1
rdrec:  sta     rr
        stx     rr + 1
        lda     #<rbuf
        ldx     #>rbuf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     rf
        sta     @p + 1
        sta     @q + 1
        sta     @r + 1
        lda     rf + 1
        sta     @p + 2
        sta     @q + 2
        sta     @r + 2
        lda     rr
        ldy     #33
@p:     sta     $FFFF,y
        lda     rr + 1
        iny
@q:     sta     $FFFF,y
        lda     #0
        iny
@r:     sta     $FFFF,y
        lda     rf
        ldx     rf + 1
        ldy     #F_READRAND
        jsr     BDOS
        cmp     #0
        bne     :+
        rts
:       lda     #<sread
        ldx     #>sread
        jmp     fail

; A/Y = an image's size: A/X its records, ceil((16 + size) / 128).
imgrecs:
        clc
        adc     #OH_LEN + RECORD - 1
        sta     t0
        tya
        adc     #0
        sta     t1
        lda     #0
        rol     a
        sta     t2
        asl     t0                      ; / 128: x 2, the top two bytes
        rol     t1
        rol     t2
        lda     t1
        ldx     t2
        rts
; rbuf's RSX header: rlen = its file's records: the image's, then each
; bitmap's ceil(((size + 7) / 8) / 128).
rsxlen: lda     rbuf + OH_SIZE
        ldy     rbuf + OH_SIZE + 1
        jsr     imgrecs
        sta     rlen
        stx     rlen + 1
        clc                             ; (size + 7) / 8
        lda     rbuf + OH_SIZE
        adc     #7
        sta     t0
        lda     rbuf + OH_SIZE + 1
        adc     #0
        sta     t1
        lda     #0
        rol     a
        ldx     #3
:       lsr     a
        ror     t1
        ror     t0
        dex
        bne     :-
        clc                             ; / 128, up
        lda     t0
        adc     #RECORD - 1
        sta     t0
        lda     t1
        adc     #0
        sta     t1
        asl     t0
        rol     t1                      ; (t1 now the records: at most 64)
        lda     t1
        asl     a                       ; two bitmaps
        clc
        adc     rlen
        sta     rlen
        bcc     :+
        inc     rlen + 1
:       rts

; Entry oi of the old .COM's table, kept: K_OLD (with its file's records) or
; K_SCB.
oldent: lda     oi
        lsr     a
        lsr     a
        lsr     a
        clc
        adc     hdr + OH_RSXREC
        pha
        lda     hdr + OH_RSXREC + 1
        adc     #0
        tax
        pla
        jsr     rdsrc
        lda     oi
        and     #7
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        ldy     #0
:       lda     rbuf,x
        sta     oent,y
        inx
        iny
        cpy     #RT_LEN
        bne     :-
        ldx     #ELEN - 1
:       stz     tent,x
        dex
        bpl     :-
        lda     oent + RT_FLAGS
        and     #RT_SCB
        beq     @rsx
        lda     #K_SCB
        sta     tent + E_KIND
        lda     oent + RT_SCBOFF
        sta     tent + E_SPEC
        lda     oent + RT_SCBVAL
        sta     tent + E_FLAGS
        jmp     append
@rsx:   lda     #K_OLD
        sta     tent + E_KIND
        lda     oent + RT_FLAGS
        sta     tent + E_FLAGS
        ldx     #7
:       lda     oent + RT_NAME,x
        sta     tent + E_NAME,x
        dex
        bpl     :-
        lda     oent + RT_REC
        sta     tent + E_REC
        lda     oent + RT_REC + 1
        sta     tent + E_REC + 1
        lda     oent + RT_REC           ; its header, an RSX's, then its length
        ldx     oent + RT_REC + 1
        jsr     rdsrc
        ldx     #3
:       lda     rbuf,x
        cmp     MAGIC,x
        bne     @bad
        dex
        bpl     :-
        lda     rbuf + OH_FLAGS
        cmp     #OF_RELOC | OF_RSX
        beq     :+
@bad:   lda     #<sbadrsx
        ldx     #>sbadrsx
        jmp     fail
:       jsr     rsxlen
        lda     rlen
        sta     tent + E_LEN
        lda     rlen + 1
        sta     tent + E_LEN + 1
        jmp     append

; Carry set if an earlier specification of this command names cfcb's RSX:
; Duplicate input RSX said (the first taken).
dupin:  ldx     #0
        lda     onull
        bne     :+
        inx
:       stx     sj
@each:  lda     sj
        cmp     si
        bcs     @no
        jsr     specadr
        sta     @c + 1
        stx     @c + 2
        ldy     #1
@cmp:   jsr     cpeek
        and     #$7F
        sta     t0
@c:     lda     $FFFF,y
        and     #$7F
        cmp     t0
        bne     @next
        iny
        cpy     #9
        bne     @cmp
        lda     #<sdupin
        ldx     #>sdupin
        jsr     UT_SAY
        sec
        rts
@next:  inc     sj
        bra     @each
@no:    clc
        rts

; tent placed: over a K_OLD entry of its name (Duplicate RSX in header.
; Replacing old by new), or at the end.
place:  ldx     #0
@each:  cpx     nent
        beq     append
        stx     ej
        jsr     entgetb                 ; to eb
        lda     eb + E_KIND
        cmp     #K_OLD
        bne     @next
        ldy     #7
:       lda     eb + E_NAME,y
        cmp     tent + E_NAME,y
        bne     @next
        dey
        bpl     :-
        lda     #<sduphdr
        ldx     #>sduphdr
        jsr     UT_SAY
        ldx     ej
        stx     ei
        jmp     entput
@next:  ldx     ej
        inx
        bra     @each
; tent, an SCB entry, over one of its offset, or at the end.
scbplace:
        ldx     #0
@each:  cpx     nent
        beq     append
        stx     ej
        jsr     entgetb
        lda     eb + E_KIND
        cmp     #K_SCB
        bne     @next
        lda     eb + E_SPEC
        cmp     tent + E_SPEC
        bne     @next
        ldx     ej
        stx     ei
        jmp     entput
@next:  ldx     ej
        inx
        bra     @each
; tent added at the end (MAXENT + 1 at most: the 16th is refused at write).
append: lda     nent
        cmp     #MAXENT + 1
        bcc     :+
        lda     #<sfull
        ldx     #>sfull
        jmp     fail
:       sta     ei
        inc     nent
        ; falls through
; tent to entry ei; entget: entry ei to tent; entgetb: entry X to eb.
entput: jsr     eadr
        ldy     #ELEN - 1
:       lda     tent,y
ep_s:   sta     $FFFF,y
        dey
        bpl     :-
        rts
entget: jsr     eadr
        ldy     #ELEN - 1
eg_l:   lda     $FFFF,y
        sta     tent,y
        dey
        bpl     eg_l
        rts
entgetb:
        lda     ei                      ; (ei kept)
        pha
        stx     ei
        jsr     eadr
        pla
        sta     ei
        ldy     #ELEN - 1
eb_l:   lda     $FFFF,y
        sta     eb,y
        dey
        bpl     eb_l
        rts
; ents + ei * ELEN into the operands above.
eadr:   lda     #<ents
        sta     t0
        lda     #>ents
        sta     t1
        ldx     ei
@m:     beq     @d
        clc
        lda     t0
        adc     #ELEN
        sta     t0
        bcc     :+
        inc     t1
:       dex
        bra     @m
@d:     lda     t0
        sta     ep_s + 1
        sta     eg_l + 1
        sta     eb_l + 1
        lda     t1
        sta     ep_s + 2
        sta     eg_l + 2
        sta     eb_l + 2
        rts

; NAME.COM's FCB in ofcb (the target), read-only refused.
target: lda     #0
        jsr     specadr
        sta     cfcb
        stx     cfcb + 1
        ldy     #0                      ; its drive and name, type COM
:       jsr     cpeek
        and     #$7F
        sta     ofcb,y
        iny
        cpy     #9
        bne     :-
        ldx     #2
:       lda     COMT,x
        sta     ofcb + 9,x
        dex
        bpl     :-
        ldx     #23
:       stz     ofcb + 12,x
        dex
        bpl     :-
        lda     ofcb
        bne     :+
        ldy     #DRV_GET
        jsr     BDOS
        inc     a
        sta     ofcb
:       lda     #<rbuf                  ; there and read-only? (its entry in rbuf)
        ldx     #>rbuf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<ofcb
        ldx     #>ofcb
        ldy     #F_SFIRST
        jsr     BDOS
        cmp     #$FF
        beq     @none
        asl     a                       ; x32: the entry in the DMA buffer (UT_REC)
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        lda     rbuf + 9,x
        bpl     @none
        lda     #<sro
        ldx     #>sro
        jmp     fail
@none:  rts

; NAME.$$$ made (one there deleted first).
outmake:
        ldx     #35
:       lda     ofcb,x
        sta     tfcb,x
        dex
        bpl     :-
        ldx     #2
        lda     #'$'
:       sta     tfcb + 9,x
        dex
        bpl     :-
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_MAKE
        jsr     BDOS
        cmp     #$FF
        beq     :+
        rts
:       jmp     said

; rbuf written as NAME.$$$'s next record (21).
wrec:   lda     #<rbuf
        ldx     #>rbuf
        ldy     #F_DMAOFF
        jsr     BDOS
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_WRITE
        jsr     BDOS
        cmp     #0
        bne     :+
        rts
:       lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
        lda     #<swrite
        ldx     #>swrite
        jmp     fail

; The image's records: the old .COM's, its header given the count and table's
; record (NULL: hdr's record).
wimage: lda     nent
        sta     hdr + OH_RSXCOUNT
        stz     hdr + 13
        lda     irec
        ldx     irec + 1
        ldy     nent
        bne     :+
        lda     #0
        tax
:       sta     hdr + OH_RSXREC
        stx     hdr + OH_RSXREC + 1
        lda     onull
        beq     @copy
        ldx     #RECORD - 1
:       lda     hdr,x
        sta     rbuf,x
        dex
        bpl     :-
        jmp     wrec
@copy:  stz     cur
        stz     cur + 1
@rec:   lda     cur
        cmp     irec
        bne     :+
        lda     cur + 1
        cmp     irec + 1
        beq     @done
:       lda     cur
        ldx     cur + 1
        jsr     rdsrc
        lda     cur
        ora     cur + 1
        bne     @w
        ldx     #4                      ; record 0: bytes 12-15 the new ones
:       lda     hdr + 11,x
        sta     rbuf + 11,x
        dex
        bne     :-
@w:     jsr     wrec
        inc     cur
        bne     @rec
        inc     cur + 1
        bra     @rec
@done:  rts

; The table: an entry each, to whole records.
wtable: lda     nent
        bne     :+
        rts
:       ldx     #0
:       stz     tbuf,x
        stz     tbuf + 128,x
        inx
        bpl     :-
        stz     ei
@each:  lda     ei
        cmp     nent
        bcs     @out
        jsr     entget
        lda     ei
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        lda     tent + E_KIND
        cmp     #K_SCB
        bne     @rsx
        lda     tent + E_SPEC
        sta     tbuf,x
        lda     tent + E_FLAGS
        sta     tbuf + 1,x
        lda     #RT_SCB
        sta     tbuf + 2,x
        ldy     #0
        lda     #' '
:       sta     tbuf + 3,x
        inx
        iny
        cpy     #8
        bne     :-
        bra     @next
@rsx:   lda     tent + E_OUT
        sta     tbuf,x
        lda     tent + E_OUT + 1
        sta     tbuf + 1,x
        lda     tent + E_FLAGS
        sta     tbuf + 2,x
        ldy     #0
:       lda     tent + E_NAME,y
        sta     tbuf + 3,x
        inx
        iny
        cpy     #8
        bne     :-
@next:  inc     ei
        bra     @each
@out:   ldx     #RECORD - 1
:       lda     tbuf,x
        sta     rbuf,x
        dex
        bpl     :-
        jsr     wrec
        lda     trecs
        cmp     #2
        bcc     @done
        ldx     #RECORD - 1
:       lda     tbuf + 128,x
        sta     rbuf,x
        dex
        bpl     :-
        jmp     wrec
@done:  rts

; Each RSX's records, from the old .COM or its own file.
wrsxs:  stz     ei
@each:  lda     ei
        cmp     nent
        bcs     @done
        jsr     entget
        lda     tent + E_KIND
        cmp     #K_SCB
        beq     @next
        cmp     #K_OLD
        beq     @old
        lda     tent + E_SPEC           ; its file, opened again
        jsr     specadr
        jsr     openf
        jsr     setrsx
        stz     cur
        stz     cur + 1
        bra     @copy
@old:   lda     tent + E_REC
        sta     cur
        lda     tent + E_REC + 1
        sta     cur + 1
@copy:  lda     tent + E_LEN            ; tent's records from cur
        sta     left
        lda     tent + E_LEN + 1
        sta     left + 1
@rec:   lda     left
        ora     left + 1
        beq     @next
        lda     cur
        ldx     cur + 1
        ldy     tent + E_KIND
        bne     @rx
        jsr     rdsrc
        bra     @w
@rx:    jsr     rdrsx
@w:     jsr     wrec
        inc     cur
        bne     :+
        inc     cur + 1
:       lda     left
        bne     :+
        dec     left + 1
:       dec     left
        bra     @rec
@next:  inc     ei
        bra     @each
@done:  rts

; NAME.$$$ closed, NAME.COM deleted, NAME.$$$ renamed to it.
replace:
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_CLOSE
        jsr     BDOS
        cmp     #$FF
        bne     :+
        jmp     said
:       ldx     #11
:       lda     ofcb,x
        sta     dfcb,x
        lda     tfcb,x
        sta     rfcb,x
        lda     ofcb,x
        sta     rfcb + 16,x
        dex
        bpl     :-
        lda     #$FF                    ; errors returned for the delete: one message, ours
        ldy     #F_ERRMODE
        jsr     BDOS
        lda     #<dfcb
        ldx     #>dfcb
        ldy     #F_DELETE
        jsr     BDOS
        pha
        phx
        lda     #$FE
        ldy     #F_ERRMODE
        jsr     BDOS
        plx
        pla
        cmp     #$FF
        bne     @ren
        cpx     #0
        beq     @ren                    ; none there (NULL's)
        lda     #<tfcb
        ldx     #>tfcb
        ldy     #F_DELETE
        jsr     BDOS
        lda     #<sdel
        ldx     #>sdel
        jmp     fail
@ren:   lda     #<rfcb
        ldx     #>rfcb
        ldy     #F_RENAME
        jsr     BDOS
        jmp     UT_EXIT

; ---- the option list ------------------------------------------------------------

options:
        jsr     UT_OPTS
        bne     :+
        rts
:       cmp     #UT_LONE
        beq     :+
        lda     #<sunrec
        ldx     #>sunrec
        jmp     fail
:       ldx     UT_IDX                  ; at the '[' (UT_OPTS's): our own reading
@find:  lda     UT_TAIL,x
        beq     @end
        inx
        cmp     #'['
        bne     @find
@item:  jsr     @blank
        lda     UT_TAIL,x
        beq     @end
        cmp     #']'
        beq     @end
        cmp     #','
        bne     @word
        inx
        bra     @item
@word:  ldy     #0                      ; the word: N, L, S and their prefixes
@w:     lda     UT_TAIL,x
        cmp     #'A'
        bcc     @wend
        cmp     #'Z' + 1
        bcs     @wend
        sta     word,y
        inx
        iny
        cpy     #8
        bcc     @w
@wend:  lda     #0
        sta     word,y
        stx     ox
        lda     #<WNULL
        ldx     #>WNULL
        jsr     prefix
        bcc     :+
        lda     #1
        sta     onull
        jmp     @nomod
:       lda     #<WLOADER
        ldx     #>WLOADER
        jsr     prefix
        bcc     :+
        lda     #1
        sta     oloader
        jmp     @nomod
:       lda     #<WSCB
        ldx     #>WSCB
        jsr     prefix
        bcs     @scb
        lda     #<sunrec
        ldx     #>sunrec
        jmp     fail
@end:   rts
@nomod: ldx     ox
        jsr     @blank
        lda     UT_TAIL,x
        cmp     #'='
        bne     @item
        lda     #<snomod
        ldx     #>snomod
        jmp     fail
@scb:   ldx     ox                      ; SCB=(o,v){,(o,v)}
        jsr     @blank
        lda     UT_TAIL,x
        cmp     #'='
        jne     @noval
        inx
@pair:  jsr     @blank
        lda     UT_TAIL,x
        cmp     #'('
        beq     :+
        cmp     #']'
        jeq     @noval
        cmp     #0
        jeq     @noval
        lda     #<slpar
        ldx     #>slpar
        jmp     fail
:       inx
        jsr     @hex
        bcs     @noval
        cmp     #$64
        bcc     :+
        lda     #<soffset
        ldx     #>soffset
        jmp     fail
:       sta     t2
        jsr     @blank
        lda     UT_TAIL,x
        cmp     #','
        bne     @noval
        inx
        jsr     @hex
        bcs     @noval
        pha
        jsr     @blank
        lda     UT_TAIL,x
        cmp     #')'
        beq     :+
        pla
        lda     #<srpar
        ldx     #>srpar
        jmp     fail
:       inx
        lda     nscb
        cmp     #MAXENT + 1
        bcc     :+
        lda     #<sfull
        ldx     #>sfull
        jmp     fail
:       asl     a
        tay
        lda     t2
        sta     scbo,y
        pla
        sta     scbo + 1,y
        inc     nscb
        jsr     @blank                  ; another pair after a comma?
        lda     UT_TAIL,x
        cmp     #','
        beq     :+
        jmp     @item
:       stx     ox
        inx
        jsr     @blank
        lda     UT_TAIL,x
        cmp     #'('
        jeq     @pair
        ldx     ox
        jmp     @item
@noval: lda     #<snoval
        ldx     #>snoval
        jmp     fail
@blank: lda     UT_TAIL,x
        cmp     #' '
        bne     :+
        inx
        bra     @blank
:       rts
; A byte of hex digits at X (one or two): carry set if none.
@hex:   jsr     @blank
        stz     t0
        ldy     #0
@hd:    lda     UT_TAIL,x
        jsr     hexval
        bcs     @hx
        asl     t0
        asl     t0
        asl     t0
        asl     t0
        ora     t0
        sta     t0
        inx
        iny
        cpy     #3
        bcc     @hd
        sec
        rts
@hx:    cpy     #0
        beq     @hn
        lda     t0
        clc
        rts
@hn:    sec
        rts
; A hex digit's value; carry set if it isn't one.
hexval: cmp     #'0'
        bcc     @no
        cmp     #'9' + 1
        bcc     @d
        cmp     #'A'
        bcc     @no
        cmp     #'F' + 1
        bcs     @no
        sbc     #'A' - 10 - 1
        clc
        rts
@d:     sbc     #'0' - 1
        clc
        rts
@no:    sec
        rts

; Carry set if word (non-empty) is a prefix of the word at A/X.
prefix: sta     @c + 1
        stx     @c + 2
        lda     word
        beq     @no
        ldy     #0
@l:     lda     word,y
        beq     @yes
@c:     cmp     $FFFF,y
        bne     @no
        iny
        bra     @l
@yes:   sec
        rts
@no:    clc
        rts

; ---- the ends --------------------------------------------------------------------

wild:   lda     #<swild
        ldx     #>swild
        bra     fail
notcom: lda     #<snotcom
        ldx     #>snotcom
fail:   jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT
said:   jsr     UT_SAID
        jmp     UT_EXIT

        .include "cmd.inc"
        .include "util.inc"


        .rodata
MAGIC:  .byte   OODOS_MAGIC
COMT:   .byte   "COM"
RSXT:   .byte   "RSX"
WNULL:  .byte   "NULL", 0
WLOADER: .byte  "LOADER", 0
WSCB:   .byte   "SCB", 0
snotcom: .byte  "First submitted file must be a COM file$"
snullcom: .byte "COM file found and NULL option$"
sdupin: .byte   "Duplicate input RSX$"
sduphdr: .byte  "Duplicate RSX in header. Replacing old by new$"
sbadrsx: .byte  "Invalid RSX type$"
swild:  .byte   "Invalid wild card in the FCB name or type field$"
sbig:   .byte   "Total file size exceeds 64K$"
sfull:  .byte   "No more space in the header for RSXs or SCB initialization.$"
sunrec: .byte   "Unrecognized option$"
snomod: .byte   "No modifier for this option$"
slpar:  .byte   "Missing left parenthesis.$"
srpar:  .byte   "Missing right Parenthesis$"
snoval: .byte   "Missing SCB value.$"
soffset: .byte  "Invalid SCB offset$"
sro:    .byte   "Read only$"
sdel:   .byte   "Cannot delete file$"
snofile: .byte  " No File$"
sread:  .byte   "READ ERROR$"
swrite: .byte   "Disk write error$"

        .bss
nspec:  .res    1
nent:   .res    1
nscb:   .res    1
onull:  .res    1
oloader: .res   1
si:     .res    1
sj:     .res    1
ei:     .res    1
oi:     .res    1
ej:     .res    1
ox:     .res    1
t0:     .res    1
t1:     .res    1
t2:     .res    1
sp:     .res    2
cfcb:   .res    2
sfcb:   .res    2
xfcb:   .res    2
rf:     .res    2
rr:     .res    2
irec:   .res    2
trecs:  .res    1
cur:    .res    2
left:   .res    2
rlen:   .res    2
word:   .res    9
scbo:   .res    2 * (MAXENT + 1)
hdr:    .res    RECORD
oent:   .res    RT_LEN
tent:   .res    ELEN
eb:     .res    ELEN
ents:   .res    ELEN * (MAXENT + 1)
specs:  .res    FCBLEN * MAXSPEC
rbuf:   .res    RECORD
tbuf:   .res    256
ofcb:   .res    36
tfcb:   .res    36
dfcb:   .res    36
rfcb:   .res    36

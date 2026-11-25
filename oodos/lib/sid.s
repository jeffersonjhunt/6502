; OOLIB's .sid tunes (include/oolib.inc; hw/oolib.md, "The plan", L3): a
; PSID or RSID file's header read for the program (its name, author, songs),
; and the file played by the sound card's tune player (revision 3: the card
; as a device) through function 82: the whole file loaded into a tune slot
; (SN_TUNE, which the card checks), then played (SN_PLAY). The player, its
; stub and the C64 it stands in for are the card's firmware's; SID_PLAN only
; checks the song and keeps it (the owner's answer 10).
        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "physof.inc"

        .importzp SID_ZP                ; zpsid.s's
sp              = SID_ZP               ; the file's bytes

        .bss
SID_LEN:        .res    2              ; the file's length (the caller's)
SID_VERSION:    .res    1
SID_LOAD:       .res    2
SID_INIT:       .res    2
SID_PLAYA:      .res    2              ; play's address: 0, the tune installs its own interrupt
SID_SONGS:      .res    2
SID_START:      .res    2              ; 1-based
SID_FLAGS:      .res    2
SID_SONG:       .res    2              ; SID_PLAN's song, 1-based (1-256)
SID_STUB:       .res    2              ; where the stub goes
SID_STUBLEN:    .res    1
SID_CTRL:       .res    1              ; bit 0: the header says NTSC (SID_NOTE's clock)
SID_NAME:       .res    2              ; the name's and the author's 32 bytes, in the file
SID_AUTHOR:     .res    2
SID_DATA:       .res    2              ; the code, without its load address
SID_DLEN:       .res    2
speed:          .res    4              ; the header's speed bits, bit n song n + 1 (big-endian there)
spage:          .res    1              ; v2+: the free page the header names, and its length
splen:          .res    1
sid2:           .res    1              ; v3+: the second SID's address byte ($42: $D420)
tend:           .res    2              ; the last address the code fills
song0:          .res    1              ; the song less 1, for init
latch:          .res    2              ; CIA 1's timer A
vbi:            .res    1              ; $80: play at the card's frame, not the CIA
s314:           .res    1              ; $80: the stub sets $0314
hasplay:        .res    1              ; $80: a play routine
ibank:          .res    1              ; $01 for init, and for play
pbank:          .res    1
hvec:           .res    2              ; $0314's value: the handler, or the acknowledge
l_ret:          .res    2              ; the stub's labels, from its first pass
l_idle:         .res    2
l_irq:          .res    2
l_hand:         .res    2
l_nf:           .res    2
l_ack:          .res    2
l_exit:         .res    2
l_nmi:          .res    2
l_frame:        .res    2
t16:            .res    2
ovf:            .res    2              ; ovl's range
ovt:            .res    2
pages:          .res    6              ; the pages to try
npage:          .res    1
ipage:          .res    1
last:           .res    2              ; the stub's last byte
blk:            .res    10             ; function 82's block
file:           .res    2              ; SID_PARSE's file

        .code

; ---- the header ------------------------------------------------------------

; A big-endian word of the file at Y into A (high) and X (low).
be16:   lda     (sp),y
        pha
        iny
        lda     (sp),y
        tax
        pla
        rts

SID_PARSE:
        sta     sp
        stx     sp + 1
        lda     SID_LEN + 1             ; the header's least: $76 bytes
        bne     @size
        lda     SID_LEN
        cmp     #$76
        bcs     @size
@notsid:
        lda     #SE_NOTSID
        sec
        rts
@size:  ldy     #1
        lda     (sp),y
        cmp     #'S'
        bne     @notsid
        iny
        lda     (sp),y
        cmp     #'I'
        bne     @notsid
        iny
        lda     (sp),y
        cmp     #'D'
        bne     @notsid
        lda     (sp)
        cmp     #'R'                    ; an RSID plays too (the card's player)
        beq     @rsid
        cmp     #'P'
        bne     @notsid
@rsid:  lda     sp
        sta     file
        lda     sp + 1
        sta     file + 1
        ldy     #4                      ; the version, 1-4
        jsr     be16
        cmp     #0
        bne     @badv
        txa
        beq     @badv
        cmp     #5
        bcc     @ver
@badv:  lda     #SE_VERSION
        sec
        rts
@ver:   sta     SID_VERSION
        ldy     #6                      ; the data's offset: $76 for version 1, $7C after
        jsr     be16
        cmp     #0
        bne     @badoff
        ldy     #$76
        lda     SID_VERSION
        cmp     #1
        beq     @off
        ldy     #$7C
@off:   sty     t16
        cpx     t16
        bne     @badoff
        stz     t16 + 1                 ; t16: where the data starts
        ldy     #8                      ; the load address: 0, the data's first two bytes
        jsr     be16
        sta     SID_LOAD + 1
        stx     SID_LOAD
        ora     SID_LOAD
        bne     @len
        clc                             ; +2: the load address in the data
        lda     t16
        adc     #2
        sta     t16
@len:   clc                             ; the file must reach 2 bytes past it (sid_file.cpp's rule)
        lda     t16
        adc     #2
        sta     ovf
        lda     #0
        adc     #0
        sta     ovf + 1
        lda     SID_LEN
        cmp     ovf
        lda     SID_LEN + 1
        sbc     ovf + 1
        bcs     @inlen
@badoff:
        lda     #SE_OFFSET
        sec
        rts
@inlen: ldy     #$0A
        jsr     be16
        sta     SID_INIT + 1
        stx     SID_INIT
        ldy     #$0C
        jsr     be16
        sta     SID_PLAYA + 1
        stx     SID_PLAYA
        ldy     #$0E
        jsr     be16
        sta     SID_SONGS + 1
        stx     SID_SONGS
        ldy     #$10
        jsr     be16
        sta     SID_START + 1
        stx     SID_START
        ldy     #$12                    ; speed, 32 bits big-endian: speed its low byte
        ldx     #3
@speed: lda     (sp),y
        sta     speed,x
        iny
        dex
        bpl     @speed
        clc                             ; the name and the author: in the file
        lda     sp
        adc     #$16
        sta     SID_NAME
        lda     sp + 1
        adc     #0
        sta     SID_NAME + 1
        clc
        lda     sp
        adc     #$36
        sta     SID_AUTHOR
        lda     sp + 1
        adc     #0
        sta     SID_AUTHOR + 1
        stz     SID_FLAGS
        stz     SID_FLAGS + 1
        stz     spage
        stz     splen
        stz     sid2
        lda     SID_VERSION
        cmp     #2
        bcc     @data
        ldy     #$76
        jsr     be16
        sta     SID_FLAGS + 1
        stx     SID_FLAGS
        ldy     #$78
        lda     (sp),y
        sta     spage
        iny
        lda     (sp),y
        sta     splen
        lda     SID_VERSION
        cmp     #3
        bcc     @data
        iny
        lda     (sp),y
        sta     sid2
@data:  lda     SID_LOAD                ; the load address from the data's first two bytes
        ora     SID_LOAD + 1
        bne     @code
        ldy     t16
        dey
        dey
        lda     (sp),y
        sta     SID_LOAD
        iny
        lda     (sp),y
        sta     SID_LOAD + 1
@code:  clc                             ; SID_DATA: the file + t16; its length the rest
        lda     sp
        adc     t16
        sta     SID_DATA
        lda     sp + 1
        adc     #0
        sta     SID_DATA + 1
        sec
        lda     SID_LEN
        sbc     t16
        sta     SID_DLEN
        lda     SID_LEN + 1
        sbc     #0
        sta     SID_DLEN + 1
        ora     SID_DLEN
        bne     @end
        lda     #SE_NOCODE
        sec
        rts
@end:   lda     SID_DLEN                ; tend = load + (length - 1), within $FFFF
        sec
        sbc     #1
        tax
        lda     SID_DLEN + 1
        sbc     #0
        tay
        clc
        txa
        adc     SID_LOAD
        sta     tend
        tya
        adc     SID_LOAD + 1
        sta     tend + 1
        bcs     @past
        lda     SID_INIT                ; init 0: the load address
        ora     SID_INIT + 1
        bne     @songs
        lda     SID_LOAD
        sta     SID_INIT
        lda     SID_LOAD + 1
        sta     SID_INIT + 1
@songs: lda     SID_SONGS + 1           ; 1-256
        beq     @lo
        cmp     #1
        bne     @badsongs
        lda     SID_SONGS
        bne     @badsongs
        bra     @start
@lo:    lda     SID_SONGS
        beq     @badsongs
@start: lda     SID_START               ; a start song of 0 or past the last: 1
        ora     SID_START + 1
        beq     @one
        lda     SID_SONGS               ; songs >= start
        cmp     SID_START
        lda     SID_SONGS + 1
        sbc     SID_START + 1
        bcs     @ok
@one:   lda     #1
        sta     SID_START
        stz     SID_START + 1
@ok:    clc
        rts
@past:  lda     #SE_PASTEND
        sec
        rts
@badsongs:
        lda     #SE_SONGS
        sec
        rts

; ---- playing it -------------------------------------------------------------

; A/X the song, 1-based, up to 256 (0: the file's start song): checked
; against the header's count (256 only as the start song: the card names a
; song in a byte), and kept in SID_SONG; a Compute! MUS file is refused
; (SE_MUS). SID_CTRL's bit 0 the header's NTSC. SID_STUB and
; SID_STUBLEN read 0: the card's player needs no stub of the program's.
SID_PLAN:
        sta     SID_SONG
        stx     SID_SONG + 1
        ora     SID_SONG + 1
        bne     @song
        lda     SID_START               ; 0: the file's start song
        sta     SID_SONG
        lda     SID_START + 1
        sta     SID_SONG + 1
@song:  lda     SID_SONGS               ; songs >= song
        cmp     SID_SONG
        lda     SID_SONGS + 1
        sbc     SID_SONG + 1
        bcs     @have
        lda     #SE_SONG
        sec
        rts
@have:  lda     SID_SONG + 1            ; 256: only as the start song (the card's TUNE_PLAY names a song in a
        beq     @mus                    ;   byte, 0 the start song)
        lda     SID_START + 1
        bne     @mus
        lda     #SE_SONG
        sec
        rts
@mus:   lda     SID_FLAGS               ; a MUS file
        lsr     a
        bcc     @clk
        lda     #SE_MUS
        sec
        rts
@clk:   stz     SID_STUB
        stz     SID_STUB + 1
        stz     SID_STUBLEN
        stz     SID_CTRL
        lda     SID_FLAGS               ; NTSC: the clock bits (2-3) 2
        and     #$0C
        cmp     #$08
        bne     @ok
        inc     SID_CTRL
@ok:    clc
        rts

f82:    ldy     #O_SOUND
        jmp     BDOS

; The tune player claimed (SC_TUNE), the file loaded into tune slot 0 (SN_TUNE:
; its physical address, SID_LEN bytes) and SID_SONG played (SN_PLAY, till
; stopped) at SID_FLAGS' clock, as SID_PLAN names notes by it. SE_CLAIM: held, or no card; SE_LOAD: the load or the play refused
; (the card's check of the header too).
SID_PLAY:
        lda     #SN_CLAIM
        sta     blk
        lda     #SC_TUNE
        sta     blk + 1
        lda     #<blk
        ldx     #>blk
        jsr     f82
        cmp     #0
        beq     @load
        lda     #SE_CLAIM
        sec
        rts
@load:  lda     file
        ldx     file + 1
        jsr     PHYSOF
        lda     #SN_TUNE
        sta     blk
        ldx     #2
@p:     lda     PHYS,x
        sta     blk + 1,x
        dex
        bpl     @p
        lda     SID_LEN
        sta     blk + 4
        lda     SID_LEN + 1
        sta     blk + 5
        stz     blk + 6
        stz     blk + 7                 ; slot 0
        lda     #<blk
        ldx     #>blk
        jsr     f82
        cmp     #0
        bne     @fail
        lda     #SN_PLAY
        sta     blk
        stz     blk + 1                 ; slot 0
        lda     SID_SONG                ; the file's start song: 0 (so 256 too, when it is the start)
        cmp     SID_START
        bne     @s
        lda     SID_SONG + 1
        cmp     SID_START + 1
        bne     @s
        lda     #0
@s:     sta     blk + 2
        ldx     #6
@z:     stz     blk + 4 - 1,x           ; no length, no fade
        dex
        bne     @z
        lda     SID_FLAGS               ; the clock: SID_FLAGS' bits 2-3, 01 PAL (+3 bit 0) and 10 NTSC
        lsr     a                       ;   (bit 1); 00 or 11, 0: the file's
        lsr     a
        and     #3
        cmp     #3
        bne     @c
        lda     #0
@c:     sta     blk + 3
        lda     #<blk
        ldx     #>blk
        jsr     f82
        cmp     #0
        bne     @fail
        clc
        rts
@fail:  jsr     SID_STOP
        lda     #SE_LOAD
        sec
        rts

; The tune stopped, its chips silenced, the claim released.
SID_STOP:
        lda     #SN_STOP
        ldx     #0
        jsr     f82
        lda     #SN_RELEASE
        sta     blk
        lda     #SC_TUNE
        sta     blk + 1
        lda     #<blk
        ldx     #>blk
        jmp     f82

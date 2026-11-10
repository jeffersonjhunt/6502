; OOLIB's .sid tunes (include/oolib.inc; hw/oolib.md, "The plan", L3): a
; PSID file's header read, the player stub built on a free page, and the
; tune played on the sound card's 6510 through function 82. The rules are
; the emulator's player's (emulator/components/devices/sid_file.cpp), the
; same bytes for the same file: test_oodos_playsid compares the two.
;
; The stub (sid_file.h has the reasons): the 6510's port and stack as the
; KERNAL leaves them; CIA 1's timer A at the song's rate (a CIA song's, the
; KERNAL's own latch) with its interrupt on, or for a song timed to the
; vertical blank with a play routine, the card's frame interrupt instead
; ($DE03, $DE04) and the timer's off; the raster line shown at $D011 and
; $D012 (a PSID); $0314 pointed at the stub's handler where the tune
; leaves it free; init called with the song less 1 in A, $01 banked for
; it; then the handler calls play on each interrupt (a frame's, for a
; vertical-blank song) and writes the call's number to the mailbox. The
; KERNAL's interrupt entry at $FFFE, its exits at $EA31, $EA7E and $FEBC
; where the tune leaves them free, and an RTI for NMI.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "physof.inc"

        .zeropage
sp:     .res    2                       ; the file's bytes

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
SID_CTRL:       .res    1              ; CTRL's bits for SN_START: NTSC, SID B at $D500, BOTH
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
blk:            .res    8              ; function 82's block
stub:           .res    256            ; the stub as built
vecs:           .res    6              ; $FFFA-$FFFF: NMI, RESET, IRQ

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
        cmp     #'R'
        bne     @psid
        lda     #SE_RSID
        sec
        rts
@psid:  cmp     #'P'
        bne     @notsid
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

; ---- the plan -------------------------------------------------------------

; Carry set if ovf-ovt meets the tune's load-tend.
ovl:    lda     tend                    ; ovf <= tend
        cmp     ovf
        lda     tend + 1
        sbc     ovf + 1
        bcc     @no
        lda     ovt                     ; ovt >= load
        cmp     SID_LOAD
        lda     ovt + 1
        sbc     SID_LOAD + 1
        rts                             ; carry: ovt >= load
@no:    clc
        rts

; Carry set if the tune meets A (high) and X (low), to that + Y.
ovlax:  sta     ovf + 1
        stx     ovf
        sta     ovt + 1
        tya
        clc
        adc     ovf
        sta     ovt
        bcc     ovl
        inc     ovt + 1
        bra     ovl

; $01 for a routine at A (its high byte): ROMs out only where the tune is.
bank:   cmp     #$A0
        bcs     @a
        lda     #$37
        rts
@a:     cmp     #$D0
        bcs     @d
        lda     #$36
        rts
@d:     cmp     #$E0
        bcs     @e
        lda     #$34
        rts
@e:     lda     #$35
        rts

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
@have:  lda     SID_FLAGS               ; a MUS file
        lsr     a
        bcc     @vec
        lda     #SE_MUS
        sec
        rts
@vec:   lda     #$FF                    ; the 6510's vectors
        ldx     #$FA
        ldy     #5
        jsr     ovlax
        bcc     @plan
        lda     #SE_VECTORS
        sec
        rts
@plan:  ldx     SID_SONG                ; the song less 1: 256's is 255
        dex
        stx     song0
        jsr     ciatimed                ; carry set: a CIA song
        lda     #0
        ror     a
        pha                             ; bit 7: CIA-timed
        lda     SID_FLAGS               ; NTSC: the clock bits (2-3) 2
        and     #$0C
        cmp     #$08
        beq     @ntsc
        pla
        bmi     @palcia
        lda     #<(19656 - 1)
        ldx     #>(19656 - 1)
        bra     @latch
@palcia:
        lda     #$25
        ldx     #$40
        bra     @latch
@ntsc:  pla
        bmi     @ntsccia
        lda     #<(17095 - 1)
        ldx     #>(17095 - 1)
        bra     @latch
@ntsccia:
        lda     #$95
        ldx     #$42
@latch: sta     latch
        stx     latch + 1
        stz     hasplay
        lda     SID_PLAYA
        ora     SID_PLAYA + 1
        beq     @noplay
        lda     #$80
        sta     hasplay
@noplay:
        stz     vbi                     ; play at the frame: a play routine, not CIA-timed
        jsr     ciatimed
        bcs     @s314
        bit     hasplay
        bpl     @s314
        lda     #$80
        sta     vbi
@s314:  stz     s314                    ; $0314 set if the tune leaves it
        lda     #$03
        ldx     #$14
        ldy     #1
        jsr     ovlax
        bcs     @banks
        lda     #$80
        sta     s314
@banks: lda     SID_INIT + 1
        jsr     bank
        sta     ibank
        lda     SID_PLAYA + 1
        jsr     bank
        sta     pbank
        ; The pages: the header's free page (v2+, not the port's or the stack's, not $FF, a length
        ; given), then $FF, $FE, $04, $C0, $02.
        ldx     #0
        lda     SID_VERSION
        cmp     #2
        bcc     @fixed
        lda     spage
        cmp     #$02
        bcc     @fixed
        cmp     #$FF
        beq     @fixed
        ldy     splen
        beq     @fixed
        sta     pages,x
        inx
@fixed: ldy     #0
@copy:  lda     fixed,y
        sta     pages,x
        inx
        iny
        cpy     #5
        bne     @copy
        stx     npage
        stz     ipage
@try:   ldx     ipage
        cpx     npage
        bne     @page
        lda     #SE_NOPAGE
        sec
        rts
@page:  lda     pages,x
        sta     SID_STUB + 1
        stz     SID_STUB
        stz     hvec
        stz     hvec + 1
        jsr     emit                    ; the labels
        bit     hasplay                 ; $0314: the handler with play, else the acknowledge
        bpl     @ack
        lda     l_hand
        ldx     l_hand + 1
        bra     @hv
@ack:   lda     l_ack
        ldx     l_ack + 1
@hv:    sta     hvec
        stx     hvec + 1
        jsr     emit
        jsr     clash
        bcc     @found
        inc     ipage
        bra     @try
@found: lda     #0                      ; CTRL: NTSC; SID B at $D500 ($08), or BOTH for one SID
        tax
        lda     SID_FLAGS
        and     #$0C
        cmp     #$08
        bne     @pal
        ldx     #$01
@pal:   lda     sid2
        cmp     #$50
        bne     @both
        txa
        ora     #$08
        tax
        bra     @ctrl
@both:  cmp     #0
        bne     @ctrl
        txa
        ora     #$10
        tax
@ctrl:  stx     SID_CTRL
        lda     l_nmi                   ; the vectors: NMI, RESET (the stub), IRQ
        sta     vecs
        lda     l_nmi + 1
        sta     vecs + 1
        lda     SID_STUB
        sta     vecs + 2
        lda     SID_STUB + 1
        sta     vecs + 3
        lda     l_irq
        sta     vecs + 4
        lda     l_irq + 1
        sta     vecs + 5
        clc
        rts

; Carry set if the song (SID_SONG) is called from CIA 1's timer: the
; speed's bit song - 1 (bit 31 for songs past 32).
ciatimed:
        lda     #31                     ; 256: bit 31
        ldx     SID_SONG + 1
        bne     @bit
        lda     SID_SONG
        dec     a
        cmp     #31
        bcc     @bit
        lda     #31
@bit:   tay                             ; speed >> bit, its low bit
        lda     speed
        sta     t16
        lda     speed + 1
        sta     t16 + 1
        lda     speed + 2
        sta     ovf
        lda     speed + 3
        sta     ovf + 1
        cpy     #0
        beq     @low
@shift: lsr     ovf + 1
        ror     ovf
        ror     t16 + 1
        ror     t16
        dey
        bne     @shift
@low:   lda     t16
        lsr     a
        rts

; Carry set if the stub at SID_STUB clashes: with the tune; across the
; vectors ($FFFA) or the KERNAL's exit at $FEBC-$FEC1; on $D000-$DFFF or
; $EA (the other exits); over $0314 when it sets $0314.
clash:  clc                             ; last = the stub's last byte
        lda     SID_STUB
        adc     SID_STUBLEN
        tax
        lda     SID_STUB + 1
        adc     #0
        cpx     #0
        bne     @l
        dec     a
@l:     dex
        stx     last
        sta     last + 1
        lda     SID_STUB
        sta     ovf
        lda     SID_STUB + 1
        sta     ovf + 1
        lda     last
        sta     ovt
        lda     last + 1
        sta     ovt + 1
        jsr     ovl
        bcs     @yes
        lda     #$FA                    ; at <= $FFF9 and last >= $FFFA
        ldx     #$FF
        ldy     #$F9
        jsr     across
        bcs     @yes
        lda     #$BC                    ; at <= $FEC1 and last >= $FEBC
        ldx     #$FE
        ldy     #$C1
        jsr     across
        bcs     @yes
        lda     SID_STUB + 1
        cmp     #$EA
        beq     @yes
        and     #$F0
        cmp     #$D0
        beq     @yes
        bit     s314
        bpl     @no
        lda     #$14                    ; at <= $0315 and last >= $0314
        ldx     #$03
        ldy     #$15
        jsr     across
        bcs     @yes
@no:    clc
        rts
@yes:   sec
        rts

; Carry set if last >= X/A (high/low) and SID_STUB <= X/Y.
across: sta     t16
        stx     t16 + 1
        lda     last
        cmp     t16
        lda     last + 1
        sbc     t16 + 1
        bcc     @no                     ; last < it
        sty     t16
        lda     t16                     ; X/Y >= SID_STUB
        cmp     SID_STUB
        txa
        sbc     SID_STUB + 1
        rts
@no:    clc
        rts

; ---- the stub ---------------------------------------------------------------

.macro EB v
        lda     #v
        jsr     put
.endmacro
.macro EBV v
        lda     v
        jsr     put
.endmacro
.macro EW opc, adr
        lda     #opc
        jsr     put
        lda     #<(adr)
        jsr     put
        lda     #>(adr)
        jsr     put
.endmacro
.macro EWV opc, var
        lda     #opc
        jsr     put
        lda     var
        jsr     put
        lda     var + 1
        jsr     put
.endmacro
.macro MARK v
        jsr     here
        sta     v
        sty     v + 1
.endmacro

; A to the stub, X its index.
put:    sta     stub,x
        inx
        rts
; The stub's address at X: A low, Y high. Keeps X.
here:   txa
        clc
        adc     SID_STUB
        pha
        lda     SID_STUB + 1
        adc     #0
        tay
        pla
        rts

; The stub at SID_STUB into stub, SID_STUBLEN its length, its labels set
; (the forward ones from the pass before: emit twice).
emit:   ldx     #0
        EB      $78                     ; SEI CLD LDX #$FF TXS
        EB      $D8
        EB      $A2
        EB      $FF
        EB      $9A
        EB      $A9                     ; the port's DDR as the KERNAL leaves it, I/O in
        EB      $2F
        EB      $85
        EB      $00
        EB      $A9
        EB      $37
        EB      $85
        EB      $01
        EB      $A9                     ; the timer's latch
        EBV     latch
        EW      $8D, $DC04
        EB      $A9
        EBV     latch + 1
        EW      $8D, $DC05
        EB      $A9                     ; its interrupt: on for the CIA, off for a frame song
        lda     #$81
        bit     vbi
        bpl     :+
        lda     #$7F
:       jsr     put
        EW      $8D, $DC0D
        EB      $A9                     ; loaded and started, continuous
        EB      $11
        EW      $8D, $DC0E
        bit     vbi
        bpl     @fen
        EB      $A9                     ; the frame's flag cleared
        EB      $01
        EW      $8D, $DE03
@fen:   EB      $A9                     ; FRAMEEN: the raster line shown (a PSID), the frame's interrupt
        lda     #$02
        bit     vbi
        bpl     :+
        lda     #$03
:       jsr     put
        EW      $8D, $DE04
        bit     s314
        bpl     @init
        EB      $A9                     ; $0314
        EBV     hvec
        EW      $8D, $0314
        EB      $A9
        EBV     hvec + 1
        EW      $8D, $0315
@init:  EB      $A9                     ; $01 for init, and init with the song
        EBV     ibank
        EB      $85
        EB      $01
        EB      $A9
        EBV     song0
        EWV     $20, SID_INIT
        MARK    l_ret
        lda     ibank
        cmp     #$34
        bne     @cli
        EB      $A9                     ; I/O back for the interrupt
        EB      $37
        EB      $85
        EB      $01
@cli:   EB      $58
        MARK    l_idle
        EWV     $4C, l_idle
        MARK    l_irq                   ; the KERNAL's entry: A, X, Y pushed
        EB      $48
        EB      $8A
        EB      $48
        EB      $98
        EB      $48
        bit     vbi                     ; a frame to the handler, even if init pointed $0314 elsewhere
        bpl     @vec
        bit     s314
        bpl     @vec
        EB      $A5                     ; LDA $01 PHA, I/O in
        EB      $01
        EB      $48
        EB      $A9
        EB      $37
        EB      $85
        EB      $01
        EW      $AD, $DE03              ; LDA $DE03 LSR PLA STA $01
        EB      $4A
        EB      $68
        EB      $85
        EB      $01
        EB      $B0                     ; BCS past the JMP
        EB      $03
@vec:   bit     s314                    ; through $0314: its own there, or ours with no play
        bmi     @jmp
        bit     hasplay
        bmi     @hand
@jmp:   EW      $6C, $0314
@hand:  MARK    l_hand
        bit     hasplay
        bmi     @play
        jmp     @tail
@play:  EB      $A5                     ; LDA $01 PHA
        EB      $01
        EB      $48
        bit     vbi
        bpl     @call
        EB      $A9                     ; a frame? I/O in, LDA $DE03 AND #1, BEQ not one, clear it
        EB      $37
        EB      $85
        EB      $01
        EW      $AD, $DE03
        EB      $29
        EB      $01
        EB      $F0
        jsr     here                    ; not_frame - (the offset's address + 1)
        eor     #$FF
        clc
        adc     l_nf
        jsr     put
        EW      $8D, $DE03
@call:  EB      $A9                     ; $01 for play, and play
        EBV     pbank
        EB      $85
        EB      $01
        EWV     $20, SID_PLAYA
        EB      $A9                     ; I/O in: the mailbox gets the call's number
        EB      $37
        EB      $85
        EB      $01
        EWV     $EE, l_frame
        EWV     $AD, l_frame
        EW      $8D, $DE00
        bit     vbi
        bpl     @cia
        EB      $68                     ; PLA STA $01, to exit
        EB      $85
        EB      $01
        EWV     $4C, l_exit
        MARK    l_nf
@cia:   EW      $AD, $DC0D              ; the CIA's acknowledged
        EB      $68
        EB      $85
        EB      $01
        MARK    t16                     ; JMP past the acknowledge's 3 bytes to exit
        clc
        lda     t16
        adc     #6
        sta     t16
        bcc     :+
        inc     t16 + 1
:       EWV     $4C, t16
@tail:  MARK    l_ack
        EW      $AD, $DC0D
        MARK    l_exit                  ; PLA TAY PLA TAX PLA RTI
        EB      $68
        EB      $A8
        EB      $68
        EB      $AA
        EB      $68
        EB      $40
        MARK    l_nmi
        EB      $40
        MARK    l_frame
        EB      $00
        stx     SID_STUBLEN
        rts

; ---- playing it -------------------------------------------------------------

f82:    ldy     #O_SOUND
        jmp     BDOS

; SN_LOAD of A/X (CPU), to the card's t16, ovf bytes: carry set if refused.
load:   jsr     PHYSOF
        lda     #SN_LOAD
        sta     blk
        lda     PHYS
        sta     blk + 1
        lda     PHYS + 1
        sta     blk + 2
        lda     PHYS + 2
        sta     blk + 3
        lda     t16
        sta     blk + 4
        lda     t16 + 1
        sta     blk + 5
        lda     ovf
        sta     blk + 6
        lda     ovf + 1
        sta     blk + 7
        lda     #<blk
        ldx     #>blk
        jsr     f82
        cmp     #1                      ; 0: done
        rts

; An exit at Y (exits' index): its card address, its length, its bytes;
; loaded if the tune leaves it free. Carry set if a load was refused.
exit:   lda     exits,y                 ; its address
        sta     t16
        lda     exits + 1,y
        sta     t16 + 1
        lda     exits + 2,y             ; its length
        sta     ovf
        stz     ovf + 1
        phy
        lda     t16 + 1                 ; the tune there?
        ldx     t16
        ldy     ovf
        dey
        jsr     ovlax
        ply
        bcs     @free
        lda     t16                     ; ovlax used ovf: put it back
        pha
        lda     t16 + 1
        pha
        lda     exits + 2,y
        sta     ovf
        stz     ovf + 1
        pla
        sta     t16 + 1
        pla
        sta     t16
        tya                             ; its bytes follow its 3
        clc
        adc     #<(exits + 3)
        pha
        lda     #>(exits + 3)
        adc     #0
        tax
        pla
        jmp     load
@free:  clc
        rts

SID_PLAY:
        lda     #SN_CLAIM
        sta     blk
        lda     #SC_6510
        sta     blk + 1
        lda     #<blk
        ldx     #>blk
        jsr     f82
        cmp     #0
        beq     @stop
        lda     #SE_CLAIM
        sec
        rts
@stop:  lda     #SN_STOP
        ldx     #0
        jsr     f82
        lda     SID_LOAD                ; the tune
        sta     t16
        lda     SID_LOAD + 1
        sta     t16 + 1
        lda     SID_DLEN
        sta     ovf
        lda     SID_DLEN + 1
        sta     ovf + 1
        lda     SID_DATA
        ldx     SID_DATA + 1
        jsr     load
        bcs     @fail
        lda     SID_STUB                ; the stub
        sta     t16
        lda     SID_STUB + 1
        sta     t16 + 1
        lda     SID_STUBLEN
        sta     ovf
        stz     ovf + 1
        lda     #<stub
        ldx     #>stub
        jsr     load
        bcs     @fail
        ldy     #0                      ; the KERNAL's exits
        jsr     exit
        bcs     @fail
        ldy     #EXIT2 - exits
        jsr     exit
        bcs     @fail
        ldy     #EXIT3 - exits
        jsr     exit
        bcs     @fail
        lda     #$FA                    ; the vectors
        sta     t16
        lda     #$FF
        sta     t16 + 1
        lda     #6
        sta     ovf
        stz     ovf + 1
        lda     #<vecs
        ldx     #>vecs
        jsr     load
        bcs     @fail
        lda     #SN_START               ; and run, from the stub
        sta     blk
        lda     SID_STUB
        sta     blk + 1
        lda     SID_STUB + 1
        sta     blk + 2
        lda     SID_CTRL
        sta     blk + 3
        lda     #<blk
        ldx     #>blk
        jsr     f82
        clc
        rts
@fail:  jsr     SID_STOP
        lda     #SE_LOAD
        sec
        rts

SID_STOP:
        lda     #SN_STOP
        ldx     #0
        jsr     f82
        lda     #SN_RELEASE
        sta     blk
        lda     #SC_6510
        sta     blk + 1
        lda     #<blk
        ldx     #>blk
        jmp     f82

        .rodata
fixed:  .byte   $FF, $FE, $04, $C0, $02
; The KERNAL's interrupt exits: an address, a length, the bytes.
exits:  .word   $EA31
        .byte   3, $4C, $7E, $EA                         ; JMP $EA7E
EXIT2:  .word   $EA7E
        .byte   9, $AD, $0D, $DC, $68, $A8, $68, $AA, $68, $40 ; LDA $DC0D, PLA TAY PLA TAX PLA RTI
EXIT3:  .word   $FEBC
        .byte   6, $68, $A8, $68, $AA, $68, $40        ; PLA TAY PLA TAX PLA RTI

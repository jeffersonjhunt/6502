; W1HTEST: WIN1H across OODOS's ways in (B7; hw/design.md, "The video
; aperture moved out of RAM", WINnH under OODOS), for test_oodos_sys.cpp,
; test_oodos_input.cpp, test_oodos_clock.cpp and test_oodos_utils_rom.cpp,
; a case by the command tail's first letter, a line a result. "aa xx" is A
; and X as the BDOS returned them.
;
; It writes the north bridge's WIN1H itself, which a program doesn't (it
; draws through OOLIB: AGENTS.md); a test's program only. W1HON puts WIN1H
; on $0E with W1 left on block 1, so W1 shows the video card's aperture
; slot 1 ($E04000, the program's first card bank), and W1HOFF puts it back
; to 0 (RAM block 1). The test puts one byte at the card's slot 1 window
; and another at RAM block 1's $4000, so $4000 read through W1 says which
; W1 showed.
;
;   S   the BDOS stub: W1HON, B0 ($4000), "HELLO" by C_WRITESTR (the stub
;       reads A and X off the program's stack through W1), then WIN1H and
;       $4000 again: W1H hh B0 bb B1 bb (W1HOFF first)
;   R   the refusals: W1.DAT made on M:, four records; then with W1HON
;       (keys H...) and again with W1HOFF (Z...): xRD aa xx (record 0 read
;       to $4000: the BDOS's copy out of its buffer, PMAP), xWS aa xx
;       (C_WRITESTR of $4000, PMAP; with WIN1H 0 the string at RAM block
;       1's $4000 shown first), xPH aa xx and its 4 bytes (function 73 of
;       $4000), xMV aa xx (function 50's MOVE of 4 bytes to $4010: the
;       BIOS's PHYSOF), xMR aa xx and xMRCHK y/n (two records at once to
;       $3F80, the second in W1: MRANGE; Y if $3F80-$3FFF kept their $E7),
;       xEV aa xx (an event area at $4100, FT_OPEN, with a layout-1 area in
;       W0 open before it) and xEVKEPT y/n (Y if the W0 area still counts
;       after: a refusal leaves it open; then any area closed)
;   I   a layout-2 event area in W0, W1HON, 200 ms by its counter, then
;       W1H hh B1 bb (the dispatcher's way out, L2's tick)
;   H   the hooks: a tick hook (every ms) and a frame hook, set with W1HON;
;       each keeps the $4000 and the WIN1H it saw and counts its calls;
;       then W1HOFF, 200 ms in the BDOS (FT_DELAY), $4000 (B1), the hooks
;       off: TB bb TH hh FB bb FH hh B1 bb TN nn FN nn
;   Q   W1HON, a spin of about 100 ms at QSPIN (the test gives an IRQ that
;       no card asserts there: the dispatcher's no-card way out), then
;       W1H hh B1 bb
;   W   W1HON, MAP's AP8 changed, and the warm boot
;   B   W1HON, then a spin for ever at BSPIN (Break ends it)
;   A   W1HON, MAP's AP8 changed, then function 75's Apple II
;       mode (A = 1): A2 aa xx if it comes back (no IIe ROM), W1HOFF, the
;       warm boot
;
; Function 83's DM_BANK8 (B7, interface 12), each "K8 aa xx" for its A and X
; and "K8B hhbb" for its block's +1 and +2:
;
;   K   DM_BANK8, then OODOS's own work with bank 8 the RAM card's: 40
;       lines (the console scrolls), a DM_VCOPY of 1,000 bytes card to card
;       ($060000 to $0A0000: KV aa xx), W1.DAT made on M: with a record and
;       read back (KR aa xx), 100 ms of frames and ticks (FT_DELAY), KDONE,
;       the warm boot
;   L   DM_BANK8, then a spin for ever at BSPIN (Break ends it)
;   E   DM_BANK8, W1 on block 32 (bank 8's RAM now), an event area at $4100
;       (FT_OPEN, 16 bytes: E8 aa xx), 50 ms (FT_DELAY), W1 back on block 1,
;       any area closed, the warm boot
;   C   no DM_BANK8: DM_COPY of 4 bytes ("MOVE") to $84000, aperture slot
;       1's window (C1 aa xx), then to $80000, slot 0's (C0 aa xx)

        .setcpu "65C02"
        .include "oodos.inc"
        .include "backplane.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

DRV_M   = 12
RECS    = 4

        .zeropage
t0:     .res    2
spin:   .res    2
tb:     .res    1                       ; the tick hook's $4000, WIN1H and count
th:     .res    1
tn:     .res    1
fb:     .res    1                       ; the frame hook's
fh:     .res    1
fn:     .res    1

        .code
start:  lda     BP_DMA + 1              ; the tail's first letter, after its space
        cmp     #' '
        bne     @l
        lda     BP_DMA + 2
@l:     cmp     #'S'
        bne     :+
        jmp     case_s
:       cmp     #'R'
        bne     :+
        jmp     case_r
:       cmp     #'I'
        bne     :+
        jmp     case_i
:       cmp     #'H'
        bne     :+
        jmp     case_h
:       cmp     #'Q'
        bne     :+
        jmp     case_q
:       cmp     #'W'
        bne     :+
        jmp     case_w
:       cmp     #'B'
        bne     :+
        jmp     case_b
:       cmp     #'A'
        bne     :+
        jmp     case_a
:       cmp     #'K'
        bne     :+
        jmp     case_k
:       cmp     #'L'
        bne     :+
        jmp     case_l
:       cmp     #'E'
        bne     :+
        jmp     case_e
:       cmp     #'C'
        bne     :+
        jmp     case_c
:       jmp     WBOOT

; WIN1H $0E (the aperture) or 0. Uses A.
W1HON:  lda     #VIDEO_APER_H
        sta     NB_WIN1H
        rts
W1HOFF: stz     NB_WIN1H
        rts

; "KEY hh": the byte A, then CR LF (X kept).
.macro BYTEA key
        pha
        SAY     key
        pla
        jsr     HEX2
        jsr     CRLF
.endmacro
; WIN1H as it reads.
w1h:    lda     NB_WIN1H
        rts

; ---- S ---------------------------------------------------------------------

case_s: jsr     W1HON
        lda     $4000
        sta     t0
        SAY     "HELLO"
        jsr     w1h
        sta     t0 + 1
        lda     $4000
        sta     spin
        jsr     W1HOFF
        jsr     CRLF
        lda     t0 + 1
        BYTEA   "W1H "
        lda     t0
        BYTEA   "B0 "
        lda     spin
        BYTEA   "B1 "
        jmp     WBOOT

; ---- R ---------------------------------------------------------------------

case_r: lda     #$FF                    ; errors come back
        ldy     #F_ERRMODE
        jsr     BDOS
        lda     #DRV_M
        sta     TDRV
        NAME    fcb, "W1      DAT"
        CALLF   F_DELETE, fcb
        NAME    fcb, "W1      DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_DMAOFF, BUF
        stz     RN + 1
        lda     #0
@rec:   sta     RN
        jsr     FILL
        CALLF   F_WRITE, fcb
        lda     RN
        inc     a
        cmp     #RECS
        bne     @rec
        CALLF   F_CLOSE, fcb
        NAME    fcb, "W1      DAT"
        CALLF   F_OPEN, fcb
        jsr     W1HON
        lda     #'H'
        jsr     tries
        jsr     W1HOFF
        lda     #'Z'
        jsr     tries
        jmp     WBOOT

; R0-R2 of `fcb` = r.
.macro RREC fcb, r
        lda     #<(r)
        sta     fcb+33
        lda     #>(r)
        sta     fcb+34
        stz     fcb+35
.endmacro

; Each refusal's case, its keys starting with A.
.macro KEY s
        lda     key
        jsr     PUTC
        SAY     s
.endmacro
tries:  sta     key
        KEY     "RD "                   ; a record to $4000: PHYSOF
        CALLF   F_DMAOFF, $4000
        RREC    fcb, 0
        CALLF   F_READRAND, fcb
        jsr     RES
        KEY     "WS "                   ; the string at $4000: PMAP
        CALLF   C_WRITESTR, $4000
        jsr     RES
        KEY     "PH "                   ; function 73 of $4000
        lda     #<$4000
        sta     pblk
        lda     #>$4000
        sta     pblk + 1
        stz     pblk + 2
        stz     pblk + 3
        CALLF   O_PHYSADDR, pblk
        jsr     RES
        lda     #<pblk
        sta     PTR
        lda     #>pblk
        sta     PTR + 1
        ldx     #4
        jsr     HEXN
        jsr     CRLF
        KEY     "MV "                   ; MOVE to $4010: PHYSOF
        lda     #BIOS::SELMEM
        sta     pb50
        lda     #1
        sta     pb50 + 1
        jsr     bios50
        lda     #BIOS::MOVE
        sta     pb50
        lda     #4
        sta     pb50 + 1
        stz     pb50 + 2
        lda     #<mvsrc
        sta     pb50 + 4
        lda     #>mvsrc
        sta     pb50 + 5
        lda     #<$4010
        sta     pb50 + 6
        lda     #>$4010
        sta     pb50 + 7
        jsr     bios50
        jsr     RES
        ldx     #127                    ; two records to $3F80: MRANGE
        lda     #$E7
:       sta     $3F80,x
        dex
        bpl     :-
        KEY     "MR "
        CALLF   F_MULTISEC, 2
        CALLF   F_DMAOFF, $3F80
        RREC    fcb, 0
        CALLF   F_READRAND, fcb
        jsr     RES
        CALLF   F_MULTISEC, 1
        KEY     "MRCHK "
        ldx     #127
:       lda     $3F80,x
        cmp     #$E7
        bne     :+
        dex
        bpl     :-
        clc
        bra     :++
:       sec
:       jsr     YESNO
        CALLF   F_DMAOFF, BUF
        CALLF   O_FRAME, ev1blk         ; a layout-1 area in W0, then one at $4100
        KEY     "EV "
        CALLF   O_FRAME, evblk
        jsr     RES
        lda     area + EV_MS
        sta     t0
        CALLF   O_FRAME, d20blk
        KEY     "EVKEPT "
        lda     area + EV_MS            ; Y if the W0 area counted the 20 ms
        cmp     t0
        clc
        bne     :+
        sec
:       jsr     YESNO
        lda     #FT_CLOSE
        ldx     #0
        ldy     #O_FRAME
        jmp     BDOS

; Function 50 with pb50.
bios50: lda     #<pb50
        ldx     #>pb50
        ldy     #S_BIOS
        jmp     BDOS

; ---- I ---------------------------------------------------------------------

case_i: CALLF   O_FRAME, oblk2
        jsr     W1HON
        lda     area + EV_MS
        sta     t0
        lda     area + EV_MS + 1
        sta     t0 + 1
@wait:  sec                             ; 200 ms by the area's counter (the low byte first: a
        lda     area + EV_MS            ; carry between the two reads makes it a round late)
        sbc     t0
        tax
        lda     area + EV_MS + 1
        sbc     t0 + 1
        bne     @done
        cpx     #200
        bcc     @wait
@done:  jsr     w1h
        sta     t0
        lda     $4000
        sta     t0 + 1
        jsr     W1HOFF
        lda     #FT_CLOSE
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        lda     t0
        BYTEA   "W1H "
        lda     t0 + 1
        BYTEA   "B1 "
        jmp     WBOOT

; ---- H ---------------------------------------------------------------------

case_h: stz     tn
        stz     fn
        lda     #$EE
        sta     tb
        sta     th
        sta     fb
        sta     fh
        jsr     W1HON
        CALLF   O_FRAME, thblk
        CALLF   O_FRAME, fhblk
        jsr     W1HOFF
        CALLF   O_FRAME, dblk
        lda     $4000
        sta     t0
        lda     #FT_TICKHOOK
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        lda     #FT_FRAMEHOOK
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        lda     tb
        BYTEA   "TB "
        lda     th
        BYTEA   "TH "
        lda     fb
        BYTEA   "FB "
        lda     fh
        BYTEA   "FH "
        lda     t0
        BYTEA   "B1 "
        lda     tn
        BYTEA   "TN "
        lda     fn
        BYTEA   "FN "
        jmp     WBOOT

; The hooks, in W0: what W1 shows them, and WIN1H.
thook:  lda     $4000
        sta     tb
        lda     NB_WIN1H
        sta     th
        inc     tn
        rts
fhook:  lda     $4000
        sta     fb
        lda     NB_WIN1H
        sta     fh
        inc     fn
        rts

; ---- Q ---------------------------------------------------------------------

case_q: jsr     W1HON
        lda     #1                      ; 256 x 256 x 13 cycles: about 0.12 s
        sta     t0
        ldx     #0
        ldy     #0
QSPIN:  nop
        nop
        nop
        nop
        dey
        bne     QSPIN
        dex
        bne     QSPIN
        dec     t0
        bne     QSPIN
QSPEND: jsr     w1h
        sta     t0
        lda     $4000
        sta     t0 + 1
        jsr     W1HOFF
        lda     t0
        BYTEA   "W1H "
        lda     t0 + 1
        BYTEA   "B1 "
        jmp     WBOOT

; ---- W, B, A ---------------------------------------------------------------

case_w: jsr     W1HON
        jsr     AP8FLIP
        jmp     WBOOT

; MAP's AP8 the other way. Uses A.
AP8FLIP:
        lda     NB_MAP
        eor     #NB_MAP_AP8
        sta     NB_MAP
        rts

case_b: jsr     W1HON
BSPIN:  bra     BSPIN

case_a: jsr     W1HON
        jsr     AP8FLIP
        lda     #1                      ; Apple II mode: no return if it starts
        ldy     #O_ROM
        jsr     BDOS
        pha
        phx
        jsr     W1HOFF
        plx
        pla
        SAY     "A2 "
        jsr     RES
        jmp     WBOOT

; ---- K, L, E, C (DM_BANK8) -------------------------------------------------

; Function 83's DM_BANK8: K8 aa xx, K8B hhbb (its block's +1 and +2).
bank8:  lda     #DM_BANK8
        sta     k8blk
        lda     #$EE
        sta     k8blk + 1
        sta     k8blk + 2
        SAY     "K8 "
        CALLF   O_DMA, k8blk
        jsr     RES
        SAY     "K8B "
        lda     k8blk + 1
        jsr     HEX2
        lda     k8blk + 2
        jsr     HEX2
        jmp     CRLF

case_k: jsr     bank8
        lda     #40                     ; 40 lines: the console scrolls
        sta     t0
kline:  SAY     "LINE"
        jsr     CRLF
        dec     t0
        bne     kline
        SAY     "KV "
        CALLF   O_DMA, vcblk
        jsr     RES
        lda     #DRV_M                  ; a file written and read back
        sta     TDRV
        NAME    fcb, "W1      DAT"
        CALLF   F_DELETE, fcb
        NAME    fcb, "W1      DAT"
        CALLF   F_MAKE, fcb
        CALLF   F_DMAOFF, BUF
        stz     RN
        stz     RN + 1
        jsr     FILL
        CALLF   F_WRITE, fcb
        CALLF   F_CLOSE, fcb
        NAME    fcb, "W1      DAT"
        CALLF   F_OPEN, fcb
        SAY     "KR "
        CALLF   F_READ, fcb
        jsr     RES
        CALLF   O_FRAME, d100blk
        SAY     "KDONE"
        jsr     CRLF
        jmp     WBOOT

case_l: jsr     bank8
        jmp     BSPIN

case_e: jsr     bank8
        lda     #32                     ; W1 on block 32: bank 8's RAM after DM_BANK8
        sta     NB_WIN1
        CALLF   O_FRAME, evblk
        pha
        phx
        CALLF   O_FRAME, d50blk
        lda     #1
        sta     NB_WIN1
        SAY     "E8 "
        plx
        pla
        jsr     RES
        lda     #FT_CLOSE
        ldx     #0
        ldy     #O_FRAME
        jsr     BDOS
        jmp     WBOOT

case_c: SAY     "C1 "
        CALLF   O_DMA, c1blk
        jsr     RES
        SAY     "C0 "
        CALLF   O_DMA, c0blk
        jsr     RES
        jmp     WBOOT

        .export QSPIN, QSPEND, BSPIN

        .rodata
oblk2:  .byte   FT_OPEN
        .word   area
        .byte   0                       ; 256 bytes: layout 2
thblk:  .byte   FT_TICKHOOK
        .word   thook
        .word   1
fhblk:  .byte   FT_FRAMEHOOK
        .word   fhook
dblk:   .byte   FT_DELAY
        .word   200
evblk:  .byte   FT_OPEN
        .word   $4100
        .byte   16
ev1blk: .byte   FT_OPEN
        .word   area
        .byte   16
d20blk: .byte   FT_DELAY
        .word   20
d50blk: .byte   FT_DELAY
        .word   50
d100blk:
        .byte   FT_DELAY
        .word   100
vcblk:  .byte   DM_VCOPY
        .faraddr $060000, $0A0000
        .word   1000
c1blk:  .byte   DM_COPY
        .faraddr mvsrc, $84000          ; mvsrc in W0's block 0: its physical address is its own
        .word   4
c0blk:  .byte   DM_COPY
        .faraddr mvsrc, $80000
        .word   4
mvsrc:  .byte   "MOVE"

        .include "print.inc"
        .include "fs.inc"

        .bss
key:    .res    1
pblk:   .res    4
pb50:   .res    8                       ; function 50's parameter block
k8blk:  .res    3                       ; DM_BANK8's
fcb:    .res    36
area:   .res    256                     ; (in W0, the program being small)

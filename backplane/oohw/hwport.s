; HW_PAIAW and HW_PAIAC (oohw.inc, HW_PORTAIA in hw/diag.md): address in
; address through a card's data port, a register that moves its own address
; on at each access (the sound card's DOCDATA with AUTOINC, its RAMDATA; the
; video card's PDATA). The program sets the port's address before each pass;
; the bytes are numbered from there, and byte n is n's low byte XOR its
; middle XOR its high XOR HW_KEY.
;
; HW_PORT is the data register's address, HW_PLEN the length in pages of
; 256 (1-65,535). A failure: carry set, HW_BAD the byte's number (3 bytes),
; HW_GOT and HW_WANT.

        .setcpu "65C02"
        .include "oohw.inc"

        .code
HW_PAIAW:
        jsr     setup
@page:  lda     n + 1
        eor     n + 2
        eor     HW_KEY
        sta     base
        ldy     #0
@byte:  tya
        eor     base
        sta     (HW_PTR)
        iny
        bne     @byte
        jsr     next
        bne     @page
        clc
        rts

HW_PAIAC:
        jsr     setup
@page:  lda     n + 1
        eor     n + 2
        eor     HW_KEY
        sta     base
        ldy     #0
@byte:  lda     (HW_PTR)
        sta     HW_GOT
        tya
        eor     base
        cmp     HW_GOT
        bne     @bad
        iny
        bne     @byte
        jsr     next
        bne     @page
        clc
        rts
@bad:   sta     HW_WANT
        sty     HW_BAD
        lda     n + 1
        sta     HW_BAD + 1
        lda     n + 2
        sta     HW_BAD + 2
        sec
        rts

setup:  lda     HW_PORT
        sta     HW_PTR
        lda     HW_PORT + 1
        sta     HW_PTR + 1
        stz     n + 1
        stz     n + 2
        lda     HW_PLEN
        sta     left
        lda     HW_PLEN + 1
        sta     left + 1
        rts
; The next page; Z set when none is left.
next:   inc     n + 1
        bne     @c
        inc     n + 2
@c:     lda     left
        bne     @lo
        dec     left + 1
@lo:    dec     left
        lda     left
        ora     left + 1
        rts

        .bss
HW_PORT: .res   2
HW_PLEN: .res   2
n:      .res    3                       ; n + 1, n + 2: the page's number
base:   .res    1
left:   .res    2

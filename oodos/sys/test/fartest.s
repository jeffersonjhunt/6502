; FARTEST: FARCALL from common memory (hw/oodos.md, "Far calls"), found by
; function 72, with the spike's checks: A, X, Y and P reach the target as the
; caller set them (D set too, which FARCALL's own adds mustn't see) and come
; back as the target left them, with W3 the caller's again; far calls nest.
; test_oodos_sys.cpp maps blocks 20 and 21 by hand (common memory in their
; top 2 KB) with targets at $C800: 20's records what it got at $0290-$0294
; (A, X, Y, P, WIN3), far calls 21 with $44 $55 $66, records what came back
; at $0295-$0299, and returns $77 $88 $99, P $C3; 21 records at $029A-$029E
; and returns $AA $BB $CC, P $41. This records what came back at $02A0-$02A4.
;
;   OODOS <10 bytes>         function 72's block
;   FARCALL hhhh             its A/X
;   FAR <21 bytes>           $0290-$029E, then $02A0-$02A4

        .setcpu "65C02"
        .include "oodos.inc"
        .include "system.inc"

        OODOS_HEADER
        .include "printzp.inc"

        .code
start:  lda     #<info
        ldx     #>info
        ldy     #O_VERSION
        jsr     BDOS
        sta     farv
        stx     farv+1
        pha
        phx
        lda     #<oomsg
        ldx     #>oomsg
        jsr     PUTS
        lda     #<info
        sta     PTR
        lda     #>info
        sta     PTR+1
        ldx     #10
        jsr     HEXN
        jsr     CRLF
        lda     #<farmsg
        ldx     #>farmsg
        jsr     PUTS
        plx
        pla
        jsr     HEX4
        jsr     CRLF

        lda     #$CD                    ; N V D I C
        pha
        lda     #$11
        ldx     #$22
        ldy     #$33
        plp
        jsr     outer
        php
        sta     $02A0
        stx     $02A1
        sty     $02A2
        pla
        sta     $02A3
        lda     NB_WIN3
        sta     $02A4
        cld
        lda     #<resmsg
        ldx     #>resmsg
        jsr     PUTS
        lda     #<$0290
        sta     PTR
        lda     #>$0290
        sta     PTR+1
        ldx     #15
        jsr     HEXN
        lda     #<$02A0
        sta     PTR
        ldx     #5
        jsr     HEXN
        jsr     CRLF
        jmp     WBOOT

farjmp: jmp     (farv)

        .rodata
; The far call, its inline bytes data, so not in CODE.
outer:  jsr     farjmp
        .byte   20
        .word   CODE_WINDOW
        rts
oomsg:  .byte   "OODOS $"
farmsg: .byte   "FARCALL $"
resmsg: .byte   "FAR $"

        .bss
info:   .res    10
farv:   .res    2                       ; not zero page: JMP (abs) is absolute

        .include "print.inc"

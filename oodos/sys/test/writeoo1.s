; WRITEOO1: the OO-1's side of the cpmtools interoperability test
; (hw/oodos.md, "Testing"): 0:OO1.BIN on A: through the BDOS, 260 records
; (two entries), byte i of the file ((i >> 7) x 31 + i x 7 + 5) & $FF, as
; test_oodos_card.cpp and oodos/test/interop.sh expect it: made at 2026-10-25
; 13:45 and updated at 2026-10-26 15:30, the clock set for each by function
; 104. The update comes from writing a record of each entry again, at the
; later time: record 0 by function 21, record 259 by 34, then the close.
; On the test card, for test_oodos_card's Session to run as WRITEOO1.
;
;   step aa xx            a step that failed: MAKE, WRITE, CLOSE, OPEN,
;                         REWRITE, RANDOM, CLOSE2
;   OO1.BIN WRITTEN       done

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

RECORDS = 260

        .zeropage
BASE:   .res    1                       ; record RN's first byte

        .code
start:  lda     #$FF                    ; errors come back, said here
        ldy     #F_ERRMODE
        jsr     BDOS
        CALLF   F_DMAOFF, BUF
        CALLF   T_SET, made
        stz     TDRV                    ; A:
        lda     #0
        ldy     #F_USERNUM
        jsr     BDOS
        NAME    fcb, "OO1     BIN"
        CALLF   F_MAKE, fcb
        cmp     #$FF
        bne     wmade
        jsr     keep
        SAY     "MAKE "
        jmp     failed
wmade:  stz     RN
        stz     RN+1
wwrite: jsr     record
        CALLF   F_WRITE, fcb
        cmp     #0
        beq     wnext
        jsr     keep
        SAY     "WRITE "
        jmp     failed
wnext:  inc     RN
        bne     wcount
        inc     RN+1
wcount: lda     RN
        cmp     #<RECORDS
        bne     wwrite
        lda     RN+1
        cmp     #>RECORDS
        bne     wwrite
        CALLF   F_CLOSE, fcb
        cmp     #$FF
        bne     wclosed
        jsr     keep
        SAY     "CLOSE "
        jmp     failed
wclosed:
        CALLF   T_SET, updated          ; a day later: each entry written again
        NAME    fcb, "OO1     BIN"
        CALLF   F_OPEN, fcb
        cmp     #$FF
        bne     wopened
        jsr     keep
        SAY     "OPEN "
        jmp     failed
wopened:
        stz     RN
        stz     RN+1
        jsr     record
        CALLF   F_WRITE, fcb
        cmp     #0
        beq     wrewrote
        jsr     keep
        SAY     "REWRITE "
        jmp     failed
wrewrote:
        lda     #<(RECORDS - 1)
        sta     RN
        sta     fcb+33
        lda     #>(RECORDS - 1)
        sta     RN+1
        sta     fcb+34
        stz     fcb+35
        jsr     record
        CALLF   F_WRITERAND, fcb
        cmp     #0
        beq     wrandom
        jsr     keep
        SAY     "RANDOM "
        jmp     failed
wrandom:
        CALLF   F_CLOSE, fcb
        cmp     #$FF
        bne     wdone
        jsr     keep
        SAY     "CLOSE2 "
        jmp     failed
wdone:  SAY     "OO1.BIN WRITTEN"
        jmp     CRLF

failed: lda     RA                      ; the step's A and X, kept by keep
        ldx     RX
        jmp     RES

keep:   sta     RA
        stx     RX
        rts

; BUF = record RN of OO1.BIN: byte j of record r is (r x 159 + 5 + j x 7) &
; $FF (r x 927 = r x 159, modulo 256).
record: lda     #5
        ldx     RN+1                    ; RN x 159: its low byte only
        ldy     RN
@times: cpy     #0
        bne     @add
        cpx     #0
        beq     @base
        dex
@add:   dey
        clc
        adc     #159
        bra     @times
@base:  ldy     #0
@byte:  sta     BUF,y
        clc
        adc     #7
        iny
        bpl     @byte
        rts

        .include "print.inc"
        .include "fs.inc"

        .rodata
made:   .word   17830                   ; 2026-10-25, days from 1 January 1978 (day 1)
        .byte   $13, $45
updated:
        .word   17831                   ; 2026-10-26
        .byte   $15, $30

        .bss
fcb:    .res    36

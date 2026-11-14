; HW_TEST, HW_LINE, HW_FAULT and HW_SUMMARY (oohw.inc): a diagnostic's
; report, one line a test (hw/diag.md, "DIAG.COM"):
;
;   "  NB1  ID and STATUS agree, revision 1 ......................... PASS"
;
; the test's name in a field of 5 after two blanks, what the program
; writes, a blank and dots to column 64, a blank and the result at 65. The
; column is HW_COL, which the program's HW_PUTC adds 1 to for each
; character it writes (DIAG's does); HW_TEST sets it.

        .setcpu "65C02"
        .include "oohw.inc"

        OOHW_CODE
; A/X the test's name, ended by 0.
HW_TEST:
        pha
        lda     #' '
        jsr     HW_PUTC
        lda     #' '
        jsr     HW_PUTC
        pla
        jsr     HW_PUTS                 ; Y its length
@pad:   lda     #' '
        jsr     HW_PUTC
        iny
        cpy     #5
        bcc     @pad
        lda     #7
        sta     HW_COL
        rts

; A the result (HL_*), counted in HW_TALLY.
HW_LINE:
        pha
        tax
        inc     HW_TALLY,x
        lda     #' '
        jsr     HW_PUTC
@dot:   lda     HW_COL
        cmp     #64
        bcs     @end
        lda     #'.'
        jsr     HW_PUTC
        bra     @dot
@end:   lda     #' '
        jsr     HW_PUTC
        pla
        asl     a
        tax
        lda     words,x
        pha
        lda     words + 1,x
        tax
        pla
        jmp     HW_PUTS

; "read $gg, expected $ww: bit n" (or "bits n, m"), from HW_GOT and HW_WANT.
HW_FAULT:
        lda     #<sread
        ldx     #>sread
        jsr     HW_PUTS
        lda     HW_GOT
        jsr     HW_HEX2
        lda     #<sexp
        ldx     #>sexp
        jsr     HW_PUTS
        lda     HW_WANT
        jsr     HW_HEX2
        lda     HW_GOT
        eor     HW_WANT
        sta     diff
        ldx     #0                      ; the bits that differ, counted
        ldy     #8
@cnt:   lsr     a
        bcc     @c0
        inx
@c0:    dey
        bne     @cnt
        cpx     #1
        beq     @one
        lda     #<sbits
        ldx     #>sbits
        bra     @word
@one:   lda     #<sbit
        ldx     #>sbit
@word:  jsr     HW_PUTS
        stz     first
        ldx     #0
@bit:   lsr     diff
        bcc     @next
        lda     first
        beq     @no
        lda     #','
        jsr     HW_PUTC
        lda     #' '
        jsr     HW_PUTC
@no:    inc     first
        txa
        ora     #'0'
        jsr     HW_PUTC
@next:  inx
        cpx     #8
        bne     @bit
        rts

; "SUMMARY  n tests: p passed, f failed, s skipped, m measured" (the last
; only if any were): A the failures.
HW_SUMMARY:
        lda     #<ssum
        ldx     #>ssum
        jsr     HW_PUTS
        clc
        lda     HW_TALLY + HL_PASS
        adc     HW_TALLY + HL_FAIL
        sta     HW_NUM
        lda     #0
        rol     a
        sta     HW_NUM + 1
        clc
        lda     HW_NUM
        adc     HW_TALLY + HL_SKIP
        sta     HW_NUM
        bcc     @a
        inc     HW_NUM + 1
@a:     clc
        lda     HW_NUM
        adc     HW_TALLY + HL_INFO
        sta     HW_NUM
        bcc     @b
        inc     HW_NUM + 1
@b:     jsr     num16
        lda     #<stests
        ldx     #>stests
        jsr     HW_PUTS
        lda     HW_TALLY + HL_PASS
        jsr     num8
        lda     #<spassed
        ldx     #>spassed
        jsr     HW_PUTS
        lda     HW_TALLY + HL_FAIL
        jsr     num8
        lda     #<sfailed
        ldx     #>sfailed
        jsr     HW_PUTS
        lda     HW_TALLY + HL_SKIP
        jsr     num8
        lda     #<sskipped
        ldx     #>sskipped
        jsr     HW_PUTS
        lda     HW_TALLY + HL_INFO
        beq     @end
        lda     #<scomma
        ldx     #>scomma
        jsr     HW_PUTS
        lda     HW_TALLY + HL_INFO
        jsr     num8
        lda     #<smeas
        ldx     #>smeas
        jsr     HW_PUTS
@end:   lda     HW_TALLY + HL_FAIL
        rts

num8:   sta     HW_NUM
        stz     HW_NUM + 1
num16:  stz     HW_NUM + 2
        stz     HW_NUM + 3
        sec
        jmp     HW_DEC

        OOHW_RODATA
words:  .word   spass, sfail, sskip, sinfo
spass:  .byte   "PASS", 0
sfail:  .byte   "FAIL", 0
sskip:  .byte   "SKIP", 0
sinfo:  .byte   "INFO", 0
sread:  .byte   "read $", 0
sexp:   .byte   ", expected $", 0
sbit:   .byte   ": bit ", 0
sbits:  .byte   ": bits ", 0
ssum:   .byte   "SUMMARY  ", 0
stests: .byte   " tests: ", 0
spassed: .byte  " passed, ", 0
sfailed: .byte  " failed, ", 0
sskipped: .byte " skipped", 0
scomma: .byte   ", ", 0
smeas:  .byte   " measured", 0

        OOHW_BSS
HW_COL: .res    1
HW_TALLY: .res  4
diff:   .res    1
first:  .res    1

; What GET.COM and PUT.COM share (hw/oodos.md, "The RSXs, phase 5", 5b): the
; command's words, a file specification, the end of the line, and the
; messages of the Guide's Appendix A they say (decided 2026-11-04, 4). After
; cmd.inc and util.inc; no zero page.
;
;   RD_WORD     the tail's next word, from UT_IDX past blanks, to a blank, a
;               tab, a '[' or the end, in RD_W (up to 15 characters, a 0
;               after): carry clear; carry set if there is none (the end, or
;               a '[')
;   RD_IS       carry set if RD_W is the 0-ended word at A/X
;   RD_END      past blanks and one option list: the end, or End of line
;               expected and the end of the program
;   RD_SPEC     a file specification to the FCB at A/X (UT_SPEC): carry clear;
;               none: Invalid command; one 152 refuses, or a wildcard:
;               Invalid file specification; either the end
;   RD_DRIVE    the FCB at A/X given its drive: 0 (the current) made the
;               current drive (25) + 1
;   RD_FAIL     the message at A/X, the return code $FF00, the end
;   RD_CMD, RD_DELIM, RD_OPT, RD_MOD, RD_AUX, RD_MEM: RD_FAIL with each message

        .code
RD_WORD:        jsr     UT_SKIP
                beq     @none
                cmp     #'['
                beq     @none
                ldy     #0                      ; (UT_PEEK and UT_GET keep Y)
@char:          jsr     UT_PEEK
                beq     @end
                cmp     #' '
                beq     @end
                cmp     #UTC_TAB
                beq     @end
                cmp     #'['
                beq     @end
                jsr     UT_GET
                cpy     #15
                bcs     @char                   ; past 15: dropped (no word is that long)
                sta     RD_W,y
                iny
                bra     @char
@end:           lda     #0
                sta     RD_W,y
                clc
                rts
@none:          sec
                rts

RD_IS:          sta     @w + 1
                stx     @w + 2
                ldx     #0
@cmp:           lda     RD_W,x
@w:             cmp     $FFFF,x
                bne     @no
                cmp     #0
                beq     @yes
                inx
                bra     @cmp
@yes:           sec
                rts
@no:            clc
                rts

RD_END:         jsr     UT_SKIP
                beq     @done
                cmp     #'['
                bne     RD_EOL
@close:         jsr     UT_GET                  ; to after its ']'
                beq     @done
                cmp     #']'
                bne     @close
                jsr     UT_SKIP
                bne     RD_EOL
@done:          rts
RD_EOL:         lda     #<rd_seol
                ldx     #>rd_seol
                bra     RD_FAIL

RD_SPEC:        sta     rd_fcb
                stx     rd_fcb + 1
                jsr     UT_SPEC
                bcc     @one
                cmp     #0
                beq     RD_CMD
                bra     @bad
@one:           lda     rd_fcb
                clc
                adc     #1
                tay
                lda     rd_fcb + 1
                adc     #0
                tax
                tya
                jsr     UT_ISWILD
                bcs     @bad
                clc
                rts
@bad:           lda     #<rd_sspec
                ldx     #>rd_sspec
                bra     RD_FAIL

RD_DRIVE:       sta     @get + 1
                sta     @put + 1
                stx     @get + 2
                stx     @put + 2
@get:           lda     $FFFF
                bne     @done
                ldy     #DRV_GET
                jsr     BDOS
                inc     a
@put:           sta     $FFFF
@done:          rts

RD_CMD:         lda     #<rd_scmd
                ldx     #>rd_scmd
                bra     RD_FAIL
RD_DELIM:       lda     #<rd_sdelim
                ldx     #>rd_sdelim
                bra     RD_FAIL
RD_OPT:         lda     #<rd_sopt
                ldx     #>rd_sopt
                bra     RD_FAIL
RD_MOD:         lda     #<rd_smod
                ldx     #>rd_smod
                bra     RD_FAIL
RD_AUX:         lda     #<rd_saux
                ldx     #>rd_saux
                bra     RD_FAIL
RD_MEM:         lda     #<rd_smem
                ldx     #>rd_smem
RD_FAIL:        jsr     UT_SAY
                jsr     UT_FAIL
                jmp     UT_EXIT

        .rodata
rd_scmd:        .byte   "Invalid command.$"
rd_sdelim:      .byte   "Invalid delimiter.$"
rd_sopt:        .byte   "Invalid option.$"
rd_smod:        .byte   "Invalid option modifier.$"
rd_seol:        .byte   "End of line expected$"
rd_saux:        .byte   "Auxiliary device redirection not implemented$"
rd_smem:        .byte   "Insufficient Memory$"
rd_sspec:       .byte   "Invalid file specification.$"
RD_CONSOLE:     .byte   "CONSOLE", 0
RD_PRINTER:     .byte   "PRINTER", 0
RD_INPUT:       .byte   "INPUT", 0
RD_OUTPUT:      .byte   "OUTPUT", 0
RD_FROM:        .byte   "FROM", 0
RD_TO:          .byte   "TO", 0
RD_FILE:        .byte   "FILE", 0
RD_AUXW:        .byte   "AUXILIARY", 0

        .bss
RD_W:           .res    16
rd_fcb:         .res    2
        .code

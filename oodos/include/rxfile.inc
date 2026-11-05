; An RSX's file read as console input (hw/oodos.md, "The RSXs, phase 5"): GET's
; and SUBMIT's (5c's decision 1), shared, not copied. Include it after rsx.inc,
; in the RSX's CODE. It reads FCB (36 bytes, opened by the program that gave the
; RSX its file) in USER, a record at a time into BUF (BIDX the next byte, 128
; for none read yet), the caller's state put back after each read; EOF is set
; at a ^Z or the file's end. Its variables, in the RSX's DATA (its BSS isn't
; cleared): user, eof, prevcr, bidx, ch, fcb, buf.
;
;   nextch  the next input byte, A (and ch): an LF after CR dropped; carry set
;           at the file's end
;   getc    the next byte as it is: carry set at ^Z or the file's end

; The file's next input byte, A (and ch): an LF after CR dropped; carry set
; at its end.
nextch: jsr     getc
        bcs     @end
        cmp     #LF
        bne     @take
        ldx     prevcr
        stz     prevcr
        bne     nextch
@take:  stz     prevcr
        cmp     #CR
        bne     @ret
        ldx     #1
        stx     prevcr
@ret:   sta     ch
        clc
@end:   rts

; The file's next byte: carry set at ^Z or the end of its last record.
getc:   lda     eof
        bne     @end
        lda     bidx
        bpl     @have
        lda     user                    ; a record, in the caller's state put back after
        jsr     RX_SAVE
        lda     #<buf
        ldx     #>buf
        ldy     #F_DMAOFF
        jsr     RX_BDOS
        lda     #<fcb
        ldx     #>fcb
        ldy     #F_READ
        jsr     RX_BDOS
        pha
        jsr     RX_RESTORE
        pla
        bne     @last
        stz     bidx
@have:  ldx     bidx
        lda     buf,x
        cmp     #CTLZ
        beq     @last
        inc     bidx
        clc
        rts
@last:  lda     #1
        sta     eof
@end:   sec
        rts


        .data
user:   .byte   0
eof:    .byte   0
prevcr: .byte   0
bidx:   .byte   128
ch:     .byte   0
fcb:    .res    36
buf:    .res    128
        .code

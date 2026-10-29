; DRIVER.COM: the model test's driver (hw/oodos.md, "The BDOS, phase 3").
; Reads A:SCRIPT.BIN, runs each operation through the BDOS, and writes what
; each returned to A:RESULT.BIN, for the host to compare with the model's
; prediction (oodos/test/modelscript.py has both formats, and Driver, these
; steps in Python: the two make the same BDOS calls in the same order).
;
; Only CP/M 3's documented functions, with OODOS's registers: the function
; in Y, a byte in A, a word in A (low) and X (high); the result in A and X.
; Error mode $FF (45) first, so an error comes back as A = $FF, X = the code.
; It keeps 8 FCBs, its own DMA buffer, the script (16 KB at most) and the
; results (24 KB) in BSS; the operations' results go to memory and are
; written at the end, so the script's searches and drive resets aren't
; disturbed by the driver's own file I/O on A:.

        .setcpu "65C02"
        .include "oodos.inc"
        OODOS_HEADER

SCRIPT_MAX      = 16384
RESULT_MAX      = 24576
NSLOTS          = 8
FCBLEN          = 36
FILL            = $5A

        .zeropage
sp:     .res    2                       ; the next script byte
rp:     .res    2                       ; the next result byte
fp:     .res    2                       ; the operation's FCB
ptr:    .res    2
left:   .res    2                       ; operations still to run
ran:    .res    2                       ; operations run
cnt:    .res    2                       ; a loop's limit
done:   .res    2                       ; and how far it got
tag:    .res    2                       ; the write pattern's tag
fn:     .res    1
ra:     .res    1                       ; what the BDOS returned
rx:     .res    1
acc:    .res    1                       ; the pattern's running byte
step:   .res    1
s1:     .res    1                       ; Fletcher's sums
s2:     .res    1
t1:     .res    1
t2:     .res    1
status: .res    1
dig:    .res    3                       ; MAKEN's counter, in ASCII

        .code
start:  lda     #<banner
        ldx     #>banner
        ldy     #C_WRITESTR
        jsr     BDOS
        lda     #$FF                    ; errors come back to us
        ldy     #F_ERRMODE
        jsr     BDOS
        ldy     #DRV_ALLRESET
        jsr     BDOS
        jsr     setdma
        lda     #0
        ldy     #F_USERNUM
        jsr     BDOS
; The script, a record at a time, to its end or the buffer's.
        ldx     #0                      ; A:SCRIPT.BIN
        jsr     iofcb
        ldy     #F_OPEN
        jsr     iocall
        lda     #1
        sta     status
        lda     ra
        cmp     #$FF
        bne     :+
        jmp     finish
:       lda     #<script
        sta     sp
        lda     #>script
        sta     sp+1
        stz     cnt
@rec:   lda     sp
        ldx     sp+1
        ldy     #F_DMAOFF
        jsr     BDOS
        ldy     #F_READ
        jsr     iocall
        lda     ra
        bne     @eof
        clc
        lda     sp
        adc     #128
        sta     sp
        bcc     :+
        inc     sp+1
:       inc     cnt
        lda     cnt
        cmp     #SCRIPT_MAX / 128
        bne     @rec
@eof:   ldy     #F_CLOSE
        jsr     iocall
        jsr     setdma
        ldx     #3                      ; "OOMS", version 1
:       lda     script,x
        cmp     smagic,x
        bne     @bad
        dex
        bpl     :-
        lda     script+4
        cmp     #1
        beq     :+
@bad:   jmp     finish
:       lda     script+6
        sta     left
        lda     script+7
        sta     left+1
        lda     #<(script+16)
        sta     sp
        lda     #>(script+16)
        sta     sp+1
        lda     #<(results+16)
        sta     rp
        lda     #>(results+16)
        sta     rp+1
        stz     ran
        stz     ran+1
        stz     status

; Each operation: its opcode copied to the results, then its handler.
next:   lda     left
        ora     left+1
        bne     :+
        jmp     finish
:       lda     rp+1                    ; room for the largest result?
        cmp     #>(results+RESULT_MAX-64)
        bcc     :+
        lda     #2
        sta     status
        jmp     finish
:       jsr     get
        cmp     #OPCOUNT
        bcs     badop
        jsr     put
        asl     a
        tax
        jmp     (optab,x)
badop:  lda     #3
        sta     status
        jmp     finish
opdone: inc     ran
        bne     :+
        inc     ran+1
:       lda     left
        bne     :+
        dec     left+1
:       dec     left
        jmp     next

; --- the operations ---

; FCB slot, bytes 0-15, CR, R0-R2: bytes 16-31 zero.
op_fcb: jsr     slot
        ldy     #0
:       jsr     get
        sta     (fp),y
        iny
        cpy     #16
        bne     :-
        lda     #0
:       sta     (fp),y
        iny
        cpy     #32
        bne     :-
:       jsr     get
        sta     (fp),y
        iny
        cpy     #36
        bne     :-
        jmp     opdone

; NAME2 slot, bytes 16-31.
op_name2:
        jsr     slot
        ldy     #16
:       jsr     get
        sta     (fp),y
        iny
        cpy     #32
        bne     :-
        jmp     opdone

; RAND slot, R0-R2.
op_rand:
        jsr     slot
        ldy     #33
:       jsr     get
        sta     (fp),y
        iny
        cpy     #36
        bne     :-
        jmp     opdone

; FCALL fn, slot: A X FCB, and 17's entry.
op_fcall:
        jsr     get
        sta     fn
        jsr     slot
        jsr     fcall
        jsr     putax
        jsr     fcb5
        lda     fn
        cmp     #F_SFIRST
        bne     :+
        jsr     entry
:       jmp     opdone

; NEXT: 18, A X and the entry.
op_next:
        ldy     #F_SNEXT
        jsr     BDOS
        sta     ra
        stx     rx
        jsr     putax
        jsr     entry
        jmp     opdone

; READ fn, slot: A X FCB and the record's checksum.
op_read:
        jsr     get
        sta     fn
        jsr     slot
        jsr     fill
        jsr     fcall
        jsr     putax
        jsr     fcb5
        stz     s1
        stz     s2
        jsr     sum
        lda     s1
        jsr     put
        lda     s2
        jsr     put
        jmp     opdone

; WRITE fn, slot, tag: A X FCB.
op_write:
        jsr     get
        sta     fn
        jsr     slot
        jsr     get
        sta     tag
        jsr     get
        sta     tag+1
        jsr     pattern
        jsr     fcall
        jsr     putax
        jsr     fcb5
        jmp     opdone

; WRITEN slot, n, tag: n writes, record k with tag + k, to the first error.
op_writen:
        jsr     slot
        jsr     getcnt
        jsr     get
        sta     tag
        jsr     get
        sta     tag+1
        lda     #F_WRITE
        sta     fn
@loop:  jsr     more
        bcc     @end
        jsr     pattern
        jsr     fcall
        lda     ra
        bne     @end
        jsr     bump
        inc     tag
        bne     @loop
        inc     tag+1
        bra     @loop
@end:   jsr     putax
        jsr     putdone
        jsr     fcb5
        jmp     opdone

; READN slot, n: n reads, to the first error; the checksum of those read.
op_readn:
        jsr     slot
        jsr     getcnt
        stz     s1
        stz     s2
        lda     #F_READ
        sta     fn
@loop:  jsr     more
        bcc     @end
        jsr     fill
        jsr     fcall
        lda     ra
        bne     @end
        jsr     sum
        jsr     bump
        bra     @loop
@end:   jsr     putax
        jsr     putdone
        lda     s1
        jsr     put
        lda     s2
        jsr     put
        jsr     fcb5
        jmp     opdone

; SALL slot: 17, then 18 to the end; how many, and two checksums.
op_sall:
        jsr     slot
        stz     done
        stz     done+1
        stz     s1
        stz     s2
        stz     t1
        stz     t2
        lda     #F_SFIRST
        sta     fn
        jsr     fcall
@loop:  lda     ra
        cmp     #4
        bcs     @end
        jsr     bump
        lda     ra                      ; A, then the entry's bytes 0-15
        jsr     add1
        lda     ra
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        ldy     #16
:       lda     dmabuf,x
        jsr     add1
        inx
        dey
        bne     :-
        ldy     #16                     ; bytes 16-31
:       lda     dmabuf,x
        jsr     add2
        inx
        dey
        bne     :-
        ldy     #F_SNEXT
        jsr     BDOS
        sta     ra
        stx     rx
        bra     @loop
@end:   jsr     putax
        jsr     putdone
        lda     s1
        jsr     put
        lda     s2
        jsr     put
        lda     t1
        jsr     put
        lda     t2
        jsr     put
        jmp     opdone

; MAKEN slot, n: make (then close) NAME000, NAME001... to the first refusal.
op_maken:
        jsr     slot
        jsr     getcnt
        lda     #'0'
        sta     dig
        sta     dig+1
        sta     dig+2
        stz     ra
        stz     rx
@loop:  jsr     more
        bcc     @end
        ldy     #6
        lda     dig
        sta     (fp),y
        iny
        lda     dig+1
        sta     (fp),y
        iny
        lda     dig+2
        sta     (fp),y
        ldy     #12
        lda     #0
:       sta     (fp),y
        iny
        cpy     #FCBLEN
        bne     :-
        lda     #F_MAKE
        sta     fn
        jsr     fcall
        lda     ra
        cmp     #4
        bcs     @end
        lda     ra                      ; keep the make's A and X
        pha
        lda     rx
        pha
        lda     #F_CLOSE
        sta     fn
        jsr     fcall
        pla
        sta     rx
        pla
        sta     ra
        jsr     bump
        ldx     #2                      ; the counter + 1
:       inc     dig,x
        lda     dig,x
        cmp     #'9' + 1
        bne     @loop
        lda     #'0'
        sta     dig,x
        dex
        bpl     :-
        bra     @loop
@end:   jsr     putax
        jsr     putdone
        jmp     opdone

; CALL fn, A, X: A X; after 13 the DMA address is ours again.
op_call:
        jsr     get
        sta     fn
        jsr     get
        pha
        jsr     get
        tax
        pla
        ldy     fn
        jsr     BDOS
        sta     ra
        stx     rx
        jsr     putax
        lda     fn
        cmp     #DRV_ALLRESET
        bne     :+
        jsr     setdma
:       jmp     opdone

; FREE drive: 46, A X and the free records (or three zeros).
op_free:
        jsr     get
        ldy     #DRV_SPACE
        jsr     BDOS
        sta     ra
        stx     rx
        jsr     putax
        ldx     #0
:       lda     ra
        beq     :+
        lda     #0
        bra     :++
:       lda     dmabuf,x
:       jsr     put
        inx
        cpx     #3
        bne     :---
        jmp     opdone

; DPB: 31, A X and the 17 bytes there.
op_dpb: ldy     #DRV_DPB
        jsr     BDOS
        sta     ra
        stx     rx
        sta     ptr
        stx     ptr+1
        jsr     putax
        ldy     #0
:       lda     (ptr),y
        jsr     put
        iny
        cpy     #17
        bne     :-
        jmp     opdone

; TIME days, hour, minute: 104.
op_time:
        ldx     #0
:       jsr     get
        sta     dat,x
        inx
        cpx     #4
        bne     :-
        lda     #<dat
        ldx     #>dat
        ldy     #T_SET
        jsr     BDOS
        sta     ra
        stx     rx
        jsr     putax
        jmp     opdone

; --- the end: RESULT.BIN ---
finish: lda     #'O'                    ; the header: "OOMR", 1, status, ran, length
        sta     results
        sta     results+1
        lda     #'M'
        sta     results+2
        lda     #'R'
        sta     results+3
        lda     #1
        sta     results+4
        lda     status
        sta     results+5
        lda     ran
        sta     results+6
        lda     ran+1
        sta     results+7
        lda     status
        cmp     #1
        bne     :+
        lda     #<(results+16)          ; no script: no results
        sta     rp
        lda     #>(results+16)
        sta     rp+1
:       sec
        lda     rp
        sbc     #<(results+16)
        sta     results+8
        lda     rp+1
        sbc     #>(results+16)
        sta     results+9
        ldx     #5
:       stz     results+10,x
        dex
        bpl     :-
@zero:  sec                             ; zeros to the record's end
        lda     rp
        sbc     #<results
        and     #$7F
        beq     :+
        lda     #0
        jsr     put
        bra     @zero
:       lda     #0
        ldy     #DRV_FLUSH
        jsr     BDOS
        ldy     #DRV_ALLRESET
        jsr     BDOS
        lda     #0
        ldy     #F_USERNUM
        jsr     BDOS
        ldx     #11                     ; A:RESULT.BIN
        jsr     iofcb
        ldy     #F_DELETE
        jsr     iocall
        ldx     #11
        jsr     iofcb
        ldy     #F_MAKE
        jsr     iocall
        lda     ra
        cmp     #4
        bcc     :+
        lda     status                  ; RESULT.BIN can't be made: say so, unless
        bne     :+                      ; something went wrong before
        lda     #4
        sta     status
:       lda     #<results
        sta     ptr
        lda     #>results
        sta     ptr+1
@rec:   lda     ptr                     ; to rp, a record at a time
        cmp     rp
        lda     ptr+1
        sbc     rp+1
        bcs     @close
        lda     ptr
        ldx     ptr+1
        ldy     #F_DMAOFF
        jsr     BDOS
        ldy     #F_WRITE
        jsr     iocall
        clc
        lda     ptr
        adc     #128
        sta     ptr
        bcc     @rec
        inc     ptr+1
        bra     @rec
@close: ldy     #F_CLOSE
        jsr     iocall
        lda     status                  ; say how it went
        asl     a
        tax
        lda     msgs,x
        pha
        lda     msgs+1,x
        tax
        pla
        ldy     #C_WRITESTR
        jsr     BDOS
        jmp     WBOOT

; --- helpers ---

; The next script byte, in A.
get:    lda     (sp)
        inc     sp
        bne     :+
        inc     sp+1
:       rts

; A to the results; A, X and Y kept.
put:    sta     (rp)
        inc     rp
        bne     :+
        inc     rp+1
:       rts

; fp = the FCB of the slot that is the next script byte.
slot:   jsr     get
        and     #NSLOTS - 1
        tax
        lda     fcblo,x
        sta     fp
        lda     fcbhi,x
        sta     fp+1
        rts

; The function fn with A/X = fp; A and X to ra and rx.
fcall:  lda     fp
        ldx     fp+1
        ldy     fn
        jsr     BDOS
        sta     ra
        stx     rx
        rts

; The function in Y on the driver's own FCB.
iocall: lda     #<io
        ldx     #>io
        jsr     BDOS
        sta     ra
        stx     rx
        rts

; The driver's FCB: drive A:, the name at names + X, the rest zero.
iofcb:  ldy     #0
        lda     #1
        sta     io
:       lda     names,x
        sta     io+1,y
        inx
        iny
        cpy     #11
        bne     :-
        lda     #0
:       sta     io+1,y
        iny
        cpy     #FCBLEN - 1
        bne     :-
        rts

setdma: lda     #<dmabuf
        ldx     #>dmabuf
        ldy     #F_DMAOFF
        jmp     BDOS

putax:  lda     ra
        jsr     put
        lda     rx
        jmp     put

; EX, CR, R0, R1, R2 of the FCB.
fcb5:   ldy     #12
        lda     (fp),y
        jsr     put
        ldy     #32
:       lda     (fp),y
        jsr     put
        iny
        cpy     #36
        bne     :-
        rts

; With A < 4, the directory entry A of the DMA buffer.
entry:  lda     ra
        cmp     #4
        bcs     @no
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        ldy     #32
:       lda     dmabuf,x
        jsr     put
        inx
        dey
        bne     :-
@no:    rts

; The DMA buffer: all FILL, or the tag's pattern.
fill:   ldx     #127
        lda     #FILL
:       sta     dmabuf,x
        dex
        bpl     :-
        rts
pattern:
        lda     tag
        sta     acc
        lda     tag+1
        ora     #1
        sta     step
        ldy     #0
:       tya
        eor     acc
        sta     dmabuf,y
        clc
        lda     acc
        adc     step
        sta     acc
        iny
        cpy     #128
        bne     :-
        rts

; Fletcher's sums over the DMA buffer, continuing s1 and s2.
sum:    ldx     #0
:       lda     dmabuf,x
        jsr     add1
        inx
        cpx     #128
        bne     :-
        rts
add1:   clc
        adc     s1
        sta     s1
        clc
        adc     s2
        sta     s2
        rts
add2:   clc
        adc     t1
        sta     t1
        clc
        adc     t2
        sta     t2
        rts

; Loops: cnt from the script, done from 0; more is carry set while done < cnt.
getcnt: jsr     get
        sta     cnt
        jsr     get
        sta     cnt+1
        stz     done
        stz     done+1
        stz     ra
        stz     rx
        rts
more:   lda     done
        cmp     cnt
        lda     done+1
        sbc     cnt+1
        bcc     :+
        clc
        rts
:       sec
        rts
bump:   inc     done
        bne     :+
        inc     done+1
:       rts
putdone:
        lda     done
        jsr     put
        lda     done+1
        jmp     put

        .rodata
optab:  .word   badop, op_fcb, op_name2, op_rand, badop, badop, badop, badop      ; $00
        .word   badop, badop, badop, badop, badop, badop, badop, badop
        .word   op_fcall, op_next, op_read, op_write, op_writen, op_readn, op_sall, op_maken ; $10
        .word   badop, badop, badop, badop, badop, badop, badop, badop
        .word   op_call, op_free, op_dpb, op_time                                  ; $20
OPCOUNT = (* - optab) / 2
fcblo:  .repeat NSLOTS, i
        .byte   <(fcbs + i * FCBLEN)
        .endrepeat
fcbhi:  .repeat NSLOTS, i
        .byte   >(fcbs + i * FCBLEN)
        .endrepeat
smagic: .byte   "OOMS"
names:  .byte   "SCRIPT  BIN", "RESULT  BIN"
banner: .byte   "DRIVER: the model test's script", 13, 10, '$'
msgs:   .word   m_ok, m_noscript, m_full, m_badop, m_nomake
m_ok:   .byte   "DRIVER OK", 13, 10, '$'
m_noscript:
        .byte   "DRIVER: NO SCRIPT", 13, 10, '$'
m_full: .byte   "DRIVER: RESULTS FULL", 13, 10, '$'
m_badop:
        .byte   "DRIVER: BAD OPCODE", 13, 10, '$'
m_nomake:
        .byte   "DRIVER: CAN'T MAKE RESULT.BIN", 13, 10, '$'

        .bss
fcbs:   .res    NSLOTS * FCBLEN
io:     .res    FCBLEN
dmabuf: .res    128
dat:    .res    4
script: .res    SCRIPT_MAX
results:
        .res    RESULT_MAX

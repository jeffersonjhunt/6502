; HELLO: prints a line through the BDOS twice, once with C_WRITESTR and once a
; character at a time, then the command tail. Built as both a .COM and a .PRL,
; it gives the tools every kind of relocation: absolute operands, address
; words, high bytes as immediates, a zero page pointer, and a zero page
; address as an immediate.

        .include "oodos.inc"

        OODOS_HEADER

        .zeropage
ptr:    .res    2
count:  .res    1

        .code
start:  lda     #<msg                   ; C_WRITESTR: the address in A and X
        ldx     #>msg
        ldy     #C_WRITESTR
        jsr     BDOS
        ldx     #ptr                    ; a zero page address passed in X
        lda     #<msg
        ldy     #>msg
        jsr     setptr
        jsr     putstr
        ldx     #0                      ; the routines through a table
@next:  lda     table,x
        sta     vector
        lda     table+1,x
        sta     vector+1
        stx     count
        jsr     callv
        ldx     count
        inx
        inx
        cpx     #tabend-table
        bne     @next
        jmp     WBOOT

callv:  jmp     (vector)

; Point the zero page word at X to A (low), Y (high).
setptr: sta     0,x
        tya
        sta     1,x
        rts

; Print ptr's string a character at a time, to its '$'.
putstr: lda     #0
        sta     index
@loop:  ldy     index
        lda     (ptr),y
        cmp     #'$'
        beq     @done
        ldy     #C_WRITE
        jsr     BDOS
        inc     index
        bne     @loop
@done:  rts

; The command tail, a count then the characters, at the DMA buffer. The '$'
; goes after them, so a count over 126 is cut to 126 to keep it in the buffer.
tail:   lda     #<(BP_DMA + 1)
        sta     ptr
        lda     #>(BP_DMA + 1)
        sta     ptr+1
        ldy     BP_DMA
        cpy     #127
        bcc     @end
        ldy     #126
@end:   lda     #'$'
        sta     (ptr),y
        jmp     putstr

crlf:   lda     #<nl
        ldx     #>nl
        ldy     #C_WRITESTR
        jmp     BDOS

        .rodata
msg:    .byte   "Hello from OODOS", 13, 10, '$'
nl:     .byte   13, 10, '$'
table:  .word   tail, crlf
tabend:

        .bss
vector: .res    2
index:  .res    1

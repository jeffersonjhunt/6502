; int write(int fd, const void *buf, unsigned count): for fd 1 and 2 (stdout,
; stderr), each byte to the console by function 2, C_WRITE, a '\n' as CR LF.
; Returns count. Any other fd: -1, errno EBADF (files are a later step).

        .include "oodos.inc"
        .include "errno.inc"

        .export         _write
        .import         popax, __directerrno
        .importzp       ptr1, ptr2, ptr3

_write: sta     ptr2                    ; count
        stx     ptr2+1
        sta     ptr3                    ; the bytes left
        stx     ptr3+1
        jsr     popax                   ; buf
        sta     ptr1
        stx     ptr1+1
        jsr     popax                   ; fd
        cpx     #0
        bne     bad
        cmp     #1
        beq     next
        cmp     #2
        bne     bad
next:   lda     ptr3
        ora     ptr3+1
        beq     done
        lda     (ptr1)
        cmp     #10
        bne     put
        lda     #13
        jsr     cout
        lda     #10
put:    jsr     cout
        inc     ptr1
        bne     :+
        inc     ptr1+1
:       lda     ptr3
        bne     :+
        dec     ptr3+1
:       dec     ptr3
        bra     next
done:   lda     ptr2
        ldx     ptr2+1
        rts
bad:    lda     #EBADF
        jmp     __directerrno

cout:   ldy     #C_WRITE
        jmp     BDOS

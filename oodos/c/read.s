; int read(int fd, void *buf, unsigned count): for fd 0 (stdin), a line from
; the console by function 10, C_READSTR, with OODOS's line editing, then a
; '\n' after it, as C's stdin has it. A line is given out over as many calls
; as count needs. A line of a ^Z alone is the end of the file: 0. Any other
; fd: -1, errno EBADF.
;
; Function 10 echoes the CR that ends a line, not an LF: read writes the LF.

        .include "oodos.inc"
        .include "errno.inc"

        .export         _read
        .import         popax, __directerrno
        .importzp       ptr1, ptr2

LINE    = 127                           ; a line's characters at most, and its '\n'

_read:  sta     ptr2                    ; count
        stx     ptr2+1
        jsr     popax                   ; buf
        sta     ptr1
        stx     ptr1+1
        jsr     popax                   ; fd
        cpx     #0
        bne     bad
        cmp     #0
        bne     bad
        lda     ptr2
        ora     ptr2+1
        beq     none                    ; count 0: nothing read
        lda     left
        bne     give
        lda     #LINE
        sta     line
        lda     #<line
        ldx     #>line
        ldy     #C_READSTR
        jsr     BDOS
        lda     #10
        ldy     #C_WRITE
        jsr     BDOS
        ldx     line+1                  ; the line's length
        cpx     #1
        bne     :+
        lda     line+2
        cmp     #$1A
        beq     none                    ; ^Z alone: the end
:       lda     #10
        sta     line+2,x
        inx
        stx     left
        stz     at
give:   ldy     #0                      ; the bytes given
:       ldx     at
        lda     line+2,x
        sta     (ptr1),y
        inc     at
        iny
        dec     left
        beq     out
        lda     ptr2+1
        bne     :-                      ; count 256 or more: the line ends first
        cpy     ptr2
        bne     :-
out:    tya
        ldx     #0
        rts
none:   lda     #0
        tax
        rts
bad:    lda     #EBADF
        jmp     __directerrno

        .bss
line:   .res    2 + LINE + 1            ; function 10's buffer: size, length, the line
left:   .res    1                       ; its bytes not yet given
at:     .res    1                       ; the next of them

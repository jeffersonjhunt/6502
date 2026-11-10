; main's argc and argv, from the command tail (Plan C1): a constructor, run by
; crt0's initlib. The tail, at BP_DMA, is its length and the characters after
; the command's name, as the CCP leaves them (capitals); it is copied out,
; since the DMA buffer is the first file call's, and split at spaces. argv[0]
; is "" (OODOS doesn't keep the program's name), argv[argc] is NULL, and
; argc is at most MAXARGS, the words past it dropped.

        .include "oodos.inc"

        .constructor    initmainargs, 24
        .import         __argc, __argv

MAXARGS = 16
TAIL    = 127                           ; the tail's characters at most

        .segment "ONCE"
initmainargs:
        lda     #<argv
        sta     __argv
        lda     #>argv
        sta     __argv+1
        lda     #<name
        sta     argv
        lda     #>name
        sta     argv+1
        lda     #1
        sta     __argc
        stz     __argc+1
        lda     BP_DMA                  ; the tail's length
        cmp     #TAIL
        bcc     :+
        lda     #TAIL
:       tay
        lda     #0
        sta     args,y
copy:   dey
        bmi     split
        lda     BP_DMA+1,y
        sta     args,y
        bra     copy
split:  ldx     #0                      ; in args
        ldy     #2                      ; argv's next slot
skip:   lda     args,x
        beq     end
        cmp     #' '
        bne     word
        inx
        bra     skip
word:   cpy     #MAXARGS*2
        bcs     end
        txa
        clc
        adc     #<args
        sta     argv,y
        lda     #>args
        adc     #0
        sta     argv+1,y
        iny
        iny
        inc     __argc
:       inx
        lda     args,x
        beq     end
        cmp     #' '
        bne     :-
        stz     args,x
        inx
        bra     skip
end:    lda     #0
        sta     argv,y
        sta     argv+1,y
        rts

        .rodata
name:   .byte   0

        .bss
args:   .res    TAIL + 1
argv:   .res    (MAXARGS + 1) * 2

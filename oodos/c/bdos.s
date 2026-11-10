; unsigned __fastcall__ bdos(unsigned char fn, unsigned arg): any BDOS function
; from C, as oodos.inc's rule says: the function in Y, arg in A (low) and X
; (high), the result A (low) and X (high). A function that returns a byte
; gives it in the low byte; the high byte is X as the function leaves it.

        .include "oodos.inc"

        .export         _bdos
        .import         popa
        .importzp       tmp1, tmp2

_bdos:  sta     tmp1
        stx     tmp2
        jsr     popa                    ; fn
        tay
        lda     tmp1
        ldx     tmp2
        jmp     BDOS

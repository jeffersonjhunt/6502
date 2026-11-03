; CLS.COM: clears the screen, as CLEAR.COM does, then lists the files, as DIR
; with the same tail: CLS is DIR's listing on a clear screen, and CLS [SYS]
; or CLS *.COM [SIZE] are DIR's too. It chains to DIR (function 47), so the
; CCP shows its prompt and the DIR command at the top before the listing
; (hw/oodos.md, function 47). A tail too long for 47's 126 characters with
; DIR is cut off at its 123rd.

        .setcpu "65C02"
        .include "oodos.inc"

CHAIN_MAX = 126                         ; function 47's command, at most

        OODOS_HEADER

        .code
start:  lda     #<clear
        ldx     #>clear
        ldy     #C_WRITESTR
        jsr     BDOS
        ldx     BP_DMA                  ; the tail's length, its characters after it
        cpx     #CHAIN_MAX - 3 + 1      ; DIR and the tail, at most 47's 126
        bcc     @shift
        ldx     #CHAIN_MAX - 3
        stx     BP_DMA
@shift: cpx     #0                      ; the tail moved up 2, from its end, to follow DIR
        beq     @dir
        lda     BP_DMA,x
        sta     BP_DMA + 2,x
        dex
        bra     @shift
@dir:   ldx     BP_DMA
        stz     BP_DMA + 3,x            ; the command ends at a 0
        ldx     #2
@name:  lda     dir,x
        sta     BP_DMA,x
        dex
        bpl     @name
        lda     #0                      ; the CCP's drive and user
        ldy     #P_CHAIN
        jmp     BDOS                    ; ends the program

        .rodata
clear:  .byte   27, 'E', '$'
dir:    .byte   "DIR"

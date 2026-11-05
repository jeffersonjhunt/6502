; What OODOS's own RSXs share (hw/oodos.md, "The RSXs, phase 5"): GET's and
; PUT's (5b), SUBMIT's (5c). Include it after oodos.inc and RSX_PREFIX, in
; the RSX's CODE; it needs PREFIX, the RSX's first byte (its prefix), and
; uses no zero page: its pointers are in its own instructions.
;
;   RX_BDOS     a BDOS call of the RSX's own, by its offset 9 (never
;               $0203, which would reach it again): Y the function, A/X
;   RX_SAVE     the caller's DMA address and segment (52), user (32), error
;               mode and multi-sector count (49) kept, and the RSX's set: no
;               segment, the user A, errors returned, a record at a time
;   RX_RESTORE  all of RX_SAVE's put back as they were
;   RX_REMOVE   its remove flag $FF: the next warm boot takes it out
;   RX_STAY     its remove flag 0
;   RX_SCBGET   A = SCB byte X (49), by RX_BDOS
;   RX_PEEK     A = the byte at the address in RX_PTR + Y (RX_SETP sets it,
;               A/X); keeps X and Y
;   RX_PB49     after a call of 49 (A/X its block, RX_SETP'd): carry set and
;               A the byte, if it sets $19 (the CCP's state); else carry
;               clear
;
; The function bytes (RXF_*) and $19's bits (RX_*) are oodos.inc's.

        .code
RX_BDOS:        jmp     PREFIX + RSX_NEXT

RX_SAVE:        sta     rx_user
                lda     #<rx_dma
                ldx     #>rx_dma
                ldy     #F_DMAGET
                jsr     RX_BDOS
                lda     #$FF
                ldy     #F_USERNUM
                jsr     RX_BDOS
                sta     rx_ouser
                ldx     #$4B
                jsr     RX_SCBGET
                sta     rx_err
                ldx     #$4A
                jsr     RX_SCBGET
                sta     rx_mul
                lda     #$FF
                tax
                ldy     #F_DMASEG
                jsr     RX_BDOS
                lda     rx_user
                ldy     #F_USERNUM
                jsr     RX_BDOS
                lda     #$FF
                ldy     #F_ERRMODE
                jsr     RX_BDOS
                lda     #1
                ldy     #F_MULTISEC
                jmp     RX_BDOS

RX_RESTORE:     lda     rx_dma
                ldx     rx_dma + 1
                ldy     #F_DMAOFF
                jsr     RX_BDOS
                lda     rx_dma + 2
                ldx     rx_dma + 3
                ldy     #F_DMASEG
                jsr     RX_BDOS
                lda     rx_ouser
                ldy     #F_USERNUM
                jsr     RX_BDOS
                lda     #$4B
                ldx     rx_err
                jsr     rx_scbset
                lda     #$4A
                ldx     rx_mul
                ; falls through
rx_scbset:      sta     rx_pb
                lda     #$FF
                sta     rx_pb + 1
                stx     rx_pb + 2
                bra     rx_scb

RX_SCBGET:      stx     rx_pb
                stz     rx_pb + 1
rx_scb:         lda     #<rx_pb
                ldx     #>rx_pb
                ldy     #S_SCB
                jmp     RX_BDOS

RX_REMOVE:      lda     #$FF
                sta     PREFIX + RSX_REMOVE
                rts
RX_STAY:        stz     PREFIX + RSX_REMOVE
                rts

RX_SETP:        sta     RX_PEEK + 1
                stx     RX_PEEK + 2
                rts
RX_PEEK:        lda     $FFFF,y
                rts

RX_PB49:        phy
                ldy     #0
                jsr     RX_PEEK
                cmp     #SCB_CCPST
                bne     @no
                iny
                jsr     RX_PEEK
                cmp     #$FF
                bne     @no
                iny
                jsr     RX_PEEK
                ply
                sec
                rts
@no:            ply
                clc
                rts

        .data                           ; in the file: an RSX's BSS isn't cleared
rx_dma:         .res    4
rx_user:        .byte   0
rx_ouser:       .byte   0
rx_err:         .byte   0
rx_mul:         .byte   0
rx_pb:          .byte   0, 0, 0, 0
        .code

; The ROM's diagnostics, flash block 22 (hw/diag.md, plan 2): stages 7-10,
; the north bridge and the clock, DMA, the video card and the sound card.
; D3a: the block's frame, with each stage's line SKIP until D3b builds it;
; then block 21, or, if block 21 isn't this build's, the end here.

.setcpu "65C02"
.include "backplane.inc"
.include "diag.inc"

.segment "D22"
                DG_HEADER 1, START
.assert * = $8025, error, "block 22's header"

START:
                cld
                ldx     #$FF
                txs
                lda     #7
@stage:         sta     DG_KEY
                jsr     LATER
                lda     DG_KEY
                inc     a
                cmp     #11
                bne     @stage
                lda     #DG_B21
                ldx     #DE_START
                jsr     HANDOFF
                lda     #13
                sta     DG_T0 + 2
                lda     #11
                jsr     NOTFLASH
                jmp     ENDRUN

; Stage A's line, SKIP: not built yet.
LATER:
                asl     a
                tax
                lda     STNAMES - 14,x
                sta     DG_T0
                lda     STNAMES - 13,x
                sta     DG_T0 + 1
                lda     DG_KEY
                jsr     STAGEBEG
                lda     #DR_SKIP
                jsr     STAGEEND
                jsr     SCROFF
                lda     #<SLATER
                ldx     #>SLATER
                jmp     PUTS

        .include "diagcon.inc"

SLATER:         .byte   "      NOT IN THIS BUILD YET (D3B)", $0D, 0

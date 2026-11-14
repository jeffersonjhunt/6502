; The ROM's diagnostics, flash block 21 (hw/diag.md, plan 2): stages 11-14,
; storage (read-only), USB, serial and the panel, the interactive tests and the end.
; D3a: the block's frame, with each stage's line SKIP until D3b builds it;
; then the run's end (diagcon.inc's ENDRUN).

.setcpu "65C02"
.include "backplane.inc"
.include "diag.inc"
.importzp BIOS_VMAJ, BIOS_VMIN

.segment "D21"
                DG_HEADER 2, START
.assert * = $8025, error, "block 21's header"

START:
                cld
                ldx     #$FF
                txs
                lda     #11
@stage:         sta     DG_KEY
                jsr     LATER
                lda     DG_KEY
                inc     a
                cmp     #14
                bne     @stage
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

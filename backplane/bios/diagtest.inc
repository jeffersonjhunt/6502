; The card stages' report and their helpers (hw/diag.md, plan 2, D3b): included
; by blocks 22 and 21 (diag22.s, diag21.s) after diagcon.inc, each its own
; copy under its own names, with OOHW_BLK (22 or 21) set. Block 23's stages
; 1-6 don't use it.
;
; A card stage's report. Its line goes on the screen's row when the stage
; starts (CBEG: the serial line muted, DG_MUTE), with dots to the result;
; then a line a test on the serial line (TBEG, TEND), in D2's style: the
; name, what was seen, dots, and PASS, FAIL, SKIP or INFO, with a failure's
; detail under it (TDET); then the stage's line again on the serial line,
; with its result, and the result on the screen's row (CEND). A stage is
; FAIL if any of its tests is, SKIP if its card isn't there, else PASS. Each
; test is counted in DG_TALLY, which stage 14's line gives (SUMMARY). The
; screen has only the run's first failure's detail, on row 18, as D3a's.

; A card stage begun: A the stage, DG_T0 its name. The screen's line only.
CBEG:
                ldx     DG_T0
                stx     C_NAME
                ldx     DG_T0 + 1
                stx     C_NAME + 1
                stz     C_FAIL
                stz     T_OPEN
                tax
                lda     DG_SCR
                ora     #DG_MUTE
                sta     DG_SCR
                txa
                jsr     STAGEBEG
                lda     DG_SCR
                and     #<~DG_MUTE
                sta     DG_SCR
                stz     DG_COL         ; the serial line's column is still at its start
                jmp     SCROFF

; The stage's end: A the result if no test failed (DR_PASS, or DR_SKIP).
CEND:
                ldx     C_FAIL
                beq     @r
                lda     #DR_FAIL
@r:             pha
                lda     C_NAME
                sta     DG_T0
                lda     C_NAME + 1
                sta     DG_T0 + 1
                lda     DG_STAGE
                jsr     STAGEBEG
                pla
                jsr     STAGEEND
                jmp     SCROFF

; The stage skipped, its card not there: A/X why, on the line under it.
CSKIP:
                pha
                phx
                lda     #DR_SKIP
                jsr     CEND
                plx
                pla
                jmp     NOTE6

; The stage skipped: its card's revision (A) isn't one this build knows.
CSKIPREV:
                pha
                lda     #DR_SKIP
                jsr     CEND
                jsr     SIX
                SAY     "REVISION "
                pla
                jsr     PUTDEC
                SAY     ": NOT KNOWN TO THIS BUILD"
                jmp     CRLF

; A test's line: A/X its name and text, indented, the serial line only.
TBEG:
                pha
                phx
                jsr     TEND0          ; one left open: closed as it stands
                jsr     SCROFF
                ldx     #4
@ind:           jsr     SPACE
                dex
                bne     @ind
                plx
                pla
                jsr     PUTS
                lda     #1
                sta     T_OPEN
                rts

TPASS:          lda     #DR_PASS
                bra     TEND
TINFO:          lda     #DR_INFO
                bra     TEND
TEND0:          lda     T_OPEN         ; an open line: PASS (a test that said nothing wrong)
                beq     TEND9
                lda     #DR_PASS
; The test's result, A (DR_PASS, DR_FAIL, DR_SKIP or DR_INFO): dots to the
; result's column, the word; counted. Nothing if no line is open.
TEND:
                ldx     T_OPEN
                beq     TEND9
                pha
                jsr     SPACE
@dots:          lda     DG_COL
                cmp     #DG_RESCOL - 1
                bcs     @word
                lda     #'.'
                jsr     PUTC
                bra     @dots
@word:          lda     #' '
                jsr     PUTC
                pla
                jsr     WORD           ; A, X kept
                tax
                inc     DG_TALLY - 1,x
                cpx     #DR_FAIL
                bne     @shut
                stx     C_FAIL
@shut:          stz     T_OPEN
TEND9:          rts

; A failure's detail line begun: the test's line closed with FAIL; the run's
; first detail goes on the screen's row 18 too. The caller writes the text.
TDET:
                lda     T_OPEN
                beq     @closed
                lda     #DR_FAIL
                jsr     TEND
@closed:        lda     #1
                sta     C_FAIL
                jsr     SCROFF
                bit     DG_SCR         ; no screen now (V4-V7 have card memory): the next failure's
                bpl     @ser
                lda     DG_SHOWN
                bne     @ser
                inc     DG_SHOWN
                lda     #DG_ROWDET
                sta     DG_ROW
                stz     DG_COL
                jsr     SCRON
@ser:           ldx     #9
@ind:           jsr     SPACE
                dex
                bne     @ind
                rts

; TDET with the string at A/X as its text.
TDETS:
                pha
                phx
                jsr     TDET
                plx
                pla
                jsr     PUTS
                jsr     CRLF
                jmp     SCROFF

; The test skipped, A/X why (on the line under it).
TSKIPR:
                pha
                phx
                lda     #DR_SKIP
                jsr     TEND
                plx
                pla
; A note under a test: nine blanks, the string at A/X.
NOTE9:
                pha
                phx
                jsr     SCROFF
                ldx     #9
                bra     NOTEN
; Six blanks, then the string at A/X, the serial line only (a stage's note).
NOTE6:
                pha
                phx
                jsr     SCROFF
                ldx     #6
NOTEN:
@ind:           jsr     SPACE
                dex
                bne     @ind
                plx
                pla
                jsr     PUTS
                jmp     CRLF

SIX:            jsr     SCROFF
                ldx     #6
@ind:           jsr     SPACE
                dex
                bne     @ind
                rts

; A RAM block's fault as stage 6 names it (diagcon.inc's MEMFAULT's text):
; DG_T0 the block, DG_T0+1 the offset (2 bytes), DG_MGOT, DG_MWANT. The block
; is marked failed (DG_BAD).
TRAMF:
                lda     DG_T0
                jsr     MARKBAD
                jsr     TDET
                jsr     MEMTEXT
                jmp     SCROFF

; A card memory's fault: A/X what ("CARD $"), T_N the address (3 bytes; T_N+3
; 0, 2 or 3: how many of them to write), DG_MGOT read, DG_MWANT written.
TCARDF:
                pha
                phx
                jsr     TDET
                plx
                pla
                jsr     PUTS
                ldx     T_N + 3
@addr:          lda     T_N - 1,x
                jsr     HEX2
                dex
                bne     @addr
                jsr     RWBITS
                jmp     SCROFF

; ": READ gg, WROTE ww, BITS n m", then CR, from DG_MGOT and DG_MWANT.
RWBITS:
                SAY     ": READ "
                lda     DG_MGOT
                jsr     HEX2
                SAY     ", WROTE "
                lda     DG_MWANT
                jsr     HEX2
                lda     DG_MGOT
                eor     DG_MWANT
                beq     @end
                sta     T_N + 3
                SAY     ", BITS"
                ldx     #0
@bit:           lsr     T_N + 3
                bcc     @nob
                jsr     SPACE
                txa
                ora     #'0'
                jsr     PUTC
@nob:           inx
                cpx     #8
                bne     @bit
@end:           jmp     CRLF

; T_N (4 bytes) in decimal, no commas.
DEC32:
                ldx     #3
@cp:            lda     T_N,x
                sta     HW_NUM,x
                dex
                bpl     @cp
                clc
                jmp     HW_DEC

; Carry clear if T_N (4 bytes) is within the table's tolerance of its value:
; A/X a table of the value (4 bytes) and the tolerance (2).
CLOSE:
                sta     DG_PTR
                stx     DG_PTR + 1
                ldy     #0
                sec
.repeat 4, i
                lda     T_N + i
                sbc     (DG_PTR),y
                sta     T_D + i
                iny
.endrepeat
                lda     T_D + 3        ; |d|
                bpl     @pos
                sec
.repeat 4, i
                lda     #0
                sbc     T_D + i
                sta     T_D + i
.endrepeat
@pos:           lda     T_D + 3
                ora     T_D + 2
                bne     @far
                lda     (DG_PTR),y     ; the tolerance less |d|: a borrow is too far
                sec
                sbc     T_D
                iny
                lda     (DG_PTR),y
                sbc     T_D + 1
                bcc     @far
                clc
                rts
@far:           sec
                rts

; Timer 2's count of PHI2 cycles (the VIA's, one-shot as ACR 0 leaves it,
; counting on past 0): TZERO starts it, TTICK adds what it went down by
; since to T_ACC (4 bytes). TTICK must come at least every 65,536 cycles.
TZERO:
                lda     #$FF
                sta     VIA_T2CL
                sta     VIA_T2CH
                jsr     T2NOW
                sta     T_PREV
                stx     T_PREV + 1
                stz     T_ACC
                stz     T_ACC + 1
                stz     T_ACC + 2
                stz     T_ACC + 3
                rts
TTICK:
                phy
                jsr     T2NOW
                sta     T_D
                stx     T_D + 1
                sec
                lda     T_PREV
                sbc     T_D
                tay                    ; gone down by: Y low, X high
                lda     T_PREV + 1
                sbc     T_D + 1
                tax
                lda     T_D
                sta     T_PREV
                lda     T_D + 1
                sta     T_PREV + 1
                clc
                tya
                adc     T_ACC
                sta     T_ACC
                txa
                adc     T_ACC + 1
                sta     T_ACC + 1
                bcc     @r
                inc     T_ACC + 2
                bne     @r
                inc     T_ACC + 3
@r:             ply
                rts
; Timer 2 now: A low, X high, one moment's.
T2NOW:
                ldx     VIA_T2CH
                lda     VIA_T2CL
                cpx     VIA_T2CH
                bne     T2NOW
                rts

; A the block, X the page in it: T_PA its physical address (3 bytes).
PHYSB:
                stx     T_PA + 1
                pha
                asl     a
                asl     a
                asl     a
                asl     a
                asl     a
                asl     a
                ora     T_PA + 1
                sta     T_PA + 1
                pla
                pha
                lsr     a
                lsr     a
                sta     T_PA + 2
                stz     T_PA
                pla
                rts

; The next RAM block after A ($FF to start) that the memory stages found and
; tested: in A, carry clear; carry set after the last. GOODBLK passes over
; the blocks that failed this pass too; RAMBLK doesn't.
GOODBLK:
                ldx     #1
                bra     NEXTB
RAMBLK:
                ldx     #0
NEXTB:
                stx     T_GOOD
@inc:           inc     a
@chk:           cmp     #64
                bcs     @end
                ldx     DG_APER
                beq     @mod
                cmp     #32
                bcc     @mod
                cmp     #36
                bcc     @inc
@mod:           pha
                lsr     a
                lsr     a
                lsr     a
                lsr     a
                tax
                lda     MODBITS,x
                and     DG_MODS
                beq     @no
                pla
                jsr     BLKBIT         ; Y the block
                and     SKIPS,x
                bne     @pass
                lda     T_GOOD
                beq     @take
                tya
                jsr     BLKBIT
                and     DG_BAD,x
                bne     @pass
@take:          tya
                clc
@end:           rts
@no:            pla
                bra     @inc
@pass:          tya
                bra     @inc

; Block A's bit: X its byte (A / 8), A its bit's mask; Y the block.
BLKBIT:
                tay
                lsr     a
                lsr     a
                lsr     a
                tax
                tya
                and     #7
                phx
                tax
                lda     BITS8,x
                plx
                rts

; Block A failed this pass (DG_BAD). A, Y kept.
MARKBAD:
                phy
                pha
                jsr     BLKBIT
                ora     DG_BAD,x
                sta     DG_BAD,x
                pla
                ply
                rts

; Stage 14's counts, on the screen's row 19 and the serial line.
SUMMARY:
                lda     #DG_ROWRES - 1
                jsr     ATROW
                SAY     "TESTS: "
                lda     DG_TALLY
                jsr     PUTDEC
                SAY     " PASSED, "
                lda     DG_TALLY + 1
                jsr     PUTDEC
                SAY     " FAILED, "
                lda     DG_TALLY + 2
                jsr     PUTDEC
                SAY     " SKIPPED, "
                lda     DG_TALLY + 3
                jsr     PUTDEC
                SAY     " MEASURED"
                jsr     CRLF
                jmp     SCROFF

; A key, waited for: carry set and A the key (DO_ASK's questions); about 30 s
; with none is carry clear.
KEYWAIT:
                jsr     TZERO
@w:             jsr     KEYGET
                bcs     @got
                jsr     TTICK
                lda     T_ACC + 3      ; 13 x 2^24 cycles: about 30 s
                cmp     #13
                bcc     @w
                clc
@got:           rts

; A question's answer: Y (or y) PASS, anything else FAIL, nothing in 30 s FAIL.
YESNO:
                SAY     " (Y OR N) "
                jsr     KEYWAIT
                bcc     @no
                and     #$DF
                cmp     #'Y'
                bne     @no
                jmp     TPASS
@no:            TD      "ANSWERED NO"
                rts

MODBITS:        .byte   $01, $02, $04, $08
BITS8:          .byte   $01, $02, $04, $08, $10, $20, $40, $80

HW_PUTC         = PUTC                 ; oohw's output (oohw.inc): this block's own


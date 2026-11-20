; The palette's writes, in block P3 (hw/oodos.md, "The plan, phase 7": 7e5,
; U1; the space plan's reserve (a), planned from the start by the owner's
; answer 8 to the 7e5 plan): the OEM's, the project's own, moved from P5's
; p5/gfx.s for P5's room. They are function 83's card-to-card copies and
; writes (no W1: nothing of OOLIB's), so P5 far calls them with the
; palette's card page in the pseudo-registers, and the mode's colours stay
; P5's (GGRP, PALOFS). Named after p3/trptst.s, so P3's other entries keep
; their places in the jump table.
;
;   VPAL     A the operation, RCL/RCH a palette's card page (4 pages, 256
;            entries of 2 bytes):
;            0  entry 0 the console's entry RDL (a CGA colour, 0-15): the
;               backdrop behind every transparent pixel (BACKG)
;            1  GW-BASIC's palette's first use (7e1's answer 10): the
;               console's 512 bytes copied in, then its groups' entries
;               (17-19 SCREEN 1's palette 0: the console's 2, 4 and 6; 33-35
;               palette 1: 3, 5 and 7; 49 SCREEN 2's: 15) from its CGA ones
;   VCOLOR   COLOR's graphics part, from P5's GCOLOR: A the parameters'
;            count, RAL the mode (P5's GMODEV), RDL the background (RDH 0:
;            not given), RBL the palette (RBH 0: not given), RCL/RCH the
;            mode's palette. SCREEN 1 (the PC's COLOR [background]
;            [,palette]: two at most, none changes nothing, the IBM
;            manual's pp. 43-44): the background's low 4 bits to entry 0
;            (VPAL 0's); A the palette's group, 1 for an even one, 2 for an
;            odd (0: none given). Carry set (Illegal function call) for a
;            third parameter and in SCREEN 2

.include "gwbasic.inc"

;@entry VPAL
;@entry VCOLOR
.export VPAL, VCOLOR

.code

VPAL:           TAY
                LDA     RCL            ; the palette's page
                STA     VPG
                LDA     RCH
                STA     VPG + 1
                TYA
                BNE     VINIT
                LDA     RDL
                ; (on into VBACK)

; VBACK: entry 0 the console's entry A (0-15).
VBACK:          ASL     A
                LDX     #0             ; to entry 0, 2 bytes
                LDY     #2
                ; (on into VCON)

; VCON: Y bytes (1-255) from the console's palette at its byte A (card
; memory's first page) to entry X of the palette on.
VCON:           STA     VB + 1
                STZ     VB + 2
                STZ     VB + 3
                STY     VB + 7
                STZ     VB + 8
VCPY:           JSR     VDST
                LDA     #DM_VCOPY
VGO:            STA     VB
                LDA     #<VB
                LDX     #>VB
                LDY     #O_DMA
                JMP     BDOS

VINIT:          LDA     #0             ; the console's 512 bytes
                STA     VB + 1
                STA     VB + 2
                STA     VB + 3
                STA     VB + 7
                LDA     #>512
                STA     VB + 8
                LDX     #0             ; to entry 0
                JSR     VCPY
                LDX     #VGRPN - 1     ; its groups' entries, from its CGA entries
VG:             PHX
                LDA     PALDST,X
                PHA
                LDA     PALSRC,X
                ASL     A
                PLX
                LDY     #2
                JSR     VCON
                PLX
                DEX
                BPL     VG
                RTS

; VDST: VB's destination the palette's entry X (its card page x 256 + 2X).
VDST:           TXA
                ASL     A
                STA     VB + 4
                LDA     #0
                ROL     A
                ADC     VPG
                STA     VB + 5
                LDA     VPG + 1
                ADC     #0
                STA     VB + 6
                RTS

VCOLOR:         LDX     RAL
                CPX     #1             ; SCREEN 1's alone (7e1)
                BNE     @fc
                CMP     #3             ; a third parameter: the manual's form has two
                BCS     @fc
                LDA     RCL            ; the palette's page
                STA     VPG
                LDA     RCH
                STA     VPG + 1
                LDA     RDH
                BEQ     @pal
                LDA     RDL
                AND     #$0F
                JSR     VBACK
@pal:           LDA     RBH            ; even: group 1; odd: 2; none: 0
                BEQ     @x
                LDA     RBL
                AND     #1
                INC     A
@x:             CLC
                RTS
@fc:            SEC
                RTS

.segment "RODATA"
; GW-BASIC's palette's groups: the console's CGA entries (PALSRC) to theirs.
PALSRC:         .byte   2, 4, 6, 3, 5, 7, 15
PALDST:         .byte   17, 18, 19, 33, 34, 35, 49
VGRPN           = * - PALDST

.segment "BRAM"
VPG:            .res    2              ; the palette's card page
VB:             .res    9              ; function 83's block

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
;            2  (U3) SCREEN 10's own palette's first use (the owner's answer
;               2 to the 7e5 plan): entries 0-15 the console's CGA colours,
;               16-255 the OO-1's 6 x 8 x 5 set, entry 16 + 40r + 5g + b with
;               r 0-5, g 0-7 and b 0-4 each spread over the levels 0-15
;               ((v x 15 + n / 2) / n, n the top of its range): 16 black, 255
;               white. Fixed: there is no PALETTE (dropped, the IBM spec's G13)
;   VCOLOR   COLOR's graphics part, from P5's GCOLOR: A the parameters'
;            count, RAL the mode (P5's GMODEV), RDL the background (RDH 0:
;            not given), RBL the palette (RBH 0: not given), RCL/RCH the
;            mode's palette. SCREEN 1 (the PC's COLOR [background]
;            [,palette]: two at most, none changes nothing, the IBM
;            manual's pp. 43-44): the background's low 4 bits to entry 0
;            (VPAL 0's); A the palette's group, 1 for an even one, 2 for an
;            odd (0: none given). Carry set (Illegal function call) for a
;            third parameter and in SCREEN 2. 7e5: SCREEN 10 and 11 COLOR
;            [background] (the owner's answer 3), a second parameter Illegal
;            function call; A 0; SCREEN 12 as SCREEN 2

.include "gwbasic.inc"

;@entry VPAL
;@entry VCOLOR
.export VPAL, VCOLOR
.import BUF

.code

VPAL:           TAY
                LDA     RCL            ; the palette's page
                STA     VPG
                LDA     RCH
                STA     VPG + 1
                TYA
                BNE     VINIT
VBK:            LDA     RDL
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

VINIT:          CPY     #2
                BEQ     V10
                LDA     #0             ; the console's 512 bytes
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

; V10 (7e5, U3): SCREEN 10's own palette. Entries 0-15 the console's, then
; for each r its 40 entries made in BUF (the line buffer, free while a
; statement runs) and written by DM_VWRITE, 80 bytes, from its physical
; address (function 73).
V10:            LDA     #0             ; the console's 16
                LDX     #0
                LDY     #32
                JSR     VCON
                LDA     #<BUF          ; BUF's physical address: the source
                STA     VB + 4
                LDA     #>BUF
                STA     VB + 5
                LDA     #<(VB + 4)
                LDX     #>(VB + 4)
                LDY     #O_PHYSADDR
                JSR     BDOS
                LDA     VB + 6         ; the paragraph x 16 + the offset
                STA     VB + 1
                LDA     VB + 7
                STA     VB + 2
                STZ     VB + 3
                LDX     #4
@x16:           ASL     VB + 1
                ROL     VB + 2
                ROL     VB + 3
                DEX
                BNE     @x16
                LDA     VB + 1
                ORA     VB + 4
                STA     VB + 1
                LDA     #80
                STA     VB + 7
                STZ     VB + 8
                STZ     T2             ; r
                LDA     #16            ; its first entry
                STA     T3
@r:             LDY     #0
                STZ     T0             ; g
@g:             STZ     T1             ; b
@b:             LDX     T0
                LDA     LEVG,X         ; green high, blue low; then red
                LDX     T1
                ORA     LEVB,X
                STA     BUF,Y
                INY
                LDX     T2
                LDA     LEVR,X
                STA     BUF,Y
                INY
                INC     T1
                LDA     T1
                CMP     #5
                BCC     @b
                INC     T0
                LDA     T0
                CMP     #8
                BCC     @g
                LDX     T3
                JSR     VDST
                LDA     #DM_VWRITE
                JSR     VGO
                CLC
                LDA     T3
                ADC     #40
                STA     T3
                INC     T2
                LDA     T2
                CMP     #6
                BCC     @r
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
                LDY     #3             ; SCREEN 1: two parameters at most (7e1)
                CPX     #1
                BEQ     @ok
                DEY                    ; 7e5: SCREEN 10 and 11, one
                CPX     #3
                BCC     @fc            ; SCREEN 2
                CPX     #5
                BCS     @fc            ; SCREEN 12
@ok:            STY     T0
                CMP     T0             ; one too many: the manual's form has two
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
; 7e5: SCREEN 10's levels, (v x 15 + n / 2) / n: red (n 5) in byte 1's low
; bits, green (n 7) in byte 0's high, blue (n 4) its low.
LEVR:           .byte   0, 3, 6, 9, 12, 15
LEVG:           .byte   $00, $20, $40, $60, $90, $B0, $D0, $F0
LEVB:           .byte   0, 4, 8, 11, 15

.segment "BRAM"
VPG:            .res    2              ; the palette's card page
VB:             .res    9              ; function 83's block

; The graphics statements' stubs, in the home block (the project's own;
; 7e1, hw/oodos.md, "The plan, phase 7"). Each far calls its routine in
; block P5 (p5/gengrp.s) in screen mode, and is Advanced Feature in console
; mode, as it was (7d's answer 7: a serial terminal has no card memory
; GW-BASIC set up). LINE's is p8/gwmain.s's (LINE INPUT's too).
;
;   PSET PRESET   GENGRP.ASM's
;   CIRCLE        ADVGRP.ASM's (7e2): P5's CIRCP, which parses it in P3
;   PAINT         ADVGRP.ASM's (7e2): P5's PAINTP
;   DRAW          ADVGRP.ASM's (7e2): P3's DRAWP (its drawing P5's DRWOP)
;   VIEW WINDOW   the OO-1's (7e4; the source's are Advanced Feature):
;                 P5's GVIEW, P1's WINSET

.include "gwbasic.inc"

.export PSET, PRESET, CIRCLE, PAINT, DRAW, VIEW, WINDOW
.import ADVERR, SCRMOD, TXSYNC, CHRGTR, SYNCHR, GETIN2, GETBYT, FCERR, FRMEVL, FRCSNG, GFLG, FCOMP

.macro  MOVW    dst, src
                LDA     src
                STA     dst
                LDA     src + 1
                STA     dst + 1
.endmacro

.code

PSET:           BIT     SCRMOD
                BPL     ADV
                FJSR    PSETP
                RTS
PRESET:         BIT     SCRMOD
                BPL     ADV
                FJSR    PRSETP
                RTS
CIRCLE:         BIT     SCRMOD
                BPL     ADV
                FJSR    CIRCP
                RTS
PAINT:          BIT     SCRMOD
                BPL     ADV
                JSR     TXSYNC         ; 7e4: TXBUF emptied, for a tile (7e3's answer 3)
                FJSR    PAINTP
                RTS
DRAW:           BIT     SCRMOD
                BPL     ADV
                FJSR    DRAWP
                RTS
ADV:            JMP     ADVERR
; VIEW (7e4; the IBM manual's pp. 357-360): VIEW [[SCREEN](x1,y1)-(x2,y2)
; [,[fill][,[border]]]], each coordinate an integer (GETIN2: rounded), the
; colours 0-255 (GETBYT); no STEP (Syntax error at the "("). VIEW SCREEN
; alone is VIEW alone, the whole screen (OO-1: the manual's brackets, the
; owner's answer 7, E6). The parse is kept in this block's RAM (a statement
; never re-enters it), then checked: a graphics mode (P5's DSINFO: GMODEV),
; each corner on the screen (0 to 319 or 639, 0 to 199; 7e5: to the mode's
; last x and y, DSINFO's RDI and RBP), x1 <> x2 and y1 <>
; y2, else Illegal function call; the corners sorted. Then one far call to
; P5's GVIEW (p5/gfx.s), which draws: A the flags (bit 7 corners, 6
; SCREEN, 5 a fill, 4 a border), RSI on the viewport's corner (2 bytes and
; 2), its last x and y from the corner (2 and 2), the fill and the border
; (T0, T1: 7e5's words);
; the border box (x1 - 1, y1 - 1)-(x2 + 1, y2 + 1) in RCL/RCH, RDL/RDH,
; RBL/RBH, RAL/RAH.
VIEW:           BIT     SCRMOD
                BPL     ADV
                STZ     VWF
                JSR     VEND
                BEQ     @call          ; VIEW alone
                LDX     #$80
                CMP     #SCREEN_TK
                BNE     :+
                JSR     CHRGTR
                BEQ     @call          ; VIEW SCREEN alone
                LDX     #$C0
:               STX     VWF
                JSR     VPAIR          ; (x1,y1)
                MOVW    VX1, VX2
                MOVW    VY1, VY2
                JSR     SYNCHR
                .byte   MINUTK
                JSR     VPAIR          ; (x2,y2)
                JSR     VEND
                BEQ     @call
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     @nf            ; no fill
                JSR     GETBYT
                STA     VFIL
                LDA     #$20
                TSB     VWF
                JSR     VEND
                BEQ     @call
@nf:            JSR     SYNCHR
                .byte   ','
                JSR     GETBYT
                STA     VBRD
                LDA     #$10
                TSB     VWF
@call:          FJSR    DSINFO         ; A: the mode
                TAX
                BNE     :+
                JMP     FCERR          ; SCREEN 0
:               BIT     VWF
                BMI     :+
                JMP     @go            ; no corners
:
                MOVW    VLIM, RDI      ; x: 0 to the mode's last (7e5: DSINFO's)
                LDX     #VX1 - VX1
                JSR     VCHK
                LDX     #VX2 - VX1
                JSR     VCHK
                MOVW    VLIM, RBP      ; y: 0 to the mode's last
                LDX     #VY1 - VX1
                JSR     VCHK
                LDX     #VY2 - VX1
                JSR     VCHK
                LDX     #VX1 - VX1     ; sorted, each pair unequal
                JSR     VSORT
                LDX     #VY1 - VX1
                JSR     VSORT
                MOVW    RSI, VX1       ; the viewport: the corner, the last x and y
                MOVW    RSI + 2, VY1
                SEC
                LDA     VX2
                SBC     VX1
                STA     RSI + 4
                LDA     VX2 + 1
                SBC     VX1 + 1
                STA     RSI + 5
                SEC
                LDA     VY2
                SBC     VY1
                STA     RSI + 6
                LDA     VY2 + 1
                SBC     VY1 + 1
                STA     RSI + 7
                LDA     VFIL
                STA     RSI + 8
                LDA     VBRD
                STA     RSI + 9
                SEC                    ; the border box
                LDA     VX1
                SBC     #1
                STA     RCL
                LDA     VX1 + 1
                SBC     #0
                STA     RCH
                SEC
                LDA     VY1
                SBC     #1
                STA     RDL
                LDA     VY1 + 1
                SBC     #0
                STA     RDH
                CLC
                LDA     VX2
                ADC     #1
                STA     RBL
                LDA     VX2 + 1
                ADC     #0
                STA     RBH
                CLC
                LDA     VY2
                ADC     #1
                STA     RAL
                LDA     VY2 + 1
                ADC     #0
                STA     RAH
@go:            LDA     VWF
                FJSR    GVIEW
                RTS
; VCHK: the corner word at VX1 + X no more than VLIM (unsigned: a negative
; one is past it), else Illegal function call.
VCHK:           LDA     VLIM
                CMP     VX1,X
                LDA     VLIM + 1
                SBC     VX1 + 1,X
                BCC     VFC
                RTS
; VSORT: the pair at VX1 + X and VX2 + X sorted; equal, Illegal function call.
VSORT:          LDA     VX2,X
                CMP     VX1,X
                LDA     VX2 + 1,X
                SBC     VX1 + 1,X
                BCS     @s
                LDY     #2             ; swapped
@w:             LDA     VX1,X
                PHA
                LDA     VX2,X
                STA     VX1,X
                PLA
                STA     VX2,X
                INX
                DEY
                BNE     @w
                RTS
@s:             LDA     VX2,X
                CMP     VX1,X
                BNE     @x
                LDA     VX2 + 1,X
                CMP     VX1 + 1,X
                BEQ     VFC
@x:             RTS
VFC:            JMP     FCERR
; VPAIR: "(x,y)": VX2 x, VY2 y (the last pair's; the first copied by the
; caller). VEND: Z at the statement's end, A the character.
VPAIR:          JSR     SYNCHR
                .byte   '('
                JSR     GETIN2
                MOVW    VX2, RDL
                JSR     SYNCHR
                .byte   ','
                JSR     GETIN2
                MOVW    VY2, RDL
                JSR     SYNCHR
                .byte   ')'
VEND:           DECW    TXTPTR
                JMP     CHRGTR
; WINDOW (7e4; the IBM manual's pp. 370-374): WINDOW [[SCREEN](x1,y1)-
; (x2,y2)], each coordinate a single (FRMEVL, FRCSNG), kept in this block's
; RAM; then one far call to P1's WINSET (p1/window.s), which checks the
; mode and the corners: A the flags (bit 7 corners, 6 SCREEN), x1, y1, x2
; and y2 in the pseudo-registers, 4 bytes each from RDL.
WINDOW:         BIT     SCRMOD
                BMI     :+
                JMP     ADVERR
:               STZ     VWF
                JSR     VEND
                BEQ     @call          ; WINDOW alone
                LDX     #$80
                CMP     #SCREEN_TK
                BNE     :+
                JSR     CHRGTR
                LDX     #$C0
:               STX     VWF
                JSR     SYNCHR
                .byte   '('
                LDY     #0
                JSR     WSNG           ; x1
                JSR     SYNCHR
                .byte   ','
                LDY     #4
                JSR     WSNG           ; y1
                JSR     SYNCHR
                .byte   ')'
                JSR     SYNCHR
                .byte   MINUTK
                JSR     SYNCHR
                .byte   '('
                LDY     #8
                JSR     WSNG           ; x2
                JSR     SYNCHR
                .byte   ','
                LDY     #12
                JSR     WSNG           ; y2
                JSR     SYNCHR
                .byte   ')'
                JSR     WMODE          ; (T1: P1's checks, here)
                LDX     #0
                JSR     WSRT           ; x
                LDX     #4
                JSR     WSRT           ; y
                LDX     #15
@c:             LDA     WCRN,X
                STA     RDL,X
                DEX
                BPL     @c
                BRA     @go
@call:          JSR     WMODE
@go:            LDA     VWF
                FJSR    WINSET
                RTS
WMODE:          LDA     GFLG           ; not a graphics mode
                LSR     A
                BCS     :+
                JMP     FCERR
:               RTS
; WSRT: WCRN + X and WCRN + 8 + X sorted; equal, Illegal function call.
WSRT:           PHX
                TXA
                TAY
                LDX     #0
@l:             LDA     WCRN,Y
                STA     RDL,X
                LDA     WCRN + 8,Y
                STA     FACLO,X
                INX
                INY
                CPX     #4
                BNE     @l
                LDA     #VT_SNG
                STA     VALTYP
                JSR     FCOMP          ; 1: the second the greater
                PLX
                CMP     #0
                BNE     :+
                JMP     FCERR
:               BPL     @x
                LDY     #4
@w:             LDA     WCRN,X
                PHA
                LDA     WCRN + 8,X
                STA     WCRN,X
                PLA
                STA     WCRN + 8,X
                INX
                DEY
                BNE     @w
@x:             RTS
; WSNG: a single, to WCRN + Y.
WSNG:           PHY
                JSR     FRMEVL
                JSR     FRCSNG
                PLY
                LDX     #0
@s:             LDA     FACLO,X
                STA     WCRN,Y
                INY
                INX
                CPX     #4
                BCC     @s
                RTS

.segment "BRAM"
VWF:            .res    1              ; VIEW's flags (GVIEW's A)
VX1:            .res    2              ;   its corners: x1, y1 (VSORT's: VX2 4 on)
VY1:            .res    2
VX2:            .res    2
VY2:            .res    2
.assert VX2 - VX1 = 4 && VY2 - VY1 = 4, error, "VIEW's corners: VSORT's order"
.assert RSI + 9 = T1, error, "GVIEW's viewport, the fill and the border: RSI on to T1"
VFIL:           .res    1              ;   the fill
VBRD:           .res    1              ;   the border
VLIM:           .res    2              ;   VCHK's limit
WCRN:           .res    16             ; WINDOW's corners, x1, y1, x2, y2

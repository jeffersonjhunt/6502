; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GENGRP.ASM, the generalised graphics, translated, in block P5 (hw/oodos.md,
; "The plan, phase 7": 7e1, U3), with GWSTS.ASM's LINLP3, the line's inner
; loop: the statements on the OEM's pixel routines (p5/gfxoem.s, p5/gfx.s).
;
;   PSETP     PSET [STEP](x,y)[,attribute]: the attribute the foreground
;             if not given
;   PRSETP    PRESET: the same, the background if not given
;   POINTP    POINT(x,y) in a formula: the pixel's attribute, -1 off the
;             screen; the graphics cursor kept
;   GLINEP    LINE [[STEP](x1,y1)]-[STEP](x2,y2)[,[attribute][,B[F]]]
;   GRPRST    CLEARC's graphics reset (the resident CLEARC, screen mode)
;   GRPINI    the graphics cursor centred (P6's SCNCLR's, a graphics mode)
;
; Each is reached from its home block stub (p8/far5.s, p8/gwmain.s's LINE)
; or the resident evaluator (POINT) in screen mode only; in SCREEN 0 they
; are Illegal function call (PIXSIZ 0), after their parameters are parsed,
; as the source's ATRFI2 has it.
;
; The 8086's registers: CX (x) is RCL/RCH, DX (y) RDL/RDH, BX (a delta or a
; count) RBL/RBH, as the pseudo-registers are named (gwbasic.inc); BX the
; text pointer is TXTPTR. What the source pushes across a formula (a
; coordinate, the default attribute) goes on the 6502's stack, so a POINT
; inside a coordinate's formula (POINT re-enters this block) keeps it; the
; state the source keeps in its data segment (GRPACX, GXPOS, MAXDEL, ...)
; is P5's RAM, as POINT saves and restores it. OO-1: the drawing (MAPXYC on)
; maps the aperture into W1, and each statement puts block 1 back
; (GX_W1BACK) before it returns; nothing after MAPXYC can end in an error.
; DRAW's scale and angle, which GRPRST clears, are p5/advgrp.s's (7e2).

.include "gwbasic.inc"
.include "p5/p5.inc"
; OOLIB's (oolib.inc, vmem.inc: not included here, since gwbasic.inc defines
; GXP, gxmem's pointer, which only p5/seed.s exports to it)
.import GX_W1BACK

.export PSETP, PRSETP, POINTP, GLINEP, GRPRST, GRPINI, GCELL
.import VWRSTW
.import CHRGTR, CHRGT2, SYNCHR, GETIN2, GETBYT, FCERR, SNERR, MAKINT
.import FRMEVL, FRCSNG, INTFR2, CONINT, GFLG, OVERR

; Push and pull a word of zero page.
.macro  PSHW    zp
                LDA     zp + 1
                PHA
                LDA     zp
                PHA
.endmacro
.macro  PULW    zp
                PLA
                STA     zp
                PLA
                STA     zp + 1
.endmacro
.macro  MOVW    dst, src
                LDA     src
                STA     dst
                LDA     src + 1
                STA     dst + 1
.endmacro

.code

; --- SCAN1, SCAND (GENGRP.ASM 89) -------------------------------------------------------------

; SCAN1: the first point of two: "@" passed over; none (a "-" next) the
; graphics cursor. SCAND: (x,y) or STEP(x,y), STEP's relative to the
; cursor. Both: the cursor (GRPACX, GRPACY) and GXPOS, GYPOS made the point,
; CX x and DX y. 7e4: SCANDX takes it on from the first formula evaluated
; (POINTP's way in), Z relative; under a WINDOW (GFLG bit 7) x and y are
; singles, mapped by P1's WINXY to the viewport's point (STEP in world
; units from the cursor mapped back); else integers, an absolute point's
; VIEW SCREEN offset taken off (VSOFF).
SCAN1:          LDA     (TXTPTR)
                CMP     #'@'
                BNE     :+
                JSR     CHRGTR
:               STZ     RCL            ; no coordinates: the cursor (-0)
                STZ     RCH
                STZ     RDL
                STZ     RDH
                CMP     #MINUTK
                BEQ     SCANN          ; (Z: relative)
SCAND:          LDA     (TXTPTR)
                CMP     #STEP_TK
                PHP                    ; Z: relative
                BNE     :+
                JSR     CHRGTR
:               JSR     SYNCHR
                .byte   '('
                JSR     FRMEVL         ; x
                PLP
SCANDX:         PHP
                BIT     GFLG
                BMI     SCANW          ; a window
                JSR     INTFR2
                PSHW    RDL
                JSR     SYNCHR
                .byte   ','
                JSR     GETIN2         ; y
                JSR     SYNCHR
                .byte   ')'
                PULW    RCL
                PLP
SCANN:          BNE     @abs
                CLC                    ; relative: the cursor added
                LDA     GRPACX
                ADC     RCL
                STA     RCL
                LDA     GRPACX + 1
                ADC     RCH
                STA     RCH
                CLC
                LDA     GRPACY
                ADC     RDL
                STA     RDL
                LDA     GRPACY + 1
                ADC     RDH
                STA     RDH
                BRA     SCANS
@abs:           JSR     VSOFF          ; 7e4: absolute, VIEW SCREEN's offset off
SCANS:          MOVW    GRPACX, RCL
                MOVW    GXPOS, RCL
                MOVW    GRPACY, RDL
                MOVW    GYPOS, RDL
                RTS
SCANW:          JSR     FRCSNG         ; x, on the 6502's stack across y
                LDX     #3
@p:             LDA     FACLO,X
                PHA
                DEX
                BPL     @p
                JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL         ; y
                JSR     FRCSNG
                JSR     SYNCHR
                .byte   ')'
                LDX     #0             ; x in BXDX
@q:             PLA
                STA     RDL,X
                INX
                CPX     #4
                BCC     @q
                PLP
                BNE     @a
                LDA     #$80           ; STEP
                BRA     @g
@a:             LDA     #0
@g:             PHA
                MOVW    RCL, GRPACX
                MOVW    RSI, GRPACY
                PLA
                FJSR    WINXY
                BRA     SCANS

; VSOFF (OO-1, 7e4): CX, DX an absolute point made the viewport's: VIEW
; SCREEN's offset (VWSX, VWSY; 0 under VIEW, and with none) taken off. Past
; the integer range Overflow (review round 1, F3: the owner's answer 16).
; Only from a statement's parse, before anything is drawn.
VSOFF:          SEC
                LDA     RCL
                SBC     VWSX
                STA     RCL
                LDA     RCH
                SBC     VWSX + 1
                STA     RCH
                BVS     VSOV
                SEC
                LDA     RDL
                SBC     VWSY
                STA     RDL
                LDA     RDH
                SBC     VWSY + 1       ; (7e5: a word)
                STA     RDH
                BVS     VSOV
                RTS
VSOV:           JMP     OVERR

; --- PSET, PRESET, POINT (GENGRP.ASM 155) -----------------------------------------------------

PRSETP:         SEC
                JSR     GETFBC
                TXA                    ; the background
                BRA     PSETC
PSETP:          SEC
                JSR     GETFBC         ; the foreground
PSETC:          PHA
                JSR     SCAND
                PLA
                JSR     ATRENT
                JSR     SCALXY
                BCC     PSTNOT         ; not on the screen: no PSET
                JSR     MAPXYC
                JSR     SETC
                JMP     GX_W1BACK      ; (OO-1)
PSTNOT:         RTS

; POINTP: TXTPTR at POINT's token in a formula. The cursor (C), GXPOS,
; GYPOS, GRPACY and GRPACX kept on the 6502's stack across it, so that
; LINE (x1,y1)-(x2,y2),POINT(x3,y3) draws as it reads. OO-1 (the IBM
; manual, p. 258: the coordinates absolute): POINT STEP, which the source's
; SCAND takes, is Syntax error. 7e4 (pp. 258-260): POINT(n), n 0-3 (rounded;
; else Illegal function call): 0 and 1 the cursor's x and y (VIEW SCREEN's
; offset added: absolute there), 2 and 3 the same under a WINDOW mapped back
; to world coordinates (P1's WPNT), else as 0 and 1; and (x,y) -1 off the
; viewport (the owner's answer 4). SCREEN 0 Illegal function call first.
POINTP:         JSR     CHRGTR
                CMP     #STEP_TK
                BNE     :+
                JMP     SNERR
:               JSR     PIXSIZ         ; OO-1: SCREEN 0 has no pixels (PC-BASIC's)
                BNE     :+
                JMP     FCERR
:               JSR     FETCHC
                LDX     #CSIZE - 1     ; C: CSAV and CSAVP
@c:             LDA     CSAV,X
                PHA
                DEX
                BPL     @c
                PSHW    GYPOS
                PSHW    GXPOS
                PSHW    GRPACY
                PSHW    GRPACX
                JSR     SYNCHR
                .byte   '('
                JSR     FRMEVL
                LDA     (TXTPTR)
                CMP     #')'
                BEQ     PNTN           ; POINT(n)
                LDA     #1             ; (Z clear: absolute)
                JSR     SCANDX
                JSR     SCALXY
                LDA     #$FF           ; -1: not on the screen
                STA     RBL
                STA     RBH
                BCC     PNTNOT
                JSR     MAPXYC
                JSR     READC
                STA     RBL
                STZ     RBH
                JSR     GX_W1BACK      ; (OO-1)
PNTNOT:         JSR     MAKINT
PNTRST:         PULW    GRPACX
                PULW    GRPACY
                PULW    GXPOS
                PULW    GYPOS
                LDX     #0
@r:             PLA
                STA     CSAV,X
                INX
                CPX     #CSIZE
                BCC     @r
                JSR     STOREC
                DECW    TXTPTR         ; the flags for what follows ")"
                JMP     CHRGTR
PNTN:           JSR     CONINT         ; n
                CMP     #4
                BCC     :+
                JMP     FCERR
:               PHA
                JSR     SYNCHR
                .byte   ')'
                PLA
                LSR     A              ; C: y; A: 1 for 2 and 3
                PHP
                BEQ     @p             ; 0, 1
                BIT     GFLG
                BPL     @p             ; no window
                PLP                    ; the cursor's axis mapped back
                BCS     @wy
                MOVW    RCL, GRPACX
                LDA     #0
                BRA     @w
@wy:            MOVW    RCL, GRPACY
                LDA     #1
@w:             FJSR    WPNT
                JMP     PNTRST
@p:             PLP                    ; the cursor, VIEW SCREEN's offset added
                BCS     @y
                CLC
                LDA     GRPACX
                ADC     VWSX
                STA     RBL
                LDA     GRPACX + 1
                ADC     VWSX + 1
                STA     RBH
                JMP     PNTNOT
@y:             CLC
                LDA     GRPACY
                ADC     VWSY
                STA     RBL
                LDA     GRPACY + 1
                ADC     VWSY + 1
                STA     RBH
                JMP     PNTNOT

; GCELL (OO-1, P6's SCREEN( in a graphics mode: the IBM manual's p. 311):
; C set if any of the 8 x 8 pixels under the text cell at row RDL, column
; RCL (from 0) isn't 0. The graphics cursor kept. 7e4: the cell's pixels
; the screen's, whatever the viewport (MAPABS). 7e5: the mode's cell (8 x 8
; in SCREEN 1, 2 and 10, 8 x 16 in 11 and 12), its column less the columns
; beside the bitmap (4 in SCREEN 10, 8 in 11): a cell beside it has no
; pixels (C clear, a blank's a blank: the owner's answer 11).
GCELL:          JSR     FETCHC
                LDX     GMODEV
                SEC
                LDA     RCL
                SBC     GCOT - 1,X     ; the column over the picture
                CMP     GCWT - 1,X
                BCS     @none          ; beside it (or before: unsigned)
                STA     RCL
                LDA     GCHT - 1,X     ; the cell's rows
                STA     GCR
                STZ     RCH
                STZ     RDH
                LDX     #3
@m:             ASL     RDL            ; x 8: the cell's first pixel
                ROL     RDH
                ASL     RCL
                ROL     RCH
                DEX
                BNE     @m
                LDA     GCR            ; 16 rows: y x 16
                CMP     #16
                BCC     @r
                ASL     RDL
                ROL     RDH
@r:             JSR     MAPABS
                LDA     #8
                STA     GCN
@p:             JSR     READC
                TAX
                BNE     @x
                JSR     RIGHTC
                DEC     GCN
                BNE     @p
                INCW    RDL            ; (7e5: y a word)
                DEC     GCR
                BNE     @r
@none:          LDX     #0             ; none
@x:             PHX
                JSR     GX_W1BACK
                JSR     STOREC
                PLX
                CPX     #1             ; C: a pixel not 0
                RTS

; --- ATRSCN (GENGRP.ASM 226) ------------------------------------------------------------------

; ATRSCN: an attribute, the foreground if the statement ends or it is
; null; ATRENT: A the default. CX and DX kept. It must be one the mode has
; (SETATR), and the mode one with pixels (PIXSIZ), else Illegal function
; call. Returns as CHRGT2, Z at the statement's end.
ATRSCN:         SEC
                JSR     GETFBC
ATRENT:         TAX
                PSHW    RCL
                PSHW    RDL
                STX     RDL            ; the default in DL
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     ATRFIN
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     ATRFIN         ; another comma: null, the default
                JSR     GETBYT
ATRFIN:         LDA     RDL
ATRFI2:         PHA
                JSR     PIXSIZ
                BNE     :+
                JMP     FCERR          ; no graphics
:               PLA
                JSR     SETATR
                BCC     :+
                JMP     FCERR
:               STA     ATRBYT
                PULW    RDL
                PULW    RCL
                JMP     CHRGT2

; --- XDELT, YDELT, XCHGX, XCHGY, XCHGAC (GENGRP.ASM 270) --------------------------------------

; XDELT: BX = |GXPOS - CX|, C set if CX was the greater (the 8086's borrow);
; YDELT the same of GYPOS and DX; NEGHL: BX negated, C set. OO-1: the
; greater by the difference's sign (N xor V), not the borrow, since a
; line's ends are signed and may lie anywhere (the clipping, DOGRPH): BX up
; to 65,535, unsigned. For points on the screen it is the source's borrow.
XDELT:          SEC
                LDA     GXPOS
                SBC     RCL
                STA     RBL
                LDA     GXPOS + 1
                SBC     RCH
                STA     RBH
CNEGHL:         BVC     :+
                EOR     #$80
:               BMI     NEGHL
                CLC
                RTS
NEGHL:          SEC
                LDA     #0
                SBC     RBL
                STA     RBL
                LDA     #0
                SBC     RBH
                STA     RBH
                SEC
                RTS
YDELT:          SEC
                LDA     GYPOS
                SBC     RDL
                STA     RBL
                LDA     GYPOS + 1
                SBC     RDH
                STA     RBH
                BRA     CNEGHL

; XCHGY: DX with GYPOS; XCHGX: CX with GXPOS; XCHGAC both. A and X only.
XCHGY:          LDA     RDL
                LDX     GYPOS
                STA     GYPOS
                STX     RDL
                LDA     RDH
                LDX     GYPOS + 1
                STA     GYPOS + 1
                STX     RDH
                RTS
XCHGAC:         JSR     XCHGY
XCHGX:          LDA     RCL
                LDX     GXPOS
                STA     GXPOS
                STX     RCL
                LDA     RCH
                LDX     GXPOS + 1
                STA     GXPOS + 1
                STX     RCH
                RTS

; --- LINE (GENGRP.ASM 323) --------------------------------------------------------------------

; GLINEP: TXTPTR after LINE (not INPUT: the home block's stub, p8/gwmain.s).
; 7e4 (the IBM manual's pp. 156-159): [,[attribute][,[B]][,style]], the
; style an integer (GETIN2), a 16-bit mask whose 1 bits plot (LSTYL, $FFFF
; outside a LINE statement): BF takes none (NEWSTT's Syntax error at the
; comma). The OO-1's (the owner's answer 9): the first point drawn takes
; bit 15, in the order DOGRPH draws, a B box's four sides one mask on in
; BOXLIN's order, each statement from bit 15 again; a clipped line counts
; from its given start (answer 7, E11: LSKIP).
GLINEP:         JSR     SCAN1          ; the first point
                PSHW    RCL
                PSHW    RDL
                JSR     SYNCHR
                .byte   MINUTK
                JSR     SCAND          ; the second
                STZ     GLB
                JSR     ATRSCN
                BEQ     @ln            ; the statement ended: a line
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     @st            ; ",,": a line's style
                JSR     SYNCHR
                .byte   'B'
                BEQ     @bx            ; B: the box
                CMP     #','
                BEQ     @bs            ; B,style
                JSR     SYNCHR
                .byte   'F'            ; BF: filled
                PULW    RDL
                PULW    RCL
                BRA     DOBOXF
@bs:            DEC     GLB            ; (bit 7: the box)
@st:            JSR     SYNCHR
                .byte   ','
                JSR     GETIN2         ; the style (nothing after it can fail)
                MOVW    LSTYL, RDL
                BIT     GLB
                BMI     @bx
@ln:            PULW    RDL
                PULW    RCL
                JSR     DOLINE         ; a line
                BRA     @end
@bx:            PULW    RDL
                PULW    RCL
                JSR     BOXLIN
@end:           LDA     #$FF           ; solid again
                STA     LSTYL
                STA     LSTYL + 1
                RTS
DOBOXF:         JSR     BOXOFF         ; OO-1 (review round 1, F1): a box wholly
                BCC     :+             ; past one edge draws nothing (PC-BASIC's;
                RTS                    ; the truncation would fill that edge)
:               JSR     SCALXY
                JSR     XCHGAC
                JSR     SCALXY
                JSR     YDELT          ; the lines
                BCC     :+
                JSR     XCHGY          ; DX the smallest y
:               INCW    RBL            ; a count
                MOVW    BOXN, RBL
                JSR     XDELT          ; the width
                BCC     :+
                JSR     XCHGX          ; CX the smallest x
:               INCW    RBL
                MOVW    BOXW, RBL
                JSR     MAPXYC
                MOVW    RCL, BOXN      ; CX: the lines left
BOXLOP:         JSR     FETCHC
                MOVW    RBL, BOXW
                JSR     NSETCX
                JSR     STOREC
                JSR     REMAP          ; (OO-1: NSETCX may have crossed a bank)
                JSR     DOWNC
                LDA     RCL
                BNE     :+
                DEC     RCH
:               DEC     RCL
                LDA     RCL
                ORA     RCH
                BNE     BOXLOP
                JMP     GX_W1BACK      ; (OO-1)

; BOXOFF (OO-1): C set if the box (CX, DX)-(GXPOS, GYPOS) lies wholly past
; one edge of the screen: both x below 0 or past the last, or both y below
; 0 or past 199 (7e4: the viewport's edges).
BOXOFF:         LDA     RCH
                AND     GXPOS + 1
                BMI     @yes           ; both x < 0
                LDA     RDH
                AND     GYPOS + 1
                BMI     @yes           ; both y < 0
                LDA     RCL
                LDX     RCH
                JSR     XPAST
                BCC     @y
                LDA     GXPOS
                LDX     GXPOS + 1
                JSR     XPAST
                BCS     @yes
@y:             LDA     RDL
                LDX     RDH
                JSR     YPAST
                BCC     @no
                LDA     GYPOS
                LDX     GYPOS + 1
                JSR     YPAST
                BCS     @yes
@no:            CLC
                RTS
@yes:           SEC
                RTS
; XPAST, YPAST: C set if A/X (signed) is past the last x, or past 199
; (7e4: the viewport's, VWMX and VWMY; 7e5: y a word, so one compare for
; both). Y not kept.
XPAST:          LDY     #0
                BRA     :+
YPAST:          LDY     #2             ; (VWMY - VWMX: p5/gfx.s asserts it)
:               CPX     #$80
                BCS     @n             ; negative
                STA     T0
                STX     T1
                LDA     VWMX,Y
                CMP     T0
                LDA     VWMX + 1,Y
                SBC     T1
                BCS     @n             ; the last >= x
                SEC
                RTS
@n:             CLC
                RTS

; DOLINE: the line from (CX, DX) to (GXPOS, GYPOS), which are kept: the
; cursor's second point back in GXPOS, GYPOS for BOXLIN.
DOLINE:         PSHW    RCL
                PSHW    RDL
                JSR     DOGRPH
                JSR     GX_W1BACK      ; (OO-1)
                MOVW    GXPOS, GRPACX
                MOVW    GYPOS, GRPACY
                PULW    RDL
                PULW    RCL
                RTS
; BOXLIN: the four sides, each a DOLINE.
BOXLIN:         PSHW    GYPOS          ; y2
                PSHW    RDL            ; y1
                MOVW    RDL, GYPOS     ; the top: y2 to y1
                JSR     DOLINE
                PULW    GYPOS          ; y1 to y2
                LDA     GYPOS          ; (DX: y1)
                STA     RDL
                LDA     GYPOS + 1
                STA     RDH
                JSR     DOLINE
                PULW    GYPOS          ; y2 back
                PSHW    RCL            ; x1
                MOVW    RCL, GXPOS     ; x1 = x2
                JSR     DOLINE
                PULW    GXPOS          ; x2 = x1
                MOVW    RCL, GXPOS
                JMP     DOLINE

; DOGRPH: a line from (CX, DX) to (GXPOS, GYPOS), drawn down from the
; smaller y, along its major axis (MAXUPD, a step each point) with steps on
; its minor (MINUPD) when the sum passes the major's delta. OO-1 (the IBM
; spec's fix-up, the owner's G2: the IBM manual's p. 159, out-of-range
; coordinates clipped): the ends are taken as given, not truncated to the
; edge as the source's SCALXY does (an OEM placeholder, G1), and a line with
; an end off the screen is drawn by LINCLP: the pixels of the same line, on
; its own slope, that lie on the screen.
DOGRPH:         STZ     LOFF
                JSR     ONSCR
                BCS     :+
                INC     LOFF
:               JSR     XCHGAC
                JSR     ONSCR
                BCS     DOGRP2
                INC     LOFF
DOGRP2:         JSR     YDELT
                BCC     :+
                JSR     XCHGAC         ; (CX, DX) the point with the smaller y
:               MOVW    LMAJ, RBL       ; dy
                JSR     XDELT          ; dx; C: left
                LDA     #1             ; LXS: x's step, +1 or -1 (LINCLP's)
                BCC     :+
                LDA     #$FF
:               STA     LXS
                LDX     GMX
                LDA     LXS
                CMP     #$FF           ; C: left
                LDA     RIGHTV - 2,X
                LDY     RIGHTV - 1,X
                BCC     LINCN2
                LDA     LEFTV - 2,X
                LDY     LEFTV - 1,X
LINCN2:         STA     T0             ; the x routine
                STY     T1
                LDA     LMAJ           ; dy >= dx: y the major axis
                CMP     RBL
                LDA     LMAJ + 1
                SBC     RBH
                BCS     YDLTBG
                STZ     LYM
                MOVW    MINDEL, LMAJ    ; x major: MINDEL dy
                MOVW    MAXUPD, T0
                LDA     DOWNV - 2,X
                STA     MINUPD
                LDA     DOWNV - 1,X
                STA     MINUPD + 1
                MOVW    LMAJ, RBL       ; the major delta: dx
                BRA     LINCN3
YDLTBG:         LDA     #1
                STA     LYM
                MOVW    MINUPD, T0     ; y major
                LDA     DOWNV - 2,X
                STA     MAXUPD
                LDA     DOWNV - 1,X
                STA     MAXUPD + 1
                MOVW    MINDEL, RBL    ; MINDEL dx; the major delta dy
LINCN3:         MOVW    MAXDEL, LMAJ
                LDA     LOFF           ; an end off the screen: LINCLP
                BEQ     :+
                JMP     LINCLP
:               JSR     MAPXYC
                MOVW    RDL, LMAJ       ; the sum: half the major delta
                JSR     HLFDE
                MOVW    RCL, LMAJ       ; the points: the major delta + 1
                INCW    RCL
                ; (on into LINLP3)

; LINLP3 (GWSTS.ASM 486): the line's inner loop. DX the sum, CX the points.
; 7e4: each point turns LINE's style (LSTYL) a bit left, the bit out of
; bit 15 back into bit 0, and is set only for a 1 (STYSET).
LINLP3:         ASL     LSTYL
                ROL     LSTYL + 1
                BCC     :+
                INC     LSTYL
                JSR     SETC
:
                CLC
                LDA     RDL
                ADC     MINDEL
                STA     RDL
                LDA     RDH
                ADC     MINDEL + 1
                STA     RDH
                LDA     RDL            ; time to step the minor axis?
                CMP     MAXDEL
                LDA     RDH
                SBC     MAXDEL + 1
                BCC     LINLOP
                STA     RDH            ; the sum less the major delta
                LDA     RDL
                SBC     MAXDEL
                STA     RDL
                JSR     MINJMP
LINLOP:         JSR     MAXJMP
                LDA     RCL
                BNE     :+
                DEC     RCH
:               DEC     RCL
                LDA     RCL
                ORA     RCH
                BNE     LINLP3
                RTS
MINJMP:         JMP     (MINUPD)
MAXJMP:         JMP     (MAXUPD)

; LINCLP (OO-1, the IBM spec's fix-up, G2): LINLP3's line, its sum and
; steps the same, from (CX, DX) with an end off the screen: its point (LX,
; LY) followed, the cursor mapped (MAPXYC) at the first that is on the
; screen and moved with it from there, each on the screen set, and the line
; left at the first off it after (they are one run: the line is straight
; and the screen convex). LSKIP first brings the point at once to where its
; major coordinate meets the screen, so a line from far off costs no more
; than one on it; the walk then ends where the major coordinate leaves the
; screen. The deltas may be up to 65,535, so the sum may pass 16 bits.
LINCLP:         MOVW    LX, RCL
                MOVW    LY, RDL
                STZ     LVIS           ; 0: none on the screen yet; 1: drawing
                MOVW    LSUM, MAXDEL
                LSR     LSUM + 1       ; half the major delta
                ROR     LSUM
                MOVW    LCNT, MAXDEL
                INCW    LCNT           ; (0: 65,536, the loop's DEC first)
                JSR     LSKIP
                BCS     LCLX           ; never on the screen
@pt:            ASL     LSTYL          ; 7e4: the style turned at every point of the walk
                ROL     LSTYL + 1
                BCC     :+
                INC     LSTYL
:               ROR     LBIT           ;   (bit 7: this point's bit)
                JSR     LONSCR
                BCC     @off
                LDA     LVIS
                BNE     @set
                INC     LVIS
                MOVW    RCL, LX
                MOVW    RDL, LY
                JSR     MAPXYC
@set:           BIT     LBIT
                BPL     @step
                JSR     SETC
                BRA     @step
@off:           LDA     LVIS
                BNE     LCLX           ; past the screen: done
                JSR     LMAJON
                BCC     LCLX           ; the major coordinate past it: none
@step:          JSR     LADV
                LDA     LCNT
                BNE     :+
                DEC     LCNT + 1
:               DEC     LCNT
                LDA     LCNT
                ORA     LCNT + 1
                BNE     @pt
LCLX:           RTS
; LADV: the walk's step: the sum, a minor step when it passes the major
; delta, and the major step.
LADV:           CLC
                LDA     LSUM
                ADC     MINDEL
                STA     LSUM
                LDA     LSUM + 1
                ADC     MINDEL + 1
                STA     LSUM + 1
                BCS     @min           ; past 16 bits: past the major delta
                LDA     LSUM
                CMP     MAXDEL
                LDA     LSUM + 1
                SBC     MAXDEL + 1
                BCC     @maj
@min:           LDA     LSUM           ; (C set)
                SBC     MAXDEL
                STA     LSUM
                LDA     LSUM + 1
                SBC     MAXDEL + 1
                STA     LSUM + 1
                LDA     LYM            ; the minor step: x (y major) or y
                EOR     #1
                JSR     LSTEP
@maj:           LDA     LYM            ; the major step
                ; (on into LSTEP)
; LSTEP: A 1 a step of y (down), 0 of x (LXS): the point moved, and the
; cursor with it while drawing (the move MINUPD or MAXUPD names: DOWNC, or
; RIGHTC or LEFTC).
LSTEP:          TAX
                BEQ     @x
                INCW    LY
                LDA     LVIS
                BEQ     @r
                JMP     DOWNC
@x:             LDA     LXS
                BMI     @l
                INCW    LX
                LDA     LVIS
                BEQ     @r
                JMP     RIGHTC
@l:             DECW    LX
                LDA     LVIS
                BEQ     @r
                JMP     LEFTC
@r:             RTS

LSKNO:          CLC
                RTS
; LSKIP: the point moved, as k steps of the walk would move it, to where its
; major coordinate first meets the screen (y major: from above, y 0;
; x major: x 0 going right, the last going left), when it starts before
; the screen; k steps' minor moves are (LSUM + k x MINDEL) / MAXDEL, the sum
; the remainder (the walk's own arithmetic, at once). C set if the line's
; points end before it gets there; C clear otherwise, or if no move is needed.
LSKIP:          LDA     LYM
                BEQ     @xm
                LDA     LY + 1         ; y major: above the screen?
                BPL     LSKNO
                SEC
                LDA     #0
                SBC     LY
                STA     LK
                LDA     #0
                SBC     LY + 1
                STA     LK + 1
                STZ     LY
                STZ     LY + 1
                BRA     @k
@xm:            LDA     LXS
                BMI     @xl
                LDA     LX + 1         ; going right: left of the screen?
                BPL     LSKNO
                SEC
                LDA     #0
                SBC     LX
                STA     LK
                LDA     #0
                SBC     LX + 1
                STA     LK + 1
                STZ     LX
                STZ     LX + 1
                BRA     @k
@xl:            LDA     LX             ; going left: past the last?
                LDX     LX + 1
                JSR     XPAST
                BCC     LSKNO
                SEC
                LDA     LX
                SBC     VWMX
                STA     LK
                LDA     LX + 1
                SBC     VWMX + 1
                STA     LK + 1
                MOVW    LX, VWMX
@k:             LDA     LCNT           ; 0: 65,536 points, more than any k
                ORA     LCNT + 1
                BEQ     :+
                LDA     LK
                CMP     LCNT
                LDA     LK + 1
                SBC     LCNT + 1
                BCC     :+
                RTS                    ; (C set) k points or fewer: none get there
:               LDA     LK             ; 7e4: the style turned by the points skipped
                AND     #15
                TAX
                BEQ     @t
@tl:            ASL     LSTYL
                ROL     LSTYL + 1
                BCC     :+
                INC     LSTYL
:               DEX
                BNE     @tl
@t:             SEC                    ; the points left
                LDA     LCNT
                SBC     LK
                STA     LCNT
                LDA     LCNT + 1
                SBC     LK + 1
                STA     LCNT + 1
                STZ     LP + 2         ; LP = k x MINDEL (LK shifted out)
                STZ     LP + 3
                LDX     #16
@ml:            LSR     LK + 1
                ROR     LK
                BCC     @mr
                CLC
                LDA     LP + 2
                ADC     MINDEL
                STA     LP + 2
                LDA     LP + 3
                ADC     MINDEL + 1
                STA     LP + 3
@mr:            ROR     LP + 3
                ROR     LP + 2
                ROR     LP + 1
                ROR     LP
                DEX
                BNE     @ml
                CLC                    ; + the sum
                LDA     LP
                ADC     LSUM
                STA     LP
                LDA     LP + 1
                ADC     LSUM + 1
                STA     LP + 1
                BCC     :+
                INC     LP + 2
                BNE     :+
                INC     LP + 3
:               LDX     #16            ; / MAXDEL: the quotient (it fits 16
@dl:            ASL     LP             ; bits: at most k) in LP, the
                ROL     LP + 1         ; remainder in LP + 2
                ROL     LP + 2
                ROL     LP + 3
                BCS     @ds            ; past 16 bits: past MAXDEL
                LDA     LP + 2
                CMP     MAXDEL
                LDA     LP + 3
                SBC     MAXDEL + 1
                BCC     @dn
@ds:            LDA     LP + 2         ; (C set)
                SBC     MAXDEL
                STA     LP + 2
                LDA     LP + 3
                SBC     MAXDEL + 1
                STA     LP + 3
                INC     LP
@dn:            DEX
                BNE     @dl
                MOVW    LSUM, LP + 2
                LDA     LYM            ; the minor coordinate moved: y down, or
                BNE     @mx            ; x by LXS (y major)
                CLC
                LDA     LY
                ADC     LP
                STA     LY
                LDA     LY + 1
                ADC     LP + 1
                STA     LY + 1
                CLC
                RTS
@mx:            LDA     LXS
                BMI     @mxl
                CLC
                LDA     LX
                ADC     LP
                STA     LX
                LDA     LX + 1
                ADC     LP + 1
                STA     LX + 1
                CLC
                RTS
@mxl:           SEC
                LDA     LX
                SBC     LP
                STA     LX
                LDA     LX + 1
                SBC     LP + 1
                STA     LX + 1
                CLC
                RTS

; ONSCR: C set if (CX, DX) is on the screen; LONSCR the same of (LX, LY);
; LMAJON of the point's major coordinate alone (LYM: y, else x).
ONSCR:          LDA     RCL
                LDX     RCH
                JSR     XON
                BCC     ONR
                LDA     RDL
                LDX     RDH
                BRA     YON
LONSCR:         JSR     LXON
                BCC     ONR
LYON:           LDA     LY
                LDX     LY + 1
YON:            CPX     #$80
                BCS     ONNO
                JSR     YPAST
                BRA     ONINV
LMAJON:         LDA     LYM
                BNE     LYON
LXON:           LDA     LX
                LDX     LX + 1
XON:            CPX     #$80
                BCS     ONNO
                JSR     XPAST
ONINV:          ROL     A              ; C: not past
                EOR     #1
                LSR     A
ONR:            RTS
ONNO:           CLC
                RTS

; HLFDE: DX halved.
HLFDE:          LSR     RDH
                ROR     RDL
                RTS

; --- GRPRST, GRPINI (GENGRP.ASM 532) ----------------------------------------------------------

GRPRST:         PSHW    RCL            ; CX and BX kept (DX isn't, as the source's)
                PSHW    RBL
                STZ     DRWSCL         ; DRAW's scale and angle (7e2; and TA's, OO-1)
                STZ     DRWANG
                STZ     DRWTA
                JSR     VWRSTW         ; 7e4: RUN, CLEAR and NEW end VIEW and WINDOW
                JSR     GRPINI
                SEC
                JSR     GETFBC
                JSR     SETATR         ; DRAW's default colour (7e2's)
                STA     ATRBYT
                PULW    RBL
                PULW    RCL
                RTS

GRPINI:         JSR     GRPSIZ
                LDA     RCL            ; (x's last + 1) / 2
                CLC
                ADC     #1
                STA     GRPACX
                LDA     RCH
                ADC     #0
                LSR     A
                STA     GRPACX + 1
                ROR     GRPACX
                LDA     RDL
                CLC
                ADC     #1
                STA     GRPACY
                LDA     RDH
                ADC     #0
                LSR     A
                STA     GRPACY + 1
                ROR     GRPACY
                RTS

.segment "RODATA"
; 7e5: GCELL's, by mode from 1: the columns beside the picture, those over
; it, a cell's rows.
GCOT:           .byte   0, 0, 4, 8, 0
GCWT:           .byte   40, 80, 32, 64, 80
GCHT:           .byte   8, 8, 8, 16, 16

.segment "BRAM"
GRPACX:         .res    2              ; the graphics cursor (the graphics accumulator)
GRPACY:         .res    2
GXPOS:          .res    2              ; the second point, for the caller
GYPOS:          .res    2
ATRBYT:         .res    1              ; the current attribute
MINDEL:         .res    2              ; the line's minor delta
MAXDEL:         .res    2              ;   and major
MINUPD:         .res    2              ;   their moves (MINUPD+1 and MAXUPD+1, the source's JMP cells)
MAXUPD:         .res    2
LMAJ:           .res    2              ; DOGRPH's dy, then the major delta
BOXN:           .res    2              ; DOBOXF's lines
BOXW:           .res    2              ;   and width
LOFF:           .res    1              ; DOGRPH: an end off the screen (LINCLP)
LXS:            .res    1              ;   x's step, +1 or -1
LYM:            .res    1              ;   1: y the major axis
LX:             .res    2              ; LINCLP's point
LY:             .res    2
LSUM:           .res    2              ;   its sum
LCNT:           .res    2              ;   the points left
LVIS:           .res    1              ;   1: drawing (the cursor at the point)
LK:             .res    2              ; LSKIP's k, the steps skipped
LP:             .res    4              ;   k x MINDEL + the sum, then its quotient and remainder
GCR:            .res    1              ; GCELL's rows
GCN:            .res    1              ;   and pixels left in the row
GLB:            .res    1              ; 7e4: GLINEP's bit 7, a box with a style
LBIT:           .res    1              ;   LINCLP's: bit 7 the point's style bit

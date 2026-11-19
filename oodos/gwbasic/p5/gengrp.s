; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GENGRP.ASM, the generalised graphics, translated, in block P5 (hw/oodos.md,
; "The plan, phase 7": 7e1, U3), with GWSTS.ASM's LINLP3, the line's inner
; loop: the statements on the OEM's pixel routines (p5/gfxoem.s, p5/gfx.s).
;
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
; DRAW's scale and angle, which GRPRST clears, are 7e2's.

.include "gwbasic.inc"
.include "p5/p5.inc"
; OOLIB's (oolib.inc, vmem.inc: not included here, since gwbasic.inc defines
; GXP, gxmem's pointer, which only p5/seed.s exports to it)
.import GX_W1BACK

;@entry GRPRST
;@entry GRPINI
.export GRPRST, GRPINI

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

; --- GRPRST, GRPINI (GENGRP.ASM 532) ----------------------------------------------------------

GRPRST:         PSHW    RCL            ; CX and BX kept (DX isn't, as the source's)
                PSHW    RBL
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

.segment "BRAM"
GRPACX:         .res    2              ; the graphics cursor (the graphics accumulator)
GRPACY:         .res    2
ATRBYT:         .res    1              ; the current attribute

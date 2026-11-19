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
; DRAW's scale and angle, which GRPRST clears, are 7e2's.

.include "gwbasic.inc"
.include "p5/p5.inc"
; OOLIB's (oolib.inc, vmem.inc: not included here, since gwbasic.inc defines
; GXP, gxmem's pointer, which only p5/seed.s exports to it)
.import GX_W1BACK

.export PSETP, PRSETP, POINTP, GLINEP, GRPRST, GRPINI
.import CHRGTR, CHRGT2, SYNCHR, GETIN2, GETBYT, FCERR, MAKINT

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
; CX x and DX y.
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
                JSR     GETIN2         ; x
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
@abs:           MOVW    GRPACX, RCL
                MOVW    GXPOS, RCL
                MOVW    GRPACY, RDL
                MOVW    GYPOS, RDL
                RTS

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
; LINE (x1,y1)-(x2,y2),POINT(x3,y3) draws as it reads.
POINTP:         JSR     CHRGTR
                JSR     FETCHC
                LDX     #CSIZE - 1     ; C: CSAV and CSAVP
@c:             LDA     CSAV,X
                PHA
                DEX
                BPL     @c
                PSHW    GYPOS
                PSHW    GXPOS
                PSHW    GRPACY
                PSHW    GRPACX
                JSR     SCAND
                JSR     PIXSIZ         ; OO-1: SCREEN 0 has no pixels (PC-BASIC's)
                BNE     :+
                JMP     FCERR
:               JSR     SCALXY
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
                PULW    GRPACX
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
; YDELT the same of GYPOS and DX; NEGHL: BX negated, C set.
XDELT:          SEC
                LDA     GXPOS
                SBC     RCL
                STA     RBL
                LDA     GXPOS + 1
                SBC     RCH
                STA     RBH
CNEGHL:         BCC     NEGHL          ; (the 6502's carry: no borrow)
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
GLINEP:         JSR     SCAN1          ; the first point
                PSHW    RCL
                PSHW    RDL
                JSR     SYNCHR
                .byte   MINUTK
                JSR     SCAND          ; the second
                JSR     ATRSCN
                PHP                    ; Z: the statement ended
                PLX
                PULW    RDL
                PULW    RCL
                PHX
                PLP
                BNE     :+
                JMP     DOLINE         ; a line
:               JSR     SYNCHR
                .byte   ','
                JSR     SYNCHR
                .byte   'B'
                BNE     :+
                JMP     BOXLIN         ; B: the box
:               JSR     SYNCHR
                .byte   'F'            ; BF: filled
DOBOXF:         JSR     SCALXY
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
; its minor (MINUPD) when the sum passes the major's delta.
DOGRPH:         JSR     SCALXY         ; "cheaty scaling - just truncate"
                JSR     XCHGAC
                JSR     SCALXY
DOGRP2:         JSR     YDELT
                BCC     :+
                JSR     XCHGAC         ; (CX, DX) the point with the smaller y
:               MOVW    LMAJ, RBL       ; dy
                JSR     XDELT          ; dx; C: left
                LDX     GMX
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
                MOVW    MINDEL, LMAJ    ; x major: MINDEL dy
                MOVW    MAXUPD, T0
                LDA     DOWNV - 2,X
                STA     MINUPD
                LDA     DOWNV - 1,X
                STA     MINUPD + 1
                MOVW    LMAJ, RBL       ; the major delta: dx
                BRA     LINCN3
YDLTBG:         MOVW    MINUPD, T0     ; y major
                LDA     DOWNV - 2,X
                STA     MAXUPD
                LDA     DOWNV - 1,X
                STA     MAXUPD + 1
                MOVW    MINDEL, RBL    ; MINDEL dx; the major delta dy
LINCN3:         MOVW    MAXDEL, LMAJ
                JSR     MAPXYC
                MOVW    RDL, LMAJ       ; the sum: half the major delta
                JSR     HLFDE
                MOVW    RCL, LMAJ       ; the points: the major delta + 1
                INCW    RCL
                ; (on into LINLP3)

; LINLP3 (GWSTS.ASM 486): the line's inner loop. DX the sum, CX the points.
LINLP3:         JSR     SETC
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

; HLFDE: DX halved.
HLFDE:          LSR     RDH
                ROR     RDL
                RTS

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

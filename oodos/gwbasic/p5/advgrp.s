; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; ADVGRP.ASM's drawing, translated, in block P5 (hw/oodos.md, "The plan,
; phase 7": 7e2; the owner's answer 1 and option 1 of 2026-11-19: the
; parsing in P3, the drawing here, where the aperture is mapped):
;
;   CIRCP     CIRCLE: its parse by one far call to P3's CIRCA (p3/advgrp.s),
;             then the circle's loop (CIRCLP) and its plotting (CPLOT8,
;             CPLOT4, CPLOT), with SCALE, GTABSC and CLINE2
;
; The 8086's registers are the pseudo-registers, as in p5/gengrp.s: BX
; (HL in the source's comments) RBL/RBH, DX (DE) RDL/RDH, CX RCL/RCH. What
; the source keeps on the 8086's stack across CPLOT (the offsets, the loop
; count) and its data segment's CIRCLE words are zero page here: SI, DI, BP
; and PTR, which the drawing (DOGRPH, MAPXYC, SETC) doesn't use, and the
; floating point's scratch from DBUFF ($3A-$51), which nothing uses while a
; statement draws (OO-1: for room and speed). The drawing calls nothing resident: the aperture is in
; W1 from the first point, and block 1 is put back (GX_W1BACK) at the end.

.include "gwbasic.inc"
.include "p5/p5.inc"
.import GX_W1BACK

.export CIRCP, CIRCC
.import POLKEY

; Zero page: the loop's, and CIRCA's results from CZ, in the order
; CIRCA leaves them from DX on.
CRCX            = RSI                  ; the loop's X (twice the radius at first)
CRCY            = RDI                  ;   its Y
CRCSUM          = RBP                  ;   and SUM
CPC             = PTR                  ; CPLOT: this point's count
CZ              = DBUFF
CSTCNT          = CZ                   ; the start's point count
CENCNT          = CZ + 2               ; the end's
CLINEF          = CZ + 4               ; bit 0: a line from the start; bit 7 from the end
CPLOTF          = CZ + 5               ; 0: plot between start and end; $FF outside
CNPNTS          = CZ + 6               ; the points an octant
ASPECT          = CZ + 8               ; the aspect, a fraction of 256
CZOUT           = 10                   ;   (CIRCA's to here)
CSCLXY          = CZ + 10              ; 1: x scaled, not y
CPCNT           = CZ + 11              ; CPLOT8's point count
CPCNT8          = CZ + 13              ;   the octants' counts so far
CXOFF           = CZ + 15              ;   the offsets swapped between points
CYOFF           = CZ + 17
CHL             = CZ + 19              ; CPLOT: HL and DE (the 8086's stack)
CDE             = CZ + 21
CPLN            = CZ + 23              ;   the points left of four
.assert CPLN < FOEXP + 1, error, "CIRCLE's zero page past the floating point's scratch"

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
.macro  NEGW    zp                     ; NEGDE, NEGHL
                SEC
                LDA     #0
                SBC     zp
                STA     zp
                LDA     #0
                SBC     zp + 1
                STA     zp + 1
.endmacro

.code

; --- CIRCLE (ADVGRP.ASM 460) ------------------------------------------------------------------

CIRCP:          JSR     GTASPC         ; for CIRCA: GTASPC's pair, the cursor,
                MOVW    RSI, RDL       ; the foreground, PIXSIZ
                MOVW    RCL, GRPACX
                MOVW    RDL, GRPACY
                SEC
                JSR     GETFBC
                STA     RAL
                JSR     PIXSIZ
                STA     RAH
                FJSR    CIRCA          ; the statement parsed (P3)
                LDX     #CZOUT - 1     ; CSTCNT .. ASPECT, from DX .. SI
@o:             LDA     RDL,X
                STA     CZ,X
                DEX
                BPL     @o
                LDA     T1
                STA     CSCLXY
                MOVW    CRCX, RDI      ; X = the radius x 2
                ASL     CRCX
                ROL     CRCX + 1
                LDA     T0             ; the colour: the attribute (ATRFI2's SETATR)
                JSR     SETATR
                STA     ATRBYT
                STZ     CRCY           ; Y = 0
                STZ     CRCY + 1
                STZ     CRCSUM         ; SUM = 0
                STZ     CRCSUM + 1
; CIRCLP: the half-unit loop: at each even Y the eight points of
; ((X + 1) / 2, (Y + 1) / 2), until Y reaches X; SUM = SUM + 2Y + 1, Y + 1,
; and while SUM isn't negative SUM = SUM - 2X + 1, X - 1.
CIRCLP:         LDA     CRCY
                LSR     A
                BCS     CRCLP2         ; Y odd: no points
                CLC                    ; HL = (X + 1) / 2
                LDA     CRCX
                ADC     #1
                STA     RBL
                LDA     CRCX + 1
                ADC     #0
                LSR     A
                STA     RBH
                ROR     RBL
                CLC                    ; DE = (Y + 1) / 2
                LDA     CRCY
                ADC     #1
                STA     RDL
                LDA     CRCY + 1
                ADC     #0
                LSR     A
                STA     RDH
                ROR     RDL
                JSR     CPLOT8
                LDA     CRCY           ; Y >= X: done
                CMP     CRCX
                LDA     CRCY + 1
                SBC     CRCX + 1
                BCC     CRCLP2
                JMP     GX_W1BACK      ; (OO-1)
CRCLP2:         SEC                    ; SUM + 2Y + 1
                LDA     CRCY
                ROL     A
                STA     T0
                LDA     CRCY + 1
                ROL     A
                STA     T1
                CLC
                LDA     CRCSUM
                ADC     T0
                STA     CRCSUM
                LDA     CRCSUM + 1
                ADC     T1
                STA     CRCSUM + 1
                BMI     CNODEX         ; negative: X stays
                LDA     CRCX           ; SUM - (2X - 1), X - 1
                ASL     A
                STA     T0
                LDA     CRCX + 1
                ROL     A
                STA     T1
                LDA     T0
                BNE     :+
                DEC     T1
:               DEC     T0
                SEC
                LDA     CRCSUM
                SBC     T0
                STA     CRCSUM
                LDA     CRCSUM + 1
                SBC     T1
                STA     CRCSUM + 1
                DECW    CRCX
CNODEX:         INC     CRCY           ; Y + 1
                BNE     :+
                INC     CRCY + 1       ; OO-1 (the owner's ruling of 2026-11-19):
                JSR     GX_W1BACK      ; every 256 steps Ctrl-C's poll, block 1
                JSR     POLKEY         ; back (the next point's MAPXYC maps the
:               JMP     CIRCLP         ; aperture again), for a radius's long loop

; CIRCC: from P3's CIRCA, the centre parsed (CX, DX): the cursor (GRPACX,
; GRPACY) and GXPOS, GYPOS made it, as SCAN1 makes them.
CIRCC:          MOVW    GRPACX, RCL
                MOVW    GXPOS, RCL
                MOVW    GRPACY, RDL
                MOVW    GYPOS, RDL
                RTS

; CPLSCX: DE scaled by the aspect (SCALEY) to DX, BX the unscaled; the two
; swapped when the aspect was over 1 (CSCLXY: x scaled, not y).
CPLSCX:         PSHW    RDL
                JSR     SCALEY
                PULW    RBL
                LDA     CSCLXY
                BEQ     XCHRT
; XCHBD: XCHG BX,DX.
XCHBD:          LDX     RBL
                LDY     RDL
                STX     RDL
                STY     RBL
                LDX     RBH
                LDY     RDH
                STX     RDH
                STY     RBH
XCHRT:          RTS

; CPLOT8: the eight points of HL (x) and DE (y), offsets from the centre:
; the even octants, then the odd ones backwards (the point count NP - DE).
CPLOT8:         MOVW    CPCNT, RDL     ; the point count: Y
                PSHW    RBL            ; X
                STZ     CPCNT8
                STZ     CPCNT8 + 1
                JSR     CPLSCX         ; Y scaled
                MOVW    CXOFF, RBL
                PULW    RBL
                JSR     XCHBD
                PSHW    RBL
                JSR     CPLSCX         ; X scaled
                MOVW    CYOFF, RDL
                PULW    RDL
                NEGW    RDL            ; DE = -SY, HL = X, CXOFF Y, CYOFF SX
                JSR     CPLOT4
                SEC                    ; the odd octants: CPCNT8 NP, CPCNT NP - CPCNT,
                LDA     CNPNTS         ; CXOFF and DE negated
                STA     CPCNT8
                SBC     CPCNT
                STA     CPCNT
                LDA     CNPNTS + 1
                STA     CPCNT8 + 1
                SBC     CPCNT + 1
                STA     CPCNT + 1
                NEGW    CXOFF
                NEGW    RDL
CPLOT4:         LDA     #4             ; four points
                STA     CPLN
CPLOT:          MOVW    CHL, RBL
                MOVW    CDE, RDL
                CLC                    ; this point's count: CPCNT + CPCNT8, and
                LDA     CPCNT8         ; CPCNT8 + 2 NP for the next
                ADC     CPCNT
                STA     CPC
                LDA     CPCNT8 + 1
                ADC     CPCNT + 1
                STA     CPC + 1
                LDA     CNPNTS
                ASL     A
                STA     T0
                LDA     CNPNTS + 1
                ROL     A
                STA     T1
                CLC
                LDA     CPCNT8
                ADC     T0
                STA     CPCNT8
                LDA     CPCNT8 + 1
                ADC     T1
                STA     CPCNT8 + 1
                LDA     CSTCNT         ; the start: the same, a line from it?
                CMP     CPC
                BNE     :+
                LDA     CSTCNT + 1
                CMP     CPC + 1
                BEQ     CLINSC
:               LDA     CPC            ; past this point: not between
                CMP     CSTCNT
                LDA     CPC + 1
                SBC     CSTCNT + 1
                BCC     CNBTWN
                LDA     CENCNT         ; the end
                CMP     CPC
                BNE     :+
                LDA     CENCNT + 1
                CMP     CPC + 1
                BEQ     CLINEC
:               LDA     CPC            ; the end past this point: between
                CMP     CENCNT
                LDA     CPC + 1
                SBC     CENCNT + 1
                BCC     CBTWEN
CNBTWN:         LDA     CPLOTF         ; not between: plotted if CPLOTF says
                BNE     CPLTIT
                BRA     CPLFIN
CLINEC:         BIT     CLINEF         ; bit 7: a line from the centre
                BPL     CPLTIT
                BRA     CLINE
CLINSC:         LDA     CLINEF         ; bit 0: a line to the centre
                LSR     A
                BCC     CPLTIT
CLINE:          JSR     GTABSC
                JSR     CLINE2
                BRA     CPLFIN
CBTWEN:         LDA     CPLOTF         ; between: plotted if CPLOTF is 0
                BNE     CPLFIN
CPLTIT:         JSR     GTABSC
                JSR     SCALXY         ; off the screen: no plot
                BCC     CPLFIN
                JSR     MAPXYC
                JSR     SETC
CPLFIN:         DEC     CPLN
                BNE     :+
                MOVW    RBL, CHL
                MOVW    RDL, CDE
                RTS
:               SEC                    ; HL = -CXOFF, CXOFF = HL; DE = -CYOFF,
                LDA     #0             ; CYOFF = DE
                SBC     CXOFF
                STA     RBL
                LDA     #0
                SBC     CXOFF + 1
                STA     RBH
                MOVW    CXOFF, CHL
                SEC
                LDA     #0
                SBC     CYOFF
                STA     RDL
                LDA     #0
                SBC     CYOFF + 1
                STA     RDH
                MOVW    CYOFF, CDE
                JMP     CPLOT

; CLINE2: a line from (CX, DX) to the cursor (GRPACX, GRPACY).
CLINE2:         MOVW    GXPOS, GRPACX
                MOVW    GYPOS, GRPACY
                JMP     DOGRPH

; GTABSC: (CX, DX) = (GRPACX + HL, GRPACY + DE).
GTABSC:         CLC
                LDA     GRPACX
                ADC     RBL
                STA     RCL
                LDA     GRPACX + 1
                ADC     RBH
                STA     RCH
                CLC
                LDA     GRPACY
                ADC     RDL
                STA     RDL
                LDA     GRPACY + 1
                ADC     RDH
                STA     RDH
                RTS

; SCALEY: DE scaled by ASPECT; SCALE by BX: x BL / 256, rounded (SCAL2, the
; 8086's MUL as an 8 x 16 multiply here); BL 0 is x 1 if BH isn't 0, else x 0.
SCALEY:         MOVW    RBL, ASPECT
SCALE:          LDA     RBL
SCALE2:         BNE     SCAL2          ; A x DE (A not 0)
                ORA     RBH
                BEQ     :+
                RTS                    ; x 1
:               MOVW    RBL, RDL       ; x 0: XCHG BX,DX (BX 0)
                STZ     RDL
                STZ     RDH
                RTS
SCAL2:          STA     T0             ; T3:T2:T1 = A x DE
                STZ     T1
                STZ     T2
                STZ     T3
                LDX     #8
@m:             ASL     T1
                ROL     T2
                ROL     T3
                ASL     T0
                BCC     @n
                CLC
                LDA     T1
                ADC     RDL
                STA     T1
                LDA     T2
                ADC     RDH
                STA     T2
                BCC     @n
                INC     T3
@n:             DEX
                BNE     @m
                CLC                    ; + 128, the high 16 bits
                LDA     T1
                ADC     #128
                LDA     T2
                ADC     #0
                STA     RDL
                LDA     T3
                ADC     #0
                STA     RDH
                RTS


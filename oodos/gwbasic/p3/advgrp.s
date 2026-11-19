; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; ADVGRP.ASM's CIRCLE, its parsing, translated, in block P3 (hw/oodos.md,
; "The plan, phase 7": 7e2, U2; the owner's answer 1 and option 1 of
; 2026-11-19: a statement's parsing in P3, its drawing in P5):
;
;   CIRCA     from P5's CIRCP (p5/advgrp.s), by one far call a CIRCLE:
;             CIRCLE [STEP](x,y),r[,[c][,[start][,[end][,aspect]]]] parsed
;             at TXTPTR, the centre (SCAN1), the radius (GETIN2), the
;             colour (ATRSCN's parse), the start and end (CGTCNT, CMPONE)
;             and the aspect, with CNPNTS (the radius x SQR(2)/2), all the
;             floating point of the statement
;
; In: RCL/RCH and RDL/RDH the graphics cursor (GRPACX, GRPACY: STEP's
; base), RAL the mode's foreground (the default colour), RAH PIXSIZ (0 in
; SCREEN 0: Illegal function call after the colour, as ATRSCN's ATRFI2),
; RBL/RBH and RSI GTASPC's pair (the aspect x 256 and its inverse). Out, for
; P5 (P3 can't reach P5's RAM): RDL/RDH CSTCNT, RBL/RBH CENCNT, RAL CLINEF,
; RAH CPLOTF, RCL/RCH CNPNTS, RSI ASPECT, RDI the radius, T0 the colour
; (P5's SETATR makes it the attribute), T1 CSCLXY. Every error is raised
; here, before P5 draws anything. The graphics cursor is P5's: the centre
; goes to it by a far call to P5's CIRCC as soon as it is parsed (SCAN1's
; place), so an error in the colour, the angles or the aspect leaves the
; cursor at the centre, as the source's does (the owner's ruling of
; 2026-11-19).

.include "gwbasic.inc"

.export CIRCA
.import CHRGTR, CHRGT2, SYNCHR, GETIN2, GETBYT, FCERR, FRMEVL, FRCSNG, FRCINT, FMULS
.import FCOMP, FDIVS, MAKINT, PUSHF, POPR

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

; --- CIRCLE (ADVGRP.ASM 460) ------------------------------------------------------------------

CIRCA:          MOVW    CCURX, RCL
                MOVW    CCURY, RDL
                LDA     RAL
                STA     CFG
                LDA     RAH
                STA     CPIX
                MOVW    CASP, RBL
                MOVW    CINV, RSI
                JSR     CSCAN1         ; the centre, to P5's cursor at once (the
                FJSR    CIRCC          ; source's SCAN1: an error after it leaves
                                       ; the cursor there; the owner's ruling)
                JSR     SYNCHR
                .byte   ','
                JSR     GETIN2         ; the radius
                MOVW    CRAD, RDL
                MOVW    RBL, RDL       ; MKINTC
                JSR     MAKINT
                JSR     FRCSNG
                LDA     #$80           ; SQR(2)/2 (MOVRI 200O,65O,4O,363O)
                STA     RBH
                LDA     #$35
                STA     RBL
                LDA     #$04
                STA     RDH
                LDA     #$F3
                STA     RDL
                JSR     FMULS
                JSR     FRCINT
                MOVW    CNPNTS, RBL    ; the points an octant
                STZ     CLINEF         ; no lines to the centre
                STZ     CSCLXY         ; scaling y
                JSR     CATRS          ; the colour
                LDA     #1             ; CLINEF's bit 0: a line from the start
                STA     CGF
                STZ     RDL            ; the start's default: 0
                STZ     RDH
                JSR     CGTCNT
                PSHW    RDL
                LDA     #128           ; bit 7: a line from the end
                STA     CGF
                LDA     #$FF           ; the end's default: "infinity"
                STA     RDL
                STA     RDH
                JSR     CGTCNT
                MOVW    RBL, RDL       ; BX the end
                PULW    RDL            ; DX the start
                LDA     RBL            ; end >= start: plot between them
                CMP     RDL
                LDA     RBH
                SBC     RDH
                LDA     #0
                BCS     CSTPLT
                LDX     RBL            ; else above and below, start and
                LDY     RDL            ; end swapped (start < end)
                STX     RDL
                STY     RBL
                LDX     RBH
                LDY     RDH
                STX     RDH
                STY     RBH
                LDA     CLINEF         ; the centre lines' flags swapped
                ASL     A              ; (ROL AL twice, OR, ROR: 8-bit
                ADC     #0             ; rotates)
                ASL     A
                ADC     #0
                ORA     CLINEF
                LSR     A
                BCC     :+
                ORA     #$80
:               STA     CLINEF
                LDA     #$FF
CSTPLT:         STA     CPLOTF         ; the plot polarity
                MOVW    CSTCNT, RDL
                MOVW    CENCNT, RBL
                DECW    TXTPTR         ; an aspect?
                JSR     CHRGTR
                BNE     CIRC1
                MOVW    RBL, CASP      ; GTASPC's: past 1 (BH not 0) its inverse,
                LDA     CASP + 1       ; scaling x
                BEQ     CIRC2
                LDA     #1
                STA     CSCLXY
                MOVW    RBL, CINV
                BRA     CIRC2
CIRC1:          JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL
                JSR     FRCSNG
                JSR     CMPONE         ; over 1?
                BNE     CIRC11         ; no: scaling y
                INC     A
                STA     CSCLXY         ; scaling x, by 1/the aspect
                JSR     CONE
                JSR     FDIVS
CIRC11:         CLC                    ; x 256: 8 on the exponent
                LDA     FACEXP
                ADC     #8
                STA     FACEXP
                JSR     FRCINT
CIRC2:          ; (BX the aspect, a fraction of 256)
                MOVW    RSI, RBL       ; out, for P5
                MOVW    RDL, CSTCNT
                MOVW    RBL, CENCNT
                LDA     CLINEF
                STA     RAL
                LDA     CPLOTF
                STA     RAH
                MOVW    RCL, CNPNTS
                MOVW    RDI, CRAD
                LDA     CATR
                STA     T0
                LDA     CSCLXY
                STA     T1
                RTS

; CATRS: ATRSCN's parse (GENGRP.ASM 226), the colour to CATR: the
; foreground if the statement ends or it is null, else GETBYT's; then
; PIXSIZ's test (ATRFI2). P5's SETATR takes it after (every attribute is
; legal: SETATR is the OEM's, its carry always clear).
CATRS:          LDA     CFG
                STA     RDL
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     @fin
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     @fin           ; another comma: null, the default
                JSR     GETBYT
@fin:           LDA     RDL
                STA     CATR
                LDA     CPIX
                BNE     :+
                JMP     FCERR          ; no graphics (SCREEN 0)
:               JMP     CHRGT2

; CGTCNT (ADVGRP.ASM 782): an angle, DX its point count (the default DX if
; there is none): a negative one made positive with CGF's bit set in
; CLINEF (a line to the centre); over 2 x PI Illegal function call; the
; count angle / (2 x PI) x CNPNTS x 8, rounded (FRCINT).
CGTCNT:         DECW    TXTPTR
                JSR     CHRGTR
                BEQ     CGTRET             ; nothing: the default
                JSR     SYNCHR
                .byte   ','
                CMP     #','
                BEQ     CGTRET             ; no argument: the default
                LDA     CGF
                PHA
                JSR     FRMEVL
                JSR     FRCSNG
                PLA
                STA     CGF
                LDA     FACEXP         ; negative?
                BEQ     CGTC2
                LDA     FACM1
                BPL     CGTC2
                AND     #$7F           ; made positive
                STA     FACM1
                LDA     CLINEF
                ORA     CGF
                STA     CLINEF
CGTC2:          LDA     #$83           ; 1/(2 x PI) (MOVRI 176O,42O,371O,203O)
                STA     RDL
                LDA     #$F9
                STA     RDH
                LDA     #$22
                STA     RBL
                LDA     #$7E
                STA     RBH
                JSR     FMULS
                JSR     CMPONE         ; over one: Illegal function call
                BNE     :+
                JMP     FCERR
:               JSR     PUSHF
                MOVW    RBL, CNPNTS    ; x 8, the circumference
                LDX     #3
@x8:            ASL     RBL
                ROL     RBH
                DEX
                BNE     @x8
                JSR     MAKINT
                JSR     FRCSNG
                JSR     POPR
                JSR     FMULS
                JSR     FRCINT
                MOVW    RDL, RBL
CGTRET:             RTS

; CMPONE (ADVGRP.ASM 838): the FAC against 1: A FCOMP's less one (0, Z,
; when the FAC is over 1). CONE: BXDX 1.
CMPONE:         JSR     CONE
                JSR     FCOMP
                DEC     A
                RTS
CONE:           LDA     #$81
                STA     RBH
                STZ     RBL
                STZ     RDH
                STZ     RDL
                RTS

; CSCAN1: GENGRP.ASM's SCAN1 and SCAND (p5/gengrp.s's), for P3: "@" passed
; over; none (a "-" next) the cursor; (x,y) or STEP(x,y), STEP's relative
; to the cursor (CCURX, CCURY). CX x and DX y.
CSCAN1:         LDA     (TXTPTR)
                CMP     #'@'
                BNE     :+
                JSR     CHRGTR
:               STZ     RCL            ; no coordinates: the cursor (-0)
                STZ     RCH
                STZ     RDL
                STZ     RDH
                CMP     #MINUTK
                BEQ     @rel           ; (Z: relative)
                LDA     (TXTPTR)
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
@rel:           BNE     @x
                CLC                    ; relative: the cursor added
                LDA     CCURX
                ADC     RCL
                STA     RCL
                LDA     CCURX + 1
                ADC     RCH
                STA     RCH
                CLC
                LDA     CCURY
                ADC     RDL
                STA     RDL
                LDA     CCURY + 1
                ADC     RDH
                STA     RDH
@x:             RTS

.segment "BRAM"
CCURX:          .res    2              ; the graphics cursor, P5's (in)
CCURY:          .res    2
CFG:            .res    1              ; the mode's foreground (in)
CPIX:           .res    1              ; PIXSIZ (in)
CASP:           .res    2              ; GTASPC's aspect (in)
CINV:           .res    2              ;   and its inverse
CRAD:           .res    2              ; the radius (the source's GXPOS)
CATR:           .res    1              ; the colour
CNPNTS:         .res    2              ; the points an octant
CSTCNT:         .res    2              ; the start's count
CENCNT:         .res    2              ; the end's
CLINEF:         .res    1              ; bit 0: a line from the start, bit 7 from the end
CPLOTF:         .res    1              ; 0: plot between start and end; $FF: outside
CSCLXY:         .res    1              ; 1: scaling x (an aspect over 1)
CGF:            .res    1              ; CGTCNT's flag for CLINEF

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; ADVGRP.ASM's DRAW, translated, in block P3 (hw/oodos.md, "The plan, phase
; 7": 7e2, U4; the owner's answer 1: the parsing in P3, the drawing in P5):
;
;   DRAWP     DRAW string: P5's DRWOP 0 first (PIXSIZ: Illegal function call
;             in SCREEN 0 before the string is read; the scale, the angle,
;             TA's state and GTASPC's pair back), then MACLNG with DRWTAB
;
; Each command that draws, colours, paints or sets the state far calls P5's
; DRWOP (p5/advgrp.s), Y its operation: 1 a relative move (BX, DX: GTABSC
; and DRWABS there), 2 an absolute one (CX, DX), A DRWFLG (B and N) for
; both; 3 C (A the colour); 4 P; 5 the scale, the angle and TA's state
; stored (GRPRST, P5's, clears them, as the source's, and an error part way
; through a string keeps what was set).
;
; The IBM manual (the owner's spec): S 1-255, so S0 is Illegal function call
; (p. 74; the source takes it as no scaling). Not in the source (the manual's,
; 2.0 and later, p. 74-75; the owner's answer 9), written from the manual's
; meaning, the OO-1's:
;   TA n      the turn, -360 to 360 (else Illegal function call), positive
;             counter-clockwise. A n and TA n set the one angle: A n clears
;             TA's, TA n A's (the last given turns the moves). Each relative
;             move (U-H, M +-) is turned in single precision, in the
;             screen's square space: x' = x cos + y (1/aspect) sin, y' = y
;             cos - x aspect sin, each rounded (FRCINT), the aspect GTASPC's
;   P p,b     PAINT at the cursor in p to the border b, both required (else
;             Illegal function call), no tile; p becomes the drawing colour
;             (the manual's "figure color", the owner's G10). Its queue is
;             kept below the DRAW string when that lies in free memory (the
;             string MACLNG reads, which FRESTR may have freed: OO-1), and
;             it collects no garbage (that would move the string)

.include "gwbasic.inc"

.export DRAWP
.import FCERR, FETCHR, FETCHZ, DECFET, VALSCN, VALSC3, NEGD, MACLNG, MCLXEQ, MCLOUT
.import MAKINT, FRCSNG, FRCINT, FMULS, FADDS, PUSHF, POPR, MOVFR

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
.macro  NEGW    zp
                SEC
                LDA     #0
                SBC     zp
                STA     zp
                LDA     #0
                SBC     zp + 1
                STA     zp + 1
.endmacro

.code

; --- DRAW (ADVGRP.ASM 1103) -------------------------------------------------------------------

DRAWP:          LDY     #0             ; PIXSIZ, the state
                FJSR    DRWOP
                MOVW    DASP, RBL      ; GTASPC's pair
                MOVW    DINV, RDL
                LDA     RAL
                STA     DRWSCL
                LDA     RAH
                STA     DRWANG
                LDA     RCL
                STA     DTAON
                BEQ     :+
                JSR     TATRIG         ; (TA's turn for this mode's aspect)
:               STZ     DRWFLG
                LDA     #<DRWTAB
                LDX     #>DRWTAB
                JMP     MACLNG

; The commands (DRWTAB: bit 7 of the letter, a number taken).
DRUP:           NEGW    RDL            ; U: +0, -Y
DRDOWN:         STZ     RCL            ; D: +0, +Y
                STZ     RCH
                JMP     DOMOVR
DRLEFT:         NEGW    RDL            ; L: -X, +0
DRIGHT:         MOVW    RCL, RDL       ; R: +X, +0
                STZ     RDL
                STZ     RDH
                JMP     DOMOVR
DRWHHH:         NEGW    RDL            ; H: -X, -Y
DRWFFF:         MOVW    RCL, RDL       ; F: +X, +Y
                BRA     DOMOVR
DRWEEE:         MOVW    RCL, RDL       ; E: +X, -Y
DRWHHC:         NEGW    RDL
                BRA     DOMOVR
DRWGGG:         NEGW    RDL            ; G: -X, +Y
                MOVW    RCL, RDL
                BRA     DRWHHC

; DMOVE: M x,y: relative with a sign before x, else absolute.
DMOVE:          JSR     FETCHZ
                LDX     #0             ; relative
                CMP     #'+'
                BEQ     :+
                CMP     #'-'
                BEQ     :+
                INX
:               PHX
                JSR     DECFET         ; VALSCN sees the sign
                JSR     VALSCN         ; x
                PSHW    RDL
                JSR     FETCHZ
                CMP     #','
                BEQ     :+
                JMP     FCERR
:               JSR     VALSCN         ; y
                PULW    RCL
                PLA
                BEQ     DOMOVR
                LDY     #2             ; absolute: DRWABS
DRWOPF:         LDA     DRWFLG
                FJSR    DRWOP
                STZ     DRWFLG         ; DNSTOR: the flags cleared
                RTS

; DOMOVR: a relative move by (CX, DX), scaled (DSCLDE), turned (A's odd
; angles by the aspect, GOSCAL; A2 and A3 negated; or TA's), then from the
; cursor (P5's GTABSC) to DRWABS.
DOMOVR:         JSR     DSCLDE         ; y
                PSHW    RDL
                MOVW    RDL, RCL
                JSR     DSCLDE         ; x
                MOVW    RBL, RDL
                PULW    RDL
                STZ     CSCLXY
                LDA     DTAON          ; (OO-1) TA's turn
                BEQ     :+
                JSR     TATURN
                JMP     ANGPOS
:               LDA     DRWANG
                LSR     A
                JCC     ANGEVN         ; even: no swap
                PHA
                PSHW    RDL            ; dy
                PSHW    RBL            ; dx
                MOVW    RBL, DASP      ; GTASPC's pair: past 1, its inverse
                MOVW    RDL, DINV
                LDA     RBH
                BEQ     ASPLS0
                LDA     #1
                STA     CSCLXY
                JSR     XCHBD
ASPLS0:         LDA     RBL            ; CL the fraction for GOSCAL
                STA     GSCL
                PULW    RBL            ; dx
                LDA     CSCLXY
                BEQ     ASPLS1
                PLA                    ; XTHL: BX dy, dx on the stack
                TAX
                PLA
                TAY
                LDA     RBH
                PHA
                LDA     RBL
                PHA
                STX     RBL
                STY     RBH
ASPLS1:         JSR     XCHBD          ; DX dx (or dy), BX 1/the aspect
                PSHW    RBL
                JSR     GOSCAL         ; x the aspect
                PULW    RCL            ; CX 1/the aspect
                PULW    RBL            ; dy (or dx)
                PSHW    RDL            ; scaled
                MOVW    RDL, RBL
                STZ     RBL            ; BX = DX x CH
                STZ     RBH
                LDX     RCH
                BEQ     @m2
@mul:           CLC
                LDA     RBL
                ADC     RDL
                STA     RBL
                LDA     RBH
                ADC     RDH
                STA     RBH
                DEX
                BNE     @mul
@m2:            PSHW    RBL
                LDA     RCL            ; x CL
                STA     GSCL
                JSR     GOSCAL
                PULW    RBL
                CLC                    ; BX = y x 1/the aspect
                LDA     RBL
                ADC     RDL
                STA     RBL
                LDA     RBH
                ADC     RDH
                STA     RBH
                PULW    RDL            ; DX the scaled x
                LDA     CSCLXY
                BEQ     :+
                JSR     XCHBD
:               NEGW    RDL            ; the new y negated
                PLA
ANGEVN:         LSR     A              ; bit 1: both negated
                BCC     ANGPOS
                NEGW    RBL
                NEGW    RDL
ANGPOS:         LDY     #1             ; GTABSC, DRWABS (P5)
                JMP     DRWOPF

; XCHBD: XCHG BX,DX.
XCHBD:          LDX     RBL
                LDY     RDL
                STX     RDL
                STY     RBL
                LDX     RBH
                LDY     RDH
                STX     RDH
                STY     RBH
                RTS

; DNOMOV, DNOPLT: N and B, DRWFLG's bits 6 and 7.
DNOMOV:         LDA     #64
                BRA     DSTFLG
DNOPLT:         LDA     #128
DSTFLG:         ORA     DRWFLG
                STA     DRWFLG
                RTS

; DANGLE: A n, n under 4 (DL alone, the source's), TA's turn cleared (OO-1).
DANGLE:         BCC     NCFCER
                LDA     RDL
                CMP     #4
                BCS     NCFCER
                STA     DRWANG
                STZ     DTAON
                BRA     DSTORE
NCFCER:         JMP     FCERR
; DSCALE: S n, 1-255 (the manual's p. 74: S0 Illegal function call).
DSCALE:         BCC     NCFCER
                LDA     RDH
                BNE     NCFCER
                LDA     RDL
                BEQ     NCFCER         ; (OO-1, the manual's)
                STA     DRWSCL
DSTORE:         LDA     DRWSCL         ; DRWOP 5: the state to P5
                STA     RAL
                LDA     DRWANG
                STA     RAH
                LDA     DTAON
                STA     RCL
                LDY     #5
                FJSR    DRWOP
                RTS
; DCOLR: C n, SETATR's (P5).
DCOLR:          BCC     NCFCER
                LDA     RDL
                LDY     #3
                FJSR    DRWOP
                RTS

; DSCLDE (ADVGRP.ASM 1305): DX x DRWSCL / 4, toward zero (0: as it is).
DSCLDE:         LDX     DRWSCL
                BEQ     DSCRT
                STZ     RBL            ; BX = DX x the scale
                STZ     RBH
@m:             CLC
                LDA     RBL
                ADC     RDL
                STA     RBL
                LDA     RBH
                ADC     RDH
                STA     RBH
                DEX
                BNE     @m
                MOVW    RDL, RBL
                LDA     RDH            ; negative: one less first
                PHP
                BPL     :+
                LDA     RDL
                BNE     @d
                DEC     RDH
@d:             DEC     RDL
:               LSR     RDH            ; / 4
                ROR     RDL
                LSR     RDH
                ROR     RDL
                PLP
                BPL     DSCRT
                LDA     RDH            ; negative: its top two bits set, one
                ORA     #$C0           ; more (so it truncates toward zero)
                STA     RDH
                INC     RDL
                BNE     DSCRT
                INC     RDH
DSCRT:          RTS

; GOSCAL (ADVGRP.ASM 1330): DX x GSCL / 256, rounded (SCALE2's), its sign
; kept.
GOSCAL:         LDA     RDH
                PHP
                BPL     :+
                JSR     NEGD
:               LDA     GSCL
                BEQ     @x             ; (0: x 1 or x 0 by BH; never here)
                STA     T0
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
                CLC
                LDA     T1
                ADC     #128
                LDA     T2
                ADC     #0
                STA     RDL
                LDA     T3
                ADC     #0
                STA     RDH
@x:             PLP
                BPL     :+
                JMP     NEGD
:               RTS

; --- TA and P (OO-1, the manual's pp. 74-75) --------------------------------------------------

; DTURN: T: TA n, -360 to 360; its cosine and sine (P1's COS and SIN) kept,
; A's angle cleared, the state to P5 (0: no turn).
DTURN:          JSR     FETCHZ
                CMP     #'A'
                JNE     NCFCER
                JSR     VALSCN
                CLC                    ; -360 <= n <= 360: n + 360 <= 720
                LDA     RDL
                ADC     #<360
                TAX
                LDA     RDH
                ADC     #>360
                CPX     #<721
                SBC     #>721
                JCS     NCFCER
                STZ     DRWANG
                STZ     DTAON
                LDA     RDL
                ORA     RDH
                JEQ     DSTORE
                INC     DTAON
                MOVW    RBL, RDL       ; n degrees in radians: n x PI/180
                JSR     MAKINT
                JSR     FRCSNG
                LDA     #$35           ; PI/180, single (MBF: 7B 0E FA 35)
                STA     RDL
                LDA     #$FA
                STA     RDH
                LDA     #$0E
                STA     RBL
                LDA     #$7B
                STA     RBH
                JSR     FMULS
                JSR     PUSHF
                FJSR    COS
                LDX     #3
@c:             LDA     FACLO,X
                STA     TCOS,X
                DEX
                BPL     @c
                JSR     POPR
                JSR     MOVFR
                FJSR    SIN
                LDX     #3
@s:             LDA     FACLO,X
                STA     TSIN,X
                DEX
                BPL     @s
                JSR     TATRIG
                JMP     DSTORE

; TATRIG: TSA = the aspect x the sine, TSI = 1/the aspect x the sine (the
; mode's GTASPC pair, DASP and DINV, a fraction of 256).
TATRIG:         MOVW    RBL, DASP
                LDX     #TSA - TSIN
                JSR     TAMUL
                MOVW    RBL, DINV
                LDX     #TSI - TSIN
; TAMUL: TSIN,X = TSIN x BX / 256.
TAMUL:          PHX
                JSR     MAKINT
                JSR     FRCSNG
                DEC     FACEXP         ; / 256: 8 off the exponent
                DEC     FACEXP
                DEC     FACEXP
                DEC     FACEXP
                DEC     FACEXP
                DEC     FACEXP
                DEC     FACEXP
                DEC     FACEXP
                JSR     LDSIN
                JSR     FMULS
                PLY                    ; (zero page by X: the 65C816 rules)
                LDX     #0
@t:             LDA     FACLO,X
                STA     TSIN,Y
                INY
                INX
                CPX     #4
                BCC     @t
                RTS
LDSIN:          LDX     #3
@l:             LDA     TSIN,X
                STA     RDL,X
                DEX
                BPL     @l
                RTS
; TATURN: (BX, DX) turned by TA: BX = round(x cos + y TSI), DX = round(y cos
; - x TSA).
TATURN:         MOVW    TX, RBL
                MOVW    TY, RDL
                LDX     #TCOS - TSIN   ; x cos
                LDA     #TX - TX
                JSR     TAPROD
                JSR     PUSHF
                LDX     #TSI - TSIN    ; + y TSI
                LDA     #TY - TX
                JSR     TAPROD
                JSR     POPR
                JSR     FADDS
                JSR     FRCINT
                MOVW    TXN, RBL
                LDX     #TSA - TSIN    ; -(x TSA)
                LDA     #TX - TX
                JSR     TAPROD
                LDA     FACEXP
                BEQ     :+
                LDA     FACM1
                EOR     #$80
                STA     FACM1
:               JSR     PUSHF
                LDX     #TCOS - TSIN   ; + y cos
                LDA     #TY - TX
                JSR     TAPROD
                JSR     POPR
                JSR     FADDS
                JSR     FRCINT
                MOVW    RDL, RBL
                MOVW    RBL, TXN
                RTS
; TAPROD: the FAC = TX or TY (A its offset) x TSIN,X.
TAPROD:         PHX
                TAX
                LDA     TX,X
                STA     RBL
                LDA     TX + 1,X
                STA     RBH
                JSR     MAKINT
                JSR     FRCSNG
                PLY
                LDX     #0
@l:             LDA     TSIN,Y
                STA     RDL,X
                INY
                INX
                CPX     #4
                BCC     @l
                JMP     FMULS

; DPAINT: P p,b: both numbers required; DRWOP 4, the queue's end below the
; string (MCLOUT) when that is in free memory, else FRETOP.
DPAINT:         JSR     DPARG
                PHA
                JSR     FETCHZ
                CMP     #','
                BNE     DPFC
                JSR     DPARG
                STA     RCH            ; the border
                PLA
                STA     RCL            ; the paint
                LDA     FRETOP
                STA     RBL
                LDA     FRETOP + 1
                STA     RBH
                LDA     MCLOUT         ; the string between STREND and FRETOP?
                CMP     STREND
                LDA     MCLOUT + 1
                SBC     STREND + 1
                BCC     @go
                LDA     FRETOP
                CMP     MCLOUT
                LDA     FRETOP + 1
                SBC     MCLOUT + 1
                BCC     @go
                MOVW    RBL, MCLOUT    ; the queue below it
@go:            LDY     #4
                FJSR    DRWOP
                RTS
; DPARG: A a number's low byte; one must be there (not "," nor ";").
DPARG:          JSR     FETCHZ
                CMP     #','
                BEQ     DPFC
                CMP     #';'
                BEQ     DPFC
                JSR     VALSC3
                LDA     RDL
                RTS
DPFC:           JMP     FCERR

.segment "RODATA"
; DRWTAB (ADVGRP.ASM 1115): the letter (bit 7: a number taken), its routine.
DRWTAB:         .byte   'U' + $80
                .addr   DRUP
                .byte   'D' + $80
                .addr   DRDOWN
                .byte   'L' + $80
                .addr   DRLEFT
                .byte   'R' + $80
                .addr   DRIGHT
                .byte   'M'
                .addr   DMOVE
                .byte   'E' + $80
                .addr   DRWEEE
                .byte   'F' + $80
                .addr   DRWFFF
                .byte   'G' + $80
                .addr   DRWGGG
                .byte   'H' + $80
                .addr   DRWHHH
                .byte   'A' + $80
                .addr   DANGLE
                .byte   'B'
                .addr   DNOPLT
                .byte   'N'
                .addr   DNOMOV
                .byte   'X'
                .addr   MCLXEQ
                .byte   'C' + $80
                .addr   DCOLR
                .byte   'S' + $80
                .addr   DSCALE
                .byte   'T'                    ; (OO-1, the manual's: TA)
                .addr   DTURN
                .byte   'P'                    ; (OO-1, the manual's: P)
                .addr   DPAINT
                .byte   0

.segment "BRAM"
DRWFLG:         .res    1              ; bit 7: B, bit 6: N (the next move's)
DRWSCL:         .res    1              ; S (0: none), P5's at each DRAW
DRWANG:         .res    1              ; A
DTAON:          .res    1              ; 1: TA's turn (OO-1)
DASP:           .res    2              ; GTASPC's aspect x 256
DINV:           .res    2              ;   and its inverse
CSCLXY:         .res    1              ; 1: the aspect past 1
GSCL:           .res    1              ; GOSCAL's fraction (CL)
TSIN:           .res    4              ; (OO-1) TA's sine, single
TCOS:           .res    4              ;   its cosine
TSA:            .res    4              ;   the sine x the aspect
TSI:            .res    4              ;   x 1/the aspect
TX:             .res    2              ; TATURN's x and y
TY:             .res    2
TXN:            .res    2              ;   the new x

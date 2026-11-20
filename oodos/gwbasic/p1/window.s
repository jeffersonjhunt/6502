; WINDOW's map, PMAP and POINT(2) and (3), in block P1 (hw/oodos.md, "The
; plan, phase 7": 7e4; the space plan's answer 6: the math block): the
; OO-1's, written from the IBM manual (pp. 256-260, 370-374), the source
; having no code for them (GWSTS.ASM's WINDOW and PMAP fall into ADVERR).
;
;   WINSET  WINDOW, from the home block's stub (p8/far5.s): A bit 7 corners
;           given, bit 6 SCREEN; the four corners singles in the pseudo-
;           registers, x1 RDL-RBH, y1 RAL-RCH, x2 RSI-RDI, y2 RBP-PTR (each
;           in memory order). Not a graphics mode (GFLG bit 0): Illegal
;           function call. No corners: the window ended. Else each pair
;           sorted (x1 = x2 or y1 = y2 Illegal function call), GFLG's bits 7
;           (a window) and 6 (WINDOW SCREEN) set, the viewport asked of P5
;           (GVWGET), and the map made (WINRSC)
;   WINRSC  the map for a viewport (from P5's GVIEW, or WINSET): RDL/RDH its
;           last x, RBL its last y, RBH/RAL and RAH VIEW SCREEN's offset
;   WINXY   a world point to the viewport's: x in BXDX, y in the FAC
;           (singles), A bit 7 STEP (the cursor, RCL/RCH and RSI, mapped back
;           and the point added), out CX, DX; Overflow past 32,767
;   PMAPP   PMAP(x,n), from the home block's ERDEVP: x in BXDX, A n; the
;           value in the FAC
;   WPNT    POINT(2) and (3), from P5's POINTP: A 0 x, 1 y, RCL/RCH the
;           cursor's; the world value in the FAC
;   WINRAD  CIRCLE's radius under a window, from P3's CIRCA: the FAC's x
;           the x scale, to the nearest integer in DX
;
; The map, each axis: p = FRCINT((v - v0) x s), s = (the viewport's last) /
; (v2 - v1), v0 = v1, but for y without SCREEN (y up: v0 = y2, s negated);
; the cursor stays physical (the owner's answer 7). Back: v = (p - o) / s,
; o = -(v0 x s), the more accurate of the forms compared (the owner's answer
; 11; hw/oodos.md, 7e4 as built). PMAP 0 and 1, and POINT(0) and (1), are
; absolute under VIEW SCREEN: its offset added, as PMAP 2 and 3 take it off.

.include "gwbasic.inc"

;@entry WINSET
;@entry WINRSC
;@entry WINXY
;@entry PMAPP
;@entry WPNT
;@entry WINRAD
.export WINSET, WINRSC, WINXY, PMAPP, WPNT, WINRAD
.import FCERR, GFLG, FCOMP, FSUBS, FADDS, FMULS, FDIVS, FRCINT, FLT, MOVFR, MOVRF, MAKINT

.macro  MOVW    dst, src
                LDA     src
                STA     dst
                LDA     src + 1
                STA     dst + 1
.endmacro

; W's layout (offsets): the corners, the axes (v0, s, o each), the viewport.
WC              = 0                    ; x1, y1, x2, y2 (4 bytes each)
WAX0            = 16                   ; x's v0, s, o; y's 12 on
WMX             = 40                   ; the viewport's last x (2), y (1), VIEW SCREEN's x (2), y (1)
WSX             = WMX + 3
WSY             = WMX + 5
WT              = 46                   ; WAXIS's span
WTX             = 50                   ; WINXY's x and y
WTY             = 54
WSC             = WT                   ; WINSET's corners, before they're checked (WT's to WTY's,
WEND            = WSC + 16             ;   free until WAXIS)

.code

WINSET:         STA     WFL
                LDA     GFLG
                LSR     A
                BCS     :+
WFC:            JMP     FCERR          ; not a graphics mode
:               BIT     WFL
                BMI     @c
                LDA     GFLG           ; WINDOW alone: ended
                AND     #$3F
                STA     GFLG
                RTS
@c:             LDX     #15
@k:             LDA     RDL,X
                STA     W + WSC,X
                DEX
                BPL     @k
                LDY     #WSC           ; x, then y
                JSR     WSORT
                LDY     #WSC + 4
                JSR     WSORT
                LDX     #15            ; checked: the window's
@m:             LDA     W + WSC,X
                STA     W + WC,X
                DEX
                BPL     @m
                LDA     GFLG
                AND     #$3F
                ORA     #$80
                BIT     WFL
                BVC     :+
                ORA     #$40
:               STA     GFLG
                FJSR    GVWGET
WINRSC:         LDX     #WT - WMX - 1
@v:             LDA     RDL,X
                STA     W + WMX,X
                DEX
                BPL     @v
                LDX     #0
                JSR     WAXIS
                LDX     #1
                ; (on into WAXIS)

; WAXIS: X the axis (0 x, 1 y): its v0, s and o.
WAXIS:          STX     WAX
                TXA
                ASL     A
                ASL     A
                STA     WK             ; the corners' offset: 0, 4
                CLC
                ADC     #8
                TAY
                JSR     LDR            ; v2
                LDY     WK
                JSR     LDF            ; v1
                JSR     FSUBS          ; v2 - v1
                LDY     #WT
                JSR     STF
                LDA     WAX
                BNE     @y
                MOVW    RDL, W + WMX
                BRA     @n
@y:             LDA     W + WMX + 2
                STA     RDL
                STZ     RDH
@n:             JSR     FLT            ; the viewport's last
                JSR     MOVRF
                LDY     #WT
                JSR     LDF
                JSR     FDIVS          ; s
                LDY     WK             ; v0: v1
                LDA     WAX
                BEQ     @p
                BIT     GFLG
                BVS     @p             ; WINDOW SCREEN: y down
                LDA     FACM1          ; y up: s negated, v0 y2
                EOR     #$80
                STA     FACM1
                TYA
                ORA     #8
                TAY
@p:             STY     WV
                LDA     WAX
                JSR     WAXB
                STY     WB
                INY
                INY
                INY
                INY
                JSR     STF            ; s (the FAC kept)
                LDY     WV
                JSR     LDR            ; v0
                JSR     FMULS
                LDA     FACM1          ; o = -(v0 x s)
                EOR     #$80
                STA     FACM1
                LDA     WB
                CLC
                ADC     #8
                TAY
                JSR     STF
                LDX     WB             ; v0
                LDY     WV
                LDA     #4
                STA     WN
@c:             LDA     W,Y
                STA     W,X
                INX
                INY
                DEC     WN
                BNE     @c
                RTS

; WSORT: the pair at W + Y and W + Y + 8 sorted; equal, Illegal function call.
WSORT:          PHY
                JSR     LDR            ; the first
                PLY
                PHY
                TYA
                CLC
                ADC     #8
                TAY
                JSR     LDF            ; the second
                JSR     FCOMP          ; 1: the second the greater
                PLY
                CMP     #0
                BNE     :+
                JMP     FCERR          ; equal
:               BPL     @x
                LDX     #4
@w:             LDA     W,Y
                PHA
                LDA     W + 8,Y
                STA     W,Y
                PLA
                STA     W + 8,Y
                INY
                DEX
                BNE     @w
@x:             RTS

WINXY:          STA     WFL
                LDY     #WTX
                LDX     #RDL
                JSR     Z2W            ; x
                LDY     #WTY
                JSR     STF            ; y
                MOVW    WCX, RCL
                MOVW    WCY, RSI
                BIT     WFL
                BPL     @m
                MOVW    RDL, WCX       ; STEP: the cursor mapped back, the step added
                LDA     #0
                JSR     WINV
                LDY     #WTX
                JSR     WADD
                MOVW    RDL, WCY
                LDA     #1
                JSR     WINV
                LDY     #WTY
                JSR     WADD
@m:             LDY     #WTX
                JSR     LDF
                LDA     #0
                JSR     WFWD
                MOVW    WPX, RBL
                LDY     #WTY
                JSR     LDF
                LDA     #1
                JSR     WFWD
                MOVW    RDL, RBL
                MOVW    RCL, WPX
                RTS
; WADD: W + Y's single plus the FAC, back there.
WADD:           PHY
                JSR     LDR
                JSR     FADDS
                PLY
                ; (on into STF)

; STF: the FAC to W + Y; LDF: W + Y to the FAC (a single); LDR: to BXDX;
; W2Z, Z2W: 4 bytes between W + Y and zero page X on. Y past them.
STF:            LDX     #FACLO
Z2W:            LDA     #4
                STA     WN
@l:             LDA     0,X
                STA     W,Y
                INX
                INY
                DEC     WN
                BNE     @l
                RTS
LDF:            LDA     #VT_SNG
                STA     VALTYP
                LDX     #FACLO
                BRA     W2Z
LDR:            LDX     #RDL
W2Z:            LDA     #4
                STA     WN
@l:             LDA     W,Y
                STA     0,X
                INX
                INY
                DEC     WN
                BNE     @l
                RTS

; WAXB: Y A's axis's place (0 x, 1 y).
WAXB:           LDY     #WAX0
                CMP     #0
                BEQ     :+
                LDY     #WAX0 + 12
:               RTS

; WFWD: A the axis, the FAC v: BX = FRCINT((v - v0) x s).
WFWD:           JSR     WAXB
                JSR     LDR            ; v0
                PHY
                JSR     FSUBS          ; v0 - v
                PLY
                JSR     LDR            ; s
                JSR     FMULS
                LDA     FACM1          ; (v - v0) x s
                EOR     #$80
                STA     FACM1
                JMP     FRCINT

; WINV: A the axis, DX p: the FAC = (p - o) / s.
WINV:           STA     WAX
                JSR     FLT
                JSR     MOVRF          ; p
                LDA     WAX
                JSR     WAXB
                TYA
                CLC
                ADC     #8
                TAY
                JSR     LDF            ; o
                JSR     FSUBS          ; p - o
                JSR     MOVRF
                LDA     WAX
                JSR     WAXB
                INY
                INY
                INY
                INY
                JSR     LDF            ; s
                JMP     FDIVS

PMAPP:          STA     WN2
                LDA     GFLG
                LSR     A
                BCC     WFC2           ; not a graphics mode
                LDA     WN2
                CMP     #4
                BCS     WFC2
                JSR     MOVFR
                LDA     #VT_SNG
                STA     VALTYP
                LDA     WN2
                CMP     #2
                BCS     @inv
                BIT     GFLG           ; 0, 1: no window, the integer
                BMI     :+
                JMP     FRCINT
:               JSR     WFWD           ; (A the axis)
                LDX     WN2            ; + VIEW SCREEN's offset
                JSR     WOFFX
                CLC
                LDA     RBL
                ADC     W + WSX,X
                STA     RBL
                LDA     RBH
                ADC     T0
                STA     RBH
                JMP     MAKINT
@inv:           JSR     FRCINT         ; 2, 3: the integer physical point
                MOVW    RDL, RBL
                BIT     GFLG
                BMI     :+
                JMP     FLT            ; no window: as a single
:               LDA     WN2
                AND     #1
                TAX
                JSR     WOFFX
                SEC
                LDA     RDL
                SBC     W + WSX,X
                STA     RDL
                LDA     RDH
                SBC     T0
                STA     RDH
                LDA     WN2
                AND     #1
                JMP     WINV
WFC2:           JMP     FCERR
; WOFFX: X the axis (0, 1): X its offset's place from WSX (0, 2), T0 the
; offset's high byte (y's is 0).
WOFFX:          STZ     T0
                CPX     #0
                BNE     :+
                LDA     W + WSX + 1
                STA     T0
                RTS
:               LDX     #WSY - WSX
                RTS

WPNT:           STA     WAX
                MOVW    RDL, RCL
                LDA     WAX
                JMP     WINV

WINRAD:         LDY     #WAX0 + 4      ; x's s
                JSR     LDR
                JSR     FMULS
                JSR     FRCINT
                MOVW    RDL, RBL
                RTS

.segment "BRAM"
W:              .res    WEND           ; (above)
WFL:            .res    1              ; WINSET's flags, WINXY's STEP
WAX:            .res    1              ; an axis
WK:             .res    1              ;   its corners' offset
WV:             .res    1              ;   its v0's corner
WB:             .res    1              ;   its place
WN:             .res    1              ; a count
WN2:            .res    1              ; PMAP's n
WCX:            .res    2              ; WINXY's cursor
WCY:            .res    2
WPX:            .res    2              ;   its x, mapped

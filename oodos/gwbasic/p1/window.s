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
;           last x, RBL/RBH its last y, RAL/RAH and RCL/RCH VIEW SCREEN's
;           offset (7e5: y a word, past 255)
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
.import OVERR, GFLG, FSUBS, FADDS, FMULS, FDIVS, FRCINT, FLT, MOVFR, MOVRF, MAKINT

.macro  MOVW    dst, src
                LDA     src
                STA     dst
                LDA     src + 1
                STA     dst + 1
.endmacro

; W's layout (offsets): the corners, the axes (v0, s, o each), the viewport.
WC              = 0                    ; x1, y1, x2, y2 (4 bytes each)
WAX0            = 16                   ; x's v0, s, o; y's 12 on
WMX             = 40                   ; the viewport's last x, y, VIEW SCREEN's x, y (7e5: words)
WSX             = WMX + 4
WSY             = WMX + 6
WT              = 48                   ; WAXIS's span
WTX             = 52                   ; WINXY's x and y
WTY             = 56
WEND            = WTY + 4

.code

; WAXB: Y A's axis's place (0 x, 1 y).
WAXB:           LDY     #WAX0
                CMP     #0
                BEQ     WSRTS
                LDY     #WAX0 + 12
WSRTS:          RTS
WINSET:         STA     WFL            ; (review trim T1: checked and sorted by P8)
                LDA     GFLG
                AND     #$3F
                ORA     WFL
                STA     GFLG
                BPL     WSRTS          ; WINDOW alone: ended
                LDX     #15
@m:             LDA     RDL,X
                STA     W + WC,X
                DEX
                BPL     @m
                FJSR    GVWGET
WINRSC:         LDX     #7             ; (7e5: P5's GVWGET, four words)
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
@y:             MOVW    RDL, W + WMX + 2
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
                LDY     WB             ; (T2) v0 to its place
                LDX     #RDL
                JSR     Z2W
                JSR     FMULS
                LDA     FACM1          ; o = -(v0 x s)
                EOR     #$80
                STA     FACM1
                LDA     WB
                CLC
                ADC     #8
                TAY
                JMP     STF

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

; WINV: A the axis, DX p: the FAC = (p - o) / s. WPNT (T4) falls in.
WPNT:           LDX     RCL
                STX     RDL
                LDX     RCH
                STX     RDH
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

PMAPP:          STA     WN2            ; (T6: the mode and n checked by P8)
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
                LDA     WN2            ; + VIEW SCREEN's offset (T3)
                ASL     A
                TAX
                CLC
                LDA     RBL
                ADC     W + WSX,X
                STA     RBL
                LDA     RBH
                ADC     W + WSX + 1,X
                STA     RBH
                BVS     WOV            ; (review round 1, F2: past 32,767 Overflow)
                JMP     MAKINT
@inv:           JSR     FRCINT         ; 2, 3: the integer physical point
                MOVW    RDL, RBL
                BIT     GFLG
                BMI     :+
                JMP     FLT            ; no window: as a single
:               LDA     WN2
                AND     #1
                ASL     A
                TAX
                SEC
                LDA     RDL
                SBC     W + WSX,X
                STA     RDL
                LDA     RDH
                SBC     W + WSX + 1,X
                STA     RDH
                BVS     WOV
                LDA     WN2
                AND     #1
                JMP     WINV
WOV:            JMP     OVERR
WINRAD:         LDY     #WAX0 + 4      ; x's s
                JSR     LDR
                JSR     FMULS
                JMP     FRCINT         ; (T5: BX; P3 takes it)

.segment "BRAM"
W:              .res    WEND           ; (above)
WFL:            .res    1              ; WINSET's flags, WINXY's STEP
WAX:            .res    1              ; an axis
WN:             .res    1              ; a count
WN2             = WFL                  ; (T7) PMAP's n
WK              = W + WTX              ; (T7) WAXIS's: WTX free then
WV              = W + WTX + 1
WB              = W + WTX + 2
WCX             = W + WT               ; (T7) WINXY's cursor: WT free then
WCY             = W + WT + 2
WPX             = W + WTX              ; (T7) after WTX is read

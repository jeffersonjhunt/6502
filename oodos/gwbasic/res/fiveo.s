; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; FIVEO.ASM's WHILE and WEND, translated, resident (hw/oodos.md, "The plan,
; phase 7"). (FIVEO.ASM's CALL, CHAIN, COMMON and WRITE are later
; sub-phases'.)
;
;   WHILE    its WEND found (WNDSCN), any entry of this same WHILE dropped
;            with everything above it, the entry made, the formula tried
;   WEND     the WHILE's entry found, its formula tried again
;   FNDWND   the WHILE entry whose WEND is at DX, past any FOR
;
; The entry is on the BASIC stack (res/gwmain.s has its layout). The 8086
; pushes it a word at a time and the token last, only if the formula is
; true; here its room is taken at once and the token written last. Both
; leave SAVSTK as the 8086 does, and NEWSTT's return on the 6502's stack.

.include "gwbasic.inc"

.export WHILE, WEND, FNDWND
.import WNDSCN, CHRGTR, FRMEVL, VSIGN, SNERR, ERROR, FORADV, FORSSP, NXTLIN

ERRWE           = 30                   ; WEND without WHILE
FORSIZ          = 20
WHLSIZ          = 8

.code

; WHILE (FIVEO.ASM 59): TXTPTR at the formula.
WHILE:          LDA     TXTPTR         ; ENDFOR: the formula's text
                STA     WHLTXT
                LDA     TXTPTR+1
                STA     WHLTXT+1
                JSR     WNDSCN         ; to the WEND, or WHILE without WEND
                JSR     CHRGTR         ; the text after it
                LDA     TXTPTR
                STA     RDL
                LDA     TXTPTR+1
                STA     RDH
                JSR     FNDWND         ; this WHILE's entry already?
                BCC     WNOTOL
                LDA     #WHLSIZ        ; it and everything above it go
                JSR     FORADV
                JSR     FORSSP
WNOTOL:         SEC                    ; the entry: CURLIN, the formula, the WEND
                LDA     SSP
                SBC     #WHLSIZ
                STA     SSP
                STA     FORPTR
                LDA     SSP+1
                SBC     #0
                STA     SSP+1
                STA     FORPTR+1
                LDY     #6
                LDA     CURLIN
                STA     (FORPTR),Y
                INY
                LDA     CURLIN+1
                STA     (FORPTR),Y
                LDY     #4
                LDA     WHLTXT
                STA     (FORPTR),Y
                INY
                LDA     WHLTXT+1
                STA     (FORPTR),Y
                LDY     #2
                LDA     RDL
                STA     (FORPTR),Y
                INY
                LDA     RDH
                STA     (FORPTR),Y
                LDA     WHLTXT         ; the formula
                STA     TXTPTR
                LDA     WHLTXT+1
                STA     TXTPTR+1
                BRA     FNWEND

; WEND (FIVEO.ASM 84): nothing may follow it. The entry whose WEND this is,
; or WEND without WHILE; NXTLIN keeps WEND's line, CURLIN the WHILE's.
WEND:           BEQ     :+
                JMP     SNERR
:               LDA     TXTPTR
                STA     RDL
                LDA     TXTPTR+1
                STA     RDH
                JSR     FNDWND
                BCS     :+
                LDA     #ERRWE
                JMP     ERROR
:               LDA     FORPTR         ; SSP at it; SAVSTK past its token, as
                STA     SSP            ; the 8086 leaves it
                LDA     FORPTR+1
                STA     SSP+1
                CLC
                LDA     FORPTR
                ADC     #2
                STA     SAVSTK+1
                LDA     FORPTR+1
                ADC     #0
                STA     SAVSTK+2
                LDA     CURLIN
                STA     NXTLIN
                LDA     CURLIN+1
                STA     NXTLIN+1
                LDY     #6             ; the WHILE's line, for errors
                LDA     (FORPTR),Y
                STA     CURLIN
                INY
                LDA     (FORPTR),Y
                STA     CURLIN+1
                LDY     #4             ; and its formula
                LDA     (FORPTR),Y
                STA     TXTPTR
                INY
                LDA     (FORPTR),Y
                STA     TXTPTR+1
; FNWEND: true, the token put in (the entry whole) and on after the formula;
; false, the entry dropped and on after the WEND, in its line.
FNWEND:         JSR     FRMEVL
                JSR     VSIGN
                BEQ     FLSWHL
                LDY     #1
                LDA     #WHILE_TK
                STA     (FORPTR),Y
                RTS                    ; NEWSTT
FLSWHL:         LDA     NXTLIN
                STA     CURLIN
                LDA     NXTLIN+1
                STA     CURLIN+1
                LDY     #2
                LDA     (FORPTR),Y
                STA     TXTPTR
                INY
                LDA     (FORPTR),Y
                STA     TXTPTR+1
                CLC
                LDA     FORPTR
                ADC     #WHLSIZ
                STA     SSP
                LDA     FORPTR+1
                ADC     #0
                STA     SSP+1
                RTS                    ; NEWSTT

; FNDWND (FIVEO.ASM 131): C set and FORPTR at the WHILE entry whose WEND
; text is DX, past any FOR entries; C clear at anything else.
FNDWND:         LDA     SSP
                STA     FORPTR
                LDA     SSP+1
                STA     FORPTR+1
FNDWN2:         LDY     #1
                LDA     (FORPTR),Y
                CMP     #FOR_TK
                BNE     FNDWN3
                LDA     #FORSIZ
FNDWNX:         JSR     FORADV
                BRA     FNDWN2
FNDWN3:         CMP     #WHILE_TK
                BEQ     :+
                CLC
                RTS
:               INY                    ; the same WEND?
                LDA     (FORPTR),Y
                CMP     RDL
                BNE     @no
                INY
                LDA     (FORPTR),Y
                CMP     RDH
                BNE     @no
                SEC
                RTS
@no:            LDA     #WHLSIZ
                BRA     FNDWNX

.segment "RAM"
WHLTXT:         .res    2              ; WHILE's formula (the 8086's ENDFOR)

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; NEXT86.ASM, translated: NEXT, resident (hw/oodos.md, "The plan, phase 7").
;
;   NEXT     the statement: NEXT [v[,v...]]
;   NEXTS    FOR's way in, with the start value (NXTFLG 0): the loop's first
;            test, so a loop that runs no times goes on past its NEXT
;
; The FOR entry is on the BASIC stack (res/gwmain.s has its layout). The
; 8086 cuts its stack at the entry, re-pushes parts of it while it works and
; pushes the token back to loop (NXTCON); here the entry stays where it is,
; SSP at it, and the values are read from it by FORPTR. NEWSTT's return
; stays on the 6502's stack: NEXT returns to it.

.include "gwbasic.inc"

.export NEXT, NEXTS
.import PTRGET, FNDFOR, FORADV, FORSSP, FVALSV, FADDS, FCOMP, ICOMP, NFERR
.import OVERR, CHRGTR

.code

NF1:            JMP     NFERR

; NEXT (NEXT86.ASM 49): NXTFLG 1, the step added. NEXTS: NXTFLG 0, the
; value is FOR's start. Z set: no variable named (any FOR will do: DX 0).
NEXT:           PHP
                LDA     #1
                STA     NXTFLG
                PLP
                BRA     NEXTS1
NEXTS:          STZ     NXTFLG
NEXTS1:         STZ     RDL
                STZ     RDH
; NEXTC: the next variable's FOR entry; it must be the one whose NEXT is
; this one (NXTTXT), else NEXT without FOR.
NEXTC:          PHP
                LDA     TXTPTR         ; NXTTXT: where this NEXT's variable starts
                STA     NXTTXT
                LDA     TXTPTR+1
                STA     NXTTXT+1
                PLP
                BEQ     NEXTC1
                JSR     PTRGET
                LDA     PTR
                STA     RDL
                LDA     PTR+1
                STA     RDH
NEXTC1:         LDA     TXTPTR         ; the text after it (the 8086's TEMP)
                STA     NXTTMP
                LDA     TXTPTR+1
                STA     NXTTMP+1
                JSR     FNDFOR
                BCC     NF1
                LDY     #4             ; this NEXT's FOR?
                LDA     (FORPTR),Y
                CMP     NXTTXT
                BNE     NF1
                INY
                LDA     (FORPTR),Y
                CMP     NXTTXT+1
                BNE     NF1
                LDA     FORPTR         ; the stack cut at the entry (SAVSTK
                STA     SSP            ; isn't, until the loop is done)
                LDA     FORPTR+1
                STA     SSP+1
                LDA     RDL            ; the loop variable
                STA     NXTVAR
                LDA     RDH
                STA     NXTVAR+1
                LDY     #7             ; an integer loop?
                LDA     (FORPTR),Y
                BMI     INTNXT
                LDY     #8             ; single: the step to the FAC
                LDX     #0
@st:            LDA     (FORPTR),Y
                STA     FACLO,X
                INY
                INX
                CPX     #4
                BNE     @st
                JSR     NXTPTR
                LDA     NXTFLG
                BNE     NXTDO
                LDX     #3             ; FOR's: the start instead
@sv:            LDA     FVALSV,X
                STA     FACLO,X
                DEX
                BPL     @sv
                BRA     NXTDOA
NXTDO:          LDY     #0             ; FADDS: the variable to BXDX, added
                LDX     #0
@v:             LDA     (PTR),Y
                STA     RDL,X
                INY
                INX
                CPX     #4
                BNE     @v
                JSR     FADDS
                JSR     NXTPTR         ; (an Overflow message may move PTR)
NXTDOA:         LDY     #0             ; the FAC to the variable
                LDX     #0
@f:             LDA     FACLO,X
                STA     (PTR),Y
                INY
                INX
                CPX     #4
                BNE     @f
                LDY     #15            ; the limit to BXDX, compared
                LDX     #3
@l:             LDA     (FORPTR),Y
                STA     RDL,X
                DEY
                DEX
                BPL     @l
                JSR     FCOMP          ; A: 1, 0 or $FF, the FAC against it
                BRA     FINNXT
INTNXT:         LDY     #12            ; integer: the step to CX
                LDA     (FORPTR),Y
                STA     RCL
                INY
                LDA     (FORPTR),Y
                STA     RCH
                JSR     NXTPTR
                LDA     NXTFLG
                BNE     INXTDO
                LDA     FVALSV         ; FOR's: the start
                STA     RDL
                LDA     FVALSV+1
                STA     RDH
                BRA     IFORIN
INXTDO:         CLC                    ; the variable plus the step
                LDA     (PTR)
                ADC     RCL
                STA     RDL
                LDY     #1
                LDA     (PTR),Y
                ADC     RCH
                STA     RDH
                BVS     OV1
IFORIN:         LDA     RDL            ; to the variable, and to BX (the
                STA     (PTR)          ; 8086's AX)
                STA     RBL
                LDY     #1
                LDA     RDH
                STA     (PTR),Y
                STA     RBH
                LDY     #14            ; the limit to DX, compared (ICMPA)
                LDA     (FORPTR),Y
                STA     RDL
                INY
                LDA     (FORPTR),Y
                STA     RDH
                JSR     ICOMP          ; A: 1 if BX is the greater
; FINNXT: done when the compare's sign less the step's is zero.
FINNXT:         SEC
                LDY     #6
                SBC     (FORPTR),Y
                BEQ     LOOPDN
                LDY     #16            ; not done: on after the FOR, in its line
                LDA     (FORPTR),Y
                STA     CURLIN
                INY
                LDA     (FORPTR),Y
                STA     CURLIN+1
                INY
                LDA     (FORPTR),Y
                STA     TXTPTR
                INY
                LDA     (FORPTR),Y
                STA     TXTPTR+1
                RTS                    ; NXTCON: NEWSTT
; LOOPDN: the entry dropped, the stack saved; past the NEXT's variable, and
; a comma names another, with a DX no entry has (SSP, as the 8086's SP).
LOOPDN:         LDA     #20            ; FORSIZ
                JSR     FORADV
                JSR     FORSSP
                LDA     NXTTMP
                STA     TXTPTR
                LDA     NXTTMP+1
                STA     TXTPTR+1
                LDA     (TXTPTR)
                CMP     #','
                BNE     NS1
                LDA     #$FF           ; LOOPD1: not FOR's NEXT any more
                STA     NXTFLG
                JSR     CHRGTR
                PHP
                LDA     SSP
                STA     RDL
                LDA     SSP+1
                STA     RDH
                PLP
                JMP     NEXTC
OV1:            JMP     OVERR
NS1:            RTS                    ; NEWSTT

; NXTPTR: PTR at the loop variable.
NXTPTR:         LDA     NXTVAR
                STA     PTR
                LDA     NXTVAR+1
                STA     PTR+1
                RTS

.segment "RAM"
; GWDATA.ASM's
NXTFLG:         .res    1              ; 0: FOR's first test; else NEXT's
NXTTXT:         .res    2              ; where this NEXT's variable starts
NXTTMP:         .res    2              ; the text after it
NXTVAR:         .res    2              ; the loop variable

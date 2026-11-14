; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWSTS.ASM's VARPTR$, in block P3 (hw/oodos.md, "The plan, phase 7"),
; reached from the resident VARPTR (res/gweval.s) by its stub (res/far3.s):
;
;   VARPT2P   VARPTR$(v): a 3-byte string, v's type and the address of its
;             value (low, high), as VARPTR gives it
;
; It returns by PUTNEW, a tail call into resident code that returns to EVAL's
; caller (the far call rule).

.include "gwbasic.inc"

;@entry VARPT2P
.export VARPT2P
.import CHRGTR, SYNCHR, PTRGTN, STRINI, PUTNEW, FCERR

.code

; --- VARPT2 (GWSTS.ASM 512) --------------------------------------------------------------

; TXTPTR at the "$": "(", the variable (PTRGTN: PTR 0 if there is none, an
; Illegal function call), ")". The type and the address go on the 6502's
; stack round STRINI (which may collect), as on the 8086's.
VARPT2P:        JSR     CHRGTR         ; past the "$"
                JSR     SYNCHR
                .byte   '('
                JSR     PTRGTN
                JSR     SYNCHR
                .byte   ')'
                LDA     PTR            ; undefined: Illegal function call
                ORA     PTR+1
                BNE     VARRT2
                JMP     FCERR
; VARRT2 (GWSTS.ASM 528)
VARRT2:         LDA     PTR+1          ; the address
                PHA
                LDA     PTR
                PHA
                LDA     VALTYP         ; the type
                PHA
                LDA     #3
                JSR     STRINI         ; DX: 3 bytes (DSCTMP's address)
                PLA
                STA     (RDL)          ; byte 0: the type
                LDY     #1
                PLA
                STA     (RDL),Y        ; bytes 1 and 2: the address
                INY
                PLA
                STA     (RDL),Y
                JMP     PUTNEW         ; the FAC at it

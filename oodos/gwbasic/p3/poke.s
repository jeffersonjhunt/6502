; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM's POKE, in block P3 (hw/oodos.md, "The plan, phase 7": 7c's
; tail), reached by its home block stub (p8/far3.s):
;
;   POKEP     POKE address, byte: the byte to DEF SEG's segment x 16 + the
;             address
;
; As PEEK (p1/peek.s): straight to the CPU's own address under segment 0
; below $C000, else by P5's DSBYTE (p5/seed.s), the OEM's POKFLT's place;
; there a byte OODOS's function 83 won't write, and bank 8 outside the
; screen page (7d3: in screen mode the active page, up to its reach), are
; Illegal function call.

.include "gwbasic.inc"

;@entry POKEP
.export POKEP
.import FRMQNT, PRODIR, SYNCHR, GETBYT, SAVSEG

.code

; POKEP (GWMAIN.ASM 3062): the address (FRMQNT: DX), kept on the 6502's
; stack as the 8086 keeps it on its own; PRODIR; ","; the byte (GETBYT).
POKEP:          JSR     FRMQNT
                LDA     RDH            ; PUSH DX
                PHA
                LDA     RDL
                PHA
                JSR     PRODIR
                JSR     SYNCHR
                .byte   ','
                JSR     GETBYT         ; A
                PLX                    ; POP DX
                STX     RDL
                PLX
                STX     RDH
                LDX     SAVSEG         ; segment 0 below $C000: the CPU's own
                BNE     @far
                LDX     SAVSEG+1
                BNE     @far
                LDX     RDH
                CPX     #$C0
                BCS     @far
                STA     (RDL)          ; STOSB
                RTS
@far:           SEC                    ; DSBYTE: C set a POKE of A
                FJSR    DSBYTE
                RTS

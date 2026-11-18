; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWMAIN.ASM's PEEK, in block P1 (hw/oodos.md, "The plan, phase 7": 7c's
; tail), beside FRQINT (p1/strfn.s), reached from FUNDSP by the resident
; F_PEEK (res/bistrs.s), back through LABBCK:
;
;   PEEKP     PEEK(address): the byte at DEF SEG's segment x 16 + the
;             address, as an integer
;
; The 8086 loads ES from SAVSEG and asks the OEM's PEKFLT first. Here the
; byte comes straight from the CPU's own address when the segment is 0 and
; the address is below $C000 (the program's blocks 0-2: VARPTR's results);
; any other comes from P5's DSBYTE, the OEM's router (p5/seed.s), one far
; call more: blocks 0-2 by another segment, an allocated block or banks 9-B
; by GX_BLOCK, the rest by function 83, bank 8 Illegal function call. IBM's
; BASVAR words, which PEKFLT gave in place of some of BASIC's own, aren't
; given (the owner, 2026-11-18: ledger.txt): VARPTR serves.

.include "gwbasic.inc"

;@entry PEEKP
.export PEEKP
.import FRQINT, SNGFLT, FCERR, SAVSEG

.code

; PEEKP: the FAC (PARCHK's) the address; FRQINT: -32768..65535, unsigned.
; PRODIR inline: a protected program's direct line is Illegal function
; call (GIODSK.ASM's; P3 has it, P1 not).
PEEKP:          JSR     FRQINT         ; BX
                LDA     CURLIN         ; PRODIR: direct (CURLIN $FFFF)
                AND     CURLIN+1
                INC     A
                BNE     @seg
                LDA     PROFLG         ; and protected
                BEQ     @seg
                JMP     FCERR
@seg:           LDA     SAVSEG         ; segment 0 below $C000: the CPU's own
                ORA     SAVSEG+1
                BNE     @far
                LDA     RBH
                CMP     #$C0
                BCS     @far
                LDA     (RBL)
                JMP     SNGFLT         ; PEKVAL
@far:           LDA     RBL            ; DSBYTE: DX the address, C clear a PEEK
                STA     RDL
                LDA     RBH
                STA     RDH
                CLC
                FJSR    DSBYTE
                JMP     SNGFLT

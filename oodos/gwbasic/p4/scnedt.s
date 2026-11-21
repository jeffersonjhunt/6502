; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; SCNEDT.ASM's DSKCHI, translated, in block P4, the file layer (hw/oodos.md,
; "The plan, phase 7": 7c's LOAD step): a program line from a text file
; being loaded or merged (file 0), for MAIN (p8/gwmain.s), which reads it
; instead of a typed one while PTRFIL is a file, as the 8086's PINLIN does.
;
;   DSKCHP          the line into BUF, its CR dropped, to a zero; TXTPTR at
;                   BUF-1 (BUFMIN); C set back if the file's end runs the
;                   program (INCHR's NOTCHN, p4/gio86.s: LDRUN)
;
; The whole line comes back in one far call (V1: the device interface takes
; runs), where the 8086 reads it by INCHR in PINLIN itself. It returns by
; RTS, or ends in an error.

.include "gwbasic.inc"

;@entry DSKCHP
;@entry ERDVGT
;@entry FILESQ
;@entry PRGFLI
;@entry CHNSTP
;@entry SAVFGT
; (ERDVGT: p4/giodsk.s's, OO-1, after DSKCHP so P4's other entries keep their places; PRGFLI, p4/dskcom.s's, and
; CHNSTP and SAVFGT, p4/fiveo.s's, the home block's p8/chain.s's far calls since F3's M4, after them, for the same)
.export DSKCHP
.import INCHR, LDRUN, BUF, ERROR, ERDVGT, FILESQ

.code

; DSKCHP (SCNEDT.ASM 408, ISLOAD): bytes to BUF (DCIDX the 8086's BX, DCCNT
; its CL: room for BUFLEN), to a CR. A CR after an LF is dropped, and so is
; an LF that starts the line; a line longer than BUFLEN is Line buffer
; overflow (a file BASIC didn't write).
DSKCHP:         STZ     LDRUN
                LDA     #BUFLEN
                STA     DCCNT
                STZ     DCIDX
LOPBUF:         JSR     INCHR          ; (the file's end: INCHR's)
                LDX     DCIDX
                STA     BUF,X
                BIT     LDRUN          ; the end runs the program: at once, as
                BMI     FINLIN         ; the 8086's NOTCHN jumps to RUNC (W4)
                CMP     #13
                BNE     INOTCR
                LDA     BUF-1,X        ; after an LF: dropped (BUF-1 is BUFMIN, a comma)
                CMP     #10
                BEQ     LOPBUF
                BRA     FINLIN
INOTCR:         LDY     DCCNT
                BEQ     LTLONG
                CMP     #10            ; a leading LF: dropped
                BNE     INOTLF
                CPY     #BUFLEN
                BEQ     LOPBUF
INOTLF:         INC     DCIDX
                DEC     DCCNT
                BRA     LOPBUF
FINLIN:         STZ     BUF,X
                LDA     #<(BUF-1)
                STA     TXTPTR
                LDA     #>(BUF-1)
                STA     TXTPTR+1
                LDA     LDRUN          ; C: run (OO-1)
                ASL     A
                RTS
LTLONG:         LDA     #LBOERN
                JMP     ERROR

.segment "BRAM"
DCIDX:          .res    1              ; the place in BUF
DCCNT:          .res    1              ; the room left

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; FIVEO.ASM's WRITE, in block P3 (hw/oodos.md, "The plan, phase 7"),
; reached by its resident stub (res/far3.s):
;
;   WRITEP    WRITE [list]: the values with commas between them, strings in
;             quotes, numbers as $FOUT gives them without the positive sign's
;             space; then the line's end
;
; WRITE # is a file's (7c): P4's FILGET sets PTRFIL to it, for output, a
; far call made only for a "#" (its first test is here), and OUTDO sends
; the output there. WRTFIN pads a random file's record (P4's CMPFBP). The 8086 keeps the text pointer on
; its stack round $FOUT and STRLIT (which reads at the text pointer); here
; the 6502's stack, as P1's PRINT does.

.include "gwbasic.inc"

;@entry WRITEP
.export WRITEP
.import CHRGTR, SYNCHR, FRMEVL, GETYP, STRLIT, STRPRT, OUTDO, CRDO, FINPRT

.code

; --- WRITE (FIVEO.ASM 723) ---------------------------------------------------------------

WRITEP:         CMP     #'#'           ; FILGET's own first test
                BNE     WRTCHR
                LDX     #MD_SQO
                STX     RCL
                FJSR    FILGET
; WRTCHR (FIVEO.ASM 728)
WRTCHR:         DECW    TXTPTR
                JSR     CHRGTR
                JEQ     WRTFIN         ; WRITE alone: the line's end
; WRTMLP (FIVEO.ASM 731): a value.
WRTMLP:         JSR     FRMEVL
                LDA     TXTPTR+1       ; the text pointer kept
                PHA
                LDA     TXTPTR
                PHA
                JSR     GETYP
                BEQ     WRTSTR         ; a string
                FJSR    FOUT           ; a number: $FOUT's text at FBPTR
                LDA     FBPTR          ; STRLIT reads at TXTPTR
                STA     TXTPTR
                LDA     FBPTR+1
                STA     TXTPTR+1
                JSR     STRLIT         ; the FAC at its descriptor
                LDY     #1             ; DX: the text
                LDA     (FACLO),Y
                STA     RDL
                INY
                LDA     (FACLO),Y
                STA     RDH
                LDA     (RDL)          ; a positive number's space?
                CMP     #' '
                BNE     WRTNEG
                INCW    RDL            ; past it, the length one less
                LDY     #2
                LDA     RDH
                STA     (FACLO),Y
                DEY
                LDA     RDL
                STA     (FACLO),Y
                LDA     (FACLO)
                DEC     A
                STA     (FACLO)
; WRTNEG (FIVEO.ASM 748)
WRTNEG:         JSR     STRPRT
; NXTWRV (FIVEO.ASM 749): a comma or ";" and the next, or the end.
NXTWRV:         PLA
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     WRTFIN
                CMP     #';'
                BEQ     WASEMI
                JSR     SYNCHR
                .byte   ','
                DECW    TXTPTR         ; for the CHRGTR next
; WASEMI (FIVEO.ASM 759): a comma out, whichever it was.
WASEMI:         JSR     CHRGTR
                LDA     #','
                JSR     OUTDO
                BRA     WRTMLP
; WRTSTR (FIVEO.ASM 763): a string, in quotes.
WRTSTR:         LDA     #'"'
                JSR     OUTDO
                JSR     STRPRT
                LDA     #'"'
                JSR     OUTDO
                BRA     NXTWRV
; WRTFIN (FIVEO.ASM 769): a random file's record padded with spaces to two
; bytes from its end, then CR LF and the console again. The test of PTRFIL's
; file (a disk's, random) and CMPFBC are P4's (CMPFBP, a far call, which
; sends OUTDO's run first): C set, BX the spaces. The 8086 counts BX down
; to 0 after each: with none (two bytes left) it goes round 65536 times, so
; the record overflows (FIELD overflow), as in the source.
WRTFIN:         FJSR    CMPFBP
                BCC     NTRNDW
NXTWSP:         LDA     #' '
                JSR     OUTDO
                DECW    RBL
                LDA     RBL
                ORA     RBH
                BNE     NXTWSP
NTRNDW:         JSR     CRDO
                JMP     FINPRT

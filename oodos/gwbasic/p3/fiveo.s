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
; WRITE # (FILGET, a file's) is 7c's: Advanced Feature, as PRINT # gives in
; 7a. So PTRFIL is never a file here, and WRTFIN's padding of a random
; file's record (CMPFBC) is 7c's too. The 8086 keeps the text pointer on
; its stack round $FOUT and STRLIT (which reads at the text pointer); here
; the 6502's stack, as P1's PRINT does.

.include "gwbasic.inc"

;@entry WRITEP
.export WRITEP
.import CHRGTR, SYNCHR, FRMEVL, GETYP, STRLIT, STRPRT, OUTDO, CRDO, FINPRT
.import ADVERR

.code

; --- WRITE (FIVEO.ASM 723) ---------------------------------------------------------------

WRITEP:         CMP     #'#'           ; FILGET: a file's (7c)
                BNE     WRTCHR
                JMP     ADVERR
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
; WRTFIN (FIVEO.ASM 769): a random file's record padded (NTRNDW's way past
; it: 7c's), then CR LF and the console again.
WRTFIN:
NTRNDW:         JSR     CRDO
                JMP     FINPRT

; Stand-ins, the project's own: what the translated code calls but 7a hasn't
; translated yet, so it links and runs (the spike's stubs.s, at first). Each
; goes as its routine is translated; what is left at 7a's end is listed in the
; ledger (ledger.txt) as later sub-phases' work. (The errors, END, OUTDO,
; EVALX, OCTCNS, NEWSTX, ISMID and CHKINT went at step I1.)
;
;   PRINT         numbers and strings, with ";" between them
;   NOTSPK        Illegal function call, for a 7a statement not translated yet
;   the rest      USR, FN and the event traps' GOSUB: Illegal function call,
;                 or nothing

.include "gwbasic.inc"

.export NOTSPK, STROUI, PRINT, USRFN, FNDOER, GOTRP, FBUFF
.import CHRGTR, CHRGT2, FRMEVL, STROUT, GETYP, OUTDO, STRPRT, FCERR

.code

; The untranslated routines.
USRFN:
FNDOER:
NOTSPK:         JMP     FCERR
GOTRP:          RTS

CRLF:           LDA     #13
                JSR     OUTDO
                LDA     #10
                JMP     OUTDO

; STROUI: print $FOUT's string, at FBPTR, from its second character (BISTRS.ASM's
; STROUI steps over the sign's place, then prints it as a string literal): LINPRT's.
; FOUTPR: all of it, the stand-in PRINT's.
STROUI:         CLC
                LDA     FBPTR
                ADC     #1
                STA     PTR
                LDA     FBPTR+1
                ADC     #0
                STA     PTR+1
                JMP     STROUT
FOUTPR:         LDA     FBPTR
                STA     PTR
                LDA     FBPTR+1
                STA     PTR+1
                JMP     STROUT

; PRINT: numbers, each followed by a space as GW-BASIC's are, and strings
; (STRPRT); ";" keeps the line going.
PRINT:          BEQ     @crlf
@item:          CMP     #';'
                BEQ     @semi
                JSR     FRMEVL
                JSR     GETYP
                BNE     @num
                JSR     STRPRT
                BRA     @next
@num:           FJSR    FOUT
                JSR     FOUTPR
                LDA     #' '
                JSR     OUTDO
@next:          JSR     CHRGT2
                BEQ     @crlf
                BRA     @item
@semi:          JSR     CHRGTR
                BNE     @item
                RTS
@crlf:          JMP     CRLF

.segment "RAM"

FBUFF:          .res    32             ; $FOUT's output

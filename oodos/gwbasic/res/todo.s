; Stand-ins, the project's own: what the translated code calls but 7a hasn't
; translated yet, so it links and runs (the spike's stubs.s, at first). Each
; goes as its routine is translated; what is left at 7a's end is listed in the
; ledger (ledger.txt) as later sub-phases' work.
;
;   errors        print "?ERR n IN line" and stop, or hand the number to the
;                 harness (ERRHOOK) when it is running the math vectors
;   OUTDO         one character to the console, through the BDOS
;   PTRGET        single-letter variables, A-Z with %, ! or nothing (single)
;   PRINT         numbers only, with ";" between them
;   END           stop: a warm boot through the BDOS
;   the rest      Illegal function call

.include "gwbasic.inc"

.export TMERR, OVERR, DV0ERR, SNERR, FCERR, MOERR, OMERR, ERROR, NOTSPK
.export CRFIN, STROUI, PTRGET, PRINT, ENDST, PRGEND, LETSTR
.export CAT, STRCMP
.export EVALX, STRLTI, OCTCNS, VARPTR, USRFN, INSTR, FNDOER, GETBYT
.export NEWSTX, ISMID, GOTRP, CHKINT, FBUFF, VARS, ERRHOOK
.import CHRGTR, CHRGT2, FRMEVL, STROUT, GETYP, OUTDO

.code

SNERR:          LDA     #2
                BRA     ERROR
FCERR:
NOTSPK:         LDA     #5
                BRA     ERROR
OVERR:          LDA     #6
                BRA     ERROR
OMERR:          LDA     #7
                BRA     ERROR
DV0ERR:         LDA     #11
                BRA     ERROR
TMERR:          LDA     #13
                BRA     ERROR
MOERR:          LDA     #22
ERROR:          STA     ERRFLG
                LDX     ERRHOOK+1      ; the harness takes it?
                BEQ     :+
                JMP     (ERRHOOK)
:               LDA     #<ERRMSG
                STA     PTR
                LDA     #>ERRMSG
                STA     PTR+1
                JSR     STROUT
                LDA     ERRFLG
                STA     RBL
                STZ     RBH
                FJSR    LINOUT
                JSR     STROUI
                LDA     #<INMSG
                STA     PTR
                LDA     #>INMSG
                STA     PTR+1
                JSR     STROUT
                LDA     CURLIN
                STA     RBL
                LDA     CURLIN+1
                STA     RBH
                FJSR    LINOUT
                JSR     STROUI
                JSR     CRLF
ENDST:
PRGEND:         LDY     #P_TERMCPM
                JMP     BDOS

; The untranslated routines.
CAT:
STRCMP:
STRLTI:
OCTCNS:
VARPTR:
USRFN:
INSTR:
FNDOER:
GETBYT:
NEWSTX:
ISMID:
LETSTR:         JMP     NOTSPK
EVALX:
GOTRP:
CHKINT:
CRFIN:          RTS

CRLF:           LDA     #13
                JSR     OUTDO
                LDA     #10
                JMP     OUTDO

; STROUI: print $FOUT's string, at FBPTR.
STROUI:         LDA     FBPTR
                STA     PTR
                LDA     FBPTR+1
                STA     PTR+1
                JMP     STROUT

; PTRGET: PTR = the variable named at TXTPTR, VALTYP its type; returns past
; the name with CHRGTR's A and flags.
PTRGET:         LDA     (TXTPTR)
                SEC
                SBC     #'A'
                ASL     A
                ASL     A
                ASL     A
                CLC
                ADC     #<VARS
                STA     PTR
                LDA     #>VARS
                ADC     #0
                STA     PTR+1
                LDA     #VT_SNG
                STA     VALTYP
                JSR     CHRGTR
                CMP     #'!'
                BEQ     @sng
                CMP     #'%'
                BNE     @ret
                LDA     #VT_INT        ; integers at +4
                STA     VALTYP
                CLC
                LDA     PTR
                ADC     #4
                STA     PTR
                BCC     @sng
                INC     PTR+1
@sng:           JMP     CHRGTR
@ret:           RTS

; PRINT: numbers, each followed by a space as GW-BASIC's are; ";" keeps the
; line going.
PRINT:          BEQ     @crlf
@item:          CMP     #';'
                BEQ     @semi
                JSR     FRMEVL
                FJSR    FOUT
                JSR     STROUI
                LDA     #' '
                JSR     OUTDO
                JSR     CHRGT2
                BEQ     @crlf
                BRA     @item
@semi:          JSR     CHRGTR
                BNE     @item
                RTS
@crlf:          JMP     CRLF

.rodata
ERRMSG:         .byte   "?ERR", 0
INMSG:          .byte   " IN", 0

.segment "RAM"

FBUFF:          .res    32             ; $FOUT's output
VARS:           .res    26 * 8         ; PTRGET's variables
ERRHOOK:        .res    2              ; the harness's error entry, or 0

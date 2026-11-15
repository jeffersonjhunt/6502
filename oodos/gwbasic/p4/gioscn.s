; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIOSCN.ASM's SCRN: device, translated, in block P4 (hw/oodos.md, "The plan,
; phase 7": 7c, the devices as files): a file OPENed on "SCRN:" for output,
; to the console through BASIC's own output, its column and width kept.
;
;   SCNDSP          its functions, by GIO86U's G_* (TBLDSP's table)
;   SCNINI          its start (GIOINI's): the width the console's (WDTFLG)
;   SCNOPN          OPEN: output only, the width WDTFLG, the console's column
;   SCNSOT SCNOL1   a byte out: a CR as CR LF, an LF straight after a CR
;   SCNOL2          dropped (LSTCHR), a CR LF first at the file's width
;   SCNGPS SCNGWD   the column (0 relative, within the line), the width
;
; OO-1, until the screen (7d): SCNOUT, the screen editor's output, is the
; console's (OUTDO with PTRFIL 0, as CALTTY does), whose column is TTYPOS
; (the 8086's CSRX is TTYPOS + 1) and which wraps at the line's end itself,
; so SCNOL2 has no cursor to move back there; the two-byte characters
; (TWOBYT, the editor's F_EDIT) are 7d's; SCNSWD, WIDTH's, is 7d's (the
; screen editor's SWIDTH), with WIDTH. The keyboard's file (p4/giokyb.s)
; writes and asks its column here, as the 8086's KYBDSP does.

.include "gwbasic.inc"

.export SCNDSP, SCNINI, SCNSWD, SCNSOT, SCNGPS, SCNGWD, SCNSCW, SCNGCW
.import DERBFM, INIFDB, DEVBIN, DEVBOT, OUTDO, LINLEN, ADVERR

.rodata

; SCNDSP: the screen's functions, by G_*.
SCNDSP:         .word   DERBFM         ; G_EOF
                .word   DERBFM         ; G_LOC
                .word   DERBFM         ; G_LOF
                .word   SCNCLS         ; G_CLS
                .word   SCNSWD         ; G_SWD
                .word   DERBFM         ; G_RND
                .word   SCNOPN         ; G_OPN
                .word   DERBFM         ; G_SIN
                .word   SCNSOT         ; G_SOT
                .word   SCNGPS         ; G_GPS
                .word   SCNGWD         ; G_GWD
                .word   SCNSCW         ; G_SCW
                .word   SCNGCW         ; G_GCW
                .word   DEVBIN         ; G_BIN
                .word   DEVBOT         ; G_BOT

.code

; SCNINI (GIOSCN.ASM 76): SCRN:'s width, the console's line.
SCNINI:         LDA     LINLEN
                STA     WDTFLG
; SCNCLS (93), SCNSCW, SCNGCW: nothing to do.
SCNCLS:
SCNSCW:
SCNGCW:         RTS

; SCNSWD (99): WIDTH's (7d's, the screen editor's SWIDTH).
SCNSWD:         JMP     ADVERR

; SCNOPN (138): A the device id: output only, the width WDTFLG, the column
; the console's (SCNGPS).
SCNOPN:         PHA
                JSR     SCNGPS         ; DH: the column
                STA     RDH
                LDA     WDTFLG         ; DL: the width
                STA     RDL
                PLA
                LDX     #MD_SQO
                JMP     INIFDB

; SCNSOT (174): A out to FDBP's file (OO-1: no two-byte character: 7d's),
; through SCNOL1. A kept.
SCNSOT:         PHA
                JSR     SCNOL1
                PLA
                RTS

; SCNOL1 (217): a CR goes as CR LF; an LF straight after a CR is dropped.
SCNOL1:         LDX     LSTCHR         ; the last
                STA     LSTCHR
                CPX     #13
                BNE     NTCRLF
                CMP     #10
                BEQ     RET22          ; an LF after a CR: eaten
NTCRLF:         PHA
                JSR     SCNOL2
                PLA
                CMP     #13
                BNE     RET22
                LDA     #10            ; a CR: an LF after it
                JSR     SCNOL2
RET22:          RTS

; SCNOL2 (242): A out (SI is always a file's here): not a CR, and the next
; column (CSRX, 1 relative) past the file's width: a CR LF first, unless the
; width is the line's (the console wraps there). OO-1: past the line's end
; the console has wrapped already (no CSRX to move back).
SCNOL2:         CMP     #13
                BEQ     SCNSO8         ; a CR: no wrap
                PHA
                LDA     TTYPOS         ; CSRX
                INC     A
                LDY     #F_WID
                CMP     (FDBP),Y
                BEQ     SCNSO7
                BCS     SCNSCR         ; past the width: a new line
SCNSO7:         PLA
SCNSO8:         JMP     SCNOUT
SCNSCR:         CMP     LINLEN
                BEQ     SCNSO7         ; the width the line's: its wrap
                LDA     #13
                JSR     SCNOUT
                LDA     #10
                JSR     SCNOUT
                BRA     SCNSO7

; SCNOUT (OO-1: the screen editor's, 7d's): A to the console, by OUTDO with
; PTRFIL 0, as CALTTY sends it; PTRFIL and A kept.
SCNOUT:         PHA
                LDX     PTRFIL
                PHX
                LDX     PTRFIL+1
                PHX
                STZ     PTRFIL
                STZ     PTRFIL+1
                JSR     OUTDO
                PLX
                STX     PTRFIL+1
                PLX
                STX     PTRFIL
                PLA
                RTS

; SCNGPS (330): A the column, 0 relative, within the line: CSRX, at most
; LINLEN, less 1.
SCNGPS:         LDA     TTYPOS         ; CSRX
                INC     A
                CMP     LINLEN
                BCC     SCNGP1
                LDA     LINLEN
SCNGP1:         DEC     A
                RTS

; SCNGWD (348): A the width, the file's (F_WID: SI is always a file's here).
SCNGWD:         LDY     #F_WID
                LDA     (FDBP),Y
                RTS

.segment "BRAM"
WDTFLG:         .res    1              ; GWDATA.ASM's: SCRN:'s width
LSTCHR:         .res    1              ; and the last byte SCNOL1 had

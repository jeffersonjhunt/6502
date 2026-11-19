; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIOLPT.ASM's line printer device, translated, in block P4 (hw/oodos.md,
; "The plan, phase 7": 7c, the devices as files): a file OPENed on "LPT1:".
;
;   LPTDSP          its functions, by GIO86U's G_* (TBLDSP's table)
;   LPTINI          its start (GIOINI's): width 80, column 0, no CR sent
;   LPTOPN          OPEN: output or random only, its options none or BIN
;   LPTSOT LPOUT1   a byte out: CRIFEL's CR LF at the width, then LPOUT1's
;   LPROUT          LF after a CR, an LF straight after a CR dropped; LPROUT
;                   the byte itself, to LST: by function 5 (SNDLPT, the OEM's)
;   LPTGPS LPTGWD   the column, the width; LPTSWD sets it (WIDTH's, 7d)
;   LPTSWD
;   LPOSP           LPOS(n), from its resident stub (res/far4.s LPOS)
;
; OO-1: LPRINT and LLIST's pseudo FDB (PTRFIL $FFFF) is the OEM's OUTDO's
; (res/oem.s, 7a's), which drives LPT1 by the same rules; a file comes here.
; Both share LPT1's device control block, as the 8086's LP1DCB is shared:
; its width is the resident LINLEN+1, its column LPTPOS, its CR flag LPCRF
; (bit 7, the 8086's _LPCRF bit). LPT2 and LPT3 have no driver (DVPTR's 0:
; Device unavailable), so GLPDCB's DCB is always LPT1's. SNDLPT has no error
; to give (function 5 returns none), so LPROUT's LPERR is never reached.

.include "gwbasic.inc"

;@entry LPOSP
.export LPTDSP, LPTINI, LPOSP
.import DERBFM, DERIFN, INIFDB, SCDBIN, DEVBIN, DEVBOT, FILOPT
.import CRIFEL, UPDPOS, CRDL, CRDH, BXVEC
.import LINLEN, LPCRF, CONINT, SNGFLT, FCERR

.rodata

; LPTDSP: the line printer's functions, by G_*.
LPTDSP:         .word   DERBFM         ; G_EOF
                .word   DERBFM         ; G_LOC
                .word   DERBFM         ; G_LOF
                .word   LPTCLS         ; G_CLS
                .word   LPTSWD         ; G_SWD
                .word   DERBFM         ; G_RND
                .word   LPTOPN         ; G_OPN
                .word   DERBFM         ; G_SIN
                .word   LPTSOT         ; G_SOT
                .word   LPTGPS         ; G_GPS
                .word   LPTGWD         ; G_GWD
                .word   LPTSCW         ; G_SCW
                .word   LPTGCW         ; G_GCW
                .word   DEVBIN         ; G_BIN
                .word   DEVBOT         ; G_BOT

.code

; LPTINI (GIOLPT.ASM 59): at start-up: width 80, column 0, the flags clear.
LPTINI:         LDA     #80
                STA     LINLEN+1
                STZ     LPTPOS
                STZ     LPCRF
; LPTCLS (73), LPTTRM (52), LPTSCW, LPTGCW: nothing to do.
LPTCLS:
LPTSCW:
LPTGCW:         RTS

; LPTSWD (81): the width, DL.
LPTSWD:         LDA     RDL
                STA     LINLEN+1
                RTS

; LPTOPN (112): A the device id, OPNNUM the file, FILMOD its mode (output or
; random, else Bad file mode: INIFDB), FILOPT its options: none, or BIN (in
; either case; anything else is Bad file name). The FDB's width and column
; are the DCB's (DL, DH); BIN makes the file binary (SCDBIN): its bytes go
; to the printer as they are (LPTSOT's LPROUT).
LPTOPN:         PHA                    ; the device id, for INIFDB
                LDA     FILOPT         ; SI: the options
                AND     #$DF           ; upper case (bit 5 off)
                STA     LPOPT          ; 0: no option (the 8086's PUSHF)
                BEQ     NOOPT          ; no option
                CMP     #'B'
                BNE     IFNERR
                LDA     FILOPT+1
                AND     #$DF
                CMP     #'I'
                BNE     IFNERR
                LDA     FILOPT+2
                AND     #$DF
                CMP     #'N'
                BNE     IFNERR
                LDA     FILOPT+3       ; the options' end
                BNE     IFNERR
NOOPT:          LDA     LPTPOS         ; DH: the column
                STA     RDH
                LDA     LINLEN+1       ; DL: the width
                STA     RDL
                PLA                    ; A: the device id
                LDX     #MD_SQO | MD_RND ; output or random
                JSR     INIFDB
                LDA     LPOPT          ; BIN?
                BEQ     NOBIN
                JMP     SCDBIN
NOBIN:          RTS
IFNERR:         JMP     DERIFN         ; Bad file name

; LPTSOT (159): A out to FDBP's file. Binary: as it is (LPROUT). Else
; CRIFEL with the FDB's width (DL) and the DCB's column (DH): a printable
; byte at the width after a CR LF, each through LPOUT1, and the column
; back in the DCB. (The pseudo FDB's path, with the device's width, is
; res/oem.s's.) A, X and Y used.
LPTSOT:         PHA
                LDY     #F_CODE
                LDA     (FDBP),Y
                CMP     #FC_BIN
                BNE     NOTBIN
                PLA
                JMP     LPROUT         ; binary: raw
NOTBIN:         LDY     #F_WID         ; DL: the file's width
                LDA     (FDBP),Y
                STA     CRDL
                LDA     LPTPOS         ; DH: the column
                STA     CRDH
                LDA     #<LPOUT1       ; BX: the raw output, LPOUT1
                STA     BXVEC
                LDA     #>LPOUT1
                STA     BXVEC+1
                PLA
                JSR     CRIFEL
                LDA     CRDH           ; the new column
                STA     LPTPOS
                RTS

; LPOUT1 (205): A out, the column (DH) moved first (UPDPOS). An LF straight
; after a CR is dropped; after a CR, an LF, unless the file is random and
; its width 255 (IBM's). A kept.
LPOUT1:         JSR     UPDPOS
                PHA
                CMP     #10
                BNE     LPOUT2
                BIT     LPCRF          ; the last out a CR: eaten
                BMI     LPOUT3
LPOUT2:         JSR     LPROUT
LPOUT3:         PLA
                STZ     LPCRF
                CMP     #13
                BNE     LPOUTX
                LDX     #$80           ; the last out a CR
                STX     LPCRF
                LDY     #F_WID         ; (SI is never the pseudo FDB here)
                LDA     (FDBP),Y
                CMP     #255
                BNE     OUTLF
                LDY     #F_MODE
                LDA     (FDBP),Y
                CMP     #MD_RND
                BEQ     LPOUTC         ; no LF after the CR
OUTLF:          LDA     #10
                JSR     LPROUT
LPOUTC:         LDA     #13
LPOUTX:         RTS

; LPROUT (248): A to the printer: SNDLPT, OO-1 LST: by function 5, which has
; no error to give. A kept.
LPROUT:         PHA
                LDY     #L_WRITE
                JSR     BDOS
                PLA
                RTS

; LPOSP: LPOS(n) (272), n the FAC: the printer's column plus 1. OO-1 (the
; IBM manual's p. 178: 0 or 1 LPT1:, 2 LPT2:, 3 LPT3:; the source reads the
; DCB at (n - 1) / 2): 0 and 1 LPT1's; 2 and 3, which have no driver here,
; 1 (column 0); past 3 Illegal function call (a default, for the owner's
; veto).
LPOSP:          JSR     CONINT         ; A: n, 0-255
                CMP     #4
                BCC     :+
                JMP     FCERR
:               CMP     #2
                LDA     #0             ; LPT2:, LPT3:
                BCS     @r
                LDA     LPTPOS         ; LP1DCB+_LPPOS: LPT1's column
@r:             INC     A              ; 1 relative
                JMP     SNGFLT

; LPTGPS (291): A the column. LPTGWD (300): A the width.
LPTGPS:         LDA     LPTPOS
                RTS
LPTGWD:         LDA     LINLEN+1
                RTS

.segment "BRAM"
LPOPT:          .res    1              ; LPTOPN's: an option given (BIN), the 8086's flags

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GIO86.ASM's PRINT and LPRINT, translated, in block P1 (hw/oodos.md, "The
; plan, phase 7"), with the device routines they use:
;
;   PRINTP LPRNTP   PRINT and LPRINT (PRINT, LPRINT): items, ";", ","'s
;                   print zones, TAB( and SPC(, numbers as $FOUT gives them
;                   with a space after, the device's width
;   PTRGPS          the current output's column
;   PTRWDC          its width, and the last column a comma may go to
;   ADRGET          a formula as 0..65535 (TAB's and SPC's)
;
; The resident core's PRINT and LPRINT (res/gwmain.s) far call these, once a
; statement. PRINT # (FILGET, the files) is 7c's: Advanced Feature. PRINT
; USING and LPRINT USING go to P3's PRINUS (p3/biprtu.s), as GIO86.ASM's
; JPRINU jumps: a far call, whose return is this statement's (PRINUS ends in
; FINPRT, as PRINT does). The 8086 keeps the width and the last comma column
; in CX on its stack, and the text pointer beside them; here they are PRTWID
; and PRTCMA, and the text pointer is TXTPTR, which the 8086's pushes keep
; and which nothing here moves but the scan itself.

.include "gwbasic.inc"

;@entry PRINTP
;@entry LPRNTP
.export PRINTP, LPRNTP, PTRGPS, PTRWDC, ADRGET
.import CHRGTR, FRMEVL, GETYP, FOUT, STRLIT, STRPRT, CRDO, FINPRT, OUTDO
.import SYNCHR, FRQINT, IMOD, ADVERR, LINLEN

CLMWID          = 14                   ; OEM.H: the print zones' width

.code

; LPRINT (GIO86.ASM 431): to LPT1 (PTRFIL $FFFF), then as PRINT. PRINT: a
; "#" is a file's (7c).
LPRNTP:         LDA     #$FF
                STA     PTRFIL
                STA     PTRFIL+1
                BRA     PRINT1
PRINTP:         CMP     #'#'           ; FILGET
                BNE     PRINT1
                JMP     ADVERR
PRINT1:         JSR     PTRWDC
                STX     PRTWID
                STA     PRTCMA
NEWCHR:         DECW    TXTPTR
                JSR     CHRGTR
                BEQ     PRINTX         ; the end with no ";" or ",": CR LF
PRINTC:         BEQ     PRNTX1         ; the end after one
                CMP     #USING_TK      ; PRINT USING?
                BEQ     JPRINU
                CMP     #TAB_TK
                BEQ     TABERJ
                CMP     #SPC_TK
                BEQ     TABERJ
                CMP     #','
                JEQ     COMPRT
                CMP     #';'
                BNE     PRTS
                JMP     NOTABR
TABERJ:         JMP     TABER
; JPRINU (GIO86.ASM 425): PRINUS, TXTPTR at USING; its end is the statement's.
JPRINU:         FJSR    PRINUS
                RTS
PRINTX:         JSR     CRDO
PRNTX1:         JMP     FINPRT

; An item: a string printed as it is; a number as $FOUT's text, a literal
; in FBUFF with a space put after it, which counts unless the number was
; followed by a comma and ends just before a print zone (so the next number
; starts at the zone, not a column past it). Either way, a new line first
; if it would pass the line's end and isn't at its start.
PRTS:           JSR     FRMEVL
                JSR     GETYP
                BEQ     STRDON
                DECW    TXTPTR         ; the number's terminator
                JSR     CHRGTR
                CMP     #','
                PHP                    ; Z: a comma
                LDA     TXTPTR+1
                PHA
                LDA     TXTPTR
                PHA
                JSR     FOUT           ; FBPTR at the text
                LDA     FBPTR          ; STRLIT reads at TXTPTR
                STA     TXTPTR
                LDA     FBPTR+1
                STA     TXTPTR+1
                JSR     STRLIT         ; TXTPTR at the zero after it
                LDA     #' '
                STA     (TXTPTR)
                PLA
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                PLP
                BNE     INCLEN         ; not a comma: the space counts
                JSR     PTRGPS         ; where the number ends, its space not
                CLC                    ; counted
                ADC     (FACLO)
                LDX     PRTWID         ; no width: by the zones alone
                CPX     #255
                BEQ     MODCOM
                CMP     PRTCMA         ; past the last zone: the space counts
                BCS     INCLEN
MODCOM:         INC     A              ; one before a zone?
MODCM1:         SEC
                SBC     #CLMWID
                BEQ     STRDON         ; yes: the space doesn't count
                BCS     MODCM1
INCLEN:         LDA     (FACLO)        ; the length, with the space
                INC     A
                STA     (FACLO)
STRDON:         LDA     PRTWID         ; no width: never a new line
                CMP     #255
                BEQ     LINCH2
                JSR     PTRGPS         ; at the margin: never either
                BEQ     LINCH2
                CLC
                ADC     (FACLO)        ; past 255, or past the width: a new line
                BCS     LINCRD
                DEC     A
                CMP     PRTWID
                BCC     LINCH2
LINCRD:         JSR     CRDO
LINCH2:         JSR     STRPRT
                JMP     NEWCHR

; COMPRT (GIO86.ASM 518): "," to the next print zone, or past the last a
; new line.
COMPRT:         JSR     PTRGPS
                LDX     PRTWID
                CPX     #255
                BEQ     MORCOM
                CMP     PRTCMA
                BCC     MORCOM
                JSR     CRDO
                BRA     NOTABR
MORCOM:         SEC                    ; the position mod CLMWID, less CLMWID
                SBC     #CLMWID
                BCS     MORCOM
                EOR     #$FF           ; NEG, DEC: CLMWID - mod - 1
                BRA     ASPA2

; TABER (GIO86.ASM 537): TAB(n) to column n-1 (0-based: n), a new line first
; if past it; SPC(n), n spaces. Both mod the width (unless 255); a
; negative n is 0.
TABER:          PHA                    ; TAB_TK or SPC_TK
                JSR     CHRGTR
                JSR     ADRGET         ; DX
                PLA
                PHA
                LDX     RDH            ; JG: greater than 0, signed
                BMI     @zero
                BNE     TBNONG
                LDX     RDL
                BNE     TBNONG
@zero:          STZ     RDL
                STZ     RDH
                BRA     SPCNDC
TBNONG:         CMP     #SPC_TK        ; TAB: less one
                BEQ     SPCNDC
                DECW    RDL
SPCNDC:         LDA     PRTWID         ; mod the width: DL the place to go
                CMP     #255
                BEQ     LNOMOD
                STA     RBL
                STZ     RBH
                JSR     IMOD
                LDA     FACLO
                STA     RDL
LNOMOD:         JSR     SYNCHR
                .byte   ')'
                DECW    TXTPTR
                PLA
                SEC
                SBC     #SPC_TK        ; SPC: A = 0, the count DL
                BEQ     DOSIZT
                JSR     PTRGPS         ; TAB: from the column
DOSIZT:         EOR     #$FF           ; NEG, DEC: DL - A - 1
                CLC
                ADC     RDL
                BCS     ASPA2          ; ahead: spaces to it
                INC     A
                BEQ     NOTABR         ; there already
                JSR     CRDO           ; behind: a new line, then DL spaces
                LDA     RDL
                DEC     A
                BMI     NOTABR
ASPA2:          INC     A
ASPAC:          STA     RDL            ; A spaces (none: 256, as the 8086)
                LDA     #' '
REPOUT:         JSR     OUTDO
                DEC     RDL
                BNE     REPOUT
NOTABR:         JSR     CHRGTR         ; past ";", "," or ")"
                JMP     PRINTC         ; a new line only if more follows

; --- PTRGPS, PTRWDC (GIO86.ASM 1208) ------------------------------------------------

; PTRGPS: the current output's column (the console's or LPT1's) in A, Z set
; at the margin.
PTRGPS:         LDX     #0
                LDA     PTRFIL
                BEQ     :+
                INX
:               LDA     TTYPOS,X
                RTS

; PTRWDC: X = the current output's width (LINLEN; WIDTH is 7d's), A the last
; column a comma may print at: the width less its remainder mod 14, less 14
; (80: 56). (PTRWID's file widths are 7c's.)
PTRWDC:         LDX     #0
                LDA     PTRFIL
                BEQ     :+
                INX
:               LDA     LINLEN,X
                TAX
@m:             SEC
                SBC     #14
                BCS     @m
                CLC
                ADC     #28
                EOR     #$FF           ; NEG
                INC     A
                STX     T0
                CLC
                ADC     T0
                RTS

; ADRGET (GIO86.ASM 766): a formula as 0..65535 in DX (FRQINT's), TXTPTR
; past it.
ADRGET:         JSR     FRMEVL
                JSR     FRQINT
                LDA     FACLO
                STA     RDL
                LDA     FACLO+1
                STA     RDH
                RTS

.segment "BRAM"
PRTWID:         .res    1              ; the 8086's CH: the width
PRTCMA:         .res    1              ; and CL: the last comma column

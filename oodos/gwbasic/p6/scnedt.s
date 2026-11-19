; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; SCNEDT.ASM's screen editor, translated, in block P6 (hw/oodos.md, "The
; plan, phase 7": 7d1): a line typed on GW-BASIC's own screen, in screen
; mode (the resident PINLIN, QINLIN, INLIN and SINLIN far call SCINLN; in
; console mode they read by function 10, res/oem.s).
;
;   SCINLN    INLIN0 to INCRTF: C set a program line or command (PINLIN's),
;             clear INPUT's. The keys echoed through the screen driver,
;             which does the editing; at a CR the logical line read back
;             from the cells (SCNRDL) into BUF, a program line's trailing
;             blanks and line feeds dropped, the cursor past the line.
;             TXTPTR at BUF-1, C clear
;   EDTBRK    a break's (CNTCCN's): the cursor off, the editor's flags
;             cleared, the cursor past the logical line, AUTO ended
;
; DSKCHI (a program line from a file) is 7c's, p4/scnedt.s. OO-1: INLHLP,
; the edit key the IBM keyboard hasn't, isn't reached (Ctrl-A is a
; character); ERREDT's and EDIT's line and AUTO's number come as BUF's
; preload (PRELOD, 7b's way for function 10), printed here first as the
; source prints them, so the line read back holds them: EDIT's with the
; cursor at its start (PRELOD 1), AUTO's with it after them (2).

.include "gwbasic.inc"
.include "p6/p6.inc"

;@entry SCINLN
;@entry EDTBRK
.export SCINLN, EDTBRK
.import BUF, BUFMIN, SEMFLG, PRELOD

.macro  MOVB6   dst, src
                LDA     src
                STA     dst
.endmacro

.code

SCINLN:         PHP
                JSR     P6IN
                PLP
                LDA     #0             ; SBB AL,AL: F_EDPG
                BCC     :+
                LDA     #$FF
:               STA     F_EDP6
                EOR     #$FF
                STA     MKINP          ; the 8086's ZF: an INPUT statement
                LDA     F_EDIT         ; (1: EDIT's line)
                LDX     PRELOD
                BEQ     INLIN1
                STZ     PRELOD         ; the preload printed, as the source prints it:
                STX     F_EDIT         ;   EDIT's line (1: ERED5's F_EDIT, the cursor to
                PHX                    ;   its start, SCNMRK's LSTART) or AUTO's number
                LDX     #0             ;   (2: the cursor after it, MAINA's LINPRT)
@p:             LDA     BUF,X
                BEQ     @e
                PHX
                JSR     PUTC
                PLX
                INX
                BNE     @p
@e:             PLA
INLIN1:         LDX     #$FF
                STX     F_EDIT
                JSR     SCNMRK

; INLLOP (SCNEDT.ASM 164): each key echoed, to a CR.
INLLOP:         CLC
                JSR     STCTYP
                JSR     KEYIN
                BCS     INL2BY
                CMP     #CHRRET
                BEQ     INLRET
                CMP     #254
                BEQ     INLLOP
                BRA     INLOP3
INL2BY:         CPX     #$FF
                BNE     INLOP0
                CMP     #CHRDEL
                BEQ     INLP0
                CMP     #' '
                BCS     INLOP0
INLP0:          CMP     #CHRRET
                BEQ     INLRET
INLOP0:         PHA
                TXA
                JSR     PUTC
                PLA
INLOP3:         JSR     PUTC
                BRA     INLLOP

; INLRET (SCNEDT.ASM 243): the logical line into BUF.
INLRET:         STZ     xBL
                LDA     #255
                STA     xCL
                MOVB6   xDL, CSRY
                MOVB6   xDH, CSRX
                JSR     SCNRDL
                LDX     xBL
                LDA     F_EDP6
                BEQ     INCRTX
@t:             CPX     #0             ; a program line: no trailing blanks or line feeds
                BEQ     INCRTX
                LDA     BUF - 1,X
                CMP     #' '
                BEQ     @d
                CMP     #CHRLNF
                BNE     INCRTX
@d:             DEX
                BRA     @t
INCRTX:         STZ     BUF,X
                LDA     SEMFLG
                BNE     INCRTF
                LDA     #CHRAPP        ; to the logical line's end
                JSR     PUTC
                STZ     F_EDIT
                LDA     #13            ; CRDO
                JSR     PUTC
                LDA     #10
                JSR     PUTC
INCRTF:         LDA     #<BUFMIN
                STA     TXTPTR
                LDA     #>BUFMIN
                STA     TXTPTR + 1
                JSR     CLRFLG
                JSR     P6OUT
                CLC
                RTS

; EDTBRK (SCNEDT.ASM 278), and SCNBRK.
EDTBRK:         JSR     P6IN
                STZ     CSRTYP
                JSR     SETCSR
                JSR     CLRFLG
                LDA     #CHRAPP
                STA     F_EDIT
                JSR     PUTC
                STZ     F_EDIT
                LDA     #13
                JSR     PUTC
                LDA     #10
                JSR     PUTC
                STZ     AUTFLG
                JSR     SCNBRK
                JMP     P6OUT

; CLRFLG (SCNEDT.ASM 299)
CLRFLG:         STZ     F_EDIT
                STZ     F_EDP6
                STZ     F_INST
                STZ     SEMFLG
                RTS


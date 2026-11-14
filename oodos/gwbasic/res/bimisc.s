; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; BIMISC.ASM's SYNCHR, GETSTK and ISLET2, translated (segment BIMISC), and
; the BASIC stack (segment RUNTIME), which has no 8086 counterpart: it is
; what the 8086's PUSH and POP of BASIC's own data become (gwbasic.inc).
;
; And the program's life (step I1), from BIMISC.ASM:
;
;   SCRATH SCRTCH NODSKS   NEW: no program, then RUNC
;   RUNC CLEARC            the variables, arrays, strings and DATA reset
;   STKINI STKERR          both stacks reset, the string temporaries freed
;   RESTORE RESFIN         the DATA pointer
;   STOP ENDST CONSTP      STOP, END, and ^C's break (STPEND, ENDCON):
;   STPEND ENDCON          "Break in 20" or nothing, then READY
;   CONT                   on from OLDTXT
;   TON TOFF               TRON, TROFF
;   OMERR                  Out of memory, the links fixed first
;
; and ITSA86.ASM's INITSA, the end of start-up.

.include "gwbasic.inc"

.export SYNCHR, GETSTK, ISLET2, SPUSH, SPOP, SPUSHA, SPOPA
.export SCRATH, SCRTCH, NODSKS, RUNC, CLEARC, STKINI, STKERR, RESTORE, RESFIN
.export STOP, STOPRG, ENDST, STPEND, ENDCON, CONT, TON, TOFF, OMERR, INITSA
.export TOPMEM, TOPS, DATPTR, BSTACK, BSTOP
.import CHRGTR, SNERR, ERROR, LINGET, FNDLIN, USERR, READY, CRDONZ, FINLPT
.import FINPRT, MEMSIZ, TEMPST, DEFTBL, OPTVAL, SUBFLG, RNDX, RNDCOP
.import ONEFLG, OLDLIN, OLDTXT, PRMLN2

NUMLEV          = 16                   ; words GETSTK keeps spare, as the 8086's NUMLEV
ERROM           = 7                    ; Out of memory
ERRCN           = 17                   ; Can't continue
STKSIZ          = 512                  ; the BASIC stack (hw/oodos.md, "The plan, phase 7")

.code

; SYNCHR: JSR SYNCHR / .byte c. The character at TXTPTR must be c (else
; Syntax error); then CHRGTR past it. Returns past the inline byte. Reading
; the return address with PLA is a stack instruction, which OODOS's rule 3
; allows.
SYNCHR:         PLA
                STA     SYNPTR
                PLA
                STA     SYNPTR+1
                INCW    SYNPTR         ; the inline byte
                LDA     (TXTPTR)
                CMP     (SYNPTR)
                BNE     SYNERR
                LDA     SYNPTR+1
                PHA
                LDA     SYNPTR
                PHA
                JMP     CHRGTR
SYNERR:         JMP     SNERR

; GETSTK: make sure the BASIC stack has room for A more words and NUMLEV
; spare, and the 6502's stack a little, or Out of memory.
GETSTK:         ASL     A
                ADC     #2*NUMLEV
                STA     SPN
                SEC
                LDA     SSP
                SBC     SPN
                TAY
                LDA     SSP+1
                SBC     #0
                BCC     @om
                CMP     STKLOW+1
                BCC     @om
                BNE     @hw
                CPY     STKLOW
                BCC     @om
@hw:            TSX
                CPX     #$40
                BCC     @om
                RTS
@om:            JMP     OMERR

; OMERR (BIMISC.ASM 128): Out of memory. The links are fixed first (a line
; being replaced may have been taken out already), and SAVSTK made the top
; of both stacks, so the error drops every FOR and GOSUB.
OMERR:          FJSR    LINKER
                LDA     TOPS
                STA     SAVSTK
                SEC
                LDA     TOPMEM
                SBC     #2             ; the FNDFOR stopper kept
                STA     SAVSTK+1
                LDA     TOPMEM+1
                SBC     #0
                STA     SAVSTK+2
OMERRR:         LDA     #ERROM
                JMP     ERROR

; ISLET2: C clear if A is a letter.
ISLET2:         CMP     #'A'
                BCC     @no
                CMP     #'Z'+1
                RTS
@no:            SEC
                RTS

.code

; SPUSH: A bytes from zero page at X onto the BASIC stack, as one block.
; SPOP: the top block of A bytes back to zero page at X. A block pushed
; with SPUSH comes back whole with SPOP of the same length.
SPUSH:          STA     SPN
                SEC
                LDA     SSP
                SBC     SPN
                STA     SSP
                BCS     :+
                DEC     SSP+1
:               LDY     #0
@m:             LDA     0,X
                STA     (SSP),Y
                INX
                INY
                CPY     SPN
                BNE     @m
                RTS
SPOP:           STA     SPN
                LDY     #0
@m:             LDA     (SSP),Y
                STA     0,X
                INX
                INY
                CPY     SPN
                BNE     @m
                CLC
                LDA     SSP
                ADC     SPN
                STA     SSP
                BCC     :+
                INC     SSP+1
:               RTS

; SPUSHA, SPOPA: one byte, in A; SPOPA sets N and Z by it.
SPUSHA:         PHA
                LDA     SSP
                BNE     :+
                DEC     SSP+1
:               DEC     SSP
                PLA
                STA     (SSP)
                RTS
SPOPA:          LDA     (SSP)
                INCW    SSP
                ORA     #0
                RTS

; --- SCRATH, SCRTCH, RUNC, CLEARC, STKINI (BIMISC.ASM 168) -------------------

; NEW (SCRATH: nothing may follow it): no program, no trace, no line
; pointers, no AUTO; then RUNC. NODSKS is start-up's way in (so AUTFLG
; starts clear). PROFLG (a protected program) is 7c's.
SCRATH:         BEQ     SCRTCH
                RTS
NODSKS:
SCRTCH:         LDA     TXTTAB
                STA     PTR
                LDA     TXTTAB+1
                STA     PTR+1
                JSR     TOFF           ; A = 0
                STZ     AUTFLG
                STZ     PTRFLG
                STA     (PTR)          ; the program: its end link alone
                LDY     #1
                STA     (PTR),Y
                CLC
                LDA     PTR
                ADC     #2
                STA     VARTAB
                LDA     PTR+1
                ADC     #0
                STA     VARTAB+1
; RUNC: CLEARC with TXTPTR at TXTTAB-1, where NEWSTT starts the program.
RUNC:           LDA     TXTTAB
                STA     TXTPTR
                LDA     TXTTAB+1
                STA     TXTPTR+1
                DECW    TXTPTR
; CLEARC: the variables and arrays gone, every letter single, no OPTION
; BASE, RND's seed back, no ON ERROR, nothing to CONT, the strings gone, DATA
; from the start; then STKINI. TXTPTR is kept. Every event trap off, with
; no line (P3's INITRP); the source calls INITRP twice, round GRPRST (the
; graphics' reset), and the second finds nothing more to do: once here till
; 7e brings GRPRST. (The screen, the sound and the files it resets too are
; 7c's to 7f's; CHAIN's MRGFLG and CHNFLG are 7c's.)
CLEARC:         LDA     TXTPTR
                STA     TEMP
                LDA     TXTPTR+1
                STA     TEMP+1
                FJSR    INITRP         ; init the trap table
                STZ     OPTFLG         ; no OPTION BASE seen
                STZ     OPTVAL         ; OPTION BASE 0
                LDX     #25
                LDA     #VT_SNG
LOPDFT:         STA     DEFTBL,X
                DEX
                BPL     LOPDFT
LEVDTB:         LDX     #3             ; RNDX = RNDCOP
@r:             LDA     RNDCOP,X
                STA     RNDX,X
                DEX
                BPL     @r
                STZ     ONEFLG
                STZ     ONELIN
                STZ     ONELIN+1
                STZ     OLDTXT
                STZ     OLDTXT+1
                STZ     ERRLIN
                STZ     ERRLIN+1
                LDA     MEMSIZ         ; the string space empty
                STA     FRETOP
                LDA     MEMSIZ+1
                STA     FRETOP+1
GODFRE:         LDA     #0             ; Z: RESTORE from the start
                JSR     RESTORE
                LDA     VARTAB
                STA     ARYTAB
                STA     STREND
                LDA     VARTAB+1
                STA     ARYTAB+1
                STA     STREND+1
; STKINI: both stacks empty, with a zero word (the FNDFOR stopper) on the
; BASIC stack, SAVSTK at it; the string temporaries free, the overflow
; message back to normal, output to the console, no user function being
; evaluated (DEF FN's parameter state: PRMLEN, NOFUNS, PRMLN2, FUNACT and
; PRMSTK zero; PRMFLG too, which the 8086 needn't: NOARYS's), subscripts
; allowed. Returns to its caller (popped first, pushed back), TXTPTR from
; TEMP.
STKINI:         PLA                    ; the return address, in CX
                STA     RCL
                PLA
                STA     RCH
                LDX     TOPS
                STX     SAVSTK
                TXS
                SEC
                LDA     TOPMEM
                SBC     #2
                STA     SAVSTK+1
                LDA     TOPMEM+1
                SBC     #0
                STA     SAVSTK+2
                LDA     TOPMEM
                STA     SSP
                LDA     TOPMEM+1
                STA     SSP+1
; STKERR: from here with S and SSP set (ERESET's way in).
STKERR:         LDA     #<TEMPST
                STA     TEMPPT
                LDA     #>TEMPST
                STA     TEMPPT+1
                STZ     FLGOVC         ; CLROVC
                JSR     FINPRT
                LDX     #7             ; PRMLN2, FUNACT, PRMSTK, PRMLEN: together
@p:             STZ     PRMLN2,X
                DEX
                BPL     @p
                STZ     NOFUNS
                STZ     PRMFLG
                STZ     SUBFLG
                LDA     #0             ; the stopper
                JSR     SPUSHA
                JSR     SPUSHA
                LDA     RCH
                PHA
                LDA     RCL
                PHA
GTMPRT:         LDA     TEMP
                STA     TXTPTR
                LDA     TEMP+1
                STA     TXTPTR+1
                RTS

; --- RESTORE (BIMISC.ASM 552) ----------------------------------------------------

; RESTORE (Z: the end of the statement): DATA from the start; RESTORE line:
; from that line, or Undefined line number. DATPTR is left at the byte
; before, as READ wants it; RESFIN is READ's way out.
RESTORE:        BNE     @line
                LDA     TXTTAB
                LDX     TXTTAB+1
                BRA     BGNRST
@line:          JSR     LINGET
                LDA     TXTPTR+1
                PHA
                LDA     TXTPTR
                PHA
                JSR     FNDLIN
                PLA
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                BCS     :+
                JMP     USERR
:               LDA     RCL
                LDX     RCH
BGNRST:         SEC                    ; less one
                SBC     #1
                BCS     RESFIN
                DEX
RESFIN:         STA     DATPTR
                STX     DATPTR+1
                RTS

; --- STOP, END (BIMISC.ASM 566) ------------------------------------------------

; STOP and END must end their statement (else back to NEWSTT, which gives
; Syntax error). Both keep the text for CONT and free the string
; temporaries, and pop NEWSTT's return; then ENDCON. STOP says "Break"
; (STOPRG, with A = 0 when ^C came in direct mode: no message). END leaves
; ON ERROR's routine (ONEFLG). STPEND: INPUT's ^C, which says "Break" too.
; The message flag is C here (the 8086 keeps it in Z through a PUSHF).
; (END's closing of the files is 7c's.)
STOP:           BEQ     STOPRG
                RTS
STOPRG:         INC     A
                BRA     CONSTP
ENDST:          BEQ     :+
                RTS
:               STZ     ONEFLG
                LDA     #0
CONSTP:         PHP
                LDA     TXTPTR         ; for CONT
                STA     SAVTXT
                LDA     TXTPTR+1
                STA     SAVTXT+1
                LDA     #<TEMPST
                STA     TEMPPT
                LDA     #>TEMPST
                STA     TEMPPT+1
                PLP
                CLC                    ; Z: no message
                BEQ     CONPOP
STPEND:         SEC
CONPOP:         PLX                    ; NEWSTT's return address (C kept)
                PLX
                BRA     ENDCN2
; ENDCON: the end, from PRGEND: no message. CONT's line and text, unless
; direct; LPT1 to its margin, the console to a new line; then "Break in
; 20" (P2's BRKMSG) if C, and READY.
ENDCON:         CLC
ENDCN2:         PHP
                LDA     CURLIN
                AND     CURLIN+1
                CMP     #$FF
                BEQ     DIRIS
                LDA     CURLIN
                STA     OLDLIN
                LDA     CURLIN+1
                STA     OLDLIN+1
                LDA     SAVTXT
                STA     OLDTXT
                LDA     SAVTXT+1
                STA     OLDTXT+1
DIRIS:          JSR     FINLPT
                JSR     CRDONZ
                PLP
                BCC     :+
                FJSR    BRKMSG
:               JMP     READY

; --- CONT, TRON, TROFF (BIMISC.ASM 639) ----------------------------------------------

; CONT: on from OLDTXT (0: Can't continue), at OLDLIN.
CONT:           LDA     OLDTXT
                ORA     OLDTXT+1
                BNE     :+
                LDA     #ERRCN         ; RESERR
                JMP     ERROR
:               LDA     OLDTXT
                STA     TXTPTR
                LDA     OLDTXT+1
                STA     TXTPTR+1
                LDA     OLDLIN
                STA     CURLIN
                LDA     OLDLIN+1
                STA     CURLIN+1
                RTS
TON:            LDA     #$FF
                BRA     TONOFF
TOFF:           LDA     #0
TONOFF:         STA     TRCFLG
                RTS

; --- INITSA (ITSA86.ASM 52) ------------------------------------------------------

; The end of start-up, from the loader once P2's INIT has set memory up and
; printed the heading: the 6502's stack top kept, NEW, the zero before the
; program, and READY. (A program named on the command line, LRUN, is 7c's.)
INITSA:         TSX
                STX     TOPS
                JSR     NODSKS
                LDA     TXTTAB
                STA     PTR
                LDA     TXTTAB+1
                STA     PTR+1
                DECW    PTR
                LDA     #0
                STA     (PTR)
                JMP     READY

.segment "RAM"
; GWDATA.ASM's, and the BASIC stack (hw/oodos.md, "The plan, phase 7": fixed RAM)
TOPMEM:         .res    2              ; the BASIC stack's top (SSP when it is empty)
TOPS:           .res    1              ; the 6502's S at start-up, when its stack is empty
DATPTR:         .res    2              ; READ's place in the DATA
BSTACK:         .res    STKSIZ         ; the BASIC stack, down from BSTOP to STKLOW
BSTOP:

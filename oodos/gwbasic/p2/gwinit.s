; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWINIT.ASM's INIT and ITSA86.ASM's MAPINI, in block P2: start-up, from the
; loader, before INITSA (res/bimisc.s) makes the program empty and goes to
; READY. And CMDERR, for an error before start-up is done. The OO-1's own
; parts (GWINI and GETHED, the OEM's) are marked.
;
; The memory map, as GW-BASIC's (GWINIT.ASM 330) but for the BASIC stack,
; which is fixed RAM here (hw/oodos.md, "The plan, phase 7"):
;
;   TXTTAB        the loader's first byte, plus one (the zero before the
;                 program): the loader runs once and becomes the workspace
;   MEMSIZ        the TPA's top (the base page's $0204 less one, which RSXs
;                 lower) less one: the top byte is a zero, for VAL
;   FRETOP        MEMSIZ: no strings
;   TOPMEM STKLOW the BASIC stack, BSTOP down to BSTACK
;
; "Bytes free" is MEMSIZ - TXTTAB - 2, as ITSA86.ASM's: FRE(0) with no
; program.

.include "gwbasic.inc"

;@entry GWINIT
;@entry CMDERR
.export GWINIT, CMDERR
.import STROUT, CRDO, SYSTME, LOAD, MEMSIZ, TEMPST, TOPMEM, BSTOP, BSTACK
.import INITFG, OLDMOD, BUFMIN, KBUF, ERROR

.code

GWINIT:
; GWINI, the OEM's: the console mode kept for SYSTEM, then bit 3 set, so a
; ^C is GW-BASIC's break and doesn't end the program.
                LDA     #$FF
                LDX     #$FF
                LDY     #C_MODE
                JSR     BDOS
                STA     OLDMOD
                STX     OLDMOD+1
                ORA     #$08
                LDY     #C_MODE
                JSR     BDOS
                STZ     TTYPOS
                STZ     LPTPOS
                STZ     PTRFIL
                STZ     PTRFIL+1
; INIT's own (GWINIT.ASM 65): KBUF-1 a colon, so a direct line can be run
; again; BUFMIN a comma; no error yet; the string temporaries free.
                LDA     #':'
                STA     KBUF-1
                LDA     #','
                STA     BUFMIN
                STZ     ERRFLG
; OO-1: AUTO's increment 0 until an AUTO sets one, as GW-BASIC's data
; segment starts (so AUTO n, first is Illegal function call, as PC-BASIC
; gives it too); OODOS doesn't clear a program's zero page. (AUTFLG starts
; clear by INITSA's NODSKS.)
                STZ     AUTINC
                STZ     AUTINC+1
                LDA     #<TEMPST
                STA     TEMPPT
                LDA     #>TEMPST
                STA     TEMPPT+1
; The memory map.
                CLC                    ; TXTTAB
                LDA     #<LOAD
                ADC     #1
                STA     TXTTAB
                LDA     #>LOAD
                ADC     #0
                STA     TXTTAB+1
                LDA     BDOS+1         ; the TPA's top
                STA     PTR
                LDA     BDOS+2
                STA     PTR+1
                DECW    PTR
                LDA     #0             ; a zero at the top, for VAL
                STA     (PTR)
                DECW    PTR
                LDA     PTR            ; MEMSIZ, FRETOP
                STA     MEMSIZ
                STA     FRETOP
                LDA     PTR+1
                STA     MEMSIZ+1
                STA     FRETOP+1
                LDA     TXTTAB         ; room for a program at all?
                CMP     MEMSIZ
                LDA     TXTTAB+1
                SBC     MEMSIZ+1
                BCC     :+
                LDA     #7             ; OMERRR: Out of memory (and CMDERR)
                JMP     ERRORJ
:               LDA     #<BSTOP        ; the BASIC stack
                STA     TOPMEM
                LDA     #>BSTOP
                STA     TOPMEM+1
                LDA     #<BSTACK
                STA     STKLOW
                LDA     #>BSTACK
                STA     STKLOW+1
; The heading: GETHED's (the OEM's) line, then HEDING (GWRAM.ASM 36), with
; Microsoft's copyright as GW-BASIC has it and its date's field, blank.
                LDA     #<OEMHED
                STA     PTR
                LDA     #>OEMHED
                STA     PTR+1
                JSR     STROUT
                LDA     #<HEDING
                STA     PTR
                LDA     #>HEDING
                STA     PTR+1
                JSR     STROUT
; MAPINI's message (ITSA86.ASM 189): the bytes free.
                SEC
                LDA     MEMSIZ
                SBC     TXTTAB
                STA     RBL
                LDA     MEMSIZ+1
                SBC     TXTTAB+1
                STA     RBH
                SEC
                LDA     RBL
                SBC     #2
                STA     RBL
                BCS     :+
                DEC     RBH
:               FJSR    LINPRT
                LDA     #<WORDS
                STA     PTR
                LDA     #>WORDS
                STA     PTR+1
                JSR     STROUT
                JSR     CRDO
                LDA     #$FF           ; errors are BASIC's from now on
                STA     INITFG
                RTS
ERRORJ:         JMP     ERROR

; CMDERR (GWINIT.ASM 445): an error before start-up was done: the heading,
; the command line blamed, and back to OODOS.
CMDERR:         LDA     #<HEDING
                STA     PTR
                LDA     #>HEDING
                STA     PTR+1
                JSR     STROUT
                LDA     #<CERMSG
                STA     PTR
                LDA     #>CERMSG
                STA     PTR+1
                JSR     STROUT
                JMP     SYSTME

.rodata
OEMHED:         .byte   "GW-BASIC for the OO-1", 0        ; GETHED's, the OO-1's
HEDING:         .byte   13, 10, "(C) Copyright Microsoft 1982"
                .byte   "         ", 13, 10, 0           ; $DATE: blank
CERMSG:         .byte   13, 10, "Error detected in command line", 13, 10, 0
WORDS:          .byte   " Bytes free", 0

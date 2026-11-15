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
.import INITFG, OLDMOD, BUFMIN, KBUF, ERROR, BUF, CHRGTR
.import MAKUPL, SYNCHR, SNERR, LINGET, OCTCNS

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
; OO-1: error mode $FE (function 45): a physical error (a drive with no
; image, a disk that fails) is said by the BDOS and returned, and P4's PBDOS
; makes it GW-BASIC's error, as the 8086's DSKERR takes MS-DOS's critical
; errors; the default mode would end GW-BASIC. SYSTEM needn't put it back:
; the CCP sets the default mode before each program it runs.
                LDA     #$FE
                LDY     #F_ERRMODE
                JSR     BDOS
                STZ     TTYPOS
                STZ     LPTPOS
                STZ     PTRFIL
                STZ     PTRFIL+1
; GIOINI's (GIO86.ASM 2052): no load under way (NLONLY 0); OO-1: no file 0
; open (F0OPN), OODOS doesn't clear a program's zero page. Then its DOALDV,
; each device's start (P4's GIOINP: LPT1:'s width, COM1:'s DCB, SCRN:'s
; width, the keyboard's queue).
                STZ     NLONLY
                STZ     F0OPN
                FJSR    GIOINP
; CPMVR1's (GWINIT.ASM 158): no CHAIN, no MERGE under way.
                STZ     CHNFLG
                STZ     MRGFLG
; INIT's own (GWINIT.ASM 65): KBUF-1 a colon, so a direct line can be run
; again; BUFMIN a comma; no error yet; the string temporaries free.
                LDA     #':'
                STA     KBUF-1
                LDA     #','
                STA     BUFMIN
                STZ     ERRFLG
                STZ     MSWFLG         ; no /M: yet (GWINIT.ASM 107)
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
; TBFLP to NXTBYT (GWINIT.ASM 196): the command line's file name, if there
; is one, to BUF with a quote before it, for INITSA's LRUN to take as a
; formula (the 8086's TEMP8 then BUF); else BUF a zero. The tail (OO-1:
; BP_DMA's, its length first, as CP/M's TBUFF) is moved down over its
; length and ended by a zero; leading spaces skipped (CHRGTR); the name runs
; to the end or a "/", where the switches start (SCANSW). PTR2 is TEMP8.
                LDX     BP_DMA         ; CH: the count
                LDY     #0
                CPX     #0
                BEQ     ENDCMD
TBFLP:          LDA     BP_DMA+1,Y
                STA     BP_DMA,Y
                INY
                DEX
                BNE     TBFLP
ENDCMD:         LDA     #0             ; the terminator, for CHRGTR
                STA     BP_DMA,Y
                TYA                    ; TEMP8 at it: no name
                CLC
                ADC     #<BP_DMA
                STA     PTR2
                LDA     #>BP_DMA
                ADC     #0
                STA     PTR2+1
                LDA     #<(BP_DMA-1)
                STA     TXTPTR
                LDA     #>(BP_DMA-1)
                STA     TXTPTR+1
                JSR     CHRGTR         ; leading spaces passed over
                CMP     #0
                JEQ     DONCMD
                CMP     #'/'
                BEQ     FNDSLH
                DECW    TXTPTR         ; a quote before the name
                PHA
                LDA     #'"'
                STA     (TXTPTR)
                LDA     TXTPTR
                STA     PTR2
                LDA     TXTPTR+1
                STA     PTR2+1
                PLA
                INCW    TXTPTR
ISSLH:          CMP     #'/'
                BEQ     FNDSLH
                JSR     CHRGTR
                CMP     #0
                BNE     ISSLH
                BRA     DONCMD
FNDSLH:         LDA     #0             ; the name ends at the "/"
                STA     (TXTPTR)
; SCANSW (GWINIT.ASM 237): the switches, each "/" and a letter, a colon and
; a number (GETVAL). /M: is the memory's top (MSWSIZ, MSWFLG set); /S: and
; /F: are read and ignored, as the 8086's ("GIO has dynamic record size",
; "dynamic number of files": OO-1, the files are 3 and /S: stays 128, the
; owner's, 2026-11-14); /C: is read and ignored (OO-1: COM1's buffer isn't
; sized, so CSWFLG and CSWSIZ aren't kept). Any other letter, or a switch
; not followed by "/" or the end, is Syntax error, which before start-up is
; done is CMDERR's "Error detected in command line".
SCANSW:         JSR     CHRGTR         ; the letter after the "/"
SCANS1:         JSR     MAKUPL
                CMP     #'S'
                BEQ     WASS
                CMP     #'C'
                BEQ     WASC
                CMP     #'F'
                BEQ     WASF
                CMP     #'M'
                BEQ     :+
                JMP     SNERR
:               JSR     GETVAL         ; DX: the top asked for
                LDA     RDL
                STA     MSWSIZ
                LDA     RDH
                STA     MSWSIZ+1
                LDA     #$FF
                STA     MSWFLG
FOK:            DECW    TXTPTR         ; that character again
                JSR     CHRGTR
                BEQ     DONCMD         ; the end
                JSR     SYNCHR
                .byte   '/'
                BRA     SCANS1
WASC:                                  ; OO-1: /C: read, not kept
WASS:                                  ; the source's: any value, ignored
WASF:           JSR     GETVAL
                BRA     FOK
; GETVAL (GWINIT.ASM 272): the letter passed, a colon, then CNSGET's number
; in DX.
GETVAL:         JSR     CHRGTR
                JSR     SYNCHR
                .byte   ':'
; CNSGET (GWEVAL.ASM 804): "&" is OCTCNS's &H or &O, anything else LINGET's
; line number (0 to 65529). TXTPTR returns at the character after it.
CNSGET:         CMP     #'&'
                BEQ     :+
                JMP     LINGET
:               JMP     OCTCNS
GOMERR:         LDA     #7             ; ITSA86.ASM 207 (OMERR): Out of memory
                JMP     ERROR
; DONCMD (GWINIT.ASM 280): MAPCLC's check of /M:, then the name to BUF.
; OO-1: /M:'s n is CLEAR's n (p3/bimisc.s CLEARP), the workspace's top: VAL's
; zero at n - 1, MEMSIZ and FRETOP n - 2; above n is left alone. MAPCLC's
; check: n no more than the TPA's top (the base page's BDOS address, the
; 8086's maximum), else Out of memory. And MAPINI's (ITSA86.ASM 160): n less
; 32 above TXTTAB (OO-1: the BASIC stack is fixed RAM, not taken from
; below n), else Out of memory; the 8086 makes that check after the heading,
; as a BASIC error that leaves the memory as it was: OO-1, here with
; MAPCLC's, so a /M: too small is the command line's error too. Either is,
; before start-up is done, CMDERR's.
DONCMD:         LDA     MSWFLG
                BEQ     NOMOPT
                LDA     BDOS+1         ; MAXREQ: the most there is >= n?
                CMP     MSWSIZ
                LDA     BDOS+2
                SBC     MSWSIZ+1
                BCC     GOMERR
                SEC                    ; n - 32 (none: Out of memory)
                LDA     MSWSIZ
                SBC     #32
                STA     RBL
                LDA     MSWSIZ+1
                SBC     #0
                STA     RBH
                BCC     GOMERR
                LDA     TXTTAB         ; above TXTTAB? (JBE GOMERR)
                CMP     RBL
                LDA     TXTTAB+1
                SBC     RBH
                BCS     GOMERR
                LDA     MSWSIZ         ; the new top: VAL's zero, then MEMSIZ
                STA     PTR
                LDA     MSWSIZ+1
                STA     PTR+1
                DECW    PTR
                LDA     #0
                STA     (PTR)
                DECW    PTR
                LDA     PTR
                STA     MEMSIZ
                STA     FRETOP
                LDA     PTR+1
                STA     MEMSIZ+1
                STA     FRETOP+1
NOMOPT:         LDY     #0             ; NXTBYT: to BUF, to its zero
NXTBYT:         LDA     (PTR2),Y
                STA     BUF,Y
                INY
                CMP     #0
                BNE     NXTBYT
; The heading: GETHED's (the OEM's) line, then HEDING (GWRAM.ASM 36), with
; Microsoft's copyright as GW-BASIC has it and its date's field, blank. Not
; with a program named on the command line (GWINIT.ASM 400: PRNTND, and
; FREFLG set, so no bytes free either).
                LDA     BUF
                BNE     PRNTND
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
PRNTND:         LDA     #$FF           ; errors are BASIC's from now on
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

.segment "BRAM"
MSWSIZ:         .res    2              ; GWRAM.ASM's: /M:'s value
MSWFLG:         .res    1              ; and whether there was one

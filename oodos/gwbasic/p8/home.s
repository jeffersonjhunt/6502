; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; The home block, P8 (hw/oodos.md, "The plan, phase 7": 7c's home block):
; its entries, and IBMRES.ASM's statement table.
;
; The home block holds code that no banked block calls: the statements the
; resident NEWSTT dispatches and the main loop. Start-up far calls it once
; (res/home.s) and never returns to the caller, so W3 holds it whenever the
; statement loop runs, and FARCALL puts it back after each far call. Code
; the blocks call stays resident. The build keeps the rule: a block links
; with the resident core's exports alone, and only the resident core and
; this block see the home block's entries (homent.inc, tools/gwgen.py).
;
; Resident code that ends in the main loop, from wherever W3 is (an error,
; a ^C, P2's FINI), first sets the 6502's stack back to its base, HOMSTK,
; below the outermost far call, then far calls one of these:
;
;   HREADY   READY: TOPS, STKINI's stack top, is the S it finds, the same
;            at every entry (the base less the far call's frame)
;   HGONE4   GONE4 (resident), ON ERROR's routine started from its line
;   HFINI    FINI: the program changed, its variables cleared, then MAIN
;
; STMDSP is at a fixed address, HOMTAB (gwbasic.inc), right after the
; header (home.cfg), so the resident GONE2 reads it there, in W3; STMDSX,
; the extended statements', follows it, for the resident GONE4.

.include "gwbasic.inc"

;@entry HREADY
;@entry HGONE4
;@entry HFINI
;@entry ERDEVP
.export HREADY, HGONE4, HFINI, ERDEVP
.import TOPS, GONE4, READY, FINIH, CHRGTR, MAKINT, DSEGZ, STRINI, PUTNEW, DSCTMP
.import ENDST, GOTO, IFS, LET, REM, DATA, ELSES, PRINT, SNERR
.import RUN, STOP, LIST, LLIST, SCRATH, CONT, TON, TOFF, DELETE, ADVERR
.import FOR, NEXT, INPUT, READ, RESTORE, GOSUB, RETURN, LPRINT, LINE, WHILE, WEND
.import ONGOTO, ERRORS, RESUME, SWAP, ERASE, CLEAR, OPTION, RANDOM, WRITE, DEF
.import EDIT, AUTO, RESEQ, DIM, DEFSTR, DEFINT, DEFREA, DEFDBL, SYSTEM
.import OPEN, CLOSE, RESET, LOADS, MERGES, SAVES
.import FILES, KILL, NAME, CHDIR, MKDIR, RMDIR, FIELD, LSET, RSET, PUT, GET, CHAIN
.import COMS, LCOPY, POKE, BSAVE, BLOAD
.import WIDTH, COLOR, CLS, SCREEN, LOCATE, KEY, PSET, PRESET, CIRCLE, PAINT, DRAW, VIEW, WINDOW
.import SYNCHR, FRMEVL, FRCSNG, PUSHF, POPR, GETBYT, SCRMOD

.code

HREADY:         TSX                    ; STKINI's top: the S under the far call
                STX     TOPS
                JMP     READY
HGONE4:         JMP     GONE4
HFINI:          JMP     FINIH

; ERDEVP (OO-1: the IBM manual's pp. 91-93): ERDEV or ERDEV$ in a formula
; (the resident EVALX's, TXTPTR at its $FE), read-only variables: the last
; device error's code, and its device's name. The OO-1's (the owner's answer
; 2): a device error is a physical error of OODOS's (appendix A of the
; Programmer's Guide: 1 Disk I/O, 2 Read Only, 3 File R/O, 4 Invalid Drive,
; 7 Password Error, 8 File Exists, 9 ? in Filename), which P4's PBDOS keeps
; (ERDVGT): ERDEV is its code (the high byte 0, where a PC's has the device's
; attributes), 0 before any; ERDEV$ the drive it was on, "A:" to "P:" (the
; SCB's error drive, $51), "" before any. Returns as EVAL, past the token,
; CHRGTR's flags. 7e4: and PMAP (PMAPH, below).
ERDEVP:         INCW    TXTPTR         ; the token
                LDA     (TXTPTR)
                CMP     #PMAP_XS
                BEQ     PMAPH
                FJSR    ERDVGT         ; RBL: the code
                STZ     RBH
                LDA     (TXTPTR)
                CMP     #ERDEVS_XS
                BEQ     @s
                JSR     MAKINT         ; ERDEV
                JMP     CHRGTR
@s:             LDA     RBL            ; ERDEV$: none, the null string
                BNE     @d
                LDA     #<DSEGZ
                STA     FACLO
                LDA     #>DSEGZ
                STA     FACLO+1
                LDA     #VT_STR
                STA     VALTYP
                JMP     CHRGTR
@d:             LDA     #<ERDSCB       ; the drive: the SCB's $51 (function 49)
                LDX     #>ERDSCB
                LDY     #S_SCB
                JSR     BDOS
                PHA
                LDA     #2             ; "d:"
                JSR     STRINI
                LDA     DSCTMP+1
                STA     PTR
                LDA     DSCTMP+2
                STA     PTR+1
                PLA
                CLC
                ADC     #'A'
                STA     (PTR)
                LDY     #1
                LDA     #':'
                STA     (PTR),Y
                JSR     PUTNEW
                JMP     CHRGTR

; PMAPH (OO-1, 7e4; the IBM manual's pp. 256-257: the source has no PMAP
; function): PMAP(x,n), TXTPTR at its second token byte. Console mode:
; Syntax error, as it was (no graphics there: 7d's answer 7). x a single
; (FRMEVL, FRCSNG), n a byte (GETBYT: rounded, 0-255 else Illegal function
; call); then P1's PMAPP (p1/window.s) by one far call, x in BXDX and n in
; A, which leaves the value in the FAC. Returns as EVAL, CHRGTR's flags for
; what follows ")".
PMAPH:          BIT     SCRMOD
                BMI     :+
                JMP     SNERR
:               JSR     CHRGTR
                JSR     SYNCHR
                .byte   '('
                JSR     FRMEVL         ; x
                JSR     FRCSNG
                JSR     PUSHF
                JSR     SYNCHR
                .byte   ','
                JSR     GETBYT         ; n
                PHA
                JSR     SYNCHR
                .byte   ')'
                JSR     POPR           ; BXDX: x
                PLA
                FJSR    PMAPP
                DECW    TXTPTR         ; the flags for what follows ")"
                JMP     CHRGTR

.rodata
ERDSCB:         .byte   $51, 0, 0, 0   ; function 49: get SCB $51, the drive of the last physical error

.segment "HOMTAB"
.export STMDSP
.ifndef FLAT
.assert STMDSP = HOMTAB, lderror, "STMDSP isn't at HOMTAB (home.cfg)"
.endif

; STMDSP: the statements END ($81) to LOCATE ($CA), each address less one
; for GONE2's RTS, in IBMRES.ASM's order. ADVERR (Advanced Feature): a
; later sub-phase's; SNERR: IBMRES.ASM's padding.
.define S(addr) .word addr-1
STMDSP:         S       ENDST          ; END
                S       FOR            ; FOR
                S       NEXT           ; NEXT
                S       DATA           ; DATA
                S       INPUT          ; INPUT
                S       DIM            ; DIM
                S       READ           ; READ
                S       LET            ; LET
                S       GOTO           ; GOTO
                S       RUN            ; RUN
                S       IFS            ; IF
                S       RESTORE        ; RESTORE
                S       GOSUB          ; GOSUB
                S       RETURN         ; RETURN
                S       REM            ; REM
                S       STOP           ; STOP
                S       PRINT          ; PRINT
                S       CLEAR          ; CLEAR
                S       LIST           ; LIST
                S       SCRATH         ; NEW
                S       ONGOTO         ; ON
                S       ADVERR         ; WAIT (refused, 7g)
                S       DEF            ; DEF
                S       POKE           ; POKE (p8/far3.s)
                S       CONT           ; CONT
                S       SNERR          ; (padding)
                S       SNERR          ; (padding)
                S       ADVERR         ; OUT (refused, 7g)
                S       LPRINT         ; LPRINT
                S       LLIST          ; LLIST
                S       SNERR          ; (padding)
                S       WIDTH          ; WIDTH (p8/far6.s: 7d1's, in screen mode)
                S       ELSES          ; ELSE
                S       TON            ; TRON
                S       TOFF           ; TROFF
                S       SWAP           ; SWAP
                S       ERASE          ; ERASE
                S       EDIT           ; EDIT
                S       ERRORS         ; ERROR
                S       RESUME         ; RESUME
                S       DELETE         ; DELETE
                S       AUTO           ; AUTO
                S       RESEQ          ; RENUM
                S       DEFSTR         ; DEFSTR
                S       DEFINT         ; DEFINT
                S       DEFREA         ; DEFSNG
                S       DEFDBL         ; DEFDBL
                S       LINE           ; LINE (LINE INPUT, and 7e1's graphics LINE: p8/gwmain.s)
                S       WHILE          ; WHILE
                S       WEND           ; WEND
                S       ADVERR         ; CALL (7g)
                S       SNERR          ; (padding)
                S       SNERR          ; (padding)
                S       SNERR          ; (padding)
                S       WRITE          ; WRITE
                S       OPTION         ; OPTION
                S       RANDOM         ; RANDOMIZE
                S       OPEN           ; OPEN
                S       CLOSE          ; CLOSE
                S       LOADS          ; LOAD
                S       MERGES         ; MERGE
                S       SAVES          ; SAVE
                S       COLOR          ; COLOR (p8/far6.s)
                S       CLS            ; CLS (p8/far6.s)
                S       ADVERR         ; MOTOR
.assert MOTOR_TK + 1 = BSAVE_TK, error, "STMDSP: COLOR CLS MOTOR before BSAVE"
                S       BSAVE          ; BSAVE (p8/far4.s)
                S       BLOAD          ; BLOAD (p8/far4.s)
                S       ADVERR         ; SOUND (7f)
                S       ADVERR         ; BEEP (7f)
                S       PSET           ; PSET (p8/far5.s: 7e1's, in screen mode)
                S       PRESET         ; PRESET (p8/far5.s)
                S       SCREEN         ; SCREEN (p8/far6.s; modes but 0 7e's)
                S       KEY            ; KEY (p8/far6.s; KEY(n) 7f's)
                S       LOCATE         ; LOCATE (p8/far6.s)
.assert LOCATE_TK - BLOAD_TK = 7, error, "STMDSP: SOUND .. LOCATE after BLOAD"
.assert * - STMDSP = 2 * NUMCMD, error, "STMDSP: one entry a statement"

; STMDSX (IBMRES.ASM 553): the extended statements, $FE $81 (FILES) to
; $FE $A1 (CALLS), each address less one, right after STMDSP, so the
; resident GONE4 (res/ibmres.s) reads it in W3 at HOMTAB + 2 * NUMCMD.
; SYSTEM is resident (res/oem.s); 7c's statements are stubs in this block.
.export STMDSX
.ifndef FLAT
.assert STMDSX = HOMTAB + 2 * NUMCMD, lderror, "STMDSX isn't after STMDSP (res/ibmres.s)"
.endif
STMDSX:         S       FILES          ; FILES
                S       FIELD          ; FIELD
                S       SYSTEM         ; SYSTEM
                S       NAME           ; NAME
                S       LSET           ; LSET
                S       RSET           ; RSET
                S       KILL           ; KILL
                S       PUT            ; PUT (a file's; the graphics PUT is 7e's)
                S       GET            ; GET
                S       RESET          ; RESET
                S       DATA           ; COMMON (FIVEO.ASM's COMMON: JMP DATA)
                S       CHAIN          ; CHAIN (p8/far4.s)
.repeat PAINT_XS - CHAIN_XS - 1
                S       ADVERR         ; DATE$, TIME$
.endrepeat
                S       PAINT          ; PAINT (p8/far5.s, 7e2)
                S       COMS           ; COM (p8/far3.s)
                S       CIRCLE         ; CIRCLE (p8/far5.s, 7e2)
                S       DRAW           ; DRAW (p8/far5.s, 7e2)
.repeat ERDEV_XS - DRAW_XS - 1
                S       ADVERR         ; PLAY, TIMER
.endrepeat
                S       SNERR          ; ERDEV: OO-1, a read-only variable (the IBM manual's p. 91)
                S       ADVERR         ; IOCTL: the source's, Advanced Feature
                S       CHDIR          ; CHDIR
                S       MKDIR          ; MKDIR
                S       RMDIR          ; RMDIR
.assert VIEW_XS - RMDIR_XS = 3, error, "STMDSX: SHELL ENVIRON before VIEW"
                S       ADVERR         ; SHELL (7g's question)
                S       ADVERR         ; ENVIRON (7g's question)
                S       VIEW           ; VIEW (p8/far5.s, 7e4)
                S       WINDOW         ; WINDOW (p8/far5.s, 7e4)
                S       SNERR          ; PMAP: OO-1 (7e4), a function only (ERDEVP's)
                S       SNERR          ; PALETTE: the source's (GWSTS.ASM PALETE: JMP SNERR; dropped, the IBM spec's G13)
.assert * - STMDSX = 2 * (LCOPY_XS - FILES_XS), error, "STMDSX: PALETTE before LCOPY"
                S       LCOPY          ; LCOPY (p8/far3.s)
                S       ADVERR         ; CALLS
.assert * - STMDSX = 2 * NUMSTX, error, "STMDSX: one entry an extended statement"

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
.export HREADY, HGONE4, HFINI
.import TOPS, GONE4, READY, FINIH
.import ENDST, GOTO, IFS, LET, REM, DATA, ELSES, PRINT, SNERR
.import RUN, STOP, LIST, LLIST, SCRATH, CONT, TON, TOFF, DELETE, ADVERR
.import FOR, NEXT, INPUT, READ, RESTORE, GOSUB, RETURN, LPRINT, LINE, WHILE, WEND
.import ONGOTO, ERRORS, RESUME, SWAP, ERASE, CLEAR, OPTION, RANDOM, WRITE, DEF
.import EDIT, AUTO, RESEQ, DIM, DEFSTR, DEFINT, DEFREA, DEFDBL, SYSTEM
.import OPEN, CLOSE, RESET

.code

HREADY:         TSX                    ; STKINI's top: the S under the far call
                STX     TOPS
                JMP     READY
HGONE4:         JMP     GONE4
HFINI:          JMP     FINIH

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
                S       ADVERR         ; POKE (7c)
                S       CONT           ; CONT
                S       SNERR          ; (padding)
                S       SNERR          ; (padding)
                S       ADVERR         ; OUT (refused, 7g)
                S       LPRINT         ; LPRINT
                S       LLIST          ; LLIST
                S       SNERR          ; (padding)
                S       ADVERR         ; WIDTH (7d)
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
                S       LINE           ; LINE (LINE INPUT; the graphics LINE is 7e's)
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
.repeat LOCATE_TK - LOAD_TK + 1
                S       ADVERR         ; LOAD .. LOCATE (7c-7f)
.endrepeat
.assert * - STMDSP = 2 * NUMCMD, error, "STMDSP: one entry a statement"

; STMDSX (IBMRES.ASM 553): the extended statements, $FE $81 (FILES) to
; $FE $A1 (CALLS), each address less one, right after STMDSP, so the
; resident GONE4 (res/ibmres.s) reads it in W3 at HOMTAB + 2 * NUMCMD.
; SYSTEM is resident (res/oem.s); 7c's statements are stubs in this block.
.export STMDSX
.ifndef FLAT
.assert STMDSX = HOMTAB + 2 * NUMCMD, lderror, "STMDSX isn't after STMDSP (res/ibmres.s)"
.endif
STMDSX:         S       ADVERR         ; FILES
                S       ADVERR         ; FIELD
                S       SYSTEM         ; SYSTEM
.repeat RESET_XS - NAME_XS
                S       ADVERR         ; NAME .. GET
.endrepeat
                S       RESET          ; RESET
.repeat CALLS_XS - RESET_XS
                S       ADVERR         ; COMMON .. CALLS
.endrepeat
.assert * - STMDSX = 2 * NUMSTX, error, "STMDSX: one entry an extended statement"

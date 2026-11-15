; P4's statements' stubs, in the home block (the project's own; 7c's file
; layer, hw/oodos.md, "The plan, phase 7"). Each statement of block P4 that
; STMDSP or STMDSX names (p8/home.s) is reached through a stub here, a far
; call to its banked routine and back to NEWSTT, as p8/far3.s's are. FARCALL
; keeps A and the flags both ways, so the banked routine starts with
; CHRGTR's character and flags, as the statement would.
;
;   OPEN      OPEN mode, [#]n, name[, len] and OPEN name [FOR mode] AS [#]n
;             [LEN = len] (p4/gio86.s OPENP)
;   CLOSE     CLOSE [[#]n[, [#]n ...]] (p4/gio86.s CLOSEP)
;   RESET     RESET (p4/giodsk.s RESETP)
;   LOADS     LOAD name[,R] (DSKCOM.ASM's LOAD and LRUN3, p4/dskcom.s LOADP
;             and LOADP2)
;   HLRUN     RUN name[,R] and a program named on the command line, from
;             the resident LRUN (res/far4.s): a home block entry, S set back
;             to HOMSTK first, as for HREADY; LOAD with its flag 1
;   MERGES    MERGE name (DSKCOM.ASM's MERGE, p4/dskcom.s MERGEP)
;   SAVES     SAVE name[,A|,P] (DSKCOM.ASM's SAVE, p4/dskcom.s SAVEP)
;
; LOAD, RUN name and MERGE end in the main loop, as the 8086's do: from the
; home block, never from P4. P4 opens file 0 and reads it, and returns what
; comes next: SCRTCH (which resets both stacks) between LOADP and LOADP2,
; then MAIN (a text file: its lines are read as typed, p8/gwmain.s MAIN),
; RUNC and READY or NEWSTT (a tokenised one, BINLOD's end), or SCRTCH and
; Out of memory (OUTLOD). MERGE's MAIN drops NEWSTT's return first, as
; its POP CX does (EDTMAI). SAVE returns, or (",A") goes to LIST, which
; lists to file 0; READY then closes it (FINPRT). None leaves a far call's
; frame behind: each far call has returned before the stub goes on.

.include "gwbasic.inc"

;@entry HLRUN
.export OPEN, CLOSE, RESET, LOADS, MERGES, SAVES, HLRUN
.import SCRTCH, RUNC, FINPRT, NEWSTT, READY, MAIN, OMERR, EDTMAI, LIST, CHRGTR, TOPS

.code

OPEN:           FJSR    OPENP
                RTS
CLOSE:          FJSR    CLOSEP
                RTS
RESET:          FJSR    RESETP
                RTS

; HLRUN: TOPS, STKINI's stack top, as HREADY sets it (LRUN set S back to
; HOMSTK), then LRUN2's flag 1: the files closed, the program run.
HLRUN:          TSX
                STX     TOPS
                LDX     #1
                BRA     LRUN3
; LOAD: the flag 0, the files closed, not run. LRUN3 (DSKCOM.ASM 300): the
; name and ",R" (P4, which opens file 0 and sets RUNFLG and NLONLY:
; NOTRN1's), then SCRTCH (NOTRN1's: the program, the variables and the
; files gone, but what NLONLY keeps), the ^C fix (NOTINI: start-up's line,
; 65534, made direct), and the file read (P4's LOADP2, X what comes next).
LOADS:          LDX     #0
LRUN3:          FJSR    LOADP
                JSR     SCRTCH
                LDA     CURLIN+1       ; 65534?
                INC     A
                BNE     NOTINI
                LDA     CURLIN
                CMP     #<65534
                BNE     NOTINI
                INC     CURLIN         ; direct
NOTINI:         FJSR    LOADP2         ; X: 0 MAIN, 1 loaded (A RUNFLG), 2 too big
                CPX     #1
                BEQ     LDBIN
                BCS     OUTLOD
                JMP     MAIN
OUTLOD:         JSR     SCRTCH         ; DSKCOM.ASM 426
                JMP     OMERR
; BINLOD's end (DSKCOM.ASM 396): the variables cleared, every file may be
; closed again, file 0 closed (FINPRT), then READY, or NEWSTT for RUN or ,R.
LDBIN:          STA     LDFLG          ; (not on the stack: RUNC's STKINI resets it)
                JSR     RUNC
                STZ     NLONLY
                JSR     FINPRT
                LDA     LDFLG
                BNE     :+
                JMP     READY
:               JMP     NEWSTT

MERGES:         FJSR    MERGEP
                JMP     EDTMAI         ; POP CX, JMP MAIN

SAVES:          FJSR    SAVEP          ; C set: ",A", TXTPTR at the "A"
                BCS     :+
                RTS
:               JSR     CHRGTR         ; past the "A" (DSKCOM.ASM 531)
                JMP     LIST           ; the LIST code lists to file 0

.segment "BRAM"
LDFLG:          .res    1              ; LDBIN's RUNFLG, kept over RUNC

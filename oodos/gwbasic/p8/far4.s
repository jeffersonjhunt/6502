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
;   FILES     FILES [name] (p4/giodsk.s FILESP)
;   KILL      KILL name (p4/giodsk.s KILLP)
;   NAME      NAME old AS new (p4/giodsk.s NAMEP)
;   CHDIR     CHDIR, MKDIR, RMDIR path: the directory hook, X which of the
;   MKDIR     three (p4/oem.s DIRHK; OO-1: words the 1983 table hasn't,
;   RMDIR     p2/ibmres.s)
;   FIELD     FIELD [#]n, w AS v$, ... (p4/dskcom.s FIELDP)
;   LSET      LSET, RSET v$ = s$ (p4/dskcom.s LSETP, RSETP)
;   RSET
;   PUT       PUT, GET [#]n[, record] (p4/gwsts.s PUTP, GETP: a graphics
;   GET       PUT or GET is 7e's)
;   BSAVE     BSAVE name, offset, length and BLOAD name[, offset] (7c's
;   BLOAD     tail): BPARMS here, then p4/gio86.s BSAVEP and BLOADP
;   CHAIN     CHAIN [MERGE] name[, [line][, ALL][, DELETE range]]
;             (FIVEO.ASM's CHAIN, p4/fiveo.s CHAINP; its end, CHNRET, from
;             LDBIN here and from MAIN, p8/gwmain.s)
;
; LOAD, RUN name and MERGE end in the main loop, as the 8086's do: from the
; home block, never from P4. P4 opens file 0 and reads it, and returns what
; comes next: SCRTCH (which resets both stacks) between LOADP and LOADP2,
; then MAIN (a text file: its lines are read as typed, p8/gwmain.s MAIN),
; RUNC and READY or NEWSTT (a tokenised one, BINLOD's end), or SCRTCH and
; Out of memory (OUTLOD). MERGE's MAIN drops NEWSTT's return first, as
; its POP CX does (EDTMAI). SAVE returns, or (",A") goes to LIST, which
; lists to file 0; READY then closes it (FINPRT). CHAIN too: P4 does its
; steps 1-5 and returns which way step 6 goes, CHAIN MERGE's MAIN (OKGETM's:
; NEWSTT's return dropped first, as MERGE's) or CHNENT's SCRTCH and file
; read, LOAD's (CHNSCR); at the load's end CHNRET (P4) moves the variables
; back and sets TXTPTR, and NEWSTT runs the program from here (the 8086's
; JMP NEWSTT in CHNRET). None leaves a far call's frame behind: each far
; call has returned before the stub goes on.

.include "gwbasic.inc"

;@entry HLRUN
.export OPEN, CLOSE, RESET, LOADS, MERGES, SAVES, HLRUN
.export FILES, KILL, NAME, CHDIR, MKDIR, RMDIR, FIELD, LSET, RSET, PUT, GET, CHAIN, BSAVE, BLOAD
.import SCRTCH, RUNC, FINPRT, NEWSTT, READY, MAIN, OMERR, EDTMAI, LIST, CHRGTR, TOPS
.import SYNCHR, SNERR, FCERR, SAVSEG

.code

OPEN:           FJSR    OPENP
                RTS
CLOSE:          FJSR    CLOSEP
                RTS
RESET:          FJSR    RESETP
                RTS
FILES:          FJSR    FILESP
                RTS
KILL:           FJSR    KILLP
                RTS
NAME:           FJSR    NAMEP
                RTS
FIELD:          FJSR    FIELDP
                RTS
LSET:           FJSR    LSETP
                RTS
RSET:           FJSR    RSETP
                RTS
PUT:            FJSR    PUTP
                RTS
GET:            FJSR    GETP
                RTS
CHDIR:          LDX     #0
                BRA     DIRGO
MKDIR:          LDX     #1
                BRA     DIRGO
RMDIR:          LDX     #2
DIRGO:          FJSR    DIRHK
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
CHNSCR:         JSR     SCRTCH         ; (CHAIN's CHNENT comes here)
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
                LDA     CHNFLG         ; a CHAIN (DSKCOM.ASM 401): CHNRET
                BEQ     :+
                FJSR    CHNRET
                JMP     NEWSTT
:               STZ     NLONLY
                JSR     FINPRT
                LDA     LDFLG
                BNE     :+
                JMP     READY
:               JMP     NEWSTT

MERGES:         FJSR    MERGEP
                JMP     EDTMAI         ; POP CX, JMP MAIN

; CHAIN: P4's CHAINP, X what comes next: 0 CHAIN MERGE's MAIN (OKGETM; the
; 8086's CHAIN leaves NEWSTT's return, which MAIN never takes: dropped here,
; as MERGE's), else CHNENT's way, LOAD's from its SCRTCH on.
CHAIN:          FJSR    CHAINP
                CPX     #0
                BNE     CHNSCR
                JMP     EDTMAI

SAVES:          FJSR    SAVEP          ; C set: ",A", TXTPTR at the "A"
                BCS     :+
                RTS
:               JSR     CHRGTR         ; past the "A" (DSKCOM.ASM 531)
                JMP     LIST           ; the LIST code lists to file 0


; BSAVE, BLOAD (GIO86.ASM 307, 331; 7c's tail): BPARMS here, which is
; parsing alone (the name by P4's NAMSCN, whose FILNAM and FILDEV wait in
; P4's RAM, as LIST's NULOPM finds them; the numbers by P1's ADRGET), and
; the run checked whole by P5's DSRUN (BRUN) before P4's BSAVEP, or after
; BLOADP has read the file's header and before BLDRUN reads the bytes
; (p4/gio86.s). 7d3: a run on the screen page DSRUN stages in bank 8's RAM
; (the page's bytes, for BSAVEP to save), and after BLDRUN P5's DSPUT writes
; it to the card (nothing if the run isn't the page's). The text pointer is kept, as the 8086's PUSH BX and
; LODEND's POP BX. BSAVE needs both numbers, BLOAD at most the offset
; (SNERR2). BHD8 is the header: the segment, the offset, the length.
BSAVE:          JSR     BPARMS
                INC     A
                BNE     SNERR2         ; 1 or no numbers
                JSR     BKEEP
                LDX     #1             ; DEF SEG's segment, CX the offset, DX the length
@h:             LDA     SAVSEG,X
                STA     BHD8,X
                LDA     RCL,X
                STA     BHD8+2,X
                LDA     RDL,X
                STA     BHD8+4,X
                DEX
                BPL     @h
                JSR     BRUN
                LDX     BTYPE
                FJSR    BSAVEP
                BRA     BBACK
BLOAD:          JSR     BPARMS
                CMP     #$FF
                BEQ     SNERR2         ; 2 numbers
                PHA                    ; BLODP: 1 none, 0 the offset
                JSR     BKEEP
                LDA     RCL
                STA     BOF8
                LDA     RCH
                STA     BOF8+1
                LDX     BTYPE
                FJSR    BLOADP         ; RDL-RAH: the file's header
                LDX     #5
@f:             LDA     RDL,X
                STA     BHD8,X
                DEX
                BPL     @f
                PLA
                BNE     DEFSAD         ; no offset: the file's segment and offset
                LDX     #1             ; else DEF SEG's and the offset given
@g:             LDA     SAVSEG,X
                STA     BHD8,X
                LDA     BOF8,X
                STA     BHD8+2,X
                DEX
                BPL     @g
DEFSAD:         JSR     BRUN
                FJSR    BLDRUN
                FJSR    DSPUT          ; 7d3: a run on the screen page, staged in bank 8, to the card
BBACK:          LDA     BTXP           ; POP BX
                STA     TXTPTR
                LDA     BTXP+1
                STA     TXTPTR+1
                RTS
SNERR2:         JMP     SNERR
; BKEEP: the text pointer kept (PUSH BX); A kept.
BKEEP:          LDY     TXTPTR
                STY     BTXP
                LDY     TXTPTR+1
                STY     BTXP+1
                RTS
; BRUN: BHD8's run checked by DSRUN (DX the segment, BX the offset, CX the
; length): Illegal function call if refused. Then RSI-RDI's first byte its
; 20-bit start, RDL-RAH the header again, CX the length.
BRUN:           LDX     #3
@a:             LDA     BHD8,X
                STA     RDL,X
                DEX
                BPL     @a
                LDA     BHD8+4
                STA     RCL
                LDA     BHD8+5
                STA     RCH
                FJSR    DSRUN          ; DX, BL: the start
                LDX     #2
@b:             LDA     RDL,X
                STA     RSI,X
                DEX
                BPL     @b
                LDX     #5
@c:             LDA     BHD8,X
                STA     RDL,X
                DEX
                BPL     @c
                RTS

; BPARMS (GIO86.ASM 376): PRODIR (inline: a protected program's direct line
; is Illegal function call); the name (NAMSCN; BTYPE bit 7 if it had a
; type); then none, one or two numbers after commas, A 1, 0 or $FF; CX the
; first (the offset), DX the second (BSAVE's length), and the statement's
; end after two (SNERR3).
BPARMS:         LDA     CURLIN
                AND     CURLIN+1
                INC     A
                BNE     :+
                LDA     PROFLG
                BEQ     :+
                JMP     FCERR
:               FJSR    NAMSCN
                ROR     BTYPE          ; C: a "."
                DECW    TXTPTR
                JSR     CHRGTR
                BNE     BPARM2
                LDA     #1             ; no numbers
                RTS
BPARM2:         JSR     SYNCHR
                .byte   ','
                FJSR    ADRGET         ; DX
                LDA     RDH            ; PUSH DX
                PHA
                LDA     RDL
                PHA
                DECW    TXTPTR
                JSR     CHRGTR
                BNE     BPARM3
                PLA                    ; CX the offset
                STA     RCL
                PLA
                STA     RCH
                LDA     #0             ; one
                RTS
BPARM3:         JSR     SYNCHR
                .byte   ','
                FJSR    ADRGET         ; DX the length
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     :+
                JMP     SNERR          ; SNERR3: the statement must end
:               PLA
                STA     RCL
                PLA
                STA     RCH
                LDA     #$FF           ; two
                RTS

.segment "BRAM"
LDFLG:          .res    1              ; LDBIN's RUNFLG, kept over RUNC
BTYPE:          .res    1              ; BPARMS: bit 7, the name had a type
BTXP:           .res    2              ; BSAVE's and BLOAD's text pointer, kept
BHD8:           .res    6              ; their header: the segment, the offset, the length
BOF8:           .res    2              ; BLOAD's offset given (CX)

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
;   PUT       PUT, GET [#]n[, record] (p4/gwsts.s PUTP, GETP); a graphics
;   GET       PUT or GET, P5's GPUTG (7e3)
;   BSAVE     BSAVE name, offset, length and BLOAD name[, offset] (7c's
;   BLOAD     tail): BPARMS here, then p4/gio86.s BSAVEP and BLOADP
;   CHAIN     CHAIN [MERGE] name[, [line][, ALL][, DELETE range]]
;             (FIVEO.ASM's CHAIN, p8/chain.s CHAINP, by JSR since F3's M4;
;             its end, CHNRET, from LDBIN here and from MAIN, p8/gwmain.s)
;
; LOAD, RUN name and MERGE end in the main loop, as the 8086's do: from the
; home block, never from P4. P4 opens file 0 and reads it, and returns what
; comes next: SCRTCH (which resets both stacks) between LOADP and LOADP2,
; then MAIN (a text file: its lines are read as typed, p8/gwmain.s MAIN),
; RUNC and READY or NEWSTT (a tokenised one, BINLOD's end), or SCRTCH and
; Out of memory (OUTLOD). MERGE's MAIN drops NEWSTT's return first, as
; its POP CX does (EDTMAI). SAVE returns, or (",A") goes to LIST, which
; lists to file 0; READY then closes it (FINPRT). CHAIN too: CHAINP (the
; home block's p8/chain.s since F3's M4, its file opened and its load begun
; by P4) does its steps 1-5 and returns which way step 6 goes, CHAIN MERGE's
; MAIN (OKGETM's: NEWSTT's return dropped first, as MERGE's) or CHNENT's
; SCRTCH and file read, LOAD's (CHNSCR); at the load's end CHNRET moves the
; variables back and sets TXTPTR, and NEWSTT runs the program from here (the 8086's
; JMP NEWSTT in CHNRET). None leaves a far call's frame behind: each far
; call has returned before the stub goes on.

.include "gwbasic.inc"
.import CHAINP, CHNRET

;@entry HLRUN
.export OPEN, CLOSE, RESET, LOADS, MERGES, SAVES, HLRUN
.export FILES, KILL, NAME, CHDIR, MKDIR, RMDIR, FIELD, LSET, RSET, PUT, GET, CHAIN, BSAVE, BLOAD
.import SCRTCH, RUNC, FINPRT, NEWSTT, READY, MAIN, OMERR, EDTMAI, LIST, CHRGTR, TOPS
.import SYNCHR, SNERR, FCERR, SAVSEG, SCRMOD, TXSYNC
.import FRMEVL, FRESTR, OUTDO, CRDO, STROUT, FRCDBL

ASCCTZ          = 26                   ; ^Z (DSPUTH: a file's end)

.code

OPEN:           FJSR    OPENP
                RTS
CLOSE:          FJSR    CLOSEP
                RTS
RESET:          FJSR    RESETP
                RTS
; FILES (OO-1, the IBM spec's fix-up: the IBM manual's pp. 102-103, the
; owner's answer 1): the directory's name on a line before the names, here
; the drive's root ("A:\": OODOS has no directories), and the drive's free
; bytes on a line after them (" n Bytes free": function 46's records x 128,
; printed as a double); P4's FILESP lists the names as the source does, and
; is unchanged. The drive is the name's "d:" or the current one (function
; 25), A-P only. No heading for a name that isn't a string, is null, or
; names another drive (FILESQ's Type mismatch, Bad file name), nor the free
; bytes when the listing ends in an error. The name is evaluated once, here,
; and P4's FILESQ lists from its descriptor (review round 1, F10).
FILES:          PHP                    ; CHRGTR's Z: no name
                LDY     #DRV_GET       ; the current drive
                JSR     BDOS
                STA     FLSDRV
                PLP
                BNE     @name
                JSR     FLSHED         ; no name: the current drive's
                DECW    TXTPTR         ; (CHRGTR's Z again for FILESP)
                JSR     CHRGTR
                FJSR    FILESP
                BRA     @free
@name:          JSR     FRMEVL         ; the name, once (review round 1, F10):
                JSR     FRESTR         ; P4's FILESQ takes its descriptor (a
                LDA     RBL            ; number: Type mismatch)
                STA     FLSDSC
                LDA     RBH
                STA     FLSDSC+1
                LDA     (RBL)          ; its length
                BEQ     @list          ; null: FILESQ's Bad file name
                CMP     #2
                BCC     @h             ; one character: the current drive
                LDY     #1             ; its bytes
                LDA     (RBL),Y
                STA     PTR
                INY
                LDA     (RBL),Y
                STA     PTR+1
                LDY     #1
                LDA     (PTR),Y
                CMP     #':'
                BNE     @h
                LDA     (PTR)          ; "d:": its drive, A-P only (else no
                AND     #$DF           ; heading: FILESQ's Bad file name)
                SEC
                SBC     #'A'
                CMP     #16
                BCS     @list
                STA     FLSDRV
@h:             JSR     FLSHED
@list:          LDA     FLSDSC
                STA     RBL
                LDA     FLSDSC+1
                STA     RBH
                FJSR    FILESQ         ; (an error ends it: no free bytes)
@free:
                LDA     #<FLSREC       ; function 46's 3 bytes to FLSREC
                LDX     #>FLSREC
                LDY     #F_DMAOFF
                JSR     BDOS
                LDA     FLSDRV
                LDY     #DRV_SPACE
                JSR     BDOS
                CMP     #0
                BNE     @x             ; (its error: no line)
                LDA     FLSREC         ; the records, 24 bits, made a single
                STA     FACLO          ; (exact), times 128: P4's DSKLOF's way
                LDA     FLSREC+1
                STA     FACLO+1
                LDA     FLSREC+2
                STA     FACM1
                LDA     #VT_SNG
                STA     VALTYP
                STZ     FACEXP
                LDA     FACLO
                ORA     FACLO+1
                ORA     FACM1
                BEQ     @dbl
                LDA     #$80 + 24 + 7
                STA     FACEXP
@n:             BIT     FACM1
                BMI     @sgn
                DEC     FACEXP
                ASL     FACLO
                ROL     FACLO+1
                ROL     FACM1
                BRA     @n
@sgn:           LDA     FACM1
                AND     #$7F
                STA     FACM1
@dbl:           JSR     FRCDBL         ; a double: every digit
                FJSR    FOUT           ; " n", FBPTR at it
                LDA     FBPTR
                STA     PTR
                LDA     FBPTR+1
                STA     PTR+1
                JSR     STROUT
                LDA     #<FLSBYT
                STA     PTR
                LDA     #>FLSBYT
                STA     PTR+1
                JSR     STROUT
                JMP     CRDO
@x:             RTS
; FLSHED (OO-1): FILES's heading, FLSDRV's root, "A:\".
FLSHED:         LDA     FLSDRV
                CLC
                ADC     #'A'
                JSR     OUTDO
                LDA     #':'
                JSR     OUTDO
                LDA     #$5C           ; a backslash
                JSR     OUTDO
                JMP     CRDO
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
; PUT, GET (7e3): GWSTS.ASM's PARSE's test here: "(" or "@" after the word
; is the graphics statement (P5's GPUTG, the corners there: the owner's
; answer 1), and so is GET's STEP (answer 5: the IBM manual's p. 115, a
; relative first corner; a file's GET can't begin with it), in screen mode,
; TXBUF emptied first (P5's rows pass through it: answer 3). Anything else,
; and every graphics form in console mode, P4's PUTP and GETP as before
; (Advanced Feature for the graphics there, its GRPVER).
PUT:            CMP     #'('
                BEQ     :+
                CMP     #'@'
                BNE     PUTF
:               BIT     SCRMOD
                BPL     PUTF
                LDX     #1
                BRA     GPUTH
PUTF:           FJSR    PUTP
                RTS
GET:            BIT     SCRMOD
                BPL     GETF
                CMP     #'('
                BEQ     :+
                CMP     #'@'
                BEQ     :+
                CMP     #STEP_TK
                BNE     GETF
:               LDX     #0
GPUTH:          PHX
                JSR     TXSYNC
                PLA
                FJSR    GPUTG
                RTS
GETF:           FJSR    GETP
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
                JSR     CHNRET
                JMP     NEWSTT
:               STZ     NLONLY
                JSR     FINPRT
                LDA     LDFLG
                BNE     :+
                JMP     READY
:               JMP     NEWSTT

MERGES:         FJSR    MERGEP
                JMP     EDTMAI         ; POP CX, JMP MAIN

; CHAIN: CHAINP (p8/chain.s), X what comes next: 0 CHAIN MERGE's MAIN (OKGETM; the
; 8086's CHAIN leaves NEWSTT's return, which MAIN never takes: dropped here,
; as MERGE's), else CHNENT's way, LOAD's from its SCRTCH on.
CHAIN:          JSR     CHAINP
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
; the run checked whole by DSRUNH (BRUN; P5's DSRUN until 7e2) before P4's BSAVEP, or after
; BLOADP has read the file's header and before BLDRUN reads the bytes
; (p4/gio86.s). 7d3: a run on the screen page DSRUN stages in bank 8's RAM
; (the page's bytes, for BSAVEP to save), and after BLDRUN DSPUTH writes
; it to the card (nothing if the run isn't the page's). The text pointer is
; kept, as the 8086's PUSH BX and LODEND's POP BX. BSAVE needs both numbers,
; BLOAD at most the offset (SNERR2). BHD8 is the header: the segment, the
; offset, the length.
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
                DEC     DSBSV          ; 7e3: BSAVE's (DSGSR fills the graphics page's staging)
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
                JSR     DSPUTH         ; 7d3: a run on the screen page, staged in bank 8, to the card
BBACK:          LDA     BTXP           ; POP BX
                STA     TXTPTR
                LDA     BTXP+1
                STA     TXTPTR+1
                RTS
SNERR2:         JMP     SNERR
; BKEEP: the text pointer kept (PUSH BX); A kept. 7e3: DSBSV cleared (a
; BLOAD's; BSAVE then sets it, for DSGSR).
BKEEP:          STZ     DSBSV
                LDY     TXTPTR
                STY     BTXP
                LDY     TXTPTR+1
                STY     BTXP+1
                RTS
; BRUN: BHD8's run checked by DSRUNH (DX the segment, BX the offset, CX the
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
                JSR     DSRUNH         ; DX, BL: the start (7e2: the home block's)
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


; --- DSRUNH, DSPUTH: BLOAD's and BSAVE's runs (7c's tail, 7d3) -----------------------------

; 7e2 (the owner's option 3 of 2026-11-19): moved here from P5's seed
; (p5/seed.s) to give P5 room, the routines as they were there but for
; their names and two things: what they need of P5 (its graphics mode and
; DSB8, GX_BANK8's answer) comes by one far call to P5's DSINFO, on the
; screen page's path only; and the scratch byte (the probe's DM_COPY
; target, and DSPUTH's ^Z test) is this block's own, its physical address
; found by function 73 at the first run (DSSPH). BSAVE and BLOAD call them
; here without a far call. DSADR, BLKOF, DSPAGE, DSVX, DSSWAP and DSCALL are
; copies of P5's, which DSBYTE (PEEK and POKE) keeps.
;
;   DSRUNH    BLOAD's and BSAVE's run checked whole, before anything moves:
;             its 20-bit start returned; a run on the screen page staged
;             in bank 8's RAM (7d3)
;   DSPUTH    after BLOAD's BLDRUN: a run DSRUNH staged for the screen
;             page, from bank 8's RAM to the card, as far as the file gave
;             bytes (7d3)
;
; DSRUN's run is linear, from segment x 16 + offset for its length (the
; 8086 wraps the offset inside the segment: a recorded deviation). Refused,
; Illegal function call: a run past $FFFFF (it would meet bank F, OODOS's);
; one that meets bank 8 but for a run on the screen page; one with any
; 16 KB block that function 83 won't copy from, found by a one-byte DM_COPY
; from each block's first byte (83 decides by the block, so one byte
; answers for all of it). A length of 0 is no run: nothing checked.
;
; 7d3, the screen page (the owner's answers 3-6 of 2026-11-18): a run inside
; bank 8, in screen mode, whose last byte is below the page's reach (DSRUN
; routes it by the 20-bit address, as DSBYTE does) is staged in bank 8's own
; RAM at its own address, $80000 + its offset (GW-BASIC's after GX_BANK8,
; B7's question 1): DSSRUN copies the page's bytes there by DM_VREAD, P4's
; BSAVEP saves them from there and P4's BLDRUN loads the file over them, and
; DSPUT then writes to the card by DM_VWRITE the bytes the file gave (a
; file shorter than its header: those before its last record's ^Z), so the
; rest stays as it was, as a PC's BLOAD. P4 is unchanged: its DM_COPY and
; DM_LOAD reach only GW-BASIC's RAM, and the card only function 83's port
; operations. Refused, Illegal function call: in console mode, where
; GX_BANK8 refused (bank 8 isn't RAM: a revision 1 bridge, a BIOS before
; 0.17; DSB8), a run from bank 7 into bank 8 or from it into bank 9, and one
; past the reach. The staging takes $80000 up to the reach ($812BF at most):
; nothing else of GW-BASIC's uses bank 8's RAM.
;
; 7e3, SCREEN 1 and 2's page (the PC's CGA layout, 7e's answer 9; the owner's
; answers 2 and 7 of the 7e3 plan): DSSRUN's DSGSR, the run's last byte below
; &H4000, staged the same way, $80000 up to $83FFF; BSAVE's staging filled
; from the card and BLOAD's written to it by P5's DSGRUN (one far call each),
; which only P5 may do (the aperture). The gaps (&H1F40-&H1FFF, &H3F40-&H3FFF)
; are bank 8's RAM at their own address: never filled, never written to the
; card, so BSAVE saves them and BLOAD keeps the file's there.

; DSRUNH: DX the segment, BX the offset, CX the length (0: none). The run
; checked whole (above): Illegal function call, nothing moved, if refused;
; else DX and BL its 20-bit start (DSA).
DSRUNH:         STZ     DSSCF          ; 7d3: no run staged for DSPUT yet
                LDA     RDL
                STA     GXQ
                LDA     RDH
                STA     GXQ+1
                LDA     RBL
                STA     GXP
                LDA     RBH
                STA     GXP+1
                JSR     DSADR
                LDA     RCL            ; no bytes: no run
                ORA     RCH
                BNE     :+
                JMP     @ok
:               LDA     RCL            ; DSE: its last byte, DSA + CX - 1
                BNE     :+
                DEC     RCH
:               DEC     RCL
                CLC
                LDA     DSA
                ADC     RCL
                STA     DSE
                LDA     DSA+1
                ADC     RCH
                STA     DSE+1
                LDA     DSA+2
                ADC     #0
                STA     DSE+2
                INC     RCL            ; CX back
                BNE     :+
                INC     RCH
:               LDA     DSE+2
                CMP     #$10
                BCS     @fc            ; past $FFFFF: into bank F
                CMP     #8             ; meets bank 8: from it or below, to it or past
                BCC     @blocks
                BNE     @past
                LDA     DSA+2          ; to it: from it, the screen page (7d3); else from below
                CMP     #8
                BCC     @fc
                JSR     DSSRUN
                BRA     @ok
@past:          LDA     DSA+2
                CMP     #9
                BCC     @fc 
@blocks:        LDX     #DSE - DSA     ; the last block
                JSR     BLKOF
                STA     DSLAST
                LDX     #0             ; from the first
                JSR     BLKOF
                JSR     DSSPH          ; 7e2: the scratch byte's physical address
@probe:         STA     DSBLK
                STZ     DSCB+1         ; its first byte: the block x $4000
                STZ     DSCB+2
                LSR     A
                ROR     DSCB+2
                LSR     A
                ROR     DSCB+2
                STA     DSCB+3
                LDX     #2
@s:             LDA     DSSP,X         ; to the scratch byte
                STA     DSCB+4,X
                DEX
                BPL     @s
                JSR     DSCALL
                CMP     #$FF
                BEQ     @fc 
                LDA     DSBLK
                CMP     DSLAST
                INC     A
                BCC     @probe         ; short of the last
@ok:            LDA     DSA            ; DX, BL: the start
                STA     RDL
                LDA     DSA+1
                STA     RDH
                LDA     DSA+2
                STA     RBL
                RTS
@fc:            JMP     FCERR

; DSSRUN (7d3): a run inside bank 8, in screen mode, with bank 8 the
; program's RAM (GX_BANK8): on the active page (P6's SCPAGP, the cache
; forgotten) within its reach, else Illegal function call. Staged in bank
; 8's RAM at its own address ($80000 + its offset): the page's bytes copied
; there by DM_VREAD, and DSSCF set for DSPUT.
DSSRUN:         BIT     SCRMOD
                BPL     @fc
                FJSR    DSINFO         ; 7e2: P5's GMODEV (A), DSB8 (X)
                CPX     #$80
                BCS     @fc            ; GX_BANK8 refused
                CMP     #3             ; 7e5: the OO-1's modes, SCREEN 0's route: the page
                BCS     @lin           ; and its reach DSINFO's (T0-T2, GXT)
                CMP     #0
                BNE     DSGSR          ; 7e3: SCREEN 1 and 2's page
                SEC
                FJSR    SCPAGP
@pg:            JSR     DSPAGE
                BCS     @fc
                LDX     #2
@s:             LDA     DSA,X
                STA     DSPH,X
                DEX
                BPL     @s
                LDA     RCL
                STA     DSVL
                LDA     RCH
                STA     DSVL+1
                LDA     #DM_VREAD
                JSR     DSVX
                DEC     DSSCF
                RTS
@lin:           LDA     T0
                LDX     T1
                LDY     T2
                BRA     @pg
@fc:            JMP     FCERR

; DSGSR (7e3): SCREEN 1 and 2's page, the PC's CGA layout (P5's DSGRUN:
; p5/seed.s): the run's last byte below &H4000, else Illegal function call;
; staged at its own address as SCREEN 0's (DSPH, DSVL; DSSCF 1, a graphics
; run, for DSPUTH). BSAVE's (DSBSV) staging filled from the card by DSGRUN,
; TXBUF emptied first; BLOAD's not (DSPUTH writes only the bytes the file
; gave). No SCPAGP: the text page isn't reached in a graphics mode. CX kept.
DSGSR:          LDA     DSE+1
                CMP     #$40
                BCS     @fc
                LDX     #2
@s:             LDA     DSA,X
                STA     DSPH,X
                DEX
                BPL     @s
                LDA     RCL
                STA     DSVL
                LDA     RCH
                STA     DSVL+1
                LDA     #1
                STA     DSSCF
                BIT     DSBSV
                BPL     @x
                JSR     TXSYNC
                LDA     RCH            ; CX kept (BRUN's)
                PHA
                LDA     RCL
                PHA
                STA     RDL            ; DSGRUN: BX the offset, DX the bytes, C set
                LDA     RCH
                STA     RDH
                LDA     DSA
                STA     RBL
                LDA     DSA+1
                STA     RBH
                SEC
                FJSR    DSGRUN
                PLA
                STA     RCL
                PLA
                STA     RCH
@x:             RTS
@fc:            JMP     FCERR

; DSPUTH (7d3): after BLOAD's BLDRUN, a run DSRUN put on the screen page:
; the bytes the file gave, staged in bank 8's RAM, to the card (DSVC, DSPH
; and DSVL as DSSRUN left them: nothing between calls DSBYTE). Only those
; (review round 1, F3: a PC's BLOAD of a short file leaves the rest of the
; screen as it was): the run less the CX that BLDRUN's DFSTLD left (it
; counts CX down by the bytes it reads, and FINPRT's close keeps AX-DX);
; and if the file ended first, its last record's bytes only to its first
; ^Z, the file's end as APPFIN finds it (CP/M's files are whole records:
; the rest of the record is DSKCLS's ^Z and whatever the buffer held).
DSPUTH:         LDA     DSSCF
                BNE     :+
                RTS                    ; no run staged
:               STA     DSSCG          ; 7e3: 1, a graphics run
                STZ     DSSCF
                SEC                    ; DSVL: the bytes read, the run less CX
                LDA     DSVL
                SBC     RCL
                STA     DSVL
                LDA     DSVL+1
                SBC     RCH
                STA     DSVL+1
                LDA     RCL
                ORA     RCH
                BEQ     DSPW           ; the whole run read
                SEC                    ; the file ended: its last record, DSVL - 128 on (or 0)
                LDA     DSVL
                SBC     #128
                TAX
                LDA     DSVL+1
                SBC     #0
                BCS     :+
                LDA     #0
                TAX
:               STX     DSE            ; DSE (DSRUNH's, free now): the run's byte looked at
                STA     DSE+1
                JSR     DSSPH          ; 7e2: the scratch byte's physical address
@z:             LDA     DSE            ; past the bytes read: no ^Z
                CMP     DSVL
                LDA     DSE+1
                SBC     DSVL+1
                BCS     DSPW
                CLC                    ; its staged byte to the scratch byte (DM_COPY)
                LDA     DSPH
                ADC     DSE
                STA     DSCB+1
                LDA     DSPH+1
                ADC     DSE+1
                STA     DSCB+2
                LDA     DSPH+2
                ADC     #0
                STA     DSCB+3
                LDX     #2
@s:             LDA     DSSP,X
                STA     DSCB+4,X
                DEX
                BPL     @s
                JSR     DSCALL
                CMP     #$FF
                BEQ     DSPW           ; (refused: none, the bytes read kept)
                LDA     DSSB
                CMP     #ASCCTZ
                BEQ     @end
                INC     DSE
                BNE     @z
                INC     DSE+1
                BRA     @z
@end:           LDA     DSE            ; the ^Z: the file's end
                STA     DSVL
                LDA     DSE+1
                STA     DSVL+1
DSPW:           LDA     DSVL           ; none: nothing written
                ORA     DSVL+1
                BEQ     @x
                LDA     DSSCG          ; 7e3: SCREEN 1 and 2's page by P5's DSGRUN
                BPL     @g
                LDA     #DM_VWRITE
                JMP     DSVX
@g:             LDA     DSVL           ; BX the offset, DX the bytes, C clear: to the card
                STA     RDL
                LDA     DSVL+1
                STA     RDH
                LDA     DSA
                STA     RBL
                LDA     DSA+1
                STA     RBH
                JSR     TXSYNC
                CLC
                FJSR    DSGRUN
@x:             RTS

; DSSPH (7e2): DSSP the physical address of the scratch byte DSSB, by
; function 73 while this block is in W3, once (DSSPF).
DSSPH:          PHA
                LDA     DSSPF
                BNE     @x
                LDA     #<DSSB
                STA     DSPB
                LDA     #>DSSB
                STA     DSPB+1
                LDA     #<DSPB
                LDX     #>DSPB
                LDY     #O_PHYSADDR
                JSR     BDOS
                LDA     DSPB+2         ; the paragraph x 16 + the offset (0-15)
                STA     DSSP
                LDA     DSPB+3
                STA     DSSP+1
                STZ     DSSP+2
                LDX     #4
@x16:           ASL     DSSP
                ROL     DSSP+1
                ROL     DSSP+2
                DEX
                BNE     @x16
                LDA     DSSP
                ORA     DSPB
                STA     DSSP
                DEC     DSSPF
@x:             PLA
                RTS

; DSPAGE (7d3): DSVC the page's card address A/X/Y + DSA's offset; C set if
; DSE's offset (the run's last byte) isn't below the reach, GXT.
DSPAGE:         CLC
                ADC     DSA
                STA     DSVC
                TXA
                ADC     DSA+1
                STA     DSVC+1
                TYA
                ADC     #0
                STA     DSVC+2
                LDA     GXT            ; (7e5: 0 the whole 64 KB, SCREEN 11's)
                ORA     GXT+1
                CMP     #1
                BCC     :+
                LDA     DSE
                CMP     GXT
                LDA     DSE+1
                SBC     GXT+1
:               RTS

; DSVX (7d3): function 83's A (DM_VREAD, from the card's DSVC to the
; physical DSPH; DM_VWRITE, the other way), DSVL bytes.
DSVX:           STA     DSCB
                LDX     #2
@a:             LDA     DSVC,X
                STA     DSCB+1,X
                LDA     DSPH,X
                STA     DSCB+4,X
                DEX
                BPL     @a
                LDA     DSVL
                STA     DSCB+7
                LDA     DSVL+1
                STA     DSCB+8
                LDA     DSCB
                CMP     #DM_VREAD
                BEQ     DSGO
                JSR     DSSWAP
                BRA     DSGO

; DSSWAP: DSCB's source and destination swapped.
DSSWAP:         LDX     #2
@b:             LDA     DSCB+1,X
                PHA
                LDA     DSCB+4,X
                STA     DSCB+1,X
                PLA
                STA     DSCB+4,X
                DEX
                BPL     @b
                RTS

; DSCALL: function 83's DM_COPY of DSCB's one byte. A the result.
DSCALL:         LDA     #DM_COPY
                STA     DSCB
                LDA     #1
                STA     DSCB+7
                STZ     DSCB+8
DSGO:           LDA     #<DSCB
                LDX     #>DSCB
                LDY     #O_DMA
                JMP     BDOS

; DSADR: DSA = GXQ (a segment) x 16 + GXP (an offset), wrapping at 1 MB.
DSADR:          LDA     GXQ
                STA     DSA
                LDA     GXQ+1
                STA     DSA+1
                STZ     DSA+2
                LDX     #4
@x16:           ASL     DSA
                ROL     DSA+1
                ROL     DSA+2
                DEX
                BNE     @x16
                CLC
                LDA     DSA
                ADC     GXP
                STA     DSA
                LDA     DSA+1
                ADC     GXP+1
                STA     DSA+1
                LDA     DSA+2
                ADC     #0
                AND     #$0F
                STA     DSA+2
DSRET:          RTS

; BLKOF: A the 16 KB block (0-63) of DSA (X 0) or DSE (X 3), a 20-bit
; address: A16-A19 x 4 and A14-A15.
BLKOF:          LDA     DSA+1,X
                ASL     A
                LDA     DSA+2,X
                ROL     A
                STA     DSBLK
                LDA     DSA+1,X
                ASL     A
                ASL     A
                LDA     DSBLK
                ROL     A
                RTS

.segment "BRAM"
FLSDSC:         .res    2              ; FILES's name's descriptor, kept (OO-1)
FLSDRV:         .res    1              ;   its drive, 0 A:
FLSREC:         .res    3              ;   the drive's free records (function 46)
LDFLG:          .res    1              ; LDBIN's RUNFLG, kept over RUNC
BTYPE:          .res    1              ; BPARMS: bit 7, the name had a type
BTXP:           .res    2              ; BSAVE's and BLOAD's text pointer, kept
BHD8:           .res    6              ; their header: the segment, the offset, the length
BOF8:           .res    2              ; BLOAD's offset given (CX)
.assert DSE = DSA + 3, error, "BLKOF takes DSE as DSA + 3"
DSA:            .res    3              ; the 20-bit address (DSADR)
DSE:            .res    3              ; DSRUNH: the run's last byte (just past DSA: BLKOF)
DSBLK:          .res    1              ; a block (BLKOF; DSRUNH's probe)
DSLAST:         .res    1              ; DSRUNH: the run's last block
DSCB:           .res    9              ; function 83's DM_COPY block
DSPB:           .res    4              ; function 73's
DSSB:           .res    1              ; 7e2: the scratch byte
DSSP:           .res    3              ;   its physical address (DSSPH)
DSSPF:          .res    1              ;   nonzero once DSSP is found
DSVC:           .res    3              ; 7d3: the screen page's card address (DSPAGE)
DSPH:           .res    3              ;   the physical side (the scratch byte, or bank 8's RAM)
DSVL:           .res    2              ;   the bytes
DSSCF:          .res    1              ; 7d3: DSRUNH's run staged for the screen page, for DSPUTH
                                       ;   ($FF SCREEN 0's; 7e3: 1 SCREEN 1 or 2's)
DSSCG:          .res    1              ; 7e3: DSPUTH's copy of it
DSBSV:          .res    1              ; 7e3: bit 7 a BSAVE (BRUN serves both)

.rodata
FLSBYT:         .byte   " Bytes free", 0 ; FILES's (OO-1)

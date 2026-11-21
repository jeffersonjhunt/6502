; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GW-BASIC's zero page, tokens and shared names (hw/oodos.md, "The plan,
; phase 7"), from the spike's (oodos/spike/gwbasic/). OODOS gives a program
; $00-$EF at fixed addresses (hw/oodos.md, "Zero page"); this uses $00-$EF,
; in the ranges below. Every module includes it, resident or banked: the
; blocks share the resident core's zero page, so it is all equates here.
;
; The 8086 source keeps the 8080's register pairs (HL in BX, DE in DX, BC in
; CX). Here they are zero page pseudo-registers in the 8086's byte order, so
; a translated line reads like its original:
;
;   RDL RDH RBL RBH   DX and BX. For single precision they are one MBF
;                     number in memory order (the 8086's BX:DX, the 8080's
;                     B,C,D,E): RDL the low mantissa byte, RBL the sign and
;                     high mantissa, RBH the exponent. For integers DX is the
;                     left operand and BX the right.
;   RAL RAH           AX. RAH is the rounding ("overflow") byte below the
;                     mantissa in the single precision routines.
;   RCL RCH           CX: counts; in the formula evaluator CH is the
;                     precedence just popped.
;
; BX is also the 8086 interpreter's text pointer. Here that is TXTPTR, which
; the math package never touches, so the 8086's PUSH BX / POP BX around math
; calls has no counterpart.
;
; Pointers. The 8086 passes addresses of numbers in BX, DI and SI, and they
; may point at the FAC. OODOS's rule 2 forbids 16-bit pointers to zero page
; (a 65C816's direct page can move), so here a number in zero page is named
; by its zero page address in X, and one in memory by PTR (or PTR2), used as
; (PTR),Y.
;
; The FAC, ARG and their neighbours are laid out as in the 8086's data
; segment, because the double precision code walks across them: DFACL is the
; low half of a double, FACLO the low byte of a single, FACM1 the sign and
; high mantissa ($FAC-1), FACEXP the exponent ($FAC) and FACSGN the unpacked
; sign ($FAC+1).
;
; Two stacks. The 8086 keeps BASIC's temporaries (the formula evaluator's
; operands and operators) on the CPU stack beside return addresses. Here the
; return addresses stay on the 6502's stack and the data go on the BASIC
; stack at SSP (SPUSH and SPOP in bimisc.s), which grows down through the
; TPA, so a deep formula costs page 1 only its return addresses.

.setcpu "65C02"
.include "oodos.inc"

; --- Pseudo-registers ($00-$0F) ---------------------------------------------
RDL             = $00
RDH             = $01
RBL             = $02
RBH             = $03
RAL             = $04
RAH             = $05
RCL             = $06
RCH             = $07
RSI             = $08                  ; SI, 2 bytes
RDI             = $0A                  ; DI, 2 bytes
RBP             = $0C                  ; BP, 2 bytes
PTR             = $0E                  ; a pointer into memory, for (PTR),Y

; --- Math scratch and state ($10-$1F) ---------------------------------------
T0              = $10                  ; T0-T7: one routine's work bytes
T1              = $11
T2              = $12
T3              = $13
T4              = $14
T5              = $15
T6              = $16
T7              = $17
FSUBF           = $18                  ; $FADDS: bit 7 set to subtract the mantissas
FLGOV           = $19                  ; $FLGOV: 0 print overflow, 1 once more, 2 not
FLGOVC          = FLGOV                ; FLGOVC: the same byte in the 8086 (GWDATA.ASM labels both)
VALTYP          = $1B                  ; 2 integer, 3 string, 4 single, 8 double
FMTCX           = $1C                  ; $FMTCX: CL commas, CH digits to the point
FMTAL           = $1E                  ; $FMTAL: PRINT USING's format flags
FMTAH           = $1F

; --- The FAC and its neighbours ($20-$58) -----------------------------------
FACX            = $20                  ; $DFACL-8..$DFACL-1: the double product's low half
DFACL           = $28                  ; $DFACL: a double's low 4 bytes
FACLO           = $2C                  ; $FACLO
FACM1           = $2E                  ; $FAC-1: sign and high mantissa
FACEXP          = $2F                  ; $FAC: exponent
FACSGN          = $30                  ; $FAC+1: the sign while unpacked
ARGX            = $31                  ; $ARGLO-1: ARG's overflow byte
ARGLO           = $32                  ; $ARGLO, 8 bytes
ARGM1           = $38                  ; $ARG-1
ARGEXP          = $39                  ; $ARG
DBUFF           = $3A                  ; $DBUFF, 9 bytes: the multiplier's copy
ZLO             = $43                  ; $ZLO, 8 bytes: $SIGD's Z
Z1LO            = $4B                  ; $Z1LO, 8 bytes: $SIGD's Z1
FBPTR           = $53                  ; into FBUFF: the 8086's BX in the output code
DPADR           = $55                  ; $DPADR: where FOTED put the "."
FOEXP           = $57                  ; $FOTNV's decimal exponent (the 8086's DI)
SPN             = $58                  ; SPUSH and SPOP's byte count

; --- The interpreter ($60-$8F) ----------------------------------------------
TXTPTR          = $60                  ; the text pointer (the 8086's BX)
TEMP2           = $62
TEMP3           = $64
TEMP            = $66
CONTXT          = $68                  ; the text after an embedded constant
CONSAV          = $6A                  ; the constant's token
CONTYP          = $6B                  ; its VALTYP
CONLO           = $6C                  ; its value, 8 bytes
OPRTYP          = $74                  ; the operator APPLOP applies
CURLIN          = $75                  ; the current line number
SAVTXT          = $77                  ; the text pointer at the statement's start
TXTTAB          = $79                  ; the program's first line
SSP             = $7B                  ; the BASIC stack's pointer (it grows down)
SAVSTK          = $7D                  ; S, then SSP, at the statement's start
ONGSBF          = $80                  ; an ON event is waiting
TRCFLG          = $81                  ; TRON
PTRFLG          = $82                  ; line numbers in the text have become pointers
ERRFLG          = $83                  ; the last error's number
ERRLIN          = $84                  ; its line
ONELIN          = $86                  ; ON ERROR's line
STKLOW          = $88                  ; how low the BASIC stack may grow
SYNPTR          = $8A                  ; SYNCHR's return address
PTR2            = $8C                  ; a second pointer into memory

; --- The rest of the math, in P1 ($90-$9F) ----------------------------------
FINDI           = $90                  ; $FIN: the 8086's DI, digits past the point (XTON: N)
FINCX           = $92                  ; its CX: $FFFF before a point, 0 after
FINSI           = $94                  ; its SI: the exponent's sign, 0 or $FF
FINDX           = $95                  ; its DX: the exponent typed
FINNEG          = $97                  ; the number's sign: $FF for a "-"
FINDIG          = $98                  ; the digit $FIDIG adds
PX              = $99                  ; $POLY's x, 4 bytes
PCP             = $9D                  ; $POLY's coefficient pointer
PCN             = $9F                  ; $POLY's coefficients left

; --- Variables and strings ($A0-$BF): BIPTRG.ASM's and BISTRS.ASM's ----------
;
; The workspace, from TXTTAB up: the program, the simple variables (VARTAB),
; the arrays (ARYTAB) to STREND, free space, then the strings down from
; MEMSIZ, the lowest at FRETOP+1. The pointers the search and the string
; space use most are here; the rest (MEMSIZ, TEMPST, DSCTMP, NAMBUF, DEFTBL)
; are fixed RAM (res/biptrg.s, res/bistrs.s).
VARTAB          = $A0                  ; the simple variables
ARYTAB          = $A2                  ; the arrays
STREND          = $A4                  ; the first byte past them
FRETOP          = $A6                  ; the top of free space: strings above it
TEMPPT          = $A8                  ; the first free string temporary
ARYTA2          = $AA                  ; where the simple search stops
VPTR            = $AC                  ; the symbol table search (the 8086's SI)
DIMFLG          = $AE                  ; DIM's: bit 7 set when DIM called PTRGET
NDIM            = $AF                  ; an array's subscripts so far (the 8086's D)
ATEMP3          = $B0                  ; NOTFDD's TEMP3: the new array's dimension count
NCNT            = $B2                  ; NOTFDD's dimensions to go
GSLEN           = $B3                  ; GETSPA's length
GSDONE          = $B4                  ; GETSPA's: nonzero once it has collected
GCMAX           = $B5                  ; the garbage collector's highest string so far
GCVAR           = $B7                  ; and the place past its descriptor, or 0
TEMP8           = $B9                  ; the end of the string array being collected
S1              = $BB                  ; a string routine's descriptor (or pointer)
S2              = $BD                  ; and a second
SLEN            = $BF                  ; and a length

; --- Program entry, listing and the console ($C0-$D5): step I1's ------------
;
; CRUNCH and BUFLIN (P2) keep in zero page what the 8086 keeps in DX, CX and
; on its stack while they call CHRGTR, LINGET and $FIN, which use the
; pseudo-registers; the console's column and device are here because every
; character out tests them.
KDP             = $C0                  ; CRUNCH's and BUFLIN's deposit pointer (the 8086's DX; CX)
KCNT            = $C2                  ; the bytes left there (CX; BUFLIN's DH); CRUNCH: then the count
KRP             = $C4                  ; the reserved word table's pointer (DX in the search)
KSRC            = $C6                  ; the text pointer saved while a word is tried
DONUM           = $C8                  ; CRUNCH: 0 a number, 1 a line number, $FF in a name
DORES           = $C9                  ; CRUNCH: crunching stopped (DATA); BUFLIN: quote, DATA, REM
TEMPA           = $CA                  ; BUFLIN: what went last, for its spaces
KCH             = $CB                  ; a character kept (the 8086's CL, DL)
KTRM            = $CC                  ; CRUNCH: a string's terminator; BUFLIN: the token sought
KLEAD           = $CD                  ; BUFLIN: the reserved word's first letter
PTRFIL          = $CE                  ; output: 0 the console, $FFFF LPT1 (GIO86.ASM's file pointer)
TTYPOS          = $D0                  ; the console's column
LPTPOS          = $D1                  ; LPT1's
LSTMAX          = $D2                  ; SCNLIN: the range's last line (the 8086 keeps it on its stack)
LSTLIN          = $D4                  ; LIST: the line being listed
BUFLEN          = 255                  ; OEM.H: a line's characters (BUF)
KBFLEN          = BUFLEN + BUFLEN / 4  ; GWMAIN.ASM: the crunch buffer (KBUF), somewhat larger

; --- The statements ($D8-$DF): step I2's -------------------------------------
;
; FNDFOR and FNDWND walk the BASIC stack's FOR, GOSUB and WHILE entries
; (res/gwmain.s) with FORPTR, as the 8086 walks its own stack with BX.
FORPTR          = $D8                  ; an entry on the BASIC stack

; --- 7b step a2's ($1A) --------------------------------------------------------
;
; OPTION BASE's flag, which the resident CLEARC resets (GWDATA.ASM's OPTFLG).
OPTFLG          = $1A                  ; nonzero once OPTION BASE has set a base

; --- 7b step b's ($E2-$E5) -----------------------------------------------------
;
; DEF FN's state that the resident PTRGET and GARBA2 test on their every pass
; (GWDATA.ASM's; the rest, PARM1 and the counts, is fixed RAM: res/bistrs.s).
TEMP9           = $E2                  ; GARBA2: the parameter block being walked (a pointer)
NOFUNS          = $E4                  ; zero if no user function is being evaluated
PRMFLG          = $E5                  ; PTRGET: PARM1 still to search before the variables
PRMSIZ          = 100                  ; GWDATA.ASM: the bytes for a function's parameters (PARM1, PARM2)

; --- 7b step c's ($E6-$EA) -----------------------------------------------------
;
; AUTO's state (GWDATA.ASM's), which the resident MAIN, SCRTCH and CNTCCN
; test or clear, P2's EDENT2 sets and P3's AUTO and MAIN part read.
AUTFLG          = $E6                  ; AUTO on: nonzero; in MAIN, "*" or " " as offered
AUTLIN          = $E7                  ; the line AUTO offers next
AUTINC          = $E9                  ; AUTO's increment

; --- 7b step d's ($59-$5E) ---------------------------------------------------
;
; PRINT USING's scan of its format (BIPRTU.ASM's BX, CH, CL, DL and DH; p3/
; biprtu.s), kept across FRMEVL, LEFT$ and P1's PUFOUT: only PRINUS uses them.
USP             = $59                  ; BX: the next character of the format
USCH            = $5B                  ; CH: the format's characters left
USCL            = $5C                  ; CL: places after the point, the point counted; a string's width
USDL            = $5D                  ; DL: places before the point (BGSTRF: the count kept)
USDH            = $5E                  ; DH: the field's flags (PUFOUT's A)

; --- 7b step e's ($EC-$ED): the event traps ----------------------------------
;
; OO-1: the 8086 tests ONGSBF at NEWSTT, which its interrupts' sources raise
; through EVTRP. The OO-1's sources are polled, never raised in an interrupt
; (hw/oodos.md, "The plan, phase 7": 7b), so NEWSTT tests TRPON alone, and only
; while it is nonzero polls the sources and then ONGSBF (res/gwmain.s TRPOLL).
; P3 keeps TRPON as the traps' T_ON bits change (p3/bimisc.s ONTRP, OFFTRP,
; INITRP). Both bytes are exported (res/gwmain.s), for the label file that
; test_gwbasic_stmts writes them by.
TRPON           = $EC                  ; OO-1: nonzero while any event trap is on (T_ON)
POLSRC          = $ED                  ; 7f: the polled sources armed (P3 keeps bits 0-1): bit 0 ON TIMER, bit 1
                                       ;   ON COM(1); bit 7 the test-only trigger (its operation in the resident
                                       ;   TRPTST, p3/trptst.s); 0: the poll costs what TRPTST's test cost (7b)
PS_TIMER        = $01
PS_COM          = $02
PS_TEST         = $80
POLLN           = 16                   ; 7f: the statements between two polls of the armed sources (TRPPOL)

; --- 7c's, the file layer ($DA-$DD) ---------------------------------------------
;
; The sequential files (block P4, hw/oodos.md, "The plan, phase 7": 7c, V1's
; route (c)): every file's FDB, its FCB and its 128-byte buffer are in P4's
; own RAM, and only P4's code touches them. PTRFIL (above) is a file's FDB
; address there (in $C800-$F7FF, so its high byte is neither 0, the console,
; nor $FF, LPT1). What resident code needs is these bytes: OUTDO sends a
; file's bytes as a run (RUNBUF, res/oem.s), handed to P4 in one far call
; when it is full and at FINPRT, and keeps the file's column as P4's DSKSOT
; keeps F_POS, for PTRGPS (P1).
FDBP            = $DA                  ; P4: an FDB (the 8086's SI in the file code)
FILPOS          = $DC                  ; PTRFIL's file's column: F_POS, as OUTDO's run moves it
RUNLEN          = $DD                  ; the bytes in RUNBUF not yet sent to PTRFIL's file
RUNSIZ          = 32                   ; RUNBUF's size (res/oem.s)

; --- 7c's LOAD, SAVE and MERGE ($DE, $DF, $EB) ------------------------------------
;
; What the resident core, the home block and the blocks test of a program
; being loaded or saved (GWDATA.ASM's NLONLY and PROFLG), and (OO-1) whether
; file 0, LOAD's, SAVE's and MERGE's own, is open, so the resident FINPRT
; far calls P4 to close it only when it is. In zero page: no fixed RAM.
; ($E0, free to the program, is the flat test harness's: test/harness.s.)
NLONLY          = $DE                  ; bit 7: no file closed (LOAD ,R); bit 0: not file 0 (a load under way)
PROFLG          = $DF                  ; nonzero: the program is protected (a SAVE ,P loaded)
F0OPN           = $EB                  ; OO-1: nonzero while file 0 is open (p4/gio86.s FALLOC, FFREE; $EE until
                                       ;   7c's tail, which wanted $EE-$EF whole for GXT)
LBOERN          = 23                   ; GWDATA.ASM's Line buffer overflow
ERRFDR          = 66                   ; and Direct statement in file
; --- 7c's tail: OOLIB's gxmem in block P5 ($8E-$8F, $E0-$E1, $EE-$EF) -----------
;
; P5's seed (hw/oodos.md, "The plan, phase 7": 7c's tail) links OOLIB's
; gxmem.s, whose zero page is three words (lib/zpgxmem.s's GXP, GXQ, GXT).
; GW-BASIC's zero page is all equates, so a .zeropage segment of the
; library's would land on $00: they are here instead, and p5/seed.s exports
; them to gxmem (.globalzp). Only P5 touches them. ($E0-$E1 is the flat test
; harness's too, test/harness.s, but the flat image has no P5.)
GXP             = $8E                  ; a pointer: DSBYTE's address, gxmem's into W1
GXQ             = $E0                  ; a second: DSADR's segment
GXT             = $EE                  ; gxmem's scratch (GX_BLOCK's bit)

; --- 7f's: OOLIB's music in block P7 ($E0-$E1) -------------------------------------
;
; P7 links OOLIB's mu.s and mutone.s (7f's plan: the music driver for SOUND
; and PLAY), whose zero page is one word, MUP (lib/zpmu.s's): an alias of
; GXQ, exported by p7/sound.s (.globalzp, as p5/seed.s exports GXQ). The rule
; that makes the sharing safe: P5 never calls P7, and P7 never holds MUP
; across a far call (the library's calls make none), so neither block finds
; the other's value there.
MUP             = GXQ                  ; mu.s's block pointer (MU_INSTR, MU_TONE)

; 7f (the owner's answer 20): nonzero once this run has used sound (SOUND,
; BEEP and the bell, PLAY, the driver's start: P7 sets it), so P3's INITRP
; far calls P7's SNDINI at CLEARC only then, and a CHAIN or RUN with no
; sound makes no crossing for it; SNDINI clears it. $5F is free: no
; module's equate names it (PRINT USING's USP-USDH end at $5E), and no
; image's code (the resident core, P1-P8 with OOLIB's modules linked in)
; reaches it, by a scan of every zero page operand (7f's record).
SNDUSE          = $5F

; --- 7c's CHAIN ($D6, $D7) -------------------------------------------------------
;
; A CHAIN under way (GWDATA.ASM's CHNFLG and MRGFLG, set by P4's CHAINP,
; p4/fiveo.s): the resident REASON collects no strings while it is, CLEARC
; leaves FRETOP (the variables kept lie just above it), and for CHAIN MERGE
; the DEF types and the files; P2's line entry leaves FRETOP (LEVFRE); the
; end of file 0's load goes to CHNRET; ERROR and start-up clear both. In
; zero page: no fixed RAM.
CHNFLG          = $D6                  ; nonzero: a CHAIN is under way
MRGFLG          = $D7                  ; nonzero: a CHAIN MERGE

; The file modes (MSDOSU's F_MODE) and F_CODE, F_FLGS.
MD_SQI          = 1                    ; sequential input
MD_SQO          = 2                    ; sequential output
MD_RND          = 4                    ; random
MD_APP          = 8                    ; append
FC_ASC          = 0                    ; an ASCII file
FC_BIN          = 1                    ; a binary one (BSAVE, a tokenised SAVE)
FL_BKC          = $80                  ; F_BAKC holds the next input byte
NMFILE          = 3                    ; OO-1: the files a program may open (1-3), and file 0 (P4's table)
KYBQLN          = 32                   ; the keyboard queue's size, a power of 2 (res/oem.s's KYBQ)

; 7c's devices as files (GIOTBL.ASM's numbers, p4/giotbl.s): F_DEV and FILDEV
; of a device are negative, its place in DEVTBL; a disk's are 0-16.
NDEV            = 8                    ; $_NDEV: the devices that aren't disks
DEVKYB          = $FF                  ; $_KYBD: the keyboard (input)
DEVSCN          = $FE                  ; $_SCRN: the screen (output)
DEVCON          = $FD                  ; $_CONS: the console, raw (output)
DEVLP1          = $FC                  ; $_LPT1 (LPT2 $FB, LPT3 $FA: no driver, OO-1)
DEVCM1          = $F9                  ; $_COM1 (COM2 $F8: no driver, OO-1)

; The FDB (MSDOSU's file data block, its fields in their order), in P4's
; RAM: OO-1, a CP/M FCB of 36 bytes where MS-DOS's of 38 is (DR, the name
; and type, EX S1 S2 RC, the allocation, CR, R0-R2), and no F_NEXT (the
; FDBs are a fixed table, not a chain in the string space), nor the unused
; F_NULn. The random files' fields follow the buffer; their FIELD buffers
; are in the TPA (the plan), so FD_DAT isn't here: p4/gio86.s's FDDAT holds
; each one's address, by the file's number.
F_NUM           = 0                    ; the file number, 0-3
F_MODE          = 1                    ; the mode (MD_*); 0: the FDB is free
F_FCB           = 2                    ; the FCB, 36 bytes
F_CLOC          = 38                   ; sequential: records read or written; random: the last + 1
                                       ;   (OO-1, F3: its top byte F_CLOCH, past the random fields)
F_ORCT          = 40                   ; the bytes in the buffer (input: when read; output: so far)
F_BREM          = 41                   ; the bytes left to read in it
F_CODE          = 42                   ; FC_ASC or FC_BIN
F_BAKC          = 43                   ; the byte backed up (FL_BKC)
F_DEV           = 44                   ; the device: 0-n a disk
F_WID           = 45                   ; the width
F_FLGS          = 46                   ; FL_BKC
F_POS           = 47                   ; the column
FDBSIZ          = 48                   ; the device-independent part's size
DATOFS          = 48                   ; the record's buffer, 128 bytes (DATPSC)
FD_SIZ          = DATOFS + 128         ; random: the record's length
FD_LOGH         = FD_SIZ + 2           ; OO-1 (F3): the logical record's top byte (the source's FD_PHY,
                                       ;   the physical record in the buffer, which nothing set; its
                                       ;   second byte unused)
FD_LOG          = FD_LOGH + 2          ; the logical record (its low 16 bits)
FD_OPS          = FD_LOG + 2           ; the place in the record
F_CLOCH         = FD_OPS + 2           ; OO-1 (F3, the owner's answer 4): F_CLOC's top byte, 24 bits
FDBLEN          = F_CLOCH + 1          ; an FDB's bytes (185)
DATPSC          = 128                  ; a record's bytes
FCB_DV          = F_FCB + 0            ; the drive: 0 the current one, 1 A:
FCB_FN          = F_FCB + 1            ; the name, 8 bytes, then the type, 3
FCB_EX          = F_FCB + 12           ; OO-1: CP/M's EX, S1, S2, RC
FCB_NR          = F_FCB + 32           ; CP/M's CR, the next record (MS-DOS's NR)
FCB_RN          = F_FCB + 33           ; CP/M's R0-R2, the random record (MS-DOS's 4 bytes)

; The disk device's functions (GIO86U's G_*: DSKDSP's entries, p4/giodsk.s).
G_EOF           = 0
G_LOC           = 2
G_LOF           = 4
G_CLS           = 6
G_SWD           = 8
G_RND           = 10
G_OPN           = 12
G_SIN           = 14
G_SOT           = 16
G_GPS           = 18
G_GWD           = 20
G_SCW           = 22
G_GCW           = 24
G_BIN           = 26
G_BOT           = 28

; The traps' flags (BIMISC.ASM; the routines shift them: don't change) and
; TRPTBL's events (OEM.H, GWSTS.ASM): 3 bytes each, the flag and the GOSUB
; line's pointer (its link), in event id order: the pen, the 14 keys, the 2
; COM ports, the 4 triggers.
T_ON            = 1                    ; the trap is on
T_STOP          = 2                    ; stopped: a request is remembered, not taken
T_REQ           = 4                    ; requested: the event has happened
NMKEYT          = 14                   ; OEM.H: the function key traps
NMCOMT          = 2                    ; the COM ports
NMPENT          = 1                    ; the light pens
NMSTRT          = 4                    ; the joystick triggers
NUMTRP          = NMKEYT + NMCOMT + NMPENT + NMSTRT
PENOFF          = 0                    ; GWSTS.ASM: each class's first event id
KEYOFF          = PENOFF + NMPENT
COMOFF          = KEYOFF + NMKEYT
STROFF          = COMOFF + NMCOMT

; --- Value types ------------------------------------------------------------
VT_INT          = 2
VT_STR          = 3
VT_SNG          = 4
VT_DBL          = 8

; --- Embedded constants in program text (GWMAIN.ASM) -----------------------
OCTCON          = 11                   ; &O constant, 2 bytes
HEXCON          = 12                   ; &H constant, 2 bytes
PTRCON          = 13                   ; a line pointer, 2 bytes
LINCON          = 14                   ; a line number, 2 bytes
IN2CON          = 15                   ; an integer 11-255, 1 byte
CONCN2          = 16                   ; scan past the constant (NUMCON+1)
ONECON          = 17                   ; 17-26: the integers 0-9
INTCON          = 28                   ; an integer, 2 bytes
SNGCON          = 29                   ; a single, 4 bytes
CONCON          = 30                   ; re-scan the constant (NUMCON)
DBLCON          = 31                   ; a double, 8 bytes

; --- Tokens (IBMRES.ASM) ----------------------------------------------------
;
; Every reserved word's token, in IBMRES.ASM's order (its R, R2 and T macros
; count them; a DUMMY takes a number and has no word). NAME_TK a statement or
; another one-byte token; NAME_FN the byte after $FF of a function; NAME_XS
; the byte after $FE of an extended statement; NAME_XF the byte after $FD of
; an extended function. A "$" in a name is an S here: STRINGS_TK is STRING$.
END_TK          = $81
FOR_TK          = $82
NEXT_TK         = $83
DATA_TK         = $84
INPUT_TK        = $85
DIM_TK          = $86
READ_TK         = $87
LET_TK          = $88
GOTO_TK         = $89
RUN_TK          = $8A
IF_TK           = $8B
RESTORE_TK      = $8C
GOSUB_TK        = $8D
RETURN_TK       = $8E
REM_TK          = $8F
STOP_TK         = $90
PRINT_TK        = $91
CLEAR_TK        = $92
LIST_TK         = $93
NEW_TK          = $94
ON_TK           = $95
WAIT_TK         = $96
DEF_TK          = $97
POKE_TK         = $98
CONT_TK         = $99
OUT_TK          = $9C
LPRINT_TK       = $9D
LLIST_TK        = $9E
WIDTH_TK        = $A0
ELSE_TK         = $A1
TRON_TK         = $A2
TROFF_TK        = $A3
SWAP_TK         = $A4
ERASE_TK        = $A5
EDIT_TK         = $A6
ERROR_TK        = $A7
RESUME_TK       = $A8
DELETE_TK       = $A9
AUTO_TK         = $AA
RENUM_TK        = $AB
DEFSTR_TK       = $AC
DEFINT_TK       = $AD
DEFSNG_TK       = $AE
DEFDBL_TK       = $AF
LINE_TK         = $B0
WHILE_TK        = $B1
WEND_TK         = $B2
CALL_TK         = $B3
WRITE_TK        = $B7
OPTION_TK       = $B8
RANDOMIZE_TK    = $B9
OPEN_TK         = $BA
CLOSE_TK        = $BB
LOAD_TK         = $BC
MERGE_TK        = $BD
SAVE_TK         = $BE
COLOR_TK        = $BF
CLS_TK          = $C0
MOTOR_TK        = $C1
BSAVE_TK        = $C2
BLOAD_TK        = $C3
SOUND_TK        = $C4
BEEP_TK         = $C5
PSET_TK         = $C6
PRESET_TK       = $C7
SCREEN_TK       = $C8
KEY_TK          = $C9
LOCATE_TK       = $CA
TO_TK           = $CC
THEN_TK         = $CD
TAB_TK          = $CE
STEP_TK         = $CF
USR_TK          = $D0
FN_TK           = $D1
SPC_TK          = $D2
NOT_TK          = $D3
ERL_TK          = $D4
ERR_TK          = $D5
STRINGS_TK      = $D6
USING_TK        = $D7
INSTR_TK        = $D8
VARPTR_TK       = $DA
CSRLIN_TK       = $DB
POINT_TK        = $DC
OFF_TK          = $DD
INKEYS_TK       = $DE
GREATK          = $E6                  ; the operators: > = < + - * / ^
EQULTK          = $E7
LESSTK          = $E8
PLUSTK          = $E9
MINUTK          = $EA
MULTK           = $EB
DIVTK           = $EC
EXPTK           = $ED
AND_TK          = $EE
OR_TK           = $EF
XOR_TK          = $F0
EQV_TK          = $F1
IMP_TK          = $F2
MOD_TK          = $F3
IDIVTK          = $F4                  ; \
SNGQTK          = $D9                  ; ' (a DUMMY: REM's other form)
LEFTS_FN        = $81
RIGHTS_FN       = $82
MIDS_FN         = $83
SGN_FN          = $84
INT_FN          = $85
ABS_FN          = $86
SQR_FN          = $87
RND_FN          = $88
SIN_FN          = $89
LOG_FN          = $8A
EXP_FN          = $8B
COS_FN          = $8C
TAN_FN          = $8D
ATN_FN          = $8E
FRE_FN          = $8F
INP_FN          = $90
POS_FN          = $91
LEN_FN          = $92
STRS_FN         = $93
VAL_FN          = $94
ASC_FN          = $95
CHRS_FN         = $96
PEEK_FN         = $97
SPACES_FN       = $98
OCTS_FN         = $99
HEXS_FN         = $9A
LPOS_FN         = $9B
CINT_FN         = $9C
CSNG_FN         = $9D
CDBL_FN         = $9E
PEN_FN          = $A0
STICK_FN        = $A1
STRIG_FN        = $A2
EOF_FN          = $A3
LOC_FN          = $A4
LOF_FN          = $A5
FIX_FN          = $9F                  ; (IBMRES.ASM: R2 DUMMY,FIXER)
FILES_XS        = $81
FIELD_XS        = $82
SYSTEM_XS       = $83
NAME_XS         = $84
LSET_XS         = $85
RSET_XS         = $86
KILL_XS         = $87
PUT_XS          = $88
GET_XS          = $89
RESET_XS        = $8A
COMMON_XS       = $8B
CHAIN_XS        = $8C
DATES_XS        = $8D
TIMES_XS        = $8E
PAINT_XS        = $8F
COM_XS          = $90
CIRCLE_XS       = $91
DRAW_XS         = $92
PLAY_XS         = $93
TIMER_XS        = $94
ERDEV_XS        = $95
IOCTL_XS        = $96
CHDIR_XS        = $97
MKDIR_XS        = $98
RMDIR_XS        = $99
SHELL_XS        = $9A
ENVIRON_XS      = $9B
VIEW_XS         = $9C
WINDOW_XS       = $9D
PMAP_XS         = $9E
PALETTE_XS      = $9F
LCOPY_XS        = $A0
CALLS_XS        = $A1
ERDEVS_XS       = $A2                  ; OO-1: ERDEV$, a token past the statements (a formula's only)
CVI_XF          = $81
CVS_XF          = $82
CVD_XF          = $83
MKIS_XF         = $84
MKSS_XF         = $85
MKDS_XF         = $86
KTN_XF          = $87
JIS_XF          = $88
KPOS_XF         = $89
KLEN_XF         = $8A
NUMCMD          = LOCATE_TK - END_TK + 1 ; the statements END-LOCATE
NUMSTX          = CALLS_XS - FILES_XS + 1 ; the extended statements FILES-CALLS
LSTOPK          = IDIVTK + 1 - PLUSTK  ; + - * / ^ AND OR XOR EQV IMP MOD \
NMREL           = LESSTK - GREATK + 1
OPCNT           = 4                    ; the compare's place in the dispatch tables
ONEFUN          = LEFTS_FN             ; functions follow $FF: LEFT$ is $FF $81
MIDTK           = MIDS_FN
NUMGFN          = 6                    ; LEFT$ RIGHT$ MID$, times 2
RNDFN           = RND_FN - ONEFUN
BOTCON          = 2 * (SQR_FN - ONEFUN) ; SQR...
TOPCON          = 2 * (ATN_FN - ONEFUN) + 2 ; ...ATN: these take a single
DATCO           = DATA_TK - ':'        ; $DATCO and $REMCO, CRUNCH's
REMCO           = REM_TK - ':'

; --- Far calls --------------------------------------------------------------
;
; GW-BASIC's code is the resident core (the TPA) and banked blocks P1-P8, each
; linked at $C800 for W3's code window (hw/oodos.md, "The plan, phase 7").
; A block starts with a header (BLK_FIX, BLK_NFIX: its far call sites) and a
; jump table, entry n at BLK_ENTRY + 3n. tools/gwgen.py makes build/entries.inc
; from the blocks' ";@entry NAME" lines: PG_NAME the block's page (1-8) and
; E_NAME its entry. FJSR NAME calls it from anywhere: a JSR FARCALL with the
; block and the address after it, as FARCALL takes them. It is assembled as a
; JSR FARMISS (the resident core's: "far call before load"), and the loader
; puts FARCALL's address and the page's block in for every site the FARFIX
; segment lists, before anything runs: so OODOS's rules gate sees a JSR
; inside the program (rules.py --farcall FARMISS), and FARCALL is OODOS's,
; unchanged.
BLK_BASE        = $C800
BLK_FIX         = BLK_BASE             ; +0-1 the block's FARFIX list, +2-3 its count
BLK_NFIX        = BLK_BASE + 2
BLK_ENTRY       = BLK_BASE + 4         ; the jump table
BLK_END         = $F800                ; common memory from here, copied in by the loader

.macro  FAR     page, target
        .local  @site                  ; a cheap local: the caller's @ labels
@site:  JSR     FARMISS                ; stay in scope across it
        .byte   page
        .word   target
        .pushseg
        .segment "FARFIX"
        .word   @site + 1
        .popseg
.endmacro

.ifdef FLAT
; The flat test build (test/, the project's own): every module, resident or
; banked, in one image for the W65C02S core alone, so a far call is a JSR.
.macro  FJSR    name
        .global name
        JSR     name
.endmacro
.else
.macro  FJSR    name
        FAR     .ident(.concat("PG_", .string(name))), .ident(.concat("E_", .string(name)))
.endmacro
.endif

; --- 16-bit helpers ---------------------------------------------------------
.macro  INCW    addr
                INC     addr
                BNE     :+
                INC     addr+1
:
.endmacro

.macro  DECW    addr
                LDA     addr
                BNE     :+
                DEC     addr+1
:               DEC     addr
.endmacro

; Push the address of a routine for RTS to go to: the 8086's PUSH of a
; return address, MOV CX,OFFSET RETAOP / PUSH CX.
.macro  PUSHRET target
                LDA     #>(target-1)
                PHA
                LDA     #<(target-1)
                PHA
.endmacro

.global FARMISS
.include "entries.inc"
; The home block's entries (P8, p8/home.s: 7c's home block) are the resident
; core's and the home block's alone: a banked block P1-P7 is assembled with
; NOHOME, so a FJSR from it to the home block names no page, and fails.
.ifndef NOHOME
.include "homent.inc"
.endif
HOMTAB          = BLK_BASE + $20       ; the home block's STMDSP (home.cfg)
HOMPAGE         = 8                    ; the home block's page

; Long branches: a branch past +-127 bytes, as a short one round a JMP.
.macro  JNE     t
                BEQ     :+
                JMP     t
:
.endmacro
.macro  JEQ     t
                BNE     :+
                JMP     t
:
.endmacro
.macro  JCS     t
                BCC     :+
                JMP     t
:
.endmacro
.macro  JCC     t
                BCS     :+
                JMP     t
:
.endmacro

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; FIVEO.ASM's CHAIN, translated, in the home block, P8 (hw/oodos.md, "The
; plan, phase 7": 7c; moved whole from P4 at F3's M4), beside its stubs
; (p8/far4.s CHAIN and LDBIN, p8/gwmain.s MAIN's LDREOF), which call it by
; JSR. The load it ends in is P4's (p4/dskcom.s LOADP2 and OKGETM), reached
; by far calls to P4's entries PRGFLI, CHNSTP and SAVFGT (p4/fiveo.s):
;
;   CHAINP          CHAIN [MERGE] name[, [line][, ALL][, DELETE range]]: the
;                   source's steps 1-5 and its way to step 6 (FIVEO.ASM 216:
;                   CHAIN to NTMDLT): the arguments, file 0 opened; the
;                   COMMON statements' variables marked (NTCHAL to CHKCST);
;                   the rest squeezed out (CLPFIN to DNCMDS, CLPAKP); string
;                   literals copied to the string space (DNCMDA to CAYSTR);
;                   the variables moved up below the strings (DNCCLS), FRETOP
;                   under them; MERGE's DELETE; CHNFLG set; then X 0, MERGE
;                   (P4's CHNSTP: OKGETM; the stub's MAIN reads the file's
;                   lines), or X 1, CHNENT (CHNSTP's too; the stub's SCRTCH
;                   and LOADP2, as LOAD's)
;   CBAKBL          a variable's name marked COMMON (its second byte's bit 7)
;   VARDLS          a variable taken out, the rest moved down
;   CDVARS          a string variable's text copied to the string space if it
;                   is in the program
;   CHNRET          step 7, at the new program's load's end (the binary
;                   load's, the stub's LDBIN; the text one's, INCHR's LDREOF,
;                   by the home block's MAIN): the variables moved back down
;                   after it, OPTION BASE as it was, file 0 closed, DATA from
;                   the start, TXTPTR where the program starts (or the line
;                   given: Undefined line number if it isn't there); the
;                   stub's JMP NEWSTT runs it
;   CMNSRC          (the project's own) NOARYS's search as for PTRGTN
;   SKPNAM          FIVEO.ASM's, a copy of p4/gio86.s's (XFRSTR's there):
;                   CDVARS's and the COMMON scan's, a variable at a time, so
;                   a JSR here rather than a far call
;
; FIVEO.ASM's COMMON is DATA (p8/home.s's STMDSX names it).
;
; SAVFRE, FRETOP before the variables moved up, stays in P4's RAM, since
; P4's XFRSTR moves it with the string space (p4/gio86.s), as the source's
; does: CHNTOP keeps it here from DNCCLS to step 6 (P2's DEL and LINKER run
; between), CHNSTP stores it in P4 and CHNRET fetches it back (SAVFGT).
;
; The far call rule: CHAINP and CHNRET return by RTS, or end in an error;
; the home block's stubs go on to MAIN, SCRTCH and NEWSTT, as the 8086's
; JMP OKGETM, JMP CHNENT and JMP NEWSTT do. What the 8086 keeps on its stack
; here is in this block's RAM: CMNTXT (the text pointer round a COMMON
; variable's searches), CHNCUR (CURLIN while the COMMONs are scanned), CMVAR
; (a variable's start while it is squeezed, an array's elements' start),
; CMTYP (an array's VALTYP), CMDSC (a descriptor copied to). The ALL flag is
; on the 6502's stack, as the 8086's PUSHF. BX, CX and DX are RBL/RBH,
; RCL/RCH and RDL/RDH, as elsewhere.

.include "gwbasic.inc"

.export CHAINP, CHNRET
.import CHRGTR, SYNCHR, SNERR, FCERR, USERR, FRMEVL, EVAL, GETYP, DATA, FNDLIN
.import PTRGTN, PTRGTR, ERSFIN, NOARYS, SUBFLG, OPTVAL, BLTUC, GARBA2, STRCPY, DSCTMP
.import FINPRT, RESTORE

.code

; --- CHAIN (FIVEO.ASM 216) ----------------------------------------------------------------

; CHAINP: step 1, the arguments. No MERGE, no DELETE; OPTION BASE kept
; (TOPTFG, TOPTVL: CLEARC resets it, CHNRET puts it back). MERGE first sets
; MRGFLG; then the name: file 0 opened for input (PRGFLI). No line (0) unless
; one is given; after it, ALL (letters, not a word), DELETE, or both, ALL
; first. DELETE's range (SCNLIN, P2's: the first line in CX, the last number
; in LSTMAX) is checked as DELETE checks it, its line pointers back to
; numbers (DEPTR) first: the last line must be there, and after the first,
; else Illegal function call; CMSPTR its first line, CMEPTR the line after
; it. The text after the range isn't looked at (the 8086's BX is lost to
; FNDLIN there). With ALL, the COMMON scan and the squeeze are passed over
; (DNCMDA).
CHAINP:         STZ     MRGFLG         ; no MERGE
                STZ     MDLFLG         ; nor MERGE's DELETE
                LDA     OPTFLG         ; OPTION BASE saved
                STA     TOPTFG
                LDA     OPTVAL
                STA     TOPTVL
                LDA     (TXTPTR)       ; MERGE?
                CMP     #MERGE_TK
                BNE     NTCHNM
                STA     MRGFLG
                INCW    TXTPTR
NTCHNM:         DECW    TXTPTR         ; the name, again
                JSR     CHRGTR
                FJSR    PRGFLI         ; evaluated, and file 0 opened (P4's)
                STZ     CHNLIN         ; no CHAIN line
                STZ     CHNLIN+1
                DECW    TXTPTR
                JSR     CHRGTR
                BNE     :+
                JMP     NTCHAL         ; nothing more
:               JSR     SYNCHR
                .byte   ','
                CMP     #','           ; no line (",,ALL")
                BEQ     NTLINF
                JSR     FRMEVL         ; the line
                FJSR    FRQINT         ; BX: it, an integer
                LDA     RBL
                STA     CHNLIN
                LDA     RBH
                STA     CHNLIN+1
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     NTCHAL         ; no ALL: the COMMONs
NTLINF:         JSR     SYNCHR
                .byte   ','
                CMP     #DELETE_TK
                BEQ     CHMWDL         ; DELETE (Z set: no ALL)
                JSR     SYNCHR
                .byte   'A'
                JSR     SYNCHR
                .byte   'L'
                JSR     SYNCHR
                .byte   'L'
                BNE     :+
                JMP     DNCMDA         ; ALL alone: step 4
:               JSR     SYNCHR
                .byte   ','
                CMP     #DELETE_TK     ; then DELETE
                BEQ     :+
                JMP     SNERR
:               ORA     #0             ; Z clear: ALL
CHMWDL:         PHP                    ; the ALL flag
                STA     MDLFLG         ; MERGE with DELETE
                JSR     CHRGTR
                FJSR    SCNLIN         ; RCL: the first line; LSTMAX the last
                LDA     RCH            ; (PUSH CX round DEPTR)
                PHA
                LDA     RCL
                PHA
                FJSR    DEPTR          ; line pointers back to numbers
                PLA
                STA     RCL
                STA     CMSPTR         ; the first line
                PLA
                STA     RCH
                STA     CMSPTR+1
                LDA     LSTMAX         ; DX: the last line's number
                STA     RDL
                LDA     LSTMAX+1
                STA     RDH
                JSR     FNDLIN         ; it must be there (C set)
                BCC     FCERRG
                LDA     PTR2           ; the line after it
                STA     CMEPTR
                LDA     PTR2+1
                STA     CMEPTR+1
                LDA     CMSPTR         ; the first before it
                CMP     PTR2
                LDA     CMSPTR+1
                SBC     PTR2+1
                BCC     :+
FCERRG:         JMP     FCERR
:               PLP                    ; ALL?
                BEQ     NTCHAL
                JMP     DNCMDA

; NTCHAL: step 2, the COMMONs. CURLIN kept (CHNCUR) while each line's number
; is put there, for an error's message. Each statement of each line: a
; COMMON ($FE COMMON_XS) is read, any other skipped (DATA). In a COMMON,
; each name: an array (SUBFLG 1: ERSFIN's search, with no subscripts) or,
; failing that, one already COMMON (its second byte's bit 7 set: ERSFIN
; again), if a "(" follows; else a simple variable (PTRGTN), or one already
; COMMON (NOARYS, as for PTRGTN: CMNSRC). One found is marked COMMON
; (CBAKBL); one not there, or an array not there, is passed over. An
; array's "(" may hold a number (the compiler's dimension count, evaluated
; and dropped: Illegal function call if a string) before its ")"; a comma
; goes on to the next name.
NTCHAL:         LDA     CURLIN         ; PUSH CURLIN
                STA     CHNCUR
                LDA     CURLIN+1
                STA     CHNCUR+1
                LDA     TXTTAB         ; from the program's start
                STA     TXTPTR
                LDA     TXTTAB+1
                STA     TXTPTR+1
                DECW    TXTPTR
CLPSC1:         INCW    TXTPTR         ; the next line's link
CLPSCN:         LDA     (TXTPTR)
                INCW    TXTPTR
                ORA     (TXTPTR)       ; the program's end?
                BNE     :+
                JMP     CLPFIN
:               INCW    TXTPTR
                LDA     (TXTPTR)       ; its number, CURLIN for errors
                STA     CURLIN
                LDY     #1
                LDA     (TXTPTR),Y
                STA     CURLIN+1
                INCW    TXTPTR
CSTSCN:         JSR     CHRGTR         ; a statement's first byte
AFTCOM:         CMP     #0
                BEQ     CLPSC1         ; the line's end
                CMP     #':'
                BEQ     CSTSCN
                CMP     #$FE           ; COMMON is an extended statement
                BNE     NOCOMM
                INCW    TXTPTR
                LDA     (TXTPTR)
                CMP     #COMMON_XS
                BEQ     DOCOMM
                DECW    TXTPTR
NOCOMM:         JSR     CHRGTR         ; not one: skipped
                JSR     DATA
                DECW    TXTPTR         ; its terminator, again
                BRA     CSTSCN
DOCOMM:         JSR     CHRGTR         ; past COMMON
                BEQ     AFTCOM
NXTCOM:         LDA     TXTPTR         ; PUSH BX: the name's start
                STA     CMNTXT
                LDA     TXTPTR+1
                STA     CMNTXT+1
                LDA     #1             ; an array, by ERASE's search
                STA     SUBFLG
                JSR     PTRGTN
                BEQ     FNDAAY         ; found
                LDA     RCH            ; one already COMMON?
                ORA     #$80
                STA     RCH
                JSR     ERSFIN
                STZ     SUBFLG         ; (the flags kept)
                BNE     NTFN2T         ; no
                LDA     (TXTPTR)       ; it is, if "(" follows
                CMP     #'('
                BNE     SCNSMP
                BRA     COMADY         ; already COMMON
NTFN2T:         LDA     (TXTPTR)
                CMP     #'('           ; an array not there: passed over
                BEQ     SKPCOM
SCNSMP:         LDA     CMNTXT         ; the name again: a simple variable
                STA     TXTPTR
                LDA     CMNTXT+1
                STA     TXTPTR+1
                JSR     PTRGTN         ; PTR 0: not there
                LDA     PTR
                ORA     PTR+1
                BNE     COMFNS
                LDA     RCH            ; one already COMMON?
                ORA     #$80
                STA     RCH
                JSR     CMNSRC
COMPT2:         LDA     PTR
                ORA     PTR+1
                BEQ     SKPCOM         ; not there: passed over
COMFNS:         LDA     PTR            ; CX: at its value
                STA     RCL
                LDA     PTR+1
                STA     RCH
                JSR     CBAKBL         ; marked
                BRA     BCKUCM

; CBAKBL (FIVEO.ASM 383): CX at a variable's value (or an array's size, +2):
; back over the name's extra characters (bit 7 set) to its count, and the
; second character before it marked COMMON (bit 7). LPBKNC its loop.
CBAKBL:         DECW    RCL            ; the name's last byte
LPBKNC:         LDA     (RCL)
                PHA
                DECW    RCL
                PLA
                BMI     LPBKNC         ; an extra character
                LDA     (RCL)          ; CX: the second character
                ORA     #$80
                STA     (RCL)
                RTS

; FNDAAY: an array found (BX at its dimension count, the 8086's CX). If no
; "(" follows, a simple variable is sought instead. BAKCOM: back past the
; size to the name, which is marked. BCKUCM: the text pointer (after the
; name: TXTPTR, kept); SKPCOM: what follows it. ")" or a number and ")"
; (COMADY); ","; or the statement's end.
FNDAAY:         STZ     SUBFLG
                LDA     (TXTPTR)       ; really an array?
                CMP     #'('
                BNE     SCNSMP
BAKCOM:         SEC                    ; CX: the size's first byte
                LDA     RBL
                SBC     #2
                STA     RCL
                LDA     RBH
                SBC     #0
                STA     RCH
                JSR     CBAKBL
BCKUCM:
SKPCOM:         DECW    TXTPTR         ; the terminator, again
                JSR     CHRGTR
                BNE     :+
                JMP     AFTCOM         ; the COMMON's end
:               CMP     #'('
                BNE     CHKCST
COMADY:         LDA     TXTPTR         ; PUSH BX: at the "("
                STA     CMNTXT
                LDA     TXTPTR+1
                STA     CMNTXT+1
                JSR     CHRGTR
                CMP     #')'
                BEQ     COMRPN         ; "()"
                LDA     CMNTXT         ; POP BX
                STA     TXTPTR
                LDA     CMNTXT+1
                STA     TXTPTR+1
                JSR     EVAL           ; a dimension count (the compiler's)
                JSR     GETYP
                BNE     COMRP1
                JMP     FCERR          ; not a string
COMRPN:                                ; (POP DX: dropped)
COMRP1:         JSR     SYNCHR
                .byte   ')'
                BNE     CHKCST
                JMP     AFTCOM
CHKCST:         JSR     SYNCHR
                .byte   ','
                JMP     NXTCOM

; CMNSRC (the project's own): the 8086 pushes PTRGTR's address and jumps
; to NOARYS, so the search returns as PTRGTN's does: PTR 0, nothing made,
; if the variable isn't there (SMKVAR's test of its caller).
CMNSRC:         PUSHRET PTRGTR
                JMP     NOARYS

; --- Step 3: the rest squeezed out (FIVEO.ASM 438) ----------------------------------------

; CLPFIN: CURLIN back. Each simple variable, VARTAB to ARYTAB: its COMMON
; bit cleared; kept if it was set, else taken out (VARDLS), ARYTAB down by
; its size. Then each array to STREND (DNCMDS, CLPAKP), the same.
CLPFIN:         LDA     CHNCUR         ; POP BX: CURLIN
                STA     CURLIN
                LDA     CHNCUR+1
                STA     CURLIN+1
                LDA     ARYTAB         ; DX: the simple variables' end
                STA     RDL
                LDA     ARYTAB+1
                STA     RDH
                LDA     VARTAB         ; BX: their start
                STA     RBL
                LDA     VARTAB+1
                STA     RBH
CLPSLP:         LDA     RBL            ; done?
                CMP     RDL
                BNE     :+
                LDA     RBH
                CMP     RDH
                BNE     :+
                JMP     DNCMDS
:               LDA     RBL            ; PUSH BX: where it is
                STA     CMVAR
                LDA     RBH
                STA     CMVAR+1
                LDY     #2             ; the COMMON bit
                LDA     (RBL),Y
                PHP
                AND     #$7F
                STA     (RBL),Y
                INY                    ; past it: the name's rest, VALTYP bytes
                LDA     (RBL),Y
                SEC
                ADC     #3
                CLC
                ADC     (RBL)
                CLC
                ADC     RBL
                STA     RBL
                BCC     :+
                INC     RBH
:               PLP
                BMI     CLPSLP         ; COMMON: kept
                LDA     CMVAR          ; CX: where it starts
                STA     RCL
                LDA     CMVAR+1
                STA     RCH
                JSR     VARDLS
                CLC                    ; ARYTAB by DX (negative)
                LDA     ARYTAB
                ADC     RDL
                STA     ARYTAB
                STA     RDL
                LDA     ARYTAB+1
                ADC     RDH
                STA     ARYTAB+1
                STA     RDH
                LDA     CMVAR          ; POP BX: the next is here now
                STA     RBL
                LDA     CMVAR+1
                STA     RBH
                BRA     CLPSLP

; VARDLS (470): the variable from CX to BX taken out: the bytes from BX (DX)
; to STREND (BX) moved down to CX (DLSVLP: STREND's own byte too, as the
; 8086's). DX returns the new place less the old (negative), STREND and BX
; the new STREND.
VARDLS:         LDA     RBL            ; DX: the variable's end
                STA     RDL
                LDA     RBH
                STA     RDH
VARDL1:
DLSVLP:         LDA     (RDL)
                STA     (RCL)
                LDA     RDL            ; STREND's byte moved?
                CMP     STREND
                BNE     :+
                LDA     RDH
                CMP     STREND+1
                BEQ     @d
:               INCW    RDL
                INCW    RCL
                BRA     DLSVLP
@d:             SEC                    ; DX: the new STREND less the old
                LDA     RCL
                SBC     STREND
                STA     RDL
                LDA     RCH
                SBC     STREND+1
                STA     RDH
                LDA     RCL            ; CX: the new STREND
                STA     STREND
                STA     RBL
                LDA     RCH
                STA     STREND+1
                STA     RBH
                RTS

; DNCMDS, CLPAKP: the arrays, to STREND: an array's size is past its name.
DNCMDS:         LDA     STREND         ; DX: the search's end
                STA     RDL
                LDA     STREND+1
                STA     RDH
CLPAKP:         LDA     RBL            ; done?
                CMP     RDL
                BNE     :+
                LDA     RBH
                CMP     RDH
                BEQ     DNCMDA
:               LDA     RBL            ; PUSH BX
                STA     CMVAR
                LDA     RBH
                STA     CMVAR+1
                LDY     #2             ; the COMMON bit
                LDA     (RBL),Y
                PHP
                AND     #$7F
                STA     (RBL),Y
                INY                    ; the size, past the name
                LDA     (RBL),Y
                CLC
                ADC     #4
                TAY
                LDA     (RBL),Y        ; CX: the size
                STA     RCL
                INY
                LDA     (RBL),Y
                STA     RCH
                INY                    ; BX: past the size, then past the array
                TYA
                CLC
                ADC     RBL
                STA     RBL
                BCC     :+
                INC     RBH
:               CLC
                LDA     RBL
                ADC     RCL
                STA     RBL
                LDA     RBH
                ADC     RCH
                STA     RBH
                PLP
                BMI     CLPAKP         ; COMMON: kept
                LDA     CMVAR          ; CX: its start
                STA     RCL
                LDA     CMVAR+1
                STA     RCH
                JSR     VARDLS
                LDA     RBL            ; DX: the new STREND
                STA     RDL
                LDA     RBH
                STA     RDH
                LDA     CMVAR          ; POP BX
                STA     RBL
                LDA     CMVAR+1
                STA     RBH
                BRA     CLPAKP

; --- Step 4: literals copied to the string space (FIVEO.ASM 526) -----------------------------

; DNCMDA: each string variable (CSVAR) and each string array's element
; (CAYVAR, CAYSTR) whose text is in the program is given a copy in the
; string space (CDVARS), as the program is about to go.
DNCMDA:         LDA     VARTAB         ; the simple strings
                STA     RBL
                LDA     VARTAB+1
                STA     RBH
CSVAR:          LDA     RBL            ; to ARYTAB
                CMP     ARYTAB
                BNE     :+
                LDA     RBH
                CMP     ARYTAB+1
                BEQ     CAYVAR
:               JSR     SKPNAM         ; A: VALTYP; Z a string
                BNE     CSKPVA
                JSR     CDVARS         ; (past the descriptor)
                LDA     #0
CSKPVA:         CLC                    ; past its value
                ADC     RBL
                STA     RBL
                BCC     CSVAR
                INC     RBH
                BRA     CSVAR
CAYVA2:
CAYVAR:         LDA     RBL            ; the arrays, to STREND
                CMP     STREND
                BNE     :+
                LDA     RBH
                CMP     STREND+1
                BNE     :+
                JMP     DNCCLS
:               JSR     SKPNAM         ; BX at its size
                STA     CMTYP          ; PUSH AX: VALTYP
                LDA     (RBL)          ; CX: the size
                STA     RCL
                LDY     #1
                LDA     (RBL),Y
                STA     RCH
                CLC                    ; BX past it, at the dimension count
                LDA     RBL
                ADC     #2
                STA     RBL
                STA     CMVAR          ; PUSH BX
                LDA     RBH
                ADC     #0
                STA     RBH
                STA     CMVAR+1
                CLC                    ; past the array
                LDA     RBL
                ADC     RCL
                STA     RBL
                LDA     RBH
                ADC     RCH
                STA     RBH
                LDA     CMTYP          ; a string array?
                CMP     #VT_STR
                BNE     CAYVA2         ; no: the next
                LDA     RBL            ; TEMP3: its end
                STA     TEMP3
                LDA     RBH
                STA     TEMP3+1
                LDA     CMVAR          ; POP BX: its start
                STA     RBL
                LDA     CMVAR+1
                STA     RBH
                LDA     (RBL)          ; past the dimensions: 2 each, and the count
                STA     T0
                STZ     T1
                ASL     T0
                ROL     T1
                SEC
                LDA     RBL
                ADC     T0
                STA     RBL
                LDA     RBH
                ADC     T1
                STA     RBH
CAYSTR:         LDA     RBL            ; the array's end?
                CMP     TEMP3
                BNE     :+
                LDA     RBH
                CMP     TEMP3+1
                BEQ     CAYVAR
:               JSR     CDVARS
                BRA     CAYSTR

; CDVARS (567): BX at a string's descriptor, BX past it. A null string, or
; one in the string space (above VARTAB), or below the program (TXTTAB and
; under: none here; the 8080's disk buffers), is left; one in the program is
; copied (STRCPY) and the descriptor made the copy's (MOVE1's 3 bytes).
CDVARS:         LDA     (RBL)          ; the length
                STA     T2
                LDY     #1             ; DX: the data
                LDA     (RBL),Y
                STA     RDL
                INY
                LDA     (RBL),Y
                STA     RDH
                CLC
                LDA     RBL
                ADC     #3
                STA     RBL
                BCC     :+
                INC     RBH
:               LDA     T2
                BNE     :+
                RTS                    ; null
:               LDA     VARTAB         ; in the string space?
                CMP     RDL
                LDA     VARTAB+1
                SBC     RDH
                BCS     :+
                RTS
:               LDA     TXTTAB         ; below the program?
                CMP     RDL
                LDA     TXTTAB+1
                SBC     RDH
                BCC     :+
                RTS
:               LDA     RBL            ; PUSH BX: where we are
                PHA
                LDA     RBH
                PHA
                SEC                    ; the descriptor
                LDA     RBL
                SBC     #3
                STA     RBL
                STA     CMDSC
                LDA     RBH
                SBC     #0
                STA     RBH
                STA     CMDSC+1
                JSR     STRCPY         ; DSCTMP: the copy
                LDA     CMDSC          ; MOVE1: DSCTMP to the descriptor
                STA     PTR
                LDA     CMDSC+1
                STA     PTR+1
                LDY     #2
@m:             LDA     DSCTMP,Y
                STA     (PTR),Y
                DEY
                BPL     @m
                PLA                    ; POP BX
                STA     RBH
                PLA
                STA     RBL
                RTS

; --- Step 5: the variables moved up (FIVEO.ASM 600) -----------------------------------------

; DNCCLS: the strings collected (GARBA2), then the variables, VARTAB to
; STREND, moved up to end at FRETOP (BLTUC), and FRETOP put below them;
; TEMP9 the simple variables' length and CHNTOP the old FRETOP (SAVFRE's,
; for CHNRET).
; MERGE's DELETE (MDLFLG) takes its lines out (DEL, P2's, which sets
; VARTAB, ARYTAB and STREND at the program's new end, as the 8086's two
; MOVs after it do: "in case we get error in CHAIN"), and the lines are
; linked again (LINKER). NTMDLT: step 6, CHNFLG set; then P4's CHNSTP, BX
; the old FRETOP (SAVFRE): MERGE's way (OKGETM: its first byte read, the
; file made text; X 0) or CHNENT's (X 1), X back as it went.
DNCCLS:         JSR     GARBA2
                LDA     STREND         ; CX: the last byte
                STA     RCL
                LDA     STREND+1
                STA     RCH
                LDA     VARTAB         ; DX: the first
                STA     RDL
                LDA     VARTAB+1
                STA     RDH
                SEC                    ; TEMP9: the simple variables' length
                LDA     ARYTAB
                SBC     VARTAB
                STA     TEMP9
                LDA     ARYTAB+1
                SBC     VARTAB+1
                STA     TEMP9+1
                LDA     FRETOP         ; BX: where the last goes; SAVFRE's
                STA     RBL
                STA     CHNTOP
                LDA     FRETOP+1
                STA     RBH
                STA     CHNTOP+1
                JSR     BLTUC          ; CX: where the first went
                SEC                    ; FRETOP below it
                LDA     RCL
                SBC     #1
                STA     FRETOP
                LDA     RCH
                SBC     #0
                STA     FRETOP+1
                LDA     MDLFLG         ; MERGE with DELETE?
                BEQ     NTMDLT
                LDA     CMSPTR         ; CX: the first line; BX: the one after
                STA     RCL
                LDA     CMSPTR+1
                STA     RCH
                LDA     CMEPTR
                STA     RBL
                LDA     CMEPTR+1
                STA     RBH
                FJSR    DEL            ; (ARYTAB and STREND: DEL's)
                FJSR    LINKER
NTMDLT:         LDA     #1             ; the CHAIN flag
                STA     CHNFLG
                LDA     CHNTOP         ; BX: SAVFRE's
                STA     RBL
                LDA     CHNTOP+1
                STA     RBH
                LDX     #0             ; MERGE? X 0, else 1
                LDA     MRGFLG
                BNE     :+
                INX
:               FJSR    CHNSTP         ; SAVFRE, then OKGETM or CHNENT (P4's)
                RTS

; SKPNAM (FIVEO.ASM 701; p4/gio86.s's, copied): BX at a variable: A its
; VALTYP, BX past its name (the 8086's IADAHL: the type, two letters, the
; count of the rest, the rest), Z set if it is a string.
SKPNAM:         LDA     (RBL)
                PHA
                LDY     #3
                LDA     (RBL),Y
                CLC
                ADC     #4
                ADC     RBL
                STA     RBL
                BCC     :+
                INC     RBH
:               PLA
                CMP     #VT_STR
                RTS

; --- Step 7: the variables moved back (FIVEO.ASM 639) ---------------------------------------

; CHNRET: OPTION BASE as it was; no CHAIN, no MERGE now. The variables, from
; FRETOP + 1 to SAVFRE (MVBKVR: its own byte too, as the 8086's), moved
; down to VARTAB, after the new program: ARYTAB at VARTAB + TEMP9, STREND at
; the last byte, FRETOP SAVFRE again (P4's SAVFGT: its value, which P4's
; XFRSTR may have moved). Every file may be closed (NLONLY 0):
; file 0 closed and output to the console (FINPRT); DATA from the start;
; then TXTPTR at TXTTAB - 1, or before CHNLIN's line if one was given: it
; must be there (Undefined line number). The stub goes on at NEWSTT.
CHNRET:         LDA     TOPTVL         ; OPTION BASE back
                STA     OPTVAL
                LDA     TOPTFG
                STA     OPTFLG
                STZ     CHNFLG
                STZ     MRGFLG
                LDA     VARTAB         ; CX: VARTAB
                STA     RCL
                CLC
                ADC     TEMP9          ; ARYTAB: past the simple variables
                STA     ARYTAB
                LDA     VARTAB+1
                STA     RCH
                ADC     TEMP9+1
                STA     ARYTAB+1
                CLC                    ; DX: FRETOP + 1
                LDA     FRETOP
                ADC     #1
                STA     RDL
                LDA     FRETOP+1
                ADC     #0
                STA     RDH
                FJSR    SAVFGT         ; BX: the last, SAVFRE (P4's); FRETOP again
                LDA     RBL
                STA     FRETOP
                LDA     RBH
                STA     FRETOP+1
MVBKVR:         LDA     (RDL)
                STA     (RCL)
                LDA     RDL            ; done?
                CMP     RBL
                BNE     :+
                LDA     RDH
                CMP     RBH
                BEQ     @d
:               INCW    RDL
                INCW    RCL
                BRA     MVBKVR
@d:             LDA     RCL            ; STREND: the last byte
                STA     STREND
                LDA     RCH
                STA     STREND+1
                STZ     NLONLY         ; every file may be closed
                JSR     FINPRT         ; file 0 closed, PTRFIL 0
                LDA     #0             ; RESTORE
                JSR     RESTORE
                LDA     TXTTAB         ; BX: before the program
                STA     TXTPTR
                LDA     TXTTAB+1
                STA     TXTPTR+1
                DECW    TXTPTR
                LDA     CHNLIN         ; line 0: from the start
                ORA     CHNLIN+1
                BEQ     @go
                LDA     CHNLIN
                STA     RDL
                LDA     CHNLIN+1
                STA     RDH
                JSR     FNDLIN
                BCS     :+
                JMP     USERR          ; not there
:               LDA     RCL            ; before its line
                STA     TXTPTR
                LDA     RCH
                STA     TXTPTR+1
                DECW    TXTPTR
@go:            RTS

.segment "BRAM"
; GWDATA.ASM's
TOPTVL:         .res    1              ; OPTION BASE's value and flag, kept over the CHAIN
TOPTFG:         .res    1
MDLFLG:         .res    1              ; nonzero: CHAIN MERGE with DELETE
CHNLIN:         .res    2              ; CHAIN's line, 0 if none
CMSPTR:         .res    2              ; DELETE's first line
CMEPTR:         .res    2              ; and the line after its last
CHNTOP:         .res    2              ; FRETOP before the variables moved up, till SAVFRE has it
; What the 8086 keeps on its stack
CHNCUR:         .res    2              ; CURLIN while the COMMONs are scanned
CMNTXT:         .res    2              ; a COMMON variable's text pointer
CMVAR:          .res    2              ; a variable's start; an array's elements'
CMTYP:          .res    1              ; an array's VALTYP
CMDSC:          .res    2              ; the descriptor a copy goes to

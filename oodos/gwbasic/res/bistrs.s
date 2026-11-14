; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; BISTRS.ASM, translated: the string core, resident (hw/oodos.md, "The plan,
; phase 7"). With GWMAIN.ASM's LET of a string (LETSTR, the string half of
; LETCN6) and MATH1.ASM's GETBCD:
;
;   STRCMP                     compare, for DOCMP
;   STRCPY STRINI STRAD2       a copy; space and DSCTMP for a result
;   STRLIT STRLTI STRLT2       a literal's descriptor (in the text: no copy)
;   PUTNEW PUTTMP              DSCTMP to a temporary, the FAC at it
;   STROUI DSOUT STRPRT        print a zero-ended text, or the FAC's string
;   GETSPA GARBAG GARBA2 DVARS space in the string space, collecting it
;   CAT MOVINS MOVSTR          concatenation
;   FRESTR FREFAC FRETMP FRETMS temporaries freed (and their space, if lowest)
;   LEN ASC CHR$ SPACE$        the functions, as LEFT$ RIGHT$ MID$ INSTR FRE
;
; A string is its descriptor: the length, then the address of the data. The
; FAC holds the address of a descriptor (FACLO), VALTYP 3. A descriptor is in
; a variable, an array, or one of the NUMTMP temporaries at TEMPST, which a
; function's result goes into (PUTNEW) and its user frees (FRETMP), last
; first. The data is in the program's text (a literal), in a buffer below
; TXTTAB, or in the string space: down from MEMSIZ, FRETOP the highest free
; byte. GETSPA takes from FRETOP; when it can't, the collector (GARBA2) moves
; every string still named by a descriptor to the top, the highest first, and
; GETSPA tries once more: then Out of string space.
;
; Registers as the 8086's: BX (RBL), DX (RDL) and CX (RCL) are pointers when
; the 8086's are, used as (RBL),Y. Where the 8086 keeps a descriptor on its
; stack across an evaluation, here it is the BASIC stack; across GETSPA,
; whose collection moves data (never descriptors), it is S1 and S2, which the
; collector doesn't touch.
;
; STR$ VAL HEX$ OCT$ are in P1 (p1/strfn.s), through the F_ stubs here.
; MID$ as a statement (LHSMID) and STRING$ are P3's (7b step a2), with
; their stubs in res/far3.s; MIDRST is LHSMID's too. Later: the PRINT USING
; entry (LEFTUS) is 7b's. The collector walks DEF FN's parameter blocks too
; (7b step b): PARM1, the active function's, and those of the functions it
; was called from, on the BASIC stack (p3/gweval.s FNDOER), chained from
; PRMSTK; their state (PRMSTK, PRMLEN, PARM1, PRMLN2, FUNACT) is here, after
; DSCTMP, as GWDATA.ASM has it: a descriptor at DSCTMP or above isn't a
; temporary (LETSTR, FNDOER's NOCPRS).

.include "gwbasic.inc"

.export STRCMP, STRCPY, STRIN1, STRINI, STRAD2, STRAD1, STRLIT, STRLTI, STRLT3
.export STRLT2, PUTNEW, PUTTMP, STRPRT, STROUI, DSOUT, GETSPA, GARBA2, CAT, MOVSTR
.export FRESTR, FREFAC, FRETM2, FRETMP, FRETMS, LEN, LEN1, ASC, ASC2, CHR
.export SETSTR, SPACE, SPACE2, LEFT, RIGHT, MID, MIDRST, INSTR, FRE, LETSTR, GETBCD
.export MEMSIZ, TEMPST, DSCTMP, DSEGZ, NUMTMP
.export PRMSTK, PRMLEN, PARM1, PRMLN2, FUNACT
.export F_STR, F_VAL, F_STRO, F_STRH
.import CHRGTR, SYNCHR, FCERR, ERROR, CHKSTR, GETYP, EVAL, TSTOP, SPUSH, SPOP
.import SPUSHA, SPOPA, FRMEVL, FRMPRN, GETBYT, CONINT, SNGFLT, GIVDBL, OUTDO, CRFIN
.import BLTUC

NUMTMP          = 10                   ; OEM.H: the string temporaries
ERRSO           = 14                   ; Out of string space
ERRLS           = 15                   ; String too long
ERRST           = 16                   ; String formula too complex

.code

; --- STRCMP (BISTRS.ASM 61) ----------------------------------------------------

; The left string's descriptor on the BASIC stack (DORELS put it there), the
; right's in the FAC; both freed. A = 0 equal, 1 (C clear) if the left is
; less, $FF (C set) if greater: DOCMP's.
STRCMP:         JSR     FRESTR         ; BX: the right
                LDY     #1
                LDA     (RBL),Y
                STA     S2
                INY
                LDA     (RBL),Y
                STA     S2+1
                LDA     (RBL)
                PHA
                LDX     #RDL
                LDA     #2
                JSR     SPOP           ; DX: the left
                JSR     FRETMP         ; BX: the left
                PLA
                STA     RDH            ; DH: the right's length
                LDA     (RBL)
                STA     RDL            ; DL: the left's
                LDY     #1
                LDA     (RBL),Y
                STA     RCL            ; CX: the left's data
                INY
                LDA     (RBL),Y
                STA     RCH
                LDY     #0
CSLOOP:         LDA     RDL            ; both ended: equal
                ORA     RDH
                BEQ     @eq
                LDA     RDH            ; the right ended: greater
                BEQ     @gt
                LDA     RDL            ; the left ended: less
                BEQ     @lt
                DEC     RDH
                DEC     RDL
                LDA     (RCL),Y
                CMP     (S2),Y
                BNE     @ne
                INY
                BRA     CSLOOP
@ne:            BCS     @gt
@lt:            LDA     #1
                CLC
                RTS
@gt:            LDA     #$FF
                SEC
                RTS
@eq:            CLC
                RTS

; --- STR$ VAL OCT$ HEX$: P1's (BISTRS.ASM 117, 792) ----------------------------

F_STR:          FJSR    STR
                RTS
F_VAL:          FJSR    VAL
                RTS
F_STRO:         FJSR    STRO
                RTS
F_STRH:         FJSR    STRH
                RTS

; --- STRCPY, STRINI, STRAD2 (BISTRS.ASM 138) -----------------------------------

; STRCPY: a copy, in the string space, of the string whose descriptor is at
; BX; DX returns at DSCTMP, which describes it.
STRCPY:         LDA     RBH
                PHA
                LDA     RBL
                PHA
                LDA     (RBL)
                JSR     GETSPA         ; DX: the space
                PLA
                STA     RBL
                PLA
                STA     RBH
                LDY     #1             ; CX: the data, after any collection
                LDA     (RBL),Y
                STA     RCL
                INY
                LDA     (RBL),Y
                STA     RCH
                LDA     (RBL)
                JSR     STRAD2
                JSR     MOVSTR
                LDA     #<DSCTMP
                STA     RDL
                LDA     #>DSCTMP
                STA     RDH
                RTS

; STRIN1, STRINI: space for 1 (CHR$, INKEY$) or A characters, described at
; DSCTMP. STRAD2: DSCTMP = A, DX; STRAD1: the same at BX. BX returns at the
; descriptor, A kept.
STRIN1:         LDA     #1
STRINI:         JSR     GETSPA
STRAD2:         LDX     #<DSCTMP
                STX     RBL
                LDX     #>DSCTMP
                STX     RBH
STRAD1:         STA     (RBL)
PUTDEI:         PHA
                LDY     #1
                LDA     RDL
                STA     (RBL),Y
                INY
                LDA     RDH
                STA     (RBL),Y
                PLA
                RTS

; --- STRLIT, STRLTI (BISTRS.ASM 168) -------------------------------------------

; A literal at TXTPTR+1 (STRLTI: TXTPTR at its opening quote; STRLIT: at its
; first character), to a quote, a zero, CH or DH (STRLT3, STRLT2: CH and DH
; given; a comma in CH drops trailing spaces, for INPUT and READ). Its
; descriptor points into the text, in a temporary; TXTPTR returns past it
; (and past a closing quote), A the character there.
STRLIT:         DECW    TXTPTR
STRLTI:         LDA     #'"'
                STA     RCH
STRLT3:         LDA     RCH
                STA     RDH
STRLT2:         LDA     TXTPTR         ; S1: where it starts, less one
                STA     S1
                LDA     TXTPTR+1
                STA     S1+1
                LDX     #$FF           ; CL: the count
STRGET:         INCW    TXTPTR
                INX
                LDA     (TXTPTR)
                BEQ     STRFIN
                CMP     RDH
                BEQ     STRFIN
                CMP     RCH
                BNE     STRGET
STRFIN:         STX     RCL
                CMP     #'"'           ; past a closing quote
                BNE     :+
                JSR     CHRGTR
:               LDA     RCH            ; unquoted: no trailing spaces
                CMP     #','
                BNE     NTTRLS
                LDA     TXTPTR
                STA     RBL
                LDA     TXTPTR+1
                STA     RBH
                INC     RCL
LPTRLS:         DEC     RCL
                BEQ     NTTRLS
                DECW    RBL
                LDA     (RBL)
                CMP     #' '
                BEQ     LPTRLS
NTTRLS:         CLC                    ; DX: the first character
                LDA     S1
                ADC     #1
                STA     RDL
                LDA     S1+1
                ADC     #0
                STA     RDH
                LDA     RCL
                JSR     STRAD2

; PUTNEW: DSCTMP's string the result: copied to the next temporary, the
; FAC at it. PUTTMP: DX's. String formula too complex when they are all used.
PUTNEW:         LDA     #<DSCTMP
                STA     RDL
                LDA     #>DSCTMP
                STA     RDH
PUTTMP:         LDA     TEMPPT
                STA     FACLO
                LDA     TEMPPT+1
                STA     FACLO+1
                LDA     #VT_STR
                STA     VALTYP
                LDY     #2
@m:             LDA     (RDL),Y
                STA     (TEMPPT),Y
                DEY
                BPL     @m
                CLC
                LDA     TEMPPT
                ADC     #3
                STA     TEMPPT
                BCC     :+
                INC     TEMPPT+1
:               CMP     #<(DSCTMP+3)   ; TEMPPT was DSCTMP: none free
                BNE     @ok
                LDA     TEMPPT+1
                CMP     #>(DSCTMP+3)
                BNE     @ok
                LDA     #ERRST
                JMP     ERROR
@ok:            LDA     (TXTPTR)
                RTS

; --- STRPRT (BISTRS.ASM 271) ---------------------------------------------------

; STROUI (BISTRS.ASM 247): print the zero-ended text at FBPTR+1 ($FOUT's,
; past the sign's place: LINPRT's). DSOUT: at FBPTR. A literal of it first
; (STRLIT, which reads at TXTPTR: the text pointer kept round it), then
; STRPRT.
STROUI:         INCW    FBPTR
DSOUT:          LDA     TXTPTR+1
                PHA
                LDA     TXTPTR
                PHA
                LDA     FBPTR
                STA     TXTPTR
                LDA     FBPTR+1
                STA     TXTPTR+1
                JSR     STRLIT
                PLA
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
; STRPRT: print the FAC's string, and free it.
STRPRT:         JSR     FREFAC
                JSR     GETBCD
@l:             LDA     RDH
                BEQ     @r
                DEC     RDH
                LDA     (RCL)
                JSR     OUTDO
                CMP     #13
                BNE     :+
                JSR     CRFIN
:               INCW    RCL
                BRA     @l
@r:             RTS

; GETBCD (MATH1.ASM 2701): DH = the length, CX = the data, of BX's string.
GETBCD:         LDA     (RBL)
                STA     RDH
                LDY     #1
                LDA     (RBL),Y
                STA     RCL
                INY
                LDA     (RBL),Y
                STA     RCH
                RTS

; --- GETSPA, GARBAG (BISTRS.ASM 302) -------------------------------------------

; GETSPA: A characters from the string space: DX returns at them, A kept.
; With no room, a collection and once more; then Out of string space. (For a
; null string the 8086's flag says collected already.)
GETSPA:         STA     GSLEN
                LDX     #0
                CMP     #0
                BNE     :+
                INX
:               STX     GSDONE
TRYGI3:         SEC                    ; BX = FRETOP - the length
                LDA     FRETOP
                SBC     GSLEN
                STA     RBL
                LDA     FRETOP+1
                SBC     #0
                STA     RBH
                LDA     RBL            ; below STREND: no room
                CMP     STREND
                LDA     RBH
                SBC     STREND+1
                BCC     GARBAG
                LDA     RBL
                STA     FRETOP
                LDA     RBH
                STA     FRETOP+1
                CLC
                LDA     RBL
                ADC     #1
                STA     RDL
                LDA     RBH
                ADC     #0
                STA     RDH
                LDA     GSLEN
                RTS
GARBAG:         LDA     GSDONE
                BEQ     :+
                LDA     #ERRSO
                JMP     ERROR
:               INC     GSDONE
                JSR     GARBA2
                BRA     TRYGI3

; GARBA2: each pass finds the highest string not yet moved (below FRETOP,
; above STREND, so not a literal in the text) among the temporaries, the
; simple variables and the string arrays, and moves it up to FRETOP; FRETOP
; then goes below it, and again, until no string is left. Uses BX, CX, DX,
; SI, DI, T0 and T1, and its own GCMAX, GCVAR and TEMP8.
GARBA2:         LDA     MEMSIZ
                STA     RBL
                LDA     MEMSIZ+1
                STA     RBH
FNDVAR:         LDA     RBL
                STA     FRETOP
                LDA     RBH
                STA     FRETOP+1
                STZ     GCVAR          ; none seen this pass
                STZ     GCVAR+1
                LDA     STREND         ; and none below STREND counts
                STA     GCMAX
                LDA     STREND+1
                STA     GCMAX+1
                LDA     #<TEMPST
                STA     RBL
                LDA     #>TEMPST
                STA     RBH
TVAR:           LDA     RBL            ; the temporaries
                CMP     TEMPPT
                BNE     @t
                LDA     RBH
                CMP     TEMPPT+1
                BEQ     @v
@t:             JSR     DVARS
                BRA     TVAR
@v:             LDA     #<PRMPRV       ; the parameter blocks' chain, after
                STA     TEMP9          ; the simple variables
                LDA     #>PRMPRV
                STA     TEMP9+1
                LDA     ARYTAB         ; the simple variables
                STA     ARYTA2
                LDA     ARYTAB+1
                STA     ARYTA2+1
                LDA     VARTAB
                STA     RBL
                LDA     VARTAB+1
                STA     RBH
SVAR:           LDA     RBL
                CMP     ARYTA2
                BNE     @s
                LDA     RBH
                CMP     ARYTA2+1
                BEQ     ARYVAR
@s:             LDA     (RBL)          ; the type
                PHA
                LDY     #3             ; past the name: 4 and its extra characters
                LDA     (RBL),Y
                CLC
                ADC     #4
                ADC     RBL
                STA     RBL
                BCC     :+
                INC     RBH
:               PLA
                CMP     #VT_STR
                BNE     SKPVAR
                JSR     DVARS
                BRA     SVAR
SKPVAR:         CLC
                ADC     RBL
                STA     RBL
                BCC     SVAR
                INC     RBH
                BRA     SVAR
; ARYVAR (BISTRS.ASM 361): the next parameter block, walked as the simple
; variables are: TEMP9 at the link to it (PRMPRV's, at first, to PRMSTK:
; PARM1 itself), the block its link, its length and its entries. A zero link:
; the arrays.
ARYVAR:         LDA     (TEMP9)        ; DX: the next block
                TAX
                LDY     #1
                LDA     (TEMP9),Y
                BNE     @blk
                CPX     #0
                BEQ     ARYVA0         ; the end of the chain
@blk:           STA     TEMP9+1        ; TEMP9 at it, to go on from
                STX     TEMP9
                CLC                    ; BX past the link and the length
                TXA
                ADC     #4
                STA     RBL
                LDA     TEMP9+1
                ADC     #0
                STA     RBH
                LDY     #2             ; ARYTA2: BX + the length
                CLC
                LDA     (TEMP9),Y
                ADC     RBL
                STA     ARYTA2
                INY
                LDA     (TEMP9),Y
                ADC     RBH
                STA     ARYTA2+1
                JMP     SVAR
ARYVA0:         LDA     ARYTAB         ; the arrays
                STA     RBL
                LDA     ARYTAB+1
                STA     RBH
ARYVA4:         LDA     RBL
                CMP     STREND
                BNE     @a
                LDA     RBH
                CMP     STREND+1
                BNE     @a
                JMP     GRBPAS
@a:             LDA     (RBL)
                PHA                    ; the type
                LDY     #3
                LDA     (RBL),Y
                CLC
                ADC     #4
                TAY                    ; the size
                LDA     (RBL),Y
                STA     RCL
                INY
                LDA     (RBL),Y
                STA     RCH
                INY
                TYA                    ; BX past it
                CLC
                ADC     RBL
                STA     RBL
                BCC     :+
                INC     RBH
:               CLC                    ; TEMP8: the array's end
                LDA     RBL
                ADC     RCL
                STA     TEMP8
                LDA     RBH
                ADC     RCH
                STA     TEMP8+1
                PLA
                CMP     #VT_STR
                BEQ     @str
                LDA     TEMP8          ; not strings: past it
                STA     RBL
                LDA     TEMP8+1
                STA     RBH
                BRA     ARYVA4
@str:           LDA     (RBL)          ; past the dimensions, 2 bytes each
                ASL     A
                STA     RCL
                LDA     #0
                ROL     A
                STA     RCH
                SEC
                LDA     RBL
                ADC     RCL
                STA     RBL
                LDA     RBH
                ADC     RCH
                STA     RBH
ARYSTR:         LDA     RBL            ; each descriptor
                CMP     TEMP8
                BNE     @d
                LDA     RBH
                CMP     TEMP8+1
                BEQ     ARYVA4
@d:             JSR     DVARS
                BRA     ARYSTR

; DVARS: the descriptor at BX (BX left past it): the highest so far?
DVARS:          LDY     #1
                LDA     (RBL),Y
                STA     RDL
                INY
                LDA     (RBL),Y
                STA     RDH
                LDA     (RBL)
                TAX
                CLC
                LDA     RBL
                ADC     #3
                STA     RBL
                BCC     :+
                INC     RBH
:               TXA                    ; the null string
                BEQ     @r
                LDA     FRETOP         ; moved already: above FRETOP
                CMP     RDL
                LDA     FRETOP+1
                SBC     RDH
                BCC     @r
                LDA     GCMAX          ; not above the highest so far
                CMP     RDL
                LDA     GCMAX+1
                SBC     RDH
                BCS     @r
                LDA     RDL
                STA     GCMAX
                LDA     RDH
                STA     GCMAX+1
                LDA     RBL
                STA     GCVAR
                LDA     RBH
                STA     GCVAR+1
@r:             RTS

; GRBPAS: a pass done. The highest string up to FRETOP, its descriptor
; changed, FRETOP below it, and another pass; none: done.
GRBPAS:         LDA     GCVAR
                ORA     GCVAR+1
                BNE     :+
                RTS
:               SEC                    ; GCVAR: the descriptor
                LDA     GCVAR
                SBC     #3
                STA     GCVAR
                BCS     :+
                DEC     GCVAR+1
:               LDY     #1             ; DX: the data
                LDA     (GCVAR),Y
                STA     RDL
                INY
                LDA     (GCVAR),Y
                STA     RDH
                LDA     (GCVAR)        ; CX: its last byte
                CLC
                ADC     RDL
                STA     RCL
                LDA     RDH
                ADC     #0
                STA     RCH
                DECW    RCL
                LDA     FRETOP         ; BX: where that goes
                STA     RBL
                LDA     FRETOP+1
                STA     RBH
                JSR     BLTUC          ; CX: where the first went
                LDY     #1
                LDA     RCL
                STA     (GCVAR),Y
                INY
                LDA     RCH
                STA     (GCVAR),Y
                SEC                    ; FRETOP below it
                LDA     RCL
                SBC     #1
                STA     RBL
                LDA     RCH
                SBC     #0
                STA     RBH
                JMP     FNDVAR

; --- CAT (BISTRS.ASM 496) ------------------------------------------------------

; "+" with a string on the left (TSTOP saw it): the next operand, which must
; be a string, joined to it, and back to TSTOP. CH: the old precedence.
CAT:            LDA     RCH            ; back on, for RETAOP
                JSR     SPUSHA
                LDX     #FACLO         ; the first's descriptor
                LDA     #2
                JSR     SPUSH
                JSR     EVAL           ; the second
                LDX     #S1
                LDA     #2
                JSR     SPOP           ; S1: the first
                JSR     CHKSTR
                LDA     FACLO          ; S2: the second
                STA     S2
                LDA     FACLO+1
                STA     S2+1
                LDA     (S1)
                CLC
                ADC     (S2)
                BCC     :+
                LDA     #ERRLS
                JMP     ERROR
:               JSR     STRINI         ; the space
                LDA     S2             ; both freed
                STA     RDL
                LDA     S2+1
                STA     RDH
                JSR     FRETMP
                LDA     S1
                STA     RBL
                LDA     S1+1
                STA     RBH
                JSR     FRETM2
                LDA     DSCTMP+1       ; DX: the copy
                STA     RDL
                LDA     DSCTMP+2
                STA     RDH
                LDA     S1
                STA     RBL
                LDA     S1+1
                STA     RBH
                JSR     MOVINS
                LDA     S2
                STA     RBL
                LDA     S2+1
                STA     RBH
                JSR     MOVINS
                JSR     PUTNEW
                JMP     TSTOP

; MOVINS: BX's string to DX. MOVSTR: A bytes from CX to DX; both left past.
MOVINS:         LDA     (RBL)
                PHA
                LDY     #1
                LDA     (RBL),Y
                STA     RCL
                INY
                LDA     (RBL),Y
                STA     RCH
                PLA
MOVSTR:         TAX
                BEQ     @r
                LDY     #0
@m:             LDA     (RCL),Y
                STA     (RDL),Y
                INY
                DEX
                BNE     @m
                TYA
                CLC
                ADC     RCL
                STA     RCL
                BCC     :+
                INC     RCH
:               TYA
                CLC
                ADC     RDL
                STA     RDL
                BCC     @r
                INC     RDH
@r:             RTS

; --- FRESTR, FREFAC, FRETMP, FRETMS (BISTRS.ASM 573) ---------------------------

; FRETMP: the descriptor at DX freed if it is the last temporary, and its
; data too if that is the lowest in the string space. BX returns at it
; (still good until the next temporary). FRESTR: the FAC's, which must be a
; string; FREFAC: the FAC's; FRETM2: BX's.
FRESTR:         JSR     CHKSTR
FREFAC:         LDA     FACLO
                STA     RBL
                LDA     FACLO+1
                STA     RBH
FRETM2:         LDA     RBL
                STA     RDL
                LDA     RBH
                STA     RDH
FRETMP:         JSR     FRETMS
                PHP
                LDA     RDL
                STA     RBL
                LDA     RDH
                STA     RBH
                PLP
                BNE     NOTLST
                SEC                    ; its data at FRETOP+1?
                LDA     RCL
                SBC     #1
                TAX
                LDA     RCH
                SBC     #0
                CPX     FRETOP
                BNE     NOTLST
                CMP     FRETOP+1
                BNE     NOTLST
                LDA     (RBL)          ; then FRETOP up past it
                CLC
                ADC     FRETOP
                STA     FRETOP
                BCC     NOTLST
                INC     FRETOP+1
NOTLST:         RTS

; FRETMS: if DX is the last temporary, it is freed: Z set. CX: the last
; temporary's data; BX: the last temporary.
FRETMS:         SEC
                LDA     TEMPPT
                SBC     #3
                STA     RBL
                LDA     TEMPPT+1
                SBC     #0
                STA     RBH
                LDY     #1
                LDA     (RBL),Y
                STA     RCL
                INY
                LDA     (RBL),Y
                STA     RCH
                LDA     RBL
                CMP     RDL
                BNE     @r
                LDA     RBH
                CMP     RDH
                BNE     @r
                LDA     RBL
                STA     TEMPPT
                LDA     RBH
                STA     TEMPPT+1
                LDA     #0
@r:             RTS

; --- LEN ASC CHR$ SPACE$ (BISTRS.ASM 619) --------------------------------------

; From FUNDSP, the argument in the FAC, back through LABBCK.
LEN:            JSR     LEN1
                JMP     SNGFLT
LEN1:           JSR     FRESTR         ; BX: the descriptor
                STZ     RDH            ; (for VAL)
                LDA     (RBL)          ; the length, Z by it
                RTS
ASC:            JSR     ASC2
                JMP     SNGFLT
ASC2:           JSR     LEN1
                BNE     :+
                JMP     FCERR          ; a null string
:               LDY     #1
                LDA     (RBL),Y
                STA     RDL
                INY
                LDA     (RBL),Y
                STA     RDH
                LDA     (RDL)
                RTS
CHR:            JSR     STRIN1         ; the space first
                JSR     CONINT         ; DL: the code, at most 255
SETSTR:         LDA     DSCTMP+1
                STA     RBL
                LDA     DSCTMP+2
                STA     RBH
                LDA     RDL
                STA     (RBL)
FINBCK:         JMP     PUTNEW
SPACE:          JSR     CONINT         ; DL: how many
                LDA     #' '
SPACE2:         PHA                    ; A: the character
                LDA     RDL
                JSR     STRINI
                TAY
                PLA
                CPY     #0
                BEQ     FINBCK
                LDX     DSCTMP+1
                STX     RBL
                LDX     DSCTMP+2
                STX     RBH
@f:             DEY
                STA     (RBL),Y
                BNE     @f
                BRA     FINBCK

; --- LEFT$ RIGHT$ MID$ (BISTRS.ASM 707) ----------------------------------------

; From ISFUN, the string's descriptor and the first number on the BASIC
; stack, the number on top, TXTPTR after it; back to FRMEVL directly.
LEFT:           JSR     PREAM
                LDA     #0             ; LEFT$ keeps the start
LEFT3:          STA     RCL            ; CL: the offset
                LDX     #RBL
                LDA     #2
                JSR     SPOP           ; BX: the descriptor
LEFT2:          LDA     (RBL)          ; CH characters, or all there are
                CMP     RCH
                BCC     ALLSTR
                LDA     RCH
                BRA     :+
ALLSTR:         STZ     RCL
:               STA     SLEN
                LDA     RBL
                STA     S1
                LDA     RBH
                STA     S1+1
                LDA     RCL
                PHA
                LDA     SLEN
                JSR     GETSPA         ; DX: the space
                PLA                    ; CX: the data, after any collection,
                LDY     #1             ; and the offset
                CLC
                ADC     (S1),Y
                STA     RCL
                INY
                LDA     (S1),Y
                ADC     #0
                STA     RCH
                LDA     SLEN
                JSR     STRAD2
                JSR     MOVSTR
                LDA     S1             ; the argument freed
                STA     RDL
                LDA     S1+1
                STA     RDH
                JSR     FRETMP
                JMP     PUTNEW
RIGHT:          JSR     PREAM
                LDA     (SSP)          ; the descriptor, left on the stack
                STA     RDL
                LDY     #1
                LDA     (SSP),Y
                STA     RDH
                LDA     (RDL)          ; the offset: the length less CH
                SEC
                SBC     RCH
                BRA     LEFT3
MID:            JSR     PREAM2         ; CH: the start
                LDA     RCH
                BNE     :+
                JMP     FCERR
:               JSR     SPUSHA
                LDA     (TXTPTR)
                JSR     MIDRST         ; DL: the length
                JSR     SPOPA
                PHA
                LDX     #RBL
                LDA     #2
                JSR     SPOP           ; BX: the descriptor
                PLA
                DEC     A
                CMP     (RBL)          ; past the end: the null string
                BCC     :+
                STZ     RCH
                JMP     LEFT2
:               STA     RCL
                LDA     (RBL)
                SEC
                SBC     RCL
                CMP     RDL
                STA     RCH
                BCC     @l2
                LDA     RDL
                STA     RCH
@l2:            JMP     LEFT2

; PREAM: ")" and the number off the BASIC stack, CH (DX) its value. PREAM2:
; without the ")".
PREAM:          JSR     SYNCHR
                .byte   ')'
PREAM2:         LDX     #RDL
                LDA     #2
                JSR     SPOP
                LDA     RDL
                STA     RCH
                RTS

; MIDRST: MID$'s third argument, or 255; and ")".
MIDRST:         LDX     #$FF
                STX     RDL
                CMP     #')'
                BEQ     MID2
                JSR     SYNCHR
                .byte   ','
                JSR     GETBYT
MID2:           JSR     SYNCHR
                .byte   ')'
                RTS

; --- INSTR (BISTRS.ASM 855) ----------------------------------------------------

; From EVAL, TXTPTR at INSTR: INSTR([I,]S1$,S2$), the position of S2$ in
; S1$ from I (or 1), or 0; I if S2$ is null and I is within S1$. In the
; search, S1 and S2 keep where it started in S1$ and S2$, ATEMP3 the counts
; there and GCMAX the counts at the first (no allocation happens here).
INSTR:          JSR     CHRGTR
                JSR     FRMPRN
                JSR     GETYP
                BNE     @num
                LDA     #1             ; a string: from 1
                JSR     SPUSHA
                BRA     WUZSTR
@num:           JSR     CONINT
                LDA     RDL
                BNE     :+
                JMP     FCERR          ; not from 0
:               JSR     SPUSHA
                JSR     SYNCHR
                .byte   ','
                JSR     FRMEVL
                JSR     CHKSTR
WUZSTR:         JSR     SYNCHR
                .byte   ','
                LDX     #FACLO         ; S1$
                LDA     #2
                JSR     SPUSH
                JSR     FRMEVL
                JSR     SYNCHR
                .byte   ')'
                JSR     FRESTR         ; S2$, freed
                LDA     RBL
                STA     S2
                LDA     RBH
                STA     S2+1
                LDX     #S1
                LDA     #2
                JSR     SPOP
                JSR     SPOPA          ; the start
                PHA
                LDA     S1
                STA     RBL
                LDA     S1+1
                STA     RBH
                JSR     FRETM2         ; S1$ freed
                PLA
                STA     RCH            ; CH: the start as given
                DEC     A
                STA     RCL            ; CL: the offset
                CMP     (S1)
                BCS     RETZR1         ; past S1$: 0
                LDA     (S2)
                BNE     :+
                LDA     RCH            ; S2$ null: the start
                JMP     SNGFLT
:               LDY     #1             ; BX: S1$ from the offset
                LDA     (S1),Y
                CLC
                ADC     RCL
                STA     RBL
                INY
                LDA     (S1),Y
                ADC     #0
                STA     RBH
                LDA     (S1)           ; CH: what is left of S1$
                SEC
                SBC     RCL
                STA     RCH
                STA     GCMAX+1
                LDA     RCL
                STA     GCMAX
                LDA     (S2)           ; CL: S2$'s length; DX its data
                STA     RCL
                LDY     #1
                LDA     (S2),Y
                STA     RDL
                INY
                LDA     (S2),Y
                STA     RDH
CHK1:           LDA     RBL
                STA     S1
                LDA     RBH
                STA     S1+1
                LDA     RDL
                STA     S2
                LDA     RDH
                STA     S2+1
                LDA     RCL
                STA     ATEMP3
                LDA     RCH
                STA     ATEMP3+1
CHK:            LDA     (RDL)
                CMP     (RBL)
                BNE     OHWELL
                INCW    RDL
                DEC     RCL
                BEQ     GOTSTR         ; all of S2$
                INCW    RBL
                DEC     RCH
                BNE     CHK
RETZR1:         LDA     #0             ; S1$ ended
                JMP     SNGFLT
GOTSTR:         LDA     GCMAX+1        ; the position
                SEC
                SBC     ATEMP3+1
                CLC
                ADC     GCMAX
                INC     A
                JMP     SNGFLT
OHWELL:         LDA     ATEMP3         ; from the next character
                STA     RCL
                LDA     ATEMP3+1
                STA     RCH
                LDA     S2
                STA     RDL
                LDA     S2+1
                STA     RDH
                LDA     S1
                STA     RBL
                LDA     S1+1
                STA     RBH
                INCW    RBL
                DEC     RCH
                BNE     CHK1
                BRA     RETZR1

; --- FRE (BISTRS.ASM 1097) -----------------------------------------------------

; FRE(x): the bytes between STREND and FRETOP; for a string, after a
; collection.
FRE:            JSR     GETYP
                BNE     CLCDIF
                JSR     FREFAC
                JSR     GARBA2
CLCDIF:         LDA     STREND
                STA     RDL
                LDA     STREND+1
                STA     RDH
                LDA     FRETOP
                STA     RBL
                LDA     FRETOP+1
                STA     RBH
                JMP     GIVDBL

; --- LET of a string (GWMAIN.ASM 2305, LETCN6's string half) -----------------

; From LET, PTR at the variable's descriptor, the FAC a string. A new copy
; of the data is made if it is in a buffer (at or below TXTTAB), or in the
; string space but named by another variable; not for a literal in the text,
; nor for a temporary's, which the variable takes over. DX returns at the
; variable, for FOR.
LETSTR:         LDA     PTR+1
                PHA
                LDA     PTR
                PHA
                LDA     FACLO          ; DX: the result's descriptor
                STA     RDL
                LDA     FACLO+1
                STA     RDH
                LDY     #1             ; CX: its data
                LDA     (RDL),Y
                STA     RCL
                INY
                LDA     (RDL),Y
                STA     RCH
                LDA     TXTTAB         ; in a buffer: copied
                CMP     RCL
                LDA     TXTTAB+1
                SBC     RCH
                BCS     INBUFC
                LDA     STREND         ; not in the string space: not copied
                CMP     RCL
                LDA     STREND+1
                SBC     RCH
                BCS     DNTCPY
                LDA     #<(DSCTMP-1)   ; a variable's: copied
                CMP     RDL
                LDA     #>(DSCTMP-1)
                SBC     RDH
                BCC     INBUFC
                LDA     #<(TEMPST-1)   ; a temporary's: not copied
                CMP     RDL
                LDA     #>(TEMPST-1)
                SBC     RDH
                BCC     DNTCPY
INBUFC:         JSR     FRETMS         ; (a temporary freed: its data stays)
                LDA     RDL
                STA     RBL
                LDA     RDH
                STA     RBH
                JSR     STRCPY         ; DX: DSCTMP
DNTCPY:         JSR     FRETMS         ; the temporary freed, not its space
                PLA
                STA     PTR
                PLA
                STA     PTR+1
                LDY     #2             ; COPNUM: the descriptor in
@m:             LDA     (RDL),Y
                STA     (PTR),Y
                DEY
                BPL     @m
                LDA     PTR
                STA     RDL
                LDA     PTR+1
                STA     RDH
                RTS

.segment "RAM"

; GWDATA.ASM's
MEMSIZ:         .res    2              ; the string space's top byte
                .res    1              ; (FRETMS reads below TEMPST when there are none)
TEMPST:         .res    3 * NUMTMP     ; the temporaries
DSCTMP:         .res    3              ; a result's descriptor: just past them
DSEGZ:          .byte   0, 0, 0        ; the null string (an undefined A$)

; DEF FN's parameters (GWDATA.ASM 1310, 7b step b). PRMSTK, PRMLEN and PARM1
; in this order: FNDOER pushes and pops them as one block. PRMLN2, FUNACT,
; PRMSTK and PRMLEN together: STKERR zeroes them in one loop. PARM2, where
; FNDOER builds the next PARM1, is P3's own (p3/gweval.s).
PRMLN2:         .res    2              ; the bytes in PARM2 so far
FUNACT:         .res    2              ; the functions being evaluated
PRMSTK:         .res    2              ; the last parameter block on the BASIC stack, or 0
PRMLEN:         .res    2              ; the bytes in PARM1
PARM1:          .res    PRMSIZ         ; the active function's parameters, as simple variables
.assert PARM1 > DSCTMP, error, "PARM1 must be past DSCTMP"
.assert PRMLEN = PRMLN2 + 6 && PARM1 = PRMLEN + 2, error, "STKERR and FNDOER take these as one block"

.rodata

PRMPRV:         .word   PRMSTK         ; the link to PRMSTK: GARBA2 walks PARM1 first

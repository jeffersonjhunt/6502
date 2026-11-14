; The math harness of GW-BASIC's flat test image (the project's own, from the
; size spike's oodos/spike/gwbasic/harness.s): segment HARNESS, at $0300.
; check.py writes the vectors and reads them back; run6502 runs the image.
;
; A control block at $9FF0 says what to do:
;
;   $9FF0   0: run the math vectors at $A000; 1: run the program at $9000;
;           2: GW-BASIC itself, from READY, with its workspace at $9001 (to
;           MEMSIZ), its lines typed from run6502's --input file
;   $9FF2   the number of vectors
;   $9FF6   a program's MEMSIZ, the string space's top (0: $9FEE, leaving a
;           byte below the control block for VAL's zero)
;   $9FFE   run6502's count of the characters output (--outcount)
;
; A vector is 80 bytes:
;
;   +0      the operation (OPS below)
;   +1      A's VALTYP
;   +2      out: where $FIN stopped, from +56
;   +3      out: the characters it printed (Overflow and the rest)
;   +4      A, 8 bytes (an integer or single in its first 2 or 4)
;   +12     B, 8 bytes (RND: RNDX before)
;   +20     out: VALTYP after
;   +21     out: A after (a compare's result)
;   +22     out: the error number, or 0
;   +24     out: DFACL..FACEXP, 8 bytes
;   +32     out: $FOUT's text, to its zero (23 characters at most); RND:
;           RNDX after
;   +56     in: $FIN's text, to its zero (23 characters at most)
;
; The program is GW-BASIC's tokenised form at $9001 (link, line number,
; tokens, zero), after a zero at $9000, ending with a zero link.

.include "gwbasic.inc"

.import FADDS, FSUBS, FMULS, FDIVS, FCOMP, FLT, FRCINT, INT, FIXER, IADD
.import ISUB, IMULT, IDIV, IMOD, ICOMP, INTDIV, FOUT, DMULT, DDIV, CSD
.import SGN, VNEG, VABS, MUL10, DIV10, NEWSTT, ERRHOOK, DADD, DSUB, DCOMP
.import DINT, FIN, FINDBL, SQR, EXP, LOG, SIN, COS, TAN, ATN, FPWR, RND
.import FOUTO, FOUTH, RNDX
.import MEMSIZ, TEMPST, DEFTBL, OPTVAL, SUBFLG, INITFG, TOPS, TOPMEM, INITSA

CTRL            = $9FF0
OUTCNT          = $9FFE
VECTORS         = $A000
PROGRAM         = $9000
VLEN            = 80

HREC            = $E0                  ; the vector being run
HCOUNT          = $E2
HRES            = $E4
HSAVES          = $E5                  ; S and SSP, to recover from an error
HOUT            = $E8                  ; OUTCNT before the vector

.segment "HARNESS"

START:          CLD
                LDX     #$FF
                TXS
                LDA     #<SSTOP        ; the BASIC stack, and the state
                STA     SSP
                LDA     #>SSTOP
                STA     SSP+1
                LDA     #<SSTACK
                STA     STKLOW
                LDA     #>SSTACK
                STA     STKLOW+1
                STZ     FLGOV
                STZ     ONELIN
                STZ     ONELIN+1
                STZ     TRCFLG
                STZ     ONGSBF
                STZ     TRPON          ; no event trap on, no test trigger (7b step e)
                STZ     TRPTST
                STZ     ERRFLG
                STZ     PTRFLG
                STZ     FMTAL
                STZ     FMTAH
                STZ     ERRHOOK
                STZ     ERRHOOK+1
                LDA     #$FF           ; as start-up leaves them (GWINIT, INITSA): errors
                STA     INITFG         ; are BASIC's; both stacks' tops for STKINI
                STA     TOPS
                LDA     #<SSTOP
                STA     TOPMEM
                LDA     #>SSTOP
                STA     TOPMEM+1
                LDA     CTRL
                CMP     #2
                BEQ     INTRUN
                CMP     #1
                BEQ     BASRUN
                JMP     MATHRUN

; --- GW-BASIC from READY ------------------------------------------------------------

; As P2's GWINIT leaves memory (but the workspace here), then INITSA: NEW and
; READY. No heading.
INTRUN:         LDA     #<(PROGRAM+1)
                STA     TXTTAB
                LDA     #>(PROGRAM+1)
                STA     TXTTAB+1
                LDA     CTRL+6
                LDX     CTRL+7
                BNE     :+
                LDA     #<$9FEE
                LDX     #>$9FEE
:               STA     MEMSIZ
                STA     FRETOP
                STX     MEMSIZ+1
                STX     FRETOP+1
                LDA     #<TEMPST
                STA     TEMPPT
                LDA     #>TEMPST
                STA     TEMPPT+1
                STZ     PTRFIL
                STZ     PTRFIL+1
                STZ     TTYPOS
                STZ     LPTPOS
                JMP     INITSA

; --- A program -----------------------------------------------------------------

BASRUN:         LDA     #<(PROGRAM+1)
                STA     TXTTAB
                LDA     #>(PROGRAM+1)
                STA     TXTTAB+1
                LDA     #<PROGRAM
                STA     TXTPTR
                LDA     #>PROGRAM
                STA     TXTPTR+1
                LDA     #$FF
                STA     CURLIN
                STA     CURLIN+1
; The workspace as RUN's CLEARC leaves it (BIMISC.ASM's, the interpreter's
; own to translate): the variables after the program's zero link, no
; arrays, the string space empty, no temporaries, every letter single.
                LDA     TXTTAB
                STA     PTR
                LDA     TXTTAB+1
                STA     PTR+1
@link:          LDY     #1             ; to the zero link
                LDA     (PTR),Y
                TAX
                LDA     (PTR)
                BNE     :+
                CPX     #0
                BEQ     @end
:               STA     PTR
                STX     PTR+1
                BRA     @link
@end:           CLC
                LDA     PTR
                ADC     #2
                STA     VARTAB
                STA     ARYTAB
                STA     STREND
                LDA     PTR+1
                ADC     #0
                STA     VARTAB+1
                STA     ARYTAB+1
                STA     STREND+1
                LDA     CTRL+6
                LDX     CTRL+7
                BNE     :+
                LDA     #<$9FEE
                LDX     #>$9FEE
:               STA     MEMSIZ
                STA     FRETOP
                STX     MEMSIZ+1
                STX     FRETOP+1
                LDA     #<TEMPST
                STA     TEMPPT
                LDA     #>TEMPST
                STA     TEMPPT+1
                LDX     #25
                LDA     #VT_SNG
@def:           STA     DEFTBL,X
                DEX
                BPL     @def
                STZ     OPTVAL
                STZ     SUBFLG
                JMP     NEWSTT

; --- The math vectors ------------------------------------------------------------

MATHRUN:        LDA     #<HERROR
                STA     ERRHOOK
                LDA     #>HERROR
                STA     ERRHOOK+1
                LDA     #<VECTORS
                STA     HREC
                LDA     #>VECTORS
                STA     HREC+1
                LDA     CTRL+2
                STA     HCOUNT
                LDA     CTRL+3
                STA     HCOUNT+1
NEXT:           LDA     HCOUNT
                ORA     HCOUNT+1
                BNE     :+
                LDY     #P_TERMCPM
                JMP     BDOS
:               TSX
                STX     HSAVES
                LDA     SSP
                STA     HSAVES+1
                LDA     SSP+1
                STA     HSAVES+2
                STZ     FLGOV
                LDA     OUTCNT
                STA     HOUT
                LDY     #22
                LDA     #0
                STA     (HREC),Y
                LDY     #2
                STA     (HREC),Y
                LDY     #1
                LDA     (HREC),Y
                STA     VALTYP
                LDA     (HREC)
                ASL     A
                TAX
                JSR     CALLOP
                STA     HRES
DONE:           LDY     #20
                LDA     VALTYP
                STA     (HREC),Y
                INY
                LDA     HRES
                STA     (HREC),Y
                LDY     #24
                LDX     #0
@out:           LDA     DFACL,X
                STA     (HREC),Y
                INY
                INX
                CPX     #8
                BNE     @out
                LDA     OUTCNT         ; what it printed
                SEC
                SBC     HOUT
                LDY     #3
                STA     (HREC),Y
                CLC
                LDA     HREC
                ADC     #VLEN
                STA     HREC
                BCC     :+
                INC     HREC+1
:               LDA     HCOUNT
                BNE     :+
                DEC     HCOUNT+1
:               DEC     HCOUNT
                JMP     NEXT
CALLOP:         JMP     (OPS,X)

; An error during a vector: its number, and on to the next.
HERROR:         LDY     #22
                STA     (HREC),Y
                LDX     HSAVES
                TXS
                LDA     HSAVES+1
                STA     SSP
                LDA     HSAVES+2
                STA     SSP+1
                STZ     HRES
                BRA     DONE

; A to BXDX, B to the FAC (singles); or A to DX and B to BX (integers).
LDAB:           LDY     #4
                LDX     #0
@a:             LDA     (HREC),Y
                STA     RDL,X
                INY
                INX
                CPX     #4
                BNE     @a
                LDY     #12
                LDX     #0
@b:             LDA     (HREC),Y
                STA     FACLO,X
                INY
                INX
                CPX     #4
                BNE     @b
                RTS
; A to DX and B to BX, integers (and B to FACLO, as INTDPC leaves it).
LDINT:          LDY     #4
                LDA     (HREC),Y
                STA     RDL
                INY
                LDA     (HREC),Y
                STA     RDH
                LDY     #12
                LDA     (HREC),Y
                STA     RBL
                STA     FACLO
                INY
                LDA     (HREC),Y
                STA     RBH
                STA     FACLO+1
                RTS
; A to the FAC: 8 bytes from DFACL if a double, else 4 from FACLO.
LDFAC:          LDY     #4
                LDX     #0
                LDA     VALTYP
                CMP     #VT_DBL
                BEQ     @d
@s:             LDA     (HREC),Y
                STA     FACLO,X
                INY
                INX
                CPX     #4
                BNE     @s
                RTS
@d:             LDA     (HREC),Y
                STA     DFACL,X
                INY
                INX
                CPX     #8
                BNE     @d
                RTS
; A to the FAC and B to the ARG, doubles.
LDDBL:          JSR     LDFAC
                LDY     #12
                LDX     #0
@b:             LDA     (HREC),Y
                STA     ARGLO,X
                INY
                INX
                CPX     #8
                BNE     @b
                RTS
LDCMP:          JSR     LDAB           ; A against B: the FAC is A
                LDX     #3
@sw:            LDA     RDL,X
                LDY     FACLO,X
                STA     FACLO,X
                STY     RDL,X
                DEX
                BPL     @sw
                RTS
; The text at +56 for $FIN, the FAC zero.
LDTXT:          LDX     #7
@z:             STZ     DFACL,X
                DEX
                BPL     @z
                CLC
                LDA     HREC
                ADC     #56
                STA     TXTPTR
                LDA     HREC+1
                ADC     #0
                STA     TXTPTR+1
                RTS
; Where $FIN stopped, to +2.
FINEND:         SEC
                LDA     TXTPTR
                SBC     HREC
                SEC
                SBC     #56
                LDY     #2
                STA     (HREC),Y
                LDA     #0
                RTS
; $FOUT's text (at FBPTR) to +32.
TXTOUT:         LDY     #0
@t:             LDA     (FBPTR),Y
                TAX
                PHY
                TYA
                CLC
                ADC     #32
                TAY
                TXA
                STA     (HREC),Y
                PLY
                TXA
                BEQ     @done
                INY
                CPY     #23
                BNE     @t
@done:          LDA     #0
                RTS

OPS:            .word   0, OPADD, OPSUB, OPMUL, OPDIV, OPCMP, OPFLT, OPCINT
                .word   OPINT, OPFIX, OPIADD, OPISUB, OPIMUL, OPIDIV, OPIMOD
                .word   OPICMP, OPINTDV, OPFOUT, OPDMUL, OPDDIV, OPCSD, OPSGN
                .word   OPNEG, OPABS, OPMUL10, OPDIV10, OPDADD, OPDSUB, OPDCMP
                .word   OPDINT, OPFIN, OPFINDB, OPSQR, OPEXP, OPLOG, OPSIN
                .word   OPCOS, OPTAN, OPATN, OPPWR, OPRND, OPFOUTO, OPFOUTH
                .word   LDAB, LDINT, LDFAC, LDDBL, LDCMP, OPFOUTT, OPLDTXT

OPADD:          JSR     LDAB
                JMP     FADDS
OPSUB:          JSR     LDAB
                JMP     FSUBS
OPMUL:          JSR     LDAB
                JMP     FMULS
OPDIV:          JSR     LDAB
                JMP     FDIVS
OPCMP:          JSR     LDCMP
                JMP     FCOMP
OPFLT:          JSR     LDAB
                JMP     FLT
OPCINT:         JSR     LDFAC
                JMP     FRCINT
OPINT:          JSR     LDFAC
                JMP     INT
OPFIX:          JSR     LDFAC
                JMP     FIXER
OPIADD:         JSR     LDINT
                JMP     IADD
OPISUB:         JSR     LDINT
                JMP     ISUB
OPIMUL:         JSR     LDINT
                JMP     IMULT
OPIDIV:         JSR     LDINT
                JMP     IDIV
OPIMOD:         JSR     LDINT
                JMP     IMOD
OPICMP:         JSR     LDINT
                JMP     ICOMP
OPINTDV:        JSR     LDINT
                JMP     INTDIV
OPDMUL:         JSR     LDDBL          ; the operators by DBLDSP's stubs
                JMP     DMULT
OPDDIV:         JSR     LDDBL
                JMP     DDIV
OPDADD:         JSR     LDDBL
                JMP     DADD
OPDSUB:         JSR     LDDBL
                JMP     DSUB
OPDCMP:         JSR     LDDBL
                JMP     DCOMP
OPCSD:          JSR     LDFAC
                JMP     CSD
OPSGN:          JSR     LDFAC
                JMP     SGN
OPNEG:          JSR     LDFAC
                JMP     VNEG
OPABS:          JSR     LDFAC
                JMP     VABS
OPMUL10:        JSR     LDFAC
                JMP     MUL10
OPDIV10:        JSR     LDFAC
                JMP     DIV10
OPDINT:         JSR     LDFAC
                JMP     DINT
OPFIN:          JSR     LDTXT
                JSR     FIN
                JMP     FINEND
OPFINDB:        JSR     LDTXT
                JSR     FINDBL
                JMP     FINEND
OPLDTXT:        JSR     LDTXT
                JMP     FINEND
; The functions as ISFUN calls them: overflow printed once.
.macro FUNOP routine
                JSR     LDFAC
                LDA     #1
                STA     FLGOVC
                JMP     routine
.endmacro
OPSQR:          FUNOP   SQR
OPEXP:          FUNOP   EXP
OPLOG:          FUNOP   LOG
OPSIN:          FUNOP   SIN
OPCOS:          FUNOP   COS
OPTAN:          FUNOP   TAN
OPATN:          FUNOP   ATN
OPPWR:          JSR     LDAB           ; x in BXDX, y in the FAC
                JMP     FPWR
OPRND:          JSR     LDFAC          ; B is RNDX before; RNDX after to +32
                LDY     #12
                LDX     #0
@in:            LDA     (HREC),Y
                STA     RNDX,X
                INY
                INX
                CPX     #4
                BNE     @in
                LDA     #1
                STA     FLGOVC
                JSR     RND
                LDY     #32
                LDX     #0
@o:             LDA     RNDX,X
                STA     (HREC),Y
                INY
                INX
                CPX     #4
                BNE     @o
                LDA     #0
                RTS
OPFOUTO:        JSR     LDFAC
                JSR     FOUTO
                JMP     TXTOUT
OPFOUTH:        JSR     LDFAC
                JSR     FOUTH
                JMP     TXTOUT
OPFOUTT:        JSR     LDFAC          ; $FOUT without OPFOUT's copy of the text
                JMP     FOUT
OPFOUT:         JSR     LDFAC          ; the text to +32
                JSR     FOUT
                JMP     TXTOUT

.segment "FARFIX"                      ; none in a flat image, but the loader names it

.segment "BSS"

SSTACK:         .res    1024           ; the BASIC stack
SSTOP:

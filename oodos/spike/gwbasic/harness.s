; The spike's test harness (segment HARNESS, at $0300, not measured).
;
; A control block at $7FF0 says what to do:
;
;   $7FF0   0: run the math vectors at $8000; 1: run the program at $6100
;   $7FF2   the number of vectors
;
; A vector is 48 bytes; check.py writes them and reads them back:
;
;   +0      the operation (OPS below)
;   +1      A's VALTYP
;   +4      A, 8 bytes (an integer or single in its first 2 or 4)
;   +12     B, 8 bytes
;   +20     out: VALTYP after
;   +21     out: A after (a compare's result)
;   +22     out: the error number, or 0
;   +24     out: DFACL..FACEXP, 8 bytes
;   +32     out: $FOUT's text, to its zero (15 characters at most)
;
; The program is GW-BASIC's tokenised form at $6101 (link, line number,
; tokens, zero), after a zero at $6100, ending with a zero link.

.include "gwbasic.inc"

.import FADDS, FSUBS, FMULS, FDIVS, FCOMP, FLT, FRCINT, INT, FIXER, IADD
.import ISUB, IMULT, IDIV, IMOD, ICOMP, INTDIV, FOUT, FMULD, DDIVFA, CSD
.import SGN, VNEG, VABS, MUL10, DIV10, NEWSTT, ERRHOOK, FARCALL

CTRL            = $7FF0
VECTORS         = $8000
PROGRAM         = $6100
VLEN            = 48

HREC            = $E0                  ; the vector being run
HCOUNT          = $E2
HRES            = $E4
HSAVES          = $E5                  ; S and SSP, to recover from an error

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
                STZ     FLGOVC
                STZ     ONELIN
                STZ     ONELIN+1
                STZ     TRCFLG
                STZ     ONGSBF
                STZ     ERRFLG
                STZ     PTRFLG
                STZ     FMTAL
                STZ     FMTAH
                STZ     ERRHOOK
                STZ     ERRHOOK+1
                LDA     CTRL
                BNE     BASRUN
                JMP     MATHRUN

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
                LDY     #22
                LDA     #0
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

OPS:            .word   0, OPADD, OPSUB, OPMUL, OPDIV, OPCMP, OPFLT, OPCINT
                .word   OPINT, OPFIX, OPIADD, OPISUB, OPIMUL, OPIDIV, OPIMOD
                .word   OPICMP, OPINTDV, OPFOUT, OPDMUL, OPDDIV, OPCSD, OPSGN
                .word   OPNEG, OPABS, OPMUL10, OPDIV10, OPFAR, OPNEAR, OPFARF

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

OPADD:          JSR     LDAB
                JMP     FADDS
OPSUB:          JSR     LDAB
                JMP     FSUBS
OPMUL:          JSR     LDAB
                JMP     FMULS
OPDIV:          JSR     LDAB
                JMP     FDIVS
OPCMP:          JSR     LDAB           ; A against B: the FAC is A
                LDX     #3
@sw:            LDA     RDL,X
                LDY     FACLO,X
                STA     FACLO,X
                STY     RDL,X
                DEX
                BPL     @sw
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
OPDMUL:         JSR     LDDBL
                JMP     FMULD
OPDDIV:         JSR     LDDBL
                JMP     DDIVFA
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
OPFAR:          JSR     FARCALL        ; FARCALL's cost: against OPNEAR
                .byte   5
                .word   DUMMY
                RTS
OPNEAR:         JSR     DUMMY
                RTS
DUMMY:          RTS

; FARCALL's promises (farcall.s), for check.py: A, X, Y and P reach the
; target as the caller set them and come back as the target left them, the
; target's block is mapped for the call and the caller's after it, a far
; call nests in another, and S ends where it began. Each stage puts A, X, Y,
; P (as PHP pushes it) and the block in W3 in FFREC, 5 bytes a stage, and
; S before and after at FFREC+20; check.py dumps it.
NB_WIN3         = $C683

.macro FFSAVE n                        ; after a PHP: the stage's registers
                STA     FFREC+n
                STX     FFREC+n+1
                STY     FFREC+n+2
                PLA
                STA     FFREC+n+3
                LDA     NB_WIN3
                STA     FFREC+n+4
.endmacro
.macro FFSET pv, av, xv, yv            ; P last: the loads change the flags
                LDA     #pv
                PHA
                LDA     #av
                LDX     #xv
                LDY     #yv
                PLP
.endmacro

OPFARF:         LDA     #$42           ; the caller's block
                STA     NB_WIN3
                TSX
                STX     FFREC+20
                FFSET   $CD, $33, $11, $22     ; N V D I C: FARCALL adds
                JSR     FARCALL
                .byte   5
                .word   FTGT1
                PHP                    ; what the outer target left
                FFSAVE  15
                TSX
                STX     FFREC+21
                LDA     #0
                RTS
FTGT1:          PHP                    ; in block 5: what the caller set
                FFSAVE  0
                FFSET   $41, $44, $55, $66     ; V C
                JSR     FARCALL
                .byte   6
                .word   FTGT2
                PHP                    ; what the inner target left
                FFSAVE  5
                FFSET   $80, $77, $88, $99     ; N
                RTS
FTGT2:          PHP                    ; in block 6: what the outer target set
                FFSAVE  10
                FFSET   $03, $AA, $BB, $CC     ; Z C
                RTS
OPFOUT:         JSR     LDFAC          ; the text to +32
                JSR     FOUT
                LDY     #0
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
                CPY     #15
                BNE     @t
@done:          LDA     #0
                RTS

.segment "BSS"

SSTACK:         .res    1024           ; the BASIC stack
FFREC:          .res    22             ; OPFARF's record
SSTOP:

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; BIPTRG.ASM, translated: the variables, resident (hw/oodos.md, "The plan,
; phase 7"). With MATH1.ASM's search loops and block moves, and BIMISC.ASM's
; REASON, which only this and the string space use so far:
;
;   PTRGET PTRGT1 PTRGT2   a variable's name to its value's address
;   DIM DIMCON             DIM: PTRGET with DIMFLG set, for each name
;   LOPFND LOPFD1          the searches, simple and array (MATH1.ASM)
;   SMKVAR FINZER VARNOT   a simple variable made, or not
;   ISARY ARYEXT GETDEF    an array's subscripts and its element
;   NOTFDD                 an array made, DIM's sizes or 11 each
;   NPUTSB                 the name's extra characters into an entry
;   UMULT BLTU BLTUC       MATH1.ASM's multiply and block move up
;   REASON                 BIMISC.ASM's room check, collecting strings first
;
; A simple variable is its VALTYP, the name's first two characters, the count
; of the characters past them (NAMCNT) and those characters, with bit 7 set,
; then the value: VALTYP bytes (a string's is its descriptor, 3). An array is
; the same name part, then its size past the size field, the number of
; dimensions, the size of each (the last subscript's first) and the values,
; the first subscript varying fastest. Names keep 40 characters.
;
; The 8086 keeps a subscript's state on its stack across the recursive
; FRMEVL; here it goes on the BASIC stack (SPUSH), and so do the subscripts
; until the element is found. SMKVAR asks who called PTRGET, as the 8086
; does, by the return address on top of the 6502's stack: EVAL's RETVAR
; doesn't make a variable it hasn't got (FINZER gives zero), and VARPTR's
; PTRGTN gets 0.
;
; Left for later: DEF FN's parameter table, which the 8086 searches first
; when a function is active (NOFUNS; 7b), ERASE's entry (ERSFIN: SUBFLG 1;
; 7b), and CALL's SARYFL (7b). OPTION BASE is 7b's, but OPTVAL is read here.

.include "gwbasic.inc"

.export PTRGET, PTRGT1, PTRGT2, PTRGTN, PTRGTR, DIM, NOARYS, BSERR, DDERR
.export NPUTSB, UMULT, BLTU, BLTUC, REASON
.export NAMCNT, NAMBUF, DEFTBL, OPTVAL, SUBFLG
.import CHRGTR, SYNCHR, SNERR, ISLET2, GETSTK, OMERR, ERROR, INTIDX
.import SPUSH, SPOP, SPUSHA, SPOPA, RETVAR, DZERO, GETYP, GARBA2, DSEGZ, ERSFIN

NAMLEN          = 40                   ; OEM.H: the longest name
ERRBS           = 9                    ; Subscript out of range
ERRDD           = 10                   ; Duplicate Definition

.code

; --- DIM, PTRGET (BIPTRG.ASM 49) -----------------------------------------------

; DIMCON: back here after each name DIM makes; a comma, and the next.
DIMCON:         DECW    TXTPTR
                JSR     CHRGTR
                BNE     :+
                RTS
:               JSR     SYNCHR
                .byte   ','
DIM:            PUSHRET DIMCON
                LDA     #$80
                BRA     PTRGT1

; PTRGET: the variable named at TXTPTR. Returns PTR at its value (a string's
; descriptor), VALTYP its type, TXTPTR past the name and any subscripts.
PTRGET:         LDA     #0
PTRGT1:         STA     DIMFLG
                LDA     (TXTPTR)
                STA     RCL            ; CL: the first character
                BRA     PTRGTL
PTRGT2:         LDA     (TXTPTR)       ; ISLET, inline: it must be a letter
PTRGTL:         CMP     #'A'
                BCC     @sn
                CMP     #'Z'+1
                BCC     :+
@sn:            JMP     SNERR
:               STZ     RCH            ; no second character yet
                STZ     NAMCNT
                INCW    TXTPTR
                LDA     (TXTPTR)
                CMP     #'.'
                BCC     NOSEC
                BEQ     ISSEC
                CMP     #'9'+1
                BCS     PTRGT3
                CMP     #'0'
                BCS     ISSEC
PTRGT3:         JSR     ISLET2
                BCS     NOSEC
ISSEC:          STA     RCH            ; CH: the second
                LDX     #0             ; the 8086 stores from NAMBUF-1, NAMCNT
VMORCH:         ORA     #$80           ; bit 7 set, so ERASE can scan back
                CPX     #NAMLEN-1      ; (past the buffer, counted and not kept:
                BCS     :+             ; the name is too long anyway)
                STA     NAMCNT,X
:               INX
                INCW    TXTPTR
                LDA     (TXTPTR)
                CMP     #'9'+1
                BCS     VMORC1
                CMP     #'0'
                BCS     VMORCH
VMORC1:         JSR     ISLET2
                BCC     VMORCH
                CMP     #'.'
                BEQ     VMORCH
                DEX                    ; the characters past the second
                CPX     #NAMLEN-1
                BCC     :+
                JMP     SNERR
:               STX     NAMCNT
NOSEC:          CMP     #'%'+1         ; a type character?
                BCS     TABTYP
                LDX     #VT_INT
                CMP     #'%'
                BEQ     HAVTYP
                INX
                CMP     #'$'
                BEQ     HAVTYP
                INX
                CMP     #'!'
                BEQ     HAVTYP
                LDX     #VT_DBL
                CMP     #'#'
                BEQ     HAVTYP
TABTYP:         LDA     RCL            ; no: DEFTBL's type for the letter
                AND     #$7F           ; (bit 7: DEF FN's)
                TAX
                LDA     DEFTBL-'A',X
                TAX
                DECW    TXTPTR
HAVTYP:         STX     VALTYP
                JSR     CHRGTR         ; past the type character
                LDA     SUBFLG
                DEC     A
                BNE     :+
                JMP     ERSFIN         ; 1: ERASE's
:               BPL     NOARYS         ; 2 and up: no arrays
                LDA     (TXTPTR)
                CMP     #'('
                BNE     :+
                JMP     ISARY
:               CMP     #'['
                BNE     NOARYS
                JMP     ISARY

; NOARYS: a simple variable. (DEF FN's parameters, PARM1, are searched first
; when NOFUNS says a function is active: 7b's.)
NOARYS:         STZ     SUBFLG
SNFUNS:         LDA     ARYTAB         ; search to ARYTA2
                STA     ARYTA2
                LDA     ARYTAB+1
                STA     ARYTA2+1
                LDA     VARTAB
                LDX     VARTAB+1

; LOPFND (MATH1.ASM 2857): the simple search, from A/X to ARYTA2.
LOPFND:         STA     VPTR
                STX     VPTR+1
                BRA     NOTIT2
LOPTOP:         LDY     #1             ; LOPTOP (MATH1.ASM 2815)
                LDA     (VPTR),Y       ; the first character?
                CMP     RCL
                BEQ     ISIT
NOTIT1:         LDY     #3             ; no: past it, 4 + NAMCNT + VALTYP
                LDA     (VPTR),Y
                CLC
                ADC     (VPTR)
                ADC     #4
NOTIT0:         CLC
                ADC     VPTR
                STA     VPTR
                BCC     NOTIT2
                INC     VPTR+1
NOTIT2:         LDA     VPTR           ; the end?
                CMP     ARYTA2
                BNE     LOPTOP
                LDA     VPTR+1
                CMP     ARYTA2+1
                BNE     LOPTOP
                BRA     NOTFNS
ISIT:           LDA     (VPTR)         ; the type, the second character and
                CMP     VALTYP         ; the rest must match
                BNE     NOTIT1
                INY
                LDA     (VPTR),Y
                CMP     RCH
                BNE     NOTIT1
                INY
                LDA     (VPTR),Y
                CMP     NAMCNT
                BNE     NOTIT1
                TAX
                BEQ     FNDITV
                LDX     #0
@cmp:           INY                    ; LENMAT
                LDA     (VPTR),Y
                CMP     NAMBUF,X
                BNE     NOTIT1
                INX
                CPX     NAMCNT
                BNE     @cmp
FNDITV:         INY                    ; found: PTR at the value
                TYA
                CLC
                ADC     VPTR
                STA     PTR
                LDA     VPTR+1
                ADC     #0
                STA     PTR+1
                RTS

; NOTFNS, SMKVAR: not found (VPTR at ARYTAB). Made, unless EVAL wants the
; value (FINZER) or VARPTR the address (VARNOT).
NOTFNS:
SMKVAR:         PLA                    ; the caller: the return address, back
                STA     T0
                PLA
                STA     T1
                PHA
                LDA     T0
                PHA
                LDA     T1
                CMP     #>(PTRGTR-1)
                BNE     @nv
                LDA     T0
                CMP     #<(PTRGTR-1)
                BEQ     VARNOT
@nv:            LDA     T1
                CMP     #>(RETVAR-1)
                BNE     MKVAR
                LDA     T0
                CMP     #<(RETVAR-1)
                BNE     MKVAR

; FINZER: the value zero of every type, a string's the null string's, and
; back to EVAL's caller, past RETVAR.
FINZER:         PLA
                PLA
                JSR     DZERO          ; all of the FAC: the 8086's math
                JSR     GETYP          ; doesn't take exponent 0 as zero
                BNE     :+
                LDA     #<DSEGZ
                STA     FACLO
                LDA     #>DSEGZ
                STA     FACLO+1
:               RTS

; PTRGTN: PTRGET that makes no variable, for VARPTR: PTR 0 if there is none.
PTRGTN:         JSR     PTRGET
PTRGTR:         RTS
VARNOT:         STZ     PTR
                STZ     PTR+1
                LDA     #0
                RTS

; MKVAR (SMKVAR's second half): room made at ARYTAB, the arrays moved up.
MKVAR:          LDA     RCH            ; the looks, past BLTU
                PHA
                LDA     RCL
                PHA
                LDA     NAMCNT         ; the entry's size
                CLC
                ADC     VALTYP
                ADC     #4
                PHA
                STA     T2
                LDA     VPTR           ; DX: where it goes
                STA     RDL
                LDA     VPTR+1
                STA     RDH
                LDA     STREND         ; CX: the last byte to move; BX: its place
                STA     RCL
                CLC
                ADC     T2
                STA     RBL
                LDA     STREND+1
                STA     RCH
                ADC     #0
                STA     RBH
                JSR     BLTU
                PLA                    ; STREND up by the size
                TAY
                CLC
                ADC     STREND
                STA     STREND
                BCC     :+
                INC     STREND+1
:               LDA     RCL            ; the arrays start past it
                STA     ARYTAB
                LDA     RCH
                STA     ARYTAB+1
                LDA     #0             ; ZEROER
@z:             DEY
                STA     (RDL),Y
                BNE     @z
                LDA     VALTYP         ; the type and the name
                STA     (RDL)
                PLA
                LDY     #1
                STA     (RDL),Y
                PLA
                INY
                STA     (RDL),Y
                CLC
                LDA     RDL
                ADC     #3
                STA     RBL
                LDA     RDH
                ADC     #0
                STA     RBH
                JSR     NPUTSB
                LDA     RBL            ; PTR at the value
                STA     PTR
                LDA     RBH
                STA     PTR+1
                RTS

; --- Arrays (BIPTRG.ASM 311) --------------------------------------------------

; ISARY: TXTPTR at "(" or "[". Each subscript is evaluated (recursively:
; it may name arrays itself) and left on the BASIC stack, under DIMFLG and
; VALTYP, the last on top.
ISARY:          LDA     DIMFLG
                JSR     SPUSHA
                LDA     VALTYP
                JSR     SPUSHA
                STZ     NDIM           ; no subscripts yet
INDLOP:         LDA     NDIM
                JSR     SPUSHA
                LDX     #RCL           ; the looks
                LDA     #2
                JSR     SPUSH
                LDA     NAMCNT
                BEQ     SHTNAM
                CLC                    ; a long name: its characters too
                ADC     #2
                LSR     A
                JSR     GETSTK
                LDX     #0
@p:             LDA     NAMBUF,X
                JSR     SPUSHA
                INX
                CPX     NAMCNT
                BNE     @p
                LDA     NAMCNT
                JSR     SPUSHA
                JSR     INTIDX         ; DX: the subscript
                JSR     SPOPA
                STA     NAMCNT
                TAX
@q:             JSR     SPOPA
                STA     NAMBUF-1,X
                DEX
                BNE     @q
                BRA     LNGNAM
SHTNAM:         JSR     INTIDX
                STZ     NAMCNT
LNGNAM:         LDA     OPTVAL         ; OPTION BASE 1: from 1
                BEQ     OPTB0
                LDA     RDL
                ORA     RDH
                BNE     :+
                JMP     BSERR
:               DECW    RDL
OPTB0:          LDX     #RCL
                LDA     #2
                JSR     SPOP           ; the looks
                JSR     SPOPA
                STA     NDIM
                JSR     SPOPA          ; VALTYP and DIMFLG, to put the
                STA     T0             ; subscript under them
                JSR     SPOPA
                STA     T1
                LDX     #RDL
                LDA     #2
                JSR     SPUSH
                LDA     T1
                JSR     SPUSHA
                LDA     T0
                JSR     SPUSHA
                INC     NDIM
                LDA     (TXTPTR)
                CMP     #','           ; more?
                BNE     :+
                JMP     INDLOP
:               CMP     #')'
                BEQ     DOCHRT
                CMP     #']'
                BEQ     DOCHRT
                JMP     SNERR
DOCHRT:         JSR     CHRGTR
SUBSOK:         JSR     SPOPA
                STA     VALTYP
                JSR     SPOPA
                STA     DIMFLG

; LOPFD1 (MATH1.ASM 2868): the array search, ARYTAB to STREND.
LOPFD1:         LDA     ARYTAB
                STA     VPTR
                LDA     ARYTAB+1
                STA     VPTR+1
LOPFDI:         LDA     VPTR
                CMP     STREND
                BNE     LOPFD0
                LDA     VPTR+1
                CMP     STREND+1
                BNE     LOPFD0
                JMP     NOTFDD
LOPFD0:         LDY     #1
                LDA     (VPTR),Y
                CMP     RCL
                BEQ     ISARY1
NMARY1:         LDY     #3             ; past it: the name, then the size
                LDA     (VPTR),Y
                CLC
                ADC     #4
                TAY
                LDA     (VPTR),Y
                STA     T0
                INY
                LDA     (VPTR),Y
                STA     T1
                INY
                TYA
                CLC
                ADC     VPTR
                TAX
                LDA     VPTR+1
                ADC     #0
                STA     VPTR+1
                TXA
                CLC
                ADC     T0
                STA     VPTR
                LDA     VPTR+1
                ADC     T1
                STA     VPTR+1
                BRA     LOPFDI
ISARY1:         LDA     (VPTR)         ; MATH1.ASM's ISARY
                CMP     VALTYP
                BNE     NMARY1
                INY
                LDA     (VPTR),Y
                CMP     RCH
                BNE     NMARY1
                INY
                LDA     (VPTR),Y
                CMP     NAMCNT
                BNE     NMARY1
                TAX
                BEQ     CNOMAT
                LDX     #0
@cmp:           INY
                LDA     (VPTR),Y
                CMP     NAMBUF,X
                BNE     NMARY1
                INX
                CPX     NAMCNT
                BNE     @cmp
CNOMAT:         TYA                    ; BX at the number of dimensions
                SEC
                ADC     #2
                CLC
                ADC     VPTR
                STA     RBL
                LDA     VPTR+1
                ADC     #0
                STA     RBH

; ARYEXT: found. DIM can't make it again; the subscripts must be as many.
ARYEXT:         LDA     DIMFLG
                BEQ     :+
DDERR:          LDA     #ERRDD         ; GWMAIN.ASM 435
                JMP     ERROR
:               LDA     NDIM
                CMP     (RBL)
                BEQ     GETDEF
BSERR:          LDA     #ERRBS
                JMP     ERROR

; GETDEF: the element. BX at the number of dimensions, the subscripts on the
; BASIC stack. CX (CURTOL) = CX * the dimension's size + the subscript, for
; each, then times VALTYP from the values' start.
GETDEF:         STZ     RCL
                STZ     RCH
                LDA     (RBL)
                STA     NDIM
                INCW    RBL
INLPNM:         LDA     (RBL)          ; DX: this dimension's size
                STA     RDL
                LDY     #1
                LDA     (RBL),Y
                STA     RDH
                CLC
                LDA     RBL
                ADC     #2
                STA     RBL
                BCC     :+
                INC     RBH
:               LDX     #RAL           ; AX: its subscript
                LDA     #2
                JSR     SPOP
                LDA     RAL            ; below the size?
                CMP     RDL
                LDA     RAH
                SBC     RDH
                BCC     :+
                JMP     BSERR
:               JSR     UMULT          ; DX = CURTOL * the size
                CLC
                LDA     RAL
                ADC     RDL
                STA     RCL
                LDA     RAH
                ADC     RDH
                STA     RCH
                DEC     NDIM
                BNE     INLPNM
                LDA     RCL            ; times the value's size
                STA     RAL
                LDA     RCH
                STA     RAH
                ASL     RAL
                ROL     RAH
                LDA     VALTYP
                CMP     #VT_STR
                BEQ     @x3
                BCC     DONMUL
                ASL     RAL
                ROL     RAH
                CMP     #VT_SNG
                BEQ     DONMUL
                ASL     RAL
                ROL     RAH
                BRA     DONMUL
@x3:            CLC
                LDA     RAL
                ADC     RCL
                STA     RAL
                LDA     RAH
                ADC     RCH
                STA     RAH
DONMUL:         CLC                    ; from the values' start
                LDA     RBL
                ADC     RAL
                STA     PTR
                LDA     RBH
                ADC     RAH
                STA     PTR+1
FINNOW:         RTS

; NOTFDD: not found; made at STREND (VPTR). DIM's subscripts plus one are
; its sizes, or 11 each (10 with OPTION BASE 1); then as found, unless DIM.
NOTFDD:         LDA     VPTR
                STA     RBL
                LDA     VPTR+1
                STA     RBH
                LDA     VALTYP
                STA     (RBL)
                STA     RDL            ; DX: one value's size
                STZ     RDH
                LDY     #1             ; the name
                LDA     RCL
                STA     (RBL),Y
                INY
                LDA     RCH
                STA     (RBL),Y
                CLC
                LDA     RBL
                ADC     #3
                STA     RBL
                BCC     :+
                INC     RBH
:               JSR     NPUTSB
                LDA     NDIM
                JSR     GETSTK
                CLC                    ; past the size, filled in at the end
                LDA     RBL
                ADC     #2
                STA     RBL
                STA     ATEMP3
                LDA     RBH
                ADC     #0
                STA     RBH
                STA     ATEMP3+1
                LDA     NDIM
                STA     (RBL)
                STA     NCNT
                INCW    RBL
LOPPTA:         BIT     DIMFLG
                BMI     POPDIM
                LDA     OPTVAL         ; 11, or 10 for OPTION BASE 1
                EOR     #11
                STA     RCL
                STZ     RCH
                BRA     NOTDIM
POPDIM:         LDX     #RCL           ; DIM's subscript, plus one
                LDA     #2
                JSR     SPOP
                INCW    RCL
NOTDIM:         LDA     RCL
                STA     (RBL)
                LDY     #1
                LDA     RCH
                STA     (RBL),Y
                CLC
                LDA     RBL
                ADC     #2
                STA     RBL
                BCC     :+
                INC     RBH
:               JSR     UMULT          ; DX = the size * DX
                DEC     NCNT
                BNE     LOPPTA
                LDA     RDL            ; CX: the values' size; DX: their start;
                STA     RCL            ; BX: their end
                LDA     RDH
                STA     RCH
                LDA     RBL
                STA     RDL
                LDA     RBH
                STA     RDH
                CLC
                LDA     RBL
                ADC     RCL
                STA     RBL
                LDA     RBH
                ADC     RCH
                STA     RBH
                BCC     :+
                JMP     OMERR
:               JSR     REASON
                LDA     RBL
                STA     STREND
                LDA     RBH
                STA     STREND+1
                LDA     RDL            ; ZERITA: zero CX bytes from DX
                STA     RSI
                LDA     RDH
                STA     RSI+1
                LDX     RCH
                LDA     #0
                TAY
                CPX     #0
                BEQ     @part
@page:          STA     (RSI),Y
                INY
                BNE     @page
                INC     RSI+1
                DEX
                BNE     @page
@part:          CPY     RCL
                BEQ     @zd
                STA     (RSI),Y
                INY
                BRA     @part
@zd:            INCW    RCL            ; the size: and the count byte and
                LDA     (ATEMP3)       ; the dimensions' sizes
                ASL     A
                TAX
                LDA     #0
                ROL     A
                TAY
                TXA
                CLC
                ADC     RCL
                STA     RDL
                TYA
                ADC     RCH
                STA     RDH
                SEC
                LDA     ATEMP3
                SBC     #2
                STA     RBL
                LDA     ATEMP3+1
                SBC     #0
                STA     RBH
                LDA     RDL
                STA     (RBL)
                LDY     #1
                LDA     RDH
                STA     (RBL),Y
                LDA     ATEMP3         ; BX at the number of dimensions
                STA     RBL
                LDA     ATEMP3+1
                STA     RBH
                BIT     DIMFLG
                BPL     :+
                RTS                    ; FINNOW: DIM's
:               JMP     GETDEF

; NPUTSB: NAMCNT and the extra characters to (RBL); RBL left past them.
NPUTSB:         LDX     NAMCNT
                LDY     #0
@m:             LDA     NAMCNT,Y
                STA     (RBL),Y
                INY
                DEX
                BPL     @m
                TYA
                CLC
                ADC     RBL
                STA     RBL
                BCC     :+
                INC     RBH
:               RTS

; --- MATH1.ASM's helpers --------------------------------------------------------

; UMULT (MATH1.ASM 2594): DX = CX * DX, unsigned; past 16 bits, Subscript
; out of range.
UMULT:          LDA     RDL
                STA     T0
                LDA     RDH
                STA     T1
                STZ     T2
                STZ     T3
                LDX     #16
@l:             ASL     T2
                ROL     T3
                BCS     @ov
                ASL     T0
                ROL     T1
                BCC     @n
                CLC
                LDA     T2
                ADC     RCL
                STA     T2
                LDA     T3
                ADC     RCH
                STA     T3
                BCS     @ov
@n:             DEX
                BNE     @l
                LDA     T2
                STA     RDL
                LDA     T3
                STA     RDH
                RTS
@ov:            JMP     BSERR

; BLTU (MATH1.ASM 2640): move DX..CX up so CX's byte lands at BX, once
; REASON has made sure BX is below the strings. BLTUC moves without asking.
; Returns BX = DX, the low address, and CX where it went.
BLTU:           JSR     REASON
BLTUC:          SEC                    ; T1:T0 = CX - DX + 1 bytes
                LDA     RCL
                SBC     RDL
                STA     T0
                LDA     RCH
                SBC     RDH
                STA     T1
                INCW    T0
                LDA     RCL            ; SI, DI: down from the top
                STA     RSI
                LDA     RCH
                STA     RSI+1
                LDA     RBL
                STA     RDI
                LDA     RBH
                STA     RDI+1
@m:             LDA     (RSI)
                STA     (RDI)
                DECW    RSI
                DECW    RDI
                DECW    T0
                LDA     T0
                ORA     T1
                BNE     @m
                CLC                    ; CX: where the low byte went
                LDA     RDI
                ADC     #1
                STA     RCL
                LDA     RDI+1
                ADC     #0
                STA     RCH
                LDA     RDL
                STA     RBL
                LDA     RDH
                STA     RBH
                RTS

; REASON (BIMISC.ASM 143): BX must not pass FRETOP. If it does, the strings
; are collected (BX, CX and DX kept: $00-$07, RDL to RCH, as the 8086 pushes
; CX, DX and BX; GARBA2 writes DX, and MKVAR and NOTFDD use it after) and it
; is asked again; then Out of memory. (CHAIN's CHNFLG, which forbids the
; collection, is 7c's.)
REASON:         LDA     FRETOP
                CMP     RBL
                LDA     FRETOP+1
                SBC     RBH
                BCS     @ok
                LDX     #7
@s:             LDA     RDL,X
                PHA
                DEX
                BPL     @s
                JSR     GARBA2
                LDX     #0
@r:             PLA
                STA     RDL,X
                INX
                CPX     #8
                BNE     @r
                LDA     FRETOP
                CMP     RBL
                LDA     FRETOP+1
                SBC     RBH
                BCS     @ok
                JMP     OMERR
@ok:            RTS

.segment "RAM"

; GWDATA.ASM's
NAMCNT:         .res    1              ; the characters past the second
NAMBUF:         .res    NAMLEN-2       ; and they
DEFTBL:         .res    26             ; each letter's VALTYP (DEFINT and the rest)
OPTVAL:         .res    1              ; OPTION BASE
SUBFLG:         .res    1              ; 0: arrays allowed; 1 ERASE's; more: none

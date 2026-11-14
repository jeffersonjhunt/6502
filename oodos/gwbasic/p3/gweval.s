; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWEVAL.ASM's user functions, in block P3 (hw/oodos.md, "The plan, phase
; 7"), each reached by its resident stub (res/far3.s), with GIODSK.ASM's
; PRODIR (a no-op until 7c):
;
;   DEFP      DEF FN (DEFNC3, SCNLIS), DEF USR (DEFUSR); DEF SEG is 7c's
;   FNDOERP   FNname[(arguments)]: the arguments bound to the parameters
;             (ASGMOR, POPASG, FINASG), the body evaluated (FINVLS), the
;             parameters put back (NOCPRS) and the result made the
;             function's type (DOCNVF)
;   USRFNP    USR[n](argument): a 6502 routine in the TPA (SCNUSR)
;   ERRDIR    Illegal direct in direct mode; GETFNM: FN's name to PTRGET
;
; A function is a simple variable whose name's first character has bit 7 set
; (GETFNM), of the function's type; its value is the text pointer just past
; its name in the DEF (0: never defined). Its parameters, while its body is
; evaluated, are PARM1: simple variables of their own, which the resident
; PTRGET searches first (NOFUNS) and the collector walks (PRMSTK's chain).
; The PARM1 of the function that called it, if any, is kept meanwhile in a
; block on the BASIC stack: the 8086 keeps these on its own stack, and so
; does it the arguments' values while they are evaluated (the BASIC stack
; here too: an argument may call a function itself). PARM2 is where the
; next PARM1 is built, the arguments bound into it last first.
;
; The far call rule (translate-7b-common.md): FN ends with DOCNVF's JMP, a
; tail call to a resident routine that returns by RTS (FRCTBL's); DEF FN's
; JMP DATA is one too; DEF SEG's JMP ADVERR and the errors reset both
; stacks. USR calls its routine and returns. None goes into the statement
; loop.

.include "gwbasic.inc"

;@entry DEFP
;@entry FNDOERP
;@entry USRFNP
.export DEFP, FNDOERP, USRFNP
.import CHRGTR, SYNCHR, PTRGET, PTRGT2, SUBFLG, SPUSH, SPOP, SPUSHA, SPOPA
.import FRMEVL, FRMEQL, PARCHK, DOCNVF, GETSTK, VMOVE, VMOVFM, LETCN4, GETYPR
.import STRCPY, PUTTMP, FREFAC, DSCTMP, PRMSTK, PRMLEN, PARM1, PRMLN2, FUNACT
.import UFERR, FCERR, SNERR, ERROR, DATA, ADVERR, FRMQNT

ERRID           = 12                   ; Illegal direct

.code

; --- DEF (GWEVAL.ASM 1209) ---------------------------------------------------------------

; DEF: A the character after DEF, as CHRGTR left it. DEF USR, DEF FN, or
; else it must be DEF SEG (the letters S, E, G: not a reserved word), which
; is 7c's (DEFNC2: SAVSEG, for PEEK, POKE, CALL, BLOAD and BSAVE): Advanced
; Feature until then, so that 7b's USR is always segment 0.
DEFP:           CMP     #USR_TK        ; DEF USR?
                BEQ     DEFUSR
                CMP     #FN_TK         ; DEF FN?
                BEQ     DEFNC3
                JSR     SYNCHR         ; else SEG
                .byte   'S'
                JSR     SYNCHR
                .byte   'E'
                JSR     SYNCHR
                .byte   'G'
                JMP     ADVERR         ; DEFNC2: 7c's

; DEFNC3 (GWEVAL.ASM 1227): the function's variable (made if it is new) gets
; the text pointer past its name; Illegal direct in direct mode (after the
; variable is made, as the 8086's order has it). Then the parameter list, if
; any, is scanned, each parameter's variable made (SCNLIS), and the rest of
; the statement, the body, skipped by DATA (a tail call).
DEFNC3:         JSR     GETFNM         ; PTR: the function's value
                JSR     ERRDIR
                LDA     TXTPTR         ; the text pointer, its value
                STA     (PTR)
                LDY     #1
                LDA     TXTPTR+1
                STA     (PTR),Y
                LDA     (TXTPTR)       ; arguments?
                CMP     #'('
                BEQ     :+
                JMP     DATA           ; no
:               JSR     CHRGTR
; SCNLIS (GWEVAL.ASM 1241)
SCNLIS:         JSR     PTRGET         ; the parameter's variable, made
                LDA     (TXTPTR)       ; the list's end?
                CMP     #')'
                BNE     :+
                JMP     DATA           ; yes
:               JSR     SYNCHR         ; "," then the next
                .byte   ','
                BRA     SCNLIS

; --- DEF USR, USR (GWEVAL.ASM 1125) -----------------------------------------------------

; DEFUSR (GWEVAL.ASM 1176): DEF USR[n] = address, into USRTAB's entry n.
; The entry's address is kept on the BASIC stack across the formula, as the
; 8086 keeps DX on its own.
DEFUSR:         JSR     SCNUSR         ; DX: USRTAB's entry
                LDX     #RDL
                LDA     #2
                JSR     SPUSH
                JSR     SYNCHR         ; "="
                .byte   EQULTK
                JSR     FRMQNT         ; DX: the address, 0..65535
                LDX     #PTR2          ; the entry
                LDA     #2
                JSR     SPOP
                LDA     RDL
                STA     (PTR2)
                LDY     #1
                LDA     RDH
                STA     (PTR2),Y
                RTS

; SCNUSR (GWEVAL.ASM 1159): USR's number, a digit (CRUNCH makes it a one-byte
; constant, ONECON to ONECON+9: CHRGTR leaves its value in CONLO); none is
; USR0. DX returns at its entry in USRTAB, TXTPTR past it.
SCNUSR:         JSR     CHRGTR
                STZ     RCL            ; CX: USR0's
                STZ     RCH
                CMP     #ONECON+10     ; a one-byte constant 0-9?
                BCS     NOARGU
                CMP     #ONECON
                BCC     NOARGU
; USERCN (GWEVAL.ASM 1166)
USERCN:         JSR     CHRGTR         ; past it
                LDA     CONLO          ; its value, times 2
                ASL     A
                STA     RCL
; NOARGU (GWEVAL.ASM 1171)
NOARGU:         CLC
                LDA     #<USRTAB
                ADC     RCL
                STA     RDL
                LDA     #>USRTAB
                ADC     RCH
                STA     RDH
                RTS

; USRFNP: USR[n](argument), from EVAL, TXTPTR at USR's token. PRODIR (a
; protected program in direct mode: Illegal function call) is 7c's, with
; protected programs: none can be loaded before it. Then the argument, and
; the routine USR n was DEFined at: $FFFF, never DEFined, is Illegal function
; call. A string argument is freed first (FREFAC), as the 8086 does.
;
; The port's convention (the 8086's, in the 6502's terms; segment 0 always,
; the TPA, until DEF SEG in 7c), for the owner to confirm: the routine is
; called by JSR, with
;
;   A         VALTYP: 2 integer, 3 string, 4 single, 8 double (the 8086's AL)
;   X         #FACLO: the FAC's zero page address (the 8086's BX = FACLO; a
;             number in zero page is named by X, never by a pointer)
;   RDL/RDH   for a string, its descriptor's address (the 8086's DX): the
;             length, then the data's address
;
; and returns by RTS with its result in the FAC (FACLO on, or DFACL for a
; double) and VALTYP. The text pointer is kept on the 6502's stack meanwhile,
; as the 8086 keeps BX on its own (POPHRT), so a routine may use any of the
; zero page but GW-BASIC's stacks' pointers (SSP) and the FAC's neighbours it
; returns in.
USRFNP:         JSR     SCNUSR         ; DX: USRTAB's entry
                LDX     #RDL
                LDA     #2
                JSR     SPUSH
                JSR     PARCHK         ; "(" formula ")"
                LDX     #PTR2          ; the entry
                LDA     #2
                JSR     SPOP
                LDA     (PTR2)         ; DX: the routine
                STA     RDL
                LDY     #1
                LDA     (PTR2),Y
                STA     RDH
                AND     RDL            ; $FFFF: never DEFined
                CMP     #$FF
                BNE     :+
                JMP     FCERR
:               LDA     TXTPTR+1       ; the text pointer, kept
                PHA
                LDA     TXTPTR
                PHA
                JSR     USRCAL         ; the routine
                PLA                    ; POPHRT: the text pointer back
                STA     TXTPTR
                PLA
                STA     TXTPTR+1
                RTS
; USRCAL: the routine's address (less one) pushed for RTS to go to, then
; the registers as the convention says.
USRCAL:         LDA     RDL            ; DX - 1
                BNE     :+
                DEC     RDH
:               DEC     RDL
                LDA     RDH
                PHA
                LDA     RDL
                PHA
                LDA     VALTYP         ; a string: freed (BX at its descriptor)
                CMP     #VT_STR
                BNE     :+
                JSR     FREFAC
                LDA     RBL            ; DX: the descriptor
                STA     RDL
                LDA     RBH
                STA     RDH
:               LDA     VALTYP         ; AL: VALTYP
                LDX     #FACLO         ; BX: the FAC
                RTS                    ; to the routine

; --- FN (GWEVAL.ASM 1251) ----------------------------------------------------------------

; FNDOERP: from EVAL, TXTPTR at FN's token. The function's type goes on the
; BASIC stack first (the result is made that type at the end); then its
; definition's text: 0 is Undefined user function. With no "(" after the
; name in the DEF, no arguments are taken (FINVLS). Else the call must have
; "(" too, and each argument is evaluated, in order, and made its
; parameter's type (DOCNVF: a string for a number, or the reverse, is Type
; mismatch), and goes on the BASIC stack with the parameter's variable (as
; PTRGET finds it now, in the variables or an outer function's PARM1) and
; its type, above a zero; the two lists must end together (else Syntax
; error).
;
; TEMP2 keeps the call's text pointer and TEMP3 the DEF's, between the
; formulas, which use both themselves (as the 8086's FRMEVL does); across a
; formula, the DEF's goes on the BASIC stack.
FNDOERP:        JSR     GETFNM         ; PTR: the function's value, VALTYP its type
                LDA     VALTYP         ; PUSH AX: the function's type
                JSR     SPUSHA
                LDA     TXTPTR         ; TEMP2: the call, past the name
                STA     TEMP2
                LDA     TXTPTR+1
                STA     TEMP2+1
                LDA     (PTR)          ; BX: the definition's text
                STA     TXTPTR
                LDY     #1
                LDA     (PTR),Y
                STA     TXTPTR+1
                ORA     TXTPTR         ; zero: never defined
                BNE     :+
                JMP     UFERR
:               LDA     (TXTPTR)       ; parameters?
                CMP     #'('
                BEQ     :+
                JMP     FINVLS         ; no
:               JSR     CHRGTR         ; past the "("
                LDA     TXTPTR         ; TEMP3: the parameters
                STA     TEMP3
                LDA     TXTPTR+1
                STA     TEMP3+1
                LDA     TEMP2          ; the call's arguments
                STA     TXTPTR
                LDA     TEMP2+1
                STA     TXTPTR+1
                JSR     SYNCHR         ; must start with "("
                .byte   '('
                LDA     #0             ; the end of the values to bind
                JSR     SPUSHA
                LDX     #TXTPTR        ; the call's text pointer
                LDA     #2
                JSR     SPUSH
                LDA     TEMP3          ; to the parameters
                STA     TXTPTR
                LDA     TEMP3+1
                STA     TXTPTR+1
; ASGMOR (GWEVAL.ASM 1301): the next parameter, then its argument.
ASGMOR:         LDA     #128           ; no arrays
                STA     SUBFLG
                JSR     PTRGET         ; PTR: the parameter's variable
                LDA     TXTPTR         ; DX: the parameter list
                STA     RDL
                LDA     TXTPTR+1
                STA     RDH
                LDA     (SSP)          ; XTHL: the call's text pointer off the
                STA     TXTPTR         ; BASIC stack, the variable onto it
                LDY     #1
                LDA     (SSP),Y
                STA     TXTPTR+1
                LDA     PTR+1
                STA     (SSP),Y
                LDA     PTR
                STA     (SSP)
                LDA     VALTYP         ; its type, for the coercion
                JSR     SPUSHA
                LDX     #RDL           ; the parameter list
                LDA     #2
                JSR     SPUSH
                JSR     FRMEVL         ; the argument
                LDA     TXTPTR         ; TEMP2: the argument list
                STA     TEMP2
                LDA     TXTPTR+1
                STA     TEMP2+1
                LDX     #TEMP3         ; TEMP3: the parameter list
                LDA     #2
                JSR     SPOP
                JSR     SPOPA          ; the parameter's type
                JSR     DOCNVF         ; the argument made it
                LDA     #4             ; room for the value: eight bytes
                JSR     GETSTK
                SEC
                LDA     SSP
                SBC     #8
                STA     SSP
                STA     PTR
                LDA     SSP+1
                SBC     #0
                STA     SSP+1
                STA     PTR+1
                JSR     VMOVE          ; VMOVMF: the value into them
                LDA     VALTYP         ; its type, for the assignment
                JSR     SPUSHA
                LDA     TEMP2          ; after the argument:
                STA     TXTPTR
                LDA     TEMP2+1
                STA     TXTPTR+1
                LDA     (TXTPTR)       ; the list's end?
                CMP     #')'
                BEQ     POPASG         ; (the parameters' must end too)
                JSR     SYNCHR         ; past the arguments' comma
                .byte   ','
                LDX     #TXTPTR        ; the argument list kept
                LDA     #2
                JSR     SPUSH
                LDA     TEMP3          ; and the parameters' comma
                STA     TXTPTR
                LDA     TEMP3+1
                STA     TXTPTR+1
                JSR     SYNCHR
                .byte   ','
                JMP     ASGMOR         ; the next

; POPAS2 (GWEVAL.ASM 1349): a value bound, PRMLN2 counts it.
POPAS2:         JSR     SPOPA          ; the new size
                STA     PRMLN2
; POPASG (GWEVAL.ASM 1351): each value off the BASIC stack, the last first,
; into PARM2: its parameter's name (from the variable PTRGET found, back over
; its extra characters, bit 7 set, to the count and the type: 4 bytes and
; them), then the value, by LET's own assignment (LETCN4: a string copied
; if it must be). More than PRMSIZ bytes in all is Illegal function call.
POPASG:         JSR     SPOPA          ; the value's type
                BEQ     FINASG         ; zero: none left
                STA     VALTYP
                LDA     SSP            ; VMOVFM: the value into the FAC
                STA     PTR
                LDA     SSP+1
                STA     PTR+1
                JSR     VMOVFM
                CLC                    ; and its eight bytes off
                LDA     SSP
                ADC     #8
                STA     SSP
                BCC     :+
                INC     SSP+1
:               LDX     #RDL           ; DX: where it is to go
                LDA     #2
                JSR     SPOP
                LDA     #3             ; BL: the name's size
                STA     RBL
; LPSIZL (GWEVAL.ASM 1363)
LPSIZL:         INC     RBL
                DECW    RDL            ; the character before
                LDA     (RDL)
                BMI     LPSIZL         ; bit 7: still the name
                SEC                    ; back over the looks and the type
                LDA     RDL
                SBC     #3
                STA     RDL
                BCS     :+
                DEC     RDH
:               LDA     VALTYP         ; CH: the entry's size
                CLC
                ADC     RBL
                STA     RCH
                LDA     PRMLN2         ; CL: PARM2's
                STA     RCL
                CLC
                ADC     RCH            ; the new size
                CMP     #PRMSIZ        ; it mustn't reach PRMSIZ
                BCC     :+
                JMP     FCERR
:               JSR     SPUSHA         ; the new size, kept
                LDX     RCL            ; BCTRAN: the name to PARM2's end
                LDY     #0
@n:             LDA     (RDL),Y
                STA     PARM2,X
                INX
                INY
                CPY     RBL
                BNE     @n
                TXA                    ; PTR: the value's place, after it
                CLC
                ADC     #<PARM2
                STA     PTR
                LDA     #>PARM2
                ADC     #0
                STA     PTR+1
                JSR     LETCN4         ; the assignment
                BRA     POPAS2

; FINASG (GWEVAL.ASM 1394): past the call's ")"; the parameters must end
; there too.
FINASG:         LDA     TEMP2          ; the argument list
                STA     TXTPTR
                LDA     TEMP2+1
                STA     TXTPTR+1
                JSR     CHRGTR         ; past its ")"
                LDX     #TXTPTR        ; the caller's text pointer
                LDA     #2
                JSR     SPUSH
                LDA     TEMP3          ; the parameter list
                STA     TXTPTR
                LDA     TEMP3+1
                STA     TXTPTR+1
                JSR     SYNCHR         ; ended at the same time
                .byte   ')'
                BRA     FINVL1         ; (the 8086 skips FINVLS's PUSH)

; FINVLS (GWEVAL.ASM 1401): the caller's text pointer on the BASIC stack (from
; FNDOER: it has no parameters). Then PARM1 (with PRMSTK and PRMLEN) goes on
; the BASIC stack as a block, PRMSTK at it (the collector's chain), and
; PARM2 becomes PARM1: NOFUNS on, PTRGET searches it first. The body is
; evaluated (FRMEQL: it may call functions itself) and must end the DEF's
; statement: else Syntax error, in the caller's line (as the 8086 says: a
; cheat). A string result that is a parameter's (at DSCTMP or above: not a
; temporary) is copied first, as PARM1 is about to be put back over it.
FINVLS:         LDX     #TEMP2         ; PUSH DX: the caller's text pointer
                LDA     #2
                JSR     SPUSH
FINVL1:         LDA     TXTPTR         ; TEMP3: the definition's text
                STA     TEMP3
                LDA     TXTPTR+1
                STA     TEMP3+1
                LDA     PRMLEN         ; PRMSTK, PRMLEN and PARM1: the bytes
                CLC
                ADC     #4
                STA     RCL
                LSR     A              ; OO-1: as words, rounded down (the 8086's
                JSR     GETSTK         ; ROR asks for 128 more for an odd count)
                SEC                    ; the block's room
                LDA     SSP
                SBC     RCL
                STA     SSP
                BCS     :+
                DEC     SSP+1
:               LDY     #0             ; BCTRAN from PRMSTK
@b:             LDA     PRMSTK,Y
                STA     (SSP),Y
                INY
                CPY     RCL
                BNE     @b
                LDA     SSP            ; PRMSTK: the block, linked for the collector
                STA     PRMSTK
                LDA     SSP+1
                STA     PRMSTK+1
                LDA     PRMLN2         ; PARM2 into PARM1
                STA     PRMLEN
                LDA     PRMLN2+1
                STA     PRMLEN+1
                LDY     #0
                BRA     @p1
@p:             LDA     PARM2,Y
                STA     PARM1,Y
                INY
@p1:            CPY     PRMLEN         ; (less than PRMSIZ)
                BNE     @p
                STZ     PRMLN2         ; PARM2 empty
                STZ     PRMLN2+1
                INC     FUNACT         ; one more function active
                BNE     :+
                INC     FUNACT+1
:               LDA     FUNACT+1
                ORA     FUNACT
                STA     NOFUNS
                LDA     TEMP3          ; the definition's "="
                STA     TXTPTR
                LDA     TEMP3+1
                STA     TXTPTR+1
                JSR     FRMEQL         ; the body: recursion may happen here
                DECW    TXTPTR         ; the statement must end
                JSR     CHRGTR
                BEQ     :+
                JMP     SNERR
:               JSR     GETYPR         ; a string?
                BNE     NOCPRS
                LDA     FACLO          ; a temporary (below DSCTMP)?
                CMP     #<DSCTMP
                LDA     FACLO+1
                SBC     #>DSCTMP
                BCC     NOCPRS         ; yes: no copy
                LDA     FACLO          ; STRCPY: a copy, DX at DSCTMP
                STA     RBL
                LDA     FACLO+1
                STA     RBH
                JSR     STRCPY
                JSR     PUTTMP         ; in a temporary, the FAC at it

; NOCPRS (GWEVAL.ASM 1444): PARM1 (with PRMSTK and PRMLEN) put back from its
; block, the block off the BASIC stack; one function fewer. Then the
; caller's text pointer, and the result made the function's type: DOCNVF, a
; tail call (its FRCTBL routine's RTS returns through FARCALL to the stub).
NOCPRS:         LDA     PRMSTK         ; DX: the block
                STA     RDL
                LDA     PRMSTK+1
                STA     RDH
                LDY     #2             ; CX: its PRMLEN, and 4 (less than PRMSIZ:
                LDA     (RDL),Y        ; a byte)
                CLC
                ADC     #4
                STA     RCL
                LDY     #0             ; BCTRAN to PRMSTK
@r:             LDA     (RDL),Y
                STA     PRMSTK,Y
                INY
                CPY     RCL
                BNE     @r
                CLC                    ; the BASIC stack past it
                LDA     RDL
                ADC     RCL
                STA     SSP
                LDA     RDH
                ADC     #0
                STA     SSP+1
                LDA     FUNACT         ; one function fewer
                BNE     :+
                DEC     FUNACT+1
:               DEC     FUNACT
                LDA     FUNACT+1
                ORA     FUNACT
                STA     NOFUNS
                STZ     PRMFLG         ; OO-1: kept 0 with NOFUNS (res/biptrg.s NOARYS)
                LDX     #TXTPTR        ; the caller's text pointer
                LDA     #2
                JSR     SPOP
                JSR     SPOPA          ; the function's type
                JMP     DOCNVF         ; GWEVAL.ASM 1469 DOCNVF: resident

; --- ERRDIR, GETFNM (GWEVAL.ASM 1498) ---------------------------------------------------

; ERRDIR: Illegal direct if CURLIN is 65535 (a direct line).
ERRDIR:         LDA     CURLIN
                AND     CURLIN+1
                CMP     #$FF
                BEQ     :+
                RTS
:               LDA     #ERRID
                JMP     ERROR

; GETFNM (GWEVAL.ASM 1511): FN, then the name, as PTRGET's with bit 7 set in
; its first character (CL) and no array; PTRGET makes it if it is new. PTR
; returns at its value, VALTYP its type.
GETFNM:         JSR     SYNCHR
                .byte   FN_TK
                LDA     #128           ; no array: "(" isn't a subscript
                STA     SUBFLG
                ORA     (TXTPTR)       ; the function's bit
                STA     RCL
                JMP     PTRGT2

.data

; USRTAB (GWDATA.ASM 867): USR0 to USR9's addresses, $FFFF until DEFined
; (start-up's; CLEAR and NEW leave them).
USRTAB:         .word   $FFFF, $FFFF, $FFFF, $FFFF, $FFFF, $FFFF, $FFFF, $FFFF, $FFFF, $FFFF

.segment "BRAM"

; PARM2 (GWDATA.ASM 1329): the next PARM1, as FN binds it. Only FN uses it,
; so it is P3's; PRMLN2, its count, is resident, as STKERR resets it.
PARM2:          .res    PRMSIZ

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GWSTS.ASM's VARPTR$, in block P3 (hw/oodos.md, "The plan, phase 7"),
; reached from the resident VARPTR (res/gweval.s) by its stub (res/far3.s):
;
;   VARPT2P   VARPTR$(v): a 3-byte string, v's type and the address of its
;             value (low, high), as VARPTR gives it
;
; It returns by PUTNEW, a tail call into resident code that returns to EVAL's
; caller (the far call rule).
;
; And step e's, GWSTS.ASM's general event trapping code (955-1275), with
; GWRAM.ASM's TRPTBL:
;
;   SEVSTT SET1EV   event-id ON, OFF or STOP, for a class's statement, and
;   SEVALL EVSET    for one event, for a class's every event (0), and the
;                   change itself (the statements, KEYSTT, COMS, STRIGS,
;                   PENS, are 7c's and 7f's: entries)
;   EVADR           an event id to its flag's address in TRPTBL
;   ONGOTP GETSUB   ON's test for ON KEY(n), ON COM(n), ON PEN, ON STRIG(n)
;   SETGSB          ON event GOSUB's line for an event (p3/gwmain.s's ONGSB0)
;   EVTRP EVCLR     a source's event signalled, reset, or tested and reset
;   TSTCEV          (the sources are 7c's to 7f's: entries)
;   EOSCHK EOSCH1   the statement must end here
;
; Each returns by RTS or ends in an error. The events are numbered from 0 in
; TRPTBL's order: PENOFF, KEYOFF, COMOFF, STROFF (gwbasic.inc).

.include "gwbasic.inc"

;@entry VARPT2P
;@entry SEVSTT
;@entry SET1EV
;@entry EVTRP
;@entry EVCLR
;@entry TSTCEV
.export VARPT2P
.export SEVSTT, SET1EV, SEVALL, EVSET, EVADR, ONGOTP, SETGSB, EVTRP, EVCLR, TSTCEV
.export EOSCHK, EOSCH1, TRPTBL
.import CHRGTR, SYNCHR, PTRGTN, STRINI, PUTNEW, FCERR, SNERR, GETBYT
.import ONTRP, OFFTRP, STPTRP, REQTRP, FRETRP

.code

; --- VARPT2 (GWSTS.ASM 512) --------------------------------------------------------------

; TXTPTR at the "$": "(", the variable (PTRGTN: PTR 0 if there is none, an
; Illegal function call), ")". The type and the address go on the 6502's
; stack round STRINI (which may collect), as on the 8086's.
VARPT2P:        JSR     CHRGTR         ; past the "$"
                JSR     SYNCHR
                .byte   '('
                JSR     PTRGTN
                JSR     SYNCHR
                .byte   ')'
                LDA     PTR            ; undefined: Illegal function call
                ORA     PTR+1
                BNE     VARRT2
                JMP     FCERR
; VARRT2 (GWSTS.ASM 528)
VARRT2:         LDA     PTR+1          ; the address
                PHA
                LDA     PTR
                PHA
                LDA     VALTYP         ; the type
                PHA
                LDA     #3
                JSR     STRINI         ; DX: 3 bytes (DSCTMP's address)
                PLA
                STA     (RDL)          ; byte 0: the type
                LDY     #1
                PLA
                STA     (RDL),Y        ; bytes 1 and 2: the address
                INY
                PLA
                STA     (RDL),Y
                JMP     PUTNEW         ; the FAC at it

; --- General event trapping (GWSTS.ASM 955) ---------------------------------------------

; SEVSTT (GWSTS.ASM 1023): "(" event-id ")" and ON, OFF or STOP, then the
; statement's end (EOSCH1), for a class's statement: CL the class's count of
; events, CH its first event id; TXTPTR at the "(". Event-id 0 is the whole
; class (SEVALL); n is the class's n-th event, but STRIG's, whose 0, 2, 4 and
; 6 are halved (ROR AL,1, bit 0 into bit 7, so an odd one comes out 128 or
; more). Past the class: Illegal function call. The 8086's JGE is a signed
; compare, kept: an id of 128 or more passes it (as negative), and is a flag
; past TRPTBL's end (a question for the statements' step, 7f).
SEVSTT:         LDA     RCL            ; save the class's count and first id
                PHA
                LDA     RCH
                PHA
                JSR     SYNCHR
                .byte   '('
                JSR     GETBYT         ; A: the class-relative event-id
                PHA
                JSR     SYNCHR
                .byte   ')'
                PHA                    ; ON, OFF or STOP
                JSR     EOSCH1         ; past it; the statement must end
                PLA                    ; DX: ON, OFF or STOP
                STA     RDL
                PLA                    ; AL: the event-id
                STA     RAL
                PLA                    ; CX: the class's count and first id
                STA     RCH
                PLA
                STA     RCL
                LDA     RAL
                LDX     RCH
                CPX     #STROFF        ; STRIG(x)?
                BNE     SEVST1
                LSR     A              ; ROR AL,1: 0, 1, 2 or 3
                BCC     SEVST2
                ORA     #$80
                BRA     SEVST2
; SEVST1 (GWSTS.ASM 1040)
SEVST1:         CMP     #0
                BEQ     SEVALL         ; 0: every event of the class
                DEC     A              ; 0-relative
; SEVST2 (GWSTS.ASM 1044)
SEVST2:         STA     RAL
                LDX     RDL            ; save ON, OFF or STOP
                PHX
                SEC                    ; JGE: signed, at or past the count?
                SBC     RCL
                BVC     :+
                EOR     #$80
:               BPL     FCERR1         ; not in range
                LDA     RAL            ; the event id
                CLC
                ADC     RCH
                JSR     EVADR          ; DX: its flag
                PLA                    ; A: ON, OFF or STOP
; SET1EV (GWSTS.ASM 1057): one event ON, OFF or STOP (A), its flag at DX.
SET1EV:         JSR     EVSET
                RTS

FCERR1:         JMP     FCERR          ; GWSTS.ASM 1060
SNERR1:         JMP     SNERR          ; GWSTS.ASM 1061

; SEVALL (GWSTS.ASM 1068): every event of the class ON, OFF or STOP (DL), CH
; its first id, CL its count. The 8086's LOOPNE also tests Z from ADD DX,3,
; which is never set (DX is TRPTBL's address and more).
SEVALL:         LDA     RDL            ; save ON, OFF or STOP
                PHA
                LDA     RCH            ; AL: the class's first id
                STZ     RCH            ; CX: the count
                JSR     EVADR          ; DX: its flag
                PLA                    ; A: ON, OFF or STOP
SEVAL1:         JSR     EVSET          ; A and DX kept
                TAY
                CLC                    ; DX: the next event's flag
                LDA     RDL
                ADC     #3
                STA     RDL
                BCC     :+
                INC     RDH
:               TYA
                DEC     RCL
                BNE     SEVAL1
                RTS

; EVSET (GWSTS.ASM 1083): one event's trap ON, OFF or STOP (A, the token),
; its flag at DX; another token is Syntax error. A, DX and CX are kept, as
; the 8086 keeps its registers (its BX, the text pointer, is TXTPTR here);
; RBL and X are used.
EVSET:          PHA
                LDX     RDL            ; BX: the flag
                STX     RBL
                LDX     RDH
                STX     RBH
                CMP     #ON_TK
                BEQ     EVON
                CMP     #OFF_TK
                BEQ     EVOFF
                CMP     #STOP_TK
                BNE     SNERR1
EVSTP:          JSR     STPTRP         ; GWSTS.ASM 1095: stop the event
                BRA     EVONX
EVON:           JSR     ONTRP          ; GWSTS.ASM 1097: enable it
                BRA     EVONX
EVOFF:          JSR     OFFTRP         ; GWSTS.ASM 1099: disable it
EVONX:          PLA                    ; GWSTS.ASM 1100
                RTS

; EVADR (GWSTS.ASM 1110): DX the address of event A's flag, TRPTBL + 3 * A,
; in 16 bits as the 8086's AX (AH 0). A is used.
EVADR:          STZ     RDH            ; DX = AX: the event id
                STA     RDL
                ASL     A              ; AX + AX
                ROL     RDH
                CLC
                ADC     RDL            ; + DX: 3 * the id
                BCC     :+
                INC     RDH
:               CLC
                ADC     #<TRPTBL
                STA     RDL
                LDA     RDH
                ADC     #>TRPTBL
                STA     RDH
                RTS

; ONGOTP (GWSTS.ASM 1140): from ON (p3/gwmain.s NTOERR), A the token after
; ON, TXTPTR at it. ON KEY(n), ON COM(n), ON PEN, ON STRIG(n): C clear, CH the
; event's id, CL 1 (the most line numbers its GOSUB takes), TXTPTR backed up
; for CHRGTR to the event's last character (")", or PEN's second byte).
; Anything else: C set, TXTPTR as it came, for ON's numeric form. A is the
; character at TXTPTR either way. A two-byte token ($FD-$FF and a byte) is
; compared as AX, CH its first byte; a one-byte token with CH 0 (the 8086's
; AH, which ON's dispatch left 0). ON TIMER(n) and ON PLAY(n) aren't events
; in this source (TIMER isn't a reserved word here; PLAY is a statement's):
; ON's numeric form takes them. The text pointer saved goes on the 6502's
; stack, as the 8086's PUSH BX.
ONGOTP:         LDX     TXTPTR+1       ; the text pointer, in case it isn't ours
                PHX
                LDX     TXTPTR
                PHX
                STZ     RCH            ; assume a one-byte token
                CMP     #$FD           ; a two-byte token?
                BCC     NT2BTK         ; not $FF, $FE or $FD
                STA     RCH            ; CH: its first byte
                JSR     CHRGTR         ; A: its second
; NT2BTK (GWSTS.ASM 1147): AX against each event's token.
NT2BTK:         LDX     RCH
                CMP     #KEY_TK        ; $KEY2B: ON KEY(n)?
                BNE     @com
                CPX     #0
                BNE     @com
                LDA     #KEYOFF        ; CX: the class's first id and count
                LDX     #NMKEYT
                BRA     ONFUNC
@com:           CMP     #COM_XS        ; $COM2B: ON COM(n)?
                BNE     @pen
                CPX     #$FE
                BNE     @pen
                LDA     #COMOFF
                LDX     #NMCOMT
                BRA     ONFUNC
@pen:           CMP     #PEN_FN        ; $PEN2B: ON PEN?
                BNE     NTONPN
                CPX     #$FF
                BNE     NTONPN
                LDA     #PENOFF        ; CX: 256 * PENOFF
                STA     RCH
                STZ     RCL
                JSR     CHRGTR         ; the next token
                BRA     ONSTM
; NTONPN (GWSTS.ASM 1159)
NTONPN:         CMP     #STRIG_FN      ; $STR2B: ON STRIG(n)?
                BNE     @not
                CPX     #$FF
                BNE     @not
                LDA     #STROFF
                LDX     #NMSTRT
                BRA     ONFUNC
@not:           PLX                    ; not an event: the entry's text pointer
                STX     TXTPTR
                PLX
                STX     TXTPTR+1
                SEC                    ; C set: not an event
                BRA     ONGOTX
ONFUNC:         STA     RCH            ; (the 8086's MOV CX before each test)
                STX     RCL
; ONFUN (GWSTS.ASM 1171): "(" n ")": n - 1 (STRIG's n halved, ROR as SEVSTT's)
; must be below the class's count (unsigned), else Illegal function call.
ONFUN:          JSR     GETSUB         ; A: the event's number in (x)
                DEC     A              ; 0-relative (if not STRIG(x))
                LDX     RCH
                CPX     #STROFF        ; STRIG(x)?
                BNE     ONFUN1
                INC     A              ; STRIG's number back
                LSR     A              ; ROR AL,1: 0, 1, 2 or 3
                BCC     ONFUN1
                ORA     #$80
; ONFUN1 (GWSTS.ASM 1178)
ONFUN1:         CMP     RCL            ; at or past the count?
                BCC     :+
                JMP     FCERR          ; FCERR2: Illegal function call
:               STA     RCL            ; CL: the event's index
; ONSTM (GWSTS.ASM 1182)
ONSTM:          CLC                    ; CH: the event's id
                LDA     RCH
                ADC     RCL
                STA     RCH
                LDA     #1             ; CL: at most one line number
                STA     RCL
                PLA                    ; the saved text pointer dropped
                PLA
                DECW    TXTPTR         ; back for CHRGTR
                CLC                    ; C clear: an event
; ONGOTX (GWSTS.ASM 1188): A the token back (C kept).
ONGOTX:         LDA     (TXTPTR)
                RTS

; GETSUB (GWSTS.ASM 1191): past the token, then "(" a byte ")": A the byte.
; CX is kept.
GETSUB:         LDA     RCL
                PHA
                LDA     RCH
                PHA
                JSR     CHRGTR         ; skip the current token
; GETSU2 (GWSTS.ASM 1193)
GETSU2:         JSR     SYNCHR
                .byte   '('
                JSR     GETBYT         ; the event class's index
                PHA
                JSR     SYNCHR
                .byte   ')'
                PLA
                PLX
                STX     RCH
                PLX
                STX     RCL
                RTS

; SETGSB (GWSTS.ASM 1207): event A's GOSUB line, DX (the line's link, or 0),
; after its flag in TRPTBL. DX is kept; RSI is used (the 8086 keeps SI).
SETGSB:         LDX     RDL            ; save the line's pointer
                PHX
                LDX     RDH
                PHX
                JSR     EVADR          ; DX: the event's flag
                LDA     RDL            ; SI: the flag
                STA     RSI
                LDA     RDH
                STA     RSI+1
                PLA                    ; DX: the line's pointer back
                STA     RDH
                PLA
                STA     RDL
                LDY     #1             ; at SI + 1
                STA     (RSI),Y
                INY
                LDA     RDH
                STA     (RSI),Y
                RTS

; EVTRP (GWSTS.ASM 1222): event A has happened, from its source (7c-7f's; the
; test trigger, p3/trptst.s, in 7b): its trap requested (REQTRP) if it is on
; and a program is running (not CURLIN 65535). Z set back: not trapped. (The
; 8086's comment says NZ for that; its code gives Z, kept.) DX and CX are
; kept (the 8086 keeps DX, CX and BX); A, X, RBL are used.
EVTRP:          LDX     RDL            ; save DX
                STX     EVSVDX
                LDX     RDH
                STX     EVSVDX+1
                LDX     CURLIN         ; direct mode: no event trapping
                INX
                BNE     @run
                LDX     CURLIN+1
                INX
                BEQ     NOT_ON         ; (Z set)
@run:           JSR     EVADR          ; DX: the event's flag
                LDX     RDL            ; BX: the flag
                STX     RBL
                LDX     RDH
                STX     RBH
                LDA     (RBL)
                AND     #T_ON          ; Z clear if trapping is on for it
                BEQ     NOT_ON
                PHP
                JSR     REQTRP         ; on: issue a request
                PLP
; NOT_ON (GWSTS.ASM 1236): DX back, the flags kept.
NOT_ON:         PHP
                LDX     EVSVDX
                STX     RDL
                LDX     EVSVDX+1
                STX     RDH
                PLP
                RTS

; EVCLR (GWSTS.ASM 1245): event A reset: its request freed (FRETRP). DX and
; CX are kept; A, X, RBL and the flags are used.
EVCLR:          LDX     RDL            ; save DX
                STX     EVSVDX
                LDX     RDH
                STX     EVSVDX+1
                JSR     EVADR          ; DX: the event's flag
                LDX     RDL            ; BX: the flag
                STX     RBL
                LDX     RDH
                STX     RBH
                JSR     FRETRP         ; reset the trap request
                BRA     NOT_ON

; TSTCEV (GWSTS.ASM 1259): test and clear event A (its index from 0): BX
; (RBL, RBH, the 8086's result) -1 if it had happened (T_REQ), else 0; its
; request freed (FRETRP). DX is used.
TSTCEV:         JSR     EVADR          ; DX: the event's flag
                LDX     RDL            ; BX: the flag
                STX     RBL
                LDX     RDH
                STX     RBH
                LDA     (RBL)          ; the event's state
                PHA
                JSR     FRETRP         ; reset the request (T_REQ cleared)
                PLA
                LDX     #0             ; BX 0: not occurred
                AND     #T_REQ
                BEQ     TSTEVX
                DEX                    ; BX -1: it occurred
; TSTEVX (GWSTS.ASM 1270)
TSTEVX:         STX     RBL
                STX     RBH
                RTS

; EOSCHK (GWSTS.ASM 467): the statement must end at TXTPTR (EOSCHK) or after
; it (EOSCH1), else Syntax error.
EOSCHK:         DECW    TXTPTR         ; back up the text pointer
EOSCH1:         JSR     CHRGTR         ; the next character, spaces skipped
                BEQ     EOSCKX         ; the end of the statement
                JMP     SNERR
EOSCKX:         RTS                    ; GWSTS.ASM 473

.segment "BRAM"
; TRPTBL (GWRAM.ASM 460): NUMTRP traps, each its flag (T_ON, T_STOP, T_REQ)
; and its GOSUB line's pointer (the line's link; 0 for none), in event id
; order (gwbasic.inc). Only P3 touches it: the resident RETURN's entry keeps
; a flag's address and hands it back to P3 (RTNTRP, p3/gwmain.s).
TRPTBL:         .res    3 * NUMTRP
EVSVDX:         .res    2              ; EVTRP's and EVCLR's DX (the 8086's PUSH DX)

; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; BIMISC.ASM's SWAP, ERASE and CLEAR, in block P3 (hw/oodos.md, "The plan,
; phase 7"), each reached by its resident stub (res/far3.s):
;
;   SWAPP     SWAP a, b: two variables (or elements) of one type exchanged
;   ERASEP    ERASE a[, b ...]: arrays removed, the ones above moved down
;   CLEARP    CLEAR [x][, n][, m]: the memory map's checks and settings; the
;             stub then jumps to CLEARC (the 8086's GCLERC)
;
; and step e's, the event traps' routines (BIMISC.ASM 338-545), on a trap's
; flag in TRPTBL (p3/gwsts.s):
;
;   ONTRP OFFTRP STPTRP    a trap on, off or stopped (EVSET's)
;   RSTTRP REQTRP          the stop taken off (RETURN's); a request
;   SETTRP FRETRP          ONGSBF counted up; a request freed
;   INITRP                 every trap off with no line (CLEARC's, an entry)
;   GOTRP                  NEWSTT's poll: the first trap on and requested,
;                          for the resident GOSUB2 (an entry)
;   TRPCNT                 OO-1: TRPON counted from the table
;
; Where the 8086 keeps a pointer or a value on its stack across PTRGET or a
; formula (which may itself reach P3: STRING$, VARPTR$), it goes on the BASIC
; stack here; the pointers are PTR and PTR2, the counts and addresses the
; pseudo-registers, as the 8086's registers.
;
; The far call rule (translate-7b-common.md): each returns by RTS, or ends in
; an error. CLEAR's JMP GCLERC is the stub's, not the block's: CLEARC's
; STKINI resets the 6502's stack. GOTRP's JMP GOSUB2 is the poll's too: GOTRP
; returns C set, and the resident poll jumps (a jump from here into the
; statement loop would leave the far call's frame on the 6502's stack).

.include "gwbasic.inc"

;@entry SWAPP
;@entry ERASEP
;@entry CLEARP
;@entry INITRP
;@entry GOTRP
.export SWAPP, ERASEP, CLEARP
.export ONTRP, OFFTRP, STPTRP, RSTTRP, REQTRP, SETTRP, FRETRP, INITRP, GOTRP, TRPCNT
.import CHRGTR, SYNCHR, PTRGET, SPUSH, SPOP, SPUSHA, SPOPA, TMERR, FCERR, SNERR
.import OMERR, INTID2, SUBFLG, MEMSIZ, TOPMEM, BSTACK, FRMQNT, CLSALL
.import ONEFLG, GETSTK, TRPTBL

CLRMIN          = 2 * 110 + 20         ; (2*NUMLEV)+20: OEM.H's NUMLEV, 110

.code

; --- SWAP (BIMISC.ASM 675) ---------------------------------------------------------------

; SWAP: the first variable's value to SWPTMP, ARYTAB and its type kept; the
; second must have the same type (Type mismatch), and making it mustn't have
; moved the arrays (a new simple variable: the first's address would be
; stale; Illegal function call). Then the second into the first, SWPTMP
; into the second. The 8086's VMOVE copies VALTYP bytes.
SWAPP:          JSR     PTRGET         ; PTR: value 1
                LDX     #PTR
                LDA     #2
                JSR     SPUSH          ; its address
                LDY     #0             ; SWPTMP = value 1
@t:             LDA     (PTR),Y
                STA     SWPTMP,Y
                INY
                CPY     VALTYP
                BNE     @t
                LDX     #ARYTAB        ; ARYTAB, to see a change
                LDA     #2
                JSR     SPUSH
                LDA     VALTYP         ; GETYPR: value 1's type
                JSR     SPUSHA
                JSR     SYNCHR
                .byte   ','
                JSR     PTRGET         ; PTR: value 2
                JSR     SPOPA
                STA     RCH            ; CH: value 1's type
                LDA     VALTYP
                CMP     RCH
                BEQ     :+
                JMP     TMERR
:               LDX     #RDL           ; DX: the old ARYTAB
                LDA     #2
                JSR     SPOP
                LDA     ARYTAB
                CMP     RDL
                BNE     GFCERR
                LDA     ARYTAB+1
                CMP     RDH
                BNE     GFCERR
                LDX     #PTR2          ; PTR2: value 1's address
                LDA     #2
                JSR     SPOP
                LDY     #0             ; value 2 into value 1's place
@a:             LDA     (PTR),Y
                STA     (PTR2),Y
                INY
                CPY     VALTYP
                BNE     @a
                LDY     #0             ; SWPTMP (value 1) into value 2's
@b:             LDA     SWPTMP,Y
                STA     (PTR),Y
                INY
                CPY     VALTYP
                BNE     @b
                RTS

GFCERR:         JMP     FCERR          ; BIMISC.ASM 721

; --- ERASE (BIMISC.ASM 725) --------------------------------------------------------------

; ERASE: PTRGET with SUBFLG 1 (ERSFIN) finds the array: Z set, BX at its
; number of dimensions, the size before it (the 8086's DX from CNOMAT); Z
; clear, not there: Illegal function call. CX goes back from the size over
; the name's extra characters (bit 7 set) and its count (clear) to the entry's
; start; then everything from the entry's end to STREND moves down to it, the
; 8086's way: the compare before each byte, the byte at STREND moved too, so
; STREND is where the last went. Then a comma and the next name.
ERASEP:         LDA     #1             ; ERASE calling PTRGET
                STA     SUBFLG
                JSR     PTRGET
                BNE     GFCERR         ; not there
                STZ     SUBFLG         ; (the 8086 stores AL, 0)
                SEC                    ; CX = BX - 3
                LDA     RBL
                SBC     #3
                STA     RCL
                LDA     RBH
                SBC     #0
                STA     RCH
; LPBKNM (BIMISC.ASM 739): back to the count, the one without bit 7.
LPBKNM:         LDA     (RCL)
                PHA
                DECW    RCL
                PLA
                BMI     LPBKNM
                DECW    RCL            ; past the name's two characters: the
                DECW    RCL            ; entry's start
                LDY     #1             ; DX: the size, before the dimensions
                SEC
                LDA     RBL
                SBC     #2
                STA     PTR
                LDA     RBH
                SBC     #0
                STA     PTR+1
                CLC                    ; DX = BX + the size: the entry's end
                LDA     (PTR)
                ADC     RBL
                STA     RDL
                LDA     (PTR),Y
                ADC     RBH
                STA     RDH
                LDA     STREND         ; BX: the last byte to move
                STA     RBL
                LDA     STREND+1
                STA     RBH
; ERSLOP (BIMISC.ASM 750): the 8086's LAHF and SAHF keep the compare's Z
; across the move and the increments; PHP and PLP here.
ERSLOP:         LDA     RBL
                CMP     RDL
                BNE     :+
                LDA     RBH
                CMP     RDH
:               PHP
                LDA     (RDL)
                STA     (RCL)
                INCW    RDL
                INCW    RCL
                PLP
                BNE     ERSLOP
                DECW    RCL            ; the new STREND
                LDA     RCL
                STA     STREND
                LDA     RCH
                STA     STREND+1
                LDA     (TXTPTR)       ; more?
                CMP     #','
                BEQ     :+
                RTS
:               JSR     CHRGTR
                JMP     ERASEP

; --- CLEAR (BIMISC.ASM 807) --------------------------------------------------------------

; CLEAR [x][, n][, m]. x is a relic, an integer, ignored. On a PC, n is the
; data segment's size, the top of BASIC's memory (TOPMEM), and m the stack's
; size, which is taken from the top: the string space ends m + 2 below n.
;
; OO-1: the BASIC stack is fixed RAM below the workspace (hw/oodos.md, "The
; plan, phase 7": the memory layout), so the two part:
;
;   n         the workspace's top: one past the byte that is VAL's zero, the
;             string space ending below it (MEMSIZ = n - 2), as start-up
;             leaves it at the TPA's top; the bytes above n are left alone,
;             as a PC's above TOPMEM. Its default is the current top
;             (MEMSIZ + 2), its most the TPA's top that start-up used (the
;             base page's BDOS address, the 8086's MAXMEM): more is Illegal
;             function call.
;   m         the BASIC stack's size, within its fixed 512 bytes: STKLOW is
;             TOPMEM - m, or BSTACK (all of it) for m over 512; its default
;             is the current size, TOPMEM - STKLOW.
;
; The source's checks are kept, in its order: n or m 0 (GETMPM), Illegal
; function call; m 240 or less, Out of memory; m more than n (SUBDE's
; borrow), Out of memory; VARTAB + 20 not below the top, Out of memory, but
; the top is n here, not n - m (OO-1: the stack isn't taken from the
; workspace). A bad end is Syntax error. FILTAB, the file blocks' base, is
; 7c's (OO-1: the files' blocks are P4's).
;
; The files are closed first (CLSALL, with NLONLY 0 so file 0 too: a stand-in
; until 7c, p3/todo_a2.s). CLEARC (the stub's) resets the rest, the event
; traps' table too (INITRP, below).
GCLERC:         RTS                    ; BIMISC.ASM 805: to the stub's JMP CLEARC
CLEARP:         PHP                    ; Z: the statement's end
                JSR     CLSALL         ; (NLONLY 0: 7c's)
                PLP
                BEQ     GCLERC         ; no formula: just clear
                CMP     #','           ; no x
                BEQ     CSKPCM
                JSR     INTID2         ; x: a dummy integer
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     GCLERC
; CSKPCM (BIMISC.ASM 825)
CSKPCM:         JSR     SYNCHR
                .byte   ','
                BEQ     GCLERC
                PHA                    ; DX: the current top, n's default
                CLC                    ; (OO-1: MEMSIZ + 2; the 8086's TOPMEM)
                LDA     MEMSIZ
                ADC     #2
                STA     RDL
                LDA     MEMSIZ+1
                ADC     #0
                STA     RDH
                PLA
                CMP     #','
                BEQ     CLEARS         ; no n
                JSR     GETMPM         ; n
                LDA     BDOS+1         ; MAXMEM >= n? (OO-1: the TPA's top)
                CMP     RDL
                LDA     BDOS+2
                SBC     RDH
                BCS     CLEARS
                JMP     FCERR
; CLEARS (BIMISC.ASM 842): n kept; then m, or the stack's size as it is.
CLEARS:         DECW    TXTPTR
                JSR     CHRGTR
                PHP
                LDX     #RDL
                LDA     #2
                JSR     SPUSH          ; n
                PLP
                BEQ     CDFSTK
                JSR     SYNCHR
                .byte   ','
                BEQ     CDFSTK
                JSR     GETMPM         ; m
                DECW    TXTPTR
                JSR     CHRGTR
                BEQ     CLEART
                JMP     SNERR
; CDFSTK (BIMISC.ASM 896): m = the stack's size now, TOPMEM - STKLOW.
CDFSTK:         SEC
                LDA     TOPMEM
                SBC     STKLOW
                STA     RDL
                LDA     TOPMEM+1
                SBC     STKLOW+1
                STA     RDH
; CLEART (BIMISC.ASM 855): DX m, BX n.
CLEART:         LDX     #RBL
                LDA     #2
                JSR     SPOP           ; BX: n
                LDA     #<CLRMIN       ; m reasonable?
                CMP     RDL
                LDA     #>CLRMIN
                SBC     RDH
                BCS     GOMERR
                LDA     RBL            ; SUBDE: n - m, a borrow wanted more than
                CMP     RDL            ; there is
                LDA     RBH
                SBC     RDH
                BCC     GOMERR
                CLC                    ; room: VARTAB + 20 below the top
                LDA     VARTAB         ; (OO-1: n, not n - m)
                ADC     #20
                TAX
                LDA     VARTAB+1
                ADC     #0
                CPX     RBL
                SBC     RBH
                BCS     GOMERR
                SEC                    ; OO-1: STKLOW = TOPMEM - m, at most all
                LDA     TOPMEM         ; of BSTACK
                SBC     RDL
                TAX
                LDA     TOPMEM+1
                SBC     RDH
                BCC     @all
                CPX     #<BSTACK
                PHA
                SBC     #>BSTACK
                PLA
                BCS     @set
@all:           LDX     #<BSTACK
                LDA     #>BSTACK
@set:           STX     STKLOW
                STA     STKLOW+1
                DECW    RBL            ; VAL's zero, below n
                LDA     #0
                STA     (RBL)
                DECW    RBL            ; MEMSIZ: the string space's top
                LDA     RBL
                STA     MEMSIZ
                LDA     RBH
                STA     MEMSIZ+1
                RTS                    ; JMP GCLERC

; GETMPM (BIMISC.ASM 885): a formula, 0..65535, in DX; 0 is Illegal
; function call.
GETMPM:         JSR     FRMQNT
                LDA     RDL
                ORA     RDH
                BNE     :+
                JMP     FCERR
:               RTS

GOMERR:         JMP     OMERR          ; BIMISC.ASM 892

; --- The traps: ON, OFF, STOP, INIT, REQUEST, FREE, RESET (BIMISC.ASM 338) ---------------

; Each takes a trap's flag at (RBL), the 8086's [BX] (TRPTBL, p3/gwsts.s; BX,
; the 8086's text pointer, is TXTPTR here, which none of them touches). Its
; bits are T_ON, T_STOP and T_REQ (gwbasic.inc). ONGSBF counts the traps
; that are on and requested and not stopped (T_ON+T_REQ exactly), for
; NEWSTT's poll; FRETP2 never takes it below 0. The 8086's CLI and STI have
; no counterpart: the OO-1's sources are polled at NEWSTT, never raised in an
; interrupt (OO-1:), so nothing can come between a flag's load and its store.
;
; OO-1: TRPON, NEWSTT's gate (gwbasic.inc), is kept here, right after each
; change to a trap's T_ON: ONTRP sets it, OFFTRP counts the table again
; (TRPCNT), INITRP clears it. No other routine changes T_ON.

; ONTRP (BIMISC.ASM 352): the trap on, its request kept. A request kept while
; it was off or stopped now counts (SETTP2). The STAs keep CMP's Z.
ONTRP:          LDA     (RBL)
                AND     #T_REQ         ; leave the request bit
                ORA     #T_ON          ; add the on bit
                CMP     (RBL)
                STA     (RBL)
                STA     TRPON          ; OO-1: a trap is on (A has T_ON)
                BEQ     ONTRP0         ; no change in status
                AND     #T_REQ
                BNE     SETTP2         ; a delayed trap request
ONTRP0:         RTS

; OFFTRP (BIMISC.ASM 367): the trap off, its request dropped, and one fewer
; counted if it was on and requested (FRECHK, with A the old flag).
OFFTRP:         LDA     (RBL)
                PHA
                LDA     #0
                STA     (RBL)
                JSR     TRPCNT         ; OO-1: TRPON counted again
                PLA
                BRA     FRECHK

; STPTRP (BIMISC.ASM 375): the trap stopped; one fewer counted if it was on
; and requested (FRECHK, A the old flag: the 8086 keeps AL and its flags
; round the OR by LAHF and SAHF).
STPTRP:         LDA     (RBL)
                PHA
                ORA     #T_STOP
                STA     (RBL)
                PLA
FRECHK:         EOR     #T_ON+T_REQ    ; stop or off an active request?
                BEQ     FRETP2         ; yes: free the active request's count
                RTS

; RSTTRP (BIMISC.ASM 394): the stop taken off; if that leaves the trap on
; and requested, it counts again (SETCHK). The STA keeps CMP's Z.
RSTTRP:         LDA     (RBL)
                AND     #T_ON+T_REQ
                CMP     (RBL)
                STA     (RBL)
                BNE     SETCHK         ; a new status: check for SETTRP
                RTS

; REQTRP (BIMISC.ASM 406): the trap requested, if it is on; a new request on
; a trap not stopped counts (SETCHK).
REQTRP:         LDA     (RBL)
                AND     #T_ON
                BEQ     REQTPX         ; the trap isn't on
                LDA     (RBL)
                ORA     #T_REQ
                CMP     (RBL)
                BEQ     REQTPX         ; no change
                STA     (RBL)
SETCHK:         EOR     #T_ON+T_REQ
                BEQ     SETTP2         ; on and requested: set the global flag
REQTPX:         RTS

; SETTRP (BIMISC.ASM 423): one more trap waiting, in ONGSBF.
SETTRP:
SETTP2:         INC     ONGSBF
                RTS

; FRETRP (BIMISC.ASM 433): the request freed; one fewer counted if there was
; one (FRETP2: if ONGSBF is 0 already, it stays 0). The STA keeps CMP's Z.
FRETRP:         LDA     (RBL)
                AND     #T_ON+T_STOP
                CMP     (RBL)
                STA     (RBL)
                BNE     FRETP2         ; decrement the global flag too
FRETP1:         RTS
FRETP2:         LDA     ONGSBF         ; SUB AL,1 / JB: not below 0
                BEQ     FRETP1
                DEC     ONGSBF
                RTS

; INITRP (BIMISC.ASM 454): every trap off, with no GOSUB line; none waiting.
INITRP:         LDX     #3 * NUMTRP - 1
                LDA     #0
INITP0:         STA     TRPTBL,X       ; the flag, and the line's pointer too
                DEX
                BPL     INITP0
                STA     ONGSBF
                STA     TRPON          ; OO-1: no trap on
                RTS

; GOTRP (BIMISC.ASM 475): from NEWSTT's poll, with ONGSBF nonzero. Not in ON
; ERROR's handler (ONEFLG), nor in a direct statement (CURLIN 65535): C
; clear. Else the first trap in TRPTBL that is on and requested and not
; stopped, and has a GOSUB line (one at line 0 is passed over), is freed and
; stopped, the BASIC stack's room checked (GETSTK, as GOSUB's), and C set
; back with DX its line's pointer and CX its flag's address: the resident
; poll then jumps to GOSUB2 (the far call rule: the 8086 drops its return
; and jumps to GOSUB2 from here). None: C clear. TXTPTR is kept for GOSUB2.
GOTRP:          LDA     ONEFLG
                BEQ     :+
                CLC
                RTS                    ; can't trap from the error trap
:               LDA     CURLIN
                AND     CURLIN+1
                INC     A
                BEQ     GOTRPX         ; can't trap from a direct statement
                LDA     #<TRPTBL       ; BX: the trap table, CH: the traps
                STA     RBL
                LDA     #>TRPTBL
                STA     RBH
                LDA     #NUMTRP
                STA     RCH
GOTRPL:         LDA     (RBL)
                CMP     #T_ON+T_REQ
                BEQ     GOTRP2         ; enabled and requested
GOTRP1:         CLC                    ; the next entry
                LDA     RBL
                ADC     #3
                STA     RBL
                BCC     :+
                INC     RBH
:               DEC     RCH
                BNE     GOTRPL
GOTRPX:         CLC
                RTS
; GOTRP2 (BIMISC.ASM 509): a request found.
GOTRP2:         LDY     #1             ; DX: the trap's GOSUB line
                LDA     (RBL),Y
                STA     RDL
                INY
                LDA     (RBL),Y
                STA     RDH
                ORA     RDL
                BEQ     GOTRP1         ; no GOSUB line: try the next trap
                JSR     FRETRP         ; free the trap request
                JSR     STPTRP         ; put stop on the trap
                LDA     #3
                JSR     GETSTK         ; make sure there is room on the stack
                LDA     RBL            ; CX: the trap's flag address
                STA     RCL
                LDA     RBH
                STA     RCH
                SEC
                RTS                    ; JMP GOSUB2: the poll's

; TRPCNT (OO-1: the project's own): TRPON nonzero if any trap in the table is
; on, else 0. X is used.
TRPCNT:         LDA     #0
                LDX     #3 * NUMTRP - 3
@t:             ORA     TRPTBL,X       ; the flags
                DEX
                DEX
                DEX
                BPL     @t
                AND     #T_ON
                STA     TRPON
                RTS

.segment "BRAM"
SWPTMP:         .res    8              ; SWAP's first value (GWDATA.ASM's SWPTMP)

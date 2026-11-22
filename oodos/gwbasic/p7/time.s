; Derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; Block P7's time (hw/oodos.md, "The plan, phase 7": 7f; the space plan's
; answer 9: DATE$ and TIME$ in P7, on function 105), both modes (the owner's
; answer 4):
;
;   TIMEP     TIMER (OO-1, the manual's p. 344; the source's TIMER is
;             ADVERR): the seconds since midnight, a single: function 105's
;             hour, minute and second, plus the ms counter's time since TIMER
;             first saw that second, at most .999 (the owner's answer 7)
;   DATEF     DATE$ (GWSTS.ASM's DATEF): "mm-dd-yyyy" from 105's day count
;   TIMEF     TIME$ (TIMEF): "hh:mm:ss"
;   DATESP    DATE$ = string (DATES, PRSDAT): m, d, y each with / or -
;             between (GNUM8, GNUM16, DATSEP); OO-1 (the owner's answer 8): a
;             year outside 2000-2099 Illegal function call, a two-digit year
;             included (the manual's 19yy, past what the clock holds); the
;             month and the day checked (the source's DYOFYR); then function
;             104 with the hour and the minute as they are (its seconds 0:
;             104's)
;   TIMESP    TIME$ = string (TIMES, PRSTIM): h, m, s (and hundredths) with :
;             or . between, each part may be left off (0); OO-1 (answer 8):
;             104 sets the hour and the minute, the seconds 0
;   TIMONP    TIMER ON (the owner's answer 23): the resident TIMHK set as
;             OOLIB's tick hook, every 1,000 ms, counting ON TIMER(n)'s n down
;             (TIMCNT) and asking for the trap when it runs out; TIMOFP the
;             hook off (TIMER OFF; CLEARC by P3's INITRP; SYSTEM by SNDINI)
;   TIMSET    ON TIMER(n)'s n (ONSETP's): 1-86,400 (the manual's p. 217), else
;             Illegal function call; kept in TIMN (resident, the hook's)
;
; The far call rule: each returns by RTS, or ends in an error.

.include "gwbasic.inc"

.export TIMEP, DATEF, TIMEF, DATESP, TIMESP, TIMONP, TIMOFP, TIMSET
.import FCERR, CHRGTR, SYNCHR, FRMEVL, FRESTR, STRINI, PUTNEW, DSCTMP, FDIVS, FRCSNG, QINT
.import MSNOW, MSN, TIMN, TIMCNT, TIMREQ, TIMON, TIMHK

.code

; --- TIMER ---------------------------------------------------------------------------------

; TIMEP: TXTPTR at TIMER's token; returns as EVAL, past it.
TIMEP:          JSR     GETDT          ; TSEC: the seconds since midnight
                JSR     MSNOW
                LDX     #2             ; a new second: its ms from now
@c:             LDA     TSEC,X
                CMP     TLAST,X
                BNE     @new
                DEX
                BPL     @c
                BRA     @old
@new:           LDX     #2
:               LDA     TSEC,X
                STA     TLAST,X
                DEX
                BPL     :-
                LDX     #3
:               LDA     MSN,X
                STA     TMS0,X
                DEX
                BPL     :-
@old:           SEC                    ; T4-T5: the ms since, at most 999
                LDA     MSN
                SBC     TMS0
                STA     T4
                LDA     MSN+1
                SBC     TMS0+1
                STA     T5
                LDA     MSN+2
                SBC     TMS0+2
                ORA     MSN+3          ; (65,536 or more: held too)
                BNE     @cap
                LDA     T5
                CMP     #>1000
                BCC     @frac
                BNE     @cap
                LDA     T4
                CMP     #<1000
                BCC     @frac
@cap:           LDA     #<999
                STA     T4
                LDA     #>999
                STA     T5
@frac:          LDX     #3             ; T0-T3: the seconds x 1,000 + the ms
:               STZ     T0,X
                DEX
                BPL     :-
                LDX     #10            ; x 1,024 ...
                JSR     ADDSH
                LDX     #4             ; ... - x 16 ...
                JSR     SUBSH
                LDX     #3             ; ... - x 8
                JSR     SUBSH
                CLC
                LDA     T0
                ADC     T4
                STA     T0
                LDA     T1
                ADC     T5
                STA     T1
                BCC     :+
                INC     T2
                BNE     :+
                INC     T3
:               JSR     U32BX          ; BXDX: it, a single
                STZ     FACLO          ; FAC: 1,000
                STZ     FACLO+1
                LDA     #$7A
                STA     FACM1
                LDA     #$8A
                STA     FACEXP
                LDA     #VT_SNG
                STA     VALTYP
                JSR     FDIVS          ; FAC = BXDX / FAC
                JMP     CHRGTR

; ADDSH, SUBSH: T0-T3 plus or less TSEC shifted left X (in RAL-RCH, AX and
; CX: the pseudo-registers' four bytes in a row).
ADDSH:          JSR     SHSEC
                CLC
                LDX     #0
                LDY     #4             ; (DEY keeps C: CPX wouldn't)
:               LDA     T0,X
                ADC     RAL,X
                STA     T0,X
                INX
                DEY
                BNE     :-
                RTS
SUBSH:          JSR     SHSEC
                SEC
                LDX     #0
                LDY     #4             ; (DEY keeps C: CPX wouldn't)
:               LDA     T0,X
                SBC     RAL,X
                STA     T0,X
                INX
                DEY
                BNE     :-
                RTS
SHSEC:          LDA     TSEC
                STA     RAL
                LDA     TSEC+1
                STA     RAH
                LDA     TSEC+2
                STA     RCL
                STZ     RCH
:               ASL     RAL
                ROL     RAH
                ROL     RCL
                ROL     RCH
                DEX
                BNE     :-
                RTS
.assert RAH = RAL + 1 && RCL = RAL + 2 && RCH = RAL + 3, error, "SHSEC: AX and CX in a row"

; U32BX: T0-T3 (unsigned) as a single in BXDX, rounded.
U32BX:          LDX     #160           ; 2^32's exponent
                LDA     T0
                ORA     T1
                ORA     T2
                ORA     T3
                BNE     @n
                STZ     RBH            ; 0
                RTS
@n:             BIT     T3
                BMI     @r
                ASL     T0
                ROL     T1
                ROL     T2
                ROL     T3
                DEX
                BRA     @n
@r:             BIT     T0             ; rounded by the bit below
                BPL     :+
                INC     T1
                BNE     :+
                INC     T2
                BNE     :+
                INC     T3
                BNE     :+
                LDA     #$80
                STA     T3
                INX
:               STX     RBH
                LDA     T3
                AND     #$7F           ; positive
                STA     RBL
                LDA     T2
                STA     RDH
                LDA     T1
                STA     RDL
                RTS

; GETDT: function 105: DAT (the day, the hour and minute BCD), TSEC the
; seconds since midnight (24 bits), TSS the second; no clock: all 0.
GETDT:          LDA     #<DAT
                LDX     #>DAT
                LDY     #T_GET
                JSR     BDOS
                CPX     #$FF
                BNE     :+
                STZ     DAT
                STZ     DAT+1
                STZ     DAT+2
                STZ     DAT+3
                LDA     #0
:               JSR     BCDBIN
                STA     TSS
                STA     TSEC           ; the second
                STZ     TSEC+1
                STZ     TSEC+2
                LDA     DAT+3          ; + the minute x 60
                JSR     BCDBIN
                LDX     #<60
                LDY     #>60
                JSR     ADDMUL
                LDA     DAT+2          ; + the hour x 3,600
                JSR     BCDBIN
                LDX     #<3600
                LDY     #>3600
; ADDMUL: TSEC += A x Y:X.
ADDMUL:         STX     T6
                STY     T7
                TAX
                BEQ     @x
:               CLC
                LDA     TSEC
                ADC     T6
                STA     TSEC
                LDA     TSEC+1
                ADC     T7
                STA     TSEC+1
                BCC     @d
                INC     TSEC+2
@d:             DEX
                BNE     :-
@x:             RTS

; BCDBIN: A, BCD, as binary.
BCDBIN:         PHA
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                TAX
                PLA
                AND     #$0F
                CLC
:               DEX
                BMI     :+
                ADC     #10
                BRA     :-
:               RTS

; BINBCD: A, 0-99, as BCD.
BINBCD:         LDX     #0
:               CMP     #10
                BCC     :+
                SBC     #10
                INX
                BRA     :-
:               STA     T6
                TXA
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                ORA     T6
                RTS

; --- DATE$, TIME$ (GWSTS.ASM 1865, 2117) -----------------------------------------------------

; DATEF: "mm-dd-yyyy". TXTPTR at DATE$'s token.
DATEF:          LDA     #10
                JSR     STRBEG
                JSR     GETDT
                JSR     DAYYMD         ; TY: the year, TM the month, TD the day
                LDA     TM
                JSR     PUTCHR
                LDA     #'-'
                JSR     PUTCH2
                LDA     TD
                JSR     PUTCHR
                LDA     #'-'
                JSR     PUTCH2
                SEC                    ; the century, then the year in it
                LDA     TY
                SBC     #<2000
                TAY
                LDA     TY+1
                SBC     #>2000
                LDA     #20
                BCS     :+
                TYA
                CLC
                ADC     #100
                TAY
                LDA     #19
:               PHY
                JSR     PUTCHR
                PLA
                JSR     PUTCHR
                BRA     TSTREND
; TIMEF: "hh:mm:ss".
TIMEF:          LDA     #8
                JSR     STRBEG
                JSR     GETDT
                LDA     DAT+2
                JSR     PUTBCD
                LDA     #':'
                JSR     PUTCH2
                LDA     DAT+3
                JSR     PUTBCD
                LDA     #':'
                JSR     PUTCH2
                LDA     TSS
                JSR     PUTCHR
TSTREND:         JSR     PUTNEW         ; the FAC at it
                JMP     CHRGTR
; STRBEG: a string of A bytes (STRINI), PTR2 at its first.
STRBEG:         JSR     STRINI
                LDA     DSCTMP+1
                STA     PTR2
                LDA     DSCTMP+2
                STA     PTR2+1
                RTS
; PUTCHR: A, 0-99, as two digits; PUTBCD: A BCD; PUTCH2: a character.
PUTCHR:         JSR     BINBCD
PUTBCD:         PHA
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     #'0'
                JSR     PUTCH2
                PLA
                AND     #$0F
                ORA     #'0'
PUTCH2:         STA     (PTR2)
                INCW    PTR2
                RTS

; DAYYMD: DAT's day (1 January 1978 day 1) to TY, TM, TD.
DAYYMD:         LDA     DAT
                STA     T0
                LDA     DAT+1
                STA     T1
                LDA     #<1978
                STA     TY
                LDA     #>1978
                STA     TY+1
@y:             JSR     YLEN           ; T2-T3: the year's days
                LDA     T1
                CMP     T3
                BNE     :+
                LDA     T0
                CMP     T2
:               BEQ     @m             ; the year's last day
                BCC     @m
                SEC
                LDA     T0
                SBC     T2
                STA     T0
                LDA     T1
                SBC     T3
                STA     T1
                INCW    TY
                BRA     @y
@m:             LDX     #0             ; the month
@ml:            JSR     MLEN           ; A: month X's days
                STA     T2
                LDA     T1
                BNE     :+
                LDA     T0
                CMP     T2
                BEQ     @d
                BCC     @d
:               SEC
                LDA     T0
                SBC     T2
                STA     T0
                BCS     :+
                DEC     T1
:               INX
                BRA     @ml
@d:             INX
                STX     TM
                LDA     T0
                STA     TD
                RTS
; YLEN: T2-T3 the days of year TY; Z set (leap: LEAP) if TY MOD 4 is 0.
YLEN:           LDA     #<365
                STA     T2
                LDA     #>365
                STA     T3
                LDA     TY
                AND     #3
                BNE     :+
                INC     T2
:               RTS
; MLEN: A the days of month X (0-11) of year TY (a February 29 in a year
; divisible by 4: the DS3231's and OODOS's rule, right for 2000-2099).
MLEN:           CPX     #1
                BNE     :+
                LDA     TY
                AND     #3
                BNE     :+
                LDA     #29
                RTS
:               LDA     DAYSPM,X
                RTS

; DATESP (DATES, PRSDAT): TXTPTR after DATE$'s token.
DATESP:         JSR     TMSTR          ; the string: TSP, TSN
                JSR     GNUM8          ; the month
                STA     TM
                JSR     DATSEP
                JSR     GNUM8          ; the day
                STA     TD
                JSR     DATSEP
                JSR     GNUM16         ; the year
                SEC                    ; OO-1: 2000-2099 only
                LDA     T0
                SBC     #<2000
                TAX
                LDA     T1
                SBC     #>2000
                JNE     DATERR
                CPX     #100
                JCS     DATERR
                LDA     T0
                STA     TY
                LDA     T1
                STA     TY+1
                LDX     TM             ; the month 1-12, the day in it
                DEX
                CPX     #12
                BCS     DATERR
                JSR     MLEN
                CMP     TD
                BCC     DATERR
                LDA     TD
                BEQ     DATERR
                STA     T0             ; T0-T1: the day of the year
                STZ     T1
                LDX     TM
                DEX
:               DEX
                BMI     :+
                JSR     MLEN
                CLC
                ADC     T0
                STA     T0
                BCC     :-
                INC     T1
                BRA     :-
:               LDA     TY             ; + the years from 1978
                PHA
                LDA     TY+1
                PHA
@yl:            LDA     TY
                BNE     :+
                DEC     TY+1
:               DEC     TY
                LDA     TY+1
                CMP     #>1978
                BNE     :+
                LDA     TY
                CMP     #<1978
:               BCC     @yd
                JSR     YLEN
                CLC
                LDA     T0
                ADC     T2
                STA     T0
                LDA     T1
                ADC     T3
                STA     T1
                BRA     @yl
@yd:            PLA
                STA     TY+1
                PLA
                STA     TY
                JSR     GETDT          ; the hour and the minute kept
                LDA     T0
                STA     DAT
                LDA     T1
                STA     DAT+1
SETDT:          LDA     #<DAT          ; function 104 (its seconds 0)
                LDX     #>DAT
                LDY     #T_SET
                JSR     BDOS
                LDA     #$FF           ; TIMER's second seen anew
                STA     TLAST
                RTS
DATERR:         JMP     FCERR

; TIMESP (TIMES, PRSTIM): TXTPTR after TIME$'s token.
TIMESP:         JSR     TMSTR
                JSR     GNUM8          ; the hour
                CMP     #24
                BCS     DATERR
                STA     TM
                JSR     TIMSEP
                JSR     GNUM8          ; the minute
                CMP     #60
                BCS     DATERR
                STA     TD
                JSR     TIMSEP
                JSR     GNUM8          ; the second (OO-1: 104 sets it to 0)
                CMP     #60
                BCS     DATERR
                JSR     TIMSEP
                JSR     GNUM8          ; its hundredths
                CMP     #100
                BCS     DATERR
                JSR     GETDT          ; the day kept
                LDA     TM
                JSR     BINBCD
                STA     DAT+2
                LDA     TD
                JSR     BINBCD
                STA     DAT+3
                BRA     SETDT

; TMSTR: "=" and a string formula (SYNCHR EQULTK, FRMEVL, FRESTR): TSP its
; text, TSN its length; a null string Illegal function call.
TMSTR:          JSR     SYNCHR
                .byte   EQULTK
                JSR     FRMEVL
                JSR     FRESTR         ; BX: the descriptor
                LDA     (RBL)
                BEQ     DATERR
                STA     TSN
                LDY     #1
                LDA     (RBL),Y
                STA     TSP
                INY
                LDA     (RBL),Y
                STA     TSP+1
                RTS
; DATSEP: a / or a -, else (the end too) Illegal function call. TIMSEP: a :
; or a ., or the end.
DATSEP:         LDA     TSN
                BEQ     DATERR
                JSR     TCH
                CMP     #'/'
                BEQ     TNEXT
                CMP     #'-'
                BEQ     TNEXT
                BRA     DATERR
TIMSEP:         LDA     TSN
                BEQ     TSXIT
                JSR     TCH
                CMP     #':'
                BEQ     TNEXT
                CMP     #'.'
                JNE     DATERR
TNEXT:          DEC     TSN
                INCW    TSP
TSXIT:          RTS
TCH:            LDA     TSP
                STA     PTR2
                LDA     TSP+1
                STA     PTR2+1
                LDA     (PTR2)
                RTS
; GNUM8: GNUM16 at most 255, in A. GNUM16: T0-T1 the digits at TSP (none 0),
; past 65,535 Illegal function call.
GNUM8:          JSR     GNUM16
                LDA     T1
                JNE     DATERR
                LDA     T0
                RTS
GNUM16:         STZ     T0
                STZ     T1
@l:             LDA     TSN
                BEQ     @x
                JSR     TCH
                SEC
                SBC     #'0'
                CMP     #10
                BCS     @x
                PHA                    ; T0-T1 x 10 + the digit
                LDA     T0
                STA     T2
                LDA     T1
                STA     T3
                LDX     #9
:               CLC
                LDA     T0
                ADC     T2
                STA     T0
                LDA     T1
                ADC     T3
                STA     T1
                BCS     @ov
                DEX
                BNE     :-
                PLA
                CLC
                ADC     T0
                STA     T0
                BCC     :+
                INC     T1
                BEQ     @ov1
:               JSR     TNEXT
                BRA     @l
@ov:            PLA
@ov1:           JMP     FCERR
@x:             RTS

; --- ON TIMER ------------------------------------------------------------------------------

; TIMSET: ON TIMER(n)'s n in the FAC: 1-86,400, else Illegal function call;
; TIMN n.
TIMSET:         JSR     FRCSNG
                BIT     FACM1          ; negative
                BMI     @fc
                JSR     QINT           ; BL:DX
                LDA     RDL            ; 0
                ORA     RDH
                ORA     RBL
                BEQ     @fc
                LDA     RBL            ; past 86,400 ($015180)
                CMP     #$01
                BCC     @ok
                BNE     @fc
                LDA     RDH
                CMP     #$51
                BCC     @ok
                BNE     @fc
                LDA     RDL
                CMP     #$81
                BCS     @fc
@ok:            LDA     TIMON          ; (review round 1, C3: the hook off while TIMN
                PHA                    ; is written)
                BEQ     :+
                JSR     TIMOFP
:               LDA     RDL
                STA     TIMN
                LDA     RDH
                STA     TIMN+1
                LDA     RBL
                STA     TIMN+2
                PLA                    ; C2 (the manual's p. 217): with TIMER on, the
                BEQ     :+             ; count starts again from the new n
                JMP     TIMONP
:               RTS
@fc:            JMP     FCERR

; TIMONP (the owner's answer 23): TIMER ON: the countdown from n (TIMN) and
; the resident TIMHK set as OOLIB's tick hook, every 1,000 ms (FT_TICKHOOK),
; TIMON its flag. TIMOFP: the hook taken off (TIMER OFF, CLEARC, SYSTEM).
TIMONP:         JSR     TIMOFP         ; (review round 1, C3: the hook off while
                LDX     #2             ; TIMCNT is written, and a request given back)
:               LDA     TIMN,X
                STA     TIMCNT,X
                DEX
                BPL     :-
                LDA     #<TIMHB
                LDX     #>TIMHB
                LDY     #O_FRAME
                JSR     BDOS
                LDA     #1
                STA     TIMON
                RTS
TIMOFP:         LDA     #FT_TICKHOOK   ; (no block: the hook off)
                LDX     #0
                LDY     #O_FRAME
                JSR     BDOS
                STZ     TIMON
                LDA     TIMREQ         ; (C1: a request not yet taken: its ONGSBF given
                BEQ     :+             ; back)
                STZ     TIMREQ
                LDA     ONGSBF
                BEQ     :+
                DEC     ONGSBF
:               RTS

.rodata
TIMHB:          .byte   FT_TICKHOOK, <TIMHK, >TIMHK, <1000, >1000 ; function 80: the hook, every 1,000 ms
DAYSPM:         .byte   31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31

.segment "BRAM"
DAT:            .res    4              ; function 105's and 104's: the day, the hour, the minute
TSS:            .res    1              ; the second (binary)
TSEC:           .res    3              ; the seconds since midnight
TLAST:          .res    3              ; TIMER's last second ($FF: none)
TMS0:           .res    4              ; and the ms counter when TIMER first saw it
TY:             .res    2              ; the year,
TM:             .res    1              ;   the month, and the hour (TIMESP's)
TD:             .res    1              ;   the day, and the minute
TSP:            .res    2              ; DATE$= and TIME$='s string
TSN:            .res    1              ;   and its bytes left

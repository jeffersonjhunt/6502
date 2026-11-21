; Partly derived from Microsoft GW-BASIC (1983).
; Copyright (c) Microsoft Corporation. MIT licence, see LICENSE.
;
; GW-BASIC's OEM layer for the console, 7a's (hw/oodos.md, "The plan, phase
; 7": 7a, the console through the BDOS). The release has no OEM code
; (hw/oodos.md, "GW-BASIC"), so most of this is the project's own. But
; OUTDO, LPOUT1, CRDO, CRDONZ, CRFIN, FINLPT, FINPRT, ISFLIO, SCNSEM, ITSCTC and
; CNTCCN are translated from GIO86.ASM, GIOLPT.ASM, BIMISC.ASM, SCNEDT.ASM and
; GIOKYB.ASM (ledger.txt), and are Microsoft's, under its MIT licence. It
; stands where GIO86.ASM's device dispatch, SCNEDT.ASM's screen editor and
; GIOKYB.ASM's keyboard are on the PC, and keeps their entries' contracts:
;
;   OUTDO     A to the current output (PTRFIL): the console by function 2,
;             or LPT1 by LST: (function 5); A, X, Y and the flags kept.
;             TTYPOS and LPTPOS are the columns: at the width (LINLEN, 80)
;             the console wraps itself (OODOS's CRT does, as the PC's screen
;             does). LPT1 is GIOLPT.ASM's LPTSOT: a printable character at
;             the width goes after a CR LF (CRIFEL); only printable
;             characters move its column, a CR to 0, a BS back (UPDPOS: a
;             TAB doesn't); a CR is followed by an LF, and an LF straight
;             after a CR is dropped (LPOUT1). $FF, which GW-BASIC sends
;             after a message to mark the line "not typed", counts a column
;             and isn't sent (the PC shows it as a blank). A disk file's
;             byte (7c) goes into RUNBUF for P4, its column in FILPOS; a
;             device's file's (FILDVC) goes to P4 at once (DEVSOT)
;   CRDO CRDONZ CRFIN  CR LF; CR LF unless at the margin; A = 0 (CRDONZ:
;                      the console's or LPT1's; no file's comes here yet)
;   FINLPT FINPRT      LPT1 to its margin; output back to the console, a
;                      file's run sent and a half-opened FDB freed first
;   ISFLIO             Z clear when output isn't the console
;   PINLIN INLIN       a line into BUF by function 10, BUF-1 returned in
;   QINLIN SINLIN      TXTPTR, as GW-BASIC's buffer: the characters, then a
;                      zero; a typed line's trailing spaces dropped. Keys
;                      queued by POLKEY, and a line ERREDT put in BUF
;                      (PRELOD), are given to function 10 as typed first.
;                      A ^C in the line is GW-BASIC's break: the line is
;                      dropped, as POLKEY's ^C (function 10 takes a ^C as a
;                      character, with bit 3 of the console mode, so it acts
;                      when the line is ended)
;   SCNSEM             INPUT's ";": no new line after its line
;   POLKEY CHKINT      the keys typed while BASIC runs: function 11 says one
;                      waits, function 6 reads it. ^C breaks (ITSCTC, as
;                      GIOKYB.ASM's); ^S waits for another key; the others
;                      go in the keyboard queue for INKEY$ and the next
;                      line. CHKINT, NEWSTT's, looks once in POLN
;                      statements, so a statement doesn't pay a BDOS call
;   CHSNS INKEY        INKEY$: a key from the queue, or the null string
;   SYSTEM SYSTME      the console mode put back, the keys typed ahead
;                      drained (X2), and the warm boot
;   FARMISS            a far call made before the loader patched it: can't
;                      happen once loaded, so it says so and ends

.include "gwbasic.inc"

.export OUTDO, CRDO, CRDONZ, CRFIN, FINLPT, FINPRT, ISFLIO, LINLEN
.export PINLIN, QINLIN, INLIN, SINLIN, SCNSEM, SEMFLG, PRELOD, BUF, BUFMIN, KBUF
.export POLKEY, CHKINT, CHSNS, KYBINI, INKEY, SYSTEM, SYSTME, OLDMOD, FARMISS
.export TIMHK, TIMN, TIMCNT, TIMREQ, TIMON
.export RUNBUF, FILOPN, FREFDB, FILWID, FILDVC, LPCRF, KYBQIN, KYBQOUT, LSTCHR, SCRMOD
.export PKQUE, EVAREA, TXBUF, TXLEN, F_SPOS, TXSYNC, STRTAB, GFLG
.import STROUT, CHRGTR, STRIN1, SETSTR, DSEGZ, STOPRG, CLSALL, STRINI, PUTNEW, DSCTMP, NOTSPK, SNGFLT

        OODOS_HEADER

.import LOAD
.segment "STARTUP"
                JMP     LOAD           ; $0300: the loader, once

POLN            = 16                   ; CHKINT: statements between looks at the keys
WIDTH           = 80                   ; the console's and LPT1's width

.code

; --- OUTDO (GIO86.ASM 1038) --------------------------------------------------------------

OUTDO:          PHP
                PHA
                PHX
                PHY
                LDX     #0             ; X: the column's index, 0 the console, 1 LPT1:
                LDY     PTRFIL         ; the console only if both of PTRFIL's bytes
                BNE     @tolpt         ; are 0, as ISFLIO has it (a file's FDB may
                LDY     PTRFIL+1       ; have a low byte of 0)
                BEQ     @con
@tolpt:         LDY     PTRFIL+1       ; a file's FDB is in P4's RAM, in W3's
                CPY     #>BLK_BASE     ; window; LPT1 is $FFFF, and so is any
                BCC     @tolp1         ; other PTRFIL, as 7a had it
                INY
                BEQ     @tolp1
                JMP     @file
@tolp1:         JMP     @lpt
@con:           BIT     SCRMOD         ; screen mode: LSTCHR is P6's PUTC's
                BMI     :+
                STA     LSTCHR         ; (SCRN:'s, p4/gioscn.s SCNOL1: W10)
:               CMP     #' '
                BCC     @ctl
                PHA
                LDA     TTYPOS,X       ; at the width: the console wraps itself,
                CMP     LINLEN,X       ; as the PC's screen does
                BCC     @col
                STZ     TTYPOS,X
@col:           INC     TTYPOS,X
                PLA
                CMP     #$FF           ; the "not typed" mark: a column, not sent
                BNE     @send          ; to the console (to the screen it is: P6's
                BIT     SCRMOD         ; PRTMAP marks the line with it)
                BPL     @done
                BRA     @send
@ctl:           CMP     #13
                BNE     @bs
                STZ     TTYPOS,X
                BRA     @send
@bs:            CMP     #8
                BNE     @tab
                LDY     TTYPOS,X
                BEQ     @send
                DEC     TTYPOS,X
                BRA     @send
@tab:           CMP     #9             ; to the next column of 8
                BNE     @send
                LDA     TTYPOS,X
                ORA     #7
                INC     A
                STA     TTYPOS,X
                LDA     #9
@send:          BIT     SCRMOD
                BMI     @txb
                JSR     @out
@done:          PLY
                PLX
                PLA
                PLP
                RTS
@lpt:           CMP     #' '           ; LPT1 (LPTSOT): CRIFEL, a printable
                BCC     @lpc           ; character at the width after a CR LF
                LDY     LPTPOS
                CPY     LINLEN+1
                BCC     @lpw
                PHA
                LDA     #13
                JSR     LPOUT1
                LDA     #10
                JSR     LPOUT1
                PLA
@lpw:           CMP     #$FF           ; the "not typed" mark: a column, not sent
                BNE     @lpc
                INC     LPTPOS
                BRA     @done
@lpc:           JSR     LPOUT1
                BRA     @done
; A file's byte (7c): the 8086's PTRDSP to the disk's DSKSOT (p4/giodsk.s).
; OO-1: the bytes wait in RUNBUF, and P4 takes them as a run, in one far
; call (FLUSHP) when RUNBUF is full or at FINPRT, or before anything else
; P4 does (hw/oodos.md, V1: the device interface takes runs). The column
; moves as SOUTPS moves F_POS: a CR to 0, a printable byte on one. A
; device's file (LPT1:, COM1:, SCRN:, CONS:: FILDVC, which P4's SETPTR
; sets) keeps its own column and line ends in P4, so its byte goes there at
; once, one far call each (DEVSOT, which sets FILPOS): a printer or a port
; is slower than the call.
@file:          BIT     FILDVC
                BMI     @dev
                LDX     RUNLEN
                STA     RUNBUF,X
                INX
                STX     RUNLEN
                CMP     #13
                BNE     @fp
                STZ     FILPOS
                BRA     @fful
@fp:            CMP     #' '
                BCC     @fful
                INC     FILPOS
@fful:          CPX     #RUNSIZ
                BCC     @done
                FJSR    FLUSHP
                BRA     @done
@dev:           FJSR    DEVSOT
                JMP     @done
; 7d1's screen mode: the byte into TXBUF, which P6 takes as a run (TXRUN,
; one far call) when it is full, at FINPRT, and at every entry of P6's (the
; editor's line, a break), as a file's bytes go to P4. TTYPOS has moved as
; for the console; P6 sets it from its cursor after each run, and the column's
; readers (PTRGPS: PRINT's commas, TAB and its line ends; POS) run TXBUF
; first (TXSYNC), since the screen's control codes move the cursor as OUTDO
; doesn't count.
@txb:           LDX     TXLEN
                STA     TXBUF,X
                INX
                STX     TXLEN
                BPL     @done          ; (TXBUF is 128)
                FJSR    TXRUN
                JMP     @done
@out:           PHX                    ; X: the device
                LDY     #C_WRITE
                CPX     #0
                BEQ     :+
                LDY     #L_WRITE
:               JSR     BDOS
                PLX
                RTS

; LPOUT1 (GIOLPT.ASM): A to LST:, LPTPOS moved as UPDPOS (GIO86.ASM) moves
; it: a printable character on one, a CR to 0, a BS back one, nothing else
; (a TAB, an LF). An LF straight after a CR (LPCRF) is dropped; a CR is
; followed by an LF (LPRINT's and LLIST's pseudo file: always). A, X and Y
; used.
LPOUT1:         CMP     #' '
                BCC     @ctl
                INC     LPTPOS
                BRA     @send
@ctl:           CMP     #13
                BNE     @bs
                STZ     LPTPOS
                BRA     @send
@bs:            CMP     #8
                BNE     @lf
                LDY     LPTPOS
                BEQ     @send
                DEC     LPTPOS
                BRA     @send
@lf:            CMP     #10
                BNE     @send
                BIT     LPCRF          ; after a CR: eaten
                BMI     @was
@send:          PHA
                LDY     #L_WRITE
                JSR     BDOS
                PLA
@was:           STZ     LPCRF
                CMP     #13
                BNE     @x
                LDA     #$80           ; a CR: an LF after it
                STA     LPCRF
                LDA     #10
                LDY     #L_WRITE
                JSR     BDOS
@x:             RTS

; CRDO (GIO86.ASM 1026): CR and LF; A = 0 and Z set. CRDONZ (1013): only if
; the current output isn't at its left margin. CRFIN (1006): A = 0.
CRDONZ:         LDX     #0             ; the device as OUTDO takes it
                JSR     ISFLIO
                BEQ     :+
                INX
:               LDA     TTYPOS,X
                BNE     CRDO
                RTS
CRDO:           LDA     #13
                JSR     OUTDO
                LDA     #10
                JSR     OUTDO
CRFIN:          LDA     #0
                RTS

; FINLPT (GIO86.ASM 2088): LPT1 to its left margin, then FINPRT (2124):
; output to the console. Before it, P4's part (FINPRP, p4/gio86.s), only
; when there is something for it to do: a file's run still in RUNBUF, an
; FDB an OPEN left half made (FREFDB), which it frees, or file 0 open
; (F0OPN, OO-1), which it closes unless a load is under way (NLONLY's bit
; 0: CLSFIL's). While one is, PTRFIL stays at file 0 (RET45). (The cursor
; is 7d's.) A, X, Y used.
FINLPT:         LDA     LPTPOS
                BEQ     FINPRT
                LDA     #$FF
                STA     PTRFIL
                STA     PTRFIL+1
                JSR     CRDO
FINPRT:         JSR     TXSYNC         ; 7d1: the screen's run out first
                LDA     RUNLEN
                ORA     FREFDB+1
                BNE     @far
                LDA     NLONLY         ; loading: nothing to close
                LSR     A
                BCS     @x
                LDA     F0OPN
                BEQ     @clr
@far:           FJSR    FINPRP
                LDA     NLONLY         ; loading: PTRFIL kept
                LSR     A
                BCS     @x
@clr:           STZ     PTRFIL
                STZ     PTRFIL+1
@x:             RTS

; ISFLIO (BIMISC.ASM 922): Z clear when PTRFIL is a file (or LPT1). A is
; used (the 8086 keeps BX, its text pointer, and uses AL's flags alone: a
; PLA here would set Z by A).
ISFLIO:         LDA     PTRFIL
                ORA     PTRFIL+1
                RTS

; --- PINLIN, QINLIN, INLIN (SCNEDT.ASM 132) --------------------------------------------

; PINLIN: a program line or a command (its trailing spaces and line feeds
; dropped). QINLIN: "? " first, INPUT's. INLIN, SINLIN: INPUT's line. Each
; returns TXTPTR at BUF-1 and C clear. (Loading a program, DSKCHI, is 7c's.)
PINLIN:         SEC
                BRA     INLIN0
QINLIN:         LDA     #'?'
                JSR     OUTDO
                LDA     #' '
                JSR     OUTDO
INLIN:
SINLIN:         CLC
INLIN0:         BIT     SCRMOD         ; 7d1: on the screen, P6's editor (C kept)
                BPL     :+
                FJSR    SCINLN
                RTS
:               ROR     F_EDPG         ; bit 7: a program line
                LDX     #0             ; X: the characters given first
                LDA     PRELOD
                BEQ     @queue
                STZ     PRELOD
@pre:           LDA     BUF,X          ; ERREDT's line, to its zero
                BEQ     @queue
                INX
                CPX     #BUFLEN
                BCC     @pre
@queue:         JSR     KYBGET         ; then the keys typed ahead
                BCS     @go
                CPX     #BUFLEN
                BCS     @go
                STA     BUF,X
                INX
                BRA     @queue
@go:            STZ     BUF,X
                LDA     #BUFLEN        ; function 10's buffer: BUF-2 its size, BUF-1 the length
                STA     RBUF
                LDA     #<RBUF         ; at the DMA address, BUF's characters taken first
                LDX     #>RBUF
                LDY     #F_DMAOFF
                JSR     BDOS
                LDA     #0
                LDX     #0
                LDY     #C_READSTR
                JSR     BDOS
                LDX     BUFMIN         ; the length
                STX     T0
                LDA     #','           ; BUFMIN's comma back, for INPUT and READ
                STA     BUFMIN
                STZ     BUF,X
                LDY     #0             ; a ^C in it: the break
@cc:            CPY     T0
                BCS     @nocc
                LDA     BUF,Y
                INY
                CMP     #3
                BNE     @cc
                STZ     SEMFLG
                JMP     ITSCTC
@nocc:          BIT     F_EDPG         ; a program line: no trailing blanks or line feeds
                BPL     INCRTX
@tr:            CPX     #0
                BEQ     @end
                LDA     BUF-1,X
                CMP     #' '
                BEQ     @dx
                CMP     #10
                BNE     @end
@dx:            DEX
                BRA     @tr
@end:           STZ     BUF,X
INCRTX:         STZ     TTYPOS         ; function 10 echoed the CR
                LDA     SEMFLG         ; INPUT; keeps the line
                BNE     INCRTF
                LDA     #10
                JSR     OUTDO
INCRTF:         LDA     #<BUFMIN
                STA     TXTPTR
                LDA     #>BUFMIN
                STA     TXTPTR+1
                STZ     SEMFLG         ; CLRFLG
                STZ     F_EDPG
                CLC
                RTS

; SCNSEM (SCNEDT.ASM 216): INPUT's ";" after the keyword: no new line after
; the line typed.
SCNSEM:         CMP     #';'
                BNE     :+
                STA     SEMFLG
                JMP     CHRGTR
:               RTS

; --- POLKEY, CHKINT (GIOKYB.ASM 346; GWSTS.ASM 996) ---------------------------------------

; CHKINT: NEWSTT's look at the keys, every POLN statements; in screen
; mode (7d1) EVPOLL's at every statement (A, X, Y and the flags not kept:
; NEWSTT needs none).
CHKINT:         BIT     SCRMOD
                BPL     :+
                JMP     EVPOLL
:                DEC     POLCNT
                BEQ     :+
                RTS
:               PHA
                LDA     #POLN
                STA     POLCNT
                PLA

; POLKEY: every key waiting read; ^C breaks, ^S waits for a key that isn't
; ^S (and that key is dropped, or breaks if it is ^C), the rest queued.
; A, X, Y and the flags kept.
POLKEY:         PHP
                PHA
                PHX
                PHY
                BIT     SCRMOD         ; 7d1: the event queue, what was printed
                BPL     GETKLP         ; on the screen first (a wait for a key
                JSR     TXSYNC         ; may follow: INPUT$, INKEY$)
                JSR     EVPOLL
                BRA     POLKYX
GETKLP:         LDY     #C_STAT
                JSR     BDOS
                CMP     #0
                BEQ     POLKYX
                LDA     #$FF
                LDY     #C_RAWIO
                JSR     BDOS
                CMP     #3
                BEQ     ITSCTC
                CMP     #$13
                BEQ     GOTCTS
                JSR     PKQUE
                BRA     GETKLP
GOTCTS:         LDA     #$FD           ; CNTCCN's pause: the next key
                LDY     #C_RAWIO
                JSR     BDOS
                CMP     #$13
                BEQ     GOTCTS
                CMP     #3
                BEQ     ITSCTC
                BRA     GETKLP
POLKYX:         PLY
                PLX
                PLA
                PLP
                RTS


; ITSCTC: the break. The keyboard queue emptied, both stacks and the text
; pointer as the statement began (SAVSTK, SAVTXT); then CNTCCN, which goes
; to STOP as GW-BASIC's does: "Break in 20", or in direct mode nothing.
; (The music's reset, SNDRST, is 7f's.)
ITSCTC:         FJSR    SNDRST         ; 7f: the music stopped (POLKEY's SNDRST)
                JSR     KYBINI
                LDX     SAVSTK
                TXS
                LDA     SAVSTK+1
                STA     SSP
                LDA     SAVSTK+2
                STA     SSP+1
                LDA     SAVTXT
                STA     TXTPTR
                LDA     SAVTXT+1
                STA     TXTPTR+1
                JSR     CNTCCN         ; (its return, popped by STOP's CONPOP)
CNTCCN:         BIT     SCRMOD         ; EDTBRK: to a new line, AUTO ended
                BPL     @con
                FJSR    EDTBRK         ; (7d1: P6's, past the logical line)
                BRA     :+
@con:           JSR     CRDO
:               STZ     AUTFLG
                JSR     FINPRT
                LDA     CURLIN         ; $FF in direct mode: STOPRG makes it 0, no message
                AND     CURLIN+1
                JMP     STOPRG

; The keyboard queue (GIOKYB.ASM's KYBQDS): PKQUE puts A in (dropped if
; full), KYBGET takes one (C set if none), KYBINI empties it and then, as
; GIOKYB.ASM's does, FINPRT.
PKQUE:          PHX
                PHA
                LDX     KYBQIN
                TXA
                INC     A
                AND     #KYBQLN - 1
                CMP     KYBQOUT
                BEQ     @full
                STA     KYBQIN
                PLA
                STA     KYBQ,X
                PLX
                RTS
@full:          PLA
                PLX
                RTS
KYBGET:         PHY
                LDY     KYBQOUT
                CPY     KYBQIN
                SEC
                BEQ     @r
                LDA     KYBQ,Y
                PHA
                TYA
                INC     A
                AND     #KYBQLN - 1
                STA     KYBQOUT
                PLA
                CLC
@r:             PLY
                RTS
KYBINI:         STZ     KYBQIN         ; INITQ
                STZ     KYBQOUT
                JMP     FINPRT         ; output to the console again (W12: a
                                       ; break's new line goes to the screen)

; TXSYNC (7d1): the screen's bytes waiting in TXBUF run, if any (TXRUN, one
; far call: TTYPOS then P6's cursor). TXLEN is 0 in console mode. A, X, Y
; and the flags used.
TXSYNC:         LDA     TXLEN
                BEQ     :+
                FJSR    TXRUN
:               RTS

; TIMHK (7f, the owner's answer 23): ON TIMER's tick hook, OOLIB's (function
; 80's FT_TICKHOOK, every 1,000 ms, set and taken off by P7's TIMONP and
; TIMOFP): resident, as a hook must be (no far call, no BDOS, interrupts off,
; within its 300 cycles). TIMCNT, the seconds left, counted down; at 0 the
; trap asked for (TIMREQ, ONGSBF one up, so NEWSTT's poll goes to P3's GOTRP,
; which turns TIMREQ into TIMER's request: P3's EVTRP) and the count from
; TIMN again.
TIMHK:          LDX     #0             ; TIMCNT less 1, 24 bits
@d:             DEC     TIMCNT,X
                LDA     TIMCNT,X
                CMP     #$FF           ; a borrow: the next byte
                BNE     @z
                INX
                CPX     #3
                BNE     @d
@z:             LDA     TIMCNT
                ORA     TIMCNT+1
                ORA     TIMCNT+2
                BNE     @x
                LDX     #2             ; run out: from TIMN again
@r:             LDA     TIMN,X
                STA     TIMCNT,X
                DEX
                BPL     @r
                INC     TIMREQ         ; the trap asked for
                INC     ONGSBF
@x:             RTS

; EVPOLL (7d1, hw/oodos.md, "The OEM layer": V2): the event queue (layout 2,
; EVAREA) taken from its tail, in order. A key pressed goes to P6's KEYEV,
; one far call, which queues its code in KYBQ; a break it reports goes to
; ITSCTC. 7f: every other event goes to P7's EVP7, one far call, Y its
; slot's offset in the queue (EVAREA + EV_QUEUE + Y), which takes a note's
; end (EK_SOUND: SNDEV) and a controller's change (EK_CTL: STREV) and drops
; the rest; a key's release is dropped here. The repeat (the
; owner's answer 8): the last key KEYEV says repeats, while it is held
; (EV_KEYS), again 500 ms after its press and every 33 ms after that, by
; EV_MS. A, X, Y and the flags used.
EVPOLL:         LDX     EVAREA + EV_QTAIL
                CPX     EVAREA + EV_QHEAD
                BNE     EVTAKE
                LDA     RPTKEY
                BNE     EVRPT
                RTS
EVTAKE:         TXA
                ASL     A
                ASL     A
                ASL     A
                TAY
                INX
                CPX     EVAREA + EV_QSLOTS
                BCC     :+
                LDX     #0
:               LDA     EVAREA + EV_QUEUE + EQ_KIND,Y
                CMP     #EK_KEY
                BNE     @oth
                LDA     EVAREA + EV_QUEUE + EQ_VALUE,Y
                BEQ     @up
                CLC                    ; its repeat's time
                LDA     EVAREA + EV_QUEUE + EQ_MS,Y
                ADC     #<500
                STA     RPTMS
                LDA     EVAREA + EV_QUEUE + EQ_MS + 1,Y
                ADC     #>500
                STA     RPTMS + 1
                LDA     EVAREA + EV_QUEUE + EQ_CODE,Y
                PHA
                LDA     EVAREA + EV_QUEUE + EQ_MODS,Y
                STX     EVAREA + EV_QTAIL
                TAX
                PLA
                STA     RPTKEY
                FJSR    KEYEV
                ASL     A              ; bit 7: the break; bit 6: it repeats
                BCS     @brk
                BMI     EVPOLL
                STZ     RPTKEY
                BRA     EVPOLL
@up:            LDA     EVAREA + EV_QUEUE + EQ_CODE,Y
                CMP     RPTKEY
                BNE     @drop
                STZ     RPTKEY
@drop:          STX     EVAREA + EV_QTAIL
                BRA     EVPOLL
@oth:           STX     EVAREA + EV_QTAIL ; 7f: P7's, Y the slot
                FJSR    EVP7
                JMP     EVPOLL
@brk:           STZ     RPTKEY
                JMP     ITSCTC
EVRPT:          LSR     A              ; still held?
                LSR     A
                LSR     A
                TAX
                LDA     RPTKEY
                AND     #7
                TAY
                LDA     EVAREA + EV_KEYS,X
                AND     BITS,Y
                BEQ     @off
                SEC                    ; its time come?
                LDA     EVAREA + EV_MS
                SBC     RPTMS
                LDA     EVAREA + EV_MS + 1
                SBC     RPTMS + 1
                BMI     @x
                CLC
                LDA     RPTMS
                ADC     #33
                STA     RPTMS
                BCC     :+
                INC     RPTMS + 1
:               LDA     RPTKEY
                LDX     EVAREA + EV_MODS
                FJSR    KEYEV
                ASL     A
                BCS     @brk
@x:             RTS
@off:           STZ     RPTKEY
                RTS
@brk:           STZ     RPTKEY
                JMP     ITSCTC


; --- CHSNS, INKEY$ (GIOKYB.ASM 520, 817) --------------------------------------------------

; CHSNS: the keys polled, then the next from the queue: Z clear and A the
; key, or Z set if none.
CHSNS:          JSR     POLKEY
                JSR     KYBGET
                BCS     @none
                LDX     #1             ; Z clear
                RTS
@none:          LDX     #0
                RTS

; INKEY$: a one-character string of the next key, or the null string (the
; 8086's DSEGZ, a zero in the data). A console key is one byte: the PC's
; two-byte keys and INKMAP are 7d's, with the screen's keyboard.
INKEY:          JSR     CHRGTR
                JSR     CHSNS
                BEQ     NULRT
                CMP     #0             ; 7d1: 0 and a scan code, the PC's two-byte
                BNE     INKEY1         ; key: only the screen's keyboard gives
                BIT     SCRMOD         ; them (in console mode a 0 is one key),
                BPL     INKEY1         ; and only if its scan code is queued
                JSR     KYBGET         ; (else the 0 alone: A still 0)
                BCS     INKEY1
                PHA
                LDA     #2
                JSR     STRINI         ; DSCTMP: its place
                LDA     DSCTMP+1
                STA     RBL
                LDA     DSCTMP+2
                STA     RBH
                LDA     #0
                STA     (RBL)
                LDY     #1
                PLA
                STA     (RBL),Y
                JMP     PUTNEW
INKEY1:         PHA
                JSR     STRIN1
                PLA
                STA     RDL
                JMP     SETSTR
NULRT:          LDA     #<DSEGZ
                STA     FACLO
                LDA     #>DSEGZ
                STA     FACLO+1
                LDA     #VT_STR
                STA     VALTYP
                RTS

; --- POS (GIOSCN.ASM 270) -----------------------------------------------------------------

; F_SPOS: POS(x), the screen's: CSRX (TTYPOS + 1, which OUTDO keeps and P6
; sets), 1 past the line's end. In console mode Illegal function call, as
; before (NOTSPK).
F_SPOS:         BIT     SCRMOD
                BMI     :+
                JMP     NOTSPK
:               JSR     TXSYNC         ; P6's cursor, TXBUF run first
                LDA     TTYPOS
                INC     A
                CMP     LINLEN
                BCC     :+
                BEQ     :+
                LDA     #1
:               JMP     SNGFLT

; --- SYSTEM (GIODSK.ASM 1200) -------------------------------------------------------------

; SYSTEM (nothing may follow it): back to OODOS, every file closed first
; (CLSALL). GWTERM, the OEM's part: the console mode as it was, and what was
; typed ahead drained, so the CCP doesn't get it (X2: while function 11 says
; a key waits, function 6 reads and drops it). (GIOTRM, the devices' end,
; has nothing to do: the disk is the only device.)
SYSTEM:         BEQ     :+
                RTS
:               JSR     CLSALL
SYSTME:         FJSR    SNDINI         ; 7f: the 6510 given back (P7)
                BIT     SCRMOD         ; 7d1: the cooked keys drained under
                BPL     :+             ; GW-BASIC's mode first (with ^S's handling
                JSR     DRAIN          ; off: X2), then again under the old one
:               LDA     OLDMOD
                LDX     OLDMOD+1
                LDY     #C_MODE
                JSR     BDOS
                JSR     DRAIN
                JMP     WBOOT
DRAIN:          LDY     #C_STAT
                JSR     BDOS
                CMP     #0
                BEQ     @x
                LDA     #$FF
                LDY     #C_RAWIO
                JSR     BDOS
                BRA     DRAIN
@x:             RTS

FARMISS:        LDA     #<MMISS
                STA     PTR
                LDA     #>MMISS
                STA     PTR + 1
                JSR     STROUT
                JMP     WBOOT

.rodata
BITS:           .byte   $01, $02, $04, $08, $10, $20, $40, $80
MMISS:          .byte   13, 10, "?far call before GW-BASIC was loaded", 13, 10, 0

.segment "LOWRAM"
KYBQ:           .res    KYBQLN         ; the keyboard queue (hw/oodos.md: in LOWRAM, for 7d's EVPOLL)
KYBQIN:         .res    1
KYBQOUT:        .res    1
; 7d1: which screen GW-BASIC has (hw/oodos.md, 7d1; the owner's answer 2 of
; 2026-11-18), chosen once at start-up by P5's GXSTRT: bit 7 its own text
; layer (a revision 2 card answered GX_INIT and CON:'s output has the CRT),
; else 0, 7a's console mode, exactly as it was. In LOWRAM, since P5 writes it.
SCRMOD:         .byte   0
; 7e4: the graphics' state that more than one block reads (hw/oodos.md,
; 7e4): bit 0 a graphics mode (P5's GMODE), bit 7 a WINDOW on, bit 6 WINDOW
; SCREEN (P1's WINSET; P5's GMODE and GRPRST end them). P5's SCAN1, P1's
; window and P3's CIRCLE test it, so no block reads another's RAM. In LOWRAM,
; since P5 writes it.
GFLG:           .byte   0
.ifndef FLAT
; 7d1's: the event area (layout 2, 256 bytes: 16 slots), which P5's GXSTRT
; opens in screen mode (FT_OPEN, then GX_INIT with it); OUTDO's run for the
; screen (TXBUF, TXLEN: P6's TXRUN takes it); EVPOLL's repeat. Left out of
; the flat image, which never has the screen.
EVAREA:         .res    256
TXBUF:          .res    128
.else
EVAREA          = KBUF
TXBUF           = KBUF
.endif
TXLEN:          .byte   0
RPTKEY:         .byte   0              ; the key repeating, 0 none
RPTMS:          .res    2              ; when (EV_MS's low word)

.segment "RAM"
; 7c's: OUTDO's run for PTRFIL's file (above); FILOPN, the files open,
; which P4 counts (INIFDB, FFREE), so CLSALL makes no far call with none
; (res/far4.s); FREFDB (GWDATA.ASM's), the FDB an OPEN is making, for
; FINPRT to free if an error comes first (P4 sets and clears it).
RUNBUF:         .res    RUNSIZ
FILOPN:         .byte   0
FREFDB:         .word   0
; 7c's devices (P4's SETPTR sets them with PTRFIL, for a file): FILWID the
; file's width (F_WID: 255 a disk's; P1's PTRWDC), FILDVC bit 7 if it is a
; device's (OUTDO, above).
FILWID:         .byte   255
FILDVC:         .byte   0
; STRTAB (GWRAM.ASM's): the ten soft keys' texts, 16 bytes each, a 0 after
; each (at most 15 kept, KEY's), with the PC's defaults (7d2, the owner's
; answers 3 and 8: resident fixed RAM). P6 reads and writes it (KEY, the key
; line, KEYEV's expansion).
.macro  SOFTK   text
        .local  @k
@k:             .byte   text, 0
                .res    16 - (* - @k)
.endmacro
STRTAB:         SOFTK   "LIST"
                SOFTK   {"RUN", 13}
                SOFTK   {"LOAD", '"'}
                SOFTK   {"SAVE", '"'}
                SOFTK   {"CONT", 13}
                SOFTK   {",", '"', "LPT1:", '"', 13}
                SOFTK   {"TRON", 13}
                SOFTK   {"TROFF", 13}
                SOFTK   "KEY "
                SOFTK   {"SCREEN 0,0,0", 13}
.assert * - STRTAB = 160, error, "STRTAB: 10 keys of 16"
; GWDATA.ASM's buffers: function 10 reads into BUF-2, its size and length
; over the byte before BUF; BUFMIN, a comma for INPUT and READ; BUF, a line
; typed (direct statements run from KBUF, which CRUNCH fills); KBUF-1 a
; colon, as GW-BASIC's.
RBUF:           .byte   BUFLEN
BUFMIN:         .byte   ','
BUF:            .res    BUFLEN + 1 + 2
ENDBUF:         .byte   0
                .byte   ':'
KBUF:           .res    KBFLEN
LINLEN:         .byte   WIDTH          ; the console's width (WIDTH, the statement, is 7d's)
                .byte   WIDTH          ; LPT1's
LPCRF:          .byte   0              ; LPOUT1: bit 7, the last byte to LST: a CR
LSTCHR:         .byte   0              ; GWDATA.ASM's: the last byte to the screen (OUTDO's, SCNOL1's)
F_EDPG:         .res    1              ; PINLIN: bit 7 a program line
SEMFLG:         .res    1              ; INPUT;
PRELOD:         .res    1              ; BUF holds a line to edit (ERREDT)
POLCNT:         .byte   POLN           ; CHKINT's count
TIMN:           .res    3              ; 7f: ON TIMER(n)'s n (P7's TIMSET)
TIMCNT:         .res    3              ;   TIMHK's seconds left
TIMREQ:         .res    1              ;   nonzero: TIMHK asks for ON TIMER's trap (P3's GOTRP takes it)
TIMON:          .res    1              ;   nonzero while TIMHK is the tick hook (P7's TIMONP, TIMOFP)
OLDMOD:         .res    2              ; the console mode before GW-BASIC (GWINIT)

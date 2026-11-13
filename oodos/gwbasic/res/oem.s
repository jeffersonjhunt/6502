; GW-BASIC's OEM layer for the console, 7a's (hw/oodos.md, "The plan, phase
; 7": 7a, the console through the BDOS). The project's own, not Microsoft's:
; the release has no OEM code (hw/oodos.md, "GW-BASIC"). It stands where
; GIO86.ASM's device dispatch, SCNEDT.ASM's screen editor and GIOKYB.ASM's
; keyboard are on the PC, and keeps their entries' contracts:
;
;   OUTDO     A to the current output (PTRFIL): the console by function 2,
;             or LPT1 by LST: (function 5); A, X, Y and the flags kept.
;             TTYPOS and LPTPOS are the columns: at the width (LINLEN, 80)
;             the console wraps itself (OODOS's CRT does, as the PC's screen
;             does) and LPT1 is sent CR LF. $FF, which
;             GW-BASIC sends after a message to mark the line "not typed",
;             counts a column and isn't sent (the PC shows it as a blank)
;   CRDO CRDONZ CRFIN  CR LF; CR LF unless at the margin; A = 0
;   FINLPT FINPRT      LPT1 to its margin; output back to the console
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
.import STROUT, CHRGTR, STRIN1, SETSTR, DSEGZ, STOPRG

        OODOS_HEADER

.import LOAD
.segment "STARTUP"
                JMP     LOAD           ; $0300: the loader, once

POLN            = 16                   ; CHKINT: statements between looks at the keys
KYBQLN          = 16                   ; the keyboard queue's size (a power of 2)
WIDTH           = 80                   ; the console's and LPT1's width

.code

; --- OUTDO (GIO86.ASM 1038) --------------------------------------------------------------

OUTDO:          PHP
                PHA
                PHX
                PHY
                LDX     #0             ; X: the column's index, 0 the console, 1 LPT1
                LDY     PTRFIL
                BEQ     :+
                INX
:               CMP     #' '
                BCC     @ctl
                PHA
                LDA     TTYPOS,X       ; at the width: the console wraps itself,
                CMP     LINLEN,X       ; as the PC's screen does; LPT1 is given
                BCC     @col           ; a new line
                STZ     TTYPOS,X
                CPX     #0
                BEQ     @col
                JSR     @crlf
@col:           INC     TTYPOS,X
                PLA
                CMP     #$FF           ; the "not typed" mark: a column, not sent
                BEQ     @done
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
@send:          JSR     @out
@done:          PLY
                PLX
                PLA
                PLP
                RTS
@crlf:          LDA     #13
                JSR     @out
                LDA     #10
                JSR     @out
                STZ     TTYPOS,X
                RTS
@out:           PHX                    ; X: the device
                LDY     #C_WRITE
                CPX     #0
                BEQ     :+
                LDY     #L_WRITE
:               JSR     BDOS
                PLX
                RTS

; CRDO (GIO86.ASM 1026): CR and LF; A = 0 and Z set. CRDONZ (1013): only if
; the current output isn't at its left margin. CRFIN (1006): A = 0.
CRDONZ:         LDX     #0
                LDA     PTRFIL
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

; FINLPT (GIO86.ASM 2088): LPT1 to its left margin, then FINPRT: output to
; the console (and, with the files, 7c, the other I/O flags cleared).
FINLPT:         LDA     LPTPOS
                BEQ     FINPRT
                LDA     #$FF
                STA     PTRFIL
                STA     PTRFIL+1
                JSR     CRDO
FINPRT:         STZ     PTRFIL
                STZ     PTRFIL+1
                RTS

; ISFLIO (BIMISC.ASM 922): Z clear when PTRFIL is a file (or LPT1).
ISFLIO:         PHA
                LDA     PTRFIL
                ORA     PTRFIL+1
                PLA
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
INLIN0:         ROR     F_EDPG         ; bit 7: a program line
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

; CHKINT: NEWSTT's look at the keys, every POLN statements.
CHKINT:         DEC     POLCNT
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
ITSCTC:         JSR     KYBINI
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
CNTCCN:         JSR     CRDO           ; EDTBRK: to a new line (AUTO's reset is 7b's)
                JSR     FINPRT
                LDA     CURLIN         ; $FF in direct mode: STOPRG makes it 0, no message
                AND     CURLIN+1
                JMP     STOPRG

; The keyboard queue (GIOKYB.ASM's KYBQDS): PKQUE puts A in (dropped if
; full), KYBGET takes one (C set if none), KYBINI empties it.
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
KYBINI:         STZ     KYBQIN
                STZ     KYBQOUT
                RTS

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

; --- SYSTEM (GIODSK.ASM 1200) -------------------------------------------------------------

; SYSTEM (nothing may follow it): back to OODOS. GWTERM, the OEM's part:
; the console mode as it was, and what was typed ahead drained, so the CCP
; doesn't get it (X2: while function 11 says a key waits, function 6 reads
; and drops it). (CLSALL, the files, is 7c's.)
SYSTEM:         BEQ     SYSTME
                RTS
SYSTME:         LDA     OLDMOD
                LDX     OLDMOD+1
                LDY     #C_MODE
                JSR     BDOS
@drain:         LDY     #C_STAT
                JSR     BDOS
                CMP     #0
                BEQ     @boot
                LDA     #$FF
                LDY     #C_RAWIO
                JSR     BDOS
                BRA     @drain
@boot:          JMP     WBOOT

FARMISS:        LDA     #<MMISS
                STA     PTR
                LDA     #>MMISS
                STA     PTR + 1
                JSR     STROUT
                JMP     WBOOT

.rodata
MMISS:          .byte   13, 10, "?far call before GW-BASIC was loaded", 13, 10, 0

.segment "LOWRAM"
KYBQ:           .res    KYBQLN         ; the keyboard queue (hw/oodos.md: in LOWRAM, for 7d's EVPOLL)
KYBQIN:         .res    1
KYBQOUT:        .res    1

.segment "RAM"
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
F_EDPG:         .res    1              ; PINLIN: bit 7 a program line
SEMFLG:         .res    1              ; INPUT;
PRELOD:         .res    1              ; BUF holds a line to edit (ERREDT)
POLCNT:         .byte   POLN           ; CHKINT's count
OLDMOD:         .res    2              ; the console mode before GW-BASIC (GWINIT)

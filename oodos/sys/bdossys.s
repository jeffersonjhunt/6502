; OODOS's BDOS, the system extras (hw/oodos.md, "The BDOS, phase 3":
; 3b2), in block 60 with bdos.s, whose dispatch comes here. Written from
; the CP/M 3 Programmer's Guide's descriptions (2.2, section 3, Appendix A).
;
;    1  C_READ      A = a console key, echoed if graphic, CR, LF, BS or tab
;    2  C_WRITE     A to the console
;    3  A_READ      A = an AUX: key (the BIOS's AUXIN)
;    4  A_WRITE     A to AUX:
;    5  L_WRITE     A to LST:
;    6  C_RAWIO     $FF a key or 0, $FE $FF or 0, $FD a key waited for, else A
;                   out as it is
;    7  A_STATIN    $FF an AUX: key waits, 0 not
;    8  A_STATOUT   $FF AUX: is ready, 0 not
;    9  C_WRITESTR  the string at A/X, to the delimiter (110)
;   10  C_READSTR   a line, edited as the banked CP/M 3's (below)
;   11  C_STAT      1 a key waits, 0 none; with console mode bit 0, 1 for a ^C
;   47  P_CHAIN     the command at $0280, to its 0, for the CCP; A = $FF with
;                   the program's drive and user. The warm boot
;   49  S_SCB       the SCB parameter block at A/X: +0 the offset, +1 0 get,
;                   $FF set a byte, $FE a word, +2-3 the value. A get returns
;                   the word in A/X; past $63, A = X = 0 and nothing done
;  107  S_SERIAL    OODOS's serial number to the 6 bytes at A/X
;  108  P_CODE      A/X = $FFFF: the program return code in A/X; else set it
;  109  C_MODE      A/X = $FFFF: the console mode in A/X; else set it
;  110  C_DELIMIT   A/X = $FFFF: the delimiter in A; else set it to A
;  111  C_WRITEBLK  the CCB at A/X (address, length): its characters as 2's
;  112  L_WRITEBLK  the CCB's characters to LST:, as they are
;  152  F_PARSE     the PFCB at A/X (the string's address, the FCB's): the
;                   name parsed into the FCB; A/X 0 the string's end, the
;                   address of what follows, or $FFFF an error
;
; The console mode (SCB $33): bit 0 11 reports only ^C; 1 no ^S, ^Q, ^P
; handling; 2 raw output (no tabs, no printer echo) for 2, 9, 111; 3 no ^C
; ending a program. Before each character 2, 9 and 111 write, the console
; is looked at (unless bit 1): ^S stops until ^Q, a ^Q or ^P alone is
; dropped, another key is kept, one, for the next input. Printer echo (SCB
; $38, ^P) sends what the console gets to LST: too.
;
; Function 10's line is LINEBUF, the previous line PREVBUF, in the system
; block; the screen shows it on rows of the console width, the cursor's
; column always the SCB's column (OUT keeps it).

.setcpu "65C02"
.include "bdos.inc"
.import WARM

ASCII_CTLC      = $03
ASCII_BEL       = $07
ASCII_BS        = $08
ASCII_TAB       = $09
ASCII_LF        = $0A
ASCII_CR        = $0D
ASCII_CTLP      = $10
ASCII_CTLQ      = $11
ASCII_CTLS      = $13
ASCII_DEL       = $7F
ASCII_CTLZ      = $1A
LINE_LEN        = 255

.segment "SYSBSS"
KEPTF:          .res    1              ; 1: a key is kept, KEPTC
KEPTC:          .res    1
ORAW:           .res    1              ; OUT: non-zero raw (no tabs, no printer echo)
BLEFT:          .res    2              ; 111 and 112: the characters left
LBUFP:          .res    2              ; function 10: the program's buffer
LSRCP:          .res    2              ;   its initial string's next byte
LINIT:          .res    1              ;   1 while the initial string is taken
LCNT:           .res    1              ;   its bytes taken
LMAX:           .res    1              ;   the buffer's size, 1-255
LN:             .res    1              ;   the characters in the line
LP:             .res    1              ;   the cursor, 0-LN
LV:             .res    1              ;   the first shown on this row
LDRAWN:         .res    1              ;   the column the row's shown characters end at
LCOLP:          .res    1              ;   a column, for SHOW
LT:             .res    1              ;   a target index
PLEN:           .res    1              ; the previous line's length
PSTR:           .res    2              ; 152: the string
PFCB:           .res    2              ;   the FCB
PIDX:           .res    1              ;   the index in the string
PEND:           .res    1              ;   where the file specification ended

.segment "BDOS"

; ---------------------------------------------------------------------------
; Characters

F_READ:         JSR     GETKEY
                JSR     FILTER
                BCS     F_READ         ; ^S, ^Q, ^P: handled, not returned
                PHA
                CMP     #' '
                BCS     @echo
                CMP     #ASCII_CR
                BEQ     @echo
                CMP     #ASCII_LF
                BEQ     @echo
                CMP     #ASCII_BS
                BEQ     @echo
                CMP     #ASCII_TAB
                BNE     @done
@echo:          JSR     OUT
@done:          PLA
                LDX     #0
                RTS

F_WRITE:        JSR     COUT
                BRA     CZERO

F_AREAD:        JSR     B_AUXIN
                LDX     #0
                RTS

F_AWRITE:       JSR     B_AUXOUT
                BRA     CZERO

F_LWRITE:       JSR     B_LIST
                BRA     CZERO

F_AISTAT:       JSR     B_AUXIST
                LDX     #0
                RTS

F_AOSTAT:       JSR     B_AUXOST
                LDX     #0
                RTS

F_RAWIO:        CMP     #$FF
                BNE     @status
                LDA     KEPTF          ; a key or 0
                BNE     @key
                JSR     B_CONST
                BEQ     CZERO
@key:           JSR     GETKEY
                LDX     #0
                RTS
@status:        CMP     #$FE
                BNE     @input
                LDA     KEPTF
                BEQ     @bios
                LDA     #$FF
                LDX     #0
                RTS
@bios:          JSR     B_CONST
                LDX     #0
                RTS
@input:         CMP     #$FD
                BEQ     @key           ; a key, waited for
                JSR     B_CONOUT       ; out as it is: no column, no handling
CZERO:          LDA     #0
                TAX
                RTS

F_STRING:       JSR     PGET
                CMP     SCB_DELIM
                BEQ     CZERO
                JSR     COUT
                BRA     F_STRING

F_STAT:         LDA     SCB_CONMODE
                LSR     A              ; bit 0: ^C only
                BCS     @ctlc
                AND     #CM_NOSTOP >> 1
                BNE     @raw
                LDA     KEPTF
                BNE     @yes
                JSR     B_CONST
                BEQ     CZERO
                JSR     B_CONIN
                JSR     FILTER
                BCS     CZERO          ; ^S (stopped, then gone on), ^Q, ^P
                JSR     KEEP
@yes:           LDA     #1
                LDX     #0
                RTS
@raw:           LDA     KEPTF          ; the BIOS's status, nothing read
                BNE     @yes
                JSR     B_CONST
                BEQ     CZERO
                BRA     @yes
@ctlc:          LDA     KEPTF          ; keys read, to a ^C, which is taken
                BNE     @read
                JSR     B_CONST
                BEQ     CZERO
@read:          JSR     GETKEY
                JSR     FILTER
                BCS     @ctlc
                CMP     #ASCII_CTLC
                BEQ     @yes
                BRA     @ctlc          ; others dropped

; 111: the CCB at A/X, its characters as function 2 writes them.
F_PBLOCK:       JSR     CCB
@char:          JSR     BNEXT
                BCS     CZERO
                JSR     COUT
                BRA     @char

; 112: the CCB's characters to LST:, as they are.
F_LBLOCK:       JSR     CCB
@char:          JSR     BNEXT
                BCS     CZERO
                JSR     B_LIST
                BRA     @char

; The CCB at PP: PP its string, BLEFT its length.
CCB:            JSR     PGET
                PHA
                JSR     PGET
                PHA
                JSR     PGET
                STA     BLEFT
                JSR     PGET
                STA     BLEFT + 1
                PLA
                STA     PP + 1
                PLA
                STA     PP
                RTS
; The block's next character in A; carry set if none is left.
BNEXT:          LDA     BLEFT
                ORA     BLEFT + 1
                BEQ     @none
                LDA     BLEFT
                BNE     @low
                DEC     BLEFT + 1
@low:           DEC     BLEFT
                JSR     PGET
                CLC
                RTS
@none:          SEC
                RTS

; A key: the kept one first, else the BIOS's, waited for.
GETKEY:         LDA     KEPTF
                BEQ     @bios
                STZ     KEPTF
                LDA     KEPTC
                RTS
@bios:          JMP     B_CONIN

; A kept for the next input function.
KEEP:           STA     KEPTC
                LDA     #1
                STA     KEPTF
                LDA     KEPTC
                RTS

; A key as the basic functions take it (the Guide, 2.2.1), unless console
; mode bit 1: ^S stops output until ^Q, ^Q and ^P alone are dropped. Carry
; set: handled (A gone), clear: A is the key.
FILTER:         TAX
                LDA     SCB_CONMODE
                AND     #CM_NOSTOP
                BNE     @key
                TXA
                CMP     #ASCII_CTLS
                BEQ     @stop
                CMP     #ASCII_CTLQ
                BEQ     @drop
                CMP     #ASCII_CTLP
                BEQ     @drop
@key:           TXA
                CLC
                RTS
@stop:          JSR     STOPPED
@drop:          SEC
                RTS

; Stopped by ^S: ^Q goes on; ^C is the warm boot (unless console mode bit
; 3); ^P turns printer echo over and stays stopped; any other key is a BEL.
STOPPED:        JSR     B_CONIN
                CMP     #ASCII_CTLQ
                BEQ     @go
                CMP     #ASCII_CTLC
                BNE     @ctlp
                LDA     SCB_CONMODE
                AND     #CM_NOCTLC
                BEQ     CTLCEND
                BRA     @bel
@ctlp:          CMP     #ASCII_CTLP
                BNE     @bel
                JSR     TOGGLEP
                BRA     STOPPED
@bel:           LDA     #ASCII_BEL
                JSR     B_CONOUT
                BRA     STOPPED
@go:            RTS

; ^P: printer echo on (with a BEL) or off.
TOGGLEP:        LDA     SCB_LISTFLAG
                BEQ     @on
                STZ     SCB_LISTFLAG
                RTS
@on:            LDA     #1
                STA     SCB_LISTFLAG
                LDA     #ASCII_BEL
                JMP     B_CONOUT

; The program ended by a ^C: the return code $FFFE (the Guide, 1.6.3).
CTLCEND:        LDA     #$FE
                STA     SCB_RETCODE
                LDA     #$FF
                STA     SCB_RETCODE + 1
                JMP     WARM

; A to the console as 2, 9 and 111 write: the console looked at first
; (unless console mode bit 1), raw with bit 2.
COUT:           PHA
                LDA     SCB_CONMODE
                AND     #CM_NOSTOP
                BNE     @raw
                LDA     KEPTF          ; one kept: not looked at again
                BNE     @raw
                JSR     B_CONST
                BEQ     @raw
                JSR     B_CONIN
                JSR     FILTER
                BCS     @raw
                JSR     KEEP
@raw:           LDA     SCB_CONMODE
                AND     #CM_RAW
                STA     ORAW
                PLA
                BRA     OUTX

; A to the console, the SCB's column kept: a tab to the next eighth column,
; and to LST: too while printer echo is on. OUTX: ORAW set by the caller.
OUT:            STZ     ORAW
OUTX:           CMP     #ASCII_TAB
                BNE     @char
                LDX     ORAW
                BNE     @char
@tab:           LDA     #' '
                JSR     @char
                LDA     SCB_COLUMN
                AND     #7
                BNE     @tab
                RTS
@char:          PHA
                CMP     #' '
                BCS     @print
                CMP     #ASCII_CR
                BNE     @bs
                STZ     SCB_COLUMN
                BRA     @out
@bs:            CMP     #ASCII_BS
                BNE     @out
                LDA     SCB_COLUMN
                BEQ     @out
                DEC     SCB_COLUMN
                BRA     @out
@print:         INC     SCB_COLUMN
@out:           PLA
                PHA
                JSR     B_CONOUT
                PLA
                LDX     ORAW
                BNE     @done
                LDX     SCB_LISTFLAG
                BEQ     @done
                JMP     B_LIST
@done:          RTS

; ---------------------------------------------------------------------------
; Function 10: the banked CP/M 3's line editing (the Guide, Table 3-3)

F_LINE:         LDA     PA
                ORA     PX
                CMP     #1             ; carry clear: A/X 0, the buffer at the DMA
                LDA     #0             ; address and its string taken first
                ROL     A
                EOR     #1
                STA     LINIT
                BEQ     @buffer
                LDA     SCB_DMA
                STA     PP
                LDA     SCB_DMA + 1
                STA     PP + 1
@buffer:        LDA     PP
                STA     LBUFP
                LDA     PP + 1
                STA     LBUFP + 1
                JSR     PGET           ; +0 the size; 0 is 1
                CMP     #0
                BNE     @size
                LDA     #1
@size:          STA     LMAX
                CLC                    ; the initial string, from +2
                LDA     LBUFP
                ADC     #2
                STA     LSRCP
                LDA     LBUFP + 1
                ADC     #0
                STA     LSRCP + 1
                STZ     LCNT
                STZ     LN
                STZ     LP
                STZ     LV
                LDA     SCB_COLUMN
                STA     LDRAWN
LOOP:           JSR     LKEY
                CMP     #' '
                BCC     @control
                CMP     #ASCII_DEL
                BNE     INSERT
@control:       LDX     #LCTL_LEN - 1
@find:          CMP     LCTL,X
                BEQ     @known
                DEX
                BPL     @find
                BRA     INSERT         ; any other: a character of the line
@known:         TXA
                ASL     A
                TAX
                JMP     (LJUMPS,X)

LCTL:           .byte   $01, $02, $03, $05, $06, $07, $08, $0A, $0B, $0D, $10, $12, $15, $17, $18, $7F
LCTL_LEN        = * - LCTL
LJUMPS:         .word   L_LEFT, L_HOME, L_CTLC, L_ROW, L_RIGHT, L_DELCUR, L_CTLH, L_END
                .word   L_KILL, L_END, L_PRINT, L_RETYPE, L_CANCEL, L_RECALL, L_DELLEFT, L_DEL
.assert * - LJUMPS = 2 * LCTL_LEN, error, "a handler for each of LCTL"

; The next key: the initial string's, to its 0 or the buffer's size, then
; the console's.
LKEY:           LDA     LINIT
                BEQ     @console
                LDA     LCNT
                CMP     LMAX
                BCS     @done
                LDA     LSRCP
                STA     PP
                LDA     LSRCP + 1
                STA     PP + 1
                JSR     PGET
                LDX     PP
                STX     LSRCP
                LDX     PP + 1
                STX     LSRCP + 1
                INC     LCNT
                CMP     #0
                BNE     @key
@done:          STZ     LINIT
@console:       JMP     GETKEY
@key:           RTS

; A key into the line at the cursor; a BEL if it's full.
INSERT:         LDX     LN
                CPX     LMAX
                BCC     @room
                LDA     #ASCII_BEL
                JSR     B_CONOUT
                JMP     LOOP
@room:          PHA
@shift:         CPX     LP             ; LP..LN-1 up one
                BEQ     @put
                LDA     LINEBUF - 1,X
                STA     LINEBUF,X
                DEX
                BRA     @shift
@put:           PLA
                STA     LINEBUF,X
                INC     LN
                LDA     LP
                INC     A
                CMP     LN
                BNE     @mid
                JSR     FITS           ; at the end: a new row if it doesn't fit
                BCS     @shown
                LDA     SCB_COLUMN
                BEQ     @shown         ; a row's start: shown all the same
                LDA     LP
                STA     LV
                JSR     NEWROW
@shown:         JSR     STEP
                JMP     LOOP
@mid:           JSR     FITS
                BCS     @show
                LDA     LP             ; past the row's end: the line again
                INC     A
                JSR     REDRAW
                JMP     LOOP
@show:          JSR     SHOW           ; it and what follows, pushed right
                JSR     STEP
                JMP     LOOP

; The cursor right over its character, which fits; the row's end kept.
STEP:           LDX     LP
                JSR     ECHOX
                INC     LP
                LDA     SCB_COLUMN
                CMP     LDRAWN
                BCC     @done
                STA     LDRAWN
@done:          RTS

; Carry set if LINEBUF[LP] fits at the cursor: column + width <= the width.
FITS:           LDX     LP
                JSR     WIDTH
                CLC
                ADC     SCB_COLUMN
                BCS     @no
                CMP     SCB_WIDTH
                BEQ     @yes
                BCC     @yes
@no:            CLC
                RTS
@yes:           SEC
                RTS

L_LEFT:         LDA     LP
                BEQ     @done
                DEC     A
                JSR     MOVELEFT
@done:          JMP     LOOP

L_RIGHT:        LDA     LP
                CMP     LN
                BEQ     @done
                INC     A
                JSR     MOVERIGHT
@done:          JMP     LOOP

L_HOME:         LDA     LP             ; to the start, or from there to the end
                BEQ     @end
                LDA     #0
                JSR     MOVELEFT
                JMP     LOOP
@end:           LDA     LN
                BEQ     @done
                JSR     MOVERIGHT
@done:          JMP     LOOP

L_CTLC:         LDA     LN             ; the first key of an empty line: the warm
                BNE     @char          ; boot, unless console mode bit 3
                LDA     SCB_CONMODE
                AND     #CM_NOCTLC
                BNE     @char
                JMP     CTLCEND
@char:          LDA     #ASCII_CTLC    ; else a character (the dispatch took A)
                JMP     INSERT

L_ROW:          LDA     LP             ; ^E: the rest of the line on a new row
                STA     LV
                JSR     NEWROW
                JSR     SHOW
                JMP     LOOP

L_DELCUR:       JSR     DELCUR         ; ^G
                JMP     LOOP

L_CTLH:         LDA     SCB_CTLH       ; ^H: a backspace, or ($FF) rub/del's
                BNE     RUBECHO
BACKSP:         LDA     LP
                BEQ     @done
                DEC     A
                JSR     MOVELEFT
                JSR     DELCUR
@done:          JMP     LOOP

L_DEL:          LDA     SCB_RUBOUT     ; rub/del: ($FF) a backspace, or its echo
                BNE     BACKSP
RUBECHO:        LDA     LP             ; at the line's end: taken out and echoed
                BEQ     @done
                CMP     LN
                BNE     BACKSP         ; within it: as a backspace
                DEC     LN
                DEC     LP
                LDX     LP
                JSR     ECHOX
                LDA     LP             ; the row from here, as typed past
                STA     LV
                LDA     SCB_COLUMN
                STA     LDRAWN
@done:          JMP     LOOP

L_KILL:         LDA     LP             ; ^K: from the cursor to the end
                STA     LN
                JSR     SHOW
                JMP     LOOP

L_DELLEFT:      LDA     LP             ; ^X: everything before the cursor
                BEQ     @done
                PHA
                LDA     #0
                JSR     MOVELEFT
                PLY                    ; those LP.. down to 0
                LDX     #0
@shift:         CPY     LN
                BEQ     @moved
                LDA     LINEBUF,Y
                STA     LINEBUF,X
                INX
                INY
                BRA     @shift
@moved:         STX     LN
                JSR     SHOW
@done:          JMP     LOOP

L_PRINT:        JSR     TOGGLEP
                JMP     LOOP

L_RETYPE:       LDA     LP             ; ^R: on a new row
                JSR     REDRAW
                JMP     LOOP

L_CANCEL:       LDX     #0             ; ^U: to the cursor the previous line
@copy:          CPX     LP
                BEQ     @copied
                LDA     LINEBUF,X
                STA     PREVBUF,X
                INX
                BRA     @copy
@copied:        STX     PLEN
                STZ     LN
                STZ     LP
                STZ     LV
                JSR     NEWROW
                JMP     LOOP

L_RECALL:       LDA     LN             ; ^W: the previous line into an empty one
                BNE     @end
                LDX     #0
@copy:          CPX     PLEN
                BEQ     @copied
                CPX     LMAX
                BEQ     @copied
                LDA     PREVBUF,X
                STA     LINEBUF,X
                INX
                BRA     @copy
@copied:        STX     LN
@end:           LDA     LN             ; the cursor to the end
                CMP     LP
                BEQ     @done
                JSR     MOVERIGHT
@done:          JMP     LOOP

L_END:          LDX     #0             ; CR, LF: the line, the previous line too
@copy:          CPX     LN
                BEQ     @copied
                LDA     LINEBUF,X
                STA     PREVBUF,X
                INX
                BRA     @copy
@copied:        STX     PLEN
                LDA     #ASCII_CR
                JSR     OUT
                LDA     LBUFP          ; +1 the count, then the line
                CLC
                ADC     #1
                STA     PP
                LDA     LBUFP + 1
                ADC     #0
                STA     PP + 1
                LDA     LN
                JSR     PPUT
                LDX     #0
@put:           CPX     LN
                BEQ     @done
                LDA     LINEBUF,X
                JSR     PPUT
                INX
                BRA     @put
@done:          JMP     CZERO

; The character at the cursor taken out, the row shown again.
DELCUR:         LDX     LP
                CPX     LN
                BEQ     @done
@shift:         INX
                CPX     LN
                BEQ     @moved
                LDA     LINEBUF,X
                STA     LINEBUF - 1,X
                BRA     @shift
@moved:         DEC     LN
                JMP     SHOW
@done:          RTS

; The cursor left to A: by BSs on this row, or the line again on a new one.
MOVELEFT:       CMP     LV
                BCC     REDRAW
                STA     LT
                LDA     #0             ; LCOLP: the columns back
                LDX     LP
@sum:           CPX     LT
                BEQ     @summed
                DEX
                STA     LCOLP
                JSR     WIDTH
                CLC
                ADC     LCOLP
                BRA     @sum
@summed:        STA     LCOLP
                LDA     LT
                STA     LP
@bs:            LDA     LCOLP
                BEQ     @done
                LDA     #ASCII_BS
                JSR     OUT
                DEC     LCOLP
                BRA     @bs
@done:          RTS

; The cursor right to A: over the characters if they fit, or the line again
; on a new row.
MOVERIGHT:      STA     LT
                LDA     SCB_COLUMN     ; the column it would reach
                LDX     LP
@sum:           CPX     LT
                BEQ     @summed
                STA     LCOLP
                JSR     WIDTH
                CLC
                ADC     LCOLP
                BCS     @far
                INX
                BRA     @sum
@summed:        CMP     SCB_WIDTH
                BEQ     @step
                BCC     @step
@far:           LDA     LT
                BRA     REDRAW
@step:          LDA     LP
                CMP     LT
                BEQ     @done
                JSR     STEP
                BRA     @step
@done:          RTS

; The line on a new row, the cursor at A: the row from as far back as fits
; with room for the character at the cursor (2 columns).
REDRAW:         STA     LT
                JSR     NEWROW
                LDA     SCB_WIDTH      ; LCOLP: the room left
                SEC
                SBC     #2
                BCS     @room
                LDA     #0
@room:          STA     LCOLP
                LDX     LT
@back:          CPX     #0
                BEQ     @from
                DEX
                JSR     WIDTH
                STA     LV             ; LV for a moment: the width
                LDA     LCOLP
                SEC
                SBC     LV
                BCC     @over
                STA     LCOLP
                BRA     @back
@over:          INX
@from:          STX     LV
                STX     LP
@type:          LDA     LP
                CMP     LT
                BEQ     SHOW
                JSR     STEP
                BRA     @type

; The row shown from the cursor: LINEBUF[LP..LN-1] while it fits, what was
; shown past it blanked, the cursor back.
SHOW:           LDA     SCB_COLUMN
                STA     LCOLP
                LDX     LP
@draw:          CPX     LN
                BEQ     @drawn
                JSR     WIDTH
                CLC
                ADC     SCB_COLUMN
                BCS     @drawn
                CMP     SCB_WIDTH
                BEQ     @fits
                BCS     @drawn
@fits:          JSR     ECHOX
                INX
                BRA     @draw
@drawn:         LDA     SCB_COLUMN     ; the row's new end
                PHA
@erase:         LDA     SCB_COLUMN
                CMP     LDRAWN
                BCS     @erased
                LDA     #' '
                JSR     OUT
                BRA     @erase
@erased:        PLA
                STA     LDRAWN
@back:          LDA     SCB_COLUMN
                CMP     LCOLP
                BEQ     @done
                BCC     @done
                LDA     #ASCII_BS
                JSR     OUT
                BRA     @back
@done:          RTS

; CR LF: a new row, nothing shown on it.
NEWROW:         LDA     #ASCII_CR
                JSR     OUT
                LDA     #ASCII_LF
                JSR     OUT
                STZ     LDRAWN
                RTS

; A = the columns LINEBUF[X] takes: 2 for a control character (^X), else 1.
; Keeps X and Y.
WIDTH:          LDA     LINEBUF,X
                CMP     #' '
                LDA     #1
                BCS     @one
                INC     A
@one:           RTS

; LINEBUF[X] shown, ^X for a control character. Keeps X.
ECHOX:          PHX
                LDA     LINEBUF,X
                CMP     #' '
                BCS     @char
                PHA
                LDA     #'^'
                JSR     OUT
                PLA
                ORA     #$40
@char:          JSR     OUT
                PLX
                RTS

; ---------------------------------------------------------------------------
; The system

; 47: the CCP runs the command at $0280 at the warm boot.
F_CHAIN:        LDX     #CCP_CHAIN
                CMP     #$FF
                BNE     @own
                LDX     #CCP_CHAIN | CCP_CHAINDU
@own:           STX     SCB_CCPFLAGS
                JMP     WARM

; 49: the SCB parameter block.
F_SCB:          JSR     PGET           ; T0 the offset, T1 the operation, T2/T3 the value
                STA     T0
                JSR     PGET
                STA     T1
                JSR     PGET
                STA     T2
                JSR     PGET
                STA     T3
                LDX     T0
                CPX     #SCB_LEN
                BCS     @none
                LDA     T1
                CMP     #$FF
                BEQ     @byte
                CMP     #$FE
                BEQ     @word
                LDA     #0             ; a get (or a reserved operation): the word
                CPX     #SCB_LEN - 1
                BEQ     @high
                LDA     SCB + 1,X
@high:          PHA
                LDA     SCB,X
                PLX
                RTS
@word:          CPX     #SCB_LEN - 1
                BEQ     @byte          ; the SCB's last byte: that byte only
                LDA     T3
                STA     SCB + 1,X
@byte:          LDA     T2
                STA     SCB,X
@none:          JMP     CZERO

; 107: OODOS's serial number.
F_SERIAL:       LDX     #0
@copy:          LDA     SERIAL,X
                JSR     PPUT
                INX
                CPX     #6
                BNE     @copy
                JMP     CZERO
SERIAL:         .byte   "OOD", 0, 0, 1

; 108, 109: A/X $FFFF gets the word, anything else sets it.
F_RETCODE:      LDX     #<(SCB_RETCODE - SCB)
                BRA     GETSET
F_CONMODE:      LDX     #<(SCB_CONMODE - SCB)
GETSET:         LDA     PA
                AND     PX
                CMP     #$FF
                BEQ     @get
                LDA     PA
                STA     SCB,X
                LDA     PX
                STA     SCB + 1,X
                JMP     CZERO
@get:           LDA     SCB + 1,X
                PHA
                LDA     SCB,X
                PLX
                RTS

; 110: the delimiter.
F_DELIM:        LDA     PA
                AND     PX
                CMP     #$FF
                BEQ     @get
                LDA     PA
                STA     SCB_DELIM
                JMP     CZERO
@get:           LDA     SCB_DELIM
                LDX     #0
                RTS

; ---------------------------------------------------------------------------
; 152: parse a file specification, {d:}filename{.typ}{;password}, into
; bytes 0-23 of an FCB (the Guide, function 152 and 2.3.1).

F_PARSE:        JSR     PGET           ; the PFCB: the string, the FCB
                STA     PSTR
                JSR     PGET
                STA     PSTR + 1
                JSR     PGET
                STA     PFCB
                JSR     PGET
                STA     PFCB + 1
                LDX     #23            ; F: drive 0, the fields blank, 12-15 zero
@init:          LDA     #' '
                CPX     #16
                BCS     @set
                CPX     #12
                BCS     @zero
                CPX     #0
                BNE     @set
@zero:          LDA     #0
@set:           STA     FC,X
                DEX
                BPL     @init
                STZ     PIDX
                JSR     SKIPBL         ; A: the first character after blanks
                JSR     ISDELIM
                BCS     @name          ; the end, or an empty name: no drive
                JSR     NEXTC          ; a drive: a letter, then ':'
                CMP     #':'
                BNE     @name
                JSR     CURC
                JSR     UPPER
                SEC
                SBC     #'A' - 1
                BCC     @nodrv
                BEQ     @nodrv         ; '@'
                CMP     #16 + 1
                BCC     @drive
@nodrv:         JMP     @bad
@drive:         STA     FC + FCB_DR
                INC     PIDX
                INC     PIDX
@name:          LDX     #FCB_NAME
                LDY     #8
                JSR     FIELD
                BCS     @bad
                JSR     CURC
                CMP     #'.'
                BNE     @type
                INC     PIDX
                LDX     #FCB_T1
                LDY     #3
                JSR     FIELD
                BCS     @bad
@type:          JSR     CURC
                CMP     #';'
                BNE     @parsed
                INC     PIDX
                LDX     #16
                LDY     #8
                SEC                    ; '*' is a character of a password
                JSR     FIELDP
                BCS     @bad
@parsed:        LDA     PIDX
                STA     PEND
                LDA     PFCB           ; F's 0-23 to the FCB
                STA     PP
                LDA     PFCB + 1
                STA     PP + 1
                LDX     #0
@put:           LDA     FC,X
                JSR     PPUT
                INX
                CPX     #24
                BNE     @put
                JSR     SKIPBL         ; what follows the blanks after it
                CMP     #0
                BEQ     @end
                CMP     #ASCII_CR
                BEQ     @end
                JSR     ISDELIM
                BCS     @at            ; a delimiter: its address
                LDA     PEND           ; else the first blank's
                STA     PIDX
@at:            LDA     PIDX
                CLC
                ADC     PSTR
                PHA
                LDA     PSTR + 1
                ADC     #0
                TAX
                PLA
                RTS
@end:           JMP     CZERO
@bad:           LDA     #$FF
                TAX
                RTS

; A field into F from X, Y characters at most, to a delimiter: capitals, a
; '*' filling it with '?'. Carry set: an error (too long, a character
; after the '*', a control character, DEL or over $7F). FIELDP with carry
; set: '*' is a character (a password).
FIELD:          CLC
FIELDP:         ROR     T5             ; bit 7: '*' a character
                STY     T4             ; the room left: 0 once full, or filled by '*'
@char:          JSR     CURC
                JSR     ISDELIM
                BCS     @done
                CMP     #' '
                BCC     @bad
                CMP     #ASCII_DEL
                BCS     @bad
                LDY     T4
                BEQ     @bad           ; full (or filled by '*')
                CMP     #'*'
                BNE     @put
                BIT     T5
                BMI     @put
@fill:          LDA     #'?'
                STA     FC,X
                INX
                DEC     T4
                BNE     @fill
                INC     PIDX
                BRA     @char
@put:           JSR     UPPER
                STA     FC,X
                INX
                DEC     T4
                INC     PIDX
                BRA     @char
@done:          CLC
                RTS
@bad:           SEC
                RTS

; Blanks and tabs from PIDX passed; A the character after them.
SKIPBL:         JSR     CURC
                CMP     #' '
                BEQ     @next
                CMP     #ASCII_TAB
                BNE     @done
@next:          INC     PIDX
                BRA     SKIPBL
@done:          RTS

; A = the string's character at PIDX (NEXTC: PIDX + 1); 0 from 128 on.
; Keeps X and Y.
NEXTC:          INC     PIDX
                JSR     CURC
                DEC     PIDX
                RTS
CURC:           LDA     PIDX
                BMI     @nul
                CLC
                ADC     PSTR
                STA     PP
                LDA     PSTR + 1
                ADC     #0
                STA     PP + 1
                JMP     PGET
@nul:           LDA     #0
                RTS

; Carry set if A is one of 152's delimiters. Keeps A, X and Y.
ISDELIM:        PHX
                LDX     #DELIMS_LEN - 1
@find:          CMP     DELIMS,X
                BEQ     @yes
                DEX
                BPL     @find
                PLX
                CLC
                RTS
@yes:           PLX
                SEC
                RTS
DELIMS:         .byte   0, ' ', ASCII_TAB, ASCII_CR, ';', '=', '<', '>', '.', ':', ',', '|', '[', ']'
DELIMS_LEN      = * - DELIMS

UPPER:          CMP     #'a'
                BCC     @done
                CMP     #'z' + 1
                BCS     @done
                AND     #$DF
@done:          RTS

; OODOS's stand-in BDOS (hw/oodos.md, "The system, phase 2"), block 60:
; $4000-$7FFF while it runs (W1), the BIOS in W2, the system block in W0.
; Phase 3's BDOS replaces it. Written from the CP/M 3 manuals' descriptions.
;
; The stub in common memory calls BDOSENT with the function in Y, the
; parameter in A (and X), and returns A and X to the program (hw/oodos.md,
; "Registers"). Functions:
;
;    0  P_TERMCPM   the warm boot
;    1  C_READ      A = a console character, echoed
;    2  C_WRITE     A to the console; a tab is spaces to the next eighth column
;    6  C_RAWIO     A = $FF: A = a character, or 0 if none; $FE: A = the
;                   status; $FD: A = a character, waited for; else A out, raw
;    9  C_WRITESTR  the string at A/X, to the SCB's delimiter ('$')
;   10  C_READSTR   a line into the buffer at A/X (0: the DMA address): +0
;                   its size, +1 the count read, then the characters. BS and
;                   DEL take one back, ^X the line; CR or LF ends it, echoed
;                   as CR; ^C at the start of the line is a warm boot
;   11  C_STAT      A = 1 a character waits, 0 none
;   12  S_BDOSVER   A/X = $0031
;   25  DRV_GET     A = the current drive
;   50  S_BIOS      the BIOS parameter block at A/X: +0 the function, +1 A,
;                   +2 X, +3 Y, +4-5 and +6-7 the words (BW1, BW2); returns
;                   the BIOS's A/X. BOOT (0), which would run cold boot again
;                   and wipe M:, and any function past XMOVE return A = X = $FF
;   72  O_VERSION   a 10-byte block at A/X gets OODOS's version (major,
;                   minor), the BIOS's, the memory fitted and the memory for
;                   programs in KB (words), and FARCALL's address; A/X = FARCALL
;  104  T_SET       the DAT at A/X (days, hour, minute in BCD) into the clock,
;                   the seconds 0
;  105  T_GET       the clock into the DAT at A/X; A = the seconds, BCD
;
; and any other A = X = $FF.
;
; The program's memory (function 9's string, 10's buffer, 50's parameter
; block, 72's and 104/105's) is read and written a byte at a time through W2:
; PGET and PPUT map the block the program's own window had there, by the
; saved WIN0-WIN3, and put the BIOS back after each byte. An address in the
; I/O hole, $C000-$C7FF, isn't memory: the function stops there and returns
; A = X = $FF (hw/oodos.md, "The system, phase 2").

.setcpu "65C02"
.include "sys.inc"
.import WARM, FARCALL
.export BDOSENT

ASCII_TAB       = $09
ASCII_CTLC      = $03
ASCII_CTLX      = $18
ASCII_BS        = $08
ASCII_DEL       = $7F
ASCII_CR        = $0D
ASCII_LF        = $0A
MEM_FITTED      = 1024                 ; KB
MEM_PROGRAMS    = 320                  ; KB: banks 1-5, for functions 53-58 (later)

.zeropage
PA:             .res    1              ; the parameter
PX:             .res    1
PP:             .res    2              ; a program address, for PGET and PPUT
TP:             .res    2              ; where PMAP put it, in W2
COUNT:          .res    1
SIZE:           .res    1
BIOSV:          .res    2

.segment "BDOS"

BDOSENT:        STA     PA
                STX     PX
                STA     PP
                STX     PP + 1
                LDX     #FUNCS_LEN - 1
@find:          TYA
                CMP     FUNCS,X
                BEQ     FGO
                DEX
                BPL     @find
NOFUNC:         LDA     #$FF
                TAX
                RTS
FGO:            TXA
                ASL     A
                TAX
                LDA     PA
                JMP     (JUMPS,X)

FUNCS:          .byte   0, 1, 2, 6, 9, 10, 11, 12, 25, 50, 72, 104, 105
FUNCS_LEN       = * - FUNCS
JUMPS:          .word   F_TERM, F_READ, F_WRITE, F_RAWIO, F_STRING, F_LINE, F_STAT
                .word   F_VERSION, F_DRIVE, F_BIOS, F_OODOS, F_TSET, F_TGET

F_TERM:         JMP     WARM

F_READ:         JSR     CONIN
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

F_WRITE:        JSR     OUT
                LDA     #0
                TAX
                RTS

F_RAWIO:        CMP     #$FF
                BNE     @status
                JSR     CONST
                BEQ     ZERO
                JSR     CONIN
                LDX     #0
                RTS
@status:        CMP     #$FE
                BNE     @input
                JSR     CONST
                LDX     #0
                RTS
@input:         CMP     #$FD
                BNE     @output
                JSR     CONIN
                LDX     #0
                RTS
@output:        JSR     CONOUT
ZERO:           LDA     #0
                TAX
                RTS

F_STRING:       JSR     PGET
                CMP     SCB_DELIM
                BEQ     ZERO
                JSR     OUT
                BRA     F_STRING

F_STAT:         JSR     CONST
                BEQ     ZERO
                LDA     #1
                LDX     #0
                RTS

F_VERSION:      LDA     #$31
                LDX     #0
                RTS

F_DRIVE:        LDA     SCB_DISK
                LDX     #0
                RTS

; A line, with what CP/M's line editing has that a terminal needs.
F_LINE:         LDA     PA
                ORA     PX
                BNE     @buffer
                LDA     SCB_DMA        ; 0: the DMA address
                STA     PP
                LDA     SCB_DMA + 1
                STA     PP + 1
@buffer:        JSR     PGET           ; +0 its size; PP is +1, the count
                STA     SIZE
                STZ     COUNT
@key:           JSR     CONIN
                CMP     #ASCII_CR
                BEQ     @end
                CMP     #ASCII_LF
                BEQ     @end
                CMP     #ASCII_BS
                BEQ     @back
                CMP     #ASCII_DEL
                BEQ     @back
                CMP     #ASCII_CTLX
                BEQ     @cancel
                CMP     #ASCII_CTLC
                BNE     @char
                LDX     COUNT
                BNE     @key
                JMP     WARM           ; ^C at the start: the warm boot
@char:          CMP     #' '
                BCC     @key           ; other control characters: nothing
                LDX     COUNT
                CPX     SIZE
                BCS     @key           ; full
                PHA
                JSR     OUT
                PLA
                LDY     COUNT          ; at +2 + COUNT: PP + 1 + COUNT
                INY
                JSR     PPUTY
                INC     COUNT
                BRA     @key
@back:          LDA     COUNT
                BEQ     @key
                JSR     RUBOUT
                DEC     COUNT
                BRA     @key
@cancel:        LDA     COUNT
                BEQ     @key
                JSR     RUBOUT
                DEC     COUNT
                BRA     @cancel
@end:           LDA     #ASCII_CR
                JSR     OUT
                LDA     COUNT
                LDY     #0
                JSR     PPUTY
                LDA     #0
                TAX
                RTS

RUBOUT:         LDA     #ASCII_BS
                JSR     OUT
                LDA     #' '
                JSR     OUT
                LDA     #ASCII_BS
                JMP     OUT

; The BIOS for the program: its parameter block in, a JSR through the jump
; table, A and X out.
F_BIOS:         JSR     PGET
                CMP     #BIOS::BOOT
                BEQ     @refused
                CMP     #BIOS::COUNT
                BCC     @known
@refused:       JMP     NOFUNC
@known:         STA     BIOSV          ; $F800 + 3 x the function
                ASL     A
                ADC     BIOSV
                ADC     #<COMMON
                STA     BIOSV
                LDA     #>COMMON
                ADC     #0
                STA     BIOSV + 1
                JSR     PGET
                STA     PA
                JSR     PGET
                STA     PX
                JSR     PGET
                PHA                    ; Y
                LDX     #0
@words:         JSR     PGET
                STA     BW1,X
                INX
                CPX     #4
                BNE     @words
                PLY
                LDA     PA
                LDX     PX
                JMP     (BIOSV)        ; its RTS is ours

F_OODOS:        LDX     #0
@copy:          LDA     VERSIONS,X
                JSR     PPUT
                INX
                CPX     #VERSIONS_LEN
                BNE     @copy
                LDA     #<FARCALL
                LDX     #>FARCALL
                RTS
VERSIONS:       .byte   OODOS_MAJOR, OODOS_MINOR, BIOS_MAJOR, BIOS_MINOR
                .word   MEM_FITTED, MEM_PROGRAMS
                .word   FARCALL
VERSIONS_LEN    = * - VERSIONS

F_TSET:         LDX     #0
@dat:           JSR     PGET
                STA     SCB_DATE,X
                INX
                CPX     #4
                BNE     @dat
                STZ     SCB_SEC
                LDA     #$FF
                JSR     TIME
                JMP     ZERO

F_TGET:         LDA     #0
                JSR     TIME
                LDX     #0
@dat:           LDA     SCB_DATE,X
                JSR     PPUT
                INX
                CPX     #4
                BNE     @dat
                LDA     SCB_SEC
                LDX     #0
                RTS

; The console's output, keeping the SCB's column: a tab to the next eighth
; column, as CP/M's.
OUT:            CMP     #ASCII_TAB
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
                JMP     CONOUT

; The BIOS, through the jump table
CONST:          JMP     JT(BIOS::CONST)
CONIN:          JMP     JT(BIOS::CONIN)
CONOUT:         JMP     JT(BIOS::CONOUT)
TIME:           JMP     JT(BIOS::TIME)

; A = the program's byte at PP, then PP + 1. Keeps X and Y.
PGET:           PHX
                JSR     PMAP
                LDA     (TP)
                BRA     PDONE

; A to the program's byte at PP, then PP + 1. Keeps X and Y.
PPUT:           PHX
                PHA
                JSR     PMAP
                PLA
                STA     (TP)
PDONE:          LDX     #BIOS_BLOCK
                STX     NB_WIN2
                INC     PP
                BNE     @done
                INC     PP + 1
@done:          PLX
                RTS

; A to the program's byte at PP + Y, PP left as it is. Keeps X and Y.
PPUTY:          PHX
                PHA
                PHY
                LDA     PP
                PHA
                LDA     PP + 1
                PHA
                TYA
                CLC
                ADC     PP
                STA     PP
                BCC     @put
                INC     PP + 1
@put:           TSX                    ; the byte, from under the three pushed after it
                LDA     $0104,X
                JSR     PPUT
                PLA
                STA     PP + 1
                PLA
                STA     PP
                PLY
                PLA
                PLX
                RTS

; W2 on the block the program's window had at PP; TP its address there. An
; address in the I/O hole ends the function: A = X = $FF, from BDOSENT's
; own frame (the stub's JSR left S at $FD), the BIOS still in W2.
PMAP:           LDA     PP + 1
                CMP     #>CODE_WINDOW
                BCS     @memory
                CMP     #$C0
                BCC     @memory
                LDX     #$FD
                TXS
                JMP     NOFUNC
@memory:        ROL     A              ; the window: the top 2 bits
                ROL     A
                ROL     A
                AND     #3
                TAX
                LDA     ST_WIN,X
                STA     NB_WIN2
                LDA     PP + 1
                AND     #$3F
                ORA     #$80
                STA     TP + 1
                LDA     PP
                STA     TP
                RTS

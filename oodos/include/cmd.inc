; Console helpers for commands (hw/oodos.md, "The CCP, phase 4"): what the
; CCP's built-ins and phase 4b's utilities share, assembled into each. It
; uses no zero page: its pointers are written into its own instructions,
; so the code must be in RAM, as the CCP's and a program's are.
;
; Before including it, define:
;
;   CMD_DATA    the name of the segment for its constant data ("RODATA"
;               in a program: data in CODE would meet tools/rules.py)
;   CMD_BSS     the name of the segment for its variables
;   CMD_BREAK   a label: where a ^C typed during paged output goes
;
; and include oodos.inc. Its code goes in the segment current at the
; .include. Every routine may change A, X and Y unless it says otherwise;
; the BDOS keeps none of them.
;
;   CM_PUTC     A to the console (function 2); keeps X and Y. An LF owed
;               (CM_LFDUE) goes first
;   CM_PUTS     the '$'-ended string at A/X (function 9), an LF owed first
;   CM_FLUSH    an LF owed, written
;   CM_CRLF     CR, LF (an LF owed is one of them)
;   CM_PAGE     start paged output: the SCB's page mode ($2C, 0 on) and
;               length ($1C); the console mode (109) kept, and bit 0 set
;               in it, so 11 reports only ^C. Called again while paged, it
;               keeps the mode the first kept (CM_PAGED). A mode with bits
;               CP/M 3 leaves undefined set so that it ORs to $FFFF would
;               be a get to 109, so bit 0 isn't set: such a mode is taken
;               not to happen
;   CM_UNPAGE   the console mode CM_PAGE kept, back; without a CM_PAGE
;               before it, nothing
;   CM_PAGED    1 while paged; in CMD_DATA, so it starts 0 with the image
;               (a program's BSS isn't cleared). A user that sets the
;               console mode itself in between clears it (the CCP does)
;   CM_NL       CR, LF, a line counted (CM_LINE)
;   CM_LINE     a line ended: past the page, "Press a key for more", a
;               key waited for (^C: CMD_BREAK), the message rubbed out
;   CM_BRK      a ^C waiting? CMD_BREAK. Another key is left alone: a
;               key is taken only in console mode bit 0 (CM_PAGE's), where
;               11 reports only ^C; without it, nothing is taken
;   CM_DIRNM    the 11 bytes at A/X as a directory listing has them:
;               "NAME     TYP", bit 7 of each dropped
;   CM_FNAME    the 11 bytes at A/X as a name is typed: "NAME.TYP",
;               "NAME" with a blank type
;   CM_READ     a line of up to 15 characters from the console (function
;               10) into CM_ANS + 2, its count at CM_ANS + 1 and a 0
;               after it. Function 10 echoes only the CR that ends it, so
;               an LF is owed (CM_LFDUE): the next output writes it first,
;               and a line that ends the output needs none
;
;   CM_LFDUE    1: an LF is owed; set it after reading a line
;   CM_ASK      CM_READ, carry set if its first non-blank is Y or y
;   CM_DECIN    a decimal number 0-255 from the string at A/X, index Y:
;               A the number, Y past its digits, carry set if there were
;               none or it is over 255
;   CM_DECOUT   A/X in decimal, no leading zeros

.ifndef CMD_INC
CMD_INC = 1

CMC_CTLC         = $03
CMC_LF           = $0A
CMC_CR           = $0D

.pushseg
.segment CMD_BSS
CM_LFDUE:       .res    1              ; 1: an LF is owed
CM_LINES:       .res    1              ; lines since the last stop
CM_PLEN:        .res    1              ; lines to a stop, 0: none
CM_NUM:         .res    2              ; CM_DECIN's and CM_DECOUT's number
CM_DIG:         .res    1              ; digits seen or written
CM_T:           .res    1
CM_MODE:        .res    2              ; the console mode CM_PAGE found (CM_PAGED)
CM_ANS:         .res    2 + 16         ; CM_READ's line: its size, count, characters
.segment CMD_DATA
CM_PAGED:       .byte   0              ; 1: CM_MODE holds the mode to put back
CM_PBMODE:      .byte   $2C, 0, 0, 0   ; 49: get the page mode
CM_PBLEN:       .byte   $1C, 0, 0, 0   ;     and the page length
CM_MORE:        .byte   "Press a key for more$"
CM_RUB:         .byte   CMC_CR, "                    ", CMC_CR, "$"
CM_POW:         .word   10000, 1000, 100, 10, 1
.popseg

CM_PUTC:        PHX
                PHY
                PHA
                JSR     CM_FLUSH
                PLA
                LDY     #C_WRITE
                JSR     BDOS
                PLY
                PLX
                RTS

CM_PUTS:        PHA
                PHX
                JSR     CM_FLUSH
                PLX
                PLA
                LDY     #C_WRITESTR
                JMP     BDOS

CM_FLUSH:       LDA     CM_LFDUE
                BEQ     @done
                STZ     CM_LFDUE
                LDA     #CMC_LF
                LDY     #C_WRITE
                JMP     BDOS
@done:          RTS

CM_CRLF:        STZ     CM_LFDUE
                LDA     #CMC_CR
                JSR     CM_PUTC
                LDA     #CMC_LF
                JMP     CM_PUTC

CM_PAGE:        STZ     CM_LINES
                STZ     CM_PLEN
                LDA     #<CM_PBMODE
                LDX     #>CM_PBMODE
                LDY     #S_SCB
                JSR     BDOS
                CMP     #0
                BNE     @mode          ; not 0: no stops
                LDA     #<CM_PBLEN
                LDX     #>CM_PBLEN
                LDY     #S_SCB
                JSR     BDOS
                CMP     #2
                BCC     @mode          ; under 2: no stops (a page of one line)
                DEC     A              ; the last line is the message's
                STA     CM_PLEN
@mode:          LDA     CM_PAGED       ; paged already: its mode is kept, bit 0 set
                BNE     @kept
                LDA     #$FF           ; the console mode kept, then bit 0 set:
                TAX                    ; 11 reports only ^C
                LDY     #C_MODE
                JSR     BDOS
                STA     CM_MODE
                STX     CM_MODE + 1
                LDA     #1
                STA     CM_PAGED
                LDA     CM_MODE
                ORA     #1
                LDX     CM_MODE + 1
                LDY     #C_MODE
                JMP     BDOS
@kept:          RTS

CM_UNPAGE:      LDA     CM_PAGED       ; not paged: nothing to put back
                BEQ     @none
                STZ     CM_PAGED
                LDA     CM_MODE
                LDX     CM_MODE + 1
                LDY     #C_MODE
                JMP     BDOS
@none:          RTS

CM_NL:          JSR     CM_CRLF
CM_LINE:        LDA     CM_PLEN
                BEQ     CM_BRK
                INC     CM_LINES
                LDA     CM_LINES
                CMP     CM_PLEN
                BCC     CM_BRK
                STZ     CM_LINES
                LDA     #<CM_MORE
                LDX     #>CM_MORE
                JSR     CM_PUTS
                LDA     #$FD           ; a key, waited for
                LDY     #C_RAWIO
                JSR     BDOS
                CMP     #CMC_CTLC
                BEQ     @brk
                LDA     #<CM_RUB
                LDX     #>CM_RUB
                JMP     CM_PUTS
@brk:           JMP     CMD_BREAK

CM_BRK:         LDY     #C_STAT
                JSR     BDOS
                CMP     #0
                BEQ     @none
                LDA     #$FF           ; console mode bit 0? Else it may be any
                TAX                    ; key: left alone
                LDY     #C_MODE
                JSR     BDOS
                LSR     A
                BCC     @none
                LDA     #$FF           ; take it: a ^C in console mode bit 0
                LDY     #C_RAWIO
                JSR     BDOS
                CMP     #CMC_CTLC
                BNE     @none
                JMP     CMD_BREAK
@none:          RTS

CM_DIRNM:       STA     @src + 1
                STX     @src + 2
                LDY     #0
@char:          CPY     #8
                BNE     @src
                LDA     #' '
                JSR     CM_PUTC
@src:           LDA     $FFFF,Y
                AND     #$7F
                JSR     CM_PUTC
                INY
                CPY     #11
                BNE     @char
                RTS

CM_FNAME:       STA     @name + 1
                STX     @name + 2
                STA     @type + 1
                STX     @type + 2
                STA     @more + 1
                STX     @more + 2
                LDY     #0
@name:          LDA     $FFFF,Y
                AND     #$7F
                CMP     #' '
                BEQ     @dot
                JSR     CM_PUTC
                INY
                CPY     #8
                BNE     @name
@dot:           LDY     #8
@type:          LDA     $FFFF,Y
                AND     #$7F
                CMP     #' '
                BEQ     @done
                LDA     #'.'
                JSR     CM_PUTC
@more:          LDA     $FFFF,Y
                AND     #$7F
                CMP     #' '
                BEQ     @done
                JSR     CM_PUTC
                INY
                CPY     #11
                BNE     @more
@done:          RTS

CM_READ:        LDA     #15
                STA     CM_ANS
                STZ     CM_ANS + 1
                LDA     #<CM_ANS
                LDX     #>CM_ANS
                LDY     #C_READSTR
                JSR     BDOS
                LDX     CM_ANS + 1
                STZ     CM_ANS + 2,X
                LDA     #1             ; function 10 echoed only the CR
                STA     CM_LFDUE
                RTS

CM_ASK:         JSR     CM_READ
                LDX     #0
@skip:          LDA     CM_ANS + 2,X
                BEQ     @no
                INX
                CMP     #' '
                BEQ     @skip
                AND     #$DF
                CMP     #'Y'
                BEQ     @yes
@no:            CLC
                RTS
@yes:           SEC
                RTS

CM_DECIN:       STA     @src + 1
                STX     @src + 2
                STZ     CM_NUM
                STZ     CM_DIG
@src:           LDA     $FFFF,Y
                SEC
                SBC     #'0'
                CMP     #10
                BCS     @end
                STA     CM_T           ; the digit
                LDA     CM_NUM
                CMP     #26
                BCS     @bad           ; x 10 would pass 255
                ASL     A
                STA     CM_NUM         ; n x 2
                ASL     A
                ASL     A              ; n x 8
                CLC
                ADC     CM_NUM
                ADC     CM_T
                BCS     @bad
                STA     CM_NUM
                INC     CM_DIG
                INY
                BRA     @src
@end:           LDA     CM_DIG
                BEQ     @bad
                LDA     CM_NUM
                CLC
                RTS
@bad:           SEC
                RTS

CM_DECOUT:      STA     CM_NUM
                STX     CM_NUM + 1
                STZ     CM_DIG
                LDX     #0             ; CM_POW's index
@pow:           LDY     #0             ; the digit
@sub:           SEC
                LDA     CM_NUM
                SBC     CM_POW,X
                STA     CM_T
                LDA     CM_NUM + 1
                SBC     CM_POW + 1,X
                BCC     @less
                STA     CM_NUM + 1
                LDA     CM_T
                STA     CM_NUM
                INY
                BRA     @sub
@less:          TYA
                BNE     @put
                LDA     CM_DIG
                BNE     @zero
                CPX     #8             ; the units: 0 is written
                BNE     @next
@zero:          LDA     #0
@put:           ORA     #'0'
                JSR     CM_PUTC
                INC     CM_DIG
@next:          INX
                INX
                CPX     #10
                BNE     @pow
                RTS

.endif

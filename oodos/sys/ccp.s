; OODOS's CCP (hw/oodos.md, "The CCP, phase 4"), in block 62's code window,
; written from the CP/M 3 User's Guide (1.2, 2.3-2.6, 4, and section 5's
; DIR, ERASE, RENAME, SETDEF, TYPE and USER). Where the Guide is silent the
; choice is ours, and hw/oodos.md says so.
;
; It runs as a program does: the program's windows (blocks 0-2, W3 on 62),
; its stack, the BDOS through $0203; no zero page. The warm boot (common.s's
; WARM) comes here with a fresh stack.
;
; At each warm boot: the base page's JMPs and $BFFD's; the RSXs a program
; wrote over dropped and the chain written again from the loader's table
; (loader.s: CHECKRSX, LINK); function 0 sent down the chain once, if the
; BDOS's function 0 didn't come this way (WBF); the RSXs whose remove flag is
; set taken out (REMOVE), but an RSX-only .COM's, kept for the next program
; (KEEP, KEEPNEW, UNKEEP); the SCB's $19 bit 7 set (CCPST: the CCP runs; its
; count steps as each command starts); the multi-sector count 1 (44); then, in error mode $FE (a failure said and gone past), the
; card's units read again (function 50's SELDSK of A:, a first select, so a
; swapped image's buffers are dropped, not written), 48 and 98; or, for a
; ^C at the prompt (ATPROMPT), 13 instead (the Programmer's Guide, 1.6.2).
; Then the CCP's own calls return their errors ($FF); the console mode 0,
; the delimiter '$'; 47's request (the SCB's $17: with CCP_CHAINDU the
; program's drive and user become the CCP's); the CCP's drive (by the SCB,
; so it isn't logged in until used) and user made current again. A program
; ended by ^C (the return code $FFFE) drops the rest of a line of several
; commands. Then 47's command, the rest of a line, or the prompt.
;
; The prompt is the CCP's user (when it isn't 0), its drive and '>'. A
; line (function 10, 255 characters) is commands separated by '!', each
; taken in capitals; each after the first is shown after a new prompt, as
; typed. A command:
;
;   (blank)         nothing
;   ;...            a comment: nothing
;   :command        the command if the return code (108) isn't $FF00-$FFFF
;   GO              the image at $0300, its header at $0280 (phase 2's)
;   DIR, DIRSYS (DIRS), ERASE (ERA), RENAME (REN), TYPE (TYP), USER (USE)
;                   the built-ins, or their utilities for forms they can't do
;   CLEAR, CLS      ours: the screen cleared; CLS then DIR, as DIR does it
;   WOZMON, APPLE2  ours: leave OODOS for the ROM (function 75)
;   d:, nd:         the CCP's drive (and user)
;   {d:}NAME{.COM}{;pw}
;                   NAME.COM: from d:, or each drive of the SCB's search
;                   chain, by Open (the current user, then user 0's SYS file),
;                   then loaded by function 59 (loader.s) at $0300, its
;                   attached RSXs first; an RSX-only .COM (size 0) loads its
;                   RSXs and runs nothing
;
; A program's tail goes to $0280 (its length, the characters, a 0), the two
; default FCBs from it by 152, the passwords' places at $0251-$0256, the
; drive it came from at $0250. The loader checks its header (hw/oodos.md,
; "The program header"; GO's, CHECKHDR, against the lowest RSX); it starts at
; $0300 with the DMA address $0280, the default error mode, the SCB's $19
; bit 7 clear, its SCB entries set (APPLYSCB), a fresh stack with the warm
; boot's address on it, and the return code 0 unless chained to.
;
; The built-ins set the return code: 0 done, $FF00 an error said, $FFFE
; stopped by ^C; so do the CCP's own refusals ($FF00). Their file calls run
; in error mode $FE, so the BDOS says its errors.

.setcpu "65C02"
.include "sys.inc"
.include "oodos.inc"
.import WARM, STUB
.import LINK, REMOVE, CHECKRSX, KEEPNEW, UNKEEP, TOPADDR, APPLYSCB, CCPSET, NRSX, KEEP, WBF, CCPST, LHDR
.export CCP, RUNHDR

.define CMD_DATA "RES"
.define CMD_BSS "RESBSS"
CMD_BREAK       = BIBREAK

LINE_MAX        = 255                  ; function 10's most
TAIL_MAX        = 126                  ; what $0280 holds with its length and a 0
CHAIN_MAX       = 126                  ; 47's command, at most
KW_MAX          = 23                   ; d:NAME.TYP;PASSWORD
RECORD          = 128
FCB_R0          = 33                   ; the FCB's random record, R0-R2
TAB             = $09
LF              = $0A
CR              = $0D
CTLZ            = $1A
BP_LOADDRV      = $0250                ; CP/M 3's 0050H: the drive it came from
BP_PW1          = $0251                ; the first specification's password: address, length
TAILSTR         = BP_DMA + 1           ; the tail's characters, as 152 reads them
DISPLAY_FLAG    = $80                  ; SCB $18: SETDEF's [DISPLAY] (ours)

.segment "RESBSS"
LINE:           .res    2 + LINE_MAX   ; function 10's: its size, the count, the line
LPOS:           .res    1              ; where the next command starts in it
CMD:            .res    LINE_MAX + 1   ; the command, in capitals, and a 0
CMDLEN:         .res    1
KWS:            .res    1              ; the keyword: CMD[KWS..KWE)
KWE:            .res    1
TAILBUF:        .res    TAIL_MAX + 8   ; the tail (DIRSYS may add "SYS,")
TAILLEN:        .res    1
PFCB:           .res    4              ; 152's: the string, the FCB
WFCB:           .res    24             ; 152's result
OP1:            .res    24             ; the tail's first two specifications, as 152 made them
OP2:            .res    24             ;   (OP2 is OP1 + 24: WILD, SETSFCB, PASSWD)
OP1S:           .res    1              ; where the first starts in the tail
OPS:            .res    1              ; specifications in the tail
OPTS:           .res    1              ; 1: an option list
PERR:           .res    1              ; 1: one 152 refused
DELIM1:         .res    1              ; the delimiter after the first (0: none)
IDX:            .res    1              ; SETTAIL's place in the tail
OPAT:           .res    1              ;   the specification's start
COMFCB:         .res    36             ; the program's FCB
SFCB:           .res    36             ; the built-ins'
LASTREC:        .res    RECORD         ; a record; the built-ins' DMA buffer
CEND:           .res    2              ; CHECKHDR's end of the image and BSS
ORDIX:          .res    1              ; the search order's place in ORDERS
SUBPRE:         .res    25             ; a .SUB's command: " d:NAME.SUB;PASSWORD"
COUNT:          .res    1              ; a count, for a moment (DIRSYS's tail, the parse)
CHAINF:         .res    1              ; 47's request, as the SCB had it
CHAINED:        .res    1              ; 1: the command is 47's
CHAINB:         .res    4              ; the drives to look on
CHIDX:          .res    1
LOADDRV:        .res    1              ; the drive a program came from, 1-16
GIVEN:          .res    1              ; the drive the keyword gave, 0 none
WANTSYS:        .res    1              ; DIR $00, DIRSYS $80
FOUND:          .res    1
OTHER:          .res    1
COL:            .res    1
NCOLS:          .res    1
DIRDRV:         .res    1              ; the letter DIR's lines start with
NEWUSER:        .res    1
SAVEB:          .res    1

.segment "RES"

CCP:            LDX     #2             ; the base page's JMPs, and $BFFD's
@jmps:          LDA     BASEJMPS,X
                STA     BP_WBOOT,X
                LDA     BASEJMPS + 3,X
                STA     BP_BDOS,X
                LDA     BASEJMPS + 6,X
                STA     BDOS_ENTRY,X
                DEX
                BPL     @jmps
                LDX     #$FF
                TXS
                JSR     CHECKRSX       ; $0203 and the RSXs' links from the loader's table,
                                       ; less any a program wrote over
                LDA     NRSX           ; every end of a program reaches the RSXs as
                BEQ     @ended         ; function 0, once (ours): the BDOS's comes back
                LDA     WBF            ; here, with WBF set
                BNE     @ended
                INC     WBF
                LDY     #P_TERMCPM
                JSR     BDOS
@ended:         STZ     WBF
                LDA     KEEP           ; the flagged RSXs out (the Guide's 1.6.4), but
                CMP     #2             ; an RSX-only .COM's, kept for the next program,
                BNE     @kept          ; go only after it
                JSR     UNKEEP
@kept:          JSR     REMOVE
                LDA     CCPST          ; the CCP runs, no command does (the SCB's $19
                ORA     #$80           ; bits 7 and 6, ours)
                AND     #$BF
                STA     CCPST
                JSR     CCPSET
                LDA     #1             ; a multi-sector count of 1, for its loads and
                LDY     #F_MULTISEC    ; each program (the Guide, 44)
                JSR     BDOS
                JSR     ERRSAY         ; a write-back that fails is said, and the
                LDA     ATPROMPT       ; warm boot goes on. A ^C at the prompt: 13,
                BEQ     @warm          ; not the warm boot's 48 and 98 (they ran
                STZ     ATPROMPT       ; before that prompt)
                LDY     #DRV_ALLRESET
                JSR     BDOS
                BRA     @own
@warm:          LDA     #<RESCANPB     ; the units read again (a first select of
                LDX     #>RESCANPB     ; A:), so 48 drops a swapped unit's buffers
                LDY     #S_BIOS
                JSR     BDOS
                LDA     #0             ; written back, and unclosed files' blocks freed
                LDY     #DRV_FLUSH
                JSR     BDOS
                LDY     #DRV_FREE
                JSR     BDOS
@own:           STZ     CM_LFDUE       ; nothing owed after a program
                JSR     ERRRET         ; the CCP's errors come back to it
                JSR     CONMODE0       ; a program's settings: the console mode 0,
                LDA     #'$'           ; the delimiter '$',
                LDY     #C_DELIMIT
                JSR     BDOS
                LDA     #<CHAINPB      ; 47's request, then none
                LDX     #>CHAINPB
                LDY     #S_SCB
                JSR     BDOS
                STA     CHAINF
                LDA     #<NOCHAINPB
                LDX     #>NOCHAINPB
                LDY     #S_SCB
                JSR     BDOS
                LDA     CHAINF         ; the program's drive and user the CCP's?
                AND     #CCP_CHAINDU
                BEQ     @drive
                LDY     #DRV_GET
                JSR     BDOS
                STA     CCPDRV
                LDA     #$FF
                LDY     #F_USERNUM
                JSR     BDOS
                STA     CCPUSR
@drive:         JSR     OWNDRV         ; the CCP's drive and user current again
                LDA     CCPUSR
                LDY     #F_USERNUM
                JSR     BDOS
                LDA     #$FF           ; a program ended by ^C: the rest of its line
                TAX                    ; dropped (ours)
                LDY     #P_CODE
                JSR     BDOS
                CMP     #$FE
                BNE     @rest
                CPX     #$FF
                BNE     @rest
                STZ     PENDING
@rest:          LDA     CHAINF
                BPL     :+
                JMP     CHAIN
:
                LDA     PENDING
                BEQ     PROMPT
                JMP     NEXTCMD

PROMPT:         LDX     #$FF
                TXS
                STZ     CHAINED
                STZ     PENDING
                LDA     COLDF          ; the first prompt after a cold boot: PROFILE.SUB
                BEQ     @line          ; on the default drive (5c)
                STZ     COLDF
                JSR     PROFILE
@line:          BIT     CCPST          ; the way to the prompt (5c): a built-in's end,
                BVC     @again         ; bit 6 cleared, then $19 written again with it
                LDA     CCPST          ; clear, as no command's end writes it: SUBMIT's
                AND     #$FF ^ CCP_CMD ; RSX skips the < lines left here, before the
                STA     CCPST          ; prompt, and not between a ! line's commands
                JSR     CCPSET         ; (a program's end clears bit 6 too, for GET and
@again:         JSR     CCPSET         ; PUT, which close their files there)
                JSR     SAYPROMPT
                LDA     #LINE_MAX
                STA     LINE
                LDA     #1             ; a ^C now is function 10's warm boot from the
                STA     ATPROMPT       ; prompt
                LDA     #<LINE
                LDX     #>LINE
                LDY     #C_READSTR
                JSR     BDOS
                STZ     ATPROMPT
                LDA     #1             ; function 10 echoed only the CR: an LF owed,
                STA     CM_LFDUE       ; written before any output (cmd.inc)
                STZ     LPOS
                JMP     RUNCMD

; PROFILE.SUB on the CCP's drive (user 0, cold boot's)? Then SUBMIT PROFILE, shown
; after the prompt as if typed, and run; back if it isn't there.
PROFILE:        LDX     #35
@z:             STZ     SFCB,X
                DEX
                BPL     @z
                LDA     CCPDRV
                INC     A
                STA     SFCB
                LDX     #10
@n:             LDA     PROFN,X
                STA     SFCB + 1,X
                DEX
                BPL     @n
                JSR     DMALAST
                LDA     #<SFCB
                LDX     #>SFCB
                LDY     #F_SFIRST
                JSR     BDOS
                CMP     #$FF
                BNE     @run
                RTS
@run:           PLA                    ; PROFILE's return dropped
                PLA
                LDX     #0
@c:             LDA     PROFCMD,X
                BEQ     @end
                STA     LINE + 2,X
                INX
                BRA     @c
@end:           STX     LINE + 1
                JSR     SAYPROMPT
                LDX     #0
@show:          CPX     LINE + 1
                BEQ     @shown
                LDA     LINE + 2,X
                JSR     PUTC
                INX
                BRA     @show
@shown:         JSR     SHOWN
                STZ     LPOS
                JMP     RUNCMD

; 47's command: from $0280 to its 0 (CHAIN_MAX at most) as the line, shown
; after the prompt as if typed. It takes the place of the rest of a line.
CHAIN:          LDX     #0
@copy:          LDA     BP_DMA,X
                BEQ     @end
                CPX     #CHAIN_MAX
                BEQ     @end
                STA     LINE + 2,X
                INX
                BRA     @copy
@end:           STX     LINE + 1
                LDA     #1
                STA     CHAINED
                JSR     SAYPROMPT
                LDX     #0
@show:          CPX     LINE + 1
                BEQ     @shown
                LDA     LINE + 2,X
                JSR     PUTC
                INX
                BRA     @show
@shown:         JSR     SHOWN
                STZ     LPOS
                JMP     RUNCMD

; The next command of a line: shown after a new prompt, as typed.
NEXTCMD:        LDX     #$FF
                TXS
                STZ     CHAINED        ; only the first of 47's line was chained to
                JSR     SAYPROMPT
                LDX     LPOS
@blank:         CPX     LINE + 1       ; past blanks, tabs and empty commands' '!'s
                BEQ     @start
                LDA     LINE + 2,X
                CMP     #' '
                BEQ     @past
                CMP     #TAB
                BEQ     @past
                CMP     #'!'
                BNE     @start
@past:          INX
                BRA     @blank
@start:         STX     LPOS           ; the command starts here, for RUNCMD
@show:          CPX     LINE + 1
                BEQ     @shown
                LDA     LINE + 2,X
                BEQ     @shown         ; a 0 ends the line (RUNCMD)
                CMP     #'!'
                BEQ     @shown
                JSR     PUTC
                INX
                BRA     @show
@shown:         JSR     SHOWN
                ; falls through

; The command at LPOS: to CMD in capitals, to a '!' or the line's end (a 0
; ends it too, as 152 and the tail's 0 would), its trailing blanks and tabs
; dropped. A '!' with only blanks, tabs and '!'s after it leaves nothing
; pending; NEXTCMD skips the empty commands between '!'s.
RUNCMD:         LDX     #$FF
                TXS
                LDX     LPOS
                LDY     #0
@copy:          CPX     LINE + 1
                BEQ     @eol
                LDA     LINE + 2,X
                BEQ     @eol
                INX
                CMP     #'!'
                BEQ     @bang
                JSR     UPPER
                STA     CMD,Y
                INY
                BRA     @copy
@bang:          STX     LPOS
@rest:          CPX     LINE + 1       ; anything after it?
                BEQ     @eol
                LDA     LINE + 2,X
                BEQ     @eol
                INX
                CMP     #' '
                BEQ     @rest
                CMP     #TAB
                BEQ     @rest
                CMP     #'!'           ; an empty command
                BEQ     @rest
                LDA     #1
                STA     PENDING
                BRA     @split
@eol:           STZ     PENDING
@split:         CPY     #0             ; trailing blanks and tabs dropped
                BEQ     @trimmed
                LDA     CMD - 1,Y
                CMP     #' '
                BEQ     @cut
                CMP     #TAB
                BNE     @trimmed
@cut:           DEY
                BRA     @split
@trimmed:       LDA     #0
                STA     CMD,Y
                STY     CMDLEN
                LDX     #0
                JSR     SKIPCMD
                LDA     CMD,X
                BEQ     DONE           ; empty
                CMP     #';'
                BEQ     DONE           ; a comment
                CMP     #':'
                BNE     KEYWORD
                INX                    ; conditional: only after a success
                STX     KWS
                LDA     #$FF
                TAX
                LDY     #P_CODE
                JSR     BDOS
                CPX     #$FF
                BEQ     DONE           ; $FF00-$FFFF: skipped, the code kept
                LDX     KWS
                JSR     SKIPCMD
                LDA     CMD,X
                BEQ     DONE
KEYWORD:        STX     KWS            ; to a blank, a tab or the end
                LDA     CCPST          ; a command starts: the count on, bit 6 set (ours)
                INC     A
                AND     #CCP_COUNT
                ORA     #$80 | CCP_CMD
                STA     CCPST
                JSR     CCPSET
                LDX     KWS
@kw:            LDA     CMD,X
                BEQ     @end
                CMP     #' '
                BEQ     @end
                CMP     #TAB
                BEQ     @end
                INX
                BRA     @kw
@end:           STX     KWE
                SEC                    ; the tail: the rest, its leading blank too
                LDA     CMDLEN
                SBC     KWE
                CMP     #TAIL_MAX + 1
                BCC     @fits
                LDA     #<TOOLONG
                LDX     #>TOOLONG
                JMP     FAILSAY
@fits:          STA     TAILLEN
                LDX     KWE
                LDY     #0
@tail:          CPY     TAILLEN
                BEQ     @tailed
                LDA     CMD,X
                STA     TAILBUF,Y
                INX
                INY
                BRA     @tail
@tailed:        JSR     BUILTIN        ; a built-in's? (back if not)
                JSR     DRVFORM        ; d: or nd:? (back if not)
                JMP     PROGRAM

; After a command the CCP did itself: the line's next, or the prompt.
DONE:           LDA     PENDING
                BEQ     @prompt
                JMP     NEXTCMD
@prompt:        JMP     PROMPT

; A command shown as if typed ends as function 10 ends a line: a CR, the LF
; owed.
SHOWN:          LDA     #CR
                JSR     PUTC
                LDA     #1
                STA     CM_LFDUE
                RTS

; X past blanks and tabs in CMD.
SKIPCMD:        LDA     CMD,X
                CMP     #' '
                BEQ     @next
                CMP     #TAB
                BNE     @done
@next:          INX
                BRA     SKIPCMD
@done:          RTS

; CR LF and the prompt: the CCP's user if not 0, its drive, '>'. An LF owed
; is the CR LF's (so a command that wrote nothing has its prompt on the next
; line, as the Guide shows them).
SAYPROMPT:      BIT     CCPST          ; a command ended: bit 6 clear before the prompt
                BVC     @shown         ; (the SCB's $19, ours)
                LDA     CCPST
                AND     #$FF ^ CCP_CMD
                STA     CCPST
                JSR     CCPSET
@shown:         JSR     CRLF
                LDA     CCPUSR
                BEQ     @drive
                LDX     #0
                JSR     CM_DECOUT
@drive:         LDA     CCPDRV
                CLC
                ADC     #'A'
                JSR     PUTC
                LDA     #'>'
                JMP     PUTC

; The CCP's drive current, by the SCB (14 would log it in).
OWNDRV:         LDA     CCPDRV
                STA     DISKPB + 2
                LDA     #<DISKPB
                LDX     #>DISKPB
                LDY     #S_SCB
                JMP     BDOS

ERRRET:         LDA     #$FF           ; errors returned
                BRA     ERRMODE
ERRSAY:         LDA     #$FE           ; errors said, then returned
ERRMODE:        LDY     #F_ERRMODE
                JMP     BDOS

CONMODE0:       STZ     CM_PAGED       ; (cmd.inc's: paging ends with the mode)
                LDA     #0
                TAX
                LDY     #C_MODE
                JMP     BDOS

; The return code: 0, $FF00.
CODEOK:         LDA     #0
                TAX
                BRA     SETCODE
CODEFAIL:       LDA     #0
                LDX     #$FF
SETCODE:        LDY     #P_CODE
                JMP     BDOS

; ---------------------------------------------------------------------------
; The built-ins. BUILTIN returns if the keyword names none (one with a
; drive or a type is a program's); else it jumps to it.

BUILTIN:        LDX     #0             ; BINAMES: a length, the name, the handler
@try:           LDA     BINAMES,X
                BEQ     @none
                STA     SAVEB
                SEC                    ; the keyword's length must match
                LDA     KWE
                SBC     KWS
                CMP     SAVEB
                BNE     @skip
                PHX
                LDY     KWS
@cmp:           LDA     BINAMES + 1,X
                CMP     CMD,Y
                BNE     @no
                INX
                INY
                DEC     SAVEB
                BNE     @cmp
                LDA     BINAMES + 1,X  ; X: past the name, at its handler
                STA     @go + 1
                LDA     BINAMES + 2,X
                STA     @go + 2
                PLX
                PLA                    ; BUILTIN's return dropped
                PLA
@go:            JMP     $FFFF
@no:            PLX
@skip:          LDA     BINAMES,X      ; the next: past the length, name, handler
                CLC
                ADC     #3
                STA     SAVEB
                TXA
                CLC
                ADC     SAVEB
                TAX
                BRA     @try
@none:          RTS

; Each built-in's start: its file errors said, the tail parsed.
BIENTER:        JSR     ERRSAY
                JMP     SETTAIL

; A built-in's end: the return code, the CCP's error and console modes.
BIOK:           JSR     CODEOK
                BRA     BIEND
BIFAIL:         JSR     CODEFAIL
BIEND:          JSR     ERRRET
                JSR     CONMODE0
                JMP     DONE
; ^C during paged output (cmd.inc's CMD_BREAK): $FFFE, the line dropped.
BIBREAK:        LDX     #$FF
                TXS
                LDA     #$FE
                LDX     #$FF
                JSR     SETCODE
                STZ     PENDING
                JSR     ERRRET
                JSR     CONMODE0
                JMP     PROMPT

; The message at A/X, then a failure.
FAILSAY:        JSR     STRING
                JMP     BIFAIL
NOFILE:         LDA     #<CM_NOFILE
                LDX     #>CM_NOFILE
                BRA     FAILSAY
; Error 10 (O:'s or P:'s disk changed under the command), which the BDOS
; returns without a message.
MEDIA:          LDA     #<MEDIACHG
                LDX     #>MEDIACHG
                BRA     FAILSAY

; The command, from its keyword, in capitals, and '?'.
BADCMD:         LDX     KWS
                LDY     CMDLEN         ; Y: past its last non-blank
@trim:          CPY     KWS
                BEQ     @say
                LDA     CMD - 1,Y
                CMP     #' '
                BEQ     @back
                CMP     #TAB
                BNE     @say
@back:          DEY
                BRA     @trim
@say:           STY     SAVEB
@char:          CPX     SAVEB
                BEQ     @q
                LDA     CMD,X
                JSR     PUTC
                INX
                BRA     @char
@q:             LDA     #'?'
                JSR     PUTC
                JMP     BIFAIL

; Is any of the 11 bytes at OP1 + 1 (Y = 0) or OP2 + 1 (Y = 24) '?'? Carry
; set if so.
WILD:           LDX     #11
@look:          LDA     OP1 + 1,Y
                CMP     #'?'
                BEQ     @yes
                INY
                DEX
                BNE     @look
                CLC
                RTS
@yes:           SEC
                RTS

; SFCB: drive A, the name at OP1 + 1 (Y = 0) or OP2 + 1 (Y = 24), the rest 0;
; the DMA address LASTREC.
SETSFCB:        STA     SFCB
                LDX     #0
@name:          LDA     OP1 + 1,Y
                STA     SFCB + 1,X
                INY
                INX
                CPX     #11
                BNE     @name
                LDA     #0
@zero:          STA     SFCB + 1,X
                INX
                CPX     #35
                BNE     @zero
                ; falls through
; The DMA address LASTREC, for a search, a record or a password.
DMALAST:        LDA     #<LASTREC
                LDX     #>LASTREC
                LDY     #F_DMAOFF
                JMP     BDOS

; The password at OP1 + 16 (Y = 0) or OP2 + 16 (Y = 24) to LASTREC, where the
; BDOS looks for it.
PASSWD:         LDX     #0
@pw:            LDA     OP1 + 16,Y
                STA     LASTREC,X
                INY
                INX
                CPX     #8
                BNE     @pw
                RTS

CLEARSCR:       LDA     #<CLEARS
                LDX     #>CLEARS
                JMP     STRING

; Search (17) for SFCB: A = $FF none (X 0) or an error (X its code, said).
SEARCH:         LDA     #<SFCB
                LDX     #>SFCB
                LDY     #F_SFIRST
                JMP     BDOS

; CLEAR and CLS (ours, 2026-11-03): the screen cleared by ESC E, which the
; BIOS sends on the serial line as ANSI's clear. CLEAR's tail is ignored; CLS
; is DIR with its tail on the cleared screen, DIR.COM's forms included.
B_CLEAR:        JSR     BIENTER
                JSR     CLEARSCR
                JMP     BIOK
B_CLS:          JSR     CLEARSCR
                BRA     B_DIR

; WOZMON and APPLE2 (ours, 2026-11-03): leave OODOS for the ROM BIOS by
; function 75, which writes back the changed buffers first. WOZMON clears
; the screen, then the Woz Monitor, as Break; it doesn't come back. APPLE2
; restarts in Apple II mode if the cartridge holds a IIe ROM, and otherwise
; says so, the return code $FF00, and OODOS goes on. Their tails are ignored.
B_WOZMON:       JSR     BIENTER
                JSR     CLEARSCR
                LDA     #0             ; the monitor
                LDY     #O_ROM
                JSR     BDOS
                JMP     BIOK           ; never: 75 with A = 0 doesn't return
B_APPLE2:       JSR     BIENTER
                LDA     #1             ; Apple II mode
                LDY     #O_ROM
                JSR     BDOS           ; back only if there is no IIe ROM
                LDA     #<NOIIE
                LDX     #>NOIIE
                JMP     FAILSAY


; DIR and DIRSYS (DIRS).
B_DIR:          STZ     WANTSYS
                BRA     DIRBOTH
B_DIRS:         LDA     #$80
                STA     WANTSYS
DIRBOTH:        JSR     BIENTER
                LDA     PERR
                BEQ     @parsed
                JMP     BADCMD
@parsed:        LDA     OPTS
                BNE     @util
                LDA     OPS
                CMP     #2
                BCC     @mine
@util:          LDA     WANTSYS
                BEQ     @dir
                JSR     SYSOPT         ; DIRSYS's options get SYS
@dir:           LDX     #<(UDIR - UNAMES)
                JMP     HANDOFF
@mine:          LDY     #0
                LDA     OP1            ; no specification: OP1 is blank, drive 0
                JSR     SETSFCB
                LDX     #11            ; a blank name and type: every file
@blank:         LDA     SFCB,X
                CMP     #' '
                BNE     @named
                DEX
                BNE     @blank
                LDX     #11
                LDA     #'?'
@all:           STA     SFCB,X
                DEX
                BNE     @all
@named:         LDA     SFCB           ; the letter its lines start with
                BNE     @given
                LDA     CCPDRV
                INC     A
@given:         CLC
                ADC     #'A' - 1
                STA     DIRDRV
                JSR     CM_PAGE
                LDA     #<SCBWIDTH     ; names a line: the width less its last column,
                LDX     #>SCBWIDTH     ; 15 a name (ours)
                LDY     #S_SCB
                JSR     BDOS
                LDX     #0
@div:           CMP     #15
                BCC     @divd
                SBC     #15
                INX
                BRA     @div
@divd:          TXA
                BNE     @cols
                INC     A
@cols:          STA     NCOLS
                STZ     COL
                STZ     FOUND
                STZ     OTHER
                JSR     SEARCH
@entry:         CMP     #$FF
                BEQ     @end
                ASL     A              ; its offset in LASTREC
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                TAX
                LDA     LASTREC + 10,X ; t2': SYS
                AND     #$80
                CMP     WANTSYS
                BEQ     @list
                LDA     #1
                STA     OTHER
                BRA     @next
@list:          STX     SAVEB
                LDA     COL
                BNE     @more
                LDA     FOUND
                BEQ     @first
                JSR     CM_NL          ; a new line: paged, ^C looked for
@first:         LDA     DIRDRV
                JSR     PUTC
                LDA     #':'
                JSR     PUTC
                LDA     #' '
                JSR     PUTC
                BRA     @name
@more:          LDA     #<SEP
                LDX     #>SEP
                JSR     STRING
@name:          LDA     SAVEB
                CLC
                ADC     #<(LASTREC + 1)
                PHA
                LDA     #>(LASTREC + 1)
                ADC     #0
                TAX
                PLA
                JSR     CM_DIRNM
                LDA     #1
                STA     FOUND
                INC     COL
                LDA     COL
                CMP     NCOLS
                BNE     @next
                STZ     COL
@next:          LDY     #F_SNEXT
                JSR     BDOS
                BRA     @entry
@end:           CPX     #0
                BEQ     @searched
                JMP     BIFAIL         ; the BDOS said what
@searched:      LDA     FOUND
                ORA     OTHER
                BNE     @some
                JMP     NOFILE
@some:          LDA     OTHER
                BEQ     @ok
                LDA     FOUND
                BEQ     @say
                JSR     CM_NL
@say:           LDA     #<SYSEXIST
                LDX     #>SYSEXIST
                LDY     WANTSYS
                BEQ     @msg
                LDA     #<NONSYSEXIST
                LDX     #>NONSYSEXIST
@msg:           JSR     STRING
@ok:            JMP     BIOK

; DIRSYS's tail for DIR.COM: "SYS," after its option list's '[', or " [SYS]"
; after it with none.
SYSOPT:         LDX     #0
@find:          CPX     TAILLEN
                BEQ     @none
                LDA     TAILBUF,X
                INX
                CMP     #'['
                BNE     @find
                LDY     #4             ; "SYS," at X, after the '['
                LDA     #<SYSINS
                STA     @ins + 1
                LDA     #>SYSINS
                STA     @ins + 2
                BRA     @insert
@none:          LDY     #6             ; " [SYS]" at the end
                LDA     #<SYSADD
                STA     @ins + 1
                LDA     #>SYSADD
                STA     @ins + 2
@insert:        STY     SAVEB          ; X: where, SAVEB: how many
                CLC
                LDA     TAILLEN
                ADC     SAVEB
                CMP     #TAIL_MAX + 1
                BCC     @room
                LDA     #<TOOLONG
                LDX     #>TOOLONG
                JMP     FAILSAY
@room:          STX     COUNT
                LDY     TAILLEN        ; the rest moved up, from its end: Y the old index
@move:          CPY     COUNT
                BEQ     @moved
                DEY
                LDA     TAILBUF,Y
                PHY
                PHA
                TYA
                CLC
                ADC     SAVEB
                TAY
                PLA
                STA     TAILBUF,Y
                PLY
                BRA     @move
@moved:         LDY     #0
@ins:           LDA     $FFFF,Y
                STA     TAILBUF,X
                INX
                INY
                CPY     SAVEB
                BNE     @ins
                LDA     TAILLEN
                CLC
                ADC     SAVEB
                STA     TAILLEN
                RTS

; ERA (ERASE) filespec: wildcards ask first.
B_ERA:          JSR     BIENTER
                LDA     PERR
                BNE     @bad
                LDA     OPTS
                BNE     @util
                LDA     OPS
                BEQ     @util
                CMP     #1
                BEQ     @one
@bad:           JMP     BADCMD
@util:          LDX     #<(UERA - UNAMES)
                JMP     HANDOFF
@one:           LDY     #0
                JSR     WILD
                BCC     @erase
                LDA     #<ERAQ         ; "ERASE spec (Y/N)?", the spec as typed
                LDX     #>ERAQ
                JSR     STRING
                LDX     OP1S
@spec:          CPX     TAILLEN
                BEQ     @specd
                LDA     TAILBUF,X
                CMP     #' '
                BEQ     @specd
                CMP     #TAB
                BEQ     @specd
                CMP     #';'
                BEQ     @specd
                CMP     #'['
                BEQ     @specd
                JSR     PUTC
                INX
                BRA     @spec
@specd:         LDA     #<YNQ
                LDX     #>YNQ
                JSR     STRING
                JSR     CM_ASK
                BCS     @erase
                JMP     BIOK           ; not erased, as asked
@erase:         LDY     #0
                LDA     OP1
                JSR     SETSFCB
                LDY     #0
                JSR     PASSWD
                LDA     #<SFCB
                LDX     #>SFCB
                LDY     #F_DELETE
                JSR     BDOS
                CMP     #0             ; 0 deleted; 10 O:'s or P:'s disk changed, nothing
                BEQ     @done          ; deleted (the BDOS doesn't say it)
                CMP     #$FF
                BEQ     :+
                JMP     MEDIA
:               CPX     #0
                BNE     @said
                JMP     NOFILE
@said:          JMP     BIFAIL
@done:          JMP     BIOK

; REN (RENAME) new=old.
B_REN:          JSR     BIENTER
                LDA     PERR
                BNE     @bad
                LDA     OPTS
                BNE     @util
                LDA     OPS
                BEQ     @util
                CMP     #2
                BNE     @bad
                LDA     DELIM1
                CMP     #'='
                BNE     @bad
                LDA     OP1 + 1        ; neither name blank
                CMP     #' '
                BEQ     @bad
                LDA     OP2 + 1
                CMP     #' '
                BEQ     @bad
                LDY     #0
                JSR     WILD
                BCS     @util
                LDY     #24
                JSR     WILD
                BCS     @util
                LDA     OP1            ; the drive: new's or old's, the same if both
                BEQ     @old
                LDX     OP2
                BEQ     @drive
                CMP     OP2
                BEQ     @drive
@bad:           JMP     BADCMD
@util:          LDX     #<(UREN - UNAMES)
                JMP     HANDOFF
@old:           LDA     OP2
@drive:         STA     LOADDRV        ; (the drive, for a moment)
                LDX     #10            ; new the same file as old: refused (a Y to
@same:          LDA     OP1 + 1,X      ; delete new would delete the only copy)
                CMP     OP2 + 1,X
                BNE     @differ
                DEX
                BPL     @same
                JMP     BADCMD
@differ:        LDA     LOADDRV
                LDY     #24            ; old there?
                JSR     SETSFCB
                JSR     SEARCH
                CMP     #$FF
                BNE     @new
                CPX     #0
                BNE     @said
                JMP     NOFILE
@new:           LDA     LOADDRV        ; new there already?
                LDY     #0
                JSR     SETSFCB
                JSR     SEARCH
                CMP     #$FF
                BNE     @exists
                CPX     #0
                BEQ     @rename
@said:          JMP     BIFAIL
@exists:        LDA     #<NOTREN
                LDX     #>NOTREN
                JSR     STRING
                LDA     #<(OP1 + 1)
                LDX     #>(OP1 + 1)
                JSR     CM_FNAME
                LDA     #<EXISTSQ
                LDX     #>EXISTSQ
                JSR     STRING
                JSR     CM_ASK
                BCS     @delete
                JMP     BIFAIL         ; not renamed
@delete:        LDY     #0
                JSR     PASSWD         ; new's password, for its delete
                LDA     #<SFCB
                LDX     #>SFCB
                LDY     #F_DELETE
                JSR     BDOS
                CMP     #0
                BEQ     @rename
                CMP     #$FF
                BEQ     @said
                JMP     MEDIA          ; 10: the disk changed, nothing done
@rename:        LDY     #24            ; SFCB: old, then new at 16
                LDA     LOADDRV
                JSR     SETSFCB
                LDX     #0
@newname:       LDA     OP1 + 1,X
                STA     SFCB + 17,X
                INX
                CPX     #11
                BNE     @newname
                LDY     #24
                JSR     PASSWD         ; old's password
                LDA     #<SFCB
                LDX     #>SFCB
                LDY     #F_RENAME
                JSR     BDOS
                CMP     #0
                BEQ     @done
                CMP     #$FF
                BEQ     :+
                JMP     MEDIA          ; 10: the disk changed, nothing renamed
:               CPX     #0
                BNE     @said
                JMP     NOFILE
@done:          JMP     BIOK

; TYPE (TYP) filespec: to the first ^Z, paged.
B_TYPE:         JSR     BIENTER
                LDA     PERR
                BNE     @bad
                LDA     OPTS
                BNE     @util
                LDA     OPS
                BEQ     @util
                CMP     #1
                BNE     @bad
                LDY     #0
                JSR     WILD
                BCC     @one
                BRA     @util          ; a wildcard: TYPE.COM's
@bad:           JMP     BADCMD
@util:          LDX     #<(UTYPE - UNAMES)
                JMP     HANDOFF
@one:           LDY     #0
                LDA     OP1
                JSR     SETSFCB
                LDY     #0
                JSR     PASSWD
                LDA     #<SFCB
                LDX     #>SFCB
                LDY     #F_OPEN
                JSR     BDOS
                CMP     #$FF
                BNE     @open
                CPX     #0
                BNE     @said
                JMP     NOFILE
@said:          JMP     BIFAIL
@open:          JSR     CM_PAGE
@record:        LDA     #<SFCB
                LDX     #>SFCB
                LDY     #F_READ
                JSR     BDOS
                CMP     #0
                BEQ     @read
                CMP     #1             ; the end
                BEQ     @done
                CMP     #10            ; the disk changed: said, as the BDOS doesn't, on a
                BNE     @said          ; line of its own
                JSR     CM_CRLF
                JMP     MEDIA
@read:          LDX     #0
@char:          LDA     LASTREC,X
                CMP     #CTLZ
                BEQ     @done
                PHX
                PHA
                JSR     CM_BRK
                PLA
                PHA
                JSR     PUTC
                PLA
                CMP     #LF
                BNE     @next
                JSR     CM_LINE
@next:          PLX
                INX
                CPX     #RECORD
                BNE     @char
                BRA     @record
@done:          JMP     BIOK

; USER (USE) {n}: 0-15. Without one it asks.
B_USER:         JSR     BIENTER
                LDY     #0
@blank:         CPY     TAILLEN
                BEQ     @ask
                LDA     TAILBUF,Y
                INY
                CMP     #' '
                BEQ     @blank
                CMP     #TAB
                BEQ     @blank
                LDY     TAILLEN        ; given: the tail, ended by a 0
                LDA     #0
                STA     TAILBUF,Y
                LDA     #<TAILBUF
                LDX     #>TAILBUF
                BRA     @number
@ask:           LDA     #<ASKUSER
                LDX     #>ASKUSER
                JSR     STRING
                JSR     CM_READ
                LDA     #<(CM_ANS + 2)
                LDX     #>(CM_ANS + 2)
@number:        STA     @src + 1       ; past blanks, the number, then only blanks
                STX     @src + 2
                LDY     #0
@lead:          JSR     @src
                CMP     #' '
                BEQ     @lead1
                CMP     #TAB
                BNE     @digits
@lead1:         INY
                BRA     @lead
@digits:        LDA     @src + 1
                LDX     @src + 2
                JSR     CM_DECIN
                BCS     @bad
                CMP     #16
                BCS     @bad
                STA     NEWUSER
@trail:         JSR     @src
                BEQ     @set
                CMP     #' '
                BEQ     @trail1
                CMP     #TAB
                BNE     @bad
@trail1:        INY
                BRA     @trail
@bad:           JMP     BADCMD
@set:           LDA     NEWUSER
                STA     CCPUSR
                LDY     #F_USERNUM
                JSR     BDOS
                JMP     BIOK
@src:           LDA     $FFFF,Y        ; (called) the string's Y'th, Z set for its 0
                RTS

; d: or nd: (n 0-15): the CCP's drive, checked by 14, and user. Returns if
; the keyword isn't one.
DRVFORM:        STZ     NEWUSER
                LDA     #$FF
                STA     SAVEB          ; no user given
                LDX     KWS
                SEC
                LDA     KWE
                SBC     KWS
                CMP     #2
                BEQ     @letter
                CMP     #5
                BCS     @not
                LDA     #<CMD          ; digits, then a letter and ':'
                LDX     #>CMD
                LDY     KWS
                JSR     CM_DECIN
                BCS     @not
                STA     NEWUSER
                STZ     SAVEB
                TYA
                TAX
                CLC
                ADC     #2
                CMP     KWE
                BNE     @not
@letter:        LDA     CMD + 1,X
                CMP     #':'
                BNE     @not
                LDA     CMD,X
                SEC
                SBC     #'A'
                CMP     #16
                BCC     @drive
@not:           RTS
@drive:         STA     LOADDRV
                PLA                    ; it is one: DRVFORM's return dropped
                PLA
                LDA     NEWUSER
                CMP     #16
                BCC     @user
                JMP     BADCMD
@user:          LDX     #0             ; nothing may follow it
@tail:          CPX     TAILLEN
                BEQ     @select
                LDA     TAILBUF,X
                INX
                CMP     #' '
                BEQ     @tail
                CMP     #TAB
                BEQ     @tail
                JMP     BADCMD
@select:        JSR     ERRSAY
                LDA     LOADDRV
                LDY     #DRV_SET
                JSR     BDOS
                CMP     #$FF
                BNE     @ok
                JSR     OWNDRV         ; said by the BDOS; the CCP's drive stays
                JMP     BIFAIL
@ok:            LDA     LOADDRV
                STA     CCPDRV
                LDA     SAVEB
                BNE     @done
                LDA     NEWUSER
                STA     CCPUSR
                LDY     #F_USERNUM
                JSR     BDOS
@done:          JMP     BIOK

; A built-in's utility (X: its name in UNAMES), run as a program with the
; tail as given; not found: "NAME.COM required".
HANDOFF:        LDY     #0
@name:          LDA     UNAMES,X
                STA     COMFCB + 1,Y
                INX
                INY
                CPY     #11
                BNE     @name
                STZ     COMFCB
                LDX     #7             ; no password
                LDA     #' '
@pw:            STA     PWBUF,X
                DEX
                BPL     @pw
                JSR     ERRRET
                JSR     CONMODE0
                JSR     LOADPROG
                JSR     CM_FLUSH
                LDA     #<(COMFCB + 1)
                LDX     #>(COMFCB + 1)
                JSR     CM_FNAME
                LDA     #<REQUIRED
                LDX     #>REQUIRED
                JMP     FAILSAY

; ---------------------------------------------------------------------------
; The tail: TAILBUF to $0280 (its length, the characters, a 0), and its
; first two file specifications into the default FCBs by 152, OP1 and OP2
; keeping them whole (their passwords too), the passwords' places at
; $0251-$0256. OPS counts the specifications, OPTS an option list, PERR one
; 152 refused (which ends the parse), DELIM1 the delimiter after the first.

SETTAIL:        LDX     TAILLEN
                STX     BP_DMA
                STZ     TAILSTR,X
@copy:          DEX
                BMI     @fcbs
                LDA     TAILBUF,X
                STA     TAILSTR,X
                BRA     @copy
@fcbs:          LDX     #BP_DMA - BP_FCB - 1 ; FCBs: drive 0, the names blank,
@zero:          STZ     BP_FCB,X       ; the rest 0
                DEX
                BPL     @zero
                LDA     #' '
                LDX     #10
@blank:         STA     BP_FCB + 1,X
                STA     BP_FCB2 + 1,X
                STA     OP1 + 1,X
                STA     OP2 + 1,X
                DEX
                BPL     @blank
                LDX     #7
@pwb:           STA     OP1 + 16,X
                STA     OP2 + 16,X
                DEX
                BPL     @pwb
                STZ     OP1
                STZ     OP2
                LDX     #5
@pwz:           STZ     BP_PW1,X
                DEX
                BPL     @pwz
                STZ     OPS
                STZ     OPTS
                STZ     PERR
                STZ     DELIM1
                STZ     OP1S
                STZ     IDX
@next:          LDX     IDX            ; past blanks and option lists
@skip:          LDA     TAILSTR,X
                BEQ     @end
                CMP     #' '
                BEQ     @sk
                CMP     #TAB
                BEQ     @sk
                CMP     #'['
                BNE     @spec
                LDA     #1
                STA     OPTS
@close:         INX                    ; to after its ']'
                LDA     TAILSTR,X
                BEQ     @end
                CMP     #']'
                BNE     @close
@sk:            INX
                BRA     @skip
@end:           RTS
@spec:          STX     OPAT
                TXA
                CLC
                ADC     #<TAILSTR
                STA     PFCB
                LDA     #>TAILSTR
                ADC     #0
                STA     PFCB + 1
                LDA     #<WFCB
                STA     PFCB + 2
                LDA     #>WFCB
                STA     PFCB + 3
                LDA     #<PFCB
                LDX     #>PFCB
                LDY     #F_PARSE
                JSR     BDOS
                CPX     #$FF
                BNE     @parsed
                CMP     #$FF
                BNE     @parsed
                LDA     #1             ; refused: the parse stops
                STA     PERR
                RTS
@parsed:        STA     SAVEB          ; what 152 returned: 0, or an address
                STX     COUNT
                INC     OPS
                LDA     OPS
                CMP     #3
                BCS     @after
                CMP     #1
                BNE     @second
                LDA     OPAT
                STA     OP1S
                LDX     #23            ; the first: OP1, $025C, $0251
@op1:           LDA     WFCB,X
                STA     OP1,X
                DEX
                BPL     @op1
                LDX     #15
@fcb1:          LDA     WFCB,X
                STA     BP_FCB,X
                DEX
                BPL     @fcb1
                LDX     #0
                BRA     @pass
@second:        LDX     #23            ; the second: OP2, $026C, $0254
@op2:           LDA     WFCB,X
                STA     OP2,X
                DEX
                BPL     @op2
                LDX     #15
@fcb2:          LDA     WFCB,X
                STA     BP_FCB2,X
                DEX
                BPL     @fcb2
                LDX     #3
@pass:          LDA     WFCB + 16      ; a password: its place after the ';'
                CMP     #' '
                BEQ     @after
                LDY     OPAT
@semi:          LDA     TAILSTR,Y
                BEQ     @after
                INY
                CMP     #';'
                BNE     @semi
                TYA
                CLC
                ADC     #<TAILSTR
                STA     BP_PW1,X
                LDA     #>TAILSTR
                ADC     #0
                STA     BP_PW1 + 1,X
                LDY     #0             ; its length: to the first blank of 8
@len:           LDA     WFCB + 16,Y
                CMP     #' '
                BEQ     @lend
                INY
                CPY     #8
                BNE     @len
@lend:          TYA
                STA     BP_PW1 + 2,X
@after:         LDA     SAVEB          ; 0: the end
                ORA     COUNT
                BNE     @more
                RTS
@more:                SEC                    ; the index of what 152 stopped at
                LDA     SAVEB
                SBC     #<TAILSTR
                TAX
                LDA     TAILSTR,X
                LDY     OPS
                CPY     #1
                BNE     @delim
                CMP     #' '
                BEQ     @delim
                STA     DELIM1
@delim:         CMP     #' '
                BEQ     @at
                CMP     #TAB
                BEQ     @at
                CMP     #'['
                BEQ     @at
                INX                    ; another delimiter: passed over
@at:            STX     IDX
                JMP     @next

; ---------------------------------------------------------------------------
; A program: {d:}NAME{.COM}{;password}.

PROGRAM:        SEC
                LDA     KWE
                SBC     KWS
                CMP     #KW_MAX + 1
                BCS     @noname
                LDX     KWE            ; the keyword alone, for 152
                LDA     CMD,X
                PHA
                STZ     CMD,X
                LDA     KWS
                CLC
                ADC     #<CMD
                STA     PFCB
                LDA     #>CMD
                ADC     #0
                STA     PFCB + 1
                LDA     #<WFCB
                STA     PFCB + 2
                LDA     #>WFCB
                STA     PFCB + 3
                LDA     #<PFCB
                LDX     #>PFCB
                LDY     #F_PARSE
                JSR     BDOS
                STA     SAVEB
                PLA
                LDY     KWE
                STA     CMD,Y
                LDA     SAVEB          ; all of it taken, or it isn't a name
                BNE     @noname
                TXA
                BNE     @noname
                LDA     WFCB + 1
                CMP     #' '
                BEQ     @noname
                LDX     #11
@wild:          LDA     WFCB,X
                CMP     #'?'
                BEQ     @noname
                DEX
                BNE     @wild
                LDA     WFCB + 9       ; the type: none (the search order's), COM or SUB
                CMP     #' '
                BNE     @typed
                LDA     #<DISPPB       ; the order: the SCB's $18 bits 0-1 (ours, 5c)
                LDX     #>DISPPB
                LDY     #S_SCB
                JSR     BDOS
                AND     #3
                ASL     A
                STA     ORDIX          ; ORDERS + 2 x the order: the first type, the second
                JSR     @settype       ; the first type, on every drive of the chain
                JSR     @load
                INC     ORDIX          ; then the second, if there is one
                LDX     ORDIX
                LDA     ORDERS,X
                BEQ     @noname
                JSR     @settype
                JSR     @load
@noname:        JMP     NONAME
@settype:       LDX     ORDIX          ; WFCB's type: ORDERS[ORDIX] (1 COM, 2 SUB)
                LDA     ORDERS,X
                LDX     #2
                CMP     #2
                BEQ     @tsub
@tcom:          LDA     COMTYPE,X
                STA     WFCB + 9,X
                DEX
                BPL     @tcom
                RTS
@tsub:          LDA     SUBTYPE,X
                STA     WFCB + 9,X
                DEX
                BPL     @tsub
                RTS
@typed:         LDX     #2
@iscom:         LDA     WFCB + 9,X
                CMP     COMTYPE,X
                BNE     @notcom
                DEX
                BPL     @iscom
                BRA     @load1
@notcom:        LDX     #2
@issub:         LDA     WFCB + 9,X
                CMP     SUBTYPE,X
                BNE     @noname
                DEX
                BPL     @issub
@load1:         JSR     @load
                JMP     NONAME
@load:          LDX     #11
@fcb:           LDA     WFCB,X
                STA     COMFCB,X
                DEX
                BPL     @fcb
                LDX     #7
@pw:            LDA     WFCB + 16,X
                STA     PWBUF,X
                DEX
                BPL     @pw
                JMP     LOADPROG       ; back only if not found

; A .SUB file found (COMFCB, on LOADDRV): the command SUBMIT d:NAME.SUB{;PW} and the
; tail, SUBMIT.COM found by the search chain as any program; not found:
; SUBMIT? (5c). A tail that doesn't fit: Command tail too long.
RUNSUB:         LDX     #0             ; the new tail's start: " d:NAME.SUB" in SUBPRE
                LDA     #' '
                STA     SUBPRE,X
                INX
                LDA     LOADDRV
                CLC
                ADC     #'A' - 1
                STA     SUBPRE,X
                INX
                LDA     #':'
                STA     SUBPRE,X
                INX
                LDY     #1
@name:          LDA     COMFCB,Y
                AND     #$7F
                CMP     #' '
                BEQ     @named
                STA     SUBPRE,X
                INX
                INY
                CPY     #9
                BNE     @name
@named:         LDY     #0
@typ:           LDA     DOTSUB,Y
                STA     SUBPRE,X
                INX
                INY
                CPY     #4
                BNE     @typ
                LDY     #0             ; ;PW, the password typed (WFCB's, in PWBUF)
                LDA     PWBUF
                CMP     #' ' + 1
                BCC     @pwend
                LDA     #';'
                STA     SUBPRE,X
                INX
@pwc:           LDA     PWBUF,Y
                CMP     #' ' + 1
                BCC     @pwend
                STA     SUBPRE,X
                INX
                INY
                CPY     #8
                BNE     @pwc
@pwend:         STX     SAVEB          ; its length
                CLC
                LDA     TAILLEN
                ADC     SAVEB
                CMP     #TAIL_MAX + 1
                BCC     @fits
                LDA     #<TOOLONG
                LDX     #>TOOLONG
                JMP     FAILSAY
@fits:          LDY     TAILLEN        ; the old tail moved up, from its end
                STA     TAILLEN
                TAX
@move:          CPY     #0
                BEQ     @put
                DEY
                DEX
                LDA     TAILBUF,Y
                STA     TAILBUF,X
                BRA     @move
@put:           LDX     SAVEB
@pre:           DEX
                BMI     @prog
                LDA     SUBPRE,X
                STA     TAILBUF,X
                BRA     @pre
@prog:          LDX     #11            ; SUBMIT.COM, by the search chain
@sname:         LDA     SUBMITN,X
                STA     COMFCB,X
                DEX
                BPL     @sname
                LDX     #7
                LDA     #' '
@spw:           STA     PWBUF,X
                DEX
                BPL     @spw
                JSR     LOADPROG       ; back only if not found
                JSR     CM_FLUSH
                LDX     #0
@q:             LDA     SUBMITN + 1,X
                CMP     #' '
                BEQ     @qm
                JSR     PUTC
                INX
                BRA     @q
@qm:            LDA     #'?'
                JSR     PUTC
                JMP     BIFAIL

; NAME?: the keyword, in capitals, and a question mark.
NONAME:         LDX     KWS
@char:          CPX     KWE
                BEQ     @q
                LDA     CMD,X
                JSR     PUTC
                INX
                BRA     @char
@q:             LDA     #'?'
                JSR     PUTC
                JMP     BIFAIL

; COMFCB's program (its drive, or the search chain's), PWBUF its password:
; found, loaded and run. Back with nothing done if not found; an error is
; said here.
LOADPROG:       LDA     COMFCB
                STA     GIVEN
                BEQ     @chain
                STA     CHAINB         ; the drive given, alone
                LDA     #$FF
                STA     CHAINB + 1
                BRA     @search
@chain:         LDA     #<CHAIN0PB     ; the SCB's search chain, $4C-$4F
                LDX     #>CHAIN0PB
                LDY     #S_SCB
                JSR     BDOS
                STA     CHAINB
                STX     CHAINB + 1
                LDA     #<CHAIN2PB
                LDX     #>CHAIN2PB
                LDY     #S_SCB
                JSR     BDOS
                STA     CHAINB + 2
                STX     CHAINB + 3
@search:        STZ     CHIDX
@try:           LDX     CHIDX
                CPX     #4
                BEQ     @none
                LDA     CHAINB,X
                CMP     #$FF
                BEQ     @none
                INC     CHIDX
                CMP     #0             ; 0: the default drive
                BNE     @drv
                LDA     CCPDRV
                INC     A
@drv:           CMP     #16 + 1
                BCS     @try           ; not a drive: passed over
                STA     COMFCB
                STA     LOADDRV
                LDX     #12            ; EX on 0
                LDA     #0
@zero:          STA     COMFCB,X
                INX
                CPX     #36
                BNE     @zero
                JSR     DMALAST        ; the password where the BDOS looks
                LDX     #7
@pw:            LDA     PWBUF,X
                STA     LASTREC,X
                DEX
                BPL     @pw
                LDA     #<COMFCB
                LDX     #>COMFCB
                LDY     #F_OPEN
                JSR     BDOS
                CMP     #$FF
                BNE     FOUNDIT
                CPX     #0             ; not there: the next
                BEQ     @try
                CPX     #4             ; no such drive: the next, if the chain named it;
                BNE     @pwerr         ; one given, or the CCP's own, can't be read (ours)
                LDA     GIVEN
                BNE     @rderr
                LDA     COMFCB
                DEC     A
                CMP     CCPDRV
                BEQ     @rderr
                BRA     @try
@pwerr:         CPX     #7
                BNE     @rderr
                LDA     #<PWERR
                LDX     #>PWERR
                JMP     FAILSAY
@rderr:         JMP     RDERR
@none:          RTS

; Found: said where, if the display mode is on; then loaded and run.
FOUNDIT:        PLA                    ; LOADPROG's return dropped: it runs or fails
                PLA
                LDA     #<DISPPB       ; SETDEF's [DISPLAY]: the SCB's $18 bit 7 (ours)
                LDX     #>DISPPB
                LDY     #S_SCB
                JSR     BDOS
                AND     #DISPLAY_FLAG
                BEQ     FOUNDSUB
                LDA     LOADDRV
                CLC
                ADC     #'A' - 1
                JSR     PUTC
                LDA     #':'
                JSR     PUTC
                LDA     #<(COMFCB + 1)
                LDX     #>(COMFCB + 1)
                JSR     CM_DIRNM
                LDA     COMFCB + 8     ; f8': opened from user 0
                BPL     @shown
                LDA     CCPUSR
                BEQ     @shown
                LDA     #<USER0
                LDX     #>USER0
                JSR     STRING
@shown:         JSR     CRLF
FOUNDSUB:       LDX     #2             ; a .SUB found: SUBMIT runs it (the Guide's 1.6.2)
@sub:           LDA     COMFCB + 9,X
                AND     #$7F
                CMP     SUBTYPE,X
                BNE     LOADCOM
                DEX
                BPL     @sub
                JMP     RUNSUB
; The open file loaded by 59, as a program loads an overlay, at $0300; its
; RSXs, if any, below the lowest. Refused: NO PROGRAM (no magic number, or
; nothing in it), BAD PROGRAM HEADER (its flags, its page, too big); a read
; error, or the file ending inside its image: READ ERROR. An RSX-only .COM
; (size 0, RSXs attached) is done: its RSXs stay for the next program.
LOADCOM:        STZ     COMFCB + FCB_R0 ; R0-R2: $0300
                LDA     #>BP_TPA
                STA     COMFCB + FCB_R0 + 1
                STZ     COMFCB + FCB_R0 + 2
                LDA     #<COMFCB
                LDX     #>COMFCB
                LDY     #P_LOAD
                JSR     BDOS
                CMP     #0
                BEQ     @loaded
                CMP     #$FE
                BNE     RDERR
                CPX     #3
                BNE     @room
                LDA     #<NOPROG
                LDX     #>NOPROG
                JMP     FAILSAY
@room:          CPX     #0             ; no room (the table, the TPA): ours
                BNE     @bad
                LDA     #<NOMEM
                LDX     #>NOMEM
                JMP     FAILSAY
@bad:           LDA     #<BADPROG
                LDX     #>BADPROG
                JMP     FAILSAY
@loaded:        LDA     LHDR + OH_SIZE
                ORA     LHDR + OH_SIZE + 1
                BEQ     @rsxs
                JMP     RUNTAIL
@rsxs:          LDA     #1             ; RSXs only: kept for the next program
                STA     KEEP
                JSR     KEEPNEW
                JSR     APPLYSCB
                JMP     BIOK

RDERR:          LDA     #<READERR
                LDX     #>READERR
                JMP     FAILSAY

; GO: the program whose header is at $0280 and image at $0300: the loader's
; checks, the tail and FCBs, a fresh stack, $0300.
RUNHDR:         LDA     CCPDRV
                INC     A
                STA     LOADDRV
                JSR     CHECKHDR
                BRA     RUNTAIL

; The header at $0280 checked as the loader checks it: back if it may run,
; else what's wrong.
CHECKHDR:       LDX     #3
@magic:         LDA     BP_DMA,X
                CMP     MAGIC,X
                BNE     @none
                DEX
                BPL     @magic
                LDA     BP_DMA + OH_FLAGS
                BNE     @bad           ; a .PRL, an RSX or unknown flags
                LDA     BP_DMA + OH_PAGE
                CMP     #>BP_TPA
                BNE     @bad
                CLC                    ; $0300 + size + BSS <= the TPA's top + 1
                LDA     BP_DMA + OH_SIZE
                ADC     BP_DMA + OH_BSS
                STA     CEND
                LDA     BP_DMA + OH_SIZE + 1
                ADC     BP_DMA + OH_BSS + 1
                BCS     @room
                ADC     #>BP_TPA
                BCS     @room
                STA     CEND + 1
                JSR     TOPADDR        ; the lowest RSX's first byte, or $BFFD
                CMP     CEND
                TXA
                SBC     CEND + 1
                BCC     @room
                RTS
@room:          LDA     #<NOMEM        ; too big: as the loader's no room (ours)
                LDX     #>NOMEM
                JMP     FAILSAY
@none:          LDA     #<NOPROG
                LDX     #>NOPROG
                JMP     FAILSAY        ; DONE's fresh stack drops CHECKHDR's return
@bad:           LDA     #<BADPROG
                LDX     #>BADPROG
                JMP     FAILSAY

; Run it: the tail and FCBs over the header, the drive it came from, the DMA
; address $0280 and the default error mode, the return code 0 unless chained
; to, a fresh stack, $0300.
RUNTAIL:        JSR     CM_FLUSH       ; a program starts on a line of its own
                JSR     SETTAIL
                LDA     LOADDRV
                STA     BP_LOADDRV
                LDA     #<BP_DMA
                LDX     #>BP_DMA
                LDY     #F_DMAOFF
                JSR     BDOS
                LDA     #0
                LDY     #F_ERRMODE
                JSR     BDOS
                LDA     CHAINED        ; the return code 0, unless chained to
                BNE     @code
                JSR     CODEOK
@code:          LDA     KEEP           ; an RSX-only .COM's RSXs: this is their program
                CMP     #1
                BNE     @prog
                INC     KEEP
@prog:          LDA     CCPST          ; a program runs (the SCB's $19 bit 7 clear)
                AND     #$7F
                STA     CCPST
                JSR     CCPSET
                JSR     APPLYSCB       ; GENCOM's SCB entries, last
                LDX     #$FF
                TXS
                LDA     #>(BP_WBOOT - 1) ; its RTS: the warm boot
                PHA
                LDA     #<(BP_WBOOT - 1)
                PHA
                LDA     #0
                TAX
                TAY
                JMP     BP_TPA

UPPER:          CMP     #'a'
                BCC     @done
                CMP     #'z' + 1
                BCS     @done
                AND     #$DF
@done:          RTS

PUTC            = CM_PUTC
STRING          = CM_PUTS
CRLF            = CM_CRLF

.include "cmd.inc"

BASEJMPS:       JMP     WARM           ; $0200
                JMP     BDOS_ENTRY     ; $0203: so $0204 is $BFFD, the TPA's top + 1
                JMP     STUB           ; $BFFD
RESCANPB:       .byte   BIOS::SELDSK, 0, 0, 0, 0, 0, 0, 0 ; A: 0, X: 0 a first select
MAGIC:          .byte   OODOS_MAGIC
COMTYPE:        .byte   "COM"
SUBTYPE:        .byte   "SUB"
BINAMES:        .byte   3, "DIR"       ; the built-ins (the Guide's Table 4-1, its short forms)
                .word   B_DIR
                .byte   6, "DIRSYS"
                .word   B_DIRS
                .byte   4, "DIRS"
                .word   B_DIRS
                .byte   5, "ERASE"
                .word   B_ERA
                .byte   3, "ERA"
                .word   B_ERA
                .byte   6, "RENAME"
                .word   B_REN
                .byte   3, "REN"
                .word   B_REN
                .byte   4, "TYPE"
                .word   B_TYPE
                .byte   3, "TYP"
                .word   B_TYPE
                .byte   4, "USER"
                .word   B_USER
                .byte   3, "USE"
                .word   B_USER
                .byte   2, "GO"        ; ours, phase 2's
                .word   RUNHDR
                .byte   5, "CLEAR"     ; ours, 2026-11-03
                .word   B_CLEAR
                .byte   3, "CLS"
                .word   B_CLS
                .byte   6, "WOZMON"
                .word   B_WOZMON
                .byte   6, "APPLE2"
                .word   B_APPLE2
                .byte   0
UNAMES:                                ; their utilities
UDIR:           .byte   "DIR     COM"
UERA:           .byte   "ERASE   COM"
UREN:           .byte   "RENAME  COM"
UTYPE:          .byte   "TYPE    COM"
SYSINS:         .byte   "SYS,"
SYSADD:         .byte   " [SYS]"
SEP:            .byte   " : $"
CLEARS:         .byte   27, 'E', '$'   ; ESC E: the screen's clear
NOIIE:          .byte   "No IIe ROM in the cartridge (the emulator: Settings, ROMs)$"
NOPROG:         .byte   "NO PROGRAM$"
BADPROG:        .byte   "BAD PROGRAM HEADER$"
NOMEM:          .byte   "Insufficient Memory$"
READERR:        .byte   "READ ERROR$"
PWERR:          .byte   "Password Error$"
TOOLONG:        .byte   "Command tail too long$"
SUBMITN:        .byte   0, "SUBMIT  COM"   ; (with a drive byte: COMFCB's 0-11)
DOTSUB:         .byte   ".SUB"
PROFN:          .byte   "PROFILE SUB"
PROFCMD:        .byte   "SUBMIT PROFILE", 0
ORDERS:         .byte   1, 0, 1, 2, 2, 1, 2, 0 ; the search order: types to try (1 COM, 2 SUB)
COLDF:          .byte   1              ; in the file: 1 at the cold boot, 0 after the first prompt
REQUIRED:       .byte   " required$"
CM_NOFILE:      .byte   "No File$"
MEDIACHG:       .byte   "Media Change$"
SYSEXIST:       .byte   "SYSTEM FILE(S) EXIST$"
NONSYSEXIST:    .byte   "NON-SYSTEM FILE(S) EXIST$"
ERAQ:           .byte   "ERASE $"
YNQ:            .byte   " (Y/N)?$"
NOTREN:         .byte   "Not renamed: $"
EXISTSQ:        .byte   " file already exists, delete (Y/N)?$"
ASKUSER:        .byte   "Enter User #: $"
USER0:          .byte   " (User 0)$"
CHAINPB:        .byte   <(SCB_CCPFLAGS - SCB), 0, 0, 0  ;   get 47's request
NOCHAINPB:      .byte   <(SCB_CCPFLAGS - SCB), $FF, 0, 0 ;  and none
DISKPB:         .byte   <(SCB_DISK - SCB), $FF, 0, 0    ;   set the current drive: CCPDRV
CHAIN0PB:       .byte   <(SCB_DRVCHAIN - SCB), 0, 0, 0  ;   the search chain's first two
CHAIN2PB:       .byte   <(SCB_DRVCHAIN + 2 - SCB), 0, 0, 0 ;  and last two
SCBWIDTH:       .byte   <(SCB_WIDTH - SCB), 0, 0, 0     ;   the console width - 1
DISPPB:         .byte   <(SCB_CCPDISP - SCB), 0, 0, 0   ;   SETDEF's display mode
PWBUF:          .res    8, ' '         ; the program's password, from its keyword
ATPROMPT:       .byte   0              ; 1 while function 10 reads the prompt's line: in
                                       ; the file, so 0 from the cold boot
PENDING:        .byte   0              ; 1: the line has more commands, from LPOS
CCPDRV:         .byte   0              ; the CCP's drive and user, A: and 0 from the
CCPUSR:         .byte   0              ; cold boot

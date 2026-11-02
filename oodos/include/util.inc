; What only phase 4b's utilities share (hw/oodos.md, "The utilities, phase
; 4b"): the tail and its option list, file specifications by 152, a line
; read, numbers of 24 bits, dates, the directory, drives and the end. The
; CCP doesn't include it. Include it after cmd.inc, whose CMD_DATA, CMD_BSS
; and CMD_BREAK it uses too (CMD_BREAK may be UT_BREAK). As cmd.inc, it
; uses no zero page: its pointers are written into its own instructions.
; Every routine may change A, X and Y unless it says otherwise.
;
; The start and the end
;
;   UT_INIT     call first: nothing owed (CM_LFDUE, CM_EOL), no paging
;               until CM_PAGE (a program's BSS isn't cleared), the tail's
;               indexes 0, the current drive kept (25), the error mode $FE
;               (45: the BDOS says its errors and returns)
;   UT_FAIL     the return code $FF00 (108): an error said
;   UT_END      the end: CM_UNPAGE, the kept drive selected again, a line
;               owed dropped (the prompt's CR LF ends the last line); RTS
;   UT_EXIT     UT_END, then the warm boot
;   UT_BREAK    a CMD_BREAK for utilities: the return code $FFFE (a ^C),
;               UT_END, the warm boot
;
; The tail ($0280: its length, the characters at UT_TAIL, a 0 after them)
;
;   UT_IDX      the index of the next character, from 0
;   UT_PEEK     A the character there, Z set at the end (the 0); keeps Y
;   UT_GET      the same, and past it (not past the end); keeps Y
;   UT_SKIP     past blanks and tabs, then as UT_PEEK; keeps Y
;   UT_READ     the '$' string at A/X written (what is owed first), then a
;               line of up to 64 characters read (function 10) and made the
;               tail, in capitals and to a 0 in it, as the CCP takes its
;               line; UT_IDX and the option list's place 0; CM_LFDUE set,
;               as CM_READ. Out: A its length, Z set if empty
;
; The option list (the Guide, 5.2: [name, name=value, name=(v,v)])
;
;   UT_OPTS     the tail's '[': A (and Z) UT_LNONE none; UT_LONE one, its
;               options next; UT_LTWO a second after it; UT_LOPEN no ']'
;   UT_OPT      the next option, matched against the table at A/X (names,
;               each ended by a 0, the table by another 0): any prefix
;               that is one name's alone, or a name in full (an exact name
;               wins over a longer one it starts), NO, blanks and a name
;               taken as one word ([NO PAGE] is NOPAGE); separated by
;               commas or blanks. Out: carry clear and A its index, from
;               0; or carry set and A UT_OEND (the ']'), UT_OUNK unknown,
;               UT_OAMB ambiguous. The name's word is in UT_WORD. A value
;               of the last option not taken is passed over first
;   UT_HASV     after UT_OPT: 1 an '=' followed the name, 0 none
;   UT_VALUE    the option's value started: carry set if it has none;
;               else carry clear and A 1 a list, (a,b,c), 0 a word
;   UT_VITEM    the value's next item into UT_WORD: carry clear, the item
;               (to ',', ']', ')', a blank; it may be empty, [PASSWORD=]);
;               carry set and A 0 the end, $FF a list not closed or an item
;               over 16 characters. A word value is one item
;   UT_WORD     the word: up to 16 characters and a 0; UT_WLEN its length
;   UT_WDEC     UT_WORD as a decimal number 0-65535: A/X; carry set if it
;               isn't digits alone, UT_OVR 1 if it is but over 65535
;   UT_WONOFF   UT_WORD ON (A 1) or OFF (A 0); carry set if neither
;   UT_WDRIVE   UT_WORD a drive, C or C: (A 1-16, A-P) or * (A 0, the
;               default); carry set if not
;
; File specifications
;
;   UT_SPEC     the tail's next specification from UT_IDX, option lists
;               passed over, into the 36-byte FCB at A/X by 152: the drive,
;               the name and type, the password at 16-23, 12-15 and 24-35 0.
;               Out: carry clear, UT_IDX after it and the delimiter after
;               it ('=', ','; not a blank or '['), which is in UT_SDELIM (0:
;               none); carry set and A 0 none left, $FF one 152 refuses,
;               or one whose delimiter has nothing after it but blanks and
;               option lists (A, or A= [X]: a specification missing; UT_IDX
;               at it either way). UT_SPAT is where it starts, UT_SEND where
;               152 stopped. From a line read: UT_READ, then UT_SPEC
;   UT_PUTSPEC  the last specification as typed, its password not shown
;
; Numbers (24 bits, UT_N: low, middle, high)
;
;   UT_NOUT     UT_N in decimal, right in a field of A characters (0: no
;               field; a wider number isn't cut), carry set: with commas
;               (9,488). UT_N kept
;   UT_DECIN    a decimal number from the string at A/X, index Y: A/X the
;               number, Y past its digits; carry set if there were none or
;               it is over 65535 (UT_OVR 1)
;   UT_LDN      UT_N = the 3 bytes at A/X
;   UT_STN      the 3 bytes at A/X = UT_N
;   UT_ADDN     UT_N += the 3 bytes at A/X (carry set: past 24 bits)
;   UT_SHLN     UT_N shifted left A bits
;   UT_DIV8     UT_N / 8, rounded up
;   UT_CMPN     UT_N against the 3 bytes at A/X: carry set if UT_N is the
;               greater or equal, Z set if equal
;   UT_DIVN     UT_N / A (1-127): the quotient in UT_N, the remainder in A
;
; Dates and times (CP/M 3's days: 1 January 1978 day 1, a Sunday; a leap
; year every fourth, as the DS3231 and the BIOS's TIME)
;
;   UT_TODATE   A/X days (1 on) into UT_YEAR (a word: 2026), UT_MONTH,
;               UT_DAY
;   UT_TODAYS   UT_YEAR (1978 on), UT_MONTH, UT_DAY into days, A/X
;   UT_MLEN     A the days in month A (1-12) of UT_YEAR
;   UT_PUTDATE  A/X days as MM/DD/YY
;   UT_PUTTIME  A the hour and X the minute (BCD) as HH:MM
;   UT_PUTTIMES the same and Y the second (BCD), HH:MM:SS
;   UT_PUTSTAMP the stamp at A/X (days, the hour and the minute in BCD:
;               4 bytes) as MM/DD/YY HH:MM, or 14 blanks if its days are 0
;   UT_PUTWDAY  A/X days as its weekday, Sun to Sat
;   UT_PUT2     A (0-99) as two digits
;
; The directory (the current drive, by 17 and 18 with '?' in the drive
; byte: every entry, free ones too, in order)
;
;   UT_SCAN     the DMA address UT_REC (26), the first entry; UT_SNEXT the
;               next. Out: carry clear, A/X the entry's address in UT_REC
;               (UT_EADR too), UT_REC its record (an SFCB is its +96),
;               UT_ENT its number from 0 (a word); carry set at the end, X
;               the BDOS's code (0: none left)
;   UT_LABEL    the label (the first entry whose byte 0 is $20) copied to
;               UT_LBL, 32 bytes: carry clear; carry set if none (X as
;               UT_SCAN's)
;   UT_SFCBS    A (and Z) 1 if the directory has SFCBs (its entry 3 is
;               $21), 0 if not; carry set if the scan failed
;   UT_PWDEC    the password of the entry at A/X (an XFCB, a label: the
;               key at +13, the 8 bytes at +16-23 reversed and XORed with
;               it, 3b1) into UT_PW, 8 bytes
;   UT_SETPAT   the FCB at A/X the pattern for UT_MATCH: its bytes 1-11
;   UT_MATCH    the 11 bytes at A/X against the pattern, '?' any, bit 7 of
;               both dropped (as 17 compares): carry set if they match
;
; Drives (0-15, A-P)
;
;   UT_DRV0     the drive current at UT_INIT
;   UT_SELECT   drive A selected (14): A 0, or $FF (the BDOS said why)
;   UT_BACK     UT_DRV0 selected again
;   UT_DPB      the current drive's DPB (31: its copy in common memory)
;               copied to UT_DPBC, 17 bytes
;
; Writing
;
;   UT_PUTDRV   drive A as "A:"
;   UT_SAY      the '$' string at A/X, then a line owed (CM_EOL)
;   UT_BLANKS   A blanks (0: none)
;
; Memory (SET and INITDIR): what a utility may use past its BSS
;
;   UT_MEM      UT_MLO the first byte past the program's BSS, UT_MHI the
;               top of the TPA + 1 ($0204's word: the BDOS's entry, or an
;               RSX's below it); A/X UT_MHI - UT_MLO, the bytes between
;
; Files (TYPE, ERASE, RENAME): a specification's files, read first, then
; worked on one by one (a file call between 17 and 18 would end the search)
;
;   UT_FDRIVE   A an FCB's drive byte (0 the current drive, UT_DRV0; 1-16
;               A-P) as a drive 0-15, for UT_PUTDRV
;   UT_ISWILD   carry set if any of the 11 bytes at A/X is '?'
;   UT_PWSET    the 8 bytes at A/X (a password, as UT_SPEC leaves it at an
;               FCB's 16-23) copied to UT_REC, and the DMA address UT_REC
;               (26): where the BDOS looks for it at 15, 19 and 23
;   UT_NAMES    the names of the current user's files that match the FCB at
;               A/X (its drive byte and bytes 1-11, '?' any), each once (17
;               and 18 with the extent 0: the extent-zero entry, as the
;               built-ins' DIR), in the directory's order, bit 7 of each
;               byte dropped, read into memory from past the BSS to the
;               TPA's top ($0204's word); the DMA address UT_REC. Out:
;               carry clear and A/X (and UT_NCNT) the count, a word, the
;               first for UT_NNEXT; carry set and A $FF an error the BDOS
;               said, or 1: more than fit (nothing worked on)
;   UT_NNEXT    the next name: carry clear and A/X its 11 bytes' address;
;               carry set when none is left
;   UT_SAID     after an error the BDOS said (its message starts with CR LF,
;               which ends a line owed, and ends with CR LF): nothing owed,
;               neither an LF nor a line (CM_NOEOL); then UT_FAIL
;   UT_ISRO     carry set if drive A (0-15) is read-only (29: made so by 28,
;               or found so by a write), for the Guide's Drive Read Only
;
; The system (SHOW, DATE, DEVICE, SETDEF)
;
;   UT_OAT      an option list where the tail is: UT_IDX at its '[', it is
;               the one UT_OPT reads next (UT_OPTS finds only the tail's
;               first). DEVICE has one after each device
;   UT_OCLOSE   after UT_OPT's UT_OEND: carry clear and UT_IDX past the ']';
;               carry set if the list wasn't closed
;   UT_SCBGET   A an SCB offset: A/X the word there (49)
;   UT_SCBSET   A an SCB offset, X the byte: set there (49)
;   UT_SCBSETW  A an SCB offset: the word at UT_SCBW set there (49)
;   UT_SCBW     the word UT_SCBSETW sets
;   UT_BIOS     a BIOS call by 50: A the function (system.inc's BIOS::),
;               X its A, Y its X, UT_BPB + 4-5 and + 6-7 its two words
;               (set them first); A/X the BIOS's A/X
;   UT_BPB      function 50's parameter block, 8 bytes

.ifndef UTIL_INC
UTIL_INC = 1

UT_TAIL         = BP_DMA + 1           ; the tail's characters
UTC_TAB         = $09

UT_LNONE        = 0                    ; UT_OPTS
UT_LONE         = 1
UT_LTWO         = 2
UT_LOPEN        = 3
UT_OEND         = $FF                  ; UT_OPT, with carry set
UT_OUNK         = $FE
UT_OAMB         = $FD

UT_WMAX         = 16                   ; UT_WORD's characters

.pushseg
.segment CMD_BSS
UT_IDX:         .res    1              ; the tail's next character
UT_OIDX:        .res    1              ; the option list's
UT_VST:         .res    1              ; the value: 0 none, 1 not started, 2 a word, 3 a list
UT_HASV:        .res    1
UT_WORD:        .res    UT_WMAX + 1
UT_WLEN:        .res    1              ; $FF: over UT_WMAX
UT_OVR:         .res    1
UT_HIT:         .res    1              ; UT_OPT's match and count
UT_NHIT:        .res    1
UT_NAME:        .res    1              ; the name's index
UT_PFCB:        .res    4              ; 152's: the string, the FCB
UT_SPAT:        .res    1
UT_SEND:        .res    1
UT_SDELIM:      .res    1
UT_LBUF:        .res    2 + 64         ; UT_READ's line: its size, count, characters
UT_N:           .res    3
UT_T:           .res    3
UT_DIGS:        .res    8              ; UT_NOUT's digits, the lowest first
UT_ND:          .res    1
UT_FW:          .res    1
UT_FC:          .res    1
UT_STMP:        .res    4
UT_DV:          .res    1
UT_YEAR:        .res    2
UT_MONTH:       .res    1
UT_DAY:         .res    1
UT_D:           .res    2
UT_REC:         .res    128            ; the directory's record (UT_SCAN's DMA)
UT_QFCB:        .res    36             ; 17's: '?' in the drive byte
UT_ENT:         .res    2
UT_EADR:        .res    2
UT_LBL:         .res    32
UT_PW:          .res    8
UT_DRV0:        .res    1
UT_DPBC:        .res    17
.segment CMD_DATA
UT_RCFAIL:      .word   $FF00
UT_MDAYS:       .byte   31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
UT_WDAYS:       .byte   "SunMonTueWedThuFriSat"
UT_SON:         .byte   "ON", 0
UT_SOFF:        .byte   "OFF", 0
.popseg

; ---- the start and the end -----------------------------------------------

UT_INIT:        STZ     CM_LFDUE
                STZ     CM_EOLDUE
                STZ     CM_PLEN
                STZ     CM_LINES
                STZ     UT_IDX
                STZ     UT_OIDX
                STZ     UT_VST
                LDY     #DRV_GET
                JSR     BDOS
                STA     UT_DRV0
                LDA     #$FE
                LDY     #F_ERRMODE
                JMP     BDOS

UT_FAIL:        LDA     #$00
                LDX     #$FF
                LDY     #P_CODE
                JMP     BDOS

UT_END:         JSR     CM_UNPAGE
                JSR     UT_BACK
                JMP     CM_NOEOL

UT_EXIT:        JSR     UT_END
                JMP     WBOOT

UT_BREAK:       LDA     #$FE
                LDX     #$FF
                LDY     #P_CODE
                JSR     BDOS
                BRA     UT_EXIT

; ---- the tail ------------------------------------------------------------

UT_PEEK:        LDX     UT_IDX
                LDA     UT_TAIL,X
                RTS

UT_GET:         LDX     UT_IDX
                LDA     UT_TAIL,X
                BEQ     @end
                INC     UT_IDX
                CMP     #0
@end:           RTS

UT_SKIP:        LDX     UT_IDX
@char:          LDA     UT_TAIL,X
                CMP     #' '
                BEQ     @past
                CMP     #UTC_TAB
                BNE     @done
@past:          INX
                BRA     @char
@done:          STX     UT_IDX
                CMP     #0
                RTS

UT_READ:        JSR     CM_PUTS
                LDA     #64
                STA     UT_LBUF
                STZ     UT_LBUF + 1
                LDA     #<UT_LBUF
                LDX     #>UT_LBUF
                LDY     #C_READSTR
                JSR     BDOS
                LDX     #0
@char:          CPX     UT_LBUF + 1
                BEQ     @end
                LDA     UT_LBUF + 2,X
                BEQ     @end           ; a 0 ends it, as the CCP's line
                CMP     #'a'
                BCC     @put
                CMP     #'z' + 1
                BCS     @put
                AND     #$DF
@put:           STA     UT_TAIL,X
                INX
                BRA     @char
@end:           STZ     UT_TAIL,X
                STX     BP_DMA
                STZ     UT_IDX
                STZ     UT_OIDX
                STZ     UT_VST
                LDA     #1             ; function 10 echoed only the CR
                STA     CM_LFDUE
                TXA
                RTS

; ---- the option list -----------------------------------------------------

UT_OPTS:        STZ     UT_VST
                LDX     #0
@open:          LDA     UT_TAIL,X
                BEQ     @none
                INX
                CMP     #'['
                BNE     @open
                STX     UT_OIDX        ; after the '['
@close:         LDA     UT_TAIL,X
                BEQ     @unclosed
                INX
                CMP     #']'
                BNE     @close
@again:         LDA     UT_TAIL,X
                BEQ     @one
                INX
                CMP     #'['
                BNE     @again
                LDA     #UT_LTWO
                RTS
@one:           LDA     #UT_LONE
                RTS
@unclosed:      LDA     #UT_LOPEN
                RTS
@none:          LDA     #UT_LNONE
                RTS

; Is A a character of a word? Carry set if so; keeps A, X and Y.
UT_ISW:         CMP     #' ' + 1       ; blanks, tabs, controls, the 0
                BCC     @no
                CMP     #','
                BEQ     @no
                CMP     #']'
                BEQ     @no
                CMP     #'='
                BEQ     @no
                CMP     #'('
                BEQ     @no
                CMP     #')'
                BEQ     @no
                SEC
                RTS
@no:            CLC
                RTS

; The option list's blanks and tabs passed over: A the character there.
UT_OSKIP:       LDX     UT_OIDX
@char:          LDA     UT_TAIL,X
                CMP     #' '
                BEQ     @past
                CMP     #UTC_TAB
                BNE     @done
@past:          INX
                BRA     @char
@done:          STX     UT_OIDX
                RTS

; A word at UT_OIDX into UT_WORD from index Y (UT_WLEN its length, $FF
; over UT_WMAX), a 0 after it; UT_OIDX past it.
UT_RWORD:       LDX     UT_OIDX
@char:          LDA     UT_TAIL,X
                JSR     UT_ISW
                BCC     @end
                INX
                CPY     #UT_WMAX
                BCS     @long
                STA     UT_WORD,Y
                INY
                BRA     @char
@long:          LDY     #$FF           ; too long: the rest passed over
                BRA     @char
@end:           STX     UT_OIDX
                STY     UT_WLEN
                CPY     #$FF
                BEQ     @done
                LDA     #0
                STA     UT_WORD,Y
@done:          RTS

UT_OPT:         STA     @tab + 1
                STX     @tab + 2
                STA     @exact + 1
                STX     @exact + 2
                STA     @skipn + 1
                STX     @skipn + 2
@pending:       LDA     UT_VST         ; the last option's value, passed over
                BEQ     @sep
                JSR     UT_VITEM
                BRA     @pending
@sep:           JSR     UT_OSKIP
                CMP     #','
                BNE     @word
                INC     UT_OIDX
                BRA     @sep
@word:          CMP     #']'
                BEQ     @end
                CMP     #0
                BNE     @read
@end:           LDA     #UT_OEND
                SEC
                RTS
@read:          LDY     #0
                JSR     UT_RWORD
                LDA     UT_WLEN        ; NO, blanks and a name: one word
                CMP     #2
                BNE     @eq
                LDA     UT_WORD
                CMP     #'N'
                BNE     @eq
                LDA     UT_WORD + 1
                CMP     #'O'
                BNE     @eq
                LDX     UT_OIDX
                LDA     UT_TAIL,X
                CMP     #' '
                BEQ     @no
                CMP     #UTC_TAB
                BNE     @eq
@no:            JSR     UT_OSKIP
                JSR     UT_ISW
                BCC     @eq
                LDY     #2
                JSR     UT_RWORD
@eq:            STZ     UT_HASV
                JSR     UT_OSKIP
                CMP     #'='
                BNE     @bare
                INC     UT_OIDX
                LDA     #1
                STA     UT_HASV
                STA     UT_VST
                BRA     @match
@bare:          LDY     UT_WLEN        ; nothing read: a '(' is a value to
                BNE     @match         ; pass over, a ')' a character
                CMP     #'('
                BNE     @paren
                LDA     #1
                STA     UT_VST
                BRA     @unk
@paren:         INC     UT_OIDX        ; (')', '=' or a control)
                BRA     @unk
@match:         LDA     UT_WLEN
                BEQ     @unk
                CMP     #$FF
                BEQ     @unk
                STZ     UT_NHIT
                STZ     UT_NAME
@name:          LDY     #0             ; the table's next name
@tab:           LDA     $FFFF,Y
                BEQ     @tabend
@cmp:           CPY     UT_WLEN
                BEQ     @prefix        ; the word is its start
                LDA     UT_WORD,Y
@exact:         CMP     $FFFF,Y
                BNE     @skip
                INY
                BRA     @cmp
@prefix:        JSR     @load          ; the name's end: exact
                BEQ     @found
                LDA     UT_NAME
                STA     UT_HIT
                INC     UT_NHIT
@skip:          JSR     @load          ; to past the name's 0
                BEQ     @next
                INY
                BRA     @skip
@next:          INY                    ; the pointers past it
                TYA
                CLC
                ADC     @tab + 1
                STA     @tab + 1
                STA     @exact + 1
                STA     @skipn + 1
                LDA     @tab + 2
                ADC     #0
                STA     @tab + 2
                STA     @exact + 2
                STA     @skipn + 2
                INC     UT_NAME
                BRA     @name
@tabend:        LDA     UT_NHIT
                BEQ     @unk
                CMP     #1
                BNE     @amb
                LDA     UT_HIT
                CLC
                RTS
@found:         LDA     UT_NAME
                CLC
                RTS
@amb:           LDA     #UT_OAMB
                SEC
                RTS
@unk:           LDA     #UT_OUNK
                SEC
                RTS
@load:
@skipn:         LDA     $FFFF,Y
                RTS

UT_VALUE:       LDA     UT_VST
                BEQ     @none
                CMP     #1
                BNE     @kind
                JSR     UT_OSKIP
                CMP     #'('
                BNE     @word
                INC     UT_OIDX
                LDA     #3
                STA     UT_VST
                BRA     @kind
@word:          LDA     #2
                STA     UT_VST
@kind:          LDA     UT_VST         ; 3 a list: 1; 2 a word: 0
                LSR     A
                LDA     #0
                ROL     A
                CLC
                RTS
@none:          SEC
                RTS

UT_VITEM:       LDA     UT_VST
                BEQ     @end
                CMP     #1
                BNE     @go
                JSR     UT_VALUE
                LDA     UT_VST
@go:            CMP     #2
                BNE     @list
                STZ     UT_VST         ; a word: its one item
                LDY     #0
                JSR     UT_RWORD
                BRA     @got
@list:          JSR     UT_OSKIP
                CMP     #')'
                BNE     @open
                INC     UT_OIDX
                STZ     UT_VST
@end:           LDA     #0
                SEC
                RTS
@open:          CMP     #']'
                BEQ     @unclosed
                CMP     #0
                BEQ     @unclosed
                LDY     #0
                JSR     UT_RWORD
                JSR     UT_OSKIP
                CMP     #','
                BEQ     @comma
                CMP     #')'
                BEQ     @got
                LDY     UT_WLEN        ; neither, and nothing read: the
                BNE     @got           ; character passed over, an error
                INC     UT_OIDX
                BRA     @bad
@comma:         INC     UT_OIDX
@got:           LDA     UT_WLEN
                CMP     #$FF
                BEQ     @bad
                CLC
                RTS
@unclosed:      STZ     UT_VST
@bad:           LDA     #$FF
                SEC
                RTS

UT_WDEC:        LDA     UT_WLEN
                BEQ     @bad
                CMP     #$FF
                BEQ     @bad
                LDA     #<UT_WORD
                LDX     #>UT_WORD
                LDY     #0
                JSR     UT_DECIN
                BCS     @out
                CPY     UT_WLEN
                BNE     @bad
                CLC
@out:           RTS
@bad:           STZ     UT_OVR
                SEC
                RTS

UT_WONOFF:      LDX     #2
@word:          LDY     #$FF
@char:          INY
                LDA     UT_WORD,Y
                CPX     #2
                BNE     @off
                CMP     UT_SON,Y
                BRA     @cmp
@off:           CMP     UT_SOFF,Y
@cmp:           BNE     @next
                CMP     #0
                BNE     @char
                TXA                    ; X 2: ON, 1: OFF
                DEC     A
                CLC
                RTS
@next:          DEX
                BNE     @word
                SEC
                RTS

UT_WDRIVE:      LDA     UT_WLEN
                CMP     #1
                BEQ     @one
                CMP     #2
                BNE     @bad
                LDA     UT_WORD + 1
                CMP     #':'
                BNE     @bad
                BRA     @letter
@one:           LDA     UT_WORD
                CMP     #'*'
                BNE     @letter
                LDA     #0
                CLC
                RTS
@letter:        LDA     UT_WORD
                SEC
                SBC     #'A'
                CMP     #16
                BCS     @bad
                INC     A
                RTS                    ; carry clear
@bad:           SEC
                RTS

; ---- file specifications -------------------------------------------------

UT_SPEC:        STA     UT_PFCB + 2
                STX     UT_PFCB + 3
                STA     @fcb + 1
                STX     @fcb + 2
@next:          JSR     UT_SKIP        ; past blanks and option lists
                BEQ     @none
                CMP     #'['
                BNE     @spec
@list:          INX
                LDA     UT_TAIL,X
                BEQ     @at
                CMP     #']'
                BNE     @list
                INX
@at:            STX     UT_IDX
                BRA     @next
@none:          LDA     #0
                SEC
                RTS
@spec:          STX     UT_SPAT
                LDY     #35            ; the FCB: blank names, the rest 0
@clear:         LDA     #0
                CPY     #12
                BCS     @pw
                CPY     #1
                BCS     @blank
                BRA     @fcb
@pw:            CPY     #16
                BCC     @fcb
                CPY     #24
                BCS     @fcb
@blank:         LDA     #' '
@fcb:           STA     $FFFF,Y
                DEY
                BPL     @clear
                LDA     UT_IDX
                CLC
                ADC     #<UT_TAIL
                STA     UT_PFCB
                LDA     #>UT_TAIL
                ADC     #0
                STA     UT_PFCB + 1
                LDA     #<UT_PFCB
                LDX     #>UT_PFCB
                LDY     #F_PARSE
                JSR     BDOS
                CPX     #$FF
                BNE     @parsed
                CMP     #$FF
                BNE     @parsed
                SEC                    ; refused: A $FF
                RTS
@parsed:        STZ     UT_SDELIM
                CPX     #0
                BNE     @stop
                CMP     #0
                BNE     @stop
                LDX     UT_IDX         ; 0: the end
@toend:         LDA     UT_TAIL,X
                BEQ     @end
                INX
                BRA     @toend
@end:           STX     UT_SEND
                BRA     @done
@stop:          SEC                    ; where 152 stopped
                SBC     #<UT_TAIL
                TAX
                STX     UT_SEND
                LDA     UT_TAIL,X
                CMP     #' '
                BEQ     @done
                CMP     #UTC_TAB
                BEQ     @done
                CMP     #'['
                BEQ     @done
                STA     UT_SDELIM      ; another delimiter: passed over
                INX
                TXA                    ; nothing after it but blanks and
                TAY                    ; option lists: refused
@look:          LDA     UT_TAIL,Y
                BEQ     @trail
                CMP     #' '
                BEQ     @skip
                CMP     #UTC_TAB
                BEQ     @skip
                CMP     #'['
                BNE     @done
@inlist:        INY
                LDA     UT_TAIL,Y
                BEQ     @trail
                CMP     #']'
                BNE     @inlist
@skip:          INY
                BRA     @look
@trail:         LDA     #$FF
                SEC
                RTS
@done:          STX     UT_IDX
                LDA     #0
                CLC
                RTS

UT_PUTSPEC:     LDX     UT_SPAT
@char:          CPX     UT_SEND
                BEQ     @done
                LDA     UT_TAIL,X
                BEQ     @done
                CMP     #' '
                BEQ     @done
                CMP     #UTC_TAB
                BEQ     @done
                CMP     #';'
                BEQ     @done
                JSR     CM_PUTC
                INX
                BRA     @char
@done:          RTS

; ---- numbers -------------------------------------------------------------

UT_DIVN:        STA     UT_DV
                LDA     #0             ; the remainder
                LDX     #24
@bit:           ASL     UT_N
                ROL     UT_N + 1
                ROL     UT_N + 2
                ROL     A
                CMP     UT_DV
                BCC     @less
                SBC     UT_DV
                INC     UT_N           ; bit 0 was 0
@less:          DEX
                BNE     @bit
                RTS

UT_NOUT:        STA     UT_FW          ; the field
                LDA     #0
                ROL     A
                STA     UT_FC          ; 1: commas
                LDX     #2
@keep:          LDA     UT_N,X
                STA     UT_T,X
                DEX
                BPL     @keep
                STZ     UT_ND
@digit:         LDA     #10
                JSR     UT_DIVN
                LDX     UT_ND
                STA     UT_DIGS,X
                INC     UT_ND
                LDA     UT_N
                ORA     UT_N + 1
                ORA     UT_N + 2
                BNE     @digit
                LDX     #2
@back:          LDA     UT_T,X
                STA     UT_N,X
                DEX
                BPL     @back
                LDA     UT_ND          ; its width: the digits, and a comma
                LDY     UT_FC          ; each 3 but the first
                BEQ     @width
                LDX     UT_ND
                DEX
@comma:         CPX     #3
                BCC     @width
                INC     A
                DEX
                DEX
                DEX
                BRA     @comma
@width:         STA     UT_T
                LDA     UT_FW
                SEC
                SBC     UT_T
                BCC     @out           ; wider than the field: no blanks
                JSR     UT_BLANKS
@out:           LDX     UT_ND
@put:           DEX
                LDA     UT_DIGS,X
                ORA     #'0'
                JSR     CM_PUTC        ; keeps X
                TXA
                BEQ     @done
                LDY     UT_FC
                BEQ     @put
@mod:           CMP     #3             ; X a multiple of 3: a comma
                BCC     @rem
                SBC     #3
                BRA     @mod
@rem:           CMP     #0
                BNE     @put
                LDA     #','
                JSR     CM_PUTC
                BRA     @put
@done:          RTS

UT_DECIN:       STA     @src + 1
                STX     @src + 2
                STZ     UT_N
                STZ     UT_N + 1
                STZ     UT_N + 2
                STZ     UT_OVR
                STZ     UT_ND
@src:           LDA     $FFFF,Y
                SEC
                SBC     #'0'
                CMP     #10
                BCS     @end
                INY
                INC     UT_ND
                LDX     UT_OVR
                BNE     @src           ; over already: the digits passed over
                PHA                    ; n x 10 + the digit
                LDX     #2
@keep:          LDA     UT_N,X
                STA     UT_T,X
                DEX
                BPL     @keep
                ASL     UT_N           ; x 4
                ROL     UT_N + 1
                ROL     UT_N + 2
                ASL     UT_N
                ROL     UT_N + 1
                ROL     UT_N + 2
                CLC                    ; x 5
                LDA     UT_N
                ADC     UT_T
                STA     UT_N
                LDA     UT_N + 1
                ADC     UT_T + 1
                STA     UT_N + 1
                LDA     UT_N + 2
                ADC     UT_T + 2
                STA     UT_N + 2
                ASL     UT_N           ; x 10
                ROL     UT_N + 1
                ROL     UT_N + 2
                PLA
                CLC
                ADC     UT_N
                STA     UT_N
                BCC     @high
                INC     UT_N + 1
                BNE     @high
                INC     UT_N + 2
@high:          LDA     UT_N + 2       ; over 65535
                BEQ     @src
                LDA     #1
                STA     UT_OVR
                BRA     @src
@end:           LDA     UT_ND
                BEQ     @bad
                LDA     UT_OVR
                BNE     @bad
                LDA     UT_N
                LDX     UT_N + 1
                CLC
                RTS
@bad:           SEC
                RTS

UT_LDN:         STA     @src + 1
                STX     @src + 2
                LDY     #2
@src:           LDA     $FFFF,Y
                STA     UT_N,Y
                DEY
                BPL     @src
                RTS

UT_STN:         STA     @dst + 1
                STX     @dst + 2
                LDY     #2
@byte:          LDA     UT_N,Y
@dst:           STA     $FFFF,Y
                DEY
                BPL     @byte
                RTS

UT_ADDN:        STA     @src + 1
                STX     @src + 2
                LDY     #0
                LDX     #3
                CLC
@byte:          LDA     UT_N,Y
@src:           ADC     $FFFF,Y
                STA     UT_N,Y
                INY
                DEX
                BNE     @byte
                RTS

UT_SHLN:        TAX
                BEQ     @done
@bit:           ASL     UT_N
                ROL     UT_N + 1
                ROL     UT_N + 2
                DEX
                BNE     @bit
@done:          RTS

UT_DIV8:        CLC                    ; + 7: carry its bit 24
                LDA     UT_N
                ADC     #7
                STA     UT_N
                LDA     UT_N + 1
                ADC     #0
                STA     UT_N + 1
                LDA     UT_N + 2
                ADC     #0
                STA     UT_N + 2
                ROR     UT_N + 2       ; then / 8
                ROR     UT_N + 1
                ROR     UT_N
                LDX     #2
@bit:           LSR     UT_N + 2
                ROR     UT_N + 1
                ROR     UT_N
                DEX
                BNE     @bit
                RTS

UT_CMPN:        STA     @src + 1
                STX     @src + 2
                LDY     #2
@byte:          LDA     UT_N,Y
@src:           CMP     $FFFF,Y
                BNE     @done
                DEY
                BPL     @byte
                LDA     #0             ; equal: Z and carry set
                CMP     #0
@done:          RTS

; ---- dates and times -----------------------------------------------------

UT_MLEN:        TAX
                LDA     UT_MDAYS - 1,X
                CPX     #2
                BNE     @done
                PHA
                LDA     UT_YEAR
                AND     #3
                BNE     @common
                PLA
                INC     A
                RTS
@common:        PLA
@done:          RTS

; The days in UT_YEAR, low in A, high in X.
UT_YLEN:        LDX     #1
                LDA     UT_YEAR
                AND     #3
                BEQ     @leap
                LDA     #<365
                RTS
@leap:          LDA     #<366
                RTS

UT_TODATE:      STA     UT_D
                STX     UT_D + 1
                LDA     #<1978
                STA     UT_YEAR
                LDA     #>1978
                STA     UT_YEAR + 1
@year:          JSR     UT_YLEN        ; a whole year left: past it
                STA     UT_T
                STX     UT_T + 1
                CMP     UT_D
                TXA
                SBC     UT_D + 1
                BCS     @months        ; the year's length >= the days left
                SEC
                LDA     UT_D
                SBC     UT_T
                STA     UT_D
                LDA     UT_D + 1
                SBC     UT_T + 1
                STA     UT_D + 1
                INC     UT_YEAR
                BNE     @year
                INC     UT_YEAR + 1
                BRA     @year
@months:        LDA     #1
                STA     UT_MONTH
@month:         LDA     UT_MONTH
                JSR     UT_MLEN
                LDX     UT_D + 1
                BNE     @past
                CMP     UT_D
                BCS     @day           ; the month's length >= the days left
@past:          STA     UT_T
                SEC
                LDA     UT_D
                SBC     UT_T
                STA     UT_D
                BCS     @on
                DEC     UT_D + 1
@on:            INC     UT_MONTH
                BRA     @month
@day:           LDA     UT_D
                STA     UT_DAY
                RTS

UT_TODAYS:      LDA     UT_YEAR        ; the given year, kept
                PHA
                LDA     UT_YEAR + 1
                PHA
                LDA     UT_DAY
                STA     UT_D
                STZ     UT_D + 1
                LDX     UT_MONTH       ; the months before
@month:         DEX
                BEQ     @years
                PHX
                TXA
                JSR     UT_MLEN
                CLC
                ADC     UT_D
                STA     UT_D
                BCC     @mnext
                INC     UT_D + 1
@mnext:         PLX
                BRA     @month
@years:         LDA     UT_YEAR        ; then the years from 1978
                BNE     @dec
                DEC     UT_YEAR + 1
@dec:           DEC     UT_YEAR
                LDA     UT_YEAR
                CMP     #<1977
                BNE     @add
                LDA     UT_YEAR + 1
                CMP     #>1977
                BEQ     @done
@add:           JSR     UT_YLEN
                CLC
                ADC     UT_D
                STA     UT_D
                TXA
                ADC     UT_D + 1
                STA     UT_D + 1
                BRA     @years
@done:          PLA
                STA     UT_YEAR + 1
                PLA
                STA     UT_YEAR
                LDA     UT_D
                LDX     UT_D + 1
                RTS

UT_PUT2:        LDX     #'0' - 1
@tens:          INX
                SEC
                SBC     #10
                BCS     @tens
                ADC     #10 + '0'
                PHA
                TXA
                JSR     CM_PUTC
                PLA
                JMP     CM_PUTC

UT_PUTDATE:     JSR     UT_TODATE
                LDA     UT_MONTH
                JSR     UT_PUT2
                LDA     #'/'
                JSR     CM_PUTC
                LDA     UT_DAY
                JSR     UT_PUT2
                LDA     #'/'
                JSR     CM_PUTC
                LDA     UT_YEAR        ; the year's last two digits
                LDX     UT_YEAR + 1
@cent:          CPX     #0
                BNE     @less
                CMP     #100
                BCC     @two
@less:          SEC
                SBC     #100
                BCS     @cent
                DEX
                BRA     @cent
@two:           JMP     UT_PUT2

; A, BCD, as two digits.
UT_PUTBCD:      PHA
                LSR     A
                LSR     A
                LSR     A
                LSR     A
                ORA     #'0'
                JSR     CM_PUTC
                PLA
                AND     #$0F
                ORA     #'0'
                JMP     CM_PUTC

UT_PUTTIME:     JSR     UT_PUTBCD
                LDA     #':'
                JSR     CM_PUTC
                TXA
                JMP     UT_PUTBCD

UT_PUTTIMES:    JSR     UT_PUTTIME
                LDA     #':'
                JSR     CM_PUTC
                TYA
                JMP     UT_PUTBCD

UT_PUTSTAMP:    STA     @src + 1
                STX     @src + 2
                LDY     #3
@src:           LDA     $FFFF,Y
                STA     UT_STMP,Y
                DEY
                BPL     @src
                LDA     UT_STMP
                ORA     UT_STMP + 1
                BNE     @stamp
                LDA     #14
                JMP     UT_BLANKS
@stamp:         LDA     UT_STMP
                LDX     UT_STMP + 1
                JSR     UT_PUTDATE
                LDA     #' '
                JSR     CM_PUTC
                LDA     UT_STMP + 2
                LDX     UT_STMP + 3
                JMP     UT_PUTTIME

UT_PUTWDAY:     SEC                    ; (days - 1) mod 7: 0 Sunday
                SBC     #1
                STA     UT_D
                TXA
                SBC     #0
                STA     UT_D + 1
                LDA     #0
                LDX     #16
@bit:           ASL     UT_D
                ROL     UT_D + 1
                ROL     A
                CMP     #7
                BCC     @less
                SBC     #7
@less:          DEX
                BNE     @bit
                STA     UT_D           ; x 3
                ASL     A
                ADC     UT_D
                TAX
                LDY     #3
@char:          LDA     UT_WDAYS,X
                JSR     CM_PUTC
                INX
                DEY
                BNE     @char
                RTS

; ---- the directory -------------------------------------------------------

UT_SCAN:        LDA     #<UT_REC
                LDX     #>UT_REC
                LDY     #F_DMAOFF
                JSR     BDOS
                LDX     #35
@fcb:           LDA     #0
                CPX     #12
                BCS     @put
                LDA     #'?'
@put:           STA     UT_QFCB,X
                DEX
                BPL     @fcb
                STZ     UT_ENT
                STZ     UT_ENT + 1
                LDA     #<UT_QFCB
                LDX     #>UT_QFCB
                LDY     #F_SFIRST
                BRA     UT_SGOT

UT_SNEXT:       INC     UT_ENT
                BNE     @next
                INC     UT_ENT + 1
@next:          LDY     #F_SNEXT
UT_SGOT:        JSR     BDOS
                CMP     #$FF
                BEQ     @end
                ASL     A              ; x 32: its place in the record
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                CLC
                ADC     #<UT_REC
                STA     UT_EADR
                LDA     #>UT_REC
                ADC     #0
                STA     UT_EADR + 1
                TAX
                LDA     UT_EADR
                CLC
                RTS
@end:           SEC
                RTS

UT_LABEL:       JSR     UT_SCAN
@entry:         BCS     @done
                STA     @src + 1
                STX     @src + 2
                STA     @first + 1
                STX     @first + 2
@first:         LDA     $FFFF
                CMP     #$20
                BEQ     @found
                JSR     UT_SNEXT
                BRA     @entry
@found:         LDY     #31
@src:           LDA     $FFFF,Y
                STA     UT_LBL,Y
                DEY
                BPL     @src
                CLC
@done:          RTS

UT_SFCBS:       JSR     UT_SCAN
@entry:         BCS     @end
                STA     @first + 1
                STX     @first + 2
                LDA     UT_ENT
                CMP     #3
                BEQ     @first
                JSR     UT_SNEXT
                BRA     @entry
@first:         LDA     $FFFF
                CMP     #$21
                BNE     @none
                LDA     #1
                CLC
                RTS
@end:           CPX     #0             ; the scan's end: no SFCBs
                BNE     @failed
@none:          LDA     #0
                CLC
                RTS
@failed:        SEC
                RTS

UT_PWDEC:       STA     @key + 1
                STX     @key + 2
                CLC
                ADC     #16
                STA     @src + 1
                TXA
                ADC     #0
                STA     @src + 2
                LDY     #13
@key:           LDA     $FFFF,Y
                STA     UT_T
                LDX     #7             ; bytes 23 to 16: the password's 0 to 7
                LDY     #0
@src:           LDA     $FFFF,X
                EOR     UT_T
                STA     UT_PW,Y
                INY
                DEX
                BPL     @src
                RTS

UT_SETPAT:      CLC                    ; its bytes 1-11
                ADC     #1
                STA     UT_MPAT + 1
                TXA
                ADC     #0
                STA     UT_MPAT + 2
                RTS

UT_MATCH:       STA     @name + 1
                STX     @name + 2
                LDY     #0
@char:          JSR     UT_MPAT
                AND     #$7F
                CMP     #'?'
                BEQ     @next
                STA     UT_T
@name:          LDA     $FFFF,Y
                AND     #$7F
                CMP     UT_T
                BNE     @no
@next:          INY
                CPY     #11
                BNE     @char
                SEC
                RTS
@no:            CLC
                RTS
UT_MPAT:        LDA     $FFFF,Y        ; the pattern's byte Y (UT_SETPAT's)
                RTS

; ---- drives --------------------------------------------------------------

UT_SELECT:      LDY     #DRV_SET
                JMP     BDOS

UT_BACK:        LDA     UT_DRV0
                BRA     UT_SELECT

UT_DPB:         LDY     #DRV_DPB
                JSR     BDOS
                STA     @src + 1
                STX     @src + 2
                LDY     #16
@src:           LDA     $FFFF,Y
                STA     UT_DPBC,Y
                DEY
                BPL     @src
                RTS

; ---- writing -------------------------------------------------------------

UT_PUTDRV:      CLC
                ADC     #'A'
                JSR     CM_PUTC
                LDA     #':'
                JMP     CM_PUTC

UT_SAY:         JSR     CM_PUTS
                JMP     CM_EOL

UT_BLANKS:      TAX
                BEQ     @done
@blank:         LDA     #' '
                JSR     CM_PUTC        ; keeps X
                DEX
                BNE     @blank
@done:          RTS

; ---- memory (SET and INITDIR) --------------------------------------------

.import __BSS_RUN__, __BSS_SIZE__
.pushseg
.segment CMD_BSS
UT_MLO:         .res    2
UT_MHI:         .res    2
.popseg

UT_MEM:         LDA     #<(__BSS_RUN__ + __BSS_SIZE__)
                STA     UT_MLO
                LDA     #>(__BSS_RUN__ + __BSS_SIZE__)
                STA     UT_MLO + 1
                LDA     BP_TOP
                STA     UT_MHI
                LDA     BP_TOP + 1
                STA     UT_MHI + 1
                SEC
                LDA     UT_MHI
                SBC     UT_MLO
                PHA
                LDA     UT_MHI + 1
                SBC     UT_MLO + 1
                TAX
                PLA
                RTS

; ---- files (TYPE, ERASE, RENAME) -----------------------------------------

UT_NLIST        = __BSS_RUN__ + __BSS_SIZE__ ; the names: past the BSS

.pushseg
.segment CMD_BSS
UT_NCNT:        .res    2              ; UT_NAMES's count
UT_NLEFT:       .res    2              ; UT_NNEXT's names left
UT_NPTR:        .res    2              ; the next name, or the list's end
.popseg

UT_FDRIVE:      DEC     A              ; 1-16: 0-15
                BPL     @done
                LDA     UT_DRV0
@done:          RTS

UT_ISRO:        STA     UT_T + 2       ; the drive (UT_T: scratch, as UT_NOUT's)
                LDY     #DRV_ROVEC
                JSR     BDOS
                STA     UT_T
                STX     UT_T + 1
                LDX     UT_T + 2
                BEQ     @bit
@shift:         LSR     UT_T + 1
                ROR     UT_T
                DEX
                BNE     @shift
@bit:           LDA     UT_T
                LSR     A              ; bit 0 into the carry
                RTS

UT_ISWILD:      STA     @src + 1
                STX     @src + 2
                LDY     #10
@src:           LDA     $FFFF,Y
                CMP     #'?'
                BEQ     @yes
                DEY
                BPL     @src
                CLC
                RTS
@yes:           SEC
                RTS

UT_PWSET:       STA     @src + 1
                STX     @src + 2
                LDY     #7
@src:           LDA     $FFFF,Y
                STA     UT_REC,Y
                DEY
                BPL     @src
                LDA     #<UT_REC
                LDX     #>UT_REC
                LDY     #F_DMAOFF
                JMP     BDOS

UT_NAMES:       STA     @src + 1
                STX     @src + 2
                LDY     #35            ; UT_QFCB: the drive and name, the rest 0
@fcb:           LDA     #0
                CPY     #12
                BCS     @put
@src:           LDA     $FFFF,Y
@put:           STA     UT_QFCB,Y
                DEY
                BPL     @fcb
                STZ     UT_NCNT
                STZ     UT_NCNT + 1
                LDA     #<UT_NLIST
                STA     UT_NPTR
                LDA     #>UT_NLIST
                STA     UT_NPTR + 1
                LDA     #<UT_REC
                LDX     #>UT_REC
                LDY     #F_DMAOFF
                JSR     BDOS
                LDA     #<UT_QFCB
                LDX     #>UT_QFCB
                LDY     #F_SFIRST
@search:        JSR     BDOS
                CMP     #$FF
                BEQ     @end
                ASL     A              ; x 32, + 1: its name in UT_REC
                ASL     A
                ASL     A
                ASL     A
                ASL     A
                INC     A
                TAX
                CLC                    ; room for 11 more, to the TPA's top?
                LDA     UT_NPTR
                ADC     #11
                STA     UT_T
                LDA     UT_NPTR + 1
                ADC     #0
                STA     UT_T + 1
                LDA     BP_TOP         ; the top less the end: below 0, full
                CMP     UT_T
                LDA     BP_TOP + 1
                SBC     UT_T + 1
                BCC     @full
                LDA     UT_NPTR
                STA     @dst + 1
                LDA     UT_NPTR + 1
                STA     @dst + 2
                LDY     #0
@name:          LDA     UT_REC,X
                AND     #$7F
@dst:           STA     $FFFF,Y
                INX
                INY
                CPY     #11
                BNE     @name
                LDA     UT_T           ; past it
                STA     UT_NPTR
                LDA     UT_T + 1
                STA     UT_NPTR + 1
                INC     UT_NCNT
                BNE     @next
                INC     UT_NCNT + 1
@next:          LDY     #F_SNEXT
                BRA     @search
@end:           CPX     #0
                BEQ     @all
                LDA     #$FF           ; the BDOS's error, said
                SEC
                RTS
@full:          LDA     #1
                SEC
                RTS
@all:           LDA     #<UT_NLIST
                STA     UT_NPTR
                LDA     #>UT_NLIST
                STA     UT_NPTR + 1
                LDA     UT_NCNT
                STA     UT_NLEFT
                LDX     UT_NCNT + 1
                STX     UT_NLEFT + 1
                CLC
                RTS

UT_NNEXT:       LDA     UT_NLEFT
                ORA     UT_NLEFT + 1
                BNE     @one
                SEC
                RTS
@one:           LDA     UT_NLEFT
                BNE     @dec
                DEC     UT_NLEFT + 1
@dec:           DEC     UT_NLEFT
                LDA     UT_NPTR        ; its address, then past it
                LDX     UT_NPTR + 1
                PHA
                CLC
                ADC     #11
                STA     UT_NPTR
                BCC     @done
                INC     UT_NPTR + 1
@done:          PLA
                CLC
                RTS

UT_SAID:        STZ     CM_LFDUE
                JSR     CM_NOEOL
                JMP     UT_FAIL
; ---- the system (SHOW, DATE, DEVICE, SETDEF) -----------------------------

.pushseg
.segment CMD_BSS
UT_SCBPB:       .res    4              ; 49's: the offset, the mode, the value
UT_SCBW:        .res    2
UT_BPB:         .res    8              ; 50's: the function, A, X, Y, two words
.popseg

UT_OAT:         LDX     UT_IDX
                INX
                STX     UT_OIDX
                STZ     UT_VST
                RTS

UT_OCLOSE:      LDX     UT_OIDX
                LDA     UT_TAIL,X
                CMP     #']'
                BNE     @open
                INX
                STX     UT_IDX
                CLC
                RTS
@open:          SEC
                RTS

UT_SCBGET:      STA     UT_SCBPB
                STZ     UT_SCBPB + 1   ; 0: get
                BRA     UT_SCBCALL

UT_SCBSET:      STA     UT_SCBPB
                LDA     #$FF           ; $FF: set a byte
                STA     UT_SCBPB + 1
                STX     UT_SCBPB + 2
                STZ     UT_SCBPB + 3
UT_SCBCALL:     LDA     #<UT_SCBPB
                LDX     #>UT_SCBPB
                LDY     #S_SCB
                JMP     BDOS

UT_SCBSETW:     STA     UT_SCBPB
                LDA     #$FE           ; $FE: set a word
                STA     UT_SCBPB + 1
                LDA     UT_SCBW
                STA     UT_SCBPB + 2
                LDA     UT_SCBW + 1
                STA     UT_SCBPB + 3
                BRA     UT_SCBCALL

UT_BIOS:        STA     UT_BPB
                STX     UT_BPB + 1
                STY     UT_BPB + 2
                LDA     #<UT_BPB
                LDX     #>UT_BPB
                LDY     #S_BIOS
                JMP     BDOS

.endif

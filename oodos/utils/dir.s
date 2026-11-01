; DIR.COM (hw/oodos.md, "The utilities, phase 4b": DIR): the directory with
; options, from the CP/M 3 User's Guide (5-19 to 5-28, Appendix A). Where
; the Guide is silent the choice is ours, and hw/oodos.md says so.
;
;   DIR {d:}                    the built-in's listing: the current user's
;   DIR {filespec} ...          files without SYS, sorted, each drive given
;                               in turn, No File, SYSTEM FILE(S) EXIST
;   DIR {d:} [options]          FULL (the default), SIZE or DATE, per drive
;   DIR {filespec} ... [options] and user, with totals
;
; The options (Table 5-5, any unambiguous prefix): ATT, DATE, DIR,
; DRIVE=ALL|d|(d,...) (DISK= too), EXCLUDE, FF, FULL, LENGTH=n, MESSAGE,
; NOPAGE, NOSORT, RO, RW, SIZE, SYS, USER=ALL|n|(n,...).
;
; Each drive's directory is read once, by 17 and 18 with '?' (util.inc's
; UT_SCAN): a record of 32 bytes for each file of a user and name wanted
; (its entries' blocks and records summed, its extent-zero entry's name,
; attributes and SFCB stamps, its XFCB's mode), then a pointer to each,
; sorted. What doesn't fit between the program and the TPA's top is "Not
; Enough Memory for Sort.", nothing listed for that drive. Both the lookup
; and the sort grow as the square of the files; a ^C stops either.
;
; Lines are separated by CM_EOL (cmd.inc), so the last is ended by the
; prompt's CR LF; EOL here also counts them for LENGTH's headings.

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK
CMD_QUIETSTOP = 1                      ; a stop's message not to the printer (FF)

        OODOS_HEADER

HEAP            = __BSS_RUN__ + __BSS_SIZE__   ; the records, then their pointers
MAXSPEC         = 64                   ; specifications kept: 12 bytes each
SPECLEN         = 12                   ; the drive byte and the name, as an FCB's 0-11
RECLEN          = 32                   ; a file's record:
R_USER          = 0                    ;   its user
R_NAME          = 1                    ;   the name and type, the extent-zero entry's bits
R_FLAGS         = 12                   ;   RF_EXT0, RF_XFCB
R_XMODE         = 13                   ;   the XFCB's mode
R_RECS          = 14                   ;   records, 24 bits
R_BLKS          = 17                   ;   blocks, 16 bits
R_STA           = 19                   ;   the SFCB's creation (or access) stamp
R_STU           = 23                   ;   and its update stamp
R_ENT           = 27                   ;   the extent-zero entry's number, for NOSORT
RF_EXT0         = $01
RF_XFCB         = $02
FF              = $0C
SCB_WIDTH       = $1A                  ; the SCB: the console width - 1
SCB_ECHO        = $38                  ;   printer echo (^P)

; The options, OPTAB's order.
O_ATT           = 0
O_DATE          = 1
O_DIR           = 2
O_DISK          = 3
O_DRIVE         = 4
O_EXCL          = 5
O_FF            = 6
O_FULL          = 7
O_LEN           = 8
O_MSG           = 9
O_NOPAGE        = 10
O_NOSORT        = 11
O_RO            = 12
O_RW            = 13
O_SIZE          = 14
O_SYS           = 15
O_USER          = 16
NOPTS           = 17

FM_FULL         = 0                    ; FORMAT
FM_SIZE         = 1
FM_DATE         = 2
FM_SHORT        = $FF                  ; no option list: the built-in's

.macro PUTS str
        LDA     #<str
        LDX     #>str
        JSR     CM_PUTS
.endmacro

.macro SAYS str
        LDA     #<str
        LDX     #>str
        JSR     SAYLN
.endmacro

.zeropage
EP:             .res    2              ; the directory entry
RP:             .res    2              ; a record
R1:             .res    2              ; CMPR's two records
R2:             .res    2
IP:             .res    2              ; the pointers
JP:             .res    2
QP:             .res    2
SP:             .res    2              ; a specification

.bss
VARS:                                  ; cleared at the start (BSS isn't)
FLAGS:          .res    NOPTS          ; 1: the option given
NSPEC:          .res    1
SPECDRV:        .res    1              ; 1: a specification has a drive
DRVOPT:         .res    1              ; 1: DRIVE= given
NDRV:           .res    1
DRVLIST:        .res    16             ; the drives, 0-15, in order
USERS:          .res    2              ; a bit a user, 0-15
HASUSER:        .res    1
LENGTH:         .res    2              ; lines between headings, 0: none
HASLEN:         .res    1
FORMAT:         .res    1
SYSF:           .res    1              ; 0 both, 1 DIR only, 2 SYS only
ROF:            .res    1              ; 0 both, 1 RO only, 2 RW only
FAILED:         .res    1              ; 1: the return code $FF00
FOUNDANY:       .res    1
SEARCHED:       .res    1              ; 1: a drive's directory was read
ANYOUT:         .res    1              ; 1: a group or message written
LCOUNT:         .res    2              ; lines since the last heading
DUE:            .res    1              ; 1: a heading before the next file line
HDRJUST:        .res    1              ; 1: the heading the last line written
VARSEND:
LISTK:          .res    1              ; UT_OPTS's answer
CURUSR:         .res    1
CURD:           .res    1              ; the drive listed
DIDX:           .res    1
VEC:            .res    2
ECHO:           .res    1
NCOL:           .res    1
COL:            .res    1
ROOM:           .res    2              ; bytes for records and pointers
USED:           .res    2
NREC:           .res    2
CNT:            .res    2
CNT8:           .res    1
PTRS:           .res    2
TUSER:          .res    1
HASLBL:         .res    1
LBLMODE:        .res    1
HASSF:          .res    1              ; 1: the drive has SFCBs
NFREE:          .res    2
NSFCB:          .res    2
NENT:           .res    2
NOTEXM:         .res    1
BSHIFT:         .res    1              ; kilobytes = blocks << BSHIFT
WORDS:          .res    1              ; 1: 16-bit block pointers
GU:             .res    1              ; the group's user
GOPEN:          .res    1
FOUND:          .res    1
OTHER:          .res    1
T:              .res    1
TOTALS:                                ; a group's
TKB:            .res    3
TRECS:          .res    3
TBLK:           .res    3
TFILES:         .res    2
TOTEND:
KB:             .res    3
SCBPB:          .res    4
SFCB36:         .res    36             ; UT_SPEC's FCB
SPECS:          .res    MAXSPEC * SPECLEN

.code
START:          JSR     UT_INIT
                LDX     #VARSEND - VARS - 1
@clear:         STZ     VARS,X
                DEX
                BPL     @clear
                LDA     #$FF
                LDY     #F_USERNUM
                JSR     BDOS
                STA     CURUSR
                JSR     UT_OPTS
                STA     LISTK
                CMP     #UT_LTWO
                BNE     @one
                LDA     #<S_GROUP
                LDX     #>S_GROUP
                JMP     ERROR
@one:           CMP     #UT_LOPEN
                BEQ     @tail
                JSR     GETSPECS
                BCC     @specs
@tail:          LDA     #<S_TAIL
                LDX     #>S_TAIL
                JMP     ERROR
@specs:         LDA     LISTK
                BNE     @opts
                LDA     #FM_SHORT
                STA     FORMAT
                BRA     @users
@opts:          JSR     GETOPTS
                BCC     @optok
                LDA     #<S_OPT
                LDX     #>S_OPT
                JMP     ERROR
@optok:         JSR     CHKFMT
                BCC     @fmtok
                LDA     #<S_FORMAT
                LDX     #>S_FORMAT
                JMP     ERROR
@fmtok:         LDA     DRVOPT
                AND     SPECDRV
                BEQ     @users
                LDA     #<S_MIX
                LDX     #>S_MIX
                JMP     ERROR
@users:         LDA     HASUSER
                BNE     @drives
                LDA     CURUSR
                JSR     ADDUSER
@drives:        LDA     DRVOPT
                BNE     @setup
                JSR     SPECDRVS
@setup:         JSR     SETUP
                STZ     DIDX
@drive:         LDX     DIDX
                CPX     NDRV
                BEQ     @end
                LDA     DRVLIST,X
                STA     CURD
                JSR     DODRIVE
                INC     DIDX
                BRA     @drive
@end:           LDA     FORMAT         ; nothing found anywhere searched: No File
                CMP     #FM_SHORT
                BEQ     @rc
                LDA     FOUNDANY
                ORA     FLAGS + O_MSG
                BNE     @rc
                LDA     SEARCHED
                BEQ     @rc
                JSR     SEPAR
                SAYS    S_NOFILE
                LDA     #1
                STA     FAILED
@rc:            LDA     FAILED
                BEQ     @exit
                JSR     UT_FAIL
@exit:          JMP     UT_EXIT

; The message at A/X on a line, the return code $FF00, the end.
ERROR:          JSR     SAYLN
                JSR     UT_FAIL
                JMP     UT_EXIT

; ---- the tail ------------------------------------------------------------

; Each specification (152's) into SPECS: its drive and name, a blank name
; and type made every name. Carry set: one refused, or more than MAXSPEC.
GETSPECS:       LDA     #<SPECS
                STA     SP
                LDA     #>SPECS
                STA     SP + 1
@next:          LDA     #<SFCB36
                LDX     #>SFCB36
                JSR     UT_SPEC
                BCC     @got
                CMP     #0
                BNE     @bad
                CLC                    ; none left
                RTS
@bad:           SEC
                RTS
@got:           LDA     NSPEC
                CMP     #MAXSPEC
                BCS     @bad
                LDY     #SPECLEN - 1
@copy:          LDA     SFCB36,Y
                STA     (SP),Y
                DEY
                BPL     @copy
                LDA     (SP)
                BEQ     @nodrv
                LDA     #1
                STA     SPECDRV
@nodrv:         LDY     #1             ; a drive alone: every file
@blank:         LDA     (SP),Y
                CMP     #' '
                BNE     @named
                INY
                CPY     #SPECLEN
                BNE     @blank
                LDY     #SPECLEN - 1
                LDA     #'?'
@all:           STA     (SP),Y
                DEY
                BNE     @all
@named:         JSR     SPNEXT
                INC     NSPEC
                BRA     @next

SPNEXT:         CLC
                LDA     SP
                ADC     #SPECLEN
                STA     SP
                BCC     @done
                INC     SP + 1
@done:          RTS

; The option list. Carry set: an unknown or ambiguous name, a value where
; none goes or none where one must, a bad value.
GETOPTS:
@next:          LDA     #<OPTAB
                LDX     #>OPTAB
                JSR     UT_OPT
                BCC     @opt
                CMP     #UT_OEND
                BNE     @bad
                CLC
                RTS
@bad:           SEC
                RTS
@opt:           CMP     #O_DISK
                BEQ     @drive
                CMP     #O_DRIVE
                BEQ     @drive
                CMP     #O_LEN
                BEQ     @len
                CMP     #O_USER
                BEQ     @user
                TAX
                LDA     UT_HASV        ; the others take no value
                BNE     @bad
                LDA     #1
                STA     FLAGS,X
                BRA     @next
@len:           LDA     UT_HASV
                BEQ     @bad
                JSR     UT_VITEM
                BCS     @bad
                JSR     UT_WDEC
                BCS     @bad
                CPX     #0             ; 5-65535
                BNE     @lok
                CMP     #5
                BCC     @bad
@lok:           STA     LENGTH
                STX     LENGTH + 1
                LDA     #1
                STA     HASLEN
                JSR     UT_VITEM       ; one number, not a list
                BCC     @bad
                CMP     #0
                BNE     @bad
                BRA     @next
@user:          LDA     UT_HASV
                BEQ     @bad
                LDA     #1
                STA     HASUSER
@uitem:         JSR     UT_VITEM
                BCC     @u
                CMP     #0
                BNE     @bad
                BRA     @next
@u:             JSR     ISALL
                BCC     @un
                LDA     #$FF
                STA     USERS
                STA     USERS + 1
                BRA     @uitem
@un:            JSR     UT_WDEC
                BCS     @bad
                CPX     #0
                BNE     @bad
                CMP     #16
                BCS     @bad
                JSR     ADDUSER
                BRA     @uitem
@drive:         LDA     UT_HASV
                BEQ     @dbad
                LDA     #1
                STA     DRVOPT
@ditem:         JSR     UT_VITEM
                BCC     @d
                CMP     #0
                BNE     @dbad
                JMP     @next
@dbad:          SEC
                RTS
@d:             JSR     ISALL
                BCC     @dn
                JSR     ALLDRVS
                BRA     @ditem
@dn:            JSR     UT_WDRIVE
                BCS     @dbad
                CMP     #0             ; '*': the current drive
                BNE     @letter
                LDA     UT_DRV0
                BRA     @add
@letter:        DEC     A
@add:           JSR     ADDDRV
                BRA     @ditem

; UT_WORD is ALL? Carry set if so.
ISALL:          LDA     UT_WLEN
                CMP     #3
                BNE     @no
                LDX     #2
@char:          LDA     UT_WORD,X
                CMP     S_ALL,X
                BNE     @no
                DEX
                BPL     @char
                SEC
                RTS
@no:            CLC
                RTS

; DRIVE=ALL: every logged-in drive (function 24), A to P. The Guide's table
; ("all accessed drives") followed over its example (the drives of the search
; chain): decided by the user, 2026-10-31. This routine alone makes the choice.
ALLDRVS:        LDY     #DRV_LOGINVEC
                JSR     BDOS
                STA     VEC
                STX     VEC + 1
                LDA     #0
@drive:         LSR     VEC + 1
                ROR     VEC
                BCC     @out
                JSR     ADDDRV         ; keeps A
@out:           INC     A
                CMP     #16
                BNE     @drive
                RTS

; Drive A (0-15) onto DRVLIST, unless it is there. Keeps A.
ADDDRV:         LDX     #0
@look:          CPX     NDRV
                BEQ     @add
                CMP     DRVLIST,X
                BEQ     @have
                INX
                BRA     @look
@add:           STA     DRVLIST,X
                INC     NDRV
@have:          RTS

; User A (0-15) into USERS.
ADDUSER:        CMP     #8
                BCS     @high
                TAX
                LDA     BITS,X
                ORA     USERS
                STA     USERS
                RTS
@high:          SBC     #8
                TAX
                LDA     BITS,X
                ORA     USERS + 1
                STA     USERS + 1
                RTS

; User A (0-15) in USERS? Carry set if so.
USERWANT:       CMP     #8
                BCS     @high
                TAX
                LDA     BITS,X
                AND     USERS
                BRA     @test
@high:          SBC     #8
                TAX
                LDA     BITS,X
                AND     USERS + 1
@test:          CMP     #1
                RTS

; The format: FULL unless SIZE or DATE; two of them, carry set. The
; attribute filters: both of a pair, or neither, is both.
CHKFMT:         LDA     FLAGS + O_FULL
                CLC
                ADC     FLAGS + O_SIZE
                ADC     FLAGS + O_DATE
                CMP     #2
                BCS     @done
                LDA     #FM_SIZE
                LDX     FLAGS + O_SIZE
                BNE     @fmt
                LDA     #FM_DATE
                LDX     FLAGS + O_DATE
                BNE     @fmt
                LDA     #FM_FULL
@fmt:           STA     FORMAT
                LDA     FLAGS + O_DIR
                EOR     FLAGS + O_SYS
                BEQ     @sys
                LDA     #1
                LDX     FLAGS + O_DIR
                BNE     @sys
                LDA     #2
@sys:           STA     SYSF
                LDA     FLAGS + O_RO
                EOR     FLAGS + O_RW
                BEQ     @ro
                LDA     #1
                LDX     FLAGS + O_RO
                BNE     @ro
                LDA     #2
@ro:            STA     ROF
                CLC
@done:          RTS

; The drives without DRIVE=: each specification's, in the order first
; given (no drive: the current), or the current drive alone.
SPECDRVS:       LDA     NSPEC
                BNE     @specs
                LDA     UT_DRV0
                JMP     ADDDRV
@specs:         LDA     #<SPECS
                STA     SP
                LDA     #>SPECS
                STA     SP + 1
                LDA     NSPEC
                STA     CNT8
@spec:          LDA     (SP)
                BNE     @given
                LDA     UT_DRV0
                BRA     @add
@given:         DEC     A
@add:           JSR     ADDDRV
                JSR     SPNEXT
                DEC     CNT8
                BNE     @spec
                RTS

; Paging, LENGTH's default, the printer's form feed, the names a line, the
; room for records.
SETUP:          JSR     CM_PAGE
                LDA     FLAGS + O_NOPAGE
                BNE     @nopage
                LDA     FLAGS + O_FF   ; FF without LENGTH= turns paging off (the
                BEQ     @paged         ; Guide); with it, a form feed every n lines
                LDA     HASLEN
                BNE     @paged
@nopage:        STZ     CM_PLEN
@paged:         LDA     HASLEN         ; no LENGTH=: a heading each page, when paged,
                BNE     @len           ; and LENGTH='s least, 5, for a short page
                LDA     CM_PLEN
                BEQ     @deflen
                CMP     #5
                BCS     @deflen
                LDA     #5
@deflen:        STA     LENGTH
                STZ     LENGTH + 1
@len:           LDA     #SCB_ECHO
                JSR     SCBGET
                STA     ECHO
                LDA     FLAGS + O_FF
                BEQ     @width
                JSR     LSTFF
@width:         LDA     #SCB_WIDTH     ; the width less its last column, an item each
                JSR     SCBGET         ; 15 (the built-in's) or 21 (SIZE's)
                STA     UT_N
                STZ     UT_N + 1
                STZ     UT_N + 2
                LDA     #15
                LDX     FORMAT
                CPX     #FM_SIZE
                BNE     @div
                LDA     #21
@div:           JSR     UT_DIVN
                LDA     UT_N
                BNE     @cols
                INC     A
@cols:          STA     NCOL
                SEC                    ; the room: the TPA's top less the program,
                LDA     BP_TOP         ; DIRROOM at most
                SBC     #<HEAP
                STA     ROOM
                LDA     BP_TOP + 1
                SBC     #>HEAP
                STA     ROOM + 1
                LDA     DIRROOM
                CMP     ROOM
                LDA     DIRROOM + 1
                SBC     ROOM + 1
                BCS     @room
                LDA     DIRROOM
                STA     ROOM
                LDA     DIRROOM + 1
                STA     ROOM + 1
@room:          RTS

; The SCB's byte A (49).
SCBGET:         STA     SCBPB
                STZ     SCBPB + 1
                LDA     #<SCBPB
                LDX     #>SCBPB
                LDY     #S_SCB
                JMP     BDOS

; A form feed to LST: when the printer echo is on (the Guide).
LSTFF:          LDA     ECHO
                BEQ     @none
                LDA     #FF
                LDY     #L_WRITE
                JMP     BDOS
@none:          RTS

; ---- a drive -------------------------------------------------------------

DODRIVE:        LDA     CM_EOLDUE      ; a line owed is held back: if the select
                PHA                    ; fails, the BDOS's CR LF ends it
                JSR     CM_NOEOL
                LDA     CURD
                JSR     UT_SELECT
                TAX
                PLA
                CPX     #0
                BEQ     @selected
                LDA     #1             ; the BDOS said why
                STA     FAILED
                STA     ANYOUT
                RTS
@selected:      CMP     #0
                BEQ     @dpb
                JSR     CM_EOL
@dpb:           JSR     UT_DPB
                LDA     UT_DPBC + 4    ; EXM
                EOR     #$FF
                STA     NOTEXM
                LDA     UT_DPBC + 2    ; BSH: 1 KB is 3
                SEC
                SBC     #3
                STA     BSHIFT
                LDA     UT_DPBC + 6    ; DSM over 255: 16-bit pointers
                STA     WORDS
                JSR     COLLECT
                BCC     @read
                CMP     #0
                BNE     @nomem
                JSR     UT_SAID        ; the BDOS said why: its CR LF ends the
                LDA     #1             ; line owed, as for a refused drive
                STA     ANYOUT
                BRA     @failed
@nomem:         LDA     #<S_NOMEM
                LDX     #>S_NOMEM
                JSR     DRVMSG
@failed:        LDA     #1
                STA     FAILED
                RTS
@read:          LDA     FORMAT
                CMP     #FM_DATE
                BNE     @list
                LDA     HASSF
                BNE     @list
                LDA     #<S_INACT
                LDX     #>S_INACT
                JSR     DRVMSG
                LDA     #1
                STA     FAILED
                RTS
@list:          LDA     #1
                STA     SEARCHED
                JSR     SORTP
                LDA     FORMAT
                CMP     #FM_SHORT
                BNE     @groups
                JMP     SHORTL
@groups:        STZ     GU
@user:          LDA     GU
                JSR     USERWANT
                BCC     @next
                JSR     GROUP
@next:          INC     GU
                LDA     GU
                CMP     #16
                BNE     @user
                RTS

; A drive's message on a line of its own.
DRVMSG:         PHA
                PHX
                JSR     SEPAR
                PLX
                PLA
                JMP     SAYLN

; The directory read into records. Carry clear: done; carry set and A $FF
; no room (nothing said), A 0 the BDOS's error (said).
COLLECT:        STZ     NREC
                STZ     NREC + 1
                STZ     USED
                STZ     USED + 1
                STZ     HASLBL
                STZ     LBLMODE
                STZ     HASSF
                STZ     NFREE
                STZ     NFREE + 1
                STZ     NSFCB
                STZ     NSFCB + 1
                JSR     UT_SCAN
@entry:         BCC     @got
                CPX     #0
                BNE     @bdos
                LDA     UT_ENT         ; past the last: the count
                STA     NENT
                LDA     UT_ENT + 1
                STA     NENT + 1
                CLC
                RTS
@bdos:          LDA     #0
                SEC
                RTS
@got:           STA     EP
                STX     EP + 1
                LDA     (EP)
                CMP     #$E5
                BNE     @used
                INC     NFREE
                BNE     @next
                INC     NFREE + 1
                BRA     @next
@used:          CMP     #$21
                BNE     @nsfcb
                INC     NSFCB
                BNE     @sfcb
                INC     NSFCB + 1
@sfcb:          LDA     UT_ENT + 1     ; entry 3 an SFCB: the drive has them
                BNE     @next
                LDA     UT_ENT
                CMP     #3
                BNE     @next
                LDA     #1
                STA     HASSF
                BRA     @next
@nsfcb:         CMP     #$20
                BNE     @nlabel
                LDA     HASLBL         ; the first label
                BNE     @next
                INC     HASLBL
                LDY     #12
                LDA     (EP),Y
                STA     LBLMODE
                BRA     @next
@nlabel:        CMP     #16
                BCS     @nfile
                JSR     FILEENT
                BCS     @full
                BRA     @next
@nfile:         CMP     #32
                BCS     @next
                JSR     XFCBENT
                BCS     @full
@next:          LDA     UT_ENT         ; a ^C every 16 entries: FIND's lookup is
                AND     #$0F           ; slow on a big directory
                BNE     @snext
                JSR     CM_BRK
@snext:         JSR     UT_SNEXT
                JMP     @entry
@full:          LDA     #$FF
                RTS

; A file's entry, user A: its record found or made, its blocks and records,
; and if it is the extent-zero entry its name, attributes and stamps.
; Carry set: no room.
FILEENT:        STA     TUSER
                JSR     USERWANT
                BCC     @skip
                JSR     NAMEOK
                BCC     @skip
                JSR     FIND
                BCC     @found
                RTS
@skip:          CLC
                RTS
@found:         JSR     EBLOCKS
                LDY     #R_BLKS
                CLC
                ADC     (RP),Y
                STA     (RP),Y
                INY
                LDA     (RP),Y
                ADC     #0
                STA     (RP),Y
                LDY     #14            ; records: (S2 x 32 + EX) x 128 + RC
                LDA     (EP),Y
                STA     UT_N
                STZ     UT_N + 1
                STZ     UT_N + 2
                LDA     #5
                JSR     UT_SHLN
                LDY     #12
                LDA     (EP),Y
                AND     #$1F
                ORA     UT_N
                STA     UT_N
                LDA     #7
                JSR     UT_SHLN
                LDY     #15
                LDA     (EP),Y
                CLC
                ADC     UT_N
                STA     UT_N
                BCC     @recs
                INC     UT_N + 1
                BNE     @recs
                INC     UT_N + 2
@recs:          LDA     #R_RECS        ; the most of its entries'
                JSR     RADDR
                JSR     UT_CMPN
                BCC     @ext0
                LDA     #R_RECS
                JSR     RADDR
                JSR     UT_STN
@ext0:          LDY     #14            ; S2 0 and EX within EXM: extent zero's
                LDA     (EP),Y
                BNE     @done
                LDY     #12
                LDA     (EP),Y
                AND     #$1F
                AND     NOTEXM
                BNE     @done
                LDY     #R_FLAGS
                LDA     (RP),Y
                ORA     #RF_EXT0
                STA     (RP),Y
                LDY     #11
@name:          LDA     (EP),Y
                STA     (RP),Y
                DEY
                BNE     @name
                LDY     #R_ENT
                LDA     UT_ENT
                STA     (RP),Y
                INY
                LDA     UT_ENT + 1
                STA     (RP),Y
                LDA     UT_REC + 96    ; its SFCB's slot, if the record has one
                CMP     #$21
                BNE     @done
                LDA     UT_ENT
                AND     #3
                CMP     #3
                BEQ     @done
                TAX
                LDA     SLOTS,X
                TAX
                LDY     #R_STA
@stamp:         LDA     UT_REC,X
                STA     (RP),Y
                INX
                INY
                CPY     #R_STA + 8
                BNE     @stamp
@done:          CLC
                RTS

; An XFCB, byte 0 A: the mode into its file's record. Carry set: no room.
XFCBENT:        SEC
                SBC     #16
                STA     TUSER
                JSR     USERWANT
                BCC     @skip
                JSR     NAMEOK
                BCC     @skip
                JSR     FIND
                BCS     @full
                LDY     #R_FLAGS
                LDA     (RP),Y
                ORA     #RF_XFCB
                STA     (RP),Y
                LDY     #12
                LDA     (EP),Y
                LDY     #R_XMODE
                STA     (RP),Y
@skip:          CLC
@full:          RTS

; The entry's block pointers that aren't 0: A.
EBLOCKS:        STZ     T
                LDY     #16
                LDA     WORDS
                BNE     @word
@byte:          LDA     (EP),Y
                BEQ     @bnext
                INC     T
@bnext:         INY
                CPY     #32
                BNE     @byte
                LDA     T
                RTS
@word:          LDA     (EP),Y
                INY
                ORA     (EP),Y
                BEQ     @wnext
                INC     T
@wnext:         INY
                CPY     #32
                BNE     @word
                LDA     T
                RTS

; The entry's name listed on CURD? Carry set if so: it matches a
; specification there (none given: any name), or none with EXCLUDE.
NAMEOK:         LDA     NSPEC
                BNE     @specs
                LDA     #1
                BRA     @excl
@specs:         LDA     #<SPECS
                STA     SP
                LDA     #>SPECS
                STA     SP + 1
                LDA     NSPEC
                STA     CNT8
@spec:          JSR     APPLIES
                BCC     @other
                LDA     SP
                LDX     SP + 1
                JSR     UT_SETPAT
                CLC
                LDA     EP
                ADC     #1
                PHA
                LDA     EP + 1
                ADC     #0
                TAX
                PLA
                JSR     UT_MATCH
                BCC     @other
                LDA     #1
                BRA     @excl
@other:         JSR     SPNEXT
                DEC     CNT8
                BNE     @spec
                LDA     #0
@excl:          EOR     FLAGS + O_EXCL
                CMP     #1
                RTS

; The specification at SP is CURD's? Carry set if so: with DRIVE= every
; one is; else its drive's, or the current drive's without one.
APPLIES:        LDA     DRVOPT
                BNE     @yes
                LDA     (SP)
                BEQ     @current
                DEC     A
                BRA     @cmp
@current:       LDA     UT_DRV0
@cmp:           CMP     CURD
                BEQ     @yes
                CLC
                RTS
@yes:           SEC
                RTS

; TUSER and the entry's name: RP its record, found or made (zeros, the user
; and the name). Carry set: no room for another.
FIND:           LDA     #<HEAP
                STA     RP
                LDA     #>HEAP
                STA     RP + 1
                LDA     NREC
                STA     CNT
                LDA     NREC + 1
                STA     CNT + 1
@record:        LDA     CNT
                ORA     CNT + 1
                BEQ     @new
                LDA     (RP)
                CMP     TUSER
                BNE     @skip
                LDY     #1
@char:          LDA     (EP),Y
                AND     #$7F
                STA     T
                LDA     (RP),Y
                AND     #$7F
                CMP     T
                BNE     @skip
                INY
                CPY     #12
                BNE     @char
                CLC
                RTS
@skip:          CLC
                LDA     RP
                ADC     #RECLEN
                STA     RP
                BCC     @count
                INC     RP + 1
@count:         LDA     CNT
                BNE     @low
                DEC     CNT + 1
@low:           DEC     CNT
                BRA     @record
@new:           CLC                    ; a record and its pointer
                LDA     USED
                ADC     #RECLEN + 2
                STA     USED
                LDA     USED + 1
                ADC     #0
                STA     USED + 1
                LDA     ROOM
                CMP     USED
                LDA     ROOM + 1
                SBC     USED + 1
                BCC     @full
                LDY     #RECLEN - 1
                LDA     #0
@zero:          STA     (RP),Y
                DEY
                BPL     @zero
                LDA     TUSER
                STA     (RP)
                LDY     #11
@copy:          LDA     (EP),Y
                STA     (RP),Y
                DEY
                BNE     @copy
                INC     NREC
                BNE     @made
                INC     NREC + 1
@made:          CLC
                RTS
@full:          SEC
                RTS

; A = offset: A/X the address RP + A.
RADDR:          CLC
                ADC     RP
                PHA
                LDA     RP + 1
                ADC     #0
                TAX
                PLA
                RTS

; The pointers to the records at PTRS, after them, sorted by insertion:
; by the user, then the name and type (their bits 7 dropped) or with NOSORT
; the extent-zero entry's number.
SORTP:          LDA     NREC
                STA     PTRS
                LDA     NREC + 1
                STA     PTRS + 1
                LDX     #5             ; x 32
@x32:           ASL     PTRS
                ROL     PTRS + 1
                DEX
                BNE     @x32
                CLC
                LDA     PTRS
                ADC     #<HEAP
                STA     PTRS
                STA     IP
                LDA     PTRS + 1
                ADC     #>HEAP
                STA     PTRS + 1
                STA     IP + 1
                LDA     #<HEAP
                STA     RP
                LDA     #>HEAP
                STA     RP + 1
                LDA     NREC
                STA     CNT
                LDA     NREC + 1
                STA     CNT + 1
@fill:          LDA     CNT
                ORA     CNT + 1
                BEQ     @filled
                LDA     RP
                STA     (IP)
                LDY     #1
                LDA     RP + 1
                STA     (IP),Y
                JSR     IPNEXT
                CLC
                LDA     RP
                ADC     #RECLEN
                STA     RP
                BCC     @fdec
                INC     RP + 1
@fdec:          JSR     CNTDEC
                BRA     @fill
@filled:        LDA     NREC + 1       ; fewer than 2: sorted
                BNE     @sort
                LDA     NREC
                CMP     #2
                BCS     @sort
                RTS
@sort:          LDA     NREC           ; CNT: those after the first
                STA     CNT
                LDA     NREC + 1
                STA     CNT + 1
                JSR     CNTDEC
                CLC
                LDA     PTRS
                ADC     #2
                STA     IP
                LDA     PTRS + 1
                ADC     #0
                STA     IP + 1
@outer:         LDA     (IP)           ; the key
                STA     R2
                LDY     #1
                LDA     (IP),Y
                STA     R2 + 1
                LDA     IP
                STA     JP
                LDA     IP + 1
                STA     JP + 1
@inner:         LDA     JP             ; at the start: its place
                CMP     PTRS
                BNE     @cmp
                LDA     JP + 1
                CMP     PTRS + 1
                BEQ     @place
@cmp:           SEC
                LDA     JP
                SBC     #2
                STA     QP
                LDA     JP + 1
                SBC     #0
                STA     QP + 1
                LDA     (QP)
                STA     R1
                LDY     #1
                LDA     (QP),Y
                STA     R1 + 1
                JSR     CMPR
                BCC     @place         ; the one before isn't greater
                LDA     R1             ; moved up
                STA     (JP)
                LDY     #1
                LDA     R1 + 1
                STA     (JP),Y
                LDA     QP
                STA     JP
                LDA     QP + 1
                STA     JP + 1
                BRA     @inner
@place:         LDA     R2
                STA     (JP)
                LDY     #1
                LDA     R2 + 1
                STA     (JP),Y
                JSR     IPNEXT
                JSR     CNTDEC
                LDA     CNT            ; a ^C every 16 placed: the sort is slow
                AND     #$0F           ; on a big directory
                BNE     @more
                JSR     CM_BRK
@more:          LDA     CNT
                ORA     CNT + 1
                BNE     @outer
                RTS

IPNEXT:         CLC
                LDA     IP
                ADC     #2
                STA     IP
                BCC     @done
                INC     IP + 1
@done:          RTS

CNTDEC:         LDA     CNT
                BNE     @low
                DEC     CNT + 1
@low:           DEC     CNT
                RTS

; R1's record after R2's? Carry set if so.
CMPR:           LDA     (R1)
                CMP     (R2)
                BNE     @done
                LDA     FLAGS + O_NOSORT
                BNE     @entry
                LDY     #R_NAME
@char:          LDA     (R2),Y
                AND     #$7F
                STA     T
                LDA     (R1),Y
                AND     #$7F
                CMP     T
                BNE     @done
                INY
                CPY     #R_NAME + 11
                BNE     @char
                CLC
                RTS
@entry:         LDY     #R_ENT + 1
                LDA     (R1),Y
                CMP     (R2),Y
                BNE     @done
                DEY
                LDA     (R1),Y
                CMP     (R2),Y
                BNE     @done
                CLC
                RTS
@done:          RTS                    ; not equal: carry set if greater

; Each pointer in turn: IP and CNT set, RP the record. Carry set at the end.
FIRSTP:         LDA     PTRS
                STA     IP
                LDA     PTRS + 1
                STA     IP + 1
                LDA     NREC
                STA     CNT
                LDA     NREC + 1
                STA     CNT + 1
THISP:          LDA     CNT
                ORA     CNT + 1
                BEQ     @end
                LDA     (IP)
                STA     RP
                LDY     #1
                LDA     (IP),Y
                STA     RP + 1
                CLC
                RTS
@end:           SEC
                RTS
NEXTP:          JSR     IPNEXT
                JSR     CNTDEC
                BRA     THISP

; ---- the listings --------------------------------------------------------

; The built-in's form: the current user's files without SYS, in columns,
; then No File or SYSTEM FILE(S) EXIST.
SHORTL:         STZ     FOUND
                STZ     OTHER
                STZ     COL
                JSR     FIRSTP
@file:          BCS     @end
                LDA     (RP)
                CMP     CURUSR
                BNE     @next
                LDY     #R_FLAGS
                LDA     (RP),Y
                LSR     A
                BCC     @next
                LDY     #R_NAME + 9    ; t2': SYS
                LDA     (RP),Y
                BPL     @dir
                LDA     #1
                STA     OTHER
                BRA     @next
@dir:           LDA     #1
                STA     FOUND
                JSR     ITEM
@next:          JSR     NEXTP
                BRA     @file
@end:           LDA     COL
                BEQ     @said
                JSR     EOL
@said:          LDA     FOUND
                ORA     OTHER
                BNE     @some
                LDA     #1
                STA     FAILED
                SAYS    S_NOFILE
                RTS
@some:          LDA     OTHER
                BEQ     @done
                SAYS    S_SYSEX
@done:          RTS

; A user's group: its heading, its files, its totals; with MESSAGE, the
; heading and No File when it has none.
GROUP:          STZ     GOPEN
                STZ     COL
                LDX     #TOTEND - TOTALS - 1
@zero:          STZ     TOTALS,X
                DEX
                BPL     @zero
                JSR     FIRSTP
@file:          BCS     @end
                JSR     LISTED
                BCC     @next
                LDA     GOPEN
                BNE     @open
                INC     GOPEN
                JSR     OPENHDR
                LDA     FORMAT
                CMP     #FM_SIZE
                BEQ     @open
                JSR     TABHDR
@open:          JSR     ONEFILE
@next:          JSR     NEXTP
                BRA     @file
@end:           LDA     GOPEN
                BEQ     @none
                LDA     #1
                STA     FOUNDANY
                JMP     CLOSEGRP
@none:          LDA     FLAGS + O_MSG
                BEQ     @done
                JSR     OPENHDR
                LDA     #1
                STA     FAILED
                SAYS    S_NOFILE
@done:          RTS

; RP's record in GU's group? Carry set if so: the user's, a file (its
; extent-zero entry seen), the attributes asked for.
LISTED:         LDA     (RP)
                CMP     GU
                BNE     @no
                LDY     #R_FLAGS
                LDA     (RP),Y
                LSR     A
                BCC     @no
                LDY     #R_NAME + 9    ; t2': SYS
                LDA     (RP),Y
                AND     #$80
                STA     T
                LDA     SYSF
                BEQ     @ro
                CMP     #1
                BNE     @sysonly
                LDA     T              ; DIR only
                BNE     @no
                BRA     @ro
@sysonly:       LDA     T
                BEQ     @no
@ro:            LDY     #R_NAME + 8    ; t1': RO
                LDA     (RP),Y
                AND     #$80
                STA     T
                LDA     ROF
                BEQ     @yes
                CMP     #1
                BNE     @rwonly
                LDA     T              ; RO only
                BEQ     @no
                BRA     @yes
@rwonly:        LDA     T
                BNE     @no
@yes:           SEC
                RTS
@no:            CLC
                RTS

; A line before the group's heading if anything is written already.
SEPAR:          LDA     FORMAT
                CMP     #FM_SHORT
                BEQ     @done
                LDA     ANYOUT
                BEQ     @first
                JSR     BLANK
@first:         LDA     #1
                STA     ANYOUT
@done:          RTS

; "Directory For Drive A:  User  0" and a blank line.
OPENHDR:        JSR     SEPAR
                PUTS    S_DIRFOR
                LDA     CURD
                JSR     UT_PUTDRV
                PUTS    S_USER
                LDA     GU
                JSR     PUTA3
                JSR     EOL
                JMP     BLANK

; FULL's or DATE's column headings, dashes and a blank line.
TABHDR:         LDA     FORMAT
                CMP     #FM_DATE
                BEQ     @date
                PUTS    S_FHEAD
                JSR     PUTOTHER
                JSR     EOL
                PUTS    S_FDASH
                BRA     @end
@date:          PUTS    S_DHEAD
                JSR     PUTOTHER
                JSR     EOL
                PUTS    S_DDASH
@end:           JSR     EOL
                JSR     BLANK
                LDA     #1             ; (EOL clears it: a line since)
                STA     HDRJUST
                RTS

; The last column's heading: Access when the label asks for access stamps,
; else Create.
PUTOTHER:       LDA     LBLMODE
                AND     #$40
                BEQ     @create
                PUTS    S_ACCESS
                RTS
@create:        PUTS    S_CREATE
                RTS

; A file of the group: counted in the totals, then its line or item.
ONEFILE:        LDY     #R_BLKS        ; kilobytes: blocks << BSHIFT
                LDA     (RP),Y
                STA     UT_N
                INY
                LDA     (RP),Y
                STA     UT_N + 1
                STZ     UT_N + 2
                LDA     BSHIFT
                JSR     UT_SHLN
                LDA     #<KB
                LDX     #>KB
                JSR     UT_STN
                LDA     #<TKB
                LDX     #>TKB
                JSR     ADDTO
                LDA     #R_RECS
                JSR     RADDR
                JSR     UT_LDN
                LDA     #<TRECS
                LDX     #>TRECS
                JSR     ADDTO
                LDA     #R_RECS        ; 1 KB blocks: records / 8, rounded up
                JSR     RADDR
                JSR     UT_LDN
                JSR     UT_DIV8
                LDA     #<TBLK
                LDX     #>TBLK
                JSR     ADDTO
                INC     TFILES
                BNE     @kind
                INC     TFILES + 1
@kind:          LDA     FORMAT
                CMP     #FM_SIZE
                BNE     @line
                JMP     ITEM
@line:          LDA     DUE            ; LENGTH's lines since the heading: again
                BEQ     @name
                JSR     CM_FLUSH       ; (a stop, a form feed at the new line's start)
                LDA     FLAGS + O_FF
                BEQ     @noff
                LDA     ECHO
                BEQ     @noff
                JSR     LSTFF          ; a new sheet: the heading on it
                BRA     @head
@noff:          LDA     HDRJUST        ; due on the heading's own last line: not
                BEQ     @head          ; again, unless a stop just began a page
                LDA     CM_PLEN
                BEQ     @skip
                LDA     CM_LINES
                BEQ     @head
@skip:          STZ     DUE
                BRA     @name
@head:          JSR     TABHDR
@name:          JSR     PUTNAME
                LDA     FORMAT
                CMP     #FM_DATE
                BEQ     @stamps
                JSR     BLANK1
                JSR     PUTKB
                JSR     BLANK1
                LDA     #R_RECS
                JSR     RADDR
                JSR     UT_LDN
                LDA     #6
                JSR     PUTN
                JSR     BLANK1
                JSR     PUTATTR
                JSR     BLANK1
                JSR     PUTPROT
                LDA     HASLBL         ; FULL's stamps: only with a label (the Guide)
                BNE     @stamps
                LDA     #31
                JSR     UT_BLANKS
                JMP     EOL
@stamps:        JSR     BLANK1
                LDA     #R_STU
                JSR     RADDR
                JSR     UT_PUTSTAMP
                JSR     BLANK1
                JSR     BLANK1
                LDA     #R_STA
                JSR     RADDR
                JSR     UT_PUTSTAMP
                JMP     EOL

; The 3 bytes at A/X += UT_N.
ADDTO:          PHA
                PHX
                JSR     UT_ADDN
                PLX
                PLA
                JMP     UT_STN

; An item of a line of columns (the built-in's, or SIZE's with the
; kilobytes): the drive first, " : " between, a line ended when full.
ITEM:           LDA     COL
                BNE     @sep
                LDA     CURD
                JSR     UT_PUTDRV
                JSR     BLANK1
                BRA     @name
@sep:           PUTS    S_SEP
@name:          JSR     PUTNAME
                LDA     FORMAT
                CMP     #FM_SIZE
                BNE     @count
                JSR     PUTKB
@count:         INC     COL
                LDA     COL
                CMP     NCOL
                BNE     @done
                STZ     COL
                JMP     EOL
@done:          RTS

PUTNAME:        LDA     #R_NAME
                JSR     RADDR
                JMP     CM_DIRNM

; KB in 5 and k.
PUTKB:          LDA     #<KB
                LDX     #>KB
                JSR     UT_LDN
                LDA     #5
                JSR     PUTN
                LDA     #'k'
                JMP     CM_PUTC

; Dir or Sys, RW or RO, A when archived, then F1-F4 as 1-4 with ATT: 12.
PUTATTR:        LDY     #R_NAME + 9
                LDA     (RP),Y
                BMI     @sys
                PUTS    S_DIRA
                BRA     @ro
@sys:           PUTS    S_SYSA
@ro:            LDY     #R_NAME + 8
                LDA     (RP),Y
                BMI     @rdonly
                PUTS    S_RW
                BRA     @arch
@rdonly:        PUTS    S_RO
@arch:          LDY     #R_NAME + 10
                LDA     (RP),Y
                ASL     A
                LDA     #' '
                BCC     @a
                LDA     #'A'
@a:             JSR     CM_PUTC
                LDA     FLAGS + O_ATT
                BNE     @att
                LDA     #4
                JMP     UT_BLANKS
@att:           LDY     #R_NAME
@f:             LDA     (RP),Y
                ASL     A
                LDA     #' '
                BCC     @fput
                TYA
                ORA     #'0'
@fput:          JSR     CM_PUTC
                INY
                CPY     #R_NAME + 4
                BNE     @f
                RTS

; The password mode from the XFCB, 6: blank when the drive has no label.
PUTPROT:        LDA     HASLBL
                BNE     @label
                LDA     #6
                JMP     UT_BLANKS
@label:         LDY     #R_FLAGS
                LDA     (RP),Y
                AND     #RF_XFCB
                BEQ     @none
                LDY     #R_XMODE
                LDA     (RP),Y
                ASL     A
                BCS     @read
                ASL     A
                BCS     @write
                ASL     A
                BCS     @delete
@none:          PUTS    S_NONE
                RTS
@read:          PUTS    S_READ
                RTS
@write:         PUTS    S_WRITE
                RTS
@delete:        PUTS    S_DELETE
                RTS

; The group's totals, after a blank line.
CLOSEGRP:       LDA     COL            ; SIZE's last line
                BEQ     @totals
                JSR     EOL
@totals:        JSR     BLANK
                PUTS    S_TBYTES
                LDA     #<TKB
                LDX     #>TKB
                JSR     UT_LDN
                LDA     #6
                JSR     PUTN
                PUTS    S_TRECS
                LDA     #<TRECS
                LDX     #>TRECS
                JSR     UT_LDN
                LDA     #7
                JSR     PUTN
                PUTS    S_TFILES
                LDA     TFILES
                LDX     TFILES + 1
                LDY     #4
                JSR     PUTAX
                JSR     EOL
                PUTS    S_TBLKS
                LDA     #<TBLK
                LDX     #>TBLK
                JSR     UT_LDN
                LDA     #6
                JSR     PUTN
                PUTS    S_TUSED
                LDA     CURD
                JSR     UT_PUTDRV
                SEC                    ; used: not free, not SFCBs
                LDA     NENT
                SBC     NFREE
                PHA
                LDA     NENT + 1
                SBC     NFREE + 1
                TAX
                PLA
                SEC
                SBC     NSFCB
                PHA
                TXA
                SBC     NSFCB + 1
                TAX
                PLA
                LDY     #5
                JSR     PUTAX
                LDA     #'/'
                JSR     CM_PUTC
                SEC                    ; of all but the SFCBs
                LDA     NENT
                SBC     NSFCB
                PHA
                LDA     NENT + 1
                SBC     NSFCB + 1
                TAX
                PLA
                LDY     #4
                JSR     PUTAX
                JMP     EOL

; ---- writing -------------------------------------------------------------

; UT_N in a field of A, no commas.
PUTN:           CLC
                JMP     UT_NOUT

; A/X in a field of Y.
PUTAX:          STA     UT_N
                STX     UT_N + 1
                STZ     UT_N + 2
                TYA
                BRA     PUTN

; A in a field of 3.
PUTA3:          LDX     #0
                LDY     #3
                BRA     PUTAX

BLANK1:         LDA     #' '
                JMP     CM_PUTC

; A line ended (owed: CM_EOL) and counted: at LENGTH's count a heading is
; due, written before the next file line (and not if another line comes
; first), so with paging's default the heading starts each page.
EOL:            JSR     CM_EOL
                STZ     DUE
                STZ     HDRJUST
                INC     LCOUNT
                BNE     @len
                INC     LCOUNT + 1
@len:           LDA     LENGTH
                ORA     LENGTH + 1
                BEQ     @done
                LDA     LCOUNT
                CMP     LENGTH
                BNE     @done
                LDA     LCOUNT + 1
                CMP     LENGTH + 1
                BNE     @done
                STZ     LCOUNT
                STZ     LCOUNT + 1
                LDA     #1
                STA     DUE
@done:          RTS

; An empty line: the one owed ended, another owed.
BLANK:          JSR     CM_FLUSH
                JMP     EOL

; The '$' string at A/X, then the line ended.
SAYLN:          JSR     CM_PUTS
                JMP     EOL

.rodata
; The most bytes the records and their pointers take: all the TPA has. A
; test makes it less, to see Not Enough Memory for Sort. (it patches the
; .COM at this label, from dir.lbl).
DIRROOM:        .word   $FFFF
OPTAB:          .byte   "ATT", 0, "DATE", 0, "DIR", 0, "DISK", 0, "DRIVE", 0
                .byte   "EXCLUDE", 0, "FF", 0, "FULL", 0, "LENGTH", 0, "MESSAGE", 0
                .byte   "NOPAGE", 0, "NOSORT", 0, "RO", 0, "RW", 0, "SIZE", 0
                .byte   "SYS", 0, "USER", 0, 0
BITS:           .byte   $01, $02, $04, $08, $10, $20, $40, $80
SLOTS:          .byte   97, 107, 117   ; an SFCB's slots in its record (UT_REC + 96)
S_ALL:          .byte   "ALL"
S_GROUP:        .byte   "Options not grouped together.$"
S_TAIL:         .byte   "Illegal command tail.$"
S_OPT:          .byte   "Illegal Option or Modifier.$"
S_FORMAT:       .byte   "Illegal Format Value.$"
S_MIX:          .byte   "Illegal Global/Local Drive Spec Mixing.$"
S_NOMEM:        .byte   "Not Enough Memory for Sort.$"
S_INACT:        .byte   "Date and Time Stamping Inactive$"
S_NOFILE:       .byte   "No File$"
S_SYSEX:        .byte   "SYSTEM FILE(S) EXIST$"
S_DIRFOR:       .byte   "Directory For Drive $"
S_USER:         .byte   "  User$"
S_FHEAD:        .byte   "    Name     Bytes   Recs   Attributes   Prot      Update          $"
S_FDASH:        .byte   "------------ ------ ------ ------------ ------ --------------  --------------$"
S_DHEAD:        .byte   "    Name         Update          $"
S_DDASH:        .byte   "------------ --------------  --------------$"
S_CREATE:       .byte   "Create$"
S_ACCESS:       .byte   "Access$"
S_SEP:          .byte   " : $"
S_DIRA:         .byte   "Dir $"
S_SYSA:         .byte   "Sys $"
S_RW:           .byte   "RW $"
S_RO:           .byte   "RO $"
S_READ:         .byte   "Read  $"
S_WRITE:        .byte   "Write $"
S_DELETE:       .byte   "Delete$"
S_NONE:         .byte   "None  $"
S_TBYTES:       .byte   "Total Bytes     = $"
S_TRECS:        .byte   "k  Total Records = $"
S_TFILES:       .byte   "  Files Found = $"
S_TBLKS:        .byte   "Total 1k Blocks = $"
S_TUSED:        .byte   "   Used/Max Dir Entries For Drive $"

.code
        .include "cmd.inc"
        .include "util.inc"

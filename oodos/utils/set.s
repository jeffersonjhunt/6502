; SET.COM (hw/oodos.md, "The utilities, phase 4b": SET; the CP/M 3 User's
; Guide, 5-86 to 5-94): a file's attributes, password and protection mode;
; a drive read-only or read-write; the drive's label, its password and
; protection, its stamp modes; the default password.
;
;   SET filespec [options]   DIR SYS RO RW ARCHIVE= F1= to F4= (30),
;                            PASSWORD= PROTECT=READ|WRITE|DELETE|NONE (103)
;   SET {d:} [options]       RO (28) RW (37), NAME= PASSWORD= PROTECT=ON|OFF
;                            CREATE= ACCESS= UPDATE= (100, one a label
;                            option), DEFAULT= (106)
;
; Everything is checked before anything is changed; then a drive's are done
; in the order DEFAULT, RW, the label's options as given, RO (ours), and a
; file's by 30, then 103, each file in the directory's order. The BDOS says
; its own errors (error mode $FE).

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

O_DIR           = 0                    ; OPTAB's order
O_SYS           = 1
O_RO            = 2
O_RW            = 3
O_ARC           = 4
O_F1            = 5
O_F4            = 8
O_PW            = 9
O_PROT          = 10
O_NAME          = 11
O_CREATE        = 12
O_ACCESS        = 13
O_UPDATE        = 14
O_DEF           = 15
NOPTS           = 16

K_NONE          = 0                    ; KINDS: an option's value
K_ONOFF         = 1
K_PW            = 2
K_PROT          = 3
K_NAME          = 4

V_READ          = 0                    ; PROTECT's values (PROTW's order)
V_WRITE         = 1
V_DELETE        = 2
V_NONE          = 3
V_ON            = 4
V_OFF           = 5

L_PW            = $80                  ; the label's byte 12
L_ACCESS        = $40
L_UPDATE        = $20
L_CREATE        = $10

REC_LEN         = 13                   ; a file found: its 11 name bytes, flags, mode
R_FLAGS         = 11
R_MODE          = 12
RF_XFCB         = 1
RF_PW           = 2

.zeropage
P:              .res    2              ; a file's record
Q:              .res    2              ; a directory entry

.code

START:          JSR     UT_INIT
                LDX     #NOPTS - 1
@clear:         STZ     GIVEN,X
                DEX
                BPL     @clear
                STZ     NLORD
                STZ     ISFILE
                LDA     #<FCB          ; one specification at most
                LDX     #>FCB
                JSR     UT_SPEC
                BCC     @spec
                CMP     #0
                BEQ     @nospec
@badspec:       LDA     #<SBADSPEC
                LDX     #>SBADSPEC
                JMP     FAIL
@nospec:        STZ     FCB
                BRA     @opts
@spec:          LDA     #<FCB2
                LDX     #>FCB2
                JSR     UT_SPEC
                BCC     @badspec
                CMP     #0
                BNE     @badspec
                LDX     #11            ; a name, or a drive alone
@name:          LDA     FCB,X
                CMP     #' '
                BNE     @file
                DEX
                BNE     @name
                BRA     @opts
@file:          INC     ISFILE
@opts:          JSR     OPTIONS
                JSR     CHECKS
                LDA     FCB            ; the drive: the specification's, or the current
                BEQ     @cur
                DEC     A
                BRA     @drv
@cur:           LDA     UT_DRV0
@drv:           STA     DRV
                JSR     UT_SELECT
                CMP     #0
                BEQ     @sel
                JMP     QUIT           ; the BDOS said why
@sel:           LDA     ISFILE
                BEQ     @drive
                JMP     FILES
@drive:         JMP     DRIVE

; ---- the option list -----------------------------------------------------

OPTIONS:        JSR     UT_OPTS
                CMP     #UT_LNONE
                BEQ     @noopts
                CMP     #UT_LONE
                BNE     @bad
                STZ     NGIVEN
@next:          LDA     #<OPTAB
                LDX     #>OPTAB
                JSR     UT_OPT
                BCC     @opt
                CMP     #UT_OEND
                BNE     @bad
                LDA     NGIVEN
                BEQ     @noopts
                RTS
@noopts:        LDA     #<SNOOPTS
                LDX     #>SNOOPTS
                JMP     FAIL
@bad:           JMP     BADOPT
@opt:           STA     CUR
                TAX
                INC     NGIVEN
                LDA     GIVEN,X        ; a label option, in the order first given
                BNE     @given
                CPX     #O_PW
                BCC     @given
                CPX     #O_DEF
                BCS     @given
                LDY     NLORD
                TXA
                STA     LORD,Y
                INC     NLORD
@given:         LDA     #1
                STA     GIVEN,X
                LDA     KINDS,X
                BNE     @value
                LDA     UT_HASV        ; DIR, SYS, RO, RW: no value
                BNE     @bad
                BRA     @next
@value:         STA     KIND
                JSR     UT_VALUE       ; a word
                BCS     @bad
                CMP     #0
                BNE     @bad
                JSR     UT_VITEM
                BCS     @bad
                LDA     KIND
                CMP     #K_ONOFF
                BNE     @pw
                JSR     UT_WONOFF
                BCS     @bad
@val:           LDX     CUR
                STA     VAL,X
                BRA     @next
@pw:            CMP     #K_PW
                BNE     @prot
                LDA     UT_WLEN        ; 8 characters at most, blanks after
                CMP     #9
                BCS     @bad
                LDA     #<PWNEW
                LDX     #>PWNEW
                LDY     CUR
                CPY     #O_PW
                BEQ     @pwto
                LDA     #<DEFPW
                LDX     #>DEFPW
@pwto:          STA     Q
                STX     Q + 1
                LDY     #0
@pwc:           LDA     #' '
                CPY     UT_WLEN
                BCS     @dst
                LDA     UT_WORD,Y
@dst:           STA     (Q),Y
                INY
                CPY     #8
                BNE     @pwc
                LDA     UT_WLEN
                BRA     @val
@prot:          CMP     #K_PROT
                BNE     @lname
                LDA     #<PROTW
                LDX     #>PROTW
                JSR     WORDIS
                BCS     @bad2
                BRA     @val
@lname:         JSR     NAMEVAL        ; K_NAME
                BCS     @bad2
                JMP     @next
@bad2:          JMP     BADOPT

BADOPT:         LDA     #<SBADOPT
                LDX     #>SBADOPT
                JMP     FAIL

; UT_WORD one of the table at A/X's words (each ended by a 0, the table by
; another): carry clear, A its index; carry set if none.
WORDIS:         STA     @tab + 1
                STX     @tab + 2
                LDX     #0             ; the table's index
                STZ     T
@word:          LDY     #0
@char:          JSR     @tab
                CMP     UT_WORD,Y
                BNE     @skip
                CMP     #0
                BEQ     @found
                INY
                BRA     @char
@skip:          JSR     @tab           ; to the next word
                BEQ     @end
                INY
                BRA     @skip
@end:           INY
                LDA     @tab + 1
                PHA
                TYA
                CLC
                ADC     @tab + 1
                STA     @tab + 1
                BCC     @on
                INC     @tab + 2
@on:            PLA
                INC     T
                LDY     #0
                JSR     @tab
                BNE     @word
                SEC
                RTS
@found:         LDA     T
                CLC
                RTS
@tab:           LDA     $FFFF,Y
                RTS

; UT_WORD as a label's name, NAME.TYP, into LNEW by 152: carry set if it
; isn't one (a drive, a wildcard, a password, or nothing).
NAMEVAL:        LDA     #<UT_WORD
                STA     PARSE
                LDA     #>UT_WORD
                STA     PARSE + 1
                LDA     #<NFCB
                STA     PARSE + 2
                LDA     #>NFCB
                STA     PARSE + 3
                LDA     #<PARSE
                LDX     #>PARSE
                LDY     #F_PARSE
                JSR     BDOS
                CMP     #0             ; the word's end
                BNE     @bad
                CPX     #0
                BNE     @bad
                LDA     NFCB           ; no drive
                BNE     @bad
                LDA     NFCB + 1
                CMP     #' '
                BEQ     @bad
                LDX     #0
@name:          LDA     NFCB + 1,X
                CMP     #'?'
                BEQ     @bad
                STA     LNEW,X
                INX
                CPX     #11
                BNE     @name
                LDX     #7
@pw:            LDA     NFCB + 16,X
                CMP     #' '
                BNE     @bad
                DEX
                BPL     @pw
                CLC
                RTS
@bad:           SEC
                RTS

; ---- what goes with what -------------------------------------------------

CHECKS:         LDA     GIVEN + O_RO
                AND     GIVEN + O_RW
                BEQ     @sysdir
                LDA     #<SROW
                LDX     #>SROW
                JMP     FAIL
@sysdir:        LDA     GIVEN + O_SYS
                AND     GIVEN + O_DIR
                BEQ     @stamps
                LDA     #<SSYSDIR
                LDX     #>SSYSDIR
                JMP     FAIL
@stamps:        LDA     GIVEN + O_CREATE
                AND     VAL + O_CREATE
                AND     GIVEN + O_ACCESS
                AND     VAL + O_ACCESS
                BEQ     @kind
                LDA     #<SBOTHST
                LDX     #>SBOTHST
                JMP     FAIL
@kind:          JSR     PROTKIND       ; X 1 a file's PROTECT, 2 a drive's, 0 none
                LDA     ISFILE
                BEQ     @drive
                LDA     GIVEN + O_NAME
                BEQ     @fonly
                LDA     #<SLABFILE
                LDX     #>SLABFILE
                JMP     FAIL
@fonly:         CPX     #2
                BEQ     @dronly
                LDA     GIVEN + O_CREATE
                ORA     GIVEN + O_ACCESS
                ORA     GIVEN + O_UPDATE
                ORA     GIVEN + O_DEF
                BEQ     @ok
@dronly:        LDA     #<SDRVONLY
                LDX     #>SDRVONLY
                JMP     FAIL
@drive:         CPX     #1
                BEQ     @needf
                LDY     #O_F4          ; DIR, SYS, ARCHIVE, F1-F4
@att:           LDA     GIVEN,Y
                BNE     @needf
                DEY
                CPY     #O_ARC
                BCS     @att
                LDA     GIVEN + O_DIR
                ORA     GIVEN + O_SYS
                BEQ     @ok
@needf:         LDA     #<SNEEDF
                LDX     #>SNEEDF
                JMP     FAIL
@ok:            RTS

PROTKIND:       LDX     #0
                LDA     GIVEN + O_PROT
                BEQ     @done
                INX
                LDA     VAL + O_PROT
                CMP     #V_ON
                BCC     @done
                INX
@done:          RTS

; ---- a drive's options ---------------------------------------------------

DRIVE:          LDA     NLORD          ; the label's checks (they may fail) first
                BEQ     @def
                JSR     LABEL
@def:           LDA     GIVEN + O_DEF
                BEQ     @rw
                LDA     #<DEFPW
                LDX     #>DEFPW
                LDY     #F_PASSWD
                JSR     BDOS
@rw:            LDA     GIVEN + O_RW
                BEQ     @lops
                LDA     #1             ; the drive's bit
                STA     T
                STZ     T + 1
                LDX     DRV
                BEQ     @reset
@bit:           ASL     T
                ROL     T + 1
                DEX
                BNE     @bit
@reset:         LDA     T
                LDX     T + 1
                LDY     #DRV_RESET
                JSR     BDOS
                CMP     #0
                BEQ     @rwok
                JMP     QUIT
@rwok:          JSR     PUTDRV
                LDA     #<SSETRW
                LDX     #>SSETRW
                JSR     UT_SAY
@lops:          LDX     #0
@lop:           CPX     NLORD
                BEQ     @ro
                PHX
                LDA     LORD,X
                JSR     LABOP
                PLX
                INX
                BRA     @lop
@ro:            LDA     GIVEN + O_RO
                BEQ     @done
                LDA     DRV            ; current for 28, then the one before (UT_END)
                JSR     UT_SELECT
                CMP     #0
                BNE     QUIT
                LDY     #DRV_SETRO
                JSR     BDOS
                CMP     #0
                BNE     QUIT
                JSR     PUTDRV
                LDA     #<SSETRO
                LDX     #>SSETRO
                JSR     UT_SAY
@done:          JMP     UT_EXIT

QUIT:           JSR     UT_FAIL
                JMP     UT_EXIT

; The label as it is (or a new one's: LABEL, no modes, no password); stamps
; asked for need SFCBs, and without them the label's own stamp bits are
; dropped (100 refuses them there: a label made elsewhere); its password
; asked for if it has one.
LABEL:          LDA     GIVEN + O_CREATE
                AND     VAL + O_CREATE
                STA     T
                LDA     GIVEN + O_ACCESS
                AND     VAL + O_ACCESS
                ORA     T
                STA     T
                LDA     GIVEN + O_UPDATE
                AND     VAL + O_UPDATE
                ORA     T
                STA     T
                JSR     UT_SFCBS
                BCS     QUIT
                STA     HASSF
                BNE     @find
                LDA     T
                BEQ     @find
                LDA     #<SREFORM
                LDX     #>SREFORM
                JMP     FAIL
@find:          LDX     #7             ; no password but for the label's own
@blank:         LDA     #' '
                STA     LPW,X
                DEX
                BPL     @blank
                JSR     UT_LABEL
                BCC     @have
                CPX     #0
                BNE     QUIT           ; the scan failed: said
                LDX     #10
@deflt:         LDA     SLABDEF,X
                STA     LNAME,X
                DEX
                BPL     @deflt
                STZ     LMODE
                RTS
@have:          LDX     #10
@copy:          LDA     UT_LBL + 1,X
                STA     LNAME,X
                DEX
                BPL     @copy
                LDA     UT_LBL + 12
                AND     #$F0
                LDX     HASSF
                BNE     @mode
                AND     #L_PW
@mode:          STA     LMODE
                LDA     #<UT_LBL
                LDX     #>UT_LBL
                JSR     UT_PWDEC
                JSR     PWNONE
                BCC     @done
                LDA     #<SPWQ         ; the Guide: SET always asks for it
                LDX     #>SPWQ
                JSR     CM_PUTS
                JSR     CM_READ
                LDA     CM_ANS + 1
                CMP     #9
                BCS     @wrong
                LDX     #0
@typed:         LDA     #' '
                CPX     CM_ANS + 1
                BCS     @put
                LDA     CM_ANS + 2,X
                CMP     #'a'
                BCC     @put
                CMP     #'z' + 1
                BCS     @put
                AND     #$DF
@put:           STA     LPW,X
                CMP     UT_PW,X
                BNE     @wrong
                INX
                CPX     #8
                BNE     @typed
@done:          RTS
@wrong:         LDA     #<SWRONG
                LDX     #>SWRONG
                JMP     FAIL

; UT_PW none (8 blanks or 8 zero bytes): carry clear; carry set if not.
PWNONE:         LDX     #7
                LDA     UT_PW
                BEQ     @zero
@blank:         LDA     UT_PW,X
                CMP     #' '
                BNE     @has
                DEX
                BPL     @blank
                CLC
                RTS
@zero:          LDA     UT_PW,X
                BNE     @has
                DEX
                BPL     @zero
                CLC
                RTS
@has:           SEC
                RTS

; Label option A: the label changed by one 100, then its line.
LABOP:          STA     CUR
                STZ     NEWPW
                CMP     #O_NAME
                BNE     @pw
                LDX     #10
@name:          LDA     LNEW,X
                STA     LNAME,X
                DEX
                BPL     @name
                BRA     @put
@pw:            CMP     #O_PW
                BNE     @prot
                INC     NEWPW
                BRA     @put
@prot:          CMP     #O_PROT
                BNE     @create
                LDA     #L_PW
                LDX     VAL + O_PROT
                CPX     #V_ON
                BRA     @bit
@create:        CMP     #O_CREATE
                BNE     @access
                LDA     VAL + O_CREATE
                BEQ     @cloff
                LDA     #L_ACCESS      ; CREATE=ON clears ACCESS (the Guide)
                TRB     LMODE
@cloff:         LDA     #L_CREATE
                LDX     VAL + O_CREATE
                CPX     #1
                BRA     @bit
@access:        CMP     #O_ACCESS
                BNE     @update
                LDA     VAL + O_ACCESS
                BEQ     @aoff
                LDA     #L_CREATE      ; and ACCESS=ON CREATE
                TRB     LMODE
@aoff:          LDA     #L_ACCESS
                LDX     VAL + O_ACCESS
                CPX     #1
                BRA     @bit
@update:        LDA     #L_UPDATE
                LDX     VAL + O_UPDATE
                CPX     #1
@bit:           BEQ     @set           ; Z: on
                TRB     LMODE
                BRA     @put
@set:           TSB     LMODE
@put:           LDA     DRV            ; the FCB: the drive, the name, the modes
                INC     A
                STA     WFCB
                LDX     #10
@lname:         LDA     LNAME,X
                STA     WFCB + 1,X
                DEX
                BPL     @lname
                LDA     LMODE
                ORA     NEWPW          ; bit 0: a new password
                STA     WFCB + 12
                JSR     WZERO
                LDX     #7             ; the DMA buffer: the password, a new one
@pwb:           LDA     LPW,X
                STA     PWBUF,X
                LDA     PWNEW,X
                STA     PWBUF + 8,X
                DEX
                BPL     @pwb
                JSR     SETDMA
                LDA     #<WFCB
                LDX     #>WFCB
                LDY     #DRV_SETLABEL
                JSR     BDOS
                JSR     BDOSOK
                LDA     NEWPW          ; the label's password from now on
                BEQ     @line
                LDX     #7
@newpw:         LDA     PWNEW,X
                STA     LPW,X
                DEX
                BPL     @newpw
@line:          JSR     PUTDRV
                LDA     CUR
                CMP     #O_NAME
                BNE     @lpw
                LDA     #<SLABEQ
                LDX     #>SLABEQ
                JSR     CM_PUTS
                LDA     #<LNAME
                LDX     #>LNAME
                JSR     CM_FNAME
                JMP     CM_EOL
@lpw:           CMP     #O_PW
                BNE     @lprot
                LDA     #<SLPWSET
                LDX     #>SLPWSET
                LDY     VAL + O_PW     ; its length: 0 none
                BNE     @say
                LDA     #<SLPWRM
                LDX     #>SLPWRM
@say:           JMP     UT_SAY
@lprot:         LDY     #0             ; "Password Protection", "Create", ...
@which:         LDA     LOPT,Y
                CMP     CUR
                BEQ     @named
                INY
                INY
                INY
                BRA     @which
@named:         LDA     LOPT + 1,Y
                LDX     LOPT + 2,Y
                JSR     CM_PUTS
                LDX     CUR
                LDA     VAL,X
                CPX     #O_PROT
                BNE     @onoff
                EOR     #V_OFF         ; V_ON 4: 1, V_OFF 5: 0
                AND     #1
@onoff:         JMP     PUTON

; ---- a file's options ----------------------------------------------------

FILES:          LDA     GIVEN + O_PW
                ORA     GIVEN + O_PROT
                STA     PWOPS
                LDX     #O_F4
                STZ     ATTOPS
@att:           LDA     GIVEN,X
                TSB     ATTOPS
                DEX
                BPL     @att
                LDA     PWOPS
                BEQ     @scan
                LDA     DRV            ; passwords on (the label's bit 7)?
                LDY     #DRV_GETLABEL
                JSR     BDOS
                CMP     #$FF
                BNE     @on
                CPX     #0
                BNE     QUIT2
@on:            AND     #L_PW
                BNE     @scan
                LDA     #<SPWOFF
                LDX     #>SPWOFF
                JMP     FAIL
@scan:          JSR     FIND
                LDA     NFILES
                ORA     NFILES + 1
                BNE     @found
                LDA     #<SNOFILE
                LDX     #>SNOFILE
                JMP     FAIL
@found:         LDA     PWOPS
                BEQ     @do
                JSR     XFCBS
                JSR     ASSIGN
@do:            JSR     CM_PAGE
                JSR     FIRST
@file:          BCS     @done
                LDA     ATTOPS
                BEQ     @pws
                JSR     ATTRIB
                BCS     @next          ; refused, said: on to the next file
@pws:           LDA     PWOPS
                BEQ     @next
                JSR     PROTECT
@next:          JSR     NEXT
                BRA     @file
@done:          JMP     UT_EXIT

QUIT2:          JMP     QUIT

; The matching files of the current user, each its extent-zero entry, into
; records from UT_MLO: NFILES of them.
FIND:           LDY     #F_USERNUM
                LDA     #$FF
                JSR     BDOS
                STA     USER
                JSR     UT_DPB
                LDA     UT_DPBC + 4    ; EXM: an extent-zero entry's EX has
                EOR     #$1F           ; none of the other bits
                AND     #$1F
                STA     EXMASK
                JSR     UT_MEM
                LDA     UT_MLO
                STA     P
                LDA     UT_MLO + 1
                STA     P + 1
                STZ     NFILES
                STZ     NFILES + 1
                LDA     #<FCB
                LDX     #>FCB
                JSR     UT_SETPAT
                JSR     UT_SCAN
@entry:         BCC     @look
                CPX     #0
                BNE     QUIT2          ; the scan failed: said
                RTS
@look:          STA     Q
                STX     Q + 1
                LDA     (Q)
                CMP     USER
                BNE     @next
                LDY     #12
                LDA     (Q),Y
                AND     EXMASK
                BNE     @next
                LDY     #14
                LDA     (Q),Y
                AND     #$3F
                BNE     @next
                LDA     Q              ; the name
                CLC
                ADC     #1
                LDX     Q + 1
                BCC     @nm
                INX
@nm:            JSR     UT_MATCH
                BCC     @next
                CLC                    ; room for its record: P + 13 <= UT_MHI?
                LDA     P
                ADC     #REC_LEN
                STA     T
                LDA     P + 1
                ADC     #0
                STA     T + 1
                LDA     UT_MHI
                CMP     T
                LDA     UT_MHI + 1
                SBC     T + 1
                BCS     @room
                LDA     #<SNOMEM
                LDX     #>SNOMEM
                JMP     FAIL
@room:          LDY     #0
@copy:          INY
                LDA     (Q),Y
                DEY
                STA     (P),Y
                INY
                CPY     #11
                BNE     @copy
                LDA     #0
                STA     (P),Y          ; R_FLAGS
                INY
                STA     (P),Y          ; R_MODE
                JSR     PNEXT
                INC     NFILES
                BNE     @next
                INC     NFILES + 1
@next:          JSR     UT_SNEXT
                JMP     @entry

; Each file's XFCB (the user + 16, its name): its mode, and whether it has a
; password.
XFCBS:          JSR     UT_SCAN
@entry:         BCC     @look
                CPX     #0
                BNE     @fail
                RTS
@fail:          JMP     QUIT
@look:          STA     Q
                STX     Q + 1
                LDA     (Q)
                SEC
                SBC     #16
                CMP     USER
                BNE     @next
                JSR     FIRST
@rec:           BCS     @next
                LDY     #0
@cmp:           INY
                LDA     (Q),Y
                DEY
                EOR     (P),Y
                AND     #$7F
                BNE     @other
                INY
                CPY     #11
                BNE     @cmp
                LDY     #R_FLAGS       ; the first XFCB of the name counts (the BDOS's)
                LDA     (P),Y
                BNE     @next
                LDY     #12            ; its mode
                LDA     (Q),Y
                LDY     #R_MODE
                STA     (P),Y
                LDA     Q
                LDX     Q + 1
                JSR     UT_PWDEC
                LDA     #RF_XFCB
                STA     T
                JSR     PWNONE
                BCC     @flags
                LDA     #RF_PW
                TSB     T
@flags:         LDA     T
                LDY     #R_FLAGS
                STA     (P),Y
                BRA     @next
@other:         JSR     NEXT
                BRA     @rec
@next:          JSR     UT_SNEXT
                BRA     @entry

; A password mode with no password, given or kept: "Assign a Password to
; this file." for any of them, nothing changed.
ASSIGN:         JSR     FIRST
@rec:           BCS     @done
                JSR     NEWMODE
                BEQ     @next          ; mode 0
                LDA     GIVEN + O_PW
                BEQ     @kept
                LDA     VAL + O_PW     ; a password given: its length
                BNE     @next
                BRA     @bad
@kept:          LDY     #R_FLAGS
                LDA     (P),Y
                AND     #RF_PW
                BNE     @next
@bad:           LDA     #<SASSIGN
                LDX     #>SASSIGN
                JMP     FAIL
@next:          JSR     NEXT
                BRA     @rec
@done:          RTS

; The record at P's mode once set: PROTECT's, else its XFCB's, else READ
; (a password given to a file without one). A (and Z) the mode.
NEWMODE:        LDA     GIVEN + O_PROT
                BEQ     @kept
                LDX     VAL + O_PROT
                LDA     MODES,X
                RTS
@kept:          LDY     #R_FLAGS
                LDA     (P),Y
                AND     #RF_XFCB
                BEQ     @read
                LDY     #R_MODE
                LDA     (P),Y
                AND     #$E0
                RTS
@read:          LDA     #$80
                RTS

; The file at P by 30: its attributes as they are, changed as asked; then
; its line, carry clear. Carry set if the BDOS refused it (said).
ATTRIB:         JSR     PFCB
                LDX     #3             ; f5'-f8' aren't the disk's (f6' would set S1)
@inter:         LDA     WFCB + 5,X
                AND     #$7F
                STA     WFCB + 5,X
                DEX
                BPL     @inter
                LDX     #0             ; each attribute option: its byte, on or off
@opt:           LDA     GIVEN,X
                BEQ     @onext
                LDY     ABYTE,X
                LDA     AON,X          ; 1: on, 0: off, 2: its value
                CMP     #2
                BNE     @fixed
                LDA     VAL,X
@fixed:         CMP     #0
                BEQ     @off
                LDA     WFCB,Y
                ORA     #$80
                BRA     @put
@off:           LDA     WFCB,Y
                AND     #$7F
@put:           STA     WFCB,Y
@onext:         INX
                CPX     #O_F4 + 1
                BNE     @opt
                JSR     SETDMA
                LDA     #<WFCB
                LDX     #>WFCB
                LDY     #F_ATTRIB
                JSR     BDOS
                JSR     FBDOSOK
                BCC     @done
                RTS
@done:          JSR     PUTFILE        ; "M:ONE.TXT set to System (SYS), ..."
                LDA     #<SSETTO
                LDX     #>SSETTO
                JSR     CM_PUTS
                STZ     T              ; 1: one written, a comma before the next
                LDY     #0
@shown:         LDX     SHOWN,Y        ; the table's order: DIR SYS RW RO ...
                LDA     GIVEN,X
                BEQ     @snext
                PHY
                PHX
                LDA     T
                BEQ     @first
                LDA     #<SCOMMA
                LDX     #>SCOMMA
                JSR     CM_PUTS
@first:         LDA     #1
                STA     T
                PLX
                PHX
                TXA
                ASL     A
                TAY
                LDA     ANAMES,Y
                LDX     ANAMES + 1,Y
                JSR     CM_PUTS
                PLX
                LDA     AON,X
                CMP     #2
                BNE     @named
                LDA     VAL,X
                JSR     PUTON0
@named:         PLY
@snext:         INY
                CPY     #O_F4 + 1
                BNE     @shown
                CLC
                JMP     CM_EOL

; The file at P by 103: its mode as NEWMODE has it, a new password if one
; was given (NONE without one: 8 blanks, the password deleted, the Guide);
; then its line. Nothing if the BDOS refused it (said).
PROTECT:        JSR     PFCB
                JSR     NEWMODE
                STA     T
                LDA     GIVEN + O_PW   ; bit 0: a new password, the DMA's 8-15
                BNE     @bits
                LDA     GIVEN + O_PROT
                BEQ     @bits
                LDA     VAL + O_PROT
                CMP     #V_NONE
                LDA     #0
                BCC     @bits          ; READ, WRITE, DELETE
                LDX     #7
                LDA     #' '
@blank:         STA     PWBUF + 8,X
                DEX
                BPL     @blank
                LDA     #1
@bits:          ORA     T
                STA     WFCB + 12
                JSR     SETDMA
                LDA     #<WFCB
                LDX     #>WFCB
                LDY     #F_WRITEXFCB
                JSR     BDOS
                JSR     FBDOSOK
                BCC     @done
                RTS
@done:          JSR     PUTFILE
                LDA     #<SPROTEQ
                LDX     #>SPROTEQ
                JSR     CM_PUTS
                LDA     WFCB + 12      ; the highest mode bit's name
                LDX     #0
@mode:          ASL     A
                BCS     @name
                INX
                CPX     #3
                BNE     @mode
@name:          TXA
                ASL     A
                TAX
                LDA     MNAMES + 1,X
                PHA
                LDA     MNAMES,X
                PLX
                JMP     UT_SAY

; WFCB from the record at P: the drive, its name and attributes, 12-35 0;
; PWBUF the specification's password, then the new one.
PFCB:           LDA     DRV
                INC     A
                STA     WFCB
                LDY     #10
@name:          LDA     (P),Y
                STA     WFCB + 1,Y
                DEY
                BPL     @name
                STZ     WFCB + 12
                JSR     WZERO
                LDX     #7
@pw:            LDA     FCB + 16,X
                STA     PWBUF,X
                LDA     PWNEW,X
                STA     PWBUF + 8,X
                DEX
                BPL     @pw
                RTS

; WFCB's bytes 13-35, 0.
WZERO:          LDX     #13
@zero:          STZ     WFCB,X
                INX
                CPX     #36
                BNE     @zero
                RTS

SETDMA:         LDA     #<PWBUF
                LDX     #>PWBUF
                LDY     #F_DMAOFF
                JMP     BDOS

; A BDOS call's A: $FF an error: said by the BDOS (X its code), or for the
; directory's $FF (X 0) "Directory full" (ours); then the end.
BDOSOK:         CMP     #$FF
                BEQ     @err
                RTS
@err:           CPX     #0
                BNE     @said
                LDA     #<SFULL
                LDX     #>SFULL
                JMP     FAIL
@said:          JMP     QUIT

; A file's call's A, as BDOSOK, but for an error the BDOS said: the return
; code $FF00 and carry set, so SET goes on with the next file; else carry
; clear.
FBDOSOK:        CMP     #$FF
                BNE     @ok
                CPX     #0
                BEQ     BDOSOK
                JSR     UT_SAID
                SEC
                RTS
@ok:            CLC
                RTS

; The records: FIRST P at the first, carry set if there are none; NEXT
; the next, carry set past the last.
FIRST:          LDA     UT_MLO
                STA     P
                LDA     UT_MLO + 1
                STA     P + 1
                LDA     NFILES
                STA     LEFT
                LDA     NFILES + 1
                STA     LEFT + 1
                BRA     ANY
NEXT:           JSR     PNEXT
                LDA     LEFT
                BNE     @dec
                DEC     LEFT + 1
@dec:           DEC     LEFT
ANY:            LDA     LEFT
                ORA     LEFT + 1
                BEQ     @none
                CLC
                RTS
@none:          SEC
                RTS

PNEXT:          CLC
                LDA     P
                ADC     #REC_LEN
                STA     P
                BCC     @done
                INC     P + 1
@done:          RTS

; ---- writing -------------------------------------------------------------

PUTDRV:         LDA     DRV
                JMP     UT_PUTDRV

; "M:ONE.TXT": the drive and the record at P's name.
PUTFILE:        JSR     PUTDRV
                LDA     P
                LDX     P + 1
                JMP     CM_FNAME

; A 1: "ON", 0: "OFF", then a line owed; PUTON0 without it.
PUTON:          JSR     PUTON0
                JMP     CM_EOL
PUTON0:         CMP     #0
                BEQ     @off
                LDA     #<SON
                LDX     #>SON
                JMP     CM_PUTS
@off:           LDA     #<SOFF
                LDX     #>SOFF
                JMP     CM_PUTS

; The message at A/X, the return code $FF00, the end.
FAIL:           JSR     UT_SAY
                JMP     QUIT

        .include "cmd.inc"
        .include "util.inc"

        .rodata
; The options (Table 5-16 and the drive's): any unambiguous prefix
OPTAB:          .byte   "DIR", 0, "SYS", 0, "RO", 0, "RW", 0, "ARCHIVE", 0
                .byte   "F1", 0, "F2", 0, "F3", 0, "F4", 0, "PASSWORD", 0, "PROTECT", 0
                .byte   "NAME", 0, "CREATE", 0, "ACCESS", 0, "UPDATE", 0, "DEFAULT", 0, 0
KINDS:          .byte   K_NONE, K_NONE, K_NONE, K_NONE, K_ONOFF
                .byte   K_ONOFF, K_ONOFF, K_ONOFF, K_ONOFF, K_PW, K_PROT
                .byte   K_NAME, K_ONOFF, K_ONOFF, K_ONOFF, K_PW
PROTW:          .byte   "READ", 0, "WRITE", 0, "DELETE", 0, "NONE", 0, "ON", 0, "OFF", 0, 0
MODES:          .byte   $80, $40, $20, 0 ; READ, WRITE, DELETE, NONE
; The attribute options: their FCB byte, and on (1), off (0) or the value (2)
ABYTE:          .byte   10, 10, 9, 9, 11, 1, 2, 3, 4
AON:            .byte   0, 1, 1, 0, 2, 2, 2, 2, 2
SHOWN:          .byte   O_DIR, O_SYS, O_RW, O_RO, O_ARC, O_F1, O_F1 + 1, O_F1 + 2, O_F4
ANAMES:         .addr   SADIR, SASYS, SARO, SARW, SAARC, SAF1, SAF2, SAF3, SAF4
MNAMES:         .addr   SMREAD, SMWRITE, SMDELETE, SMNONE
LOPT:           .byte   O_PROT
                .addr   SLPROT
                .byte   O_CREATE
                .addr   SLCREATE
                .byte   O_ACCESS
                .addr   SLACCESS
                .byte   O_UPDATE
                .addr   SLUPDATE
SLABDEF:        .byte   "LABEL      "  ; the Guide's default name
SON:            .byte   "ON$"
SOFF:           .byte   "OFF$"
SADIR:          .byte   "Directory (DIR)$"
SASYS:          .byte   "System (SYS)$"
SARO:           .byte   "Read Only (RO)$"
SARW:           .byte   "Read Write (RW)$"
SAARC:          .byte   "Archive=$"
SAF1:           .byte   "F1=$"
SAF2:           .byte   "F2=$"
SAF3:           .byte   "F3=$"
SAF4:           .byte   "F4=$"
SSETTO:         .byte   " set to $"
SCOMMA:         .byte   ", $"
SPROTEQ:        .byte   " Protection = $"
SMREAD:         .byte   "Read$"
SMWRITE:        .byte   "Write$"
SMDELETE:       .byte   "Delete$"
SMNONE:         .byte   "None$"
SSETRO:         .byte   " set to Read Only (RO)$"
SSETRW:         .byte   " set to Read Write (RW)$"
SLABEQ:         .byte   " Label = $"
SLPWSET:        .byte   " Label Password set$"
SLPWRM:         .byte   " Label Password removed$"
SLPROT:         .byte   " Password Protection = $"
SLCREATE:       .byte   " Create = $"
SLACCESS:       .byte   " Access = $"
SLUPDATE:       .byte   " Update = $"
SPWQ:           .byte   "Password: $"
SNOOPTS:        .byte   "No options specified$"
SBADOPT:        .byte   "Invalid option$"
SBADSPEC:       .byte   "Invalid file specification.$"
SROW:           .byte   "Cannot set both RO and RW.$"
SSYSDIR:        .byte   "Cannot set both SYS and DIR.$"
SBOTHST:        .byte   "Cannot have both create and access time stamps$"
SDRVONLY:       .byte   "Option only for drives.$"
SNEEDF:         .byte   "Option requires a file reference.$"
SLABFILE:       .byte   "Cannot label a drive with a file referenced$"
SNOFILE:        .byte   "File not found.$"
SWRONG:         .byte   "Wrong Password$"
SREFORM:        .byte   "Directory needs to be reformatted for date/time stamps.$"
SASSIGN:        .byte   "Assign a Password to this file.$"
SPWOFF:         .byte   "Password protection is off: SET [PROTECT=ON]$"
SNOMEM:         .byte   "Not Enough Memory$"
SFULL:          .byte   "Directory full$"

        .bss
GIVEN:          .res    NOPTS          ; 1: the option was given
VAL:            .res    NOPTS          ; its value: ON 1, OFF 0, PROTW's index, a password's length
NGIVEN:         .res    1
LORD:           .res    NOPTS          ; the label's options, in the order given
NLORD:          .res    1
CUR:            .res    1
KIND:           .res    1
T:              .res    2
ISFILE:         .res    1
DRV:            .res    1
USER:           .res    1
EXMASK:         .res    1
PWOPS:          .res    1
ATTOPS:         .res    1
NEWPW:          .res    1
NFILES:         .res    2
LEFT:           .res    2
FCB:            .res    36             ; the specification
FCB2:           .res    36             ; a second, refused
WFCB:           .res    36             ; for 30, 100 and 103
NFCB:           .res    36             ; NAME='s, by 152
PARSE:          .res    4              ; 152's: the string, the FCB
PWNEW:          .res    8              ; PASSWORD='s, blanks after
DEFPW:          .res    8              ; DEFAULT='s
PWBUF:          .res    16             ; the DMA buffer: a password, a new one
LPW:            .res    8              ; the label's password, as typed
LNAME:          .res    11             ; the label's name
LNEW:           .res    11             ; NAME='s
LMODE:          .res    1              ; the label's byte 12, bits 7-4
HASSF:          .res    1              ; 1: the drive has SFCBs

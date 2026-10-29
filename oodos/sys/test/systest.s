; SYSTEST: the BDOS's system extras (hw/oodos.md, "The BDOS, phase 3":
; 3b2) against the Programmer's Guide, a case by the command tail's first
; letter, a line a result for test_oodos_sys.cpp, which types when it sees
; a question. "aa xx" is A and X as the BDOS returned them.
;
;   S   the SCB (49): fields by their offsets, gets and sets of bytes and
;       words, past the end, a reserved operation, and what each set does
;       (the error mode, the DMA address, the drive, the user, the console
;       mode, the delimiter, the AUX: vector, the date, the version)
;   C   the console: 109, 110, 111 (tabs, raw, empty), 5 and 112 and
;       printer echo with LST: on the serial line, 4, 7, 8, 3 (AUX?), a key
;       kept by the output's look (KEEP?), ^S and ^Q (STOP?), ^S ^P (PE?,
;       PO?), ^Q and ^P alone dropped by 1 (DROP?), 1 with mode bit 1
;       (RAW1?), 11 with mode bit 0 (CC?), 11 keeping a key (S11?), ^S ^C
;       with mode bit 3 (NC?)
;   L   function 10's editing: lines of 30 (L?), then of 3 and of 0, the
;       buffer at the DMA address with its string, rub/del echoing, a width
;       of 9, ^C with mode bit 3; each "=nn hh..." the count and the line
;   P   152: each "Pnn ssss rrrr ff.." the string's address, the result,
;       the FCB's bytes 0-25 (filled with AA first)
;   N   107: SER and the 6 bytes
;   U   user 5 and M:, then 47 of "HELLO" with $FF: the CCP's
;   H   the same with 0: not the CCP's
;   V   UD uu dd: the user and the drive
;   D   M: selected, then the end: the CCP's drive comes back
;   J   47 of a command of 127 characters
;   Q   108: the return code set to 1234 and read back (CODE)
;   K   the return code 4321, then 47 of "HELLO"
;   E   in the default error mode, open of Q:X: the program ended
;   X   ^S, then ^C (XC?): the program ended
;   W   function 10, then ^C as the first key (W?): the program ended
;   Z   the delimiter '#', the console mode 8 and the multi-sector count 7
;       left for the CCP to put back

        .setcpu "65C02"
        .include "oodos.inc"

        OODOS_HEADER
        .include "printzp.inc"
        .include "fszp.inc"

        .zeropage
CNT:    .res    1
IDX:    .res    1
SAVE:   .res    1

        .bss
PB:     .res    4                       ; 49's parameter block
FCB:    .res    36
DAT:    .res    4
LBUF:   .res    40                      ; function 10's buffer
CCBK:   .res    4                       ; 111's and 112's
PFCBK:  .res    4                       ; 152's

; 49: get the word at offset off; A/X.
.macro SCBGET off
        lda     #off
        jsr     scbget
.endmacro
; 49: set the byte at off to v.
.macro SCBSETB off, v
        lda     #off
        ldx     #v
        jsr     scbsetb
.endmacro
; 49: set the word at off to v.
.macro SCBSETW off, v
        lda     #<(v)
        sta     PB+2
        lda     #>(v)
        sta     PB+3
        lda     #off
        jsr     scbsetw
.endmacro
; "KEY " and the SCB's word at off, as aa xx.
.macro SHOWW key, off
        SAY     key
        SCBGET  off
        jsr     RES
.endmacro
; "KEY " and the SCB's byte at off.
.macro SHOWB key, off
        SAY     key
        SCBGET  off
        jsr     HEX2
        jsr     CRLF
.endmacro
; A key waited for without taking it: 6 with $FE until one is there.
.macro HOLD str
        .local w
        SAY     str
w:      lda     #$FE
        ldy     #C_RAWIO
        jsr     BDOS
        cmp     #0
        beq     w
.endmacro
; 111 or 112 of the len bytes at str.
.macro BLOCK fn, str, len
        lda     #<(str)
        sta     CCBK
        lda     #>(str)
        sta     CCBK+1
        lda     #<(len)
        sta     CCBK+2
        lda     #>(len)
        sta     CCBK+3
        CALLF   fn, CCBK
.endmacro

        .code
start:  lda     BP_DMA+2                ; the case
        ldx     #CASES_LEN - 1
@find:  cmp     CASES,x
        beq     @found
        dex
        bpl     @find
        jmp     nocase
@found: txa
        asl     a
        tax
        jmp     (CJUMPS,x)
nocase: SAY     "NO CASE"
        jmp     CRLF


; ---------------------------------------------------------------------------
cases:  stz     TDRV
        SHOWW   "CMODE0 ", $33          ; 00 00
        SHOWW   "VER ", $05             ; 31 00
        SHOWW   "DMA ", $3C             ; 80 02
        SHOWB   "WIDTH ", $1A           ; 4F
        SHOWB   "PAGE ", $1C            ; 18
        SHOWW   "RUB ", $2E             ; 00 FF
        SHOWW   "DELIM ", $37           ; 24 00
        SHOWW   "MULTI ", $4A           ; 01 00
        SHOWW   "CHAIN0 ", $4C          ; 00 FF
        SHOWB   "FLAGS ", $57           ; 80
        SHOWW   "COMBASE ", $5D         ; 00 F8
        SHOWW   "PAST ", $64            ; 00 00
        SHOWW   "PASTFF ", $FF
        SAY     "SETPAST "
        SCBSETB $64, $77
        jsr     RES
        SAY     "LAST "                 ; a word set at $63: only $63
        SCBSETW $63, $1234
        jsr     RES
        SHOWW   "LASTGET ", $63         ; 34 00
        SHOWW   "LAST62 ", $62          ; 00 34
        SCBSETB $63, 0
        SAY     "RESOP "                ; a reserved operation: a get
        lda     #$06
        sta     PB
        lda     #$05
        sta     PB+1
        lda     #$AA
        sta     PB+2
        sta     PB+3
        CALLF   S_SCB, PB
        jsr     RES
        SHOWW   "USERF0 ", $06          ; 00 00
        SCBSETW $06, $BEEF
        SHOWW   "USERF ", $06           ; EF BE
        SCBSETB $4B, $FF                ; the error mode by 49: errors return
        NAME    FCB, "X          "
        lda     #16                     ; Q:
        sta     FCB
        TRY     "ERR ", F_OPEN, FCB     ; FF 04
        SHOWB   "ERRDRV ", $51          ; 10
        CALLF   F_ERRMODE, $FE
        SHOWB   "EMODE ", $4B           ; FE
        CALLF   F_ERRMODE, $FF
        CALLF   F_DMAOFF, $1234
        SHOWW   "DMA26 ", $3C           ; 34 12
        CALLF   F_DMAOFF, BP_DMA
        ldx     #0                      ; the DMA address by 49: a search's record there
@zero:  stz     BUF,x
        inx
        bpl     @zero
        SCBSETW $3C, BUF
        lda     #$FF
        sta     TDRV
        NAME    FCB, "???????????"
        CALLF   F_SFIRST, FCB
        CALLF   F_DMAOFF, BP_DMA
        SAY     "DMA49 "
        jsr     ZEROS
        jsr     YESNO                   ; N: not all 0
        SCBSETB $3E, DRV_M              ; the drive by 49
        TRY     "DISK ", DRV_GET, 0     ; 0C 00
        SCBSETB $3E, 0
        SCBSETB $44, 7                  ; the user
        TRY     "USER ", F_USERNUM, $FF ; 07 00
        SCBSETB $44, 0
        SCBSETW $33, $0300              ; the console mode
        TRY     "CMODE ", C_MODE, $FFFF ; 00 03
        SCBSETW $33, 0
        SAY     "D49 "                  ; the delimiter
        SCBSETB $37, '#'
        lda     #<hashs
        ldx     #>hashs
        jsr     PUTS
        SCBSETB $37, '$'
        jsr     CRLF                    ; D49 AB
        SCBSETW $26, 0                  ; AUXIN on nothing: ^Z
        TRY     "AUX0 ", A_READ, 0      ; 1A 00
        SCBSETW $26, $2000              ; on LPT alone
        TRY     "AUXL ", A_READ, 0      ; 1A 00
        SCBSETW $26, $4000
        CALLF   T_GET, DAT              ; the date and time
        SAY     "DATE "
        SCBGET  $58
        cmp     DAT
        bne     @no
        cpx     DAT+1
        bne     @no
        clc
        bra     @yn
@no:    sec
@yn:    jsr     YESNO
        SAY     "ABC"
        SCBGET  $1B
        sta     SAVE
        SAY     " COLUMN "
        lda     SAVE
        jsr     HEX2                    ; 03
        jsr     CRLF
        SHOWB   "LISTF ", $38           ; 00
        SCBSETB $05, $22                ; the version byte: 12 the same
        TRY     "VERSION ", S_BDOSVER, 0 ; 31 00
        SCBSETB $05, $31
        SCBSETW $06, 0
        rts

; 49 of the word at A.
scbget: sta     PB
        stz     PB+1
        CALLF   S_SCB, PB
        rts
; 49: the byte at A set to X.
scbsetb: sta    PB
        stx     PB+2
        lda     #$FF
        sta     PB+1
        CALLF   S_SCB, PB
        rts
; 49: the word at A set to PB+2.
scbsetw: sta    PB
        lda     #$FE
        sta     PB+1
        CALLF   S_SCB, PB
        rts

; ---------------------------------------------------------------------------
casec:  TRY     "MODE0 ", C_MODE, $FFFF ; 00 00
        CALLF   C_MODE, $0300
        TRY     "MODE1 ", C_MODE, $FFFF ; 00 03
        CALLF   C_MODE, 0
        TRY     "DLM0 ", C_DELIMIT, $FFFF ; 24 00
        SAY     "DLM1 "
        CALLF   C_DELIMIT, '%'
        lda     #<pcts
        ldx     #>pcts
        jsr     PUTS                    ; X
        CALLF   C_DELIMIT, $FFFF
        sta     SAVE
        CALLF   C_DELIMIT, '$'
        lda     SAVE
        jsr     HEX2                    ; 25
        jsr     CRLF
        SAY     "PBLK"
        jsr     CRLF
        BLOCK   C_WRITEBLK, tabs, 3     ; P       Q
        jsr     CRLF
        CALLF   C_MODE, CM_RAWOUT
        BLOCK   C_WRITEBLK, tabs, 3     ; P^IQ, raw
        lda     #9
        jsr     PUTC
        CALLF   C_MODE, 0
        jsr     CRLF
        SAY     "ZB<"
        BLOCK   C_WRITEBLK, tabs, 0
        SAY     ">"
        jsr     CRLF
        SCBSETW $2A, $4000              ; LST: on the serial line
        SAY     "LST "
        CALLF   L_WRITE, 'L'
        BLOCK   L_WRITEBLK, lists, 6    ; as it is: LIST^IX
        jsr     CRLF
        SCBSETB $38, 1                  ; printer echo
        SAY     "ECHO"                  ; on the serial line twice
        SCBSETB $38, 0
        jsr     CRLF
        SCBSETB $38, 1
        CALLF   C_MODE, CM_RAWOUT       ; raw: no echo
        SAY     "RAW"
        CALLF   C_MODE, 0
        SCBSETB $38, 0
        jsr     CRLF
        SAY     "AUX "
        CALLF   A_WRITE, 'A'            ; AUX: is the serial line
        jsr     CRLF
        TRY     "AUXIST ", A_STATIN, 0  ; 00 00
        TRY     "AUXOST ", A_STATOUT, 0 ; FF 00
        SAY     "AUX?"
        ldy     #A_READ
        jsr     BDOS
        jsr     got                     ; GOT 71
        HOLD    "KEEP?"                 ; a key waits, untaken
        lda     #'z'
        jsr     PUTC                    ; 2's look keeps it
        SAY     " ST "
        ldy     #C_STAT
        jsr     BDOS
        jsr     HEX2                    ; 01
        SAY     " FE "
        lda     #$FE
        ldy     #C_RAWIO
        jsr     BDOS
        jsr     HEX2                    ; FF
        SAY     " K "
        ldy     #C_READ
        jsr     BDOS
        jsr     HEX2                    ; k6B
        jsr     CRLF
        HOLD    "STOP?"                 ; ^S: stopped, x a BEL, ^Q
        SAY     "AB"
        jsr     CRLF
        SAY     "PE"                    ; LST: still on the serial line
        HOLD    "?"                     ; ^S ^P ^Q: echo on
        SAY     "XY"
        HOLD    "PO?"                   ; ^S ^P ^Q: off (echoed twice)
        SAY     "XY"
        jsr     CRLF
        SCBSETB $38, 0
        SCBSETW $2A, $2000
        SAY     "DROP?"                 ; ^Q, ^P, z: z
        ldy     #C_READ
        jsr     BDOS
        jsr     HEX2
        jsr     CRLF
        CALLF   C_MODE, CM_NOSTOP
        SAY     "RAW1?"                 ; ^S: read, not echoed
        ldy     #C_READ
        jsr     BDOS
        jsr     HEX2
        CALLF   C_MODE, 0
        jsr     CRLF
        CALLF   C_MODE, CM_CTLCST
        SAY     "CC?"                   ; a, b, ^C: 11 only for the ^C
@cc:    ldy     #C_STAT
        jsr     BDOS
        cmp     #0
        beq     @cc
        jsr     HEX2
        jsr     SPACE
        lda     #$FF                    ; nothing left
        ldy     #C_RAWIO
        jsr     BDOS
        jsr     HEX2
        CALLF   C_MODE, 0
        jsr     CRLF
        SAY     "S11?"                  ; ^Q dropped, w kept
@s11:   ldy     #C_STAT
        jsr     BDOS
        cmp     #0
        beq     @s11
        jsr     HEX2
        jsr     SPACE
        ldy     #C_READ
        jsr     BDOS
        jsr     HEX2
        jsr     CRLF
        CALLF   C_MODE, CM_NOCTLCX
        HOLD    "NC?"                   ; ^S ^C: a BEL, ^Q
        SAY     "AB"
        CALLF   C_MODE, 0
        jmp     CRLF

; " GOT hh" and a new line.
got:    pha
        SAY     " GOT "
        pla
        jsr     HEX2
        jmp     CRLF

DRV_M   = 12
CM_RAWOUT = $04
CM_NOSTOP = $02
CM_CTLCST = $01
CM_NOCTLCX = $08

; ---------------------------------------------------------------------------
casel:  lda     #LINES30
        sta     CNT
@l30:   lda     #30
        jsr     line
        dec     CNT
        bne     @l30
        lda     #3
        jsr     line
        lda     #0
        jsr     line
        ldx     #INIT1_LEN - 1          ; the DMA buffer's string: INIT
@i1:    lda     init1,x
        sta     BP_DMA+2,x
        dex
        bpl     @i1
        lda     #20
        jsr     dmaline
        ldx     #INIT2_LEN - 1          ; with a CR: back at once
@i2:    lda     init2,x
        sta     BP_DMA+2,x
        dex
        bpl     @i2
        lda     #20
        jsr     dmaline
        ldx     #INIT3_LEN - 1          ; with ^A
@i3:    lda     init3,x
        sta     BP_DMA+2,x
        dex
        bpl     @i3
        lda     #20
        jsr     dmaline
        SCBSETB $2F, 0                  ; rub/del echoes
        lda     #30
        jsr     line
        SCBSETB $2F, $FF
        SCBSETB $2E, $FF                ; ^H as rub/del
        lda     #30
        jsr     line
        SCBSETB $2E, 0
        SCBSETB $1A, 9                  ; a width of 9: columns 0-8 used
        lda     #30
        jsr     line
        lda     #30
        jsr     line
        SCBSETB $1A, 79
        CALLF   C_MODE, CM_NOCTLCX      ; ^C a character
        lda     #30
        jsr     line
        CALLF   C_MODE, 0
        rts

LINES30 = 21

; Function 10 of a buffer of A at LBUF: "L?", then "=nn hh..".
line:   sta     LBUF
        SAY     "L?"
        CALLF   C_READSTR, LBUF
        lda     #<(LBUF+1)
        ldx     #>(LBUF+1)
        bra     result
; The same with the buffer at the DMA address, $0280.
dmaline: sta    BP_DMA
        SAY     "I?"
        CALLF   C_READSTR, 0
        lda     #<(BP_DMA+1)
        ldx     #>(BP_DMA+1)
result: sta     PTR
        stx     PTR+1
        SAY     "="
        lda     (PTR)
        jsr     HEX2
        jsr     SPACE
        lda     (PTR)
        tax
        inc     PTR
        bne     @hex
        inc     PTR+1
@hex:   jsr     HEXN
        jmp     CRLF

; ---------------------------------------------------------------------------
casep:  stz     IDX
@case:  lda     IDX                     ; PTR: the IDX'th string, after the last's 0
        cmp     #PCASES
        beq     @done
        lda     #<pstrs
        sta     PTR
        lda     #>pstrs
        sta     PTR+1
        ldx     IDX
@skip:  beq     @at
        ldy     #0
@zero:  lda     (PTR),y
        iny
        cmp     #0
        bne     @zero
        tya
        clc
        adc     PTR
        sta     PTR
        bcc     @next
        inc     PTR+1
@next:  dex
        bra     @skip
@at:    ldx     #35                     ; the FCB all AA
        lda     #$AA
@aa:    sta     FCB,x
        dex
        bpl     @aa
        lda     PTR
        sta     PFCBK
        lda     PTR+1
        sta     PFCBK+1
        lda     #<FCB
        sta     PFCBK+2
        lda     #>FCB
        sta     PFCBK+3
        lda     #'P'
        jsr     PUTC
        lda     IDX
        jsr     HEX2
        jsr     SPACE
        lda     PTR
        ldx     PTR+1
        jsr     HEX4
        jsr     SPACE
        CALLF   F_PARSE, PFCBK
        jsr     HEX4
        jsr     SPACE
        lda     #<FCB
        sta     PTR
        lda     #>FCB
        sta     PTR+1
        ldx     #26
        jsr     HEXN
        jsr     CRLF
        inc     IDX
        bra     @case
@done:  rts

; ---------------------------------------------------------------------------
casen:  CALLF   S_SERIAL, BUF
        SAY     "SER "
        lda     #<BUF
        sta     PTR
        lda     #>BUF
        sta     PTR+1
        ldx     #6
        jsr     HEXN
        jmp     CRLF

caseu:  lda     #$FF
        bra     chainm
caseh:  lda     #0
chainm: sta     SAVE
        CALLF   F_USERNUM, 5
        CALLF   DRV_SET, DRV_M
        ldx     #HELLOC_LEN - 1
@cmd:   lda     helloc,x
        sta     BP_DMA,x
        dex
        bpl     @cmd
        lda     SAVE
        ldy     #P_CHAIN
        jsr     BDOS
        SAY     "NOT CHAINED"           ; never
        jmp     CRLF

casev:  SAY     "UD "
        CALLF   F_USERNUM, $FF
        jsr     HEX2
        jsr     SPACE
        ldy     #DRV_GET
        jsr     BDOS
        jsr     HEX2
        jmp     CRLF

cased:  CALLF   DRV_SET, DRV_M
        SAY     "ON M"
        jmp     CRLF

casej:  ldx     #0                      ; HELLO, a blank, 116 'y', "zzzzz", 0: 127
@h:     lda     helloc,x
        sta     BP_DMA,x
        inx
        cpx     #5
        bne     @h
        lda     #' '
        sta     BP_DMA,x
        inx
@y:     lda     #'y'
        sta     BP_DMA,x
        inx
        cpx     #122
        bne     @y
@z:     lda     #'z'
        sta     BP_DMA,x
        inx
        cpx     #127
        bne     @z
        stz     BP_DMA,x
        lda     #0
        ldy     #P_CHAIN
        jmp     BDOS

caseq:  CALLF   P_CODE, $1234
        TRY     "CODE ", P_CODE, $FFFF  ; 34 12
        rts

casek:  CALLF   P_CODE, $4321
        ldx     #HELLOC_LEN - 1
@cmd:   lda     helloc,x
        sta     BP_DMA,x
        dex
        bpl     @cmd
        lda     #0
        ldy     #P_CHAIN
        jmp     BDOS

casee:  stz     TDRV
        NAME    FCB, "X          "      ; the default error mode
        lda     #16
        sta     FCB
        CALLF   F_OPEN, FCB
        SAY     "NOT ENDED"
        jmp     CRLF

casex:  HOLD    "XC?"                   ; ^S, then ^C
        SAY     "AB"
        SAY     "NOT ENDED"
        jmp     CRLF

casew:  lda     #30
        jsr     line                    ; ^C first
        SAY     "NOT ENDED"
        jmp     CRLF

casez:  CALLF   C_DELIMIT, '#'
        CALLF   C_MODE, $0008
        SCBSETB $4A, 7
        rts

        .rodata
CASES:  .byte   "SCLPNUHVDJQKEXWZ"
CASES_LEN = * - CASES
CJUMPS: .word   cases, casec, casel, casep, casen, caseu, caseh, casev, cased, casej
        .word   caseq, casek, casee, casex, casew, casez
hashs:  .byte   "AB#CD$"
pcts:   .byte   "X%Y$"
tabs:   .byte   "P", 9, "Q"
lists:  .byte   "LIST", 9, "X"
init1:  .byte   "INIT", 0
INIT1_LEN = * - init1
init2:  .byte   "AB", 13, "CD", 0
INIT2_LEN = * - init2
init3:  .byte   "AC", 1, "B", 0
INIT3_LEN = * - init3
helloc: .byte   "HELLO", 0
HELLOC_LEN = * - helloc
; 152's strings, each to its 0 (test_oodos_sys.cpp has them too)
pstrs:  .byte   "b:foo.txt;pw", 0                       ; 00
        .byte   "  ", 9, "foo.bar  =x", 0               ; 01
        .byte   "foo  bar", 0                           ; 02
        .byte   "*.*", 0                                ; 03
        .byte   "x*.c*", 0                              ; 04
        .byte   "ab*c", 0                               ; 05
        .byte   "abcdefghi", 0                          ; 06
        .byte   "a.abcd", 0                             ; 07
        .byte   "a;123456789", 0                        ; 08
        .byte   "p:x", 0                                ; 09
        .byte   "q:x", 0                                ; 0A
        .byte   "1:x", 0                                ; 0B
        .byte   "5a:x", 0                               ; 0C
        .byte   "b:", 0                                 ; 0D
        .byte   "   ", 0                                ; 0E
        .byte   0                                       ; 0F
        .byte   "a", 1, "b", 0                          ; 10
        .byte   "a.b.c", 0                              ; 11
        .byte   "a,b", 0                                ; 12
        .byte   "=x", 0                                 ; 13
        .byte   "a", $7F, 0                             ; 14
        .byte   "a;p*w", 0                              ; 15
        .byte   "abc", 13, "x", 0                       ; 16
        .byte   "a|b", 0                                ; 17
        .byte   "@:x", 0                                ; 18
        .byte   "ab:cd", 0                              ; 19
        .byte   "a"                                     ; 1A: 140 blanks, then b,
        .res    140, ' '                                ; past the 128 read
        .byte   "b", 0
        .byte   "A.TXT;PASSWORD", 0                     ; 1B
        .byte   "m:readme", 0                           ; 1C
        .byte   "a", 9, "b", 0                          ; 1D
        .byte   "a", $80, 0                             ; 1E
        .byte   ".typ", 0                               ; 1F
        .byte   "a;b.c", 0                              ; 20
PCASES = 33

        .include "print.inc"
        .include "fs.inc"

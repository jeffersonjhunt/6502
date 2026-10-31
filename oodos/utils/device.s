; DEVICE.COM (hw/oodos.md, "The utilities, phase 4b"; the CP/M 3 User's
; Guide's DEVICE, 3.5): the BIOS's character devices and the SCB's
; redirection vectors.
;
;   DEVICE                  the physical devices, the assignments, then
;                           "Enter new assignment or hit RETURN: " and a
;                           line, taken as a DEVICE command's tail
;   DEVICE NAMES            the physical devices
;   DEVICE VALUES           the assignments
;   DEVICE physical         its line
;   DEVICE logical          its assignments' lines
;   DEVICE logical = physical {[options]} {,physical {[options]}...}
;   DEVICE logical = NULL
;   DEVICE physical [options]
;   DEVICE CONSOLE [PAGE]   "Console width set to 80 columns" and "Console
;                           page set to 24 lines" (the SCB's $1A + 1, $1C)
;   DEVICE CONSOLE [COLUMNS=n, LINES=n]   set (1-256, 0-255), then shown
;
; The devices are DEVTBL's (50), its 24 bytes copied by XMOVE and MOVE from
; bank 0; the vectors the SCB's $22-$2B (49), bit 15 the first device. The
; logical names: CONIN:, CONOUT:, AUXIN:, AUXOUT:, LST:, CON: and CONSOLE:
; (both console vectors), KEYBOARD (CONIN:), AUX: and AUXILIARY: (both
; auxiliary ones), PRINTER (LST:), the colon optional. A physical device's
; options: a baud rate ("Baud rate cannot be set for this device"), XON
; ("Xon-Xoff cannot be set for this device"), NOXON (taken: it is so).
;
; Errors: "Bad Logical Device Assignment", "Invalid Physical device." (a
; device without input in an input vector, or without output in an output
; one, too), "Invalid option.", "Invalid number.", "Invalid delimiter.",
; nothing set after any; "End of line expected" after a command done, the
; rest of the line ignored (the Guide). The return code $FF00 after any.

        .setcpu "65C02"
        .macpack longbranch
        .include "oodos.inc"
        .include "system.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

        OODOS_HEADER

SCB_VECS  = $22                         ; CONIN, CONOUT, AUXIN, AUXOUT, LST
SCB_WIDTH = $1A
SCB_PAGE  = $1C
NDEVMAX   = 3                           ; DEVTBL's 24 bytes: 3 entries of 8
M_IN      = $05                         ; the input vectors' bits (CONIN, AUXIN)
M_OUT     = $1A                         ; the output ones
K_NAMES   = 1
K_VALUES  = 2
K_NULL    = 3

        .code
start:  jsr     UT_INIT
        jsr     CM_PAGE
        stz     trail
        jsr     devtbl
        jsr     UT_SKIP
        bne     @cmd
        jsr     names                   ; DEVICE: both, and a line asked for
        jsr     blank
        jsr     values
        jsr     blank
        lda     #<sask
        ldx     #>sask
        jsr     UT_READ
        beq     done
@cmd:   jsr     command
done:   lda     trail
        beq     @end
        lda     #<strail
        ldx     #>strail
        jsr     UT_SAY
        jsr     UT_FAIL
@end:   jmp     UT_EXIT

; ---- errors ----------------------------------------------------------------

badlog: lda     #<sbadlog
        ldx     #>sbadlog
        bra     error
badphy: lda     #<sbadphy
        ldx     #>sbadphy
        bra     error
badopt: lda     #<sbadopt
        ldx     #>sbadopt
        bra     error
badnum: lda     #<sbadnum
        ldx     #>sbadnum
        bra     error
baddel: lda     #<sbaddel
        ldx     #>sbaddel
        bra     error
nobaud: lda     #<snobaud
        ldx     #>snobaud
        bra     error
noxon:  lda     #<snoxon
        ldx     #>snoxon
error:  jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT

; ---- helpers ---------------------------------------------------------------

blank:  jsr     CM_FLUSH
        jmp     CM_EOL

; The rest of the line: anything but blanks is ignored, and said at the end.
endchk: jsr     UT_SKIP
        beq     @done
        lda     #1
        sta     trail
@done:  rts

; DEVTBL's entries copied from the system's bank to tbl; ndev of them.
devtbl: lda     #BIOS::DEVTBL
        jsr     UT_BIOS
        sta     UT_BPB + 4              ; MOVE's source
        stx     UT_BPB + 5
        lda     #<tbl
        sta     UT_BPB + 6
        lda     #>tbl
        sta     UT_BPB + 7
        lda     #BIOS::XMOVE
        ldx     #0                      ; from bank 0, the system's (CP/M's C)
        ldy     #1                      ; to bank 1, the program's (B)
        jsr     UT_BIOS
        lda     #<tbl                   ; (XMOVE's call left them; again)
        sta     UT_BPB + 6
        lda     #>tbl
        sta     UT_BPB + 7
        lda     #BIOS::MOVE
        ldx     #NDEVMAX * 8
        ldy     #0
        jsr     UT_BIOS
        ldx     #0                      ; the entries to the first 0 name
        ldy     #0
@count: lda     tbl,x
        beq     @end
        iny
        txa
        clc
        adc     #8
        tax
        cpy     #NDEVMAX
        bne     @count
@end:   sty     ndev
        rts

; The tail's next word (letters and digits) at UT_IDX into wbuf, wlen its
; length (0 none, $FF over 9), a ':' after it passed over.
rdword: jsr     UT_SKIP
        ldy     #0
@char:  jsr     UT_PEEK
        cmp     #'0'
        bcc     @end
        cmp     #'9' + 1
        bcc     @take
        cmp     #'A'
        bcc     @end
        cmp     #'Z' + 1
        bcs     @end
@take:  cpy     #9
        bcs     @long
        sta     wbuf,y
        iny
        bra     @next
@long:  ldy     #$FF                    ; too long: the rest passed over
@next:  jsr     UT_GET
        bra     @char
@end:   sty     wlen
        jsr     UT_PEEK
        cmp     #':'
        bne     @done
        jsr     UT_GET
@done:  rts

; wbuf against the table at A/X (each: its length, its characters, a
; value; a 0 length ends it): carry clear and A the value, or carry set.
match:  sta     @len + 1
        stx     @len + 2
        sta     @chr + 1
        stx     @chr + 2
        sta     @val + 1
        stx     @val + 2
        ldx     #0                      ; the entry's start in the table
@len:   lda     $FFFF,x
        beq     @none
        sta     t
        cmp     wlen
        bne     @skip
        ldy     #0
@cmp:   inx
@chr:   lda     $FFFF,x
        cmp     wbuf,y
        bne     @past
        iny
        cpy     t
        bne     @cmp
        inx
@val:   lda     $FFFF,x
        clc
        rts
@past:  iny                             ; to past the value
@pst:   cpy     t
        beq     @nextv
        inx
        iny
        bra     @pst
@nextv: inx
        inx
        bra     @len
@skip:  txa                             ; length, characters, value
        sec
        adc     t
        tax
        inx
        bra     @len
@none:  sec
        rts

; wbuf a physical device's name: carry clear and A its number, or carry set.
findp:  lda     wlen
        beq     @none
        cmp     #7
        bcs     @none
        stz     dev
@dev:   lda     dev
        cmp     ndev
        bcs     @none
        asl     a
        asl     a
        asl     a
        tax
        ldy     #0
@char:  lda     tbl,x
        cpy     wlen
        bcs     @blank
        cmp     wbuf,y
        bne     @next
        bra     @on
@blank: cmp     #' '                    ; past the word: blanks to its 6
        bne     @next
@on:    inx
        iny
        cpy     #6
        bne     @char
        lda     dev
        clc
        rts
@next:  inc     dev
        bra     @dev
@none:  sec
        rts

; Device A's mode byte.
mode:   asl     a
        asl     a
        asl     a
        tax
        lda     tbl + 6,x
        rts

; ---- the command -----------------------------------------------------------

command: jsr    rdword
        lda     wlen
        jeq     badlog
        lda     #<KEYS
        ldx     #>KEYS
        jsr     match
        bcs     @log
        cmp     #K_NAMES
        bne     @values
        jsr     endchk
        jmp     names
@values: cmp    #K_VALUES
        bne     @log                    ; NULL: not a device here
        jsr     endchk
        jmp     values
@log:   lda     #<LNAMES
        ldx     #>LNAMES
        jsr     match
        bcs     @phys
        sta     lmask
        jmp     logical
@phys:  jsr     findp
        bcs     @unk
        sta     dev
        jsr     UT_SKIP
        cmp     #'['
        bne     @show
        lda     dev
        jsr     popts
        jmp     endchk
@show:  jsr     endchk
        lda     dev
        jsr     putdev
        jmp     CM_EOL
@unk:   jsr     UT_SKIP                 ; an assignment to no logical device?
        cmp     #'='
        jeq     badlog
        jmp     badphy

; Device A's option list at UT_IDX's '[': checked (nothing is set).
popts:  sta     dev
        jsr     UT_OAT
@opt:   lda     #<PTAB
        ldx     #>PTAB
        jsr     UT_OPT
        bcs     @other
        ldx     UT_HASV                 ; neither takes a value
        jne     badopt
        cmp     #0
        jeq     noxon                   ; XON
        bra     @opt                    ; NOXON: so it is
@other: cmp     #UT_OEND
        beq     @close
        cmp     #UT_OUNK
        jne     badopt
        jsr     UT_WDEC                 ; a number: a baud rate?
        bcc     @num
        lda     UT_OVR
        jne     badnum
        jmp     badopt
@num:   sta     t
        stx     t + 1
        ldy     #0
@baud:  lda     BAUDS,y
        cmp     t
        bne     @bnext
        lda     BAUDS + 1,y
        cmp     t + 1
        jeq     nobaud
@bnext: iny
        iny
        cpy     #15 * 2
        bne     @baud
        jmp     badnum
@close: jsr     UT_OCLOSE
        jcs     badopt
        rts

; DEVICE logical ...: lmask the vectors (bit 0 CONIN to bit 4 LST).
logical: jsr    UT_SKIP
        bne     @more
        jmp     vlines                  ; its lines
@more:  cmp     #'='
        beq     assign
        cmp     #'['
        jne     baddel
        lda     lmask                   ; CONSOLE [...]
        cmp     #$03
        jne     badopt
        jmp     console

assign: jsr     UT_GET                  ; past the '='
        stz     newv
        stz     ndone
@dev:   jsr     rdword
        lda     ndone
        bne     @phys
        lda     #<KEYS                  ; NULL, alone
        ldx     #>KEYS
        jsr     match
        bcs     @phys
        cmp     #K_NULL
        bne     @phys
        jsr     UT_SKIP                 ; NULL alone: no device after it
        cmp     #','
        jeq     badphy
        jsr     endchk
        bra     @set
@phys:  jsr     findp
        jcs     badphy
        sta     dev
        jsr     mode                    ; input for an input vector, output
        sta     t                       ; for an output one
        lda     lmask
        and     #M_IN
        beq     @out
        lda     t
        and     #$01
        jeq     badphy
@out:   lda     lmask
        and     #M_OUT
        beq     @ok
        lda     t
        and     #$02
        jeq     badphy
@ok:    ldx     dev
        lda     BITS,x
        ora     newv
        sta     newv
        inc     ndone
        jsr     UT_SKIP
        cmp     #'['
        bne     @sep
        lda     dev
        jsr     popts
        jsr     UT_SKIP
@sep:   cmp     #0
        beq     @set
        cmp     #','
        jne     baddel
        jsr     UT_GET
        jmp     @dev
@set:   stz     vec                     ; each vector in lmask
        lda     lmask
        sta     t
@vec:   lsr     t
        bcc     @vnext
        stz     UT_SCBW
        lda     newv
        sta     UT_SCBW + 1
        lda     vec
        asl     a
        adc     #SCB_VECS
        jsr     UT_SCBSETW
@vnext: inc     vec
        lda     vec
        cmp     #5
        bne     @vec
        rts

; DEVICE CONSOLE [PAGE | COLUMNS=n | LINES=n]: set, then shown.
console: jsr    UT_OAT
        stz     setc
        stz     setl
@opt:   lda     #<CTAB
        ldx     #>CTAB
        jsr     UT_OPT
        bcc     @known
        cmp     #UT_OEND
        jne     badopt
        jsr     UT_OCLOSE
        jcs     badopt
        jsr     endchk
        lda     setc                    ; set
        beq     @lines
        lda     #SCB_WIDTH
        ldx     cols
        jsr     UT_SCBSET
@lines: lda     setl
        beq     @show
        lda     #SCB_PAGE
        ldx     lines
        jsr     UT_SCBSET
@show:  lda     #<swidth                ; shown
        ldx     #>swidth
        jsr     CM_PUTS
        lda     #SCB_WIDTH
        jsr     UT_SCBGET
        clc
        adc     #1
        ldx     #0
        bcc     :+
        inx
:       jsr     CM_DECOUT
        lda     #<scols
        ldx     #>scols
        jsr     UT_SAY
        lda     #<spage
        ldx     #>spage
        jsr     CM_PUTS
        lda     #SCB_PAGE
        jsr     UT_SCBGET
        ldx     #0
        jsr     CM_DECOUT
        lda     #<slines
        ldx     #>slines
        jmp     UT_SAY
@known: cmp     #0                      ; PAGE: shown only
        bne     @value
        lda     UT_HASV
        jne     badopt
        bra     @opt
@value: sta     t                       ; COLUMNS 1, LINES 2: a number
        lda     UT_HASV
        jeq     badnum
        jsr     UT_VITEM
        jcs     badnum
        jsr     UT_WDEC
        jcs     badnum
        ldy     t
        cpy     #1
        bne     @l
        cpx     #0                      ; columns: 1-256, the SCB width - 1
        bne     @c256
        cmp     #0
        jeq     badnum
        dec     a
        bra     @cset
@c256:  cpx     #1
        jne     badnum
        cmp     #0
        jne     badnum
        lda     #255
@cset:  sta     cols
        inc     setc
        jmp     @opt
@l:     cpx     #0                      ; lines: 0-255
        jne     badnum
        sta     lines
        inc     setl
        jmp     @opt

; ---- showing ---------------------------------------------------------------

; Device A's line: its name in 6, the baud rate in 5 or NONE, its letters;
; A the characters written.
putdev: jsr     mode
        sta     t
        ldy     #0
@name:  lda     tbl,x
        jsr     CM_PUTC
        inx
        iny
        cpy     #6
        bne     @name
        lda     #' '
        jsr     CM_PUTC
        inx                             ; past the mode: the baud rate's code
        lda     tbl,x
        bne     @rate
        lda     #<snone
        ldx     #>snone
        jsr     CM_PUTS
        bra     @mode
@rate:  asl     a
        tax
        lda     BAUDS - 2,x
        sta     UT_N
        lda     BAUDS - 1,x
        sta     UT_N + 1
        stz     UT_N + 2
        lda     #5
        clc
        jsr     UT_NOUT
@mode:  lda     #' '
        jsr     CM_PUTC
        lda     #13                     ; 6, 1, 5, 1, then the letters
        sta     n
        ldy     #0
@bit:   lsr     t
        bcc     @nobit
        lda     LETTERS,y
        cmp     #' '
        beq     @nobit
        jsr     CM_PUTC
        inc     n
@nobit: iny
        cpy     #5
        bne     @bit
        lda     n
        rts

; The physical devices, three a line, each in a field of 21.
names:  lda     #<sphys
        ldx     #>sphys
        jsr     UT_SAY
        lda     #<sletters
        ldx     #>sletters
        jsr     UT_SAY
        stz     dev
@dev:   lda     dev
        cmp     ndev
        beq     @done
        jsr     putdev
        sta     n
        inc     dev
        lda     dev
        cmp     ndev
        beq     @eol
        cmp     #3
        beq     @eol
        cmp     #6
        beq     @eol
        lda     #21
        sec
        sbc     n
        jsr     UT_BLANKS
        bra     @dev
@eol:   jsr     CM_EOL
        bra     @dev
@done:  rts

; The assignments: every vector's line.
values: lda     #<sassign
        ldx     #>sassign
        jsr     UT_SAY
        lda     #$1F
        sta     lmask
; The lines of the vectors in lmask: "CONOUT: = CRT TTY" or "Null Device".
vlines: stz     vec
        lda     lmask
        sta     vbits
@vec:   lsr     vbits
        bcc     @next
        lda     vec                     ; its name, in 7
        asl     a
        asl     a
        asl     a
        tax
        ldy     #7
@name:  lda     VNAMES,x
        jsr     CM_PUTC
        inx
        dey
        bne     @name
        lda     #<seq
        ldx     #>seq
        jsr     CM_PUTS
        lda     vec
        asl     a
        adc     #SCB_VECS
        jsr     UT_SCBGET
        stx     t                       ; the high byte: devices 0-7
        lda     t
        bne     @devs
        lda     #<snull
        ldx     #>snull
        jsr     CM_PUTS
        bra     @eol
@devs:  stz     dev
        stz     n                       ; 1: one written
@dev:   lda     dev
        cmp     ndev
        beq     @eol
        tax
        lda     BITS,x
        and     t
        beq     @dnext
        lda     n
        beq     :+
        lda     #' '
        jsr     CM_PUTC
:       inc     n
        lda     dev
        jsr     putname
@dnext: inc     dev
        bra     @dev
@eol:   jsr     CM_EOL
@next:  inc     vec
        lda     vec
        cmp     #5
        bne     @vec
        rts

; Device A's name, its trailing blanks dropped.
putname: asl    a
        asl     a
        asl     a
        sta     pbase
        clc
        adc     #6
        sta     pend
@trim:  ldx     pend
        cpx     pbase
        beq     @done
        lda     tbl - 1,x
        cmp     #' '
        bne     @put
        dec     pend
        bra     @trim
@put:   ldx     pbase
@char:  lda     tbl,x
        jsr     CM_PUTC                 ; keeps X
        inx
        cpx     pend
        bne     @char
@done:  rts

        .include "cmd.inc"
        .include "util.inc"

        .rodata
KEYS:   .byte   5, "NAMES", K_NAMES, 6, "VALUES", K_VALUES, 4, "NULL", K_NULL, 0
; The logical names and their vectors (bit 0 CONIN: to bit 4 LST:)
LNAMES: .byte   5, "CONIN", $01, 6, "CONOUT", $02, 5, "AUXIN", $04, 6, "AUXOUT", $08
        .byte   3, "LST", $10, 3, "CON", $03, 7, "CONSOLE", $03, 8, "KEYBOARD", $01
        .byte   3, "AUX", $0C, 9, "AUXILIARY", $0C, 7, "PRINTER", $10, 0
VNAMES: .byte   "CONIN:  CONOUT: AUXIN:  AUXOUT: LST:    "
PTAB:   .byte   "XON", 0, "NOXON", 0, 0
CTAB:   .byte   "PAGE", 0, "COLUMNS", 0, "LINES", 0, 0
BITS:   .byte   $80, $40, $20, $10, $08, $04, $02, $01
; CP/M 3's baud rates by their codes, 1-15 (134 for 134.5)
BAUDS:  .word   50, 75, 110, 134, 150, 300, 600, 1200, 1800, 2400, 3600, 4800, 7200, 9600, 19200
; The mode's bits 0-4: input, output, soft baud (not shown), serial, XON
LETTERS: .byte  "IO SX"
sask:   .byte   "Enter new assignment or hit RETURN: $"
strail: .byte   "End of line expected$"
sbadlog: .byte  "Bad Logical Device Assignment$"
sbadphy: .byte  "Invalid Physical device.$"
sbadopt: .byte  "Invalid option.$"
sbadnum: .byte  "Invalid number.$"
sbaddel: .byte  "Invalid delimiter.$"
snobaud: .byte  "Baud rate cannot be set for this device$"
snoxon: .byte   "Xon-Xoff cannot be set for this device$"
sphys:  .byte   "Physical Devices:$"
sletters: .byte "I=Input,O=Output,S=Serial,X=Xon-Xoff$"
sassign: .byte  "Current Assignments:$"
snone:  .byte   " NONE$"
seq:    .byte   " = $"
snull:  .byte   "Null Device$"
swidth: .byte   "Console width set to $"
scols:  .byte   " columns$"
spage:  .byte   "Console page set to $"
slines: .byte   " lines$"

        .bss
tbl:    .res    NDEVMAX * 8
ndev:   .res    1
wbuf:   .res    9
wlen:   .res    1
t:      .res    2
n:      .res    1
dev:    .res    1
lmask:  .res    1
newv:   .res    1
ndone:  .res    1
vec:    .res    1
vbits:  .res    1
setc:   .res    1
setl:   .res    1
cols:   .res    1
lines:  .res    1
trail:  .res    1
pbase:  .res    1
pend:   .res    1

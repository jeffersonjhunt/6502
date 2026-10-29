; fs.inc's zero page and macros, for the top of a test program (after
; printzp.inc).
;
;   SAY "text"          the text
;   CALLF fn, arg       the BDOS's function fn with A/X = arg
;   TRY "KEY ", fn, arg SAY, CALLF, then RES
;   NAME fcb, "NAME    TYP"  the FCB: drive TDRV + 1 (TDRV $FF: 0, the
;                       current one), the name, the rest 0
;   KEYWAIT "PROMPT?"   the prompt, then a key (function 1), for the test to
;                       look at the disk meanwhile

        .zeropage
FP:     .res    2                       ; SETFCB's FCB
NP:     .res    2                       ; and name
RA:     .res    1                       ; RES's A and X
RX:     .res    1
RN:     .res    2                       ; FILL's and CHECK's record
TDRV:   .res    1                       ; the drive NAME gives an FCB, 0 A:

.macro SAY str
        .local s
        .pushseg
        .rodata
s:      .byte   str, "$"
        .popseg
        lda     #<s
        ldx     #>s
        jsr     PUTS
.endmacro

.macro CALLF fn, arg
        lda     #<(arg)
        ldx     #>(arg)
        ldy     #fn
        jsr     BDOS
.endmacro

.macro TRY key, fn, arg
        SAY     key
        CALLF   fn, arg
        jsr     RES
.endmacro

.macro NAME fcb, str
        .local s
        .assert .strlen(str) = 11, error, "a name is 8 + 3 characters"
        .pushseg
        .rodata
s:      .byte   str
        .popseg
        lda     #<(fcb)
        sta     FP
        lda     #>(fcb)
        sta     FP+1
        lda     #<s
        sta     NP
        lda     #>s
        sta     NP+1
        jsr     SETFCB
.endmacro

.macro KEYWAIT str
        SAY     str
        ldy     #C_READ
        jsr     BDOS
        jsr     CRLF
.endmacro


; TYPE.COM (hw/oodos.md, "The utilities, phase 4b"; the User's Guide's
; TYPE, 5-114): TYPE {filespec {[PAGE]|[NO PAGE]}}.
;
;   one file    the built-in's: its bytes by function 2 to the first ^Z or
;               its end, paged by LF (the SCB's page mode, or [PAGE] on
;               and [NOPAGE] off), a password after the name; No File
;   a wildcard  each matching file of the current user, in the directory's
;               order (the names read first): its name on a line of its own
;               (M:ONE.TXT), then its bytes
;   none        "Enter filename: " and a line read, taken as the tail; an
;               empty line, or one with no file specification, ends it
;
; Errors: "Try 'PAGE' or 'NO PAGE'" for any other option (a value, a
; second list, one not closed too: ours), "Invalid file specification."
; for one 152 refuses or a second one (ours), the BDOS's said (error mode
; $FE). The return code: $FF00 after an error or No File, $FFFE for a ^C.

        .setcpu "65C02"
        .include "oodos.inc"

        .define CMD_DATA "RODATA"
        .define CMD_BSS "BSS"
CMD_BREAK = UT_BREAK

CTLZ    = $1A
LF      = $0A

        OODOS_HEADER

        .code
start:  jsr     UT_INIT
        stz     pmode
        jsr     opts
        jsr     spec
        bcc     @have
        lda     #<sask                  ; none: asked for
        ldx     #>sask
        jsr     UT_READ
        beq     @done
        jsr     opts
        jsr     spec
        bcs     @done
@have:  lda     #<(fcb + 1)
        ldx     #>(fcb + 1)
        jsr     UT_ISWILD
        bcs     wild
        jsr     page
        lda     #<fcb
        ldx     #>fcb
        jsr     typeone
        bcc     @done
        cmp     #0
        bne     @done                   ; said
        jsr     nofile
@done:  jmp     UT_EXIT

; A wildcard: the names first, then each one's line and bytes.
wild:   lda     #<fcb
        ldx     #>fcb
        jsr     UT_NAMES
        bcc     @got
        cmp     #1
        bne     @fail
        lda     #<smem
        ldx     #>smem
        jmp     failsay
@fail:  jsr     UT_SAID
        jmp     UT_EXIT
@got:   cmp     #0
        bne     @some
        cpx     #0
        bne     @some
        jsr     nofile
        jmp     UT_EXIT
@some:  jsr     page
@next:  jsr     UT_NNEXT
        bcc     :+
        jmp     UT_EXIT
:       sta     @src + 1                ; wfcb: the drive and the name
        stx     @src + 2
        lda     fcb
        sta     wfcb
        ldy     #10
@src:   lda     $FFFF,y
        sta     wfcb + 1,y
        dey
        bpl     @src
        ldy     #7                      ; the password given, for each
:       lda     fcb + 16,y
        sta     wfcb + 16,y
        dey
        bpl     :-
        lda     fcb                     ; its line: M:ONE.TXT
        jsr     UT_FDRIVE
        jsr     UT_PUTDRV
        lda     #<(wfcb + 1)
        ldx     #>(wfcb + 1)
        jsr     CM_FNAME
        jsr     CM_NL
        lda     #<wfcb
        ldx     #>wfcb
        jsr     typeone
        bcc     @next
        cmp     #0
        bne     @next                   ; said: on to the next
        jsr     nofile
        bra     @next

; The file of the FCB at A/X (the drive and name, its password at 16-23)
; to the console. Carry clear: done; carry set and A 0: not there (nothing
; said), $FF: an error the BDOS said (UT_SAID's).
typeone:
        sta     @src + 1
        stx     @src + 2
        clc                             ; its password, where the BDOS looks
        adc     #16
        tay
        txa
        adc     #0
        tax
        tya
        jsr     UT_PWSET
        ldy     #0                      ; ofcb: the drive and name, the rest 0
@src:   lda     $FFFF,y
        sta     ofcb,y
        iny
        cpy     #12
        bne     @src
        lda     #0
:       sta     ofcb,y
        iny
        cpy     #36
        bne     :-
        lda     #<ofcb
        ldx     #>ofcb
        ldy     #F_OPEN
        jsr     BDOS
        cmp     #$FF
        bne     @open
        cpx     #0
        bne     @said
        lda     #0                      ; not there
        sec
        rts
@said:  jsr     UT_SAID
        lda     #$FF
        sec
        rts
@open:  lda     #LF                     ; nothing written yet
        sta     last
@record:                                ; (the DMA address UT_REC, UT_PWSET's)
        lda     #<ofcb
        ldx     #>ofcb
        ldy     #F_READ
        jsr     BDOS
        cmp     #0
        beq     @read
        cmp     #1                      ; the end
        beq     @end
        bra     @said
@read:  ldx     #0
@char:  lda     UT_REC,x
        cmp     #CTLZ
        beq     @end
        phx
        pha
        jsr     CM_BRK
        pla
        sta     last
        jsr     CM_PUTC
        lda     last
        cmp     #LF
        bne     :+
        jsr     CM_LINE
:       plx
        inx
        bpl     @char                   ; 128 a record
        bra     @record
@end:   lda     last                    ; not ended by an LF: a line owed, so
        cmp     #LF                     ; a name after it is on a line of its own
        beq     :+
        jsr     CM_EOL
:       clc
        rts
@dst    = @src                          ; (one pointer is enough)

; Paging: the SCB's, or the option's.
page:   jsr     CM_PAGE
        lda     pmode
        beq     @done
        cmp     #2
        beq     @off
        lda     #<CM_PBLEN              ; [PAGE]: the page length less 1 (0, 1: none)
        ldx     #>CM_PBLEN
        ldy     #S_SCB
        jsr     BDOS
        cmp     #2
        bcc     @off
        dec     a
        sta     CM_PLEN
        rts
@off:   stz     CM_PLEN
@done:  rts

; The tail's option list: PAGE (pmode 1) or NOPAGE (2), the last given.
opts:   jsr     UT_OPTS
        beq     @done
        cmp     #UT_LONE
        bne     @bad
@next:  lda     #<OPTAB
        ldx     #>OPTAB
        jsr     UT_OPT
        bcs     @end
        ldy     UT_HASV
        bne     @bad
        inc     a
        sta     pmode
        bra     @next
@end:   cmp     #UT_OEND
        bne     @bad
@done:  rts
@bad:   lda     #<stry
        ldx     #>stry
        ; falls through
; The message at A/X, the return code $FF00, the end.
failsay:
        jsr     UT_SAY
        jsr     UT_FAIL
        jmp     UT_EXIT

; The tail's file specification to fcb: carry clear; carry set: none.
; One 152 refuses, or a second: the error, the end.
spec:   lda     #<fcb
        ldx     #>fcb
        jsr     UT_SPEC
        bcc     @one
        cmp     #0
        bne     @bad
        sec
        rts
@one:   lda     #<wfcb
        ldx     #>wfcb
        jsr     UT_SPEC
        bcc     @bad                    ; a second
        cmp     #0
        bne     @bad
        clc
        rts
@bad:   lda     #<sinv
        ldx     #>sinv
        bra     failsay

nofile: jsr     UT_FAIL
        lda     #<snofile
        ldx     #>snofile
        jmp     UT_SAY

        .include "cmd.inc"
        .include "util.inc"

        .rodata
OPTAB:  .byte   "PAGE", 0, "NOPAGE", 0, 0
sask:   .byte   "Enter filename: $"
stry:   .byte   "Try 'PAGE' or 'NO PAGE'$"
sinv:   .byte   "Invalid file specification.$"
snofile: .byte  "No File$"
smem:   .byte   "Not Enough Memory$"

        .bss
pmode:  .res    1                       ; 0 the SCB's, 1 [PAGE], 2 [NOPAGE]
last:   .res    1                       ; the last byte written
fcb:    .res    36                      ; the specification
wfcb:   .res    36                      ; a file of a wildcard's (and a second spec)
ofcb:   .res    36                      ; the file open
